// FemboyEngine2D - BLAKE2b, HMAC, HKDF, X25519 and EdDSA primitive tests.
#include "Engine/Core/CryptoAsymmetric.hpp"
#include "Engine/Core/CryptoKdf.hpp"

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

void TestBlake2b()
{
    using namespace Fby::Crypto;

    std::array<std::byte, kBlake2bBytes> ScopedDigest{};
    Blake2b({}, ScopedDigest);
    FBY_CHECK(EqualsHex(ScopedDigest, "786a02f742015903c6c6fd852552d272912f4740e15847618a86e217f71f5419"
                                     "d25e1031afee585313896444934eb04b903a685b1448b755d56f701afe9be2ce"));

    const std::vector<std::byte> ScopedAbc = TextBytes("abc");
    Blake2b(ScopedAbc, ScopedDigest);
    FBY_CHECK(EqualsHex(ScopedDigest, "ba80a53f981c4d0d6a2797b69f12f6e94c212f14685ac4b74b12bb6fdbffa2d1"
                                     "7d87c5392aab792dc252d5de4533cc9518d38aa8dbf1925ab92386edd4009923"));

    const std::vector<std::byte> ScopedFox = TextBytes("The quick brown fox jumps over the lazy dog");
    Blake2b(ScopedFox, ScopedDigest);
    FBY_CHECK(EqualsHex(ScopedDigest, "a8add4bdddfd93e4877d2746e62817b116364a1fa7bc148d95090bc7333b3673"
                                     "f82401cf7aa2e4cb1ecd90296e3f14cb5413f8ed77be73045b13914cdcd6a918"));

    std::array<std::byte, 32> ScopedShort{};
    Blake2b({}, ScopedShort);
    FBY_CHECK(EqualsHex(ScopedShort, "0e5751c026e543b2e8ab2eb06099daa1d1e5df47778f7787faab45cdf12fe3a8"));

    std::array<std::byte, 65> ScopedTooLong{};
    Blake2b(ScopedAbc, ScopedTooLong);
    Blake2b(ScopedAbc, std::span<std::byte>{});
}

void TestKeyedBlake2b()
{
    using namespace Fby::Crypto;

    std::array<std::byte, 32> ScopedKey{};
    for (std::size_t ScopedIndex = 0; ScopedIndex < ScopedKey.size(); ++ScopedIndex)
    {
        ScopedKey[ScopedIndex] = static_cast<std::byte>(ScopedIndex);
    }
    const std::vector<std::byte> ScopedMessage = TextBytes("femboy-engine network key derivation");

    std::array<std::byte, kBlake2bBytes> ScopedDigest{};
    Blake2bKeyed(ScopedKey, ScopedMessage, ScopedDigest);
    FBY_CHECK(EqualsHex(ScopedDigest, "8c382d5ee9ed0f2b698279e4594d0bb9e31e1eafe9d5fd5adb49b020acf12e76"
                                     "c0ade2542f7272149fdf8f8e9381807c3f378c620b161370d31a803aa2f5d5f3"));

    std::array<std::byte, 65> ScopedOverlongKey{};
    Blake2bKeyed(ScopedOverlongKey, ScopedMessage, ScopedDigest);
    FBY_CHECK(ScopedDigest[0] == std::byte{0});
    FBY_CHECK(ScopedDigest[63] == std::byte{0});
}

void TestHmac()
{
    using namespace Fby::Crypto;

    const std::vector<std::byte> ScopedKey = TextBytes("secret key");
    const std::vector<std::byte> ScopedMessage = TextBytes("The quick brown fox jumps over the lazy dog");

    std::array<std::byte, kBlake2bBytes> ScopedMac{};
    HmacBlake2b(ScopedKey, ScopedMessage, ScopedMac);
    FBY_CHECK(EqualsHex(ScopedMac, "c450f24e7ca7facceeb920a34cd19d054e74695bb14b19b5028fac45a6dad05f"
                                  "14b4bf3633e1c51c60e4277f874236b3ac940a1672576724d87d6afafb18aee7"));

    std::array<std::byte, 32> ScopedShort{};
    HmacBlake2b(ScopedKey, ScopedMessage, ScopedShort);
    FBY_CHECK(ScopedShort[0] == std::byte{0});
}

void TestHkdf()
{
    using namespace Fby::Crypto;

    std::array<std::byte, 32> ScopedSecret{};
    ScopedSecret.fill(std::byte{0x0B});
    const std::vector<std::byte> ScopedSalt = FromHex("000102030405060708090a0b0c");
    const std::vector<std::byte> ScopedInfo = FromHex("f0f1f2f3f4f5f6f7f8f9");

    std::array<std::byte, 42> ScopedFortyTwo{};
    HkdfBlake2b(ScopedSecret, ScopedSalt, ScopedInfo, ScopedFortyTwo);
    FBY_CHECK(EqualsHex(ScopedFortyTwo, "8137831a566f11254c6368875f53cc56ffb34e73a826f685cfbf31156dff25a1"
                                       "4934a296d56d7c01f087"));

    std::array<std::byte, kBlake2bBytes> ScopedSixtyFour{};
    HkdfBlake2b(ScopedSecret, {}, {}, ScopedSixtyFour);
    FBY_CHECK(EqualsHex(ScopedSixtyFour, "8b280a074b0fce799042ef62ffd397ca7ac55e0c2a7e79ccb16cfcf4e2c6bae5"
                                         "f067f1d07299752e3d684453a492aabbf9a6f96b5fd21173e32df0bdeb6be320"));

    // HKDF expand is prefix consistent: the first 42 bytes of the 64-byte
    // output equal a 42-byte request with the same salt and info.
    std::array<std::byte, 42> ScopedPrefix{};
    HkdfBlake2b(ScopedSecret, ScopedSalt, ScopedInfo, ScopedPrefix);
    FBY_CHECK(std::memcmp(ScopedPrefix.data(), ScopedFortyTwo.data(), ScopedPrefix.size()) == 0);

    // A different info string must produce unrelated key material.
    const std::vector<std::byte> ScopedOtherInfo = FromHex("000102030405060708090a0b0c0d0e0f");
    std::array<std::byte, 42> ScopedOther{};
    HkdfBlake2b(ScopedSecret, ScopedSalt, ScopedOtherInfo, ScopedOther);
    FBY_CHECK(std::memcmp(ScopedOther.data(), ScopedFortyTwo.data(), ScopedOther.size()) != 0);

    // Rejected output sizes must be safe no-ops.
    HkdfBlake2b(ScopedSecret, ScopedSalt, ScopedInfo, std::span<std::byte>{});
}

void TestX25519()
{
    using namespace Fby::Crypto;

    const std::vector<std::byte> ScopedAliceSeed = FromHex("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a");
    const std::vector<std::byte> ScopedBobSeed = FromHex("5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb");

    X25519KeyPair ScopedAlice{};
    X25519KeyPair ScopedBob{};
    FBY_CHECK(X25519KeyPairFromSeed(ScopedAliceSeed, &ScopedAlice));
    FBY_CHECK(X25519KeyPairFromSeed(ScopedBobSeed, &ScopedBob));
    FBY_CHECK(EqualsHex(ScopedAlice.Public, "8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a"));
    FBY_CHECK(EqualsHex(ScopedBob.Public, "de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f"));

    std::array<std::byte, kX25519KeyBytes> ScopedFromAlice{};
    std::array<std::byte, kX25519KeyBytes> ScopedFromBob{};
    FBY_CHECK(X25519SharedSecret(ScopedAlice.Secret, ScopedBob.Public, ScopedFromAlice));
    FBY_CHECK(X25519SharedSecret(ScopedBob.Secret, ScopedAlice.Public, ScopedFromBob));
    FBY_CHECK(ScopedFromAlice == ScopedFromBob);
    FBY_CHECK(EqualsHex(ScopedFromAlice, "bb16f461d45d47c32a89c90de36d7c7902b314364611d6e4a58247162ec9d4d9"));

    // A low-order point must be rejected instead of yielding a shared secret.
    std::array<std::byte, kX25519KeyBytes> ScopedLowOrder{};
    std::array<std::byte, kX25519KeyBytes> ScopedRejected{};
    FBY_CHECK(!X25519SharedSecret(ScopedAlice.Secret, ScopedLowOrder, ScopedRejected));

    FBY_CHECK(!X25519KeyPairFromSeed(std::span<const std::byte>{}, &ScopedAlice));
    FBY_CHECK(!X25519SharedSecret(std::span<const std::byte>(ScopedAliceSeed.data(), 16), ScopedBob.Public,
                                  ScopedRejected));
}

void TestEddsa()
{
    using namespace Fby::Crypto;

    const std::vector<std::byte> ScopedSeed = FromHex("9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60");
    EddsaKeyPair ScopedPair{};
    FBY_CHECK(EddsaKeyPairFromSeed(ScopedSeed, &ScopedPair));
    FBY_CHECK(std::memcmp(ScopedPair.Secret.data() + kEddsaPublicBytes, ScopedPair.Public.data(), kEddsaPublicBytes) == 0);

    const std::vector<std::byte> ScopedMessage = TextBytes("femboy-engine ticket v1");

    std::array<std::byte, kEddsaSignatureBytes> ScopedFirst{};
    EddsaSign(ScopedPair.Secret, ScopedMessage, ScopedFirst);
    FBY_CHECK(EddsaVerify(ScopedPair.Public, ScopedMessage, ScopedFirst));

    std::array<std::byte, kEddsaSignatureBytes> ScopedSecond{};
    EddsaSign(ScopedPair.Secret, ScopedMessage, ScopedSecond);
    FBY_CHECK(ScopedFirst == ScopedSecond);

    std::vector<std::byte> ScopedTampered = ScopedMessage;
    ScopedTampered[0] ^= std::byte{0x01};
    FBY_CHECK(!EddsaVerify(ScopedPair.Public, ScopedTampered, ScopedFirst));

    std::array<std::byte, kEddsaSignatureBytes> ScopedTamperedSignature = ScopedFirst;
    ScopedTamperedSignature[kEddsaSignatureBytes - 1] ^= std::byte{0x01};
    FBY_CHECK(!EddsaVerify(ScopedPair.Public, ScopedMessage, ScopedTamperedSignature));

    EddsaKeyPair ScopedOther{};
    const std::vector<std::byte> ScopedOtherSeed =
        FromHex("4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb");
    FBY_CHECK(EddsaKeyPairFromSeed(ScopedOtherSeed, &ScopedOther));
    FBY_CHECK(!EddsaVerify(ScopedOther.Public, ScopedMessage, ScopedFirst));

    std::array<std::byte, 32> ScopedShortSignature{};
    EddsaSign(ScopedPair.Secret, ScopedMessage, ScopedShortSignature);
    FBY_CHECK(ScopedShortSignature[0] == std::byte{0});
    FBY_CHECK(!EddsaVerify(ScopedPair.Public, ScopedMessage, ScopedShortSignature));

    FBY_CHECK(!EddsaKeyPairFromSeed(std::span<const std::byte>{}, &ScopedPair));
}
}

int main()
{
    TestBlake2b();
    TestKeyedBlake2b();
    TestHmac();
    TestHkdf();
    TestX25519();
    TestEddsa();

    if (GFailures == 0)
    {
        std::printf("crypto_primitives_test: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "crypto_primitives_test: %u failure(s)\n", GFailures);
    return 1;
}
