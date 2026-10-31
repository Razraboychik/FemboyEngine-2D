// FemboyEngine2D - protocol v2 record layer: epoch header and the Noise REKEY chain.
#include "Engine/Assets/FemCrypto.hpp"
#include "Engine/Core/CryptoAsymmetric.hpp"
#include "Engine/Network/ByteOrder.hpp"
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

using Fby::Net::SecureChannel;
using Fby::Net::SecureChannelConfig;
using Fby::Net::SecureStatus;
using RecordBuffer = std::array<std::byte, Fby::Net::kSecureMaxRecordBytes>;

[[nodiscard]] const std::uint8_t* Raw(const std::byte* InBytes) noexcept
{
    return reinterpret_cast<const std::uint8_t*>(InBytes);
}

[[nodiscard]] std::uint8_t* RawWrite(std::byte* InBytes) noexcept
{
    return reinterpret_cast<std::uint8_t*>(InBytes);
}

[[nodiscard]] Fby::Crypto::X25519KeyPair MakeKey(std::uint8_t InSeed)
{
    std::array<std::byte, 32> ScopedSeed{};
    ScopedSeed.fill(static_cast<std::byte>(InSeed));
    Fby::Crypto::X25519KeyPair ScopedPair{};
    (void)Fby::Crypto::X25519KeyPairFromSeed(ScopedSeed, &ScopedPair);
    return ScopedPair;
}

struct V2Pair
{
    SecureChannel Client;
    SecureChannel Server;
};

void ConfigureV2(const Fby::Crypto::X25519KeyPair& InClientKey, const Fby::Crypto::X25519KeyPair& InServerKey,
                 std::uint32_t InRecordsPerKey, SecureChannelConfig* OutClient, SecureChannelConfig* OutServer)
{
    *OutClient = SecureChannelConfig{};
    *OutServer = SecureChannelConfig{};

    OutClient->LocalStatic = InClientKey;
    OutClient->Initiator = true;
    OutClient->RecordsPerKey = InRecordsPerKey;
    OutClient->ExpectedRemoteStatic = InServerKey.Public;
    OutClient->RequireExpectedRemote = true;
    OutClient->ProtocolV2 = true;

    OutServer->LocalStatic = InServerKey;
    OutServer->RecordsPerKey = InRecordsPerKey;
    OutServer->ProtocolV2 = true;
}

void Exchange(SecureChannel& InFrom, SecureChannel& InTo)
{
    std::array<std::byte, Fby::Net::kSecureHandshakeBytes> ScopedMessage{};
    std::size_t ScopedSize = 0;
    FBY_CHECK(InFrom.WriteHandshake(ScopedMessage, &ScopedSize) == SecureStatus::Ok);
    FBY_CHECK(InTo.ReadHandshake(std::span<const std::byte>(ScopedMessage.data(), ScopedSize)) == SecureStatus::Ok);
}

void StartV2(V2Pair& InPair, std::uint32_t InRecordsPerKey)
{
    const Fby::Crypto::X25519KeyPair ScopedClientKey = MakeKey(0x61);
    const Fby::Crypto::X25519KeyPair ScopedServerKey = MakeKey(0x62);
    SecureChannelConfig ScopedClientConfig{};
    SecureChannelConfig ScopedServerConfig{};
    ConfigureV2(ScopedClientKey, ScopedServerKey, InRecordsPerKey, &ScopedClientConfig, &ScopedServerConfig);

    FBY_CHECK(InPair.Client.Start(ScopedClientConfig) == SecureStatus::Ok);
    FBY_CHECK(InPair.Server.Start(ScopedServerConfig) == SecureStatus::Ok);
    Exchange(InPair.Client, InPair.Server);
    Exchange(InPair.Server, InPair.Client);
    Exchange(InPair.Client, InPair.Server);
    FBY_CHECK(InPair.Client.Established());
    FBY_CHECK(InPair.Server.Established());
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

void SealText(SecureChannel& InChannel, std::string_view InText, RecordBuffer& OutRecord, std::size_t& OutSize)
{
    const std::vector<std::byte> ScopedPlain = TextBytes(InText);
    OutSize = 0;
    FBY_CHECK(InChannel.Seal(ScopedPlain, OutRecord, &OutSize) == SecureStatus::Ok);
}

[[nodiscard]] SecureStatus OpenBytes(SecureChannel& InChannel, std::span<const std::byte> InRecord,
                                     std::string_view InExpected)
{
    std::array<std::byte, Fby::Net::kSecureMaxPlainBytes> ScopedPlain{};
    std::size_t ScopedSize = 0;
    const SecureStatus ScopedStatus = InChannel.Open(InRecord, ScopedPlain, &ScopedSize);
    if (ScopedStatus != SecureStatus::Ok)
    {
        return ScopedStatus;
    }
    FBY_CHECK(ScopedSize == InExpected.size());
    FBY_CHECK(std::memcmp(ScopedPlain.data(), InExpected.data(), InExpected.size()) == 0);
    return ScopedStatus;
}

void TestHeaderAndRoundTrip()
{
    using namespace Fby::Net;

    V2Pair ScopedPair;
    StartV2(ScopedPair, 4);

    RecordBuffer ScopedRecord{};
    std::size_t ScopedSize = 0;
    SealText(ScopedPair.Client, "header", ScopedRecord, ScopedSize);

    // The first record starts at epoch 0, sequence 0, and carries the explicit
    // 16-byte v2 header.
    FBY_CHECK(ScopedSize == kSecureV2RecordHeaderBytes + 6 + kSecureTagBytes);
    FBY_CHECK(Detail::LoadU64(Raw(ScopedRecord.data())) == 0);
    FBY_CHECK(Detail::LoadU64(Raw(ScopedRecord.data()) + 8) == 0);
    FBY_CHECK(OpenBytes(ScopedPair.Server, std::span<const std::byte>(ScopedRecord.data(), ScopedSize), "header") ==
              SecureStatus::Ok);

    // The fifth record is the first one sealed under the epoch-1 key.
    std::array<RecordBuffer, 4> ScopedRecords{};
    std::array<std::size_t, 4> ScopedSizes{};
    for (std::size_t ScopedIndex = 0; ScopedIndex < ScopedRecords.size(); ++ScopedIndex)
    {
        SealText(ScopedPair.Client, "epoch-roll", ScopedRecords[ScopedIndex], ScopedSizes[ScopedIndex]);
    }
    FBY_CHECK(Detail::LoadU64(Raw(ScopedRecords[2].data())) == 0);
    FBY_CHECK(Detail::LoadU64(Raw(ScopedRecords[2].data()) + 8) == 3);
    FBY_CHECK(Detail::LoadU64(Raw(ScopedRecords[3].data())) == 1);
    FBY_CHECK(Detail::LoadU64(Raw(ScopedRecords[3].data()) + 8) == 4);

    for (std::size_t ScopedIndex = 0; ScopedIndex < ScopedRecords.size(); ++ScopedIndex)
    {
        FBY_CHECK(OpenBytes(ScopedPair.Server,
                            std::span<const std::byte>(ScopedRecords[ScopedIndex].data(), ScopedSizes[ScopedIndex]),
                            "epoch-roll") == SecureStatus::Ok);
    }

    // An empty payload is a valid record: header plus tag only.
    FBY_CHECK(ScopedPair.Client.Seal(std::span<const std::byte>{}, ScopedRecord, &ScopedSize) == SecureStatus::Ok);
    FBY_CHECK(ScopedSize == kSecureV2RecordHeaderBytes + kSecureTagBytes);
    FBY_CHECK(OpenBytes(ScopedPair.Server, std::span<const std::byte>(ScopedRecord.data(), ScopedSize), "") ==
              SecureStatus::Ok);
}

void TestEpochRollover()
{
    using namespace Fby::Net;

    V2Pair ScopedPair;
    StartV2(ScopedPair, 4);

    std::array<RecordBuffer, 12> ScopedRecords{};
    std::array<std::size_t, 12> ScopedSizes{};
    for (std::size_t ScopedIndex = 0; ScopedIndex < ScopedRecords.size(); ++ScopedIndex)
    {
        SealText(ScopedPair.Client, "rollover", ScopedRecords[ScopedIndex], ScopedSizes[ScopedIndex]);
    }

    const auto OpenAt = [&ScopedPair, &ScopedRecords, &ScopedSizes](std::size_t InIndex)
    {
        return OpenBytes(ScopedPair.Server,
                         std::span<const std::byte>(ScopedRecords[InIndex].data(), ScopedSizes[InIndex]), "rollover");
    };

    // Deliver an epoch-2 record first: the receiver derives the chain forward
    // from the split key without knowing the sender's RecordsPerKey.
    FBY_CHECK(OpenAt(8) == SecureStatus::Ok);
    FBY_CHECK(OpenAt(5) == SecureStatus::Ok);
    // Two epochs behind is outside the retained key window.
    FBY_CHECK(OpenAt(2) == SecureStatus::Replayed);
    FBY_CHECK(OpenAt(11) == SecureStatus::Ok);
    FBY_CHECK(OpenAt(9) == SecureStatus::Ok);
    // Loss inside an epoch does not block later records, and repeats are caught
    // by the sequence replay window.
    FBY_CHECK(OpenAt(6) == SecureStatus::Ok);
    FBY_CHECK(OpenAt(10) == SecureStatus::Ok);
    FBY_CHECK(OpenAt(8) == SecureStatus::Replayed);

    // The reverse direction runs its own epoch chain from its own split key.
    RecordBuffer ScopedBack{};
    std::size_t ScopedBackSize = 0;
    SealText(ScopedPair.Server, "back", ScopedBack, ScopedBackSize);
    FBY_CHECK(Detail::LoadU64(Raw(ScopedBack.data())) == 0);
    FBY_CHECK(Detail::LoadU64(Raw(ScopedBack.data()) + 8) == 0);
    FBY_CHECK(OpenBytes(ScopedPair.Client, std::span<const std::byte>(ScopedBack.data(), ScopedBackSize), "back") ==
              SecureStatus::Ok);
}

void TestTamperAndState()
{
    using namespace Fby::Net;

    V2Pair ScopedPair;
    StartV2(ScopedPair, 4);

    std::array<RecordBuffer, 16> ScopedRecords{};
    std::array<std::size_t, 16> ScopedSizes{};
    for (std::size_t ScopedIndex = 0; ScopedIndex < ScopedRecords.size(); ++ScopedIndex)
    {
        SealText(ScopedPair.Client, "tamper", ScopedRecords[ScopedIndex], ScopedSizes[ScopedIndex]);
    }

    for (std::size_t ScopedIndex = 0; ScopedIndex < 4; ++ScopedIndex)
    {
        FBY_CHECK(OpenBytes(ScopedPair.Server,
                            std::span<const std::byte>(ScopedRecords[ScopedIndex].data(), ScopedSizes[ScopedIndex]),
                            "tamper") == SecureStatus::Ok);
    }

    // Ciphertext, epoch and sequence are all authenticated; a failed forgery
    // must not advance the receive chain.
    RecordBuffer ScopedCipherTampered = ScopedRecords[4];
    ScopedCipherTampered[ScopedSizes[4] - 1] ^= std::byte{0x01};
    FBY_CHECK(OpenBytes(ScopedPair.Server,
                        std::span<const std::byte>(ScopedCipherTampered.data(), ScopedSizes[4]), "tamper") ==
              SecureStatus::BadTag);

    RecordBuffer ScopedEpochTampered = ScopedRecords[4];
    ScopedEpochTampered[0] ^= std::byte{0x01};
    FBY_CHECK(OpenBytes(ScopedPair.Server,
                        std::span<const std::byte>(ScopedEpochTampered.data(), ScopedSizes[4]), "tamper") ==
              SecureStatus::BadTag);

    RecordBuffer ScopedSequenceTampered = ScopedRecords[4];
    ScopedSequenceTampered[8] ^= std::byte{0x01};
    FBY_CHECK(OpenBytes(ScopedPair.Server,
                        std::span<const std::byte>(ScopedSequenceTampered.data(), ScopedSizes[4]), "tamper") ==
              SecureStatus::BadTag);

    FBY_CHECK(OpenBytes(ScopedPair.Server, std::span<const std::byte>(ScopedRecords[4].data(), ScopedSizes[4]),
                        "tamper") == SecureStatus::Ok);
    for (std::size_t ScopedIndex = 5; ScopedIndex < 12; ++ScopedIndex)
    {
        FBY_CHECK(OpenBytes(ScopedPair.Server,
                            std::span<const std::byte>(ScopedRecords[ScopedIndex].data(), ScopedSizes[ScopedIndex]),
                            "tamper") == SecureStatus::Ok);
    }

    // A record that claims a far-future epoch is rejected before any derivation
    // and leaves the receiver state where it was.
    RecordBuffer ScopedFuture = ScopedRecords[12];
    ScopedFuture[ScopedSizes[12] - 1] ^= std::byte{0x01};
    FBY_CHECK(OpenBytes(ScopedPair.Server, std::span<const std::byte>(ScopedFuture.data(), ScopedSizes[12]), "tamper") ==
              SecureStatus::BadTag);
    Detail::StoreU64(RawWrite(ScopedFuture.data()), 3 + kSecureMaxEpochJump + 1);
    FBY_CHECK(OpenBytes(ScopedPair.Server, std::span<const std::byte>(ScopedFuture.data(), ScopedSizes[12]), "tamper") ==
              SecureStatus::Replayed);
    FBY_CHECK(OpenBytes(ScopedPair.Server, std::span<const std::byte>(ScopedRecords[12].data(), ScopedSizes[12]),
                        "tamper") == SecureStatus::Ok);
}

void TestEpochJumpCap()
{
    using namespace Fby::Net;

    V2Pair ScopedPair;
    StartV2(ScopedPair, 4);

    RecordBuffer ScopedRecord{};
    std::size_t ScopedSize = 0;
    SealText(ScopedPair.Client, "cap", ScopedRecord, ScopedSize);

    // Exactly the cap is derivable and fails on the tag instead of the header.
    RecordBuffer ScopedAtCap = ScopedRecord;
    Detail::StoreU64(RawWrite(ScopedAtCap.data()), kSecureMaxEpochJump);
    FBY_CHECK(OpenBytes(ScopedPair.Server, std::span<const std::byte>(ScopedAtCap.data(), ScopedSize), "cap") ==
              SecureStatus::BadTag);

    // One epoch past the cap is rejected before any derivation.
    RecordBuffer ScopedPastCap = ScopedRecord;
    Detail::StoreU64(RawWrite(ScopedPastCap.data()), kSecureMaxEpochJump + 1);
    FBY_CHECK(OpenBytes(ScopedPair.Server, std::span<const std::byte>(ScopedPastCap.data(), ScopedSize), "cap") ==
              SecureStatus::Replayed);

    // A far forged epoch is also rejected, and the genuine record still opens.
    RecordBuffer ScopedFar = ScopedRecord;
    Detail::StoreU64(RawWrite(ScopedFar.data()), 0xFFFFu);
    FBY_CHECK(OpenBytes(ScopedPair.Server, std::span<const std::byte>(ScopedFar.data(), ScopedSize), "cap") ==
              SecureStatus::Replayed);
    FBY_CHECK(OpenBytes(ScopedPair.Server, std::span<const std::byte>(ScopedRecord.data(), ScopedSize), "cap") ==
              SecureStatus::Ok);
}

void TestRekeyPrimitive()
{
    std::array<std::byte, 32> ScopedKey{};
    for (std::size_t ScopedIndex = 0; ScopedIndex < ScopedKey.size(); ++ScopedIndex)
    {
        ScopedKey[ScopedIndex] = static_cast<std::byte>((ScopedIndex * 7u + 1u) & 0xFFu);
    }

    // The specification defines REKEY(k) as the first 32 bytes of the encryption
    // of 32 zero bytes under the maximum nonce with empty associated data.
    Fby::Crypto::AeadKey ScopedAeadKey;
    std::memcpy(ScopedAeadKey.bytes.data(), ScopedKey.data(), ScopedKey.size());
    Fby::Crypto::AeadNonce ScopedNonce;
    ScopedNonce.SetCounter(Fby::Net::kSecureMaxTransportSequence);
    std::array<std::byte, 32> ScopedZeros{};
    std::array<std::byte, 32> ScopedExpected{};
    Fby::Crypto::AeadTag ScopedTag{};
    FBY_CHECK(Fby::Crypto::ChaCha20Poly1305Encrypt(ScopedAeadKey, ScopedNonce, {}, ScopedZeros, ScopedExpected,
                                                    ScopedTag));

    FBY_CHECK(Fby::Net::NoiseRekey(ScopedKey) == ScopedExpected);
    FBY_CHECK(Fby::Net::NoiseRekey(ScopedKey) != ScopedKey);

    std::array<std::byte, 16> ScopedShortKey{};
    std::array<std::byte, 32> ScopedZeroResult{};
    FBY_CHECK(Fby::Net::NoiseRekey(ScopedShortKey) == ScopedZeroResult);
}

void TestSizeGuards()
{
    using namespace Fby::Net;

    V2Pair ScopedPair;
    StartV2(ScopedPair, 4);

    RecordBuffer ScopedRecord{};
    std::size_t ScopedSize = 0;
    const std::vector<std::byte> ScopedTooLarge(kSecureMaxPlainBytes + 1, std::byte{0x5A});
    FBY_CHECK(ScopedPair.Client.Seal(ScopedTooLarge, ScopedRecord, &ScopedSize) == SecureStatus::BadSize);

    std::array<std::byte, 40> ScopedSmallOutput{};
    const std::vector<std::byte> ScopedSixtyFour(64, std::byte{0x00});
    FBY_CHECK(ScopedPair.Client.Seal(ScopedSixtyFour, ScopedSmallOutput, &ScopedSize) == SecureStatus::BadSize);

    std::array<std::byte, kSecureV2RecordHeaderBytes> ScopedHeaderOnly{};
    std::array<std::byte, kSecureMaxPlainBytes> ScopedPlain{};
    FBY_CHECK(ScopedPair.Server.Open(ScopedHeaderOnly, ScopedPlain, &ScopedSize) == SecureStatus::BadSize);

    std::array<std::byte, kSecureV2RecordHeaderBytes + kSecureTagBytes - 1> ScopedShortRecord{};
    FBY_CHECK(ScopedPair.Server.Open(ScopedShortRecord, ScopedPlain, &ScopedSize) == SecureStatus::BadSize);
}
}

int main()
{
    TestHeaderAndRoundTrip();
    TestEpochRollover();
    TestTamperAndState();
    TestEpochJumpCap();
    TestRekeyPrimitive();
    TestSizeGuards();

    if (GFailures == 0)
    {
        std::printf("record_layer_v2_test: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "record_layer_v2_test: %u failure(s)\n", GFailures);
    return 1;
}
