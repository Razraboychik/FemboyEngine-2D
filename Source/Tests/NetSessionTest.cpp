// FemboyEngine2D - role resolution, slots, time sync, ticks and events tests.
#include "Engine/Network/NetLink.hpp"
#include "Engine/Network/Session.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <string_view>
#include <vector>

namespace
{
std::uint32_t GFailures = 0;

#define FBY_CHECK(condition)                                                                                           \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);                                  \
            ++GFailures;                                                                                              \
        }                                                                                                              \
    } while (false)

struct FakeClock
{
    std::uint64_t Ms = 0;
};

std::uint64_t ReadClock(void* InUser)
{
    return static_cast<FakeClock*>(InUser)->Ms;
}

struct Recorder
{
    std::uint32_t JoinedCount = 0;
    std::uint32_t SlotChanges = 0;
    std::uint32_t TimeSyncs = 0;
    std::uint32_t Events = 0;
    std::uint32_t LastEventId = 0;
    std::uint8_t LastFromPlayer = 0xFF;
    std::vector<std::byte> LastPayload;
    std::uint32_t Disconnects = 0;
};

void OnJoined(void* InUser, std::uint8_t InPlayerId, std::uint64_t InServerTick)
{
    auto* ScopedRecorder = static_cast<Recorder*>(InUser);
    (void)InPlayerId;
    (void)InServerTick;
    ++ScopedRecorder->JoinedCount;
}

void OnSlotChanged(void* InUser, std::uint8_t InPlayerId, bool InPresent)
{
    auto* ScopedRecorder = static_cast<Recorder*>(InUser);
    (void)InPlayerId;
    (void)InPresent;
    ++ScopedRecorder->SlotChanges;
}

void OnEvent(void* InUser, std::uint8_t InFromPlayer, std::uint32_t InEventId,
             std::span<const std::byte> InPayload)
{
    auto* ScopedRecorder = static_cast<Recorder*>(InUser);
    ++ScopedRecorder->Events;
    ScopedRecorder->LastEventId = InEventId;
    ScopedRecorder->LastFromPlayer = InFromPlayer;
    ScopedRecorder->LastPayload.assign(InPayload.begin(), InPayload.end());
}

void OnTimeSync(void* InUser, std::uint64_t InServerTick, std::uint32_t InRttMs)
{
    auto* ScopedRecorder = static_cast<Recorder*>(InUser);
    (void)InServerTick;
    (void)InRttMs;
    ++ScopedRecorder->TimeSyncs;
}

void OnDisconnected(void* InUser, Fby::Net::NetDisconnectReason InReason)
{
    auto* ScopedRecorder = static_cast<Recorder*>(InUser);
    (void)InReason;
    ++ScopedRecorder->Disconnects;
}

[[nodiscard]] Fby::Net::NetSessionConfig MakeAuthorityConfig(bool InHosted)
{
    Fby::Net::NetSessionConfig ScopedConfig{};
    ScopedConfig.Mode = InHosted ? Fby::Net::NetSessionMode::Hosted : Fby::Net::NetSessionMode::Dedicated;
    ScopedConfig.Headless = !InHosted;
    std::strncpy(ScopedConfig.LocalName, "Host", sizeof(ScopedConfig.LocalName) - 1);
    ScopedConfig.TimePingIntervalMs = 100;
    ScopedConfig.RosterIntervalMs = 100;
    ScopedConfig.ServerSyncIntervalMs = 100;
    ScopedConfig.TimeoutMs = 5000;
    ScopedConfig.EventRetransmitMs = 100;
    return ScopedConfig;
}

[[nodiscard]] Fby::Net::NetSessionConfig MakeClientConfig(const Fby::Net::NetAddress& InRemote,
                                                          const char* InName, Fby::Net::NetTickMode InTickMode)
{
    Fby::Net::NetSessionConfig ScopedConfig{};
    ScopedConfig.Mode = Fby::Net::NetSessionMode::Auto;
    ScopedConfig.TickMode = InTickMode;
    ScopedConfig.Remote = InRemote;
    std::strncpy(ScopedConfig.LocalName, InName, sizeof(ScopedConfig.LocalName) - 1);
    ScopedConfig.TimePingIntervalMs = 100;
    ScopedConfig.RosterIntervalMs = 100;
    ScopedConfig.ServerSyncIntervalMs = 100;
    ScopedConfig.TimeoutMs = 5000;
    ScopedConfig.PredictionLeadTicks = 2;
    ScopedConfig.PredictionMaxDriftTicks = 4;
    ScopedConfig.EventRetransmitMs = 100;
    ScopedConfig.EventMaxAttempts = 8;
    return ScopedConfig;
}

void TestRoleResolution()
{
    using namespace Fby::Net;

    NetSessionConfig ScopedConfig{};

    ScopedConfig.Mode = NetSessionMode::Offline;
    FBY_CHECK(ResolveSessionRole(ScopedConfig) == NetRole::Offline);

    ScopedConfig.Mode = NetSessionMode::Dedicated;
    FBY_CHECK(ResolveSessionRole(ScopedConfig) == NetRole::Authority);

    ScopedConfig.Mode = NetSessionMode::Hosted;
    FBY_CHECK(ResolveSessionRole(ScopedConfig) == NetRole::Authority);

    ScopedConfig.Mode = NetSessionMode::PeerToPeer;
    FBY_CHECK(ResolveSessionRole(ScopedConfig) == NetRole::Peer);

    ScopedConfig.Mode = NetSessionMode::Hybrid;
    ScopedConfig.HasAssignedServer = true;
    FBY_CHECK(ResolveSessionRole(ScopedConfig) == NetRole::Client);

    ScopedConfig.HasAssignedServer = false;
    ScopedConfig.LobbyHost = true;
    FBY_CHECK(ResolveSessionRole(ScopedConfig) == NetRole::Authority);

    ScopedConfig.LobbyHost = false;
    FBY_CHECK(ResolveSessionRole(ScopedConfig) == NetRole::Offline);

    ScopedConfig.Remote.Host = 0x7F000001u;
    ScopedConfig.Remote.Port = 9000;
    FBY_CHECK(ResolveSessionRole(ScopedConfig) == NetRole::Client);

    ScopedConfig = NetSessionConfig{};
    ScopedConfig.Mode = NetSessionMode::Auto;
    ScopedConfig.Headless = true;
    FBY_CHECK(ResolveSessionRole(ScopedConfig) == NetRole::Authority);

    ScopedConfig.Headless = false;
    ScopedConfig.HostRequested = true;
    ScopedConfig.Remote.Port = 9000;
    FBY_CHECK(ResolveSessionRole(ScopedConfig) == NetRole::Authority);

    ScopedConfig.HostRequested = false;
    ScopedConfig.Remote = NetAddress{};
    FBY_CHECK(ResolveSessionRole(ScopedConfig) == NetRole::Offline);

    ScopedConfig.Remote.Port = 9000;
    ScopedConfig.HasAssignedServer = true;
    FBY_CHECK(ResolveSessionRole(ScopedConfig) == NetRole::Client);
}

struct Fixture
{
    Fby::Net::MemoryNetwork Network;
    Fby::Net::MemoryEndpoint ServerEndpoint;
    Fby::Net::MemoryEndpoint ClientOneEndpoint;
    Fby::Net::MemoryEndpoint ClientTwoEndpoint;
    Fby::Net::NetSession Server;
    Fby::Net::NetSession ClientOne;
    Fby::Net::NetSession ClientTwo;
    Recorder ServerRecorder;
    Recorder ClientOneRecorder;
    Recorder ClientTwoRecorder;
    FakeClock Clock;
};

[[nodiscard]] bool OpenFixture(Fixture& InFixture, bool InHosted, Fby::Net::NetTickMode InTickMode,
                               const char* InFirstName, const char* InSecondName)
{
    using namespace Fby::Net;

    if (!InFixture.ServerEndpoint.Open(&InFixture.Network, 0) ||
        !InFixture.ClientOneEndpoint.Open(&InFixture.Network, 0) ||
        !InFixture.ClientTwoEndpoint.Open(&InFixture.Network, 0))
    {
        return false;
    }

    NetSessionConfig ScopedServerConfig = MakeAuthorityConfig(InHosted);
    const NetSessionConfig ScopedClientOneConfig =
        MakeClientConfig(InFixture.ServerEndpoint.Address(), InFirstName, InTickMode);
    const NetSessionConfig ScopedClientTwoConfig =
        MakeClientConfig(InFixture.ServerEndpoint.Address(), InSecondName, InTickMode);

    NetSessionCallbacks ScopedServerCallbacks{};
    ScopedServerCallbacks.User = &InFixture.ServerRecorder;
    ScopedServerCallbacks.OnJoined = &OnJoined;
    ScopedServerCallbacks.OnSlotChanged = &OnSlotChanged;
    ScopedServerCallbacks.OnEvent = &OnEvent;
    ScopedServerCallbacks.OnTimeSync = &OnTimeSync;
    ScopedServerCallbacks.OnDisconnected = &OnDisconnected;

    NetSessionCallbacks ScopedClientOneCallbacks{};
    ScopedClientOneCallbacks.User = &InFixture.ClientOneRecorder;
    ScopedClientOneCallbacks.OnJoined = &OnJoined;
    ScopedClientOneCallbacks.OnSlotChanged = &OnSlotChanged;
    ScopedClientOneCallbacks.OnEvent = &OnEvent;
    ScopedClientOneCallbacks.OnTimeSync = &OnTimeSync;
    ScopedClientOneCallbacks.OnDisconnected = &OnDisconnected;

    NetSessionCallbacks ScopedClientTwoCallbacks{};
    ScopedClientTwoCallbacks.User = &InFixture.ClientTwoRecorder;
    ScopedClientTwoCallbacks.OnJoined = &OnJoined;
    ScopedClientTwoCallbacks.OnSlotChanged = &OnSlotChanged;
    ScopedClientTwoCallbacks.OnEvent = &OnEvent;
    ScopedClientTwoCallbacks.OnTimeSync = &OnTimeSync;
    ScopedClientTwoCallbacks.OnDisconnected = &OnDisconnected;

    InFixture.Server.SetClock(&ReadClock, &InFixture.Clock);
    InFixture.ClientOne.SetClock(&ReadClock, &InFixture.Clock);
    InFixture.ClientTwo.SetClock(&ReadClock, &InFixture.Clock);
    InFixture.Server.SetCallbacks(ScopedServerCallbacks);
    InFixture.ClientOne.SetCallbacks(ScopedClientOneCallbacks);
    InFixture.ClientTwo.SetCallbacks(ScopedClientTwoCallbacks);

    if (!InFixture.Server.Start(InFixture.ServerEndpoint.Link(), ScopedServerConfig) ||
        !InFixture.ClientOne.Start(InFixture.ClientOneEndpoint.Link(), ScopedClientOneConfig) ||
        !InFixture.ClientTwo.Start(InFixture.ClientTwoEndpoint.Link(), ScopedClientTwoConfig))
    {
        return false;
    }

    for (std::uint32_t ScopedStep = 0; ScopedStep < 8; ++ScopedStep)
    {
        InFixture.Server.Pump();
        InFixture.ClientOne.Pump();
        InFixture.ClientTwo.Pump();
    }
    return true;
}

void TestJoinAndRoster()
{
    using namespace Fby::Net;

    Fixture ScopedFixture;
    FBY_CHECK(OpenFixture(ScopedFixture, true, NetTickMode::Predicted, "Alpha", "Bravo"));
    FBY_CHECK(ScopedFixture.Server.Joined());
    FBY_CHECK(ScopedFixture.ClientOne.Joined());
    FBY_CHECK(ScopedFixture.ClientTwo.Joined());
    FBY_CHECK(ScopedFixture.ClientOne.LocalPlayer() == 1);
    FBY_CHECK(ScopedFixture.ClientTwo.LocalPlayer() == 2);
    FBY_CHECK(ScopedFixture.Server.PlayerCount() == 3);
    FBY_CHECK(ScopedFixture.ClientOne.PlayerCount() == 3);
    FBY_CHECK(ScopedFixture.ClientOneRecorder.JoinedCount == 1);
    FBY_CHECK(ScopedFixture.ClientTwoRecorder.JoinedCount == 1);

    const NetPlayerSlot* ScopedHost = ScopedFixture.ClientOne.FindPlayer(0);
    FBY_CHECK(ScopedHost != nullptr);
    if (ScopedHost != nullptr)
    {
        FBY_CHECK(std::string_view(ScopedHost->Name) == "Host");
    }
    const NetPlayerSlot* ScopedAlpha = ScopedFixture.Server.FindPlayer(1);
    FBY_CHECK(ScopedAlpha != nullptr);
    if (ScopedAlpha != nullptr)
    {
        FBY_CHECK(std::string_view(ScopedAlpha->Name) == "Alpha");
    }
}

void TestTimeSync()
{
    using namespace Fby::Net;

    Fixture ScopedFixture;
    FBY_CHECK(OpenFixture(ScopedFixture, true, NetTickMode::Predicted, "Alpha", "Bravo"));

    ScopedFixture.Clock.Ms = 500;
    ScopedFixture.ClientOne.Advance(0.0f);
    ScopedFixture.Server.Pump();
    ScopedFixture.Clock.Ms = 520;
    ScopedFixture.ClientOne.Pump();

    FBY_CHECK(ScopedFixture.ClientOne.RttMs() == 20);
    FBY_CHECK(ScopedFixture.ClientOneRecorder.TimeSyncs >= 1);
}

void TestTimeouts()
{
    using namespace Fby::Net;

    Fixture ScopedFixture;
    FBY_CHECK(OpenFixture(ScopedFixture, true, NetTickMode::Predicted, "Alpha", "Bravo"));
    FBY_CHECK(ScopedFixture.Server.PlayerCount() == 3);

    ScopedFixture.Clock.Ms = 6000;
    ScopedFixture.Server.Advance(0.0f);
    FBY_CHECK(ScopedFixture.Server.PlayerCount() == 1);
    FBY_CHECK(ScopedFixture.Server.FindPlayer(1) == nullptr);
    FBY_CHECK(ScopedFixture.ServerRecorder.SlotChanges >= 2);
}

void TestTicks()
{
    using namespace Fby::Net;

    {
        Fixture ScopedFixture;
        FBY_CHECK(OpenFixture(ScopedFixture, true, NetTickMode::Predicted, "Alpha", "Bravo"));

        for (std::uint32_t ScopedStep = 0; ScopedStep < 10; ++ScopedStep)
        {
            ScopedFixture.Clock.Ms += 100;
            ScopedFixture.Server.Advance(0.1f);
            ScopedFixture.ClientOne.Pump();
            ScopedFixture.ClientTwo.Pump();
        }
        FBY_CHECK(ScopedFixture.Server.LocalTick() == 120);
        FBY_CHECK(ScopedFixture.ClientOne.ConfirmedTick() == 120);

        // The client was not simulating during the join, so the first predicted
        // frame catches up to the confirmed tick plus the prediction lead.
        ScopedFixture.ClientOne.Advance(0.0f);
        FBY_CHECK(ScopedFixture.ClientOne.LocalTick() == 122);
        FBY_CHECK(ScopedFixture.ClientOne.TicksToSimulate() == 8);

        // One fixed step after the catch-up advances exactly one tick.
        ScopedFixture.ClientOne.Advance(1.0f / 120.0f);
        FBY_CHECK(ScopedFixture.ClientOne.LocalTick() == 123);
        FBY_CHECK(ScopedFixture.ClientOne.TicksToSimulate() >= 1);
    }

    {
        Fixture ScopedFixture;
        FBY_CHECK(OpenFixture(ScopedFixture, true, NetTickMode::Authoritative, "Alpha", "Bravo"));

        ScopedFixture.Clock.Ms += 100;
        ScopedFixture.Server.Advance(0.1f);
        ScopedFixture.ClientOne.Pump();
        FBY_CHECK(ScopedFixture.ClientOne.ConfirmedTick() == 12);

        ScopedFixture.ClientOne.Advance(0.0f);
        FBY_CHECK(ScopedFixture.ClientOne.LocalTick() == 12);
        FBY_CHECK(ScopedFixture.ClientOne.TicksToSimulate() == 12);
    }
}

void TestEvents()
{
    using namespace Fby::Net;

    Fixture ScopedFixture;
    FBY_CHECK(OpenFixture(ScopedFixture, true, NetTickMode::Predicted, "Alpha", "Bravo"));

    const std::vector<std::byte> ScopedPayload = {std::byte{0xAB}, std::byte{0xCD}};
    FBY_CHECK(ScopedFixture.ClientOne.SendEvent(42, ScopedPayload));
    ScopedFixture.Server.Pump();
    FBY_CHECK(ScopedFixture.ServerRecorder.Events == 1);
    FBY_CHECK(ScopedFixture.ServerRecorder.LastEventId == 42);
    FBY_CHECK(ScopedFixture.ServerRecorder.LastFromPlayer == 1);
    FBY_CHECK(ScopedFixture.ServerRecorder.LastPayload == ScopedPayload);

    FBY_CHECK(ScopedFixture.Server.SendEvent(7, ScopedPayload));
    // The host receives its own broadcast immediately.
    FBY_CHECK(ScopedFixture.ServerRecorder.Events == 2);
    ScopedFixture.ClientOne.Pump();
    FBY_CHECK(ScopedFixture.ClientOneRecorder.Events == 1);
    FBY_CHECK(ScopedFixture.ClientOneRecorder.LastEventId == 7);

    // Loss is recovered by the retransmit timer.
    ScopedFixture.Network.SetDropEvery(1);
    FBY_CHECK(ScopedFixture.ClientTwo.SendEvent(99, ScopedPayload));
    ScopedFixture.Server.Pump();
    FBY_CHECK(ScopedFixture.ServerRecorder.Events == 2);

    ScopedFixture.Network.SetDropEvery(0);
    ScopedFixture.Clock.Ms += 200;
    ScopedFixture.ClientTwo.Advance(0.0f);
    ScopedFixture.Server.Pump();
    FBY_CHECK(ScopedFixture.ServerRecorder.Events == 3);
    FBY_CHECK(ScopedFixture.ServerRecorder.LastEventId == 99);
}

void TestOfflineAndInvalid()
{
    using namespace Fby::Net;

    NetSessionConfig ScopedConfig{};
    ScopedConfig.Mode = NetSessionMode::Offline;
    NetSession ScopedSession;
    FBY_CHECK(ScopedSession.Start(NetLink{}, ScopedConfig));
    FBY_CHECK(ScopedSession.Role() == NetRole::Offline);
    ScopedSession.Advance(1.0f);
    FBY_CHECK(ScopedSession.LocalTick() == 120);
    FBY_CHECK(ScopedSession.TicksToSimulate() == 120);

    ScopedConfig.Mode = NetSessionMode::Hosted;
    NetSession ScopedInvalid;
    FBY_CHECK(!ScopedInvalid.Start(NetLink{}, ScopedConfig));
    FBY_CHECK(ScopedInvalid.Status() == NetSessionStatus::InvalidConfig);
}
}

int main()
{
    TestRoleResolution();
    TestJoinAndRoster();
    TestTimeSync();
    TestTimeouts();
    TestTicks();
    TestEvents();
    TestOfflineAndInvalid();

    if (GFailures == 0)
    {
        std::printf("net_session_test: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "net_session_test: %u failure(s)\n", GFailures);
    return 1;
}
