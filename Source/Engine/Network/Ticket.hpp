// FemboyEngine2D - signed join tickets and invite codes.
#pragma once

#include "Engine/Core/CryptoAsymmetric.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>

namespace Fby::Net
{

inline constexpr std::uint32_t kTicketMagic = 0x31544B46u;
inline constexpr std::uint16_t kTicketVersion = 1;
inline constexpr std::uint32_t kTicketMaxPlayers = 8;
inline constexpr std::size_t kTicketNameBytes = 32;
inline constexpr std::size_t kTicketBodyBytes = 110;
inline constexpr std::size_t kTicketBytes = kTicketBodyBytes + Crypto::kEddsaSignatureBytes;
inline constexpr std::size_t kInviteCodeCapacity = 256;
// IssuedAt may lead the verifier clock by this much before it counts as forged.
inline constexpr std::uint64_t kTicketClockSkewSeconds = 300;

inline constexpr std::uint8_t kTicketRoleClient = 0;
inline constexpr std::uint8_t kTicketRoleHost = 1;
inline constexpr std::uint8_t kTicketRoleObserver = 2;

inline constexpr std::uint32_t kTicketFlagLan = 1u << 0;
inline constexpr std::uint32_t kTicketFlagDedicated = 1u << 1;

// The body a lobby signs. `ExpiresAt == 0` means the ticket never expires,
// which is only reasonable for a local or LAN match.
struct NetTicket
{
    std::uint64_t MatchId = 0;
    std::uint64_t IssuedAt = 0;
    std::uint64_t ExpiresAt = 0;
    std::uint32_t Host = 0;
    std::uint32_t Capabilities = 0;
    std::uint16_t Port = 0;
    std::uint16_t ProtocolVersion = 0;
    std::uint8_t PlayerId = 0xFF;
    std::uint8_t Role = kTicketRoleClient;
    std::uint8_t Flags = 0;
    std::array<std::byte, Crypto::kX25519KeyBytes> ServerPublicKey{};
    char PlayerName[kTicketNameBytes]{};
};

static_assert(std::is_trivially_copyable_v<NetTicket>);
static_assert(std::is_standard_layout_v<NetTicket>);

enum class TicketStatus : std::uint8_t
{
    Ok = 0,
    BadSize,
    BadMagic,
    BadVersion,
    BadSignature,
    BadPlayerId,
    BadRole,
    BadName,
    BadProtocol,
    Expired,
    NotYetValid,
};

[[nodiscard]] constexpr const char* TicketStatusName(TicketStatus InStatus) noexcept
{
    switch (InStatus)
    {
        case TicketStatus::Ok:
            return "Ok";
        case TicketStatus::BadSize:
            return "BadSize";
        case TicketStatus::BadMagic:
            return "BadMagic";
        case TicketStatus::BadVersion:
            return "BadVersion";
        case TicketStatus::BadSignature:
            return "BadSignature";
        case TicketStatus::BadPlayerId:
            return "BadPlayerId";
        case TicketStatus::BadRole:
            return "BadRole";
        case TicketStatus::BadName:
            return "BadName";
        case TicketStatus::BadProtocol:
            return "BadProtocol";
        case TicketStatus::Expired:
            return "Expired";
        case TicketStatus::NotYetValid:
            return "NotYetValid";
    }
    return "?";
}

// Encodes the unsigned body. The output span must be exactly kTicketBodyBytes
// and the body must be field-valid; an invalid ticket is never encoded.
[[nodiscard]] bool EncodeTicketBody(const NetTicket& InTicket, std::span<std::byte> OutBytes) noexcept;
[[nodiscard]] bool DecodeTicketBody(std::span<const std::byte> InBytes, NetTicket* OutTicket) noexcept;

// Signs the body with the issuer's EdDSA key. The output is
// kTicketBodyBytes followed by the kEddsaSignatureBytes signature.
[[nodiscard]] bool SignTicket(const NetTicket& InTicket, const Crypto::EddsaKeyPair& InIssuer,
                              std::span<std::byte> OutSigned) noexcept;

// Verifies size, magic, version, signature, field validity, expiry and clock
// skew. `OutTicket` receives the decoded body only on Ok.
[[nodiscard]] TicketStatus VerifyTicket(std::span<const std::byte> InSigned,
                                        std::span<const std::byte> InIssuerPublic,
                                        std::uint64_t InNowUnixSeconds, NetTicket* OutTicket) noexcept;

// Invite text: "FBYINV1:" followed by the unpadded base64url form of the signed
// ticket. `OutSize` receives the character count, excluding a terminator.
[[nodiscard]] bool EncodeInviteCode(std::span<const std::byte> InSignedTicket, std::span<char> OutCode,
                                    std::size_t* OutSize) noexcept;
[[nodiscard]] bool DecodeInviteCode(std::string_view InCode, std::span<std::byte> OutSignedTicket,
                                    std::size_t* OutSize) noexcept;
}
