// FemboyEngine2D - Noise XX channel, cookie, replay window and rekey tests.
#include "Engine/Core/CryptoAsymmetric.hpp"
#include "Engine/Network/SecureChannel.hpp"

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

[[nodiscard]] Fby::Crypto::X25519KeyPair MakeKey(std::uint8_t InSeed)
{
    std::array<std::byte, 32> ScopedSeed{};
    ScopedSeed.fill(static_cast<std::byte>(InSeed));
    Fby::Crypto::X25519KeyPair ScopedPair{};
    (void)Fby::Crypto::X25519KeyPairFromSeed(ScopedSeed, &ScopedPair);
    return ScopedPair;
}

[[nodiscard]] std::vector<std::byte> TextBytes(std::string_view InText)
{
    std::vector<std::byte> ScopedResult(InText.size());
    for (std::size_t ScopedIndex = 0; ScopedIndex < InText.size(); ++ScopedIndex)
    {
        ScopedResult[ScopedIndex] = static_cast<std::byte>(static_cast<unsigned char>(InText[ScopedIndex]));
    }
    return ScopedResult;
}

void Configure(const Fby::Crypto::X25519KeyPair& InClientKey, const Fby::Crypto::X25519KeyPair& InServerKey,
               std::uint64_t InSessionId, std::uint32_t InRecordsPerKey, Fby::Net::SecureChannelConfig* OutClient,
               Fby::Net::SecureChannelConfig* OutServer)
{
    using Fby::Net::SecureChannelConfig;

    *OutClient = SecureChannelConfig{};
    *OutServer = SecureChannelConfig{};
    OutClient->ProtocolV2 = false;
    OutServer->ProtocolV2 = false;

    OutClient->LocalStatic = InClientKey;
    OutClient->Initiator = true;
    OutClient->SessionId = InSessionId;
    OutClient->RecordsPerKey = InRecordsPerKey;
    OutClient->ExpectedRemoteStatic = InServerKey.Public;
    OutClient->RequireExpectedRemote = true;

    OutServer->LocalStatic = InServerKey;
    OutServer->SessionId = InSessionId;
    OutServer->RecordsPerKey = InRecordsPerKey;
}

[[nodiscard]] bool Exchange(Fby::Net::SecureChannel& InFrom, Fby::Net::SecureChannel& InTo)
{
    std::array<std::byte, Fby::Net::kSecureHandshakeBytes> ScopedMessage{};
    std::size_t ScopedSize = 0;
    if (InFrom.WriteHandshake(ScopedMessage, &ScopedSize) != Fby::Net::SecureStatus::Ok)
    {
        return false;
    }
    return InTo.ReadHandshake(std::span<const std::byte>(ScopedMessage.data(), ScopedSize)) ==
           Fby::Net::SecureStatus::Ok;
}

struct ChannelPair
{
    Fby::Net::SecureChannel Client;
    Fby::Net::SecureChannel Server;
};

void StartAndHandshake(ChannelPair& InPair, std::uint32_t InRecordsPerKey = Fby::Net::kSecureDefaultRecordsPerKey)
{
    const Fby::Crypto::X25519KeyPair ScopedClientKey = MakeKey(0x11);
    const Fby::Crypto::X25519KeyPair ScopedServerKey = MakeKey(0x22);
    Fby::Net::SecureChannelConfig ScopedClientConfig{};
    Fby::Net::SecureChannelConfig ScopedServerConfig{};
    ScopedClientConfig.ProtocolV2 = false;
    ScopedServerConfig.ProtocolV2 = false;
    Configure(ScopedClientKey, ScopedServerKey, 0x1234u, InRecordsPerKey, &ScopedClientConfig, &ScopedServerConfig);

    FBY_CHECK(InPair.Client.Start(ScopedClientConfig) == Fby::Net::SecureStatus::Ok);
    FBY_CHECK(InPair.Server.Start(ScopedServerConfig) == Fby::Net::SecureStatus::Ok);
    FBY_CHECK(Exchange(InPair.Client, InPair.Server));
    FBY_CHECK(Exchange(InPair.Server, InPair.Client));
    FBY_CHECK(Exchange(InPair.Client, InPair.Server));
    FBY_CHECK(InPair.Client.Established());
    FBY_CHECK(InPair.Server.Established());
    FBY_CHECK(InPair.Server.RemoteStatic() == ScopedClientKey.Public);
    FBY_CHECK(InPair.Client.RemoteStatic() == ScopedServerKey.Public);
}

void TestHandshakeAndRecords()
{
    using namespace Fby::Net;

    ChannelPair ScopedPair;
    StartAndHandshake(ScopedPair);

    const std::vector<std::byte> ScopedPing = TextBytes("ping over the secure channel");
    std::array<std::byte, kSecureMaxRecordBytes> ScopedRecord{};
    std::size_t ScopedRecordSize = 0;
    FBY_CHECK(ScopedPair.Client.Seal(ScopedPing, ScopedRecord, &ScopedRecordSize) == SecureStatus::Ok);
    FBY_CHECK(ScopedRecordSize == ScopedPing.size() + kSecureRecordHeaderBytes + kSecureTagBytes);

    std::array<std::byte, kSecureMaxPlainBytes> ScopedPlain{};
    std::size_t ScopedPlainSize = 0;
    FBY_CHECK(ScopedPair.Server.Open(std::span<const std::byte>(ScopedRecord.data(), ScopedRecordSize), ScopedPlain,
                                     &ScopedPlainSize) == SecureStatus::Ok);
    FBY_CHECK(ScopedPlainSize == ScopedPing.size());
    FBY_CHECK(std::memcmp(ScopedPlain.data(), ScopedPing.data(), ScopedPing.size()) == 0);

    // A second presentation of the same record is a replay.
    FBY_CHECK(ScopedPair.Server.Open(std::span<const std::byte>(ScopedRecord.data(), ScopedRecordSize), ScopedPlain,
                                     &ScopedPlainSize) == SecureStatus::Replayed);

    // A flipped tag byte on a fresh sequence is a tag failure, not a replay.
    const std::vector<std::byte> ScopedFreshPayload = TextBytes("tamper-me");
    std::size_t ScopedFreshSize = 0;
    FBY_CHECK(ScopedPair.Client.Seal(ScopedFreshPayload, ScopedRecord, &ScopedFreshSize) == SecureStatus::Ok);
    std::array<std::byte, kSecureMaxRecordBytes> ScopedTampered = ScopedRecord;
    ScopedTampered[ScopedFreshSize - 1] ^= std::byte{0x01};
    FBY_CHECK(ScopedPair.Server.Open(std::span<const std::byte>(ScopedTampered.data(), ScopedFreshSize), ScopedPlain,
                                     &ScopedPlainSize) == SecureStatus::BadTag);

    // Out-of-order delivery inside the window is accepted, a repeat is not.
    std::array<std::array<std::byte, kSecureMaxRecordBytes>, 5> ScopedRecords{};
    std::array<std::size_t, 5> ScopedSizes{};
    for (std::size_t ScopedIndex = 0; ScopedIndex < ScopedRecords.size(); ++ScopedIndex)
    {
        const std::vector<std::byte> ScopedPayload = TextBytes("order-test");
        FBY_CHECK(ScopedPair.Client.Seal(ScopedPayload, ScopedRecords[ScopedIndex], &ScopedSizes[ScopedIndex]) ==
                  SecureStatus::Ok);
    }
    FBY_CHECK(ScopedPair.Server.Open(std::span<const std::byte>(ScopedRecords[4].data(), ScopedSizes[4]), ScopedPlain,
                                     &ScopedPlainSize) == SecureStatus::Ok);
    FBY_CHECK(ScopedPair.Server.Open(std::span<const std::byte>(ScopedRecords[3].data(), ScopedSizes[3]), ScopedPlain,
                                     &ScopedPlainSize) == SecureStatus::Ok);
    FBY_CHECK(ScopedPair.Server.Open(std::span<const std::byte>(ScopedRecords[3].data(), ScopedSizes[3]), ScopedPlain,
                                     &ScopedPlainSize) == SecureStatus::Replayed);
}

void TestRekey()
{
    using namespace Fby::Net;

    ChannelPair ScopedPair;
    StartAndHandshake(ScopedPair, 8);

    std::array<std::byte, kSecureMaxRecordBytes> ScopedRecord{};
    std::array<std::byte, kSecureMaxPlainBytes> ScopedPlain{};
    std::size_t ScopedPlainSize = 0;

    for (std::uint32_t ScopedIndex = 0; ScopedIndex < 20; ++ScopedIndex)
    {
        const std::vector<std::byte> ScopedPayload = TextBytes("client-rekey");
        std::size_t ScopedRecordSize = 0;
        FBY_CHECK(ScopedPair.Client.Seal(ScopedPayload, ScopedRecord, &ScopedRecordSize) == SecureStatus::Ok);
        FBY_CHECK(ScopedPair.Server.Open(std::span<const std::byte>(ScopedRecord.data(), ScopedRecordSize), ScopedPlain,
                                         &ScopedPlainSize) == SecureStatus::Ok);
    }

    for (std::uint32_t ScopedIndex = 0; ScopedIndex < 20; ++ScopedIndex)
    {
        const std::vector<std::byte> ScopedPayload = TextBytes("server-rekey");
        std::size_t ScopedRecordSize = 0;
        FBY_CHECK(ScopedPair.Server.Seal(ScopedPayload, ScopedRecord, &ScopedRecordSize) == SecureStatus::Ok);
        FBY_CHECK(ScopedPair.Client.Open(std::span<const std::byte>(ScopedRecord.data(), ScopedRecordSize), ScopedPlain,
                                         &ScopedPlainSize) == SecureStatus::Ok);
    }
}

void TestCookies()
{
    using namespace Fby::Net;

    const std::uint64_t ScopedAddressTag = 0x1122334455667788ull;
    SecureCookieIssuer ScopedIssuer;
    ScopedIssuer.Reset();
    FBY_CHECK(ScopedIssuer.Ready());

    std::array<std::byte, kSecureCookieNonceBytes> ScopedNonce{};
    for (std::size_t ScopedIndex = 0; ScopedIndex < ScopedNonce.size(); ++ScopedIndex)
    {
        ScopedNonce[ScopedIndex] = static_cast<std::byte>(ScopedIndex);
    }
    std::array<std::byte, kSecureCookieBytes> ScopedCookie{};
    ScopedIssuer.Issue(ScopedAddressTag, ScopedNonce, ScopedCookie);
    FBY_CHECK(ScopedIssuer.Validate(ScopedAddressTag, ScopedNonce, ScopedCookie));
    FBY_CHECK(!ScopedIssuer.Validate(ScopedAddressTag + 1, ScopedNonce, ScopedCookie));

    const Fby::Crypto::X25519KeyPair ScopedClientKey = MakeKey(0x31);
    const Fby::Crypto::X25519KeyPair ScopedServerKey = MakeKey(0x32);
    SecureChannelConfig ScopedClientConfig{};
    SecureChannelConfig ScopedServerConfig{};
    ScopedClientConfig.ProtocolV2 = false;
    ScopedServerConfig.ProtocolV2 = false;
    Configure(ScopedClientKey, ScopedServerKey, 7, kSecureDefaultRecordsPerKey, &ScopedClientConfig,
              &ScopedServerConfig);
    ScopedClientConfig.HasCookie = true;
    ScopedClientConfig.CookieNonce = ScopedNonce;
    ScopedClientConfig.Cookie = ScopedCookie;
    ScopedServerConfig.CookieIssuer = &ScopedIssuer;
    ScopedServerConfig.AddressTag = ScopedAddressTag;

    ChannelPair ScopedPair;
    FBY_CHECK(ScopedPair.Client.Start(ScopedClientConfig) == SecureStatus::Ok);
    FBY_CHECK(ScopedPair.Server.Start(ScopedServerConfig) == SecureStatus::Ok);
    FBY_CHECK(Exchange(ScopedPair.Client, ScopedPair.Server));
    FBY_CHECK(Exchange(ScopedPair.Server, ScopedPair.Client));
    FBY_CHECK(Exchange(ScopedPair.Client, ScopedPair.Server));
    FBY_CHECK(ScopedPair.Server.Established());

    // A forged cookie never starts a Diffie-Hellman computation.
    ScopedClientConfig.Cookie[0] ^= std::byte{0x01};
    ScopedPair.Client.Reset();
    ScopedPair.Server.Reset();
    FBY_CHECK(ScopedPair.Client.Start(ScopedClientConfig) == SecureStatus::Ok);
    FBY_CHECK(ScopedPair.Server.Start(ScopedServerConfig) == SecureStatus::Ok);
    std::array<std::byte, kSecureHandshakeBytes> ScopedMessage{};
    std::size_t ScopedSize = 0;
    FBY_CHECK(ScopedPair.Client.WriteHandshake(ScopedMessage, &ScopedSize) == SecureStatus::Ok);
    FBY_CHECK(ScopedPair.Server.ReadHandshake(std::span<const std::byte>(ScopedMessage.data(), ScopedSize)) ==
              SecureStatus::BadCookie);

    // No cookie: the responder asks for a stateless challenge, the client
    // stores it and resends message 1, then the handshake continues.
    ScopedPair.Client.Reset();
    ScopedPair.Server.Reset();
    SecureChannelConfig ScopedNoCookie = ScopedClientConfig;
    ScopedNoCookie.HasCookie = false;
    FBY_CHECK(ScopedPair.Client.Start(ScopedNoCookie) == SecureStatus::Ok);
    FBY_CHECK(ScopedPair.Server.Start(ScopedServerConfig) == SecureStatus::Ok);
    FBY_CHECK(ScopedPair.Client.WriteHandshake(ScopedMessage, &ScopedSize) == SecureStatus::Ok);
    FBY_CHECK(ScopedPair.Server.ReadHandshake(std::span<const std::byte>(ScopedMessage.data(), ScopedSize)) ==
              SecureStatus::CookieRequired);

    std::array<std::byte, 256> ScopedChallenge{};
    std::size_t ScopedChallengeSize = 0;
    FBY_CHECK(ScopedPair.Server.WriteCookieChallenge(ScopedChallenge, &ScopedChallengeSize) == SecureStatus::Ok);
    FBY_CHECK(ScopedPair.Client.ReadHandshake(
                  std::span<const std::byte>(ScopedChallenge.data(), ScopedChallengeSize)) == SecureStatus::Ok);
    FBY_CHECK(ScopedPair.Client.WriteHandshake(ScopedMessage, &ScopedSize) == SecureStatus::Ok);
    FBY_CHECK(ScopedPair.Server.ReadHandshake(std::span<const std::byte>(ScopedMessage.data(), ScopedSize)) ==
              SecureStatus::Ok);
    FBY_CHECK(Exchange(ScopedPair.Server, ScopedPair.Client));
    FBY_CHECK(Exchange(ScopedPair.Client, ScopedPair.Server));
    FBY_CHECK(ScopedPair.Server.Established());
}

void TestPinnedKeyMismatch()
{
    using namespace Fby::Net;

    const Fby::Crypto::X25519KeyPair ScopedClientKey = MakeKey(0x41);
    const Fby::Crypto::X25519KeyPair ScopedServerKey = MakeKey(0x42);
    const Fby::Crypto::X25519KeyPair ScopedWrongKey = MakeKey(0x43);
    SecureChannelConfig ScopedClientConfig{};
    SecureChannelConfig ScopedServerConfig{};
    ScopedClientConfig.ProtocolV2 = false;
    ScopedServerConfig.ProtocolV2 = false;
    Configure(ScopedClientKey, ScopedServerKey, 5, kSecureDefaultRecordsPerKey, &ScopedClientConfig,
              &ScopedServerConfig);
    ScopedClientConfig.ExpectedRemoteStatic = ScopedWrongKey.Public;

    ChannelPair ScopedPair;
    FBY_CHECK(ScopedPair.Client.Start(ScopedClientConfig) == SecureStatus::Ok);
    FBY_CHECK(ScopedPair.Server.Start(ScopedServerConfig) == SecureStatus::Ok);
    FBY_CHECK(Exchange(ScopedPair.Client, ScopedPair.Server));
    FBY_CHECK(!Exchange(ScopedPair.Server, ScopedPair.Client));
    FBY_CHECK(!ScopedPair.Client.Established());
}

void TestStateGuards()
{
    using namespace Fby::Net;

    ChannelPair ScopedPair;
    StartAndHandshake(ScopedPair);

    const std::vector<std::byte> ScopedTooLarge(Fby::Net::kSecureMaxPlainBytes + 1, std::byte{0x7F});
    std::array<std::byte, kSecureMaxRecordBytes> ScopedRecord{};
    std::size_t ScopedSize = 0;
    FBY_CHECK(ScopedPair.Client.Seal(ScopedTooLarge, ScopedRecord, &ScopedSize) == SecureStatus::BadSize);

    const Fby::Crypto::X25519KeyPair ScopedServerKey = MakeKey(0x52);
    SecureChannelConfig ScopedServerConfig{};
    ScopedServerConfig.LocalStatic = ScopedServerKey;
    ScopedServerConfig.SessionId = 1;
    SecureChannel ScopedFresh;
    FBY_CHECK(ScopedFresh.Start(ScopedServerConfig) == SecureStatus::Ok);
    FBY_CHECK(ScopedFresh.WriteHandshake(ScopedRecord, &ScopedSize) == SecureStatus::WrongTurn);
    const std::vector<std::byte> ScopedTiny = TextBytes("x");
    FBY_CHECK(ScopedFresh.Seal(ScopedTiny, ScopedRecord, &ScopedSize) == SecureStatus::NotEstablished);

    std::array<std::byte, 64> ScopedGarbage{};
    ScopedGarbage[0] = std::byte{0x5A};
    FBY_CHECK(ScopedFresh.ReadHandshake(ScopedGarbage) == SecureStatus::BadMagic);

    // A second Start on an established channel is rejected instead of silently
    // discarding the established keys.
    FBY_CHECK(ScopedPair.Client.Start(ScopedServerConfig) == SecureStatus::AlreadyEstablished);
}
}

int main()
{
    TestHandshakeAndRecords();
    TestRekey();
    TestCookies();
    TestPinnedKeyMismatch();
    TestStateGuards();

    if (GFailures == 0)
    {
        std::printf("secure_channel_test: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "secure_channel_test: %u failure(s)\n", GFailures);
    return 1;
}
