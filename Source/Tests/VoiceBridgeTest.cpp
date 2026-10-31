// FemboyEngine2D - encrypted relay voice end-to-end test.
#include "Engine/Audio/VoiceNetworkBridge.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

namespace
{
std::uint32_t GFailures = 0;

#define FBY_CHECK(condition)                                                                                           \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                                  \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);                                  \
            ++GFailures;                                                                                              \
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

[[nodiscard]] std::vector<float> Sine(std::uint32_t InFrames)
{
    std::vector<float> ScopedSamples(InFrames);
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < InFrames; ++ScopedIndex)
    {
        ScopedSamples[ScopedIndex] = 0.2f * std::sin(2.0f * 3.14159265f * 440.0f *
                                                     static_cast<float>(ScopedIndex) / 48000.0f);
    }
    return ScopedSamples;
}

[[nodiscard]] float RingRms(Fby::FloatRing& InRing)
{
    std::vector<float> ScopedSamples(InRing.AvailableRead());
    const std::uint32_t ScopedRead =
        InRing.Read(ScopedSamples.data(), static_cast<std::uint32_t>(ScopedSamples.size()));
    ScopedSamples.resize(ScopedRead);
    double ScopedSum = 0.0;
    for (const float ScopedSample : ScopedSamples)
    {
        ScopedSum += static_cast<double>(ScopedSample) * ScopedSample;
    }
    return ScopedSamples.empty() ? 0.0f : static_cast<float>(std::sqrt(ScopedSum / ScopedSamples.size()));
}

struct Client
{
    Fby::Net::NetTransport Transport;
    Fby::Net::SecureSessionTransport Link;
    Fby::Net::NetSession Session;
    Fby::DspGraph Graph;
    Fby::VoiceSession VoiceSession;
    Fby::VoiceSystem Voice;
    Fby::VoiceNetworkBridge Bridge;
    std::uint8_t LocalPlayer = 0;
};
}

int main()
{
    using namespace Fby;
    using namespace Fby::Net;

    std::string ScopedError;
    if (!DatagramSocket::EnsureSocketsInitialized(&ScopedError))
    {
        std::printf("voice_bridge_test: sockets unavailable, skipped\n");
        return 0;
    }

    NetTransport ScopedServerTransport;
    if (!ScopedServerTransport.Open(0, &ScopedError))
    {
        std::printf("voice_bridge_test: bind failed, skipped\n");
        return 0;
    }
    NetAddress ScopedServerAddress{};
    ScopedServerAddress.Host = 0x7F000001u;
    ScopedServerAddress.Port = ScopedServerTransport.LocalPort();

    Crypto::X25519KeyPair ScopedServerKey{};
    (void)Crypto::X25519KeyPairFromSeed(Seed<32>(0x21), &ScopedServerKey);

    SecureChannelConfig ScopedServerConfig{};
    ScopedServerConfig.LocalStatic = ScopedServerKey;
    ScopedServerConfig.Initiator = false;
    SecureSessionTransport ScopedServerLink;
    FBY_CHECK(ScopedServerLink.StartServer(ScopedServerTransport, ScopedServerConfig));

    std::unique_ptr<Client> ScopedClients[2];
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < 2; ++ScopedIndex)
    {
        auto ScopedClient = std::make_unique<Client>();
        FBY_CHECK(ScopedClient->Transport.Open(0, &ScopedError));
        FBY_CHECK(ScopedClient->Transport.Connect(ScopedServerAddress));
        Crypto::X25519KeyPair ScopedKey{};
        (void)Crypto::X25519KeyPairFromSeed(Seed<32>(static_cast<std::uint8_t>(0x30 + ScopedIndex)), &ScopedKey);
        SecureChannelConfig ScopedConfig{};
        ScopedConfig.LocalStatic = ScopedKey;
        ScopedConfig.ExpectedRemoteStatic = ScopedServerKey.Public;
        ScopedConfig.RequireExpectedRemote = true;
        FBY_CHECK(ScopedClient->Link.StartClient(ScopedClient->Transport, ScopedServerAddress, ScopedConfig));
        ScopedClients[ScopedIndex] = std::move(ScopedClient);
    }

    for (std::uint32_t ScopedStep = 0; ScopedStep < 400; ++ScopedStep)
    {
        ScopedServerLink.Poll(nullptr);
        ScopedServerTransport.Tick(0.01f);
        bool ScopedReady = true;
        for (const auto& ScopedClient : ScopedClients)
        {
            ScopedClient->Link.Poll(nullptr);
            ScopedClient->Transport.Tick(0.01f);
            ScopedReady = ScopedReady && ScopedClient->Link.ClientEstablished();
        }
        if (ScopedReady)
        {
            break;
        }
    }

    NetSession ScopedServerSession;
    NetSessionConfig ScopedServerSessionConfig{};
    ScopedServerSessionConfig.Mode = NetSessionMode::Dedicated;
    ScopedServerSessionConfig.Headless = true;
    std::memcpy(ScopedServerSessionConfig.LocalName, "server", 7);
    ScopedServerSession.SetSendHook(ScopedServerLink.SendHook(), &ScopedServerLink);
    FBY_CHECK(ScopedServerSession.Start(NetLink{}, ScopedServerSessionConfig));

    for (std::uint32_t ScopedIndex = 0; ScopedIndex < 2; ++ScopedIndex)
    {
        NetSessionConfig ScopedConfig{};
        ScopedConfig.Mode = NetSessionMode::Auto;
        ScopedConfig.Remote = ScopedServerAddress;
        std::memcpy(ScopedConfig.LocalName, ScopedIndex == 0 ? "alpha" : "bravo", 6);
        ScopedClients[ScopedIndex]->Session.SetSendHook(ScopedClients[ScopedIndex]->Link.SendHook(),
                                                       &ScopedClients[ScopedIndex]->Link);
        FBY_CHECK(ScopedClients[ScopedIndex]->Session.Start(NetLink{}, ScopedConfig));
    }

    for (std::uint32_t ScopedStep = 0; ScopedStep < 600 && ScopedServerSession.PlayerCount() < 2; ++ScopedStep)
    {
        ScopedServerLink.Poll(&ScopedServerSession);
        ScopedServerSession.Advance(0.01f);
        ScopedServerTransport.Tick(0.005f);
        for (auto& ScopedClient : ScopedClients)
        {
            ScopedClient->Link.Poll(&ScopedClient->Session);
            ScopedClient->Session.Advance(0.01f);
            ScopedClient->Transport.Tick(0.005f);
        }
    }
    FBY_CHECK(ScopedServerSession.PlayerCount() >= 2);

    FBY_CHECK(ScopedClients[0]->Graph.Initialize(48000));
    FBY_CHECK(ScopedClients[1]->Graph.Initialize(48000));
    DspGraph ScopedServerGraph;
    FBY_CHECK(ScopedServerGraph.Initialize(48000));

    VoiceSystemConfig ScopedServerVoiceConfig{};
    ScopedServerVoiceConfig.LocalPlayer = 0xFF;
    ScopedServerVoiceConfig.Routing = VoiceRouting::Relay;
    ScopedServerVoiceConfig.IsRelayServer = true;
    ScopedServerVoiceConfig.Aec.Enabled = false;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < 2; ++ScopedIndex)
    {
        Client& ScopedClient = *ScopedClients[ScopedIndex];
        VoiceSystemConfig ScopedConfig{};
        ScopedConfig.LocalPlayer = ScopedClient.Session.LocalPlayer();
        ScopedConfig.Routing = VoiceRouting::Relay;
        ScopedConfig.RelayPlayer = 0xFF;
        ScopedConfig.Aec.Enabled = false;
        if (ScopedIndex == 0)
        {
            FBY_CHECK(ScopedClient.Voice.Start(ScopedClient.Graph, nullptr, ScopedConfig,
                                              &VoiceNetworkBridge::SendThunk, nullptr));
        }
        else
        {
            FBY_CHECK(ScopedClient.Voice.Start(ScopedClient.Graph, nullptr, ScopedConfig,
                                              &VoiceNetworkBridge::SendThunk, nullptr));
        }
        FBY_CHECK(ScopedClient.Bridge.Start(ScopedClient.Voice, ScopedClient.Link, ScopedClient.Session,
                                            ScopedClient.VoiceSession, ScopedConfig.LocalPlayer));
    }

    VoiceSystem ScopedServerVoice;
    FBY_CHECK(ScopedServerVoice.Start(ScopedServerGraph, nullptr, ScopedServerVoiceConfig,
                                      &VoiceNetworkBridge::SendThunk, nullptr));
    VoiceSession ScopedServerVoiceSession;
    VoiceNetworkBridge ScopedServerBridge;
    FBY_CHECK(ScopedServerBridge.Start(ScopedServerVoice, ScopedServerLink, ScopedServerSession,
                                       ScopedServerVoiceSession, 0xFF));

    // Client 0 speaks; the relay server forwards to client 1 over secure links.
    const std::vector<float> ScopedSpeech = Sine(kVoiceFrameSamples);
    ScopedClients[0]->Voice.PushCaptured(ScopedSpeech);
    ScopedClients[0]->Bridge.Tick(0.02f);
    for (std::uint32_t ScopedStep = 0; ScopedStep < 40; ++ScopedStep)
    {
        ScopedServerLink.Poll(&ScopedServerSession);
        ScopedServerSession.Advance(0.005f);
        ScopedServerTransport.Tick(0.005f);
        ScopedServerBridge.Tick(0.005f);
        for (auto& ScopedClient : ScopedClients)
        {
            ScopedClient->Link.Poll(&ScopedClient->Session);
            ScopedClient->Session.Advance(0.005f);
            ScopedClient->Transport.Tick(0.005f);
            ScopedClient->Bridge.Tick(0.02f);
        }
        if (ScopedClients[1]->Voice.Channel().Stats().PacketsReceived > 0)
        {
            break;
        }
    }

    FBY_CHECK(ScopedServerVoice.Channel().Stats().PacketsForwarded >= 1);
    FBY_CHECK(ScopedClients[1]->Voice.Channel().Stats().PacketsReceived >= 1);
    FloatRing* ScopedRing = ScopedClients[1]->Voice.Channel().ParticipantRing(0);
    FBY_CHECK(ScopedRing != nullptr);
    if (ScopedRing != nullptr)
    {
        FBY_CHECK(RingRms(*ScopedRing) > 0.001f);
    }

    ScopedServerBridge.Stop();
    ScopedServerVoice.Stop();
    ScopedServerGraph.Shutdown();
    for (auto& ScopedClient : ScopedClients)
    {
        ScopedClient->Bridge.Stop();
        ScopedClient->Voice.Stop();
        ScopedClient->Graph.Shutdown();
        ScopedClient->Session.Stop();
        ScopedClient->Link.Stop();
        ScopedClient->Transport.Close();
    }
    ScopedServerSession.Stop();
    ScopedServerLink.Stop();
    ScopedServerTransport.Close();

    if (GFailures == 0)
    {
        std::printf("voice_bridge_test: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "voice_bridge_test: %u failure(s)\n", GFailures);
    return 1;
}
