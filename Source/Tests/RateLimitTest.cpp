// FemboyEngine2D - handshake and voice rate limits.
#include "Engine/Audio/VoiceNetworkBridge.hpp"
#include "Engine/Audio/VoiceSystem.hpp"
#include "Engine/Core/CryptoAsymmetric.hpp"
#include "Engine/Network/NetLink.hpp"
#include "Engine/Network/SecureSessionTransport.hpp"
#include "Engine/Network/Session.hpp"
#include "Engine/Network/Transport.hpp"

#include <array>
#include <cstdint>
#include <cstdio>

namespace
{
std::uint32_t GFailures = 0;

#define FBY_CHECK(condition)                                                                                           \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);                                  \
            ++GFailures;                                                                                               \
        }                                                                                                              \
    } while (false)

template <std::size_t N>
[[nodiscard]] std::array<std::byte, N> Seed(std::uint8_t InValue)
{
    std::array<std::byte, N> ScopedSeed{};
    for (std::byte& ScopedByte : ScopedSeed)
    {
        ScopedByte = static_cast<std::byte>(InValue);
    }
    return ScopedSeed;
}

// One tiny but structurally valid voice packet: FVOX magic, version 1, speaker
// 1, a sequence number and 960 samples in a 4-byte payload.
void WriteVoicePacket(std::array<std::byte, 16>& OutPacket, std::uint16_t InSequence) noexcept
{
    const std::uint32_t ScopedMagic = Fby::kVoicePacketMagic;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < 4; ++ScopedIndex)
    {
        OutPacket[ScopedIndex] = static_cast<std::byte>((ScopedMagic >> (ScopedIndex * 8)) & 0xFFu);
    }
    OutPacket[4] = std::byte{1};
    OutPacket[5] = std::byte{0};
    OutPacket[6] = std::byte{1};
    OutPacket[7] = std::byte{0};
    OutPacket[8] = static_cast<std::byte>(InSequence & 0xFFu);
    OutPacket[9] = static_cast<std::byte>((InSequence >> 8) & 0xFFu);
    OutPacket[10] = static_cast<std::byte>(static_cast<std::uint16_t>(Fby::kVoiceFrameSamples) & 0xFFu);
    OutPacket[11] = static_cast<std::byte>((static_cast<std::uint16_t>(Fby::kVoiceFrameSamples) >> 8) & 0xFFu);
    OutPacket[12] = std::byte{0x11};
    OutPacket[13] = std::byte{0x22};
    OutPacket[14] = std::byte{0x33};
    OutPacket[15] = std::byte{0x44};
}
}

int main()
{
    using namespace Fby;
    using namespace Fby::Net;

    std::string ScopedError;
    if (!DatagramSocket::EnsureSocketsInitialized(&ScopedError))
    {
        std::printf("rate_limit_test: sockets unavailable, skipped\n");
        return 0;
    }

    // 1. Handshake flood: one address is cut to its burst and the drops are
    // counted; another address still completes a handshake.
    Crypto::X25519KeyPair ScopedServerKey{};
    Crypto::X25519KeyPair ScopedClientKey{};
    (void)Crypto::X25519KeyPairFromSeed(Seed<32>(0x61), &ScopedServerKey);
    (void)Crypto::X25519KeyPairFromSeed(Seed<32>(0x62), &ScopedClientKey);

    NetTransport ScopedServerTransport;
    FBY_CHECK(ScopedServerTransport.Open(0, &ScopedError));
    NetAddress ScopedServerAddress{};
    ScopedServerAddress.Host = 0x7F000001u;
    ScopedServerAddress.Port = ScopedServerTransport.LocalPort();

    SecureChannelConfig ScopedServerConfig{};
    ScopedServerConfig.ProtocolV2 = false;
    ScopedServerConfig.LocalStatic = ScopedServerKey;
    ScopedServerConfig.Initiator = false;
    SecureSessionTransport ScopedServerLink;
    ScopedServerLink.SetHandshakeRateLimit(4, 1);
    FBY_CHECK(ScopedServerLink.StartServer(ScopedServerTransport, ScopedServerConfig));

    NetTransport ScopedFloodTransport;
    FBY_CHECK(ScopedFloodTransport.Open(0, &ScopedError));
    FBY_CHECK(ScopedFloodTransport.Connect(ScopedServerAddress));
    const std::array<std::byte, 16> ScopedJunk{};
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < 20; ++ScopedIndex)
    {
        (void)ScopedFloodTransport.Send(ScopedServerAddress, ScopedJunk, TransportChannel::Unreliable);
    }
    ScopedServerLink.Poll(nullptr);
    FBY_CHECK(ScopedServerLink.HandshakeRateLimited() == 16);

    NetTransport ScopedGoodTransport;
    FBY_CHECK(ScopedGoodTransport.Open(0, &ScopedError));
    FBY_CHECK(ScopedGoodTransport.Connect(ScopedServerAddress));
    SecureChannelConfig ScopedClientConfig{};
    ScopedClientConfig.ProtocolV2 = false;
    ScopedClientConfig.LocalStatic = ScopedClientKey;
    ScopedClientConfig.ExpectedRemoteStatic = ScopedServerKey.Public;
    ScopedClientConfig.RequireExpectedRemote = true;
    SecureSessionTransport ScopedClientLink;
    FBY_CHECK(ScopedClientLink.StartClient(ScopedGoodTransport, ScopedServerAddress, ScopedClientConfig));
    for (std::uint32_t ScopedStep = 0; ScopedStep < 200 && !ScopedClientLink.ClientEstablished(); ++ScopedStep)
    {
        ScopedServerLink.Poll(nullptr);
        ScopedClientLink.Poll(nullptr);
        ScopedServerTransport.Tick(0.01f);
        ScopedGoodTransport.Tick(0.01f);
    }
    FBY_CHECK(ScopedClientLink.ClientEstablished());

    // Refill restores tokens for the flood address without affecting the count.
    ScopedServerLink.Tick(2.0f);
    const std::uint64_t ScopedDropsBefore = ScopedServerLink.HandshakeRateLimited();
    (void)ScopedFloodTransport.Send(ScopedServerAddress, ScopedJunk, TransportChannel::Unreliable);
    ScopedServerLink.Poll(nullptr);
    FBY_CHECK(ScopedServerLink.HandshakeRateLimited() == ScopedDropsBefore);

    ScopedClientLink.Stop();
    ScopedGoodTransport.Close();
    ScopedServerLink.Stop();
    ScopedServerTransport.Close();
    ScopedFloodTransport.Close();

    // 2. Voice flood: a speaker over its burst is dropped before the channel can
    // decode or relay the packet; a tick refills the bucket.
    DspGraph ScopedGraph;
    FBY_CHECK(ScopedGraph.Initialize(48000));
    VoiceSystem ScopedVoice;
    VoiceSystemConfig ScopedVoiceConfig{};
    ScopedVoiceConfig.LocalPlayer = 0;
    ScopedVoiceConfig.Routing = VoiceRouting::Mesh;
    ScopedVoiceConfig.Denoise = VoiceDenoiseTier::None;
    ScopedVoiceConfig.Aec.Enabled = false;

    MemoryNetwork ScopedNetwork;
    MemoryEndpoint ScopedHostEndpoint;
    MemoryEndpoint ScopedPeerEndpoint;
    (void)ScopedHostEndpoint.Open(&ScopedNetwork, 41200);
    (void)ScopedPeerEndpoint.Open(&ScopedNetwork, 41201);
    NetSession ScopedSession;
    VoiceSession ScopedVoiceSession;
    VoiceNetworkBridge ScopedBridge;
    FBY_CHECK(ScopedVoice.Start(ScopedGraph, nullptr, ScopedVoiceConfig, &VoiceNetworkBridge::SendThunk,
                                &ScopedBridge));
    FBY_CHECK(ScopedBridge.StartLoopback(ScopedVoice, ScopedSession, ScopedVoiceSession, 0, 1,
                                         ScopedHostEndpoint.Link(), ScopedPeerEndpoint.Address()));
    ScopedBridge.SetRateLimit(8, 8);

    std::array<std::byte, 16> ScopedPacket{};
    for (std::uint16_t ScopedIndex = 0; ScopedIndex < 50; ++ScopedIndex)
    {
        WriteVoicePacket(ScopedPacket, ScopedIndex);
        VoiceNetworkBridge::ReceiveThunk(&ScopedBridge, ScopedPeerEndpoint.Address(),
                                         std::span<const std::byte>(ScopedPacket));
    }
    FBY_CHECK(ScopedBridge.PacketsRateLimited() == 42);
    FBY_CHECK(ScopedVoice.Channel().Stats().PacketsReceived == 8);

    ScopedBridge.Tick(1.0f);
    WriteVoicePacket(ScopedPacket, 50);
    VoiceNetworkBridge::ReceiveThunk(&ScopedBridge, ScopedPeerEndpoint.Address(),
                                     std::span<const std::byte>(ScopedPacket));
    FBY_CHECK(ScopedVoice.Channel().Stats().PacketsReceived == 9);

    ScopedBridge.Stop();
    ScopedVoice.Stop();
    ScopedGraph.Shutdown();
    ScopedHostEndpoint.Close();
    ScopedPeerEndpoint.Close();

    if (GFailures == 0)
    {
        std::printf("rate_limit_test: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "rate_limit_test: %u failure(s)\n", GFailures);
    return 1;
}
