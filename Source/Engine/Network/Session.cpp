// FemboyEngine2D - unified network session implementation.
#include "Engine/Core/Time.hpp"
#include "Engine/Network/Session.hpp"

#include "Engine/Core/SecureRng.hpp"
#include "Engine/Network/ByteOrder.hpp"

#include <cstring>

namespace Fby::Net
{
namespace
{
constexpr std::uint32_t kSessionMagic = 0x314E5346u;
constexpr std::uint16_t kSessionVersion = 1;
constexpr std::uint8_t kHeaderBytes = 8;

enum class MsgType : std::uint8_t
{
    Hello = 1,
    Welcome = 2,
    Roster = 3,
    TimePing = 4,
    TimePong = 5,
    ServerSync = 6,
    Event = 7,
    EventAck = 8,
    Bye = 9,
};

constexpr std::size_t kHelloBytes = kHeaderBytes + kSessionNameBytes + 8 + 4;
constexpr std::size_t kWelcomeBytes = kHeaderBytes + 1 + 1 + 2 + 8 + 8 + 2;
constexpr std::size_t kRosterEntryBytes = 1 + 1 + 2 + 8 + kSessionNameBytes + 6;
constexpr std::size_t kRosterHeaderBytes = kHeaderBytes + 1 + 1 + 8;
constexpr std::size_t kTimePingBytes = kHeaderBytes + 8;
constexpr std::size_t kTimePongBytes = kHeaderBytes + 8 + 8 + 8;
constexpr std::size_t kServerSyncBytes = kHeaderBytes + 8 + 8;
constexpr std::size_t kEventHeaderBytes = kHeaderBytes + 2 + 4 + 2 + 1 + 1;
constexpr std::size_t kEventAckBytes = kHeaderBytes + 2;
constexpr std::size_t kByeBytes = kHeaderBytes + 1 + 1;

[[nodiscard]] std::uint8_t* BytesOf(std::span<std::byte> InBytes) noexcept
{
    return reinterpret_cast<std::uint8_t*>(InBytes.data());
}

[[nodiscard]] const std::uint8_t* BytesOf(std::span<const std::byte> InBytes) noexcept
{
    return reinterpret_cast<const std::uint8_t*>(InBytes.data());
}

[[nodiscard]] std::uint64_t SteadyNowMs(void*) noexcept
{
    return Core::Clock::NowMilliseconds();
}

bool WriteHeader(std::span<std::byte> OutBytes, MsgType InType) noexcept
{
    if (OutBytes.size() < kHeaderBytes)
    {
        return false;
    }
    std::uint8_t* ScopedOut = BytesOf(OutBytes);
    std::memset(ScopedOut, 0, kHeaderBytes);
    Detail::StoreU32(ScopedOut, kSessionMagic);
    Detail::StoreU16(ScopedOut + 4, kSessionVersion);
    ScopedOut[6] = static_cast<std::uint8_t>(InType);
    ScopedOut[7] = 0;
    return true;
}

[[nodiscard]] bool ReadHeader(std::span<const std::byte> InBytes, MsgType* OutType) noexcept
{
    if (InBytes.size() < kHeaderBytes)
    {
        return false;
    }
    const std::uint8_t* ScopedIn = BytesOf(InBytes);
    if (Detail::LoadU32(ScopedIn) != kSessionMagic || Detail::LoadU16(ScopedIn + 4) != kSessionVersion)
    {
        return false;
    }
    const std::uint8_t ScopedType = ScopedIn[6];
    if (ScopedType < static_cast<std::uint8_t>(MsgType::Hello) ||
        ScopedType > static_cast<std::uint8_t>(MsgType::Bye))
    {
        return false;
    }
    if (OutType != nullptr)
    {
        *OutType = static_cast<MsgType>(ScopedType);
    }
    return true;
}

void StoreName(std::uint8_t* OutBytes, const char* InName) noexcept
{
    std::memset(OutBytes, 0, kSessionNameBytes);
    if (InName == nullptr)
    {
        return;
    }
    std::size_t ScopedLength = 0;
    while (ScopedLength + 1 < kSessionNameBytes && InName[ScopedLength] != '\0')
    {
        OutBytes[ScopedLength] = static_cast<std::uint8_t>(InName[ScopedLength]);
        ++ScopedLength;
    }
}

void LoadName(const std::uint8_t* InBytes, char* OutName) noexcept
{
    std::memcpy(OutName, InBytes, kSessionNameBytes);
    OutName[kSessionNameBytes - 1] = '\0';
}

[[nodiscard]] std::size_t EncodeHello(std::span<std::byte> OutBytes, const char* InName, std::uint64_t InNonce,
                                      std::uint32_t InCapabilities) noexcept
{
    if (OutBytes.size() < kHelloBytes || !WriteHeader(OutBytes.first(kHelloBytes), MsgType::Hello))
    {
        return 0;
    }
    std::uint8_t* ScopedOut = BytesOf(OutBytes) + kHeaderBytes;
    StoreName(ScopedOut, InName);
    Detail::StoreU64(ScopedOut + kSessionNameBytes, InNonce);
    Detail::StoreU32(ScopedOut + kSessionNameBytes + 8, InCapabilities);
    return kHelloBytes;
}

[[nodiscard]] bool DecodeHello(std::span<const std::byte> InBytes, char* OutName, std::uint64_t* OutNonce,
                               std::uint32_t* OutCapabilities) noexcept
{
    MsgType ScopedType{};
    if (InBytes.size() != kHelloBytes || !ReadHeader(InBytes, &ScopedType) || ScopedType != MsgType::Hello)
    {
        return false;
    }
    const std::uint8_t* ScopedIn = BytesOf(InBytes) + kHeaderBytes;
    if (OutName != nullptr)
    {
        LoadName(ScopedIn, OutName);
    }
    if (OutNonce != nullptr)
    {
        *OutNonce = Detail::LoadU64(ScopedIn + kSessionNameBytes);
    }
    if (OutCapabilities != nullptr)
    {
        *OutCapabilities = Detail::LoadU32(ScopedIn + kSessionNameBytes + 8);
    }
    return true;
}

std::size_t EncodeWelcome(std::span<std::byte> OutBytes, std::uint8_t InPlayerId, std::uint8_t InMaxPlayers,
                          std::uint64_t InServerTick, std::uint64_t InSessionNonce, std::uint16_t InTickRate) noexcept
{
    if (OutBytes.size() < kWelcomeBytes || !WriteHeader(OutBytes.first(kWelcomeBytes), MsgType::Welcome))
    {
        return 0;
    }
    std::uint8_t* ScopedOut = BytesOf(OutBytes) + kHeaderBytes;
    ScopedOut[0] = InPlayerId;
    ScopedOut[1] = InMaxPlayers;
    Detail::StoreU16(ScopedOut + 2, 0);
    Detail::StoreU64(ScopedOut + 4, InServerTick);
    Detail::StoreU64(ScopedOut + 12, InSessionNonce);
    Detail::StoreU16(ScopedOut + 20, InTickRate);
    return kWelcomeBytes;
}

bool DecodeWelcome(std::span<const std::byte> InBytes, std::uint8_t* OutPlayerId, std::uint64_t* OutServerTick,
                   std::uint64_t* OutSessionNonce, std::uint16_t* OutTickRate) noexcept
{
    MsgType ScopedType{};
    if (InBytes.size() != kWelcomeBytes || !ReadHeader(InBytes, &ScopedType) || ScopedType != MsgType::Welcome)
    {
        return false;
    }
    const std::uint8_t* ScopedIn = BytesOf(InBytes) + kHeaderBytes;
    *OutPlayerId = ScopedIn[0];
    *OutServerTick = Detail::LoadU64(ScopedIn + 4);
    *OutSessionNonce = Detail::LoadU64(ScopedIn + 12);
    *OutTickRate = Detail::LoadU16(ScopedIn + 20);
    return true;
}

std::size_t EncodeRoster(std::span<std::byte> OutBytes, const std::array<NetPlayerSlot, kSessionMaxPlayers>& InSlots,
                         std::uint64_t InServerTick) noexcept
{
    std::uint8_t ScopedCount = 0;
    for (const NetPlayerSlot& ScopedSlot : InSlots)
    {
        ScopedCount = static_cast<std::uint8_t>(ScopedCount + (ScopedSlot.Used ? 1 : 0));
    }
    const std::size_t ScopedBytes = kRosterHeaderBytes + ScopedCount * kRosterEntryBytes;
    if (OutBytes.size() < ScopedBytes || !WriteHeader(OutBytes.first(ScopedBytes), MsgType::Roster))
    {
        return 0;
    }

    std::uint8_t* ScopedOut = BytesOf(OutBytes) + kHeaderBytes;
    ScopedOut[0] = ScopedCount;
    ScopedOut[1] = 0;
    Detail::StoreU64(ScopedOut + 2, InServerTick);
    std::uint8_t* ScopedEntry = ScopedOut + kRosterHeaderBytes - kHeaderBytes;
    for (const NetPlayerSlot& ScopedSlot : InSlots)
    {
        if (!ScopedSlot.Used)
        {
            continue;
        }
        ScopedEntry[0] = ScopedSlot.Id;
        ScopedEntry[1] = static_cast<std::uint8_t>(ScopedSlot.State);
        Detail::StoreU16(ScopedEntry + 2, ScopedSlot.RttMs);
        Detail::StoreU64(ScopedEntry + 4, ScopedSlot.JoinTick);
        std::memcpy(ScopedEntry + 12, ScopedSlot.Name, kSessionNameBytes);
        Detail::StoreU32(ScopedEntry + 12 + kSessionNameBytes, ScopedSlot.Address.Host);
        Detail::StoreU16(ScopedEntry + 16 + kSessionNameBytes, ScopedSlot.Address.Port);
        ScopedEntry += kRosterEntryBytes;
    }
    return ScopedBytes;
}

bool DecodeRoster(std::span<const std::byte> InBytes, std::uint8_t* OutCount, std::uint64_t* OutServerTick,
                  std::array<NetPlayerSlot, kSessionMaxPlayers>* OutSlots) noexcept
{
    MsgType ScopedType{};
    if (!ReadHeader(InBytes, &ScopedType) || ScopedType != MsgType::Roster || InBytes.size() < kRosterHeaderBytes)
    {
        return false;
    }
    const std::uint8_t* ScopedIn = BytesOf(InBytes) + kHeaderBytes;
    const std::uint8_t ScopedCount = ScopedIn[0];
    if (ScopedCount > kSessionMaxPlayers)
    {
        return false;
    }
    if (InBytes.size() != kRosterHeaderBytes + ScopedCount * kRosterEntryBytes)
    {
        return false;
    }
    *OutCount = ScopedCount;
    *OutServerTick = Detail::LoadU64(ScopedIn + 2);
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedCount; ++ScopedIndex)
    {
        const std::uint8_t* ScopedEntry = BytesOf(InBytes) + kRosterHeaderBytes + ScopedIndex * kRosterEntryBytes;
        if (ScopedEntry[0] >= kSessionMaxPlayers)
        {
            return false;
        }
        NetPlayerSlot& ScopedSlot = (*OutSlots)[ScopedEntry[0]];
        ScopedSlot.Id = ScopedEntry[0];
        ScopedSlot.State = static_cast<NetSlotState>(ScopedEntry[1]);
        ScopedSlot.RttMs = Detail::LoadU16(ScopedEntry + 2);
        ScopedSlot.JoinTick = Detail::LoadU64(ScopedEntry + 4);
        std::memcpy(ScopedSlot.Name, ScopedEntry + 12, kSessionNameBytes);
        ScopedSlot.Name[kSessionNameBytes - 1] = '\0';
        ScopedSlot.Address.Host = Detail::LoadU32(ScopedEntry + 12 + kSessionNameBytes);
        ScopedSlot.Address.Port = Detail::LoadU16(ScopedEntry + 16 + kSessionNameBytes);
        ScopedSlot.Used = true;
    }
    return true;
}

std::size_t EncodeTimePing(std::span<std::byte> OutBytes, std::uint64_t InClientTimeMs) noexcept
{
    if (OutBytes.size() < kTimePingBytes || !WriteHeader(OutBytes.first(kTimePingBytes), MsgType::TimePing))
    {
        return 0;
    }
    Detail::StoreU64(BytesOf(OutBytes) + kHeaderBytes, InClientTimeMs);
    return kTimePingBytes;
}

bool DecodeTimePing(std::span<const std::byte> InBytes, std::uint64_t* OutClientTimeMs) noexcept
{
    MsgType ScopedType{};
    if (InBytes.size() != kTimePingBytes || !ReadHeader(InBytes, &ScopedType) || ScopedType != MsgType::TimePing)
    {
        return false;
    }
    *OutClientTimeMs = Detail::LoadU64(BytesOf(InBytes) + kHeaderBytes);
    return true;
}

std::size_t EncodeTimePong(std::span<std::byte> OutBytes, std::uint64_t InClientTimeMs, std::uint64_t InServerTimeMs,
                           std::uint64_t InServerTick) noexcept
{
    if (OutBytes.size() < kTimePongBytes || !WriteHeader(OutBytes.first(kTimePongBytes), MsgType::TimePong))
    {
        return 0;
    }
    std::uint8_t* ScopedOut = BytesOf(OutBytes) + kHeaderBytes;
    Detail::StoreU64(ScopedOut, InClientTimeMs);
    Detail::StoreU64(ScopedOut + 8, InServerTimeMs);
    Detail::StoreU64(ScopedOut + 16, InServerTick);
    return kTimePongBytes;
}

bool DecodeTimePong(std::span<const std::byte> InBytes, std::uint64_t* OutClientTimeMs,
                    std::uint64_t* OutServerTimeMs, std::uint64_t* OutServerTick) noexcept
{
    MsgType ScopedType{};
    if (InBytes.size() != kTimePongBytes || !ReadHeader(InBytes, &ScopedType) || ScopedType != MsgType::TimePong)
    {
        return false;
    }
    const std::uint8_t* ScopedIn = BytesOf(InBytes) + kHeaderBytes;
    *OutClientTimeMs = Detail::LoadU64(ScopedIn);
    *OutServerTimeMs = Detail::LoadU64(ScopedIn + 8);
    *OutServerTick = Detail::LoadU64(ScopedIn + 16);
    return true;
}

std::size_t EncodeServerSync(std::span<std::byte> OutBytes, std::uint64_t InServerTick,
                             std::uint64_t InServerTimeMs) noexcept
{
    if (OutBytes.size() < kServerSyncBytes || !WriteHeader(OutBytes.first(kServerSyncBytes), MsgType::ServerSync))
    {
        return 0;
    }
    std::uint8_t* ScopedOut = BytesOf(OutBytes) + kHeaderBytes;
    Detail::StoreU64(ScopedOut, InServerTick);
    Detail::StoreU64(ScopedOut + 8, InServerTimeMs);
    return kServerSyncBytes;
}

bool DecodeServerSync(std::span<const std::byte> InBytes, std::uint64_t* OutServerTick) noexcept
{
    MsgType ScopedType{};
    if (InBytes.size() != kServerSyncBytes || !ReadHeader(InBytes, &ScopedType) || ScopedType != MsgType::ServerSync)
    {
        return false;
    }
    *OutServerTick = Detail::LoadU64(BytesOf(InBytes) + kHeaderBytes);
    return true;
}

std::size_t EncodeEvent(std::span<std::byte> OutBytes, std::uint16_t InSequence, std::uint32_t InEventId,
                        std::uint8_t InFromPlayer, std::span<const std::byte> InPayload) noexcept
{
    if (InPayload.size() > kSessionEventPayloadBytes)
    {
        return 0;
    }
    const std::size_t ScopedBytes = kEventHeaderBytes + InPayload.size();
    if (OutBytes.size() < ScopedBytes || !WriteHeader(OutBytes.first(ScopedBytes), MsgType::Event))
    {
        return 0;
    }
    std::uint8_t* ScopedOut = BytesOf(OutBytes) + kHeaderBytes;
    Detail::StoreU16(ScopedOut, InSequence);
    Detail::StoreU32(ScopedOut + 2, InEventId);
    Detail::StoreU16(ScopedOut + 6, static_cast<std::uint16_t>(InPayload.size()));
    ScopedOut[8] = InFromPlayer;
    ScopedOut[9] = 0;
    if (!InPayload.empty())
    {
        std::memcpy(ScopedOut + 10, InPayload.data(), InPayload.size());
    }
    return ScopedBytes;
}

bool DecodeEvent(std::span<const std::byte> InBytes, std::uint16_t* OutSequence, std::uint32_t* OutEventId,
                 std::uint8_t* OutFromPlayer, std::span<const std::byte>* OutPayload) noexcept
{
    MsgType ScopedType{};
    if (InBytes.size() < kEventHeaderBytes || !ReadHeader(InBytes, &ScopedType) || ScopedType != MsgType::Event)
    {
        return false;
    }
    const std::uint8_t* ScopedIn = BytesOf(InBytes) + kHeaderBytes;
    const std::uint16_t ScopedPayloadBytes = Detail::LoadU16(ScopedIn + 6);
    if (ScopedPayloadBytes > kSessionEventPayloadBytes || InBytes.size() != kEventHeaderBytes + ScopedPayloadBytes)
    {
        return false;
    }
    *OutSequence = Detail::LoadU16(ScopedIn);
    *OutEventId = Detail::LoadU32(ScopedIn + 2);
    *OutFromPlayer = ScopedIn[8];
    *OutPayload = InBytes.subspan(kEventHeaderBytes, ScopedPayloadBytes);
    return true;
}

std::size_t EncodeEventAck(std::span<std::byte> OutBytes, std::uint16_t InSequence) noexcept
{
    if (OutBytes.size() < kEventAckBytes || !WriteHeader(OutBytes.first(kEventAckBytes), MsgType::EventAck))
    {
        return 0;
    }
    Detail::StoreU16(BytesOf(OutBytes) + kHeaderBytes, InSequence);
    return kEventAckBytes;
}

bool DecodeEventAck(std::span<const std::byte> InBytes, std::uint16_t* OutSequence) noexcept
{
    MsgType ScopedType{};
    if (InBytes.size() != kEventAckBytes || !ReadHeader(InBytes, &ScopedType) || ScopedType != MsgType::EventAck)
    {
        return false;
    }
    *OutSequence = Detail::LoadU16(BytesOf(InBytes) + kHeaderBytes);
    return true;
}

std::size_t EncodeBye(std::span<std::byte> OutBytes, std::uint8_t InPlayerId, NetDisconnectReason InReason) noexcept
{
    if (OutBytes.size() < kByeBytes || !WriteHeader(OutBytes.first(kByeBytes), MsgType::Bye))
    {
        return 0;
    }
    std::uint8_t* ScopedOut = BytesOf(OutBytes) + kHeaderBytes;
    ScopedOut[0] = InPlayerId;
    ScopedOut[1] = static_cast<std::uint8_t>(InReason);
    return kByeBytes;
}

bool DecodeBye(std::span<const std::byte> InBytes, std::uint8_t* OutPlayerId, NetDisconnectReason* OutReason) noexcept
{
    MsgType ScopedType{};
    if (InBytes.size() != kByeBytes || !ReadHeader(InBytes, &ScopedType) || ScopedType != MsgType::Bye)
    {
        return false;
    }
    *OutPlayerId = BytesOf(InBytes)[kHeaderBytes];
    *OutReason = static_cast<NetDisconnectReason>(BytesOf(InBytes)[kHeaderBytes + 1]);
    return true;
}

void CopyNameInto(char* OutName, const char* InName) noexcept
{
    std::memset(OutName, 0, kSessionNameBytes);
    if (InName == nullptr)
    {
        return;
    }
    std::size_t ScopedLength = 0;
    while (ScopedLength + 1 < kSessionNameBytes && InName[ScopedLength] != '\0')
    {
        OutName[ScopedLength] = InName[ScopedLength];
        ++ScopedLength;
    }
}

// Sequence comparison that tolerates the 16-bit wrap.
[[nodiscard]] bool SequenceIsNewer(std::uint16_t InSequence, std::uint16_t InLast) noexcept
{
    return static_cast<std::int16_t>(static_cast<std::uint16_t>(InSequence - InLast)) > 0;
}
}

NetRole ResolveSessionRole(const NetSessionConfig& InConfig) noexcept
{
    const bool ScopedHasRemote = InConfig.Remote.Host != 0 || InConfig.Remote.Port != 0;

    switch (InConfig.Mode)
    {
        case NetSessionMode::Offline:
            return NetRole::Offline;
        case NetSessionMode::Dedicated:
            return NetRole::Authority;
        case NetSessionMode::Hosted:
            return NetRole::Authority;
        case NetSessionMode::PeerToPeer:
            return NetRole::Peer;
        case NetSessionMode::Hybrid:
            if (InConfig.HasAssignedServer)
            {
                return NetRole::Client;
            }
            if (InConfig.LobbyHost)
            {
                return NetRole::Authority;
            }
            return ScopedHasRemote ? NetRole::Client : NetRole::Offline;
        case NetSessionMode::Auto:
            if (InConfig.Headless)
            {
                return NetRole::Authority;
            }
            if (InConfig.HasAssignedServer)
            {
                return NetRole::Client;
            }
            if (InConfig.HostRequested)
            {
                return NetRole::Authority;
            }
            return ScopedHasRemote ? NetRole::Client : NetRole::Offline;
    }
    return NetRole::Offline;
}

std::uint64_t NetSession::NowMs() const noexcept
{
    return MNow != nullptr ? MNow(MNowUser) : SteadyNowMs(nullptr);
}

void NetSession::SetClock(NowFn InNow, void* InUser) noexcept
{
    MNow = InNow;
    MNowUser = InUser;
}

void NetSession::SetCallbacks(const NetSessionCallbacks& InCallbacks) noexcept
{
    MCallbacks = InCallbacks;
}

void NetSession::ResetState() noexcept
{
    MSlots = {};
    MRemotePending = {};
    MNextRemoteSequence.fill(1);
    MLastRemoteAck = {};
    MLastClientSequence = {};
    MLocalPending = {};
    MNextLocalSequence = 1;
    MLastAuthoritySequence = 0;
    MLastReceivedAuthority = 0;
    MPlayerCount = 0;
    MLocalPlayer = kSessionNoPlayer;
    MServerTick = 0;
    MConfirmedTick = 0;
    MLocalTick = 0;
    MTickAccumulator = 0.0f;
    MTicksToSimulate = 0;
    MRttMs = 0;
    MClockOffsetMs = 0;
    MLastHelloMs = 0;
    MLastPingSentMs = 0;
    MLastRosterMs = 0;
    MLastSyncMs = 0;
    MSessionNonce = 0;
    MClientNonce = 0;
}

bool NetSession::Start(const NetLink& InLink, const NetSessionConfig& InConfig) noexcept
{
    ResetState();
    MConfig = InConfig;
    MStarted = false;
    MLink = InLink;

    if (MConfig.FixedDtSeconds <= 0.0f)
    {
        SetStatus(NetSessionStatus::InvalidConfig);
        return false;
    }
    if (MConfig.HasAssignedServer)
    {
        MConfig.Remote = MConfig.AssignedServer;
    }

    MRole = ResolveSessionRole(MConfig);
    MStartMs = NowMs();
    MLastServerMessageMs = MStartMs;

    if (MRole == NetRole::Offline)
    {
        MStarted = true;
        MLocalPlayer = 0;
        MSlots[0].Id = 0;
        MSlots[0].State = NetSlotState::Joined;
        MSlots[0].Used = true;
        CopyNameInto(MSlots[0].Name, MConfig.LocalName);
        MPlayerCount = 1;
        SetStatus(NetSessionStatus::Offline);
        return true;
    }
    if (!MLink.Valid() && MSendFn == nullptr)
    {
        SetStatus(NetSessionStatus::InvalidConfig);
        return false;
    }

    MStarted = true;
    if (IsAuthority())
    {
        if (!SecureRng::NextU64(MSessionNonce))
        {
            MStarted = false;
            SetStatus(NetSessionStatus::InvalidConfig);
            return false;
        }
        MServerTick = 0;
        MLocalTick = 0;
        MConfirmedTick = 0;
        if (!MConfig.Headless)
        {
            const std::uint32_t ScopedSlot = AllocateSlot(NetAddress{}, MConfig.LocalName);
            MLocalPlayer = static_cast<std::uint8_t>(ScopedSlot);
        }
        MLastRosterMs = MStartMs;
        MLastSyncMs = MStartMs;
        SetStatus(NetSessionStatus::Joined);
        return true;
    }
    if (IsClientRole())
    {
        if (!SecureRng::NextU64(MClientNonce))
        {
            MStarted = false;
            SetStatus(NetSessionStatus::InvalidConfig);
            return false;
        }
        SetStatus(NetSessionStatus::Connecting);
        SendHello();
        MLastHelloMs = NowMs();
        return true;
    }

    // Peer-to-peer keeps the same slot bookkeeping; the lockstep transport is a
    // later step, so a peer starts joined with its own local slot.
    const std::uint32_t ScopedSlot = AllocateSlot(NetAddress{}, MConfig.LocalName);
    MLocalPlayer = static_cast<std::uint8_t>(ScopedSlot);
    SetStatus(NetSessionStatus::Joined);
    return true;
}

void NetSession::Stop() noexcept
{
    if (MStarted && IsClientRole() && Joined())
    {
        SendBye();
    }
    MStarted = false;
    MLink = NetLink{};
    SetStatus(NetSessionStatus::Disconnected);
}

void NetSession::SetStatus(NetSessionStatus InStatus, NetDisconnectReason InReason) noexcept
{
    const bool ScopedChanged = MStatus != InStatus;
    MStatus = InStatus;
    if (ScopedChanged && InStatus == NetSessionStatus::Disconnected && MCallbacks.OnDisconnected != nullptr)
    {
        MCallbacks.OnDisconnected(MCallbacks.User, InReason);
    }
}

void NetSession::OnMessageThunk(void* InUser, const NetAddress& InFrom,
                                std::span<const std::byte> InMessage) noexcept
{
    auto* ScopedSession = static_cast<NetSession*>(InUser);
    if (ScopedSession != nullptr && ScopedSession->MStarted)
    {
        ScopedSession->HandleMessage(InFrom, InMessage);
    }
}

bool NetSession::SendBytes(std::uint8_t InPlayer, const NetAddress& InTo,
                           std::span<const std::byte> InBytes) noexcept
{
    if (MSendFn != nullptr)
    {
        return MSendFn(MSendUser, InPlayer, InTo, InBytes);
    }
    return MLink.Send(InTo, InBytes);
}

void NetSession::ReceiveMessage(const NetAddress& InFrom, std::span<const std::byte> InPayload) noexcept
{
    if (IsClientRole() && InFrom == MConfig.Remote)
    {
        MLastServerMessageMs = NowMs();
    }
    HandleMessage(InFrom, InPayload);
}

void NetSession::Pump() noexcept
{
    if (!MStarted)
    {
        return;
    }
    (void)MLink.Poll(&NetSession::OnMessageThunk, this);
}

// The helpers above are the only place that knows the wire format; the rest of
// the session works with slots, ticks and events.

void NetSession::HandleMessage(const NetAddress& InFrom, std::span<const std::byte> InMessage) noexcept
{
    MsgType ScopedType{};
    if (!ReadHeader(InMessage, &ScopedType))
    {
        return;
    }

    if (IsClientRole() && !(InFrom == MConfig.Remote))
    {
        return;
    }
    if (IsAuthority())
    {
        const std::uint32_t ScopedSlot = FindSlotIndexByAddress(InFrom);
        if (ScopedType != MsgType::Hello && ScopedSlot == kSessionMaxPlayers)
        {
            return;
        }
        if (ScopedSlot < kSessionMaxPlayers)
        {
            MSlots[ScopedSlot].LastSeenMs = NowMs();
        }
    }

    switch (ScopedType)
    {
        case MsgType::Hello:
            HandleHello(InFrom, InMessage);
            break;
        case MsgType::Welcome:
            HandleWelcome(InMessage);
            break;
        case MsgType::Roster:
            HandleRoster(InMessage);
            break;
        case MsgType::TimePing:
            HandleTimePing(InFrom, InMessage);
            break;
        case MsgType::TimePong:
            HandleTimePong(InMessage);
            break;
        case MsgType::ServerSync:
            HandleServerSync(InMessage);
            break;
        case MsgType::Event:
            HandleEvent(InFrom, InMessage);
            break;
        case MsgType::EventAck:
            HandleEventAck(InFrom, InMessage);
            break;
        case MsgType::Bye:
            HandleBye(InFrom, InMessage);
            break;
        default:
            break;
    }
}

std::uint32_t NetSession::FindSlotIndexByAddress(const NetAddress& InAddress) const noexcept
{
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < kSessionMaxPlayers; ++ScopedIndex)
    {
        if (MSlots[ScopedIndex].Used && MSlots[ScopedIndex].Address == InAddress)
        {
            return ScopedIndex;
        }
    }
    return kSessionMaxPlayers;
}

std::uint32_t NetSession::AllocateSlot(const NetAddress& InAddress, const char* InName) noexcept
{
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < kSessionMaxPlayers; ++ScopedIndex)
    {
        if (MSlots[ScopedIndex].Used)
        {
            continue;
        }
        NetPlayerSlot& ScopedSlot = MSlots[ScopedIndex];
        ScopedSlot = NetPlayerSlot{};
        ScopedSlot.Used = true;
        ScopedSlot.Id = static_cast<std::uint8_t>(ScopedIndex);
        ScopedSlot.State = NetSlotState::Joined;
        ScopedSlot.Address = InAddress;
        ScopedSlot.JoinTick = MServerTick;
        ScopedSlot.LastSeenMs = NowMs();
        CopyNameInto(ScopedSlot.Name, InName);
        ++MPlayerCount;
        if (MCallbacks.OnSlotChanged != nullptr)
        {
            MCallbacks.OnSlotChanged(MCallbacks.User, ScopedSlot.Id, true);
        }
        return ScopedIndex;
    }
    return kSessionMaxPlayers;
}

void NetSession::FreeSlot(std::uint32_t InSlotIndex, NetDisconnectReason InReason) noexcept
{
    (void)InReason;
    if (InSlotIndex >= kSessionMaxPlayers || !MSlots[InSlotIndex].Used)
    {
        return;
    }
    const std::uint8_t ScopedId = MSlots[InSlotIndex].Id;
    MSlots[InSlotIndex] = NetPlayerSlot{};
    MRemotePending[InSlotIndex] = {};
    MLastClientSequence[InSlotIndex] = 0;
    MLastRemoteAck[InSlotIndex] = 0;
    if (MPlayerCount > 0)
    {
        --MPlayerCount;
    }
    if (MCallbacks.OnSlotChanged != nullptr)
    {
        MCallbacks.OnSlotChanged(MCallbacks.User, ScopedId, false);
    }
}

void NetSession::SendToSlot(const NetPlayerSlot& InSlot, std::span<const std::byte> InMessage) noexcept
{
    if (!InSlot.Used || (InSlot.Address.Host == 0 && InSlot.Address.Port == 0))
    {
        return;
    }
    (void)SendBytes(InSlot.Id, InSlot.Address, InMessage);
}

void NetSession::HandleHello(const NetAddress& InFrom, std::span<const std::byte> InPayload) noexcept
{
    if (!IsAuthority())
    {
        return;
    }
    char ScopedName[kSessionNameBytes]{};
    std::uint64_t ScopedNonce = 0;
    std::uint32_t ScopedCapabilities = 0;
    if (!DecodeHello(InPayload, ScopedName, &ScopedNonce, &ScopedCapabilities))
    {
        return;
    }
    (void)ScopedNonce;
    (void)ScopedCapabilities;

    std::uint32_t ScopedSlot = FindSlotIndexByAddress(InFrom);
    if (ScopedSlot >= kSessionMaxPlayers)
    {
        ScopedSlot = AllocateSlot(InFrom, ScopedName);
        if (ScopedSlot >= kSessionMaxPlayers)
        {
            std::array<std::byte, kSessionMessageBytes> ScopedBye{};
            const std::size_t ScopedBytes = EncodeBye(ScopedBye, kSessionNoPlayer, NetDisconnectReason::Rejected);
            if (ScopedBytes > 0)
            {
                (void)SendBytes(0xFF, InFrom, std::span<const std::byte>(ScopedBye.data(), ScopedBytes));
            }
            return;
        }
    }
    MSlots[ScopedSlot].LastSeenMs = NowMs();
    SendWelcome(ScopedSlot);
    SendRoster();
}

void NetSession::SendWelcome(std::uint32_t InSlotIndex) noexcept
{
    if (InSlotIndex >= kSessionMaxPlayers || !MSlots[InSlotIndex].Used)
    {
        return;
    }
    std::array<std::byte, kSessionMessageBytes> ScopedMessage{};
    const std::uint16_t ScopedTickRate =
        static_cast<std::uint16_t>(MConfig.FixedDtSeconds > 0.0f ? 1.0f / MConfig.FixedDtSeconds : 0.0f);
    const std::size_t ScopedBytes = EncodeWelcome(ScopedMessage, MSlots[InSlotIndex].Id, kSessionMaxPlayers,
                                                  MServerTick, MSessionNonce, ScopedTickRate);
    if (ScopedBytes > 0)
    {
        SendToSlot(MSlots[InSlotIndex], std::span<const std::byte>(ScopedMessage.data(), ScopedBytes));
    }
}

void NetSession::SendRoster() noexcept
{
    if (!IsAuthority())
    {
        return;
    }
    std::array<std::byte, kSessionMessageBytes> ScopedMessage{};
    const std::size_t ScopedBytes = EncodeRoster(ScopedMessage, MSlots, MServerTick);
    if (ScopedBytes == 0)
    {
        return;
    }
    for (const NetPlayerSlot& ScopedSlot : MSlots)
    {
        SendToSlot(ScopedSlot, std::span<const std::byte>(ScopedMessage.data(), ScopedBytes));
    }
    MLastRosterMs = NowMs();
}

void NetSession::SendServerSync() noexcept
{
    if (!IsAuthority())
    {
        return;
    }
    std::array<std::byte, kSessionMessageBytes> ScopedMessage{};
    const std::uint64_t ScopedNow = NowMs();
    const std::size_t ScopedBytes = EncodeServerSync(ScopedMessage, MServerTick, ScopedNow - MStartMs);
    if (ScopedBytes == 0)
    {
        return;
    }
    for (const NetPlayerSlot& ScopedSlot : MSlots)
    {
        SendToSlot(ScopedSlot, std::span<const std::byte>(ScopedMessage.data(), ScopedBytes));
    }
    MLastSyncMs = ScopedNow;
}

void NetSession::SendHello() noexcept
{
    std::array<std::byte, kSessionMessageBytes> ScopedMessage{};
    const std::size_t ScopedBytes = EncodeHello(ScopedMessage, MConfig.LocalName, MClientNonce, 0);
    if (ScopedBytes == 0)
    {
        return;
    }
    (void)SendBytes(0xFF, MConfig.Remote, std::span<const std::byte>(ScopedMessage.data(), ScopedBytes));
    MLastHelloMs = NowMs();
}

void NetSession::SendBye() noexcept
{
    std::array<std::byte, kSessionMessageBytes> ScopedMessage{};
    const std::size_t ScopedBytes = EncodeBye(ScopedMessage, MLocalPlayer, NetDisconnectReason::Bye);
    if (ScopedBytes == 0)
    {
        return;
    }
    (void)SendBytes(0xFF, MConfig.Remote, std::span<const std::byte>(ScopedMessage.data(), ScopedBytes));
}

void NetSession::HandleWelcome(std::span<const std::byte> InPayload) noexcept
{
    if (!IsClientRole() || MStatus != NetSessionStatus::Connecting)
    {
        return;
    }
    std::uint8_t ScopedPlayerId = kSessionNoPlayer;
    std::uint64_t ScopedServerTick = 0;
    std::uint64_t ScopedSessionNonce = 0;
    std::uint16_t ScopedTickRate = 0;
    if (!DecodeWelcome(InPayload, &ScopedPlayerId, &ScopedServerTick, &ScopedSessionNonce, &ScopedTickRate))
    {
        return;
    }
    if (ScopedPlayerId >= kSessionMaxPlayers)
    {
        return;
    }
    (void)ScopedTickRate;
    MLocalPlayer = ScopedPlayerId;
    MSessionNonce = ScopedSessionNonce;
    MConfirmedTick = ScopedServerTick;
    MLocalTick = ScopedServerTick;
    MTickAccumulator = 0.0f;
    SetStatus(NetSessionStatus::Joined);
    if (MCallbacks.OnJoined != nullptr)
    {
        MCallbacks.OnJoined(MCallbacks.User, MLocalPlayer, ScopedServerTick);
    }
}

void NetSession::HandleRoster(std::span<const std::byte> InPayload) noexcept
{
    if (!IsClientRole())
    {
        return;
    }
    std::uint8_t ScopedCount = 0;
    std::uint64_t ScopedServerTick = 0;
    std::array<NetPlayerSlot, kSessionMaxPlayers> ScopedSlots{};
    if (!DecodeRoster(InPayload, &ScopedCount, &ScopedServerTick, &ScopedSlots))
    {
        return;
    }
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < kSessionMaxPlayers; ++ScopedIndex)
    {
        const bool ScopedWas = MSlots[ScopedIndex].Used;
        const bool ScopedNow = ScopedSlots[ScopedIndex].Used;
        // The local slot keeps the address the client uses for sending.
        MSlots[ScopedIndex] = ScopedSlots[ScopedIndex];
        if (ScopedIndex == MLocalPlayer)
        {
            MSlots[ScopedIndex].Address = MConfig.Remote;
        }
        if (!ScopedWas && ScopedNow && MCallbacks.OnSlotChanged != nullptr)
        {
            MCallbacks.OnSlotChanged(MCallbacks.User, MSlots[ScopedIndex].Id, true);
        }
        else if (ScopedWas && !ScopedNow && MCallbacks.OnSlotChanged != nullptr)
        {
            MCallbacks.OnSlotChanged(MCallbacks.User, static_cast<std::uint8_t>(ScopedIndex), false);
        }
    }
    MPlayerCount = ScopedCount;
    if (ScopedServerTick > MConfirmedTick)
    {
        MConfirmedTick = ScopedServerTick;
    }
}

void NetSession::HandleTimePing(const NetAddress& InFrom, std::span<const std::byte> InPayload) noexcept
{
    if (!IsAuthority())
    {
        return;
    }
    std::uint64_t ScopedClientTime = 0;
    if (!DecodeTimePing(InPayload, &ScopedClientTime))
    {
        return;
    }
    std::array<std::byte, kSessionMessageBytes> ScopedMessage{};
    const std::size_t ScopedBytes =
        EncodeTimePong(ScopedMessage, ScopedClientTime, NowMs() - MStartMs, MServerTick);
    if (ScopedBytes > 0)
    {
        (void)SendBytes(0xFF, InFrom, std::span<const std::byte>(ScopedMessage.data(), ScopedBytes));
    }
}

void NetSession::HandleTimePong(std::span<const std::byte> InPayload) noexcept
{
    if (!IsClientRole())
    {
        return;
    }
    std::uint64_t ScopedClientTime = 0;
    std::uint64_t ScopedServerTime = 0;
    std::uint64_t ScopedServerTick = 0;
    if (!DecodeTimePong(InPayload, &ScopedClientTime, &ScopedServerTime, &ScopedServerTick))
    {
        return;
    }
    const std::uint64_t ScopedNow = NowMs();
    const std::uint64_t ScopedRtt = ScopedNow >= ScopedClientTime ? ScopedNow - ScopedClientTime : 0;
    MRttMs = static_cast<std::uint32_t>(ScopedRtt > 0xFFFFFFFFull ? 0xFFFFFFFFull : ScopedRtt);
    const std::int64_t ScopedOffset =
        static_cast<std::int64_t>(ScopedServerTime) + static_cast<std::int64_t>(ScopedRtt / 2) -
        static_cast<std::int64_t>(ScopedNow);
    MClockOffsetMs = (MClockOffsetMs * 3 + ScopedOffset) / 4;
    if (ScopedServerTick > MConfirmedTick)
    {
        MConfirmedTick = ScopedServerTick;
    }
    if (MCallbacks.OnTimeSync != nullptr)
    {
        MCallbacks.OnTimeSync(MCallbacks.User, MConfirmedTick, MRttMs);
    }
}

void NetSession::HandleServerSync(std::span<const std::byte> InPayload) noexcept
{
    if (!IsClientRole())
    {
        return;
    }
    std::uint64_t ScopedServerTick = 0;
    if (!DecodeServerSync(InPayload, &ScopedServerTick))
    {
        return;
    }
    if (ScopedServerTick > MConfirmedTick)
    {
        MConfirmedTick = ScopedServerTick;
    }
}

void NetSession::DeliverEvent(std::uint8_t InFromPlayer, std::uint32_t InEventId,
                              std::span<const std::byte> InPayload) noexcept
{
    if (MCallbacks.OnEvent != nullptr)
    {
        MCallbacks.OnEvent(MCallbacks.User, InFromPlayer, InEventId, InPayload);
    }
}

void NetSession::HandleEvent(const NetAddress& InFrom, std::span<const std::byte> InPayload) noexcept
{
    std::uint16_t ScopedSequence = 0;
    std::uint32_t ScopedEventId = 0;
    std::uint8_t ScopedFromPlayer = kSessionNoPlayer;
    std::span<const std::byte> ScopedPayload;
    if (!DecodeEvent(InPayload, &ScopedSequence, &ScopedEventId, &ScopedFromPlayer, &ScopedPayload))
    {
        return;
    }

    std::array<std::byte, kSessionMessageBytes> ScopedAck{};
    const std::size_t ScopedAckBytes = EncodeEventAck(ScopedAck, ScopedSequence);

    if (IsAuthority())
    {
        const std::uint32_t ScopedSlot = FindSlotIndexByAddress(InFrom);
        if (ScopedSlot >= kSessionMaxPlayers)
        {
            return;
        }
        if (SequenceIsNewer(ScopedSequence, MLastClientSequence[ScopedSlot]))
        {
            MLastClientSequence[ScopedSlot] = ScopedSequence;
            DeliverEvent(ScopedFromPlayer, ScopedEventId, ScopedPayload);
        }
        if (ScopedAckBytes > 0)
        {
            (void)SendBytes(0xFF, InFrom, std::span<const std::byte>(ScopedAck.data(), ScopedAckBytes));
        }
        return;
    }

    if (IsClientRole())
    {
        if (SequenceIsNewer(ScopedSequence, MLastReceivedAuthority))
        {
            MLastReceivedAuthority = ScopedSequence;
            DeliverEvent(ScopedFromPlayer, ScopedEventId, ScopedPayload);
        }
        if (ScopedAckBytes > 0)
        {
            (void)SendBytes(0xFF, MConfig.Remote, std::span<const std::byte>(ScopedAck.data(), ScopedAckBytes));
        }
    }
}

void NetSession::HandleEventAck(const NetAddress& InFrom, std::span<const std::byte> InPayload) noexcept
{
    std::uint16_t ScopedSequence = 0;
    if (!DecodeEventAck(InPayload, &ScopedSequence))
    {
        return;
    }
    if (IsAuthority())
    {
        const std::uint32_t ScopedSlot = FindSlotIndexByAddress(InFrom);
        if (ScopedSlot < kSessionMaxPlayers)
        {
            FlushAckForSlot(ScopedSlot, ScopedSequence);
        }
        return;
    }
    for (PendingEvent& ScopedEvent : MLocalPending)
    {
        if (ScopedEvent.Used && !SequenceIsNewer(ScopedEvent.Sequence, ScopedSequence))
        {
            ScopedEvent.Used = false;
        }
    }
}

void NetSession::HandleBye(const NetAddress& InFrom, std::span<const std::byte> InPayload) noexcept
{
    std::uint8_t ScopedPlayerId = kSessionNoPlayer;
    NetDisconnectReason ScopedReason = NetDisconnectReason::None;
    if (!DecodeBye(InPayload, &ScopedPlayerId, &ScopedReason))
    {
        return;
    }
    if (IsAuthority())
    {
        const std::uint32_t ScopedSlot = FindSlotIndexByAddress(InFrom);
        if (ScopedSlot < kSessionMaxPlayers)
        {
            FreeSlot(ScopedSlot, ScopedReason);
            SendRoster();
        }
        return;
    }
    if (IsClientRole() && (InFrom == MConfig.Remote))
    {
        SetStatus(NetSessionStatus::Disconnected, ScopedReason);
    }
}

bool NetSession::QueueEvent(std::uint32_t InSlotIndex, std::uint32_t InEventId,
                            std::span<const std::byte> InPayload) noexcept
{
    if (InPayload.size() > kSessionEventPayloadBytes)
    {
        return false;
    }
    if (InSlotIndex >= kSessionMaxPlayers)
    {
        return false;
    }
    for (PendingEvent& ScopedEvent : MRemotePending[InSlotIndex])
    {
        if (ScopedEvent.Used)
        {
            continue;
        }
        ScopedEvent.Used = true;
        ScopedEvent.Sequence = MNextRemoteSequence[InSlotIndex]++;
        ScopedEvent.EventId = InEventId;
        ScopedEvent.PayloadBytes = static_cast<std::uint16_t>(InPayload.size());
        ScopedEvent.Attempts = 1;
        ScopedEvent.LastSentMs = NowMs();
        if (!InPayload.empty())
        {
            std::memcpy(ScopedEvent.Payload.data(), InPayload.data(), InPayload.size());
        }
        SendPendingForSlot(InSlotIndex, ScopedEvent, true);
        return true;
    }
    return false;
}

void NetSession::SendPendingForSlot(std::uint32_t InSlotIndex, PendingEvent& InEvent, bool InForce) noexcept
{
    (void)InForce;
    if (InSlotIndex >= kSessionMaxPlayers || !MSlots[InSlotIndex].Used)
    {
        return;
    }
    std::array<std::byte, kSessionMessageBytes> ScopedMessage{};
    const std::size_t ScopedBytes =
        EncodeEvent(ScopedMessage, InEvent.Sequence, InEvent.EventId, MLocalPlayer,
                    std::span<const std::byte>(InEvent.Payload.data(), InEvent.PayloadBytes));
    if (ScopedBytes == 0)
    {
        return;
    }
    if (SendBytes(MSlots[InSlotIndex].Id, MSlots[InSlotIndex].Address, std::span<const std::byte>(ScopedMessage.data(), ScopedBytes)))
    {
        InEvent.LastSentMs = NowMs();
    }
}

void NetSession::FlushAckForSlot(std::uint32_t InSlotIndex, std::uint16_t InAck) noexcept
{
    if (InSlotIndex >= kSessionMaxPlayers)
    {
        return;
    }
    MLastRemoteAck[InSlotIndex] = InAck;
    for (PendingEvent& ScopedEvent : MRemotePending[InSlotIndex])
    {
        if (ScopedEvent.Used && !SequenceIsNewer(ScopedEvent.Sequence, InAck))
        {
            ScopedEvent.Used = false;
        }
    }
}

bool NetSession::SendEvent(std::uint32_t InEventId, std::span<const std::byte> InPayload) noexcept
{
    if (!MStarted || InPayload.size() > kSessionEventPayloadBytes)
    {
        return false;
    }
    const std::uint64_t ScopedNow = NowMs();

    if (IsAuthority())
    {
        bool ScopedAny = false;
        if (!MConfig.Headless)
        {
            DeliverEvent(MLocalPlayer, InEventId, InPayload);
            ScopedAny = true;
        }
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < kSessionMaxPlayers; ++ScopedIndex)
        {
            if (!MSlots[ScopedIndex].Used || (MSlots[ScopedIndex].Address.Host == 0 && MSlots[ScopedIndex].Address.Port == 0))
            {
                continue;
            }
            ScopedAny = QueueEvent(ScopedIndex, InEventId, InPayload) || ScopedAny;
        }
        return ScopedAny;
    }

    if (IsClientRole())
    {
        for (PendingEvent& ScopedEvent : MLocalPending)
        {
            if (ScopedEvent.Used)
            {
                continue;
            }
            ScopedEvent.Used = true;
            ScopedEvent.Sequence = MNextLocalSequence++;
            ScopedEvent.EventId = InEventId;
            ScopedEvent.PayloadBytes = static_cast<std::uint16_t>(InPayload.size());
            ScopedEvent.Attempts = 1;
            ScopedEvent.LastSentMs = ScopedNow;
            if (!InPayload.empty())
            {
                std::memcpy(ScopedEvent.Payload.data(), InPayload.data(), InPayload.size());
            }
            std::array<std::byte, kSessionMessageBytes> ScopedMessage{};
            const std::size_t ScopedBytes =
                EncodeEvent(ScopedMessage, ScopedEvent.Sequence, ScopedEvent.EventId, MLocalPlayer, InPayload);
            return ScopedBytes > 0 &&
                   SendBytes(0xFF, MConfig.Remote, std::span<const std::byte>(ScopedMessage.data(), ScopedBytes));
        }
    }
    return false;
}

void NetSession::ProcessEventTimers() noexcept
{
    const std::uint64_t ScopedNow = NowMs();
    if (IsAuthority())
    {
        for (std::uint32_t ScopedSlot = 0; ScopedSlot < kSessionMaxPlayers; ++ScopedSlot)
        {
            if (!MSlots[ScopedSlot].Used)
            {
                continue;
            }
            for (PendingEvent& ScopedEvent : MRemotePending[ScopedSlot])
            {
                if (!ScopedEvent.Used)
                {
                    continue;
                }
                if (ScopedEvent.Attempts >= MConfig.EventMaxAttempts)
                {
                    ScopedEvent.Used = false;
                    continue;
                }
                if (ScopedNow >= ScopedEvent.LastSentMs + MConfig.EventRetransmitMs)
                {
                    ++ScopedEvent.Attempts;
                    SendPendingForSlot(ScopedSlot, ScopedEvent, false);
                }
            }
        }
        return;
    }

    for (PendingEvent& ScopedEvent : MLocalPending)
    {
        if (!ScopedEvent.Used)
        {
            continue;
        }
        if (ScopedEvent.Attempts >= MConfig.EventMaxAttempts)
        {
            ScopedEvent.Used = false;
            continue;
        }
        if (ScopedNow >= ScopedEvent.LastSentMs + MConfig.EventRetransmitMs)
        {
            ++ScopedEvent.Attempts;
            std::array<std::byte, kSessionMessageBytes> ScopedMessage{};
            const std::size_t ScopedBytes =
                EncodeEvent(ScopedMessage, ScopedEvent.Sequence, ScopedEvent.EventId, MLocalPlayer,
                            std::span<const std::byte>(ScopedEvent.Payload.data(), ScopedEvent.PayloadBytes));
            if (ScopedBytes > 0)
            {
                (void)SendBytes(0xFF, MConfig.Remote, std::span<const std::byte>(ScopedMessage.data(), ScopedBytes));
                ScopedEvent.LastSentMs = ScopedNow;
            }
        }
    }
}

void NetSession::Advance(float InDeltaSeconds) noexcept
{
    if (!MStarted)
    {
        return;
    }
    MTicksToSimulate = 0;
    const float ScopedDt = InDeltaSeconds > 0.0f ? InDeltaSeconds : 0.0f;

    if (MRole == NetRole::Offline)
    {
        MTickAccumulator += ScopedDt;
        while (MTickAccumulator >= MConfig.FixedDtSeconds)
        {
            MTickAccumulator -= MConfig.FixedDtSeconds;
            ++MServerTick;
            ++MLocalTick;
            ++MTicksToSimulate;
        }
        return;
    }

    const std::uint64_t ScopedNow = NowMs();

    if (IsAuthority())
    {
        MTickAccumulator += ScopedDt;
        std::uint32_t ScopedSteps = 0;
        while (MTickAccumulator >= MConfig.FixedDtSeconds && ScopedSteps < 16)
        {
            MTickAccumulator -= MConfig.FixedDtSeconds;
            ++MServerTick;
            ++MTicksToSimulate;
            ++ScopedSteps;
        }
        MLocalTick = MServerTick;
        MConfirmedTick = MServerTick;

        if (ScopedNow >= MLastRosterMs + MConfig.RosterIntervalMs)
        {
            SendRoster();
        }
        if (ScopedNow >= MLastSyncMs + MConfig.ServerSyncIntervalMs)
        {
            SendServerSync();
        }
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < kSessionMaxPlayers; ++ScopedIndex)
        {
            NetPlayerSlot& ScopedSlot = MSlots[ScopedIndex];
            if (!ScopedSlot.Used)
            {
                continue;
            }
            if (ScopedSlot.Address.Host == 0 && ScopedSlot.Address.Port == 0)
            {
                continue;
            }
            if (MConfig.TimeoutMs > 0 && ScopedNow > ScopedSlot.LastSeenMs + MConfig.TimeoutMs)
            {
                FreeSlot(ScopedIndex, NetDisconnectReason::Timeout);
                SendRoster();
            }
        }
        ProcessEventTimers();
        return;
    }

    if (IsClientRole())
    {
        if (!Joined())
        {
            if (ScopedNow >= MLastHelloMs + MConfig.RejoinIntervalMs)
            {
                SendHello();
            }
            ProcessEventTimers();
            return;
        }

        if (MConfig.TickMode == NetTickMode::Predicted)
        {
            MTickAccumulator += ScopedDt;
            std::uint32_t ScopedSteps = 0;
            while (MTickAccumulator >= MConfig.FixedDtSeconds && ScopedSteps < 16)
            {
                MTickAccumulator -= MConfig.FixedDtSeconds;
                ++MLocalTick;
                ++MTicksToSimulate;
                ++ScopedSteps;
            }
            const std::uint64_t ScopedTarget = MConfirmedTick + MConfig.PredictionLeadTicks;
            if (MLocalTick < ScopedTarget)
            {
                const std::uint64_t ScopedJump = ScopedTarget - MLocalTick;
                MTicksToSimulate += static_cast<std::uint32_t>(std::min<std::uint64_t>(ScopedJump, 8));
                MLocalTick = ScopedTarget;
            }
            else if (MLocalTick > ScopedTarget + MConfig.PredictionMaxDriftTicks)
            {
                MLocalTick = ScopedTarget;
                MTickAccumulator = 0.0f;
            }
        }
        else
        {
            while (MLocalTick < MConfirmedTick)
            {
                ++MLocalTick;
                ++MTicksToSimulate;
            }
        }

        if (MConfig.TimeoutMs > 0 && ScopedNow > MLastServerMessageMs + MConfig.TimeoutMs)
        {
            SetStatus(NetSessionStatus::Disconnected, NetDisconnectReason::Timeout);
            return;
        }

        if (ScopedNow >= MLastPingSentMs + MConfig.TimePingIntervalMs)
        {
            std::array<std::byte, kSessionMessageBytes> ScopedMessage{};
            const std::size_t ScopedBytes = EncodeTimePing(ScopedMessage, ScopedNow);
            if (ScopedBytes > 0)
            {
                (void)SendBytes(0xFF, MConfig.Remote, std::span<const std::byte>(ScopedMessage.data(), ScopedBytes));
                MLastPingSentMs = ScopedNow;
            }
        }
        ProcessEventTimers();
        return;
    }

    // Peer role: the deterministic lockstep transport is a later step, but the
    // local tick still advances so game code can run the same loop.
    MTickAccumulator += ScopedDt;
    while (MTickAccumulator >= MConfig.FixedDtSeconds)
    {
        MTickAccumulator -= MConfig.FixedDtSeconds;
        ++MLocalTick;
        ++MTicksToSimulate;
    }
}

const NetPlayerSlot* NetSession::FindPlayer(std::uint8_t InPlayerId) const noexcept
{
    for (const NetPlayerSlot& ScopedSlot : MSlots)
    {
        if (ScopedSlot.Used && ScopedSlot.Id == InPlayerId)
        {
            return &ScopedSlot;
        }
    }
    return nullptr;
}
}
