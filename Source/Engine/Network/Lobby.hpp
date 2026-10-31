// FemboyEngine2D - lobby protocol: rooms, ticket issuance and client join.
#pragma once

#include "Engine/Network/NetLink.hpp"
#include "Engine/Network/Ticket.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace Fby::Net
{

struct NetSessionConfig;

inline constexpr std::uint32_t kLobbyMagic = 0x424F4C46u;
inline constexpr std::uint16_t kLobbyVersion = 1;
inline constexpr std::uint32_t kLobbyMaxRooms = 8;
inline constexpr std::uint32_t kLobbyMaxPlayers = 8;
inline constexpr std::size_t kLobbyNameBytes = 32;
inline constexpr std::size_t kLobbyMessageBytes = 320;
inline constexpr std::uint8_t kLobbyNoPlayer = 0xFF;

enum class LobbyMessageType : std::uint8_t
{
    Create = 1,
    CreateAck = 2,
    Join = 3,
    JoinAck = 4,
    Start = 5,
    StartAck = 6,
    Leave = 7,
    Error = 8,
};

enum class LobbyError : std::uint8_t
{
    None = 0,
    NoRooms,
    NoSuchRoom,
    RoomFull,
    AlreadyStarted,
    BadRequest,
    TicketFailed,
    NotReady,
};

[[nodiscard]] constexpr const char* LobbyErrorName(LobbyError InError) noexcept
{
    switch (InError)
    {
        case LobbyError::None:
            return "None";
        case LobbyError::NoRooms:
            return "NoRooms";
        case LobbyError::NoSuchRoom:
            return "NoSuchRoom";
        case LobbyError::RoomFull:
            return "RoomFull";
        case LobbyError::AlreadyStarted:
            return "AlreadyStarted";
        case LobbyError::BadRequest:
            return "BadRequest";
        case LobbyError::TicketFailed:
            return "TicketFailed";
        case LobbyError::NotReady:
            return "NotReady";
    }
    return "?";
}

struct LobbyRoomInfo
{
    std::uint32_t RoomId = 0;
    std::uint8_t Players = 0;
    std::uint8_t MaxPlayers = 0;
    bool Started = false;
    char Name[kLobbyNameBytes]{};
};

struct LobbyServerConfig
{
    NetAddress GameServer{};
    std::array<std::byte, Crypto::kX25519KeyBytes> GameServerPublicKey{};
    std::uint64_t TicketLifetimeSeconds = 300;
    std::uint8_t MaxPlayersPerRoom = kLobbyMaxPlayers;
    char GameName[kLobbyNameBytes] = "FemboyEngine2D";
};

// Owns the room table and signs join tickets with the lobby identity. One
// instance serves every connection through the same link.
class LobbyServer
{
public:
    LobbyServer() = default;

    [[nodiscard]] bool Start(const NetLink& InLink, const Crypto::EddsaKeyPair& InIssuer,
                             const LobbyServerConfig& InConfig) noexcept;
    void Stop() noexcept;

    void Tick(std::uint64_t InNowUnixSeconds) noexcept;

    [[nodiscard]] std::uint32_t RoomCount() const noexcept;
    [[nodiscard]] const LobbyRoomInfo* RoomAt(std::uint32_t InIndex) const noexcept;
    [[nodiscard]] const Crypto::EddsaKeyPair& Issuer() const noexcept
    {
        return MIssuer;
    }

protected:
    static void OnMessageThunk(void* InUser, const NetAddress& InFrom,
                               std::span<const std::byte> InMessage) noexcept;
    void HandleMessage(const NetAddress& InFrom, std::span<const std::byte> InMessage) noexcept;
    void HandleCreate(const NetAddress& InFrom, std::span<const std::byte> InMessage) noexcept;
    void HandleJoin(const NetAddress& InFrom, std::span<const std::byte> InMessage) noexcept;
    void HandleStart(const NetAddress& InFrom, std::span<const std::byte> InMessage) noexcept;
    void HandleLeave(const NetAddress& InFrom, std::span<const std::byte> InMessage) noexcept;

    void SendAck(const NetAddress& InTo, std::uint32_t InRoomId, std::uint8_t InPlayerId,
                 bool InStarted) noexcept;
    void SendError(const NetAddress& InTo, LobbyError InError) noexcept;
    [[nodiscard]] std::uint32_t FindRoom(std::uint32_t InRoomId) const noexcept;
    [[nodiscard]] std::uint8_t JoinMember(std::uint32_t InRoomIndex, const NetAddress& InAddress) noexcept;

    struct Member
    {
        NetAddress Address{};
        bool Used = false;
    };

    struct Room
    {
        bool Used = false;
        std::uint32_t Id = 0;
        std::uint8_t MaxPlayers = kLobbyMaxPlayers;
        bool Started = false;
        char Name[kLobbyNameBytes]{};
        std::array<Member, kLobbyMaxPlayers> Members{};
    };

    NetLink MLink{};
    Crypto::EddsaKeyPair MIssuer{};
    LobbyServerConfig MConfig{};
    std::array<Room, kLobbyMaxRooms> MRooms{};
    std::uint32_t MNextRoomId = 1;
    std::uint64_t MNow = 0;
    bool MStarted = false;
};

struct NetLobbyClientConfig
{
    std::array<std::byte, Crypto::kEddsaPublicBytes> LobbyPublicKey{};
    char PlayerName[kLobbyNameBytes] = "Player";
    std::uint8_t MaxPlayers = kLobbyMaxPlayers;
};

// Client side: create or join a room, then hold the ticket the game feeds into
// NetSessionConfig (Hybrid mode). The ticket signature is verified locally.
class NetLobbyClient
{
public:
    NetLobbyClient() = default;

    [[nodiscard]] bool Start(const NetLink& InLink, const NetAddress& InLobby,
                             const NetLobbyClientConfig& InConfig) noexcept;
    void Stop() noexcept;

    [[nodiscard]] bool CreateRoom(const char* InName) noexcept;
    [[nodiscard]] bool JoinRoom(std::uint32_t InRoomId) noexcept;
    [[nodiscard]] bool StartMatch() noexcept;

    void Tick(std::uint64_t InNowUnixSeconds) noexcept;

    [[nodiscard]] bool Ready() const noexcept
    {
        return MReady;
    }
    [[nodiscard]] LobbyError Error() const noexcept
    {
        return MError;
    }
    [[nodiscard]] std::uint32_t RoomId() const noexcept
    {
        return MRoomId;
    }
    [[nodiscard]] std::uint8_t PlayerId() const noexcept
    {
        return MPlayerId;
    }
    [[nodiscard]] const NetTicket& Ticket() const noexcept
    {
        return MTicket;
    }
    [[nodiscard]] const std::array<std::byte, kTicketBytes>& SignedTicket() const noexcept
    {
        return MSignedTicket;
    }
    [[nodiscard]] NetAddress Server() const noexcept
    {
        return MServer;
    }
    [[nodiscard]] bool MatchStarted() const noexcept
    {
        return MStarted;
    }

    // Fills a Hybrid session config from the issued ticket and the assigned
    // server. Returns false before the match started.
    [[nodiscard]] bool ApplyToSessionConfig(NetSessionConfig* OutConfig) const noexcept;

protected:
    static void OnMessageThunk(void* InUser, const NetAddress& InFrom,
                               std::span<const std::byte> InMessage) noexcept;
    void HandleMessage(const NetAddress& InFrom, std::span<const std::byte> InMessage) noexcept;
    [[nodiscard]] bool SendRequest(LobbyMessageType InType, std::uint32_t InRoomId, const char* InName) noexcept;

    NetLink MLink{};
    NetAddress MLobby{};
    NetLobbyClientConfig MConfig{};
    NetTicket MTicket{};
    std::array<std::byte, kTicketBytes> MSignedTicket{};
    NetAddress MServer{};
    std::uint32_t MRoomId = 0;
    std::uint8_t MPlayerId = kLobbyNoPlayer;
    std::uint64_t MNonce = 0;
    std::uint64_t MNow = 0;
    LobbyError MError = LobbyError::None;
    bool MReady = false;
    bool MStarted = false;
};
}
