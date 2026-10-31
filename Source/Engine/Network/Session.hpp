// FemboyEngine2D - unified network session: roles, slots, time and events.
#pragma once

#include "Engine/Network/NetLink.hpp"
#include "Engine/Network/Transport.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace Fby::Net
{

inline constexpr std::uint32_t kSessionMaxPlayers = 8;
inline constexpr std::size_t kSessionNameBytes = 32;
inline constexpr std::size_t kSessionEventPayloadBytes = 64;
inline constexpr std::uint32_t kSessionPendingEvents = 16;
inline constexpr std::size_t kSessionMessageBytes = 512;
inline constexpr std::uint8_t kSessionNoPlayer = 0xFF;

enum class NetSessionMode : std::uint8_t
{
    Auto = 0,
    Offline,
    Dedicated,
    Hosted,
    Hybrid,
    PeerToPeer,
};

enum class NetRole : std::uint8_t
{
    Offline = 0,
    Authority,
    Client,
    Peer,
};

enum class NetTickMode : std::uint8_t
{
    Authoritative = 0,
    Predicted,
};

enum class NetSessionStatus : std::uint8_t
{
    NotStarted = 0,
    Offline,
    Connecting,
    Joined,
    Rejected,
    Disconnected,
    SendFailed,
    InvalidConfig,
};

enum class NetSlotState : std::uint8_t
{
    Free = 0,
    Joined,
    Ready,
};

enum class NetDisconnectReason : std::uint8_t
{
    None = 0,
    Timeout = 1,
    Bye = 2,
    Rejected = 3,
    Link = 4,
};

[[nodiscard]] constexpr const char* NetRoleName(NetRole InRole) noexcept
{
    switch (InRole)
    {
        case NetRole::Offline:
            return "Offline";
        case NetRole::Authority:
            return "Authority";
        case NetRole::Client:
            return "Client";
        case NetRole::Peer:
            return "Peer";
    }
    return "?";
}

[[nodiscard]] constexpr const char* NetSessionModeName(NetSessionMode InMode) noexcept
{
    switch (InMode)
    {
        case NetSessionMode::Auto:
            return "Auto";
        case NetSessionMode::Offline:
            return "Offline";
        case NetSessionMode::Dedicated:
            return "Dedicated";
        case NetSessionMode::Hosted:
            return "Hosted";
        case NetSessionMode::Hybrid:
            return "Hybrid";
        case NetSessionMode::PeerToPeer:
            return "PeerToPeer";
    }
    return "?";
}

[[nodiscard]] constexpr const char* NetSessionStatusName(NetSessionStatus InStatus) noexcept
{
    switch (InStatus)
    {
        case NetSessionStatus::NotStarted:
            return "NotStarted";
        case NetSessionStatus::Offline:
            return "Offline";
        case NetSessionStatus::Connecting:
            return "Connecting";
        case NetSessionStatus::Joined:
            return "Joined";
        case NetSessionStatus::Rejected:
            return "Rejected";
        case NetSessionStatus::Disconnected:
            return "Disconnected";
        case NetSessionStatus::SendFailed:
            return "SendFailed";
        case NetSessionStatus::InvalidConfig:
            return "InvalidConfig";
    }
    return "?";
}

struct NetPlayerSlot
{
    std::uint8_t Id = kSessionNoPlayer;
    NetSlotState State = NetSlotState::Free;
    std::uint16_t RttMs = 0;
    std::uint64_t JoinTick = 0;
    std::uint64_t LastSeenMs = 0;
    char Name[kSessionNameBytes]{};
    NetAddress Address{};
    bool Used = false;
};

// The developer-facing knob: Mode picks the topology, everything else is
// optional. `Auto` resolves from the environment (headless, lobby ticket,
// host request, explicit remote) so game code never branches per topology.
struct NetSessionConfig
{
    NetSessionMode Mode = NetSessionMode::Auto;
    NetTickMode TickMode = NetTickMode::Predicted;

    NetAddress Remote{};
    NetAddress AssignedServer{};
    bool HasAssignedServer = false;
    bool LobbyHost = false;
    bool HostRequested = false;
    bool Headless = false;

    char LocalName[kSessionNameBytes] = "Player";
    float FixedDtSeconds = 1.0f / 120.0f;
    std::uint32_t PredictionLeadTicks = 2;
    std::uint32_t PredictionMaxDriftTicks = 8;
    std::uint32_t TimeoutMs = 5000;
    std::uint32_t TimePingIntervalMs = 500;
    std::uint32_t RosterIntervalMs = 1000;
    std::uint32_t ServerSyncIntervalMs = 100;
    std::uint32_t RejoinIntervalMs = 250;
    std::uint32_t EventRetransmitMs = 100;
    std::uint32_t EventMaxAttempts = 8;
};

// The whole point of the mode table: one function decides whether this process
// listens, connects, degrades a missing server to a player host, or stays local.
[[nodiscard]] NetRole ResolveSessionRole(const NetSessionConfig& InConfig) noexcept;

struct NetSessionCallbacks
{
    void* User = nullptr;
    void (*OnJoined)(void* InUser, std::uint8_t InPlayerId, std::uint64_t InServerTick) = nullptr;
    void (*OnSlotChanged)(void* InUser, std::uint8_t InPlayerId, bool InPresent) = nullptr;
    void (*OnEvent)(void* InUser, std::uint8_t InFromPlayer, std::uint32_t InEventId,
                    std::span<const std::byte> InPayload) = nullptr;
    void (*OnTimeSync)(void* InUser, std::uint64_t InServerTick, std::uint32_t InRttMs) = nullptr;
    void (*OnDisconnected)(void* InUser, NetDisconnectReason InReason) = nullptr;
};

class NetSession
{
public:
    using NowFn = std::uint64_t (*)(void* InUser);

    NetSession() = default;

    // Starts the session. The link may be absent for Offline. For a client the
    // link must already be bound to the remote address by the backend.
    [[nodiscard]] bool Start(const NetLink& InLink, const NetSessionConfig& InConfig) noexcept;

    // Optional transport hook: when set, every session datagram is handed to the
    // hook instead of the raw link, so a secure transport can seal it. Inbound
    // datagrams are delivered with ReceiveMessage.
    using MessageSendFn = bool (*)(void* InUser, std::uint8_t InToPlayer, const NetAddress& InTo,
                                   std::span<const std::byte> InBytes) noexcept;
    void SetSendHook(MessageSendFn InFn, void* InUser) noexcept
    {
        MSendFn = InFn;
        MSendUser = InUser;
    }
    void ReceiveMessage(const NetAddress& InFrom, std::span<const std::byte> InPayload) noexcept;
    void Stop() noexcept;

    // Test seam: a deterministic millisecond clock for time-sync tests.
    void SetClock(NowFn InNow, void* InUser) noexcept;
    void SetCallbacks(const NetSessionCallbacks& InCallbacks) noexcept;

    // Drives ticks, periodic messages, timeouts and event retransmits.
    void Advance(float InDeltaSeconds) noexcept;
    // Drains the link and processes every datagram.
    void Pump() noexcept;

    // Reliable ordered events. On an authority they broadcast to every client
    // and are delivered locally; on a client they go to the authority.
    [[nodiscard]] bool SendEvent(std::uint32_t InEventId, std::span<const std::byte> InPayload) noexcept;

    [[nodiscard]] NetRole Role() const noexcept
    {
        return MRole;
    }
    [[nodiscard]] NetSessionStatus Status() const noexcept
    {
        return MStatus;
    }
    [[nodiscard]] bool Joined() const noexcept
    {
        return MStatus == NetSessionStatus::Joined;
    }
    [[nodiscard]] std::uint8_t LocalPlayer() const noexcept
    {
        return MLocalPlayer;
    }
    [[nodiscard]] std::uint64_t LocalTick() const noexcept
    {
        return MLocalTick;
    }
    [[nodiscard]] std::uint64_t ConfirmedTick() const noexcept
    {
        return MConfirmedTick;
    }
    [[nodiscard]] std::uint32_t TicksToSimulate() const noexcept
    {
        return MTicksToSimulate;
    }
    [[nodiscard]] std::uint32_t RttMs() const noexcept
    {
        return MRttMs;
    }
    [[nodiscard]] std::int64_t ClockOffsetMs() const noexcept
    {
        return MClockOffsetMs;
    }
    [[nodiscard]] std::uint32_t PlayerCount() const noexcept
    {
        return MPlayerCount;
    }
    // Address the local endpoint sends client traffic to. For a dedicated
    // server this is its own bind address; relay voice uses it directly.
    [[nodiscard]] NetAddress Remote() const noexcept
    {
        return MConfig.Remote;
    }
    [[nodiscard]] const NetPlayerSlot& Slot(std::uint32_t InIndex) const noexcept
    {
        return MSlots[InIndex];
    }
    [[nodiscard]] const NetPlayerSlot* FindPlayer(std::uint8_t InPlayerId) const noexcept;

protected:
    struct PendingEvent
    {
        bool Used = false;
        std::uint16_t Sequence = 0;
        std::uint32_t EventId = 0;
        std::uint16_t PayloadBytes = 0;
        std::uint8_t Attempts = 0;
        std::uint64_t LastSentMs = 0;
        std::array<std::byte, kSessionEventPayloadBytes> Payload{};
    };

    [[nodiscard]] std::uint64_t NowMs() const noexcept;
    void ResetState() noexcept;
    void SetStatus(NetSessionStatus InStatus, NetDisconnectReason InReason = NetDisconnectReason::None) noexcept;
    static void OnMessageThunk(void* InUser, const NetAddress& InFrom,
                               std::span<const std::byte> InMessage) noexcept;

    void HandleMessage(const NetAddress& InFrom, std::span<const std::byte> InMessage) noexcept;
    [[nodiscard]] bool SendBytes(std::uint8_t InPlayer, const NetAddress& InTo,
                                 std::span<const std::byte> InBytes) noexcept;
    void HandleHello(const NetAddress& InFrom, std::span<const std::byte> InPayload) noexcept;
    void HandleWelcome(std::span<const std::byte> InPayload) noexcept;
    void HandleRoster(std::span<const std::byte> InPayload) noexcept;
    void HandleTimePing(const NetAddress& InFrom, std::span<const std::byte> InPayload) noexcept;
    void HandleTimePong(std::span<const std::byte> InPayload) noexcept;
    void HandleServerSync(std::span<const std::byte> InPayload) noexcept;
    void HandleEvent(const NetAddress& InFrom, std::span<const std::byte> InPayload) noexcept;
    void HandleEventAck(const NetAddress& InFrom, std::span<const std::byte> InPayload) noexcept;
    void HandleBye(const NetAddress& InFrom, std::span<const std::byte> InPayload) noexcept;

    void SendHello() noexcept;
    void SendWelcome(std::uint32_t InSlotIndex) noexcept;
    void SendRoster() noexcept;
    void SendServerSync() noexcept;
    void SendBye() noexcept;
    void SendToSlot(const NetPlayerSlot& InSlot, std::span<const std::byte> InMessage) noexcept;

    [[nodiscard]] std::uint32_t FindSlotIndexByAddress(const NetAddress& InAddress) const noexcept;
    [[nodiscard]] std::uint32_t AllocateSlot(const NetAddress& InAddress, const char* InName) noexcept;
    void FreeSlot(std::uint32_t InSlotIndex, NetDisconnectReason InReason) noexcept;

    void DeliverEvent(std::uint8_t InFromPlayer, std::uint32_t InEventId,
                      std::span<const std::byte> InPayload) noexcept;
    [[nodiscard]] bool QueueEvent(std::uint32_t InSlotIndex, std::uint32_t InEventId,
                                  std::span<const std::byte> InPayload) noexcept;
    void ProcessEventTimers() noexcept;
    void SendPendingForSlot(std::uint32_t InSlotIndex, PendingEvent& InEvent, bool InForce) noexcept;
    void FlushAckForSlot(std::uint32_t InSlotIndex, std::uint16_t InAck) noexcept;

    [[nodiscard]] bool IsClientRole() const noexcept
    {
        return MRole == NetRole::Client;
    }
    [[nodiscard]] bool IsAuthority() const noexcept
    {
        return MRole == NetRole::Authority;
    }

    NetLink MLink{};
    std::uint64_t MLastServerMessageMs = 0;
    MessageSendFn MSendFn = nullptr;
    void* MSendUser = nullptr;
    NetSessionConfig MConfig{};
    NetSessionCallbacks MCallbacks{};
    NowFn MNow = nullptr;
    void* MNowUser = nullptr;

    NetRole MRole = NetRole::Offline;
    NetSessionStatus MStatus = NetSessionStatus::NotStarted;
    bool MStarted = false;
    std::uint8_t MLocalPlayer = kSessionNoPlayer;

    std::array<NetPlayerSlot, kSessionMaxPlayers> MSlots{};
    std::uint32_t MPlayerCount = 0;

    std::uint64_t MServerTick = 0;
    std::uint64_t MConfirmedTick = 0;
    std::uint64_t MLocalTick = 0;
    float MTickAccumulator = 0.0f;
    std::uint32_t MTicksToSimulate = 0;

    std::uint32_t MRttMs = 0;
    std::int64_t MClockOffsetMs = 0;

    std::uint64_t MStartMs = 0;
    std::uint64_t MLastHelloMs = 0;
    std::uint64_t MLastPingSentMs = 0;
    std::uint64_t MLastRosterMs = 0;
    std::uint64_t MLastSyncMs = 0;
    std::uint64_t MSessionNonce = 0;
    std::uint64_t MClientNonce = 0;

    std::array<std::array<PendingEvent, kSessionPendingEvents>, kSessionMaxPlayers> MRemotePending{};
    std::array<std::uint16_t, kSessionMaxPlayers> MNextRemoteSequence{};
    std::array<std::uint16_t, kSessionMaxPlayers> MLastRemoteAck{};
    std::array<std::uint16_t, kSessionMaxPlayers> MLastClientSequence{};
    std::array<PendingEvent, kSessionPendingEvents> MLocalPending{};
    std::uint16_t MNextLocalSequence = 1;
    std::uint16_t MLastAuthoritySequence = 0;
    std::uint16_t MLastReceivedAuthority = 0;
};
}
