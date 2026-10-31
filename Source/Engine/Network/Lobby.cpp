// FemboyEngine2D - lobby protocol implementation.
#include "Engine/Network/Lobby.hpp"

#include "Engine/Network/Session.hpp"

#include "Engine/Core/SecureRng.hpp"
#include "Engine/Network/ByteOrder.hpp"

#include <cstring>

namespace Fby::Net
{
namespace
{
constexpr std::size_t kHeaderBytes = 8;
constexpr std::size_t kCreateBytes = kHeaderBytes + kLobbyNameBytes + 1 + 1 + 8;
constexpr std::size_t kJoinBytes = kHeaderBytes + 4 + kLobbyNameBytes + 8;
constexpr std::size_t kRoomIdBytes = kHeaderBytes + 4;
constexpr std::size_t kAckBytes = kHeaderBytes + 4 + 1 + 1 + 1 + 1 + 4 + 2 + 2 + kTicketBytes;
constexpr std::size_t kStartAckBytes = kHeaderBytes + 4 + 1 + 3;
constexpr std::size_t kErrorBytes = kHeaderBytes + 1 + 3;

[[nodiscard]] std::uint8_t* BytesOf(std::span<std::byte> InBytes) noexcept
{
    return reinterpret_cast<std::uint8_t*>(InBytes.data());
}

[[nodiscard]] const std::uint8_t* BytesOf(std::span<const std::byte> InBytes) noexcept
{
    return reinterpret_cast<const std::uint8_t*>(InBytes.data());
}

bool WriteHeader(std::span<std::byte> OutBytes, LobbyMessageType InType) noexcept
{
    if (OutBytes.size() < kHeaderBytes)
    {
        return false;
    }
    std::uint8_t* ScopedOut = BytesOf(OutBytes);
    std::memset(ScopedOut, 0, kHeaderBytes);
    Detail::StoreU32(ScopedOut, kLobbyMagic);
    Detail::StoreU16(ScopedOut + 4, kLobbyVersion);
    ScopedOut[6] = static_cast<std::uint8_t>(InType);
    return true;
}

[[nodiscard]] bool ReadHeader(std::span<const std::byte> InBytes, LobbyMessageType* OutType) noexcept
{
    if (InBytes.size() < kHeaderBytes)
    {
        return false;
    }
    const std::uint8_t* ScopedIn = BytesOf(InBytes);
    if (Detail::LoadU32(ScopedIn) != kLobbyMagic || Detail::LoadU16(ScopedIn + 4) != kLobbyVersion)
    {
        return false;
    }
    const std::uint8_t ScopedType = ScopedIn[6];
    if (ScopedType < static_cast<std::uint8_t>(LobbyMessageType::Create) ||
        ScopedType > static_cast<std::uint8_t>(LobbyMessageType::Error))
    {
        return false;
    }
    *OutType = static_cast<LobbyMessageType>(ScopedType);
    return true;
}

void StoreName(std::uint8_t* OutBytes, const char* InName) noexcept
{
    std::memset(OutBytes, 0, kLobbyNameBytes);
    if (InName == nullptr)
    {
        return;
    }
    std::size_t ScopedLength = 0;
    while (ScopedLength + 1 < kLobbyNameBytes && InName[ScopedLength] != '\0')
    {
        OutBytes[ScopedLength] = static_cast<std::uint8_t>(InName[ScopedLength]);
        ++ScopedLength;
    }
}
}

bool LobbyServer::Start(const NetLink& InLink, const Crypto::EddsaKeyPair& InIssuer,
                        const LobbyServerConfig& InConfig) noexcept
{
    Stop();
    if (!InLink.Valid() || InConfig.GameServer.Port == 0 || InConfig.MaxPlayersPerRoom == 0 ||
        InConfig.MaxPlayersPerRoom > kLobbyMaxPlayers)
    {
        return false;
    }
    MLink = InLink;
    MIssuer = InIssuer;
    MConfig = InConfig;
    MStarted = true;
    return true;
}

void LobbyServer::Stop() noexcept
{
    MLink = NetLink{};
    MIssuer = Crypto::EddsaKeyPair{};
    MConfig = LobbyServerConfig{};
    MRooms = {};
    MNextRoomId = 1;
    MNow = 0;
    MStarted = false;
}

void LobbyServer::Tick(std::uint64_t InNowUnixSeconds) noexcept
{
    if (!MStarted)
    {
        return;
    }
    MNow = InNowUnixSeconds;
    (void)MLink.Poll(&LobbyServer::OnMessageThunk, this);
}

void LobbyServer::OnMessageThunk(void* InUser, const NetAddress& InFrom,
                                 std::span<const std::byte> InMessage) noexcept
{
    auto* ScopedServer = static_cast<LobbyServer*>(InUser);
    if (ScopedServer != nullptr)
    {
        ScopedServer->HandleMessage(InFrom, InMessage);
    }
}

void LobbyServer::HandleMessage(const NetAddress& InFrom, std::span<const std::byte> InMessage) noexcept
{
    LobbyMessageType ScopedType{};
    if (!ReadHeader(InMessage, &ScopedType))
    {
        return;
    }
    switch (ScopedType)
    {
        case LobbyMessageType::Create:
            HandleCreate(InFrom, InMessage);
            break;
        case LobbyMessageType::Join:
            HandleJoin(InFrom, InMessage);
            break;
        case LobbyMessageType::Start:
            HandleStart(InFrom, InMessage);
            break;
        case LobbyMessageType::Leave:
            HandleLeave(InFrom, InMessage);
            break;
        default:
            break;
    }
}

std::uint32_t LobbyServer::FindRoom(std::uint32_t InRoomId) const noexcept
{
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < kLobbyMaxRooms; ++ScopedIndex)
    {
        if (MRooms[ScopedIndex].Used && MRooms[ScopedIndex].Id == InRoomId)
        {
            return ScopedIndex;
        }
    }
    return kLobbyMaxRooms;
}

std::uint8_t LobbyServer::JoinMember(std::uint32_t InRoomIndex, const NetAddress& InAddress) noexcept
{
    if (InRoomIndex >= kLobbyMaxRooms || !MRooms[InRoomIndex].Used)
    {
        return kLobbyNoPlayer;
    }
    Room& ScopedRoom = MRooms[InRoomIndex];
    std::uint8_t ScopedMembers = 0;
    for (std::uint8_t ScopedId = 0; ScopedId < ScopedRoom.MaxPlayers; ++ScopedId)
    {
        if (ScopedRoom.Members[ScopedId].Used && ScopedRoom.Members[ScopedId].Address == InAddress)
        {
            return ScopedId;
        }
        ScopedMembers = static_cast<std::uint8_t>(ScopedMembers + (ScopedRoom.Members[ScopedId].Used ? 1 : 0));
    }
    if (ScopedMembers >= ScopedRoom.MaxPlayers)
    {
        return kLobbyNoPlayer;
    }
    for (std::uint8_t ScopedId = 0; ScopedId < ScopedRoom.MaxPlayers; ++ScopedId)
    {
        if (!ScopedRoom.Members[ScopedId].Used)
        {
            ScopedRoom.Members[ScopedId].Used = true;
            ScopedRoom.Members[ScopedId].Address = InAddress;
            return ScopedId;
        }
    }
    return kLobbyNoPlayer;
}

void LobbyServer::HandleCreate(const NetAddress& InFrom, std::span<const std::byte> InMessage) noexcept
{
    if (InMessage.size() != kCreateBytes)
    {
        SendError(InFrom, LobbyError::BadRequest);
        return;
    }
    const std::uint8_t* ScopedIn = BytesOf(InMessage) + kHeaderBytes;
    const std::uint8_t ScopedMaxPlayers = ScopedIn[kLobbyNameBytes];
    std::uint32_t ScopedIndex = kLobbyMaxRooms;
    for (std::uint32_t ScopedRoom = 0; ScopedRoom < kLobbyMaxRooms; ++ScopedRoom)
    {
        if (!MRooms[ScopedRoom].Used)
        {
            ScopedIndex = ScopedRoom;
            break;
        }
    }
    if (ScopedIndex >= kLobbyMaxRooms)
    {
        SendError(InFrom, LobbyError::NoRooms);
        return;
    }

    Room& ScopedRoom = MRooms[ScopedIndex];
    ScopedRoom = Room{};
    ScopedRoom.Used = true;
    ScopedRoom.Id = MNextRoomId++;
    ScopedRoom.MaxPlayers = ScopedMaxPlayers == 0 ? 1 : (ScopedMaxPlayers > kLobbyMaxPlayers ? kLobbyMaxPlayers
                                                                                              : ScopedMaxPlayers);
    std::memcpy(ScopedRoom.Name, ScopedIn, kLobbyNameBytes);
    ScopedRoom.Name[kLobbyNameBytes - 1] = '\0';

    const std::uint8_t ScopedPlayerId = JoinMember(ScopedIndex, InFrom);
    if (ScopedPlayerId == kLobbyNoPlayer)
    {
        MRooms[ScopedIndex] = Room{};
        SendError(InFrom, LobbyError::RoomFull);
        return;
    }
    SendAck(InFrom, ScopedRoom.Id, ScopedPlayerId, false);
}

void LobbyServer::HandleJoin(const NetAddress& InFrom, std::span<const std::byte> InMessage) noexcept
{
    if (InMessage.size() != kJoinBytes)
    {
        SendError(InFrom, LobbyError::BadRequest);
        return;
    }
    const std::uint8_t* ScopedIn = BytesOf(InMessage) + kHeaderBytes;
    const std::uint32_t ScopedRoomId = Detail::LoadU32(ScopedIn);
    const std::uint32_t ScopedRoomIndex = FindRoom(ScopedRoomId);
    if (ScopedRoomIndex >= kLobbyMaxRooms)
    {
        SendError(InFrom, LobbyError::NoSuchRoom);
        return;
    }
    Room& ScopedRoom = MRooms[ScopedRoomIndex];
    if (ScopedRoom.Started)
    {
        SendError(InFrom, LobbyError::AlreadyStarted);
        return;
    }
    const std::uint8_t ScopedPlayerId = JoinMember(ScopedRoomIndex, InFrom);
    if (ScopedPlayerId == kLobbyNoPlayer)
    {
        SendError(InFrom, LobbyError::RoomFull);
        return;
    }
    SendAck(InFrom, ScopedRoom.Id, ScopedPlayerId, false);
}

void LobbyServer::HandleStart(const NetAddress& InFrom, std::span<const std::byte> InMessage) noexcept
{
    if (InMessage.size() != kRoomIdBytes)
    {
        SendError(InFrom, LobbyError::BadRequest);
        return;
    }
    const std::uint32_t ScopedRoomId = Detail::LoadU32(BytesOf(InMessage) + kHeaderBytes);
    const std::uint32_t ScopedRoomIndex = FindRoom(ScopedRoomId);
    if (ScopedRoomIndex >= kLobbyMaxRooms)
    {
        SendError(InFrom, LobbyError::NoSuchRoom);
        return;
    }
    Room& ScopedRoom = MRooms[ScopedRoomIndex];
    if (!ScopedRoom.Members[0].Used || !(ScopedRoom.Members[0].Address == InFrom))
    {
        SendError(InFrom, LobbyError::NotReady);
        return;
    }
    ScopedRoom.Started = true;

    std::array<std::byte, kLobbyMessageBytes> ScopedAck{};
    if (!WriteHeader(ScopedAck, LobbyMessageType::StartAck))
    {
        return;
    }
    std::uint8_t* ScopedOut = BytesOf(std::span<std::byte>(ScopedAck)) + kHeaderBytes;
    Detail::StoreU32(ScopedOut, ScopedRoom.Id);
    ScopedOut[4] = 1;
    const std::span<const std::byte> ScopedBytes(ScopedAck.data(), kStartAckBytes);
    for (const Member& ScopedMember : ScopedRoom.Members)
    {
        if (ScopedMember.Used)
        {
            (void)MLink.Send(ScopedMember.Address, ScopedBytes);
        }
    }
}

void LobbyServer::HandleLeave(const NetAddress& InFrom, std::span<const std::byte> InMessage) noexcept
{
    if (InMessage.size() != kRoomIdBytes)
    {
        return;
    }
    const std::uint32_t ScopedRoomId = Detail::LoadU32(BytesOf(InMessage) + kHeaderBytes);
    const std::uint32_t ScopedRoomIndex = FindRoom(ScopedRoomId);
    if (ScopedRoomIndex >= kLobbyMaxRooms)
    {
        return;
    }
    Room& ScopedRoom = MRooms[ScopedRoomIndex];
    bool ScopedAny = false;
    for (Member& ScopedMember : ScopedRoom.Members)
    {
        if (ScopedMember.Used && ScopedMember.Address == InFrom)
        {
            ScopedMember = Member{};
        }
        ScopedAny = ScopedAny || ScopedMember.Used;
    }
    if (!ScopedAny)
    {
        MRooms[ScopedRoomIndex] = Room{};
        return;
    }
    // The host leaving ends the match; otherwise the room stays joinable.
    if (!ScopedRoom.Members[0].Used)
    {
        MRooms[ScopedRoomIndex] = Room{};
    }
}

void LobbyServer::SendAck(const NetAddress& InTo, std::uint32_t InRoomId, std::uint8_t InPlayerId,
                          bool InStarted) noexcept
{
    NetTicket ScopedTicket{};
    ScopedTicket.MatchId = InRoomId;
    ScopedTicket.IssuedAt = MNow;
    ScopedTicket.ExpiresAt = MConfig.TicketLifetimeSeconds == 0 ? 0 : MNow + MConfig.TicketLifetimeSeconds;
    ScopedTicket.Host = MConfig.GameServer.Host;
    ScopedTicket.Port = MConfig.GameServer.Port;
    ScopedTicket.ProtocolVersion = 1;
    ScopedTicket.PlayerId = InPlayerId;
    ScopedTicket.Role = InPlayerId == 0 ? kTicketRoleHost : kTicketRoleClient;
    ScopedTicket.Flags = kTicketFlagDedicated;
    std::memcpy(ScopedTicket.ServerPublicKey.data(), MConfig.GameServerPublicKey.data(),
                MConfig.GameServerPublicKey.size());
    std::memcpy(ScopedTicket.PlayerName, MConfig.GameName, kTicketNameBytes - 1);

    std::array<std::byte, kTicketBytes> ScopedSigned{};
    if (!SignTicket(ScopedTicket, MIssuer, ScopedSigned))
    {
        SendError(InTo, LobbyError::TicketFailed);
        return;
    }

    std::array<std::byte, kLobbyMessageBytes> ScopedAck{};
    if (!WriteHeader(ScopedAck, LobbyMessageType::CreateAck))
    {
        return;
    }
    std::uint8_t* ScopedOut = BytesOf(std::span<std::byte>(ScopedAck)) + kHeaderBytes;
    Detail::StoreU32(ScopedOut, InRoomId);
    ScopedOut[4] = InPlayerId;
    ScopedOut[5] = kLobbyMaxPlayers;
    ScopedOut[6] = InStarted ? 1 : 0;
    ScopedOut[7] = 0;
    Detail::StoreU32(ScopedOut + 8, MConfig.GameServer.Host);
    Detail::StoreU16(ScopedOut + 12, MConfig.GameServer.Port);
    Detail::StoreU16(ScopedOut + 14, 0);
    std::memcpy(ScopedOut + 16, ScopedSigned.data(), ScopedSigned.size());
    (void)MLink.Send(InTo, std::span<const std::byte>(ScopedAck.data(), kAckBytes));
}

void LobbyServer::SendError(const NetAddress& InTo, LobbyError InError) noexcept
{
    std::array<std::byte, kLobbyMessageBytes> ScopedError{};
    if (!WriteHeader(ScopedError, LobbyMessageType::Error))
    {
        return;
    }
    BytesOf(std::span<std::byte>(ScopedError))[kHeaderBytes] = static_cast<std::uint8_t>(InError);
    (void)MLink.Send(InTo, std::span<const std::byte>(ScopedError.data(), kErrorBytes));
}

std::uint32_t LobbyServer::RoomCount() const noexcept
{
    std::uint32_t ScopedCount = 0;
    for (const Room& ScopedRoom : MRooms)
    {
        ScopedCount += ScopedRoom.Used ? 1u : 0u;
    }
    return ScopedCount;
}

const LobbyRoomInfo* LobbyServer::RoomAt(std::uint32_t InIndex) const noexcept
{
    static thread_local LobbyRoomInfo ScopedInfo;
    if (InIndex >= kLobbyMaxRooms || !MRooms[InIndex].Used)
    {
        return nullptr;
    }
    const Room& ScopedRoom = MRooms[InIndex];
    ScopedInfo = LobbyRoomInfo{};
    ScopedInfo.RoomId = ScopedRoom.Id;
    ScopedInfo.MaxPlayers = ScopedRoom.MaxPlayers;
    ScopedInfo.Started = ScopedRoom.Started;
    for (const Member& ScopedMember : ScopedRoom.Members)
    {
        ScopedInfo.Players = static_cast<std::uint8_t>(ScopedInfo.Players + (ScopedMember.Used ? 1 : 0));
    }
    std::memcpy(ScopedInfo.Name, ScopedRoom.Name, kLobbyNameBytes);
    return &ScopedInfo;
}

bool NetLobbyClient::Start(const NetLink& InLink, const NetAddress& InLobby,
                           const NetLobbyClientConfig& InConfig) noexcept
{
    Stop();
    if (!InLink.Valid() || InLobby.Port == 0)
    {
        return false;
    }
    MLink = InLink;
    MLobby = InLobby;
    MConfig = InConfig;
    if (!SecureRng::NextU64(MNonce))
    {
        MLink = NetLink{};
        return false;
    }
    return true;
}

bool NetLobbyClient::ApplyToSessionConfig(NetSessionConfig* OutConfig) const noexcept
{
    if (OutConfig == nullptr || !MStarted || MServer.Port == 0)
    {
        return false;
    }
    OutConfig->Mode = NetSessionMode::Hybrid;
    OutConfig->HasAssignedServer = true;
    OutConfig->AssignedServer = MServer;
    OutConfig->Remote = MServer;
    OutConfig->HostRequested = MTicket.Role == kTicketRoleHost;
    std::memcpy(OutConfig->LocalName, MTicket.PlayerName, kSessionNameBytes);
    OutConfig->LocalName[kSessionNameBytes - 1] = ' ';
    return true;
}

void NetLobbyClient::Stop() noexcept
{
    MLink = NetLink{};
    MLobby = NetAddress{};
    MConfig = NetLobbyClientConfig{};
    MTicket = NetTicket{};
    MSignedTicket = {};
    MServer = NetAddress{};
    MRoomId = 0;
    MPlayerId = kLobbyNoPlayer;
    MNonce = 0;
    MNow = 0;
    MError = LobbyError::None;
    MReady = false;
    MStarted = false;
}

bool NetLobbyClient::SendRequest(LobbyMessageType InType, std::uint32_t InRoomId, const char* InName) noexcept
{
    if (!MLink.Valid())
    {
        return false;
    }
    std::array<std::byte, kLobbyMessageBytes> ScopedMessage{};
    if (!WriteHeader(ScopedMessage, InType))
    {
        return false;
    }
    std::uint8_t* ScopedOut = BytesOf(std::span<std::byte>(ScopedMessage)) + kHeaderBytes;
    std::size_t ScopedBytes = kHeaderBytes;
    if (InType == LobbyMessageType::Create || InType == LobbyMessageType::Join)
    {
        if (InType == LobbyMessageType::Join)
        {
            Detail::StoreU32(ScopedOut, InRoomId);
            ScopedOut += 4;
            ScopedBytes += 4;
        }
        StoreName(ScopedOut, InName);
        ScopedOut += kLobbyNameBytes;
        ScopedBytes += kLobbyNameBytes;
        if (InType == LobbyMessageType::Create)
        {
            ScopedOut[0] = MConfig.MaxPlayers;
            ScopedOut[1] = 0;
            ScopedOut += 2;
            ScopedBytes += 2;
        }
        Detail::StoreU64(ScopedOut, MNonce);
        ScopedBytes += 8;
    }
    else
    {
        Detail::StoreU32(ScopedOut, InRoomId);
        ScopedBytes += 4;
    }
    return MLink.Send(MLobby, std::span<const std::byte>(ScopedMessage.data(), ScopedBytes));
}

bool NetLobbyClient::CreateRoom(const char* InName) noexcept
{
    MError = LobbyError::None;
    MReady = false;
    return SendRequest(LobbyMessageType::Create, 0, InName);
}

bool NetLobbyClient::JoinRoom(std::uint32_t InRoomId) noexcept
{
    MError = LobbyError::None;
    MReady = false;
    return SendRequest(LobbyMessageType::Join, InRoomId, MConfig.PlayerName);
}

bool NetLobbyClient::StartMatch() noexcept
{
    if (!MReady)
    {
        MError = LobbyError::NotReady;
        return false;
    }
    return SendRequest(LobbyMessageType::Start, MRoomId, nullptr);
}

void NetLobbyClient::Tick(std::uint64_t InNowUnixSeconds) noexcept
{
    MNow = InNowUnixSeconds;
    if (MLink.Valid())
    {
        (void)MLink.Poll(&NetLobbyClient::OnMessageThunk, this);
    }
}

void NetLobbyClient::OnMessageThunk(void* InUser, const NetAddress& InFrom,
                                    std::span<const std::byte> InMessage) noexcept
{
    auto* ScopedClient = static_cast<NetLobbyClient*>(InUser);
    if (ScopedClient != nullptr)
    {
        ScopedClient->HandleMessage(InFrom, InMessage);
    }
}

void NetLobbyClient::HandleMessage(const NetAddress& InFrom, std::span<const std::byte> InMessage) noexcept
{
    if (!(InFrom == MLobby))
    {
        return;
    }
    LobbyMessageType ScopedType{};
    if (!ReadHeader(InMessage, &ScopedType))
    {
        return;
    }
    if (ScopedType == LobbyMessageType::Error)
    {
        if (InMessage.size() != kErrorBytes)
        {
            return;
        }
        MError = static_cast<LobbyError>(BytesOf(InMessage)[kHeaderBytes]);
        MReady = false;
        return;
    }
    if (ScopedType == LobbyMessageType::StartAck)
    {
        if (InMessage.size() != kStartAckBytes)
        {
            return;
        }
        const std::uint32_t ScopedRoomId = Detail::LoadU32(BytesOf(InMessage) + kHeaderBytes);
        if (ScopedRoomId == MRoomId || MRoomId == 0)
        {
            MStarted = BytesOf(InMessage)[kHeaderBytes + 4] != 0;
        }
        return;
    }
    if (ScopedType != LobbyMessageType::CreateAck && ScopedType != LobbyMessageType::JoinAck)
    {
        return;
    }
    if (InMessage.size() != kAckBytes)
    {
        return;
    }

    const std::uint8_t* ScopedIn = BytesOf(InMessage) + kHeaderBytes;
    const std::uint32_t ScopedRoomId = Detail::LoadU32(ScopedIn);
    const std::uint8_t ScopedPlayerId = ScopedIn[4];
    NetAddress ScopedServer{};
    ScopedServer.Host = Detail::LoadU32(ScopedIn + 8);
    ScopedServer.Port = Detail::LoadU16(ScopedIn + 12);
    const std::span<const std::byte> ScopedTicketBytes(
        reinterpret_cast<const std::byte*>(ScopedIn + 16), kTicketBytes);

    NetTicket ScopedTicket{};
    if (VerifyTicket(ScopedTicketBytes, MConfig.LobbyPublicKey, MNow, &ScopedTicket) != TicketStatus::Ok)
    {
        MError = LobbyError::TicketFailed;
        MReady = false;
        return;
    }
    if (ScopedTicket.PlayerId != ScopedPlayerId)
    {
        MError = LobbyError::TicketFailed;
        MReady = false;
        return;
    }

    std::memcpy(MSignedTicket.data(), ScopedTicketBytes.data(), kTicketBytes);
    MTicket = ScopedTicket;
    MServer = ScopedServer;
    MRoomId = ScopedRoomId;
    MPlayerId = ScopedPlayerId;
    MError = LobbyError::None;
    MReady = true;
}
}
