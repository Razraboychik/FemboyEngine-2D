// FemboyEngine2D - SHA-256, HMAC, HKDF and AEAD reference-vector tests.
#include "Engine/Core/CryptoAead.hpp"
#include "Engine/Core/CryptoKdf.hpp"
#include "Engine/Core/Sha256.hpp"

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

[[nodiscard]] unsigned NibbleOf(char InValue) noexcept
{
    if (InValue >= '0' && InValue <= '9')
    {
        return static_cast<unsigned>(InValue - '0');
    }
    return static_cast<unsigned>(InValue - 'a' + 10);
}

[[nodiscard]] std::vector<std::byte> FromHex(std::string_view InHex)
{
    std::vector<std::byte> ScopedResult(InHex.size() / 2);
    for (std::size_t ScopedIndex = 0; ScopedIndex < ScopedResult.size(); ++ScopedIndex)
    {
        ScopedResult[ScopedIndex] =
            static_cast<std::byte>((NibbleOf(InHex[ScopedIndex * 2]) << 4) | NibbleOf(InHex[ScopedIndex * 2 + 1]));
    }
    return ScopedResult;
}

[[nodiscard]] bool EqualsHex(std::span<const std::byte> InBytes, std::string_view InHex)
{
    const std::vector<std::byte> ScopedExpected = FromHex(InHex);
    return InBytes.size() == ScopedExpected.size() &&
           std::memcmp(InBytes.data(), ScopedExpected.data(), InBytes.size()) == 0;
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

[[nodiscard]] std::vector<std::byte> Repeat(std::byte InValue, std::size_t InCount)
{
    return std::vector<std::byte>(InCount, InValue);
}

void TestSha256Vectors()
{
    using namespace Fby::Crypto;

    std::array<std::byte, kSha256Bytes> ScopedDigest{};

    Sha256({}, ScopedDigest);
    FBY_CHECK(EqualsHex(ScopedDigest, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));

    const std::vector<std::byte> ScopedAbc = TextBytes("abc");
    Sha256(ScopedAbc, ScopedDigest);
    FBY_CHECK(EqualsHex(ScopedDigest, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));

    const std::vector<std::byte> ScopedTwoBlocks =
        TextBytes("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq");
    Sha256(ScopedTwoBlocks, ScopedDigest);
    FBY_CHECK(EqualsHex(ScopedDigest, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));

    const std::vector<std::byte> ScopedFourBlocks = TextBytes(
        "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu");
    Sha256(ScopedFourBlocks, ScopedDigest);
    FBY_CHECK(EqualsHex(ScopedDigest, "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1"));
}

void TestSha256Streaming()
{
    using namespace Fby::Crypto;

    const std::vector<std::byte> ScopedMessage = TextBytes("femboy-engine streaming sha256");
    std::array<std::byte, kSha256Bytes> ScopedOneShot{};
    Sha256(ScopedMessage, ScopedOneShot);

    Sha256Context ScopedContext;
    Sha256Init(&ScopedContext);
    for (const std::byte ScopedByte : ScopedMessage)
    {
        Sha256Update(&ScopedContext, std::span<const std::byte>(&ScopedByte, 1));
    }
    std::array<std::byte, kSha256Bytes> ScopedStreamed{};
    Sha256Final(&ScopedContext, ScopedStreamed);
    FBY_CHECK(ScopedOneShot == ScopedStreamed);

    Sha256Context ScopedZeroContext;
    Sha256Init(&ScopedZeroContext);
    std::array<std::byte, 16> ScopedWrongSize{};
    Sha256Final(&ScopedZeroContext, ScopedWrongSize);
    FBY_CHECK(ScopedWrongSize[0] == std::byte{0});
}

void TestHmacSha256()
{
    using namespace Fby::Crypto;

    std::array<std::byte, kSha256Bytes> ScopedMac{};

    const std::vector<std::byte> ScopedKey1 = Repeat(std::byte{0x0B}, 20);
    const std::vector<std::byte> ScopedData1 = TextBytes("Hi There");
    HmacSha256(ScopedKey1, ScopedData1, ScopedMac);
    FBY_CHECK(EqualsHex(ScopedMac, "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7"));

    const std::vector<std::byte> ScopedKey2 = TextBytes("Jefe");
    const std::vector<std::byte> ScopedData2 = TextBytes("what do ya want for nothing?");
    HmacSha256(ScopedKey2, ScopedData2, ScopedMac);
    FBY_CHECK(EqualsHex(ScopedMac, "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843"));

    const std::vector<std::byte> ScopedKey3 = Repeat(std::byte{0xAA}, 20);
    const std::vector<std::byte> ScopedData3 = Repeat(std::byte{0xDD}, 50);
    HmacSha256(ScopedKey3, ScopedData3, ScopedMac);
    FBY_CHECK(EqualsHex(ScopedMac, "773ea91e36800e46854db8ebd09181a72959098b3ef8c122d9635514ced565fe"));

    const std::vector<std::byte> ScopedLongKey = Repeat(std::byte{0xAA}, 131);
    const std::vector<std::byte> ScopedLongData1 =
        TextBytes("Test Using Larger Than Block-Size Key - Hash Key First");
    HmacSha256(ScopedLongKey, ScopedLongData1, ScopedMac);
    FBY_CHECK(EqualsHex(ScopedMac, "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54"));

    const std::vector<std::byte> ScopedLongData2 = TextBytes(
        "This is a test using a larger than block-size key and a larger than block-size data. The key needs to be "
        "hashed before being used by the HMAC algorithm.");
    HmacSha256(ScopedLongKey, ScopedLongData2, ScopedMac);
    FBY_CHECK(EqualsHex(ScopedMac, "9b09ffa71b942fcb27635fbcd5b0e944bfdc63644f0713938a7f51535c3a35e2"));

    std::array<std::byte, 16> ScopedWrongSize{};
    HmacSha256(ScopedKey1, ScopedData1, ScopedWrongSize);
    FBY_CHECK(ScopedWrongSize[0] == std::byte{0});
}

void TestHkdfSha256()
{
    using namespace Fby::Crypto;

    const std::vector<std::byte> ScopedSecret = Repeat(std::byte{0x0B}, 22);
    const std::vector<std::byte> ScopedSalt = FromHex("000102030405060708090a0b0c");
    const std::vector<std::byte> ScopedInfo = FromHex("f0f1f2f3f4f5f6f7f8f9");

    std::array<std::byte, 42> ScopedFortyTwo{};
    HkdfSha256(ScopedSecret, ScopedSalt, ScopedInfo, ScopedFortyTwo);
    FBY_CHECK(EqualsHex(ScopedFortyTwo, "3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf"
                                       "34007208d5b887185865"));

    std::array<std::byte, 42> ScopedNoSalt{};
    HkdfSha256(ScopedSecret, {}, {}, ScopedNoSalt);
    FBY_CHECK(EqualsHex(ScopedNoSalt, "8da4e775a563c18f715f802a063c5a31b8a11f5c5ee1879ec3454e5f3c738d2d"
                                      "9d201395faa4b61a96c8"));

    // Prefix consistency: a shorter request returns the first bytes.
    std::array<std::byte, kSha256Bytes> ScopedFull{};
    HkdfSha256(ScopedSecret, ScopedSalt, ScopedInfo, ScopedFull);
    std::array<std::byte, 16> ScopedShort{};
    HkdfSha256(ScopedSecret, ScopedSalt, ScopedInfo, ScopedShort);
    FBY_CHECK(std::memcmp(ScopedFull.data(), ScopedShort.data(), ScopedShort.size()) == 0);

    HkdfSha256(ScopedSecret, ScopedSalt, ScopedInfo, std::span<std::byte>{});
}

void TestXChaCha20Poly1305()
{
    using namespace Fby::Crypto;

    std::array<std::byte, kXChaChaKeyBytes> ScopedKey{};
    std::array<std::byte, kXChaChaNonceBytes> ScopedNonce{};
    for (std::size_t ScopedIndex = 0; ScopedIndex < ScopedKey.size(); ++ScopedIndex)
    {
        ScopedKey[ScopedIndex] = static_cast<std::byte>(ScopedIndex);
    }
    for (std::size_t ScopedIndex = 0; ScopedIndex < ScopedNonce.size(); ++ScopedIndex)
    {
        ScopedNonce[ScopedIndex] = static_cast<std::byte>(0xF0u - ScopedIndex);
    }

    const std::vector<std::byte> ScopedAad = TextBytes("session-1");
    const std::vector<std::byte> ScopedPlain = TextBytes("femboy engine aead test payload");

    std::vector<std::byte> ScopedCipher(ScopedPlain.size());
    std::array<std::byte, kXChaChaTagBytes> ScopedTag{};
    XChaCha20Poly1305Encrypt(ScopedKey, ScopedNonce, ScopedAad, ScopedPlain, ScopedCipher, ScopedTag);
    FBY_CHECK(std::memcmp(ScopedCipher.data(), ScopedPlain.data(), ScopedPlain.size()) != 0);

    std::vector<std::byte> ScopedRoundTrip(ScopedCipher.size());
    FBY_CHECK(XChaCha20Poly1305Decrypt(ScopedKey, ScopedNonce, ScopedAad, ScopedCipher, ScopedTag, ScopedRoundTrip));
    FBY_CHECK(ScopedRoundTrip == ScopedPlain);

    std::vector<std::byte> ScopedTampered = ScopedCipher;
    ScopedTampered[0] ^= std::byte{0x01};
    std::vector<std::byte> ScopedRejected(ScopedTampered.size());
    FBY_CHECK(!XChaCha20Poly1305Decrypt(ScopedKey, ScopedNonce, ScopedAad, ScopedTampered, ScopedTag, ScopedRejected));
    bool ScopedAllZero = true;
    for (const std::byte ScopedByte : ScopedRejected)
    {
        ScopedAllZero = ScopedAllZero && ScopedByte == std::byte{0};
    }
    FBY_CHECK(ScopedAllZero);

    std::array<std::byte, kXChaChaTagBytes> ScopedBadTag{};
    ScopedBadTag[0] = std::byte{0x7F};
    FBY_CHECK(!XChaCha20Poly1305Decrypt(ScopedKey, ScopedNonce, ScopedAad, ScopedCipher, ScopedBadTag, ScopedRejected));

    // An empty message still authenticates.
    std::array<std::byte, kXChaChaTagBytes> ScopedEmptyTag{};
    XChaCha20Poly1305Encrypt(ScopedKey, ScopedNonce, ScopedAad, {}, {}, ScopedEmptyTag);
    FBY_CHECK(XChaCha20Poly1305Decrypt(ScopedKey, ScopedNonce, ScopedAad, {}, ScopedEmptyTag, {}));
}
}

int main()
{
    TestSha256Vectors();
    TestSha256Streaming();
    TestHmacSha256();
    TestHkdfSha256();
    TestXChaCha20Poly1305();

    if (GFailures == 0)
    {
        std::printf("crypto_hash_test: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "crypto_hash_test: %u failure(s)\n", GFailures);
    return 1;
}
