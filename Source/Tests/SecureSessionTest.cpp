// FemboyEngine2D - encrypted multi-client session transport tests.
#include "Engine/Network/SecureSessionTransport.hpp"

#include <cstdint>
#include <cstdio>
#include <array>
#include <cstring>
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

[[nodiscard]] Fby::Crypto::X25519KeyPair MakeKey(std::uint8_t InSeed)
{
    std::array<std::byte, 32> ScopedSeed{};
    for (std::byte& ScopedByte : ScopedSeed)
    {
        ScopedByte = static_cast<std::byte>(InSeed);
    }
    Fby::Crypto::X25519KeyPair ScopedPair{};
    (void)Fby::Crypto::X25519KeyPairFromSeed(ScopedSeed, &ScopedPair);
    return ScopedPair;
}

struct EventSink
{
    std::uint32_t Count = 0;
    std::uint32_t EventId = 0;
};

void OnEvent(void* InUser, std::uint8_t InFromPlayer, std::uint32_t InEventId,
             std::span<const std::byte> InPayload)
{
    auto* ScopedSink = static_cast<EventSink*>(InUser);
    ScopedSink->Count += 1;
    ScopedSink->EventId = InEventId;
    (void)InFromPlayer;
    (void)InPayload;
}
}

int main()
{
    using namespace Fby;
    using namespace Fby::Net;

    std::string ScopedError;
    if (!DatagramSocket::EnsureSocketsInitialized(&ScopedError))
    {
        std::printf("secure_session_test: sockets unavailable, skipped\n");
        return 0;
    }

    NetTransport ScopedServerTransport;
    NetTransport ScopedClientTransports[2];
    if (!ScopedServerTransport.Open(0, &ScopedError))
    {
        std::printf("secure_session_test: bind failed, skipped\n");
        return 0;
    }
    NetAddress ScopedServerAddress{};
    ScopedServerAddress.Host = 0x7F000001u;
    ScopedServerAddress.Port = ScopedServerTransport.LocalPort();

    const Crypto::X25519KeyPair ScopedServerKey = MakeKey(0x41);
    SecureChannelConfig ScopedServerConfig{};
    ScopedServerConfig.LocalStatic = ScopedServerKey;
    ScopedServerConfig.Initiator = false;

    SecureSessionTransport ScopedServerLink;
    FBY_CHECK(ScopedServerLink.StartServer(ScopedServerTransport, ScopedServerConfig));

    SecureSessionTransport ScopedClientLinks[2];
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < 2; ++ScopedIndex)
    {
        FBY_CHECK(ScopedClientTransports[ScopedIndex].Open(0, &ScopedError));
        FBY_CHECK(ScopedClientTransports[ScopedIndex].Connect(ScopedServerAddress));
        SecureChannelConfig ScopedClientConfig{};
        ScopedClientConfig.LocalStatic = MakeKey(static_cast<std::uint8_t>(0x50 + ScopedIndex));
        ScopedClientConfig.ExpectedRemoteStatic = ScopedServerKey.Public;
        ScopedClientConfig.RequireExpectedRemote = true;
        FBY_CHECK(ScopedClientLinks[ScopedIndex].StartClient(ScopedClientTransports[ScopedIndex],
                                                             ScopedServerAddress, ScopedClientConfig));
    }

    for (std::uint32_t ScopedStep = 0; ScopedStep < 400; ++ScopedStep)
    {
        ScopedServerLink.Poll(nullptr);
        ScopedServerTransport.Tick(0.01f);
        bool ScopedAllReady = true;
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < 2; ++ScopedIndex)
        {
            ScopedClientLinks[ScopedIndex].Poll(nullptr);
            ScopedClientTransports[ScopedIndex].Tick(0.01f);
            ScopedAllReady = ScopedAllReady && ScopedClientLinks[ScopedIndex].ClientEstablished();
        }
        if (ScopedAllReady)
        {
            break;
        }
    }
    FBY_CHECK(ScopedClientLinks[0].ClientEstablished());
    FBY_CHECK(ScopedClientLinks[1].ClientEstablished());

    NetSession ScopedServerSession;
    NetSessionConfig ScopedServerSessionConfig{};
    ScopedServerSessionConfig.Mode = NetSessionMode::Dedicated;
    ScopedServerSessionConfig.Headless = true;
    std::memcpy(ScopedServerSessionConfig.LocalName, "server", 7);
    ScopedServerSession.SetSendHook(ScopedServerLink.SendHook(), &ScopedServerLink);
    FBY_CHECK(ScopedServerSession.Start(NetLink{}, ScopedServerSessionConfig));

    EventSink ScopedSink{};
    NetSessionCallbacks ScopedCallbacks{};
    ScopedCallbacks.User = &ScopedSink;
    ScopedCallbacks.OnEvent = &OnEvent;
    ScopedServerSession.SetCallbacks(ScopedCallbacks);

    NetSession ScopedClientSessions[2];
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < 2; ++ScopedIndex)
    {
        NetSessionConfig ScopedConfig{};
        ScopedConfig.Mode = NetSessionMode::Auto;
        ScopedConfig.Remote = ScopedServerAddress;
        std::memcpy(ScopedConfig.LocalName, ScopedIndex == 0 ? "alpha" : "bravo",
                    ScopedIndex == 0 ? 6 : 6);
        ScopedClientSessions[ScopedIndex].SetSendHook(ScopedClientLinks[ScopedIndex].SendHook(),
                                                      &ScopedClientLinks[ScopedIndex]);
        FBY_CHECK(ScopedClientSessions[ScopedIndex].Start(NetLink{}, ScopedConfig));
    }

    for (std::uint32_t ScopedStep = 0; ScopedStep < 600; ++ScopedStep)
    {
        ScopedServerTransport.Tick(0.005f);
        ScopedServerLink.Poll(&ScopedServerSession);
        ScopedServerSession.Advance(0.01f);

        for (std::uint32_t ScopedIndex = 0; ScopedIndex < 2; ++ScopedIndex)
        {
            ScopedClientTransports[ScopedIndex].Tick(0.005f);
            ScopedClientLinks[ScopedIndex].Poll(&ScopedClientSessions[ScopedIndex]);
            ScopedClientSessions[ScopedIndex].Advance(0.01f);
        }
        if (ScopedServerSession.PlayerCount() >= 2)
        {
            break;
        }
    }

    FBY_CHECK(ScopedServerSession.PlayerCount() >= 2);
    FBY_CHECK(ScopedServerLink.PeerCount() >= 2);

    for (std::uint32_t ScopedIndex = 0; ScopedIndex < 2; ++ScopedIndex)
    {
        const NetPlayerSlot& ScopedSlot = ScopedClientSessions[ScopedIndex].Slot(0u);
        FBY_CHECK(ScopedSlot.Used);
        FBY_CHECK(ScopedSlot.Address.Port != 0);
        bool ScopedPeerAddress = false;
        for (std::uint32_t ScopedOther = 0; ScopedOther < kSessionMaxPlayers; ++ScopedOther)
        {
            const NetPlayerSlot& ScopedOtherSlot = ScopedClientSessions[ScopedIndex].Slot(ScopedOther);
            if (ScopedOtherSlot.Used &&
                ScopedOtherSlot.Id != ScopedClientSessions[ScopedIndex].LocalPlayer())
            {
                ScopedPeerAddress = ScopedPeerAddress || ScopedOtherSlot.Address.Port != 0;
            }
        }
        FBY_CHECK(ScopedPeerAddress);
    }

    std::uint8_t ScopedPayload[4] = {1, 2, 3, 4};
    FBY_CHECK(ScopedClientSessions[0].SendEvent(77, std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(ScopedPayload), sizeof(ScopedPayload))));
    for (std::uint32_t ScopedStep = 0; ScopedStep < 120 && ScopedSink.Count == 0; ++ScopedStep)
    {
        ScopedServerLink.Poll(&ScopedServerSession);
        ScopedServerSession.Advance(0.01f);
        ScopedServerTransport.Tick(0.005f);
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < 2; ++ScopedIndex)
        {
            ScopedClientTransports[ScopedIndex].Tick(0.005f);
            ScopedClientLinks[ScopedIndex].Poll(&ScopedClientSessions[ScopedIndex]);
            ScopedClientSessions[ScopedIndex].Advance(0.01f);
        }
    }
    FBY_CHECK(ScopedSink.Count >= 1);
    FBY_CHECK(ScopedSink.EventId == 77);

    for (NetSession& ScopedSession : ScopedClientSessions)
    {
        ScopedSession.Stop();
    }
    ScopedServerSession.Stop();
    for (SecureSessionTransport& ScopedLink : ScopedClientLinks)
    {
        ScopedLink.Stop();
    }
    ScopedServerLink.Stop();
    for (NetTransport& ScopedTransport : ScopedClientTransports)
    {
        ScopedTransport.Close();
    }
    ScopedServerTransport.Close();

    if (GFailures == 0)
    {
        std::printf("secure_session_test: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "secure_session_test: %u failure(s)\n", GFailures);
    return 1;
}
