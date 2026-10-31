// FemboyEngine2D - signed join ticket implementation.
#include "Engine/Network/Ticket.hpp"

#include "Engine/Network/ByteOrder.hpp"

#include <cstring>

namespace Fby::Net
{
namespace
{
constexpr std::size_t kVersionOffset = 4;
constexpr std::size_t kFlagsOffset = 6;
constexpr std::size_t kPlayerOffset = 7;
constexpr std::size_t kMatchOffset = 8;
constexpr std::size_t kIssuedOffset = 16;
constexpr std::size_t kExpiresOffset = 24;
constexpr std::size_t kHostOffset = 32;
constexpr std::size_t kPortOffset = 36;
constexpr std::size_t kProtocolOffset = 38;
constexpr std::size_t kCapabilitiesOffset = 40;
constexpr std::size_t kRoleOffset = 44;
constexpr std::size_t kReservedOffset = 45;
constexpr std::size_t kNameOffset = 46;
constexpr std::size_t kServerKeyOffset = 78;

static_assert(kServerKeyOffset + Crypto::kX25519KeyBytes == kTicketBodyBytes,
              "ticket body layout must fill exactly kTicketBodyBytes");

constexpr char kInvitePrefix[] = "FBYINV1:";
constexpr std::size_t kInvitePrefixBytes = sizeof(kInvitePrefix) - 1;
constexpr char kBase64Alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

[[nodiscard]] std::uint8_t* BytesOf(std::span<std::byte> InBytes) noexcept
{
    return reinterpret_cast<std::uint8_t*>(InBytes.data());
}

[[nodiscard]] const std::uint8_t* BytesOf(std::span<const std::byte> InBytes) noexcept
{
    return reinterpret_cast<const std::uint8_t*>(InBytes.data());
}

// Rejects a ticket whose fields a verifier could never accept.
[[nodiscard]] TicketStatus ValidateFields(const NetTicket& InTicket) noexcept
{
    if (InTicket.Role > kTicketRoleObserver)
    {
        return TicketStatus::BadRole;
    }
    if (InTicket.PlayerId >= kTicketMaxPlayers)
    {
        return TicketStatus::BadPlayerId;
    }
    std::size_t ScopedNameLength = 0;
    while (ScopedNameLength < kTicketNameBytes && InTicket.PlayerName[ScopedNameLength] != '\0')
    {
        ++ScopedNameLength;
    }
    if (ScopedNameLength == 0 || ScopedNameLength == kTicketNameBytes)
    {
        return TicketStatus::BadName;
    }
    return TicketStatus::Ok;
}

[[nodiscard]] int DecodeBase64Char(char InChar) noexcept
{
    if (InChar >= 'A' && InChar <= 'Z')
    {
        return InChar - 'A';
    }
    if (InChar >= 'a' && InChar <= 'z')
    {
        return InChar - 'a' + 26;
    }
    if (InChar >= '0' && InChar <= '9')
    {
        return InChar - '0' + 52;
    }
    if (InChar == '-')
    {
        return 62;
    }
    if (InChar == '_')
    {
        return 63;
    }
    return -1;
}
}

bool EncodeTicketBody(const NetTicket& InTicket, std::span<std::byte> OutBytes) noexcept
{
    if (OutBytes.size() != kTicketBodyBytes || ValidateFields(InTicket) != TicketStatus::Ok)
    {
        return false;
    }
    std::memset(OutBytes.data(), 0, OutBytes.size());

    std::uint8_t* ScopedBytes = BytesOf(OutBytes);
    Detail::StoreU32(ScopedBytes, kTicketMagic);
    Detail::StoreU16(ScopedBytes + kVersionOffset, kTicketVersion);
    ScopedBytes[kFlagsOffset] = InTicket.Flags;
    ScopedBytes[kPlayerOffset] = InTicket.PlayerId;
    Detail::StoreU64(ScopedBytes + kMatchOffset, InTicket.MatchId);
    Detail::StoreU64(ScopedBytes + kIssuedOffset, InTicket.IssuedAt);
    Detail::StoreU64(ScopedBytes + kExpiresOffset, InTicket.ExpiresAt);
    Detail::StoreU32(ScopedBytes + kHostOffset, InTicket.Host);
    Detail::StoreU16(ScopedBytes + kPortOffset, InTicket.Port);
    Detail::StoreU16(ScopedBytes + kProtocolOffset, InTicket.ProtocolVersion);
    Detail::StoreU32(ScopedBytes + kCapabilitiesOffset, InTicket.Capabilities);
    ScopedBytes[kRoleOffset] = InTicket.Role;
    ScopedBytes[kReservedOffset] = 0;
    std::memcpy(ScopedBytes + kNameOffset, InTicket.PlayerName, kTicketNameBytes);
    std::memcpy(ScopedBytes + kServerKeyOffset, InTicket.ServerPublicKey.data(), Crypto::kX25519KeyBytes);
    return true;
}

bool DecodeTicketBody(std::span<const std::byte> InBytes, NetTicket* OutTicket) noexcept
{
    if (OutTicket == nullptr || InBytes.size() != kTicketBodyBytes)
    {
        return false;
    }

    const std::uint8_t* ScopedBytes = BytesOf(InBytes);
    if (Detail::LoadU32(ScopedBytes) != kTicketMagic)
    {
        return false;
    }
    if (Detail::LoadU16(ScopedBytes + kVersionOffset) != kTicketVersion)
    {
        return false;
    }

    NetTicket ScopedTicket{};
    ScopedTicket.Flags = ScopedBytes[kFlagsOffset];
    ScopedTicket.PlayerId = ScopedBytes[kPlayerOffset];
    ScopedTicket.MatchId = Detail::LoadU64(ScopedBytes + kMatchOffset);
    ScopedTicket.IssuedAt = Detail::LoadU64(ScopedBytes + kIssuedOffset);
    ScopedTicket.ExpiresAt = Detail::LoadU64(ScopedBytes + kExpiresOffset);
    ScopedTicket.Host = Detail::LoadU32(ScopedBytes + kHostOffset);
    ScopedTicket.Port = Detail::LoadU16(ScopedBytes + kPortOffset);
    ScopedTicket.ProtocolVersion = Detail::LoadU16(ScopedBytes + kProtocolOffset);
    ScopedTicket.Capabilities = Detail::LoadU32(ScopedBytes + kCapabilitiesOffset);
    ScopedTicket.Role = ScopedBytes[kRoleOffset];
    std::memcpy(ScopedTicket.PlayerName, ScopedBytes + kNameOffset, kTicketNameBytes);
    std::memcpy(ScopedTicket.ServerPublicKey.data(), ScopedBytes + kServerKeyOffset, Crypto::kX25519KeyBytes);
    if (ValidateFields(ScopedTicket) != TicketStatus::Ok)
    {
        return false;
    }
    *OutTicket = ScopedTicket;
    return true;
}

bool SignTicket(const NetTicket& InTicket, const Crypto::EddsaKeyPair& InIssuer,
                std::span<std::byte> OutSigned) noexcept
{
    if (OutSigned.size() != kTicketBytes)
    {
        return false;
    }
    const std::span<std::byte> ScopedBody = OutSigned.first(kTicketBodyBytes);
    if (!EncodeTicketBody(InTicket, ScopedBody))
    {
        std::memset(OutSigned.data(), 0, OutSigned.size());
        return false;
    }
    Crypto::EddsaSign(InIssuer.Secret, ScopedBody, OutSigned.subspan(kTicketBodyBytes));
    return true;
}

TicketStatus VerifyTicket(std::span<const std::byte> InSigned, std::span<const std::byte> InIssuerPublic,
                          std::uint64_t InNowUnixSeconds, NetTicket* OutTicket) noexcept
{
    if (InSigned.size() != kTicketBytes || InIssuerPublic.size() != Crypto::kEddsaPublicBytes)
    {
        return TicketStatus::BadSize;
    }
    const std::uint8_t* ScopedBytes = BytesOf(InSigned);
    if (Detail::LoadU32(ScopedBytes) != kTicketMagic)
    {
        return TicketStatus::BadMagic;
    }
    if (Detail::LoadU16(ScopedBytes + kVersionOffset) != kTicketVersion)
    {
        return TicketStatus::BadVersion;
    }

    const std::span<const std::byte> ScopedBody = InSigned.first(kTicketBodyBytes);
    NetTicket ScopedTicket{};
    if (!DecodeTicketBody(ScopedBody, &ScopedTicket))
    {
        // Magic and version already matched, so the rejection names a field.
        if (ScopedBytes[kRoleOffset] > kTicketRoleObserver)
        {
            return TicketStatus::BadRole;
        }
        if (ScopedBytes[kPlayerOffset] >= kTicketMaxPlayers)
        {
            return TicketStatus::BadPlayerId;
        }
        return TicketStatus::BadName;
    }
    if (!Crypto::EddsaVerify(InIssuerPublic, ScopedBody, InSigned.subspan(kTicketBodyBytes)))
    {
        return TicketStatus::BadSignature;
    }

    if (ScopedTicket.ExpiresAt != 0 && InNowUnixSeconds >= ScopedTicket.ExpiresAt)
    {
        return TicketStatus::Expired;
    }
    if (ScopedTicket.IssuedAt != 0 && ScopedTicket.IssuedAt > InNowUnixSeconds + kTicketClockSkewSeconds)
    {
        return TicketStatus::NotYetValid;
    }

    if (OutTicket != nullptr)
    {
        *OutTicket = ScopedTicket;
    }
    return TicketStatus::Ok;
}

bool EncodeInviteCode(std::span<const std::byte> InSignedTicket, std::span<char> OutCode,
                      std::size_t* OutSize) noexcept
{
    if (InSignedTicket.size() != kTicketBytes)
    {
        return false;
    }
    const std::size_t ScopedGroups = InSignedTicket.size() / 3;
    const std::size_t ScopedRemainder = InSignedTicket.size() % 3;
    const std::size_t ScopedEncoded = ScopedGroups * 4 + (ScopedRemainder == 1 ? 2 : (ScopedRemainder == 2 ? 3 : 0));
    const std::size_t ScopedTotal = kInvitePrefixBytes + ScopedEncoded;
    if (OutCode.size() < ScopedTotal)
    {
        return false;
    }

    std::memcpy(OutCode.data(), kInvitePrefix, kInvitePrefixBytes);
    std::size_t ScopedOut = kInvitePrefixBytes;
    std::size_t ScopedIn = 0;
    while (ScopedIn + 3 <= InSignedTicket.size())
    {
        const std::uint32_t ScopedValue = (static_cast<std::uint32_t>(InSignedTicket[ScopedIn]) << 16) |
                                          (static_cast<std::uint32_t>(InSignedTicket[ScopedIn + 1]) << 8) |
                                          static_cast<std::uint32_t>(InSignedTicket[ScopedIn + 2]);
        OutCode[ScopedOut++] = kBase64Alphabet[(ScopedValue >> 18) & 0x3Fu];
        OutCode[ScopedOut++] = kBase64Alphabet[(ScopedValue >> 12) & 0x3Fu];
        OutCode[ScopedOut++] = kBase64Alphabet[(ScopedValue >> 6) & 0x3Fu];
        OutCode[ScopedOut++] = kBase64Alphabet[ScopedValue & 0x3Fu];
        ScopedIn += 3;
    }
    if (ScopedRemainder == 1)
    {
        const std::uint32_t ScopedValue = static_cast<std::uint32_t>(InSignedTicket[ScopedIn]) << 16;
        OutCode[ScopedOut++] = kBase64Alphabet[(ScopedValue >> 18) & 0x3Fu];
        OutCode[ScopedOut++] = kBase64Alphabet[(ScopedValue >> 12) & 0x3Fu];
    }
    else if (ScopedRemainder == 2)
    {
        const std::uint32_t ScopedValue = (static_cast<std::uint32_t>(InSignedTicket[ScopedIn]) << 16) |
                                          (static_cast<std::uint32_t>(InSignedTicket[ScopedIn + 1]) << 8);
        OutCode[ScopedOut++] = kBase64Alphabet[(ScopedValue >> 18) & 0x3Fu];
        OutCode[ScopedOut++] = kBase64Alphabet[(ScopedValue >> 12) & 0x3Fu];
        OutCode[ScopedOut++] = kBase64Alphabet[(ScopedValue >> 6) & 0x3Fu];
    }

    if (OutSize != nullptr)
    {
        *OutSize = ScopedOut;
    }
    return true;
}

bool DecodeInviteCode(std::string_view InCode, std::span<std::byte> OutSignedTicket, std::size_t* OutSize) noexcept
{
    if (InCode.size() < kInvitePrefixBytes || InCode.size() > kInviteCodeCapacity)
    {
        return false;
    }
    if (InCode.substr(0, kInvitePrefixBytes) != kInvitePrefix)
    {
        return false;
    }

    std::array<std::byte, kTicketBytes> ScopedBuffer{};
    std::size_t ScopedOut = 0;
    std::uint32_t ScopedAccumulator = 0;
    std::uint32_t ScopedBits = 0;
    for (std::size_t ScopedIndex = kInvitePrefixBytes; ScopedIndex < InCode.size(); ++ScopedIndex)
    {
        const int ScopedValue = DecodeBase64Char(InCode[ScopedIndex]);
        if (ScopedValue < 0)
        {
            return false;
        }
        ScopedAccumulator = (ScopedAccumulator << 6) | static_cast<std::uint32_t>(ScopedValue);
        ScopedBits += 6;
        if (ScopedBits >= 8)
        {
            ScopedBits -= 8;
            const auto ScopedByte = static_cast<std::byte>((ScopedAccumulator >> ScopedBits) & 0xFFu);
            if (ScopedOut < ScopedBuffer.size())
            {
                ScopedBuffer[ScopedOut++] = ScopedByte;
            }
            else
            {
                return false;
            }
        }
    }

    // Non-zero trailing bits mean the code was re-encoded by hand; reject it
    // instead of accepting a second spelling of the same ticket.
    if (ScopedBits > 0 && (ScopedAccumulator & ((1u << ScopedBits) - 1u)) != 0)
    {
        return false;
    }
    if (ScopedOut != kTicketBytes || OutSignedTicket.size() < kTicketBytes)
    {
        return false;
    }

    std::memcpy(OutSignedTicket.data(), ScopedBuffer.data(), kTicketBytes);
    if (OutSize != nullptr)
    {
        *OutSize = kTicketBytes;
    }
    return true;
}
}
