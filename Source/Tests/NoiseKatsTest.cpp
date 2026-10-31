// FemboyEngine2D - known-answer checks for the FBY XX handshake core.
#include "Engine/Core/CryptoAsymmetric.hpp"
#include "Engine/Core/Sha256.hpp"
#include "Engine/Network/SecureChannel.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

namespace
{
std::uint32_t GFailures = 0;

#define FBY_CHECK(condition)                                                                                           \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);                                  \
            ++GFailures;                                                                                               \
        }                                                                                                              \
    } while (false)

[[nodiscard]] int Nibble(char InCharacter)
{
    if (InCharacter >= '0' && InCharacter <= '9')
    {
        return InCharacter - '0';
    }
    if (InCharacter >= 'a' && InCharacter <= 'f')
    {
        return InCharacter - 'a' + 10;
    }
    if (InCharacter >= 'A' && InCharacter <= 'F')
    {
        return InCharacter - 'A' + 10;
    }
    return -1;
}

template <std::size_t N>
[[nodiscard]] std::array<std::byte, N> FromHex(std::string_view InHex)
{
    std::array<std::byte, N> ScopedBytes{};
    if (InHex.size() != N * 2)
    {
        return ScopedBytes;
    }
    for (std::size_t ScopedIndex = 0; ScopedIndex < N; ++ScopedIndex)
    {
        const int ScopedHigh = Nibble(InHex[ScopedIndex * 2]);
        const int ScopedLow = Nibble(InHex[ScopedIndex * 2 + 1]);
        if (ScopedHigh < 0 || ScopedLow < 0)
        {
            return {};
        }
        ScopedBytes[ScopedIndex] = static_cast<std::byte>((ScopedHigh << 4) | ScopedLow);
    }
    return ScopedBytes;
}

template <std::size_t N>
[[nodiscard]] std::string ToHex(const std::array<std::byte, N>& InBytes)
{
    static constexpr char kNibbles[] = "0123456789abcdef";
    std::string ScopedText;
    ScopedText.reserve(N * 2);
    for (const std::byte ScopedByte : InBytes)
    {
        const auto ScopedValue = static_cast<std::uint8_t>(ScopedByte);
        ScopedText.push_back(kNibbles[ScopedValue >> 4]);
        ScopedText.push_back(kNibbles[ScopedValue & 0x0F]);
    }
    return ScopedText;
}

template <std::size_t N>
[[nodiscard]] std::array<std::byte, N> Seed(std::uint8_t InValue)
{
    std::array<std::byte, N> ScopedSeed{};
    for (std::byte& ScopedByte : ScopedSeed)
    {
        ScopedByte = static_cast<std::byte>(InValue);
    }
    return ScopedSeed;
}

// The standard Noise XX initial hash. The protocol name is exactly 32 bytes,
// which is not longer than SHA-256, so h0 is the name itself zero padded, not
// its digest. The FBY variant hashes its own name on purpose.
constexpr std::string_view kOfficialProtocolName = "Noise_XX_25519_ChaChaPoly_SHA256";
constexpr std::string_view kOfficialH0 = "4e6f6973655f58585f32353531395f436861436861506f6c795f534841323536";

// FBY variant: h0, the initial hash after the fixed prologue plus session id 1,
// and the same hash with a 32-byte prologue binding.
constexpr std::string_view kFbyH0 = "b66f2b64dec7f9eccd368ca8acf8168657281b14a87b9845f7707b9f01910244";
constexpr std::string_view kFbyInitial = "2bfbc80a6dc30966d543156e4c4c8239db1a420ec6277a13bcb15f727c6edbac";
constexpr std::string_view kFbyInitialBound = "2f7d1e8952fbd28e67c4768fc6ebc5bc5730e9eb2a85c3dcec09ae8263eb5c8b";

// Frozen transcript hash of the deterministic handshake below. It is a
// regression answer for this variant, not an official Noise vector: the FBY
// framing, prologue and missing message-1 payload tag change the transcript.
constexpr std::string_view kFbyFinalHandshake =
    "024c7a7fc34706df73f23a132069e3d7e0217a7d48167e6cffe1c0dc20adde0f";
}

int main()
{
    using namespace Fby;
    using namespace Fby::Net;

    // Protocol v2 is the shipping default; v1 stays available only when a test
    // or a legacy peer opts out explicitly.
    FBY_CHECK(SecureChannelConfig{}.ProtocolV2);

    // 1. Initial hashes: the standard Noise h0 is the zero-padded protocol name
    // when it fits the hash, and the FBY h0 hashes the FBY name instead.
    {
        std::array<std::byte, Crypto::kSha256Bytes> ScopedH0{};
        std::memcpy(ScopedH0.data(), kOfficialProtocolName.data(), kOfficialProtocolName.size());
        FBY_CHECK(ScopedH0 == FromHex<Crypto::kSha256Bytes>(kOfficialH0));

        std::array<std::byte, Crypto::kSha256Bytes> ScopedNameHash{};
        const std::span<const std::byte> ScopedName(
            reinterpret_cast<const std::byte*>(kOfficialProtocolName.data()), kOfficialProtocolName.size());
        Crypto::Sha256(ScopedName, std::span<std::byte>(ScopedNameHash));
        FBY_CHECK(ScopedNameHash != ScopedH0);

        const std::string_view ScopedFbyName = "Fby_XX_25519_ChaChaPoly_SHA256";
        const std::span<const std::byte> ScopedFbyBytes(
            reinterpret_cast<const std::byte*>(ScopedFbyName.data()), ScopedFbyName.size());
        std::array<std::byte, Crypto::kSha256Bytes> ScopedFbyHash{};
        Crypto::Sha256(ScopedFbyBytes, std::span<std::byte>(ScopedFbyHash));
        FBY_CHECK(ScopedFbyHash == FromHex<Crypto::kSha256Bytes>(kFbyH0));
    }

    Crypto::X25519KeyPair ScopedInitiatorStatic{};
    Crypto::X25519KeyPair ScopedResponderStatic{};
    (void)Crypto::X25519KeyPairFromSeed(Seed<32>(0x21), &ScopedInitiatorStatic);
    (void)Crypto::X25519KeyPairFromSeed(Seed<32>(0x22), &ScopedResponderStatic);

    const auto MakeConfig = [&](bool InInitiator, const std::array<std::byte, Crypto::kX25519KeyBytes>& InEphemeral,
                                bool InBound, std::uint8_t InBinding)
    {
        SecureChannelConfig ScopedConfig{};
        ScopedConfig.ProtocolV2 = false;
        ScopedConfig.LocalStatic = InInitiator ? ScopedInitiatorStatic : ScopedResponderStatic;
        ScopedConfig.Initiator = InInitiator;
        ScopedConfig.SessionId = 1;
        ScopedConfig.HasFixedEphemeral = true;
        ScopedConfig.FixedEphemeralSeed = InEphemeral;
        if (InBound)
        {
            ScopedConfig.PrologueBytes = Crypto::kSha256Bytes;
            const std::array<std::byte, Crypto::kSha256Bytes> ScopedBinding = Seed<Crypto::kSha256Bytes>(InBinding);
            std::memcpy(ScopedConfig.Prologue.data(), ScopedBinding.data(), ScopedBinding.size());
        }
        return ScopedConfig;
    };

    // 2. Start hashes: the prologue is absorbed exactly once, before any key
    // material, and the binding extends it.
    {
        SecureChannel ScopedPlain;
        FBY_CHECK(ScopedPlain.Start(MakeConfig(true, Seed<32>(0x31), false, 0x00)) == SecureStatus::Ok);
        FBY_CHECK(ScopedPlain.HandshakeHash() == FromHex<Crypto::kSha256Bytes>(kFbyInitial));

        SecureChannel ScopedBound;
        FBY_CHECK(ScopedBound.Start(MakeConfig(true, Seed<32>(0x31), true, 0xAB)) == SecureStatus::Ok);
        FBY_CHECK(ScopedBound.HandshakeHash() == FromHex<Crypto::kSha256Bytes>(kFbyInitialBound));
    }

    // 3. Deterministic transcript: fixed statics, ephemerals and binding.
    SecureChannel ScopedInitiator;
    SecureChannel ScopedResponder;
    FBY_CHECK(ScopedInitiator.Start(MakeConfig(true, Seed<32>(0x31), true, 0xAB)) == SecureStatus::Ok);
    FBY_CHECK(ScopedResponder.Start(MakeConfig(false, Seed<32>(0x32), true, 0xAB)) == SecureStatus::Ok);

    std::array<std::byte, kSecureHandshakeBytes> ScopedMessage1{};
    std::array<std::byte, kSecureHandshakeBytes> ScopedMessage2{};
    std::array<std::byte, kSecureHandshakeBytes> ScopedMessage3{};
    std::size_t ScopedBytes1 = 0;
    std::size_t ScopedBytes2 = 0;
    std::size_t ScopedBytes3 = 0;
    FBY_CHECK(ScopedInitiator.WriteHandshake(ScopedMessage1, &ScopedBytes1) == SecureStatus::Ok);
    FBY_CHECK(ScopedResponder.ReadHandshake(std::span<const std::byte>(ScopedMessage1.data(), ScopedBytes1)) ==
              SecureStatus::Ok);
    FBY_CHECK(ScopedResponder.WriteHandshake(ScopedMessage2, &ScopedBytes2) == SecureStatus::Ok);
    FBY_CHECK(ScopedInitiator.ReadHandshake(std::span<const std::byte>(ScopedMessage2.data(), ScopedBytes2)) ==
              SecureStatus::Ok);
    FBY_CHECK(ScopedInitiator.WriteHandshake(ScopedMessage3, &ScopedBytes3) == SecureStatus::Ok);
    FBY_CHECK(ScopedResponder.ReadHandshake(std::span<const std::byte>(ScopedMessage3.data(), ScopedBytes3)) ==
              SecureStatus::Ok);
    FBY_CHECK(ScopedInitiator.Established());
    FBY_CHECK(ScopedResponder.Established());
    FBY_CHECK(ScopedInitiator.HandshakeHash() == ScopedResponder.HandshakeHash());
    FBY_CHECK(ScopedInitiator.HandshakeHash() == FromHex<Crypto::kSha256Bytes>(kFbyFinalHandshake));

    // 4. The record layer agrees on both sides after the transcript.
    {
        const std::array<std::byte, 5> ScopedPlaintext{std::byte{'k'}, std::byte{'a'}, std::byte{'t'},
                                                       std::byte{'s'}, std::byte{'!'}};
        std::array<std::byte, kSecureMaxRecordBytes> ScopedRecord{};
        std::array<std::byte, kSecureMaxPlainBytes> ScopedOpened{};
        std::size_t ScopedRecordBytes = 0;
        std::size_t ScopedOpenedBytes = 0;
        FBY_CHECK(ScopedInitiator.Seal(ScopedPlaintext, ScopedRecord, &ScopedRecordBytes) == SecureStatus::Ok);
        FBY_CHECK(ScopedResponder.Open(std::span<const std::byte>(ScopedRecord.data(), ScopedRecordBytes),
                                       ScopedOpened, &ScopedOpenedBytes) == SecureStatus::Ok);
        FBY_CHECK(ScopedOpenedBytes == ScopedPlaintext.size());
        FBY_CHECK(std::memcmp(ScopedOpened.data(), ScopedPlaintext.data(), ScopedPlaintext.size()) == 0);
    }

    // 5. A different binding changes the transcript and the handshake fails.
    {
        SecureChannel ScopedWrongInitiator;
        SecureChannel ScopedResponderAgain;
        FBY_CHECK(ScopedWrongInitiator.Start(MakeConfig(true, Seed<32>(0x31), true, 0xCD)) == SecureStatus::Ok);
        FBY_CHECK(ScopedResponderAgain.Start(MakeConfig(false, Seed<32>(0x32), true, 0xAB)) == SecureStatus::Ok);

        std::array<std::byte, kSecureHandshakeBytes> ScopedMsg1{};
        std::array<std::byte, kSecureHandshakeBytes> ScopedMsg2{};
        std::size_t ScopedSize1 = 0;
        std::size_t ScopedSize2 = 0;
        FBY_CHECK(ScopedWrongInitiator.WriteHandshake(ScopedMsg1, &ScopedSize1) == SecureStatus::Ok);
        FBY_CHECK(ScopedResponderAgain.ReadHandshake(std::span<const std::byte>(ScopedMsg1.data(), ScopedSize1)) ==
                  SecureStatus::Ok);
        FBY_CHECK(ScopedResponderAgain.WriteHandshake(ScopedMsg2, &ScopedSize2) == SecureStatus::Ok);
        FBY_CHECK(ScopedWrongInitiator.ReadHandshake(std::span<const std::byte>(ScopedMsg2.data(), ScopedSize2)) ==
                  SecureStatus::BadTag);
        FBY_CHECK(!ScopedWrongInitiator.Established());
        FBY_CHECK(!ScopedResponderAgain.Established());
    }

    // 6. RFC 7748 X25519 vector: the v2 primitive is the raw curve output, not
    // the BLAKE2b-post-hashed v1 value.
    {
        const auto ScopedAliceSeed = FromHex<32>("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a");
        const auto ScopedBobSeed = FromHex<32>("5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb");
        const auto ScopedExpectedAlicePublic =
            FromHex<32>("8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a");
        const auto ScopedExpectedBobPublic =
            FromHex<32>("de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f");
        const auto ScopedExpectedShared =
            FromHex<32>("4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742");

        Crypto::X25519KeyPair ScopedAlice{};
        Crypto::X25519KeyPair ScopedBob{};
        FBY_CHECK(Crypto::X25519KeyPairFromSeed(ScopedAliceSeed, &ScopedAlice));
        FBY_CHECK(Crypto::X25519KeyPairFromSeed(ScopedBobSeed, &ScopedBob));
        FBY_CHECK(ScopedAlice.Public == ScopedExpectedAlicePublic);
        FBY_CHECK(ScopedBob.Public == ScopedExpectedBobPublic);

        std::array<std::byte, 32> ScopedRaw{};
        FBY_CHECK(Crypto::X25519RawSharedSecret(ScopedAlice.Secret, ScopedBob.Public, ScopedRaw));
        FBY_CHECK(ScopedRaw == ScopedExpectedShared);
        std::array<std::byte, 32> ScopedHashed{};
        FBY_CHECK(Crypto::X25519SharedSecret(ScopedAlice.Secret, ScopedBob.Public, ScopedHashed));
        FBY_CHECK(ScopedHashed != ScopedRaw);
    }

    // 7. Opt-in v2 handshake: standard XX name, empty default prologue and raw
    // DH. The transcript starts at the standard initial hash and both sides
    // still agree.
    {
        SecureChannelConfig ScopedV2 = MakeConfig(true, Seed<32>(0x41), false, 0x00);
        ScopedV2.ProtocolV2 = true;
        SecureChannel ScopedInitiatorV2;
        FBY_CHECK(ScopedInitiatorV2.Start(ScopedV2) == SecureStatus::Ok);
        FBY_CHECK(ScopedInitiatorV2.HandshakeHash() == FromHex<Crypto::kSha256Bytes>(kOfficialH0));

        SecureChannelConfig ScopedResponderV2Config = MakeConfig(false, Seed<32>(0x42), false, 0x00);
        ScopedResponderV2Config.ProtocolV2 = true;
        SecureChannel ScopedResponderV2;
        FBY_CHECK(ScopedResponderV2.Start(ScopedResponderV2Config) == SecureStatus::Ok);

        std::array<std::byte, kSecureHandshakeBytes> ScopedV2Msg1{};
        std::array<std::byte, kSecureHandshakeBytes> ScopedV2Msg2{};
        std::array<std::byte, kSecureHandshakeBytes> ScopedV2Msg3{};
        std::size_t ScopedV2Bytes1 = 0;
        std::size_t ScopedV2Bytes2 = 0;
        std::size_t ScopedV2Bytes3 = 0;
        FBY_CHECK(ScopedInitiatorV2.WriteHandshake(ScopedV2Msg1, &ScopedV2Bytes1) == SecureStatus::Ok);
        FBY_CHECK(ScopedResponderV2.ReadHandshake(
                      std::span<const std::byte>(ScopedV2Msg1.data(), ScopedV2Bytes1)) == SecureStatus::Ok);
        FBY_CHECK(ScopedResponderV2.WriteHandshake(ScopedV2Msg2, &ScopedV2Bytes2) == SecureStatus::Ok);
        FBY_CHECK(ScopedInitiatorV2.ReadHandshake(
                      std::span<const std::byte>(ScopedV2Msg2.data(), ScopedV2Bytes2)) == SecureStatus::Ok);
        FBY_CHECK(ScopedInitiatorV2.WriteHandshake(ScopedV2Msg3, &ScopedV2Bytes3) == SecureStatus::Ok);
        FBY_CHECK(ScopedResponderV2.ReadHandshake(
                      std::span<const std::byte>(ScopedV2Msg3.data(), ScopedV2Bytes3)) == SecureStatus::Ok);
        FBY_CHECK(ScopedInitiatorV2.Established());
        FBY_CHECK(ScopedResponderV2.Established());
        FBY_CHECK(ScopedInitiatorV2.HandshakeHash() == ScopedResponderV2.HandshakeHash());
        // The FBY envelope carries the version; the inner message is standard.
        FBY_CHECK(static_cast<std::uint8_t>(ScopedV2Msg1[4]) == 0x02 &&
                  static_cast<std::uint8_t>(ScopedV2Msg1[5]) == 0x00);
        FBY_CHECK(static_cast<std::uint8_t>(ScopedV2Msg2[4]) == 0x02 &&
                  static_cast<std::uint8_t>(ScopedV2Msg2[5]) == 0x00);

        std::array<std::byte, kSecureMaxRecordBytes> ScopedV2Record{};
        std::array<std::byte, kSecureMaxPlainBytes> ScopedV2Opened{};
        std::size_t ScopedV2RecordBytes = 0;
        std::size_t ScopedV2OpenedBytes = 0;
        const std::array<std::byte, 3> ScopedV2Plain{std::byte{'v'}, std::byte{'2'}, std::byte{'!'}};
        FBY_CHECK(ScopedInitiatorV2.Seal(ScopedV2Plain, ScopedV2Record, &ScopedV2RecordBytes) == SecureStatus::Ok);
        FBY_CHECK(ScopedResponderV2.Open(std::span<const std::byte>(ScopedV2Record.data(), ScopedV2RecordBytes),
                                         ScopedV2Opened, &ScopedV2OpenedBytes) == SecureStatus::Ok);
        FBY_CHECK(ScopedV2OpenedBytes == ScopedV2Plain.size());
        FBY_CHECK(std::memcmp(ScopedV2Opened.data(), ScopedV2Plain.data(), ScopedV2Plain.size()) == 0);
    }

    if (GFailures == 0)
    {
        std::printf("noise_kats_test: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "noise_kats_test: %u failure(s)\n", GFailures);
    return 1;
}
