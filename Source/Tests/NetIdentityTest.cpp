// FemboyEngine2D - identity record, signed ticket and invite code tests.
#include "Engine/Network/Identity.hpp"
#include "Engine/Network/Ticket.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
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

[[nodiscard]] std::array<std::byte, 32> FilledSeed(std::uint8_t InValue)
{
    std::array<std::byte, 32> ScopedSeed{};
    ScopedSeed.fill(static_cast<std::byte>(InValue));
    return ScopedSeed;
}

[[nodiscard]] Fby::Crypto::EddsaKeyPair MakeIssuer(std::uint8_t InValue)
{
    Fby::Crypto::EddsaKeyPair ScopedPair{};
    const std::array<std::byte, 32> ScopedSeed = FilledSeed(InValue);
    (void)Fby::Crypto::EddsaKeyPairFromSeed(ScopedSeed, &ScopedPair);
    return ScopedPair;
}

void SetName(char* OutName, std::string_view InName)
{
    std::memset(OutName, 0, Fby::Net::kTicketNameBytes);
    const std::size_t ScopedCount =
        InName.size() < Fby::Net::kTicketNameBytes - 1 ? InName.size() : Fby::Net::kTicketNameBytes - 1;
    std::memcpy(OutName, InName.data(), ScopedCount);
}

void TestIdentityRecord()
{
    using namespace Fby::Net;

    NetIdentity ScopedIdentity{};
    FBY_CHECK(GenerateIdentity("Hostess of the Lobby", &ScopedIdentity));
    FBY_CHECK(ScopedIdentity.Valid());
    FBY_CHECK(std::string_view(ScopedIdentity.Name) == "Hostess of the Lobby");
    FBY_CHECK(ScopedIdentity.CreatedUnixSeconds > 0);

    std::array<std::byte, kIdentityFileBytes> ScopedBytes{};
    FBY_CHECK(EncodeIdentity(ScopedIdentity, ScopedBytes));

    NetIdentity ScopedDecoded{};
    FBY_CHECK(DecodeIdentity(ScopedBytes, &ScopedDecoded));
    FBY_CHECK(ScopedDecoded.Exchange.Public == ScopedIdentity.Exchange.Public);
    FBY_CHECK(ScopedDecoded.Signing.Public == ScopedIdentity.Signing.Public);
    FBY_CHECK(ScopedDecoded.CreatedUnixSeconds == ScopedIdentity.CreatedUnixSeconds);

    std::error_code ScopedError;
    const std::filesystem::path ScopedPath = std::filesystem::current_path(ScopedError) / "fby_identity_test.bin";
    std::filesystem::remove(ScopedPath, ScopedError);

    std::string ScopedMessage;
    FBY_CHECK(SaveIdentity(ScopedIdentity, ScopedPath, &ScopedMessage));
    FBY_CHECK(std::filesystem::is_regular_file(ScopedPath, ScopedError));
    const std::uintmax_t ScopedSavedBytes = std::filesystem::file_size(ScopedPath, ScopedError);
    FBY_CHECK(ScopedSavedBytes > kIdentityStorageHeaderBytes);
    {
        std::ifstream ScopedHeaderInput(ScopedPath, std::ios::binary);
        std::array<char, 4> ScopedMagicBytes{};
        ScopedHeaderInput.read(ScopedMagicBytes.data(), 4);
        const auto* ScopedRaw = reinterpret_cast<const std::uint8_t*>(ScopedMagicBytes.data());
        const std::uint32_t ScopedMagic = static_cast<std::uint32_t>(ScopedRaw[0]) |
                                          (static_cast<std::uint32_t>(ScopedRaw[1]) << 8) |
                                          (static_cast<std::uint32_t>(ScopedRaw[2]) << 16) |
                                          (static_cast<std::uint32_t>(ScopedRaw[3]) << 24);
        FBY_CHECK(ScopedMagic == kIdentityStorageMagic);
    }

    NetIdentity ScopedLoaded{};
    FBY_CHECK(LoadIdentity(ScopedPath, &ScopedLoaded, &ScopedMessage));
    FBY_CHECK(ScopedLoaded.Exchange.Secret == ScopedIdentity.Exchange.Secret);

#if !defined(_WIN32)
    const std::filesystem::perms ScopedPerms = std::filesystem::status(ScopedPath, ScopedError).permissions();
    FBY_CHECK((ScopedPerms & std::filesystem::perms::group_read) == std::filesystem::perms::none);
    FBY_CHECK((ScopedPerms & std::filesystem::perms::others_read) == std::filesystem::perms::none);
#endif

    // One flipped payload byte must fail the digest (or the DPAPI authentication).
    std::vector<std::byte> ScopedFileBytes;
    {
        std::ifstream ScopedInput(ScopedPath, std::ios::binary);
        ScopedInput.seekg(0, std::ios::end);
        const std::streamoff ScopedEnd = ScopedInput.tellg();
        ScopedInput.seekg(0, std::ios::beg);
        if (ScopedEnd > 0)
        {
            ScopedFileBytes.resize(static_cast<std::size_t>(ScopedEnd));
            ScopedInput.read(reinterpret_cast<char*>(ScopedFileBytes.data()), ScopedEnd);
        }
    }
    FBY_CHECK(ScopedFileBytes.size() > kIdentityStorageHeaderBytes);
    ScopedFileBytes.back() ^= std::byte{0x01};
    {
        std::ofstream ScopedOutput(ScopedPath, std::ios::binary | std::ios::trunc);
        ScopedOutput.write(reinterpret_cast<const char*>(ScopedFileBytes.data()),
                           static_cast<std::streamsize>(ScopedFileBytes.size()));
    }
    FBY_CHECK(!LoadIdentity(ScopedPath, &ScopedLoaded, &ScopedMessage));
    std::filesystem::remove(ScopedPath, ScopedError);

    // A record whose keys do not agree is rejected even with a valid digest.
    NetIdentity ScopedMismatch = ScopedIdentity;
    ScopedMismatch.Exchange.Public[0] ^= std::byte{0x01};
    std::array<std::byte, kIdentityFileBytes> ScopedMismatchBytes{};
    FBY_CHECK(EncodeIdentity(ScopedMismatch, ScopedMismatchBytes));
    FBY_CHECK(!DecodeIdentity(ScopedMismatchBytes, &ScopedDecoded));
    FBY_CHECK(!ScopedMismatch.Valid());

    // Long names are truncated to kIdentityNameBytes - 1 characters.
    NetIdentity ScopedLongName{};
    FBY_CHECK(GenerateIdentity("a name that is definitely longer than thirty two bytes", &ScopedLongName));
    FBY_CHECK(std::strlen(ScopedLongName.Name) == kIdentityNameBytes - 1);
}

void TestTicketSigning()
{
    using namespace Fby::Net;

    const Fby::Crypto::EddsaKeyPair ScopedIssuer = MakeIssuer(0x21);
    const Fby::Crypto::EddsaKeyPair ScopedImpostor = MakeIssuer(0x22);

    NetTicket ScopedTicket{};
    ScopedTicket.MatchId = 0xDEADBEEFCAFEull;
    ScopedTicket.IssuedAt = 1000;
    ScopedTicket.ExpiresAt = 2000;
    ScopedTicket.Host = 0x7F000001u;
    ScopedTicket.Port = 7777;
    ScopedTicket.ProtocolVersion = 1;
    ScopedTicket.Capabilities = 0x3;
    ScopedTicket.PlayerId = 2;
    ScopedTicket.Role = kTicketRoleHost;
    ScopedTicket.Flags = kTicketFlagLan | kTicketFlagDedicated;
    SetName(ScopedTicket.PlayerName, "Host");
    ScopedTicket.ServerPublicKey[0] = std::byte{0x42};

    std::array<std::byte, kTicketBytes> ScopedSigned{};
    FBY_CHECK(SignTicket(ScopedTicket, ScopedIssuer, ScopedSigned));

    NetTicket ScopedVerified{};
    FBY_CHECK(VerifyTicket(ScopedSigned, ScopedIssuer.Public, 1500, &ScopedVerified) == TicketStatus::Ok);
    FBY_CHECK(ScopedVerified.MatchId == ScopedTicket.MatchId);
    FBY_CHECK(ScopedVerified.PlayerId == ScopedTicket.PlayerId);
    FBY_CHECK(ScopedVerified.Role == ScopedTicket.Role);
    FBY_CHECK(ScopedVerified.Port == ScopedTicket.Port);
    FBY_CHECK(ScopedVerified.ServerPublicKey == ScopedTicket.ServerPublicKey);

    FBY_CHECK(VerifyTicket(ScopedSigned, ScopedIssuer.Public, 2000, nullptr) == TicketStatus::Expired);
    FBY_CHECK(VerifyTicket(ScopedSigned, ScopedIssuer.Public, 500, nullptr) == TicketStatus::NotYetValid);
    FBY_CHECK(VerifyTicket(ScopedSigned, ScopedImpostor.Public, 1500, nullptr) == TicketStatus::BadSignature);

    std::array<std::byte, kTicketBytes> ScopedTampered = ScopedSigned;
    ScopedTampered[10] ^= std::byte{0x01};
    FBY_CHECK(VerifyTicket(ScopedTampered, ScopedIssuer.Public, 1500, nullptr) == TicketStatus::BadSignature);

    FBY_CHECK(VerifyTicket(std::span<const std::byte>(ScopedSigned.data(), 100), ScopedIssuer.Public, 1500, nullptr) ==
              TicketStatus::BadSize);

    // Field validation rejects tickets a session could never accept.
    NetTicket ScopedBadRole = ScopedTicket;
    ScopedBadRole.Role = 9;
    std::array<std::byte, kTicketBodyBytes> ScopedBody{};
    FBY_CHECK(!EncodeTicketBody(ScopedBadRole, ScopedBody));

    NetTicket ScopedBadPlayer = ScopedTicket;
    ScopedBadPlayer.PlayerId = kTicketMaxPlayers;
    FBY_CHECK(!EncodeTicketBody(ScopedBadPlayer, ScopedBody));

    NetTicket ScopedEmptyName = ScopedTicket;
    std::memset(ScopedEmptyName.PlayerName, 0, kTicketNameBytes);
    FBY_CHECK(!EncodeTicketBody(ScopedEmptyName, ScopedBody));

    std::array<std::byte, kTicketBytes> ScopedRejected{};
    FBY_CHECK(!SignTicket(ScopedBadRole, ScopedIssuer, ScopedRejected));
    bool ScopedAllZero = true;
    for (const std::byte ScopedByte : ScopedRejected)
    {
        ScopedAllZero = ScopedAllZero && ScopedByte == std::byte{0};
    }
    FBY_CHECK(ScopedAllZero);

    // A zero body is not a ticket.
    std::array<std::byte, kTicketBodyBytes> ScopedZeroBody{};
    NetTicket ScopedDecoded{};
    FBY_CHECK(!DecodeTicketBody(ScopedZeroBody, &ScopedDecoded));
}

void TestInviteCode()
{
    using namespace Fby::Net;

    const Fby::Crypto::EddsaKeyPair ScopedIssuer = MakeIssuer(0x31);

    NetTicket ScopedTicket{};
    ScopedTicket.MatchId = 7;
    ScopedTicket.IssuedAt = 100;
    ScopedTicket.ExpiresAt = 1000;
    ScopedTicket.Host = 0x0A000001u;
    ScopedTicket.Port = 5000;
    ScopedTicket.ProtocolVersion = 1;
    ScopedTicket.PlayerId = 1;
    ScopedTicket.Role = kTicketRoleClient;
    SetName(ScopedTicket.PlayerName, "Guest");

    std::array<std::byte, kTicketBytes> ScopedSigned{};
    FBY_CHECK(SignTicket(ScopedTicket, ScopedIssuer, ScopedSigned));

    std::array<char, kInviteCodeCapacity> ScopedCode{};
    std::size_t ScopedCodeSize = 0;
    FBY_CHECK(EncodeInviteCode(ScopedSigned, ScopedCode, &ScopedCodeSize));
    FBY_CHECK(ScopedCodeSize > 8 && ScopedCodeSize < kInviteCodeCapacity);

    const std::string_view ScopedTextView(ScopedCode.data(), ScopedCodeSize);
    FBY_CHECK(ScopedTextView.substr(0, 8) == "FBYINV1:");

    std::array<std::byte, kTicketBytes> ScopedDecoded{};
    std::size_t ScopedDecodedSize = 0;
    FBY_CHECK(DecodeInviteCode(ScopedTextView, ScopedDecoded, &ScopedDecodedSize));
    FBY_CHECK(ScopedDecodedSize == kTicketBytes);
    FBY_CHECK(ScopedDecoded == ScopedSigned);

    // Round-tripped text still verifies.
    NetTicket ScopedVerified{};
    FBY_CHECK(VerifyTicket(ScopedDecoded, ScopedIssuer.Public, 500, &ScopedVerified) == TicketStatus::Ok);

    std::string ScopedGarbage(ScopedTextView);
    ScopedGarbage[ScopedGarbage.size() / 2] = '!';
    FBY_CHECK(!DecodeInviteCode(ScopedGarbage, ScopedDecoded, &ScopedDecodedSize));

    std::string ScopedWrongPrefix(ScopedTextView);
    ScopedWrongPrefix[0] = 'X';
    FBY_CHECK(!DecodeInviteCode(ScopedWrongPrefix, ScopedDecoded, &ScopedDecodedSize));

    FBY_CHECK(!DecodeInviteCode(ScopedTextView.substr(0, ScopedCodeSize - 1), ScopedDecoded, &ScopedDecodedSize));
    FBY_CHECK(!DecodeInviteCode("FBYINV1:", ScopedDecoded, &ScopedDecodedSize));
    FBY_CHECK(!DecodeInviteCode("", ScopedDecoded, &ScopedDecodedSize));

    // A code that decodes to the wrong length is rejected.
    std::array<char, kInviteCodeCapacity> ScopedShortCode{};
    std::size_t ScopedShortSize = 0;
    FBY_CHECK(EncodeInviteCode(std::span<const std::byte>(ScopedSigned.data(), 100), ScopedShortCode, &ScopedShortSize) ==
              false);
}
}

int main()
{
    TestIdentityRecord();
    TestTicketSigning();
    TestInviteCode();

    if (GFailures == 0)
    {
        std::printf("net_identity_test: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "net_identity_test: %u failure(s)\n", GFailures);
    return 1;
}
