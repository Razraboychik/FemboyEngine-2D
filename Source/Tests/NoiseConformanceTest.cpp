// FemboyEngine2D - Noise XX conformance against the vendored noise-c vector.
#include "Engine/Assets/FemCrypto.hpp"
#include "Engine/Core/CryptoAsymmetric.hpp"
#include "Engine/Network/ByteOrder.hpp"
#include "Engine/Network/SecureChannel.hpp"
#include "Vectors/NoiseXxChaChaPolySha256Vector.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
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

[[nodiscard]] std::uint8_t HexNibble(char InCharacter)
{
    if (InCharacter >= '0' && InCharacter <= '9')
    {
        return static_cast<std::uint8_t>(InCharacter - '0');
    }
    if (InCharacter >= 'a' && InCharacter <= 'f')
    {
        return static_cast<std::uint8_t>(InCharacter - 'a' + 10);
    }
    if (InCharacter >= 'A' && InCharacter <= 'F')
    {
        return static_cast<std::uint8_t>(InCharacter - 'A' + 10);
    }
    return 0xFFu;
}

template <std::size_t N>
[[nodiscard]] std::array<std::byte, N> FromHex(const char* InText)
{
    std::array<std::byte, N> ScopedBytes{};
    if (std::strlen(InText) != N * 2)
    {
        FBY_CHECK(false);
        return ScopedBytes;
    }
    for (std::size_t ScopedIndex = 0; ScopedIndex < N; ++ScopedIndex)
    {
        const std::uint8_t ScopedHigh = HexNibble(InText[ScopedIndex * 2]);
        const std::uint8_t ScopedLow = HexNibble(InText[ScopedIndex * 2 + 1]);
        FBY_CHECK(ScopedHigh != 0xFFu && ScopedLow != 0xFFu);
        ScopedBytes[ScopedIndex] = static_cast<std::byte>((ScopedHigh << 4) | ScopedLow);
    }
    return ScopedBytes;
}

[[nodiscard]] std::vector<std::byte> FromHexVector(const char* InText)
{
    const std::size_t ScopedCount = std::strlen(InText);
    std::vector<std::byte> ScopedBytes(ScopedCount / 2);
    for (std::size_t ScopedIndex = 0; ScopedIndex < ScopedBytes.size(); ++ScopedIndex)
    {
        const std::uint8_t ScopedHigh = HexNibble(InText[ScopedIndex * 2]);
        const std::uint8_t ScopedLow = HexNibble(InText[ScopedIndex * 2 + 1]);
        FBY_CHECK(ScopedHigh != 0xFFu && ScopedLow != 0xFFu);
        ScopedBytes[ScopedIndex] = static_cast<std::byte>((ScopedHigh << 4) | ScopedLow);
    }
    return ScopedBytes;
}

[[nodiscard]] Fby::Crypto::X25519KeyPair PairFromHex(const char* InPrivateKeyHex)
{
    const std::array<std::byte, Fby::Crypto::kX25519KeyBytes> ScopedSeed =
        FromHex<Fby::Crypto::kX25519KeyBytes>(InPrivateKeyHex);
    Fby::Crypto::X25519KeyPair ScopedPair{};
    FBY_CHECK(Fby::Crypto::X25519KeyPairFromSeed(ScopedSeed, &ScopedPair));
    return ScopedPair;
}

void CopyPrologue(const char* InHex, Fby::Net::SecureChannelConfig& InConfig)
{
    const std::vector<std::byte> ScopedPrologue = FromHexVector(InHex);
    FBY_CHECK(ScopedPrologue.size() <= Fby::Net::kSecureMaxPrologueBytes);
    InConfig.PrologueBytes = ScopedPrologue.size();
    std::memcpy(InConfig.Prologue.data(), ScopedPrologue.data(), ScopedPrologue.size());
}

[[nodiscard]] bool MatchesVector(std::span<const std::byte> InMessage, const char* InExpectedHex)
{
    const std::vector<std::byte> ScopedExpected = FromHexVector(InExpectedHex);
    if (InMessage.size() != ScopedExpected.size())
    {
        std::fprintf(stderr, "conformance: message length %zu, expected %zu\n", InMessage.size(),
                     ScopedExpected.size());
        return false;
    }
    return std::memcmp(InMessage.data(), ScopedExpected.data(), ScopedExpected.size()) == 0;
}

// Compares a transport ciphertext with the reference by decrypting it under the
// engine's split key and by re-encrypting the payload, so both the key bytes and
// the nonce rules of the split are checked.
void CheckTransport(const std::array<std::byte, Fby::Crypto::kAeadKeyBytes>& InSendKey, std::uint64_t InNonce,
                    const char* InCipherHex, const char* InPayloadHex)
{
    const std::vector<std::byte> ScopedCipher = FromHexVector(InCipherHex);
    const std::vector<std::byte> ScopedPayload = FromHexVector(InPayloadHex);
    FBY_CHECK(ScopedCipher.size() == ScopedPayload.size() + Fby::Net::kSecureTagBytes);

    Fby::Crypto::AeadKey ScopedKey;
    std::memcpy(ScopedKey.bytes.data(), InSendKey.data(), InSendKey.size());
    Fby::Crypto::AeadNonce ScopedNonce;
    ScopedNonce.SetCounter(InNonce);

    std::vector<std::byte> ScopedPlain(ScopedPayload.size());
    Fby::Crypto::AeadTag ScopedReadTag{};
    std::memcpy(ScopedReadTag.bytes.data(), ScopedCipher.data() + ScopedPayload.size(), ScopedReadTag.bytes.size());
    FBY_CHECK(Fby::Crypto::ChaCha20Poly1305Decrypt(
        ScopedKey, ScopedNonce, {}, std::span<const std::byte>(ScopedCipher.data(), ScopedPayload.size()), ScopedPlain,
        ScopedReadTag));
    FBY_CHECK(ScopedPlain == ScopedPayload);

    std::vector<std::byte> ScopedEncrypted(ScopedPayload.size());
    Fby::Crypto::AeadTag ScopedWriteTag{};
    FBY_CHECK(Fby::Crypto::ChaCha20Poly1305Encrypt(ScopedKey, ScopedNonce, {}, ScopedPayload, ScopedEncrypted,
                                                   ScopedWriteTag));
    FBY_CHECK(std::memcmp(ScopedEncrypted.data(), ScopedCipher.data(), ScopedPayload.size()) == 0);
    FBY_CHECK(std::memcmp(ScopedWriteTag.bytes.data(), ScopedCipher.data() + ScopedPayload.size(),
                          ScopedWriteTag.bytes.size()) == 0);
}
}

int main()
{
    using namespace Fby;
    using namespace Fby::Net::Conformance;

    // The reference vector names the protocol this engine must speak on the v2
    // path; the FBY envelope around each message is not part of the transcript.
    FBY_CHECK(std::strcmp(kVectorName, "Noise_XX_25519_ChaChaPoly_SHA256") == 0);

    Net::SecureChannelConfig ScopedInitiatorConfig{};
    ScopedInitiatorConfig.LocalStatic = PairFromHex(kInitiatorStatic);
    ScopedInitiatorConfig.Initiator = true;
    ScopedInitiatorConfig.ProtocolV2 = true;
    ScopedInitiatorConfig.HasFixedEphemeral = true;
    ScopedInitiatorConfig.FixedEphemeralSeed = FromHex<Crypto::kX25519KeyBytes>(kInitiatorEphemeral);
    CopyPrologue(kInitiatorPrologue, ScopedInitiatorConfig);

    Net::SecureChannelConfig ScopedResponderConfig{};
    ScopedResponderConfig.LocalStatic = PairFromHex(kResponderStatic);
    ScopedResponderConfig.Initiator = false;
    ScopedResponderConfig.ProtocolV2 = true;
    ScopedResponderConfig.HasFixedEphemeral = true;
    ScopedResponderConfig.FixedEphemeralSeed = FromHex<Crypto::kX25519KeyBytes>(kResponderEphemeral);
    CopyPrologue(kResponderPrologue, ScopedResponderConfig);

    Net::SecureChannel ScopedInitiator;
    Net::SecureChannel ScopedResponder;
    FBY_CHECK(ScopedInitiator.Start(ScopedInitiatorConfig) == Net::SecureStatus::Ok);
    FBY_CHECK(ScopedResponder.Start(ScopedResponderConfig) == Net::SecureStatus::Ok);

    const std::vector<std::byte> ScopedPayload1 = FromHexVector(kHandshake1Payload);
    const std::vector<std::byte> ScopedPayload2 = FromHexVector(kHandshake2Payload);
    const std::vector<std::byte> ScopedPayload3 = FromHexVector(kHandshake3Payload);

    std::array<std::byte, Net::kSecureHandshakeBytes> ScopedMessage{};
    std::array<std::byte, Net::kSecureMaxHandshakePayloadBytes> ScopedReadPayload{};
    std::size_t ScopedSize = 0;
    std::size_t ScopedPayloadSize = 0;

    // Message 1: e, then the cleartext payload.
    FBY_CHECK(ScopedInitiator.WriteHandshake(ScopedMessage, &ScopedSize, ScopedPayload1) == Net::SecureStatus::Ok);
    FBY_CHECK(MatchesVector(std::span<const std::byte>(ScopedMessage.data() + Net::kSecureHandshakeHeaderBytes,
                                                       ScopedSize - Net::kSecureHandshakeHeaderBytes),
                            kHandshake1));
    FBY_CHECK(Net::Detail::LoadU16(reinterpret_cast<const std::uint8_t*>(ScopedMessage.data()) + 4) ==
              Net::kSecureVersionV2);
    FBY_CHECK(ScopedResponder.ReadHandshake(
                  std::span<const std::byte>(ScopedMessage.data(), ScopedSize), ScopedReadPayload,
                  &ScopedPayloadSize) == Net::SecureStatus::Ok);
    FBY_CHECK(ScopedPayloadSize == ScopedPayload1.size());
    FBY_CHECK(std::memcmp(ScopedReadPayload.data(), ScopedPayload1.data(), ScopedPayload1.size()) == 0);

    // Message 2: e, ee, encrypted static, es, then the encrypted payload.
    FBY_CHECK(ScopedResponder.WriteHandshake(ScopedMessage, &ScopedSize, ScopedPayload2) == Net::SecureStatus::Ok);
    FBY_CHECK(MatchesVector(std::span<const std::byte>(ScopedMessage.data() + Net::kSecureHandshakeHeaderBytes,
                                                       ScopedSize - Net::kSecureHandshakeHeaderBytes),
                            kHandshake2));
    FBY_CHECK(ScopedInitiator.ReadHandshake(
                  std::span<const std::byte>(ScopedMessage.data(), ScopedSize), ScopedReadPayload,
                  &ScopedPayloadSize) == Net::SecureStatus::Ok);
    FBY_CHECK(ScopedPayloadSize == ScopedPayload2.size());
    FBY_CHECK(std::memcmp(ScopedReadPayload.data(), ScopedPayload2.data(), ScopedPayload2.size()) == 0);

    // Message 3: encrypted static, se, then the encrypted payload.
    FBY_CHECK(ScopedInitiator.WriteHandshake(ScopedMessage, &ScopedSize, ScopedPayload3) == Net::SecureStatus::Ok);
    FBY_CHECK(MatchesVector(std::span<const std::byte>(ScopedMessage.data() + Net::kSecureHandshakeHeaderBytes,
                                                       ScopedSize - Net::kSecureHandshakeHeaderBytes),
                            kHandshake3));
    FBY_CHECK(ScopedResponder.ReadHandshake(
                  std::span<const std::byte>(ScopedMessage.data(), ScopedSize), ScopedReadPayload,
                  &ScopedPayloadSize) == Net::SecureStatus::Ok);
    FBY_CHECK(ScopedPayloadSize == ScopedPayload3.size());
    FBY_CHECK(std::memcmp(ScopedReadPayload.data(), ScopedPayload3.data(), ScopedPayload3.size()) == 0);

    FBY_CHECK(ScopedInitiator.Established());
    FBY_CHECK(ScopedResponder.Established());
    const std::array<std::byte, Crypto::kSha256Bytes> ScopedExpectedHash = FromHex<Crypto::kSha256Bytes>(kHandshakeHash);
    FBY_CHECK(ScopedInitiator.HandshakeHash() == ScopedExpectedHash);
    FBY_CHECK(ScopedResponder.HandshakeHash() == ScopedExpectedHash);

#if defined(FBY_ENGINE_TEST_HOOKS)
    // The split keys are not exposed by noise-c, so they are checked by
    // reproducing the reference transport messages: a different key, nonce or
    // direction assignment cannot produce the same ciphertext.
    std::array<std::byte, Crypto::kAeadKeyBytes> ScopedInitiatorSend{};
    std::array<std::byte, Crypto::kAeadKeyBytes> ScopedInitiatorReceive{};
    std::array<std::byte, Crypto::kAeadKeyBytes> ScopedResponderSend{};
    std::array<std::byte, Crypto::kAeadKeyBytes> ScopedResponderReceive{};
    ScopedInitiator.ExportTransportKeys(ScopedInitiatorSend, ScopedInitiatorReceive);
    ScopedResponder.ExportTransportKeys(ScopedResponderSend, ScopedResponderReceive);
    FBY_CHECK(ScopedInitiatorSend == ScopedResponderReceive);
    FBY_CHECK(ScopedInitiatorReceive == ScopedResponderSend);

    // The reference transport list starts with the responder because the
    // handshake exchange ends on the responder's turn to receive.
    CheckTransport(ScopedResponderSend, 0, kTransport1, kTransport1Payload);
    CheckTransport(ScopedInitiatorSend, 0, kTransport2, kTransport2Payload);
    CheckTransport(ScopedResponderSend, 1, kTransport3, kTransport3Payload);
#else
    std::fprintf(stderr, "noise_conformance: test hooks are disabled\n");
    ++GFailures;
#endif

    if (GFailures == 0)
    {
        std::printf("noise_conformance_test: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "noise_conformance_test: %u failure(s)\n", GFailures);
    return 1;
}
