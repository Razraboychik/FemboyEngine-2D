// FemboyEngine2D - SDK build profile, crypto and .femarc tests.
#include "Engine/Core/BuildProfile.hpp"
#include "Engine/Assets/FemArchiver.hpp"
#include "Engine/Assets/FemCrypto.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <string>
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

std::vector<std::byte> FromHex(const char* InText)
{
    const std::size_t ScopedCount = std::strlen(InText) / 2;
    std::vector<std::byte> ScopedResult(ScopedCount);
    for (std::size_t ScopedI = 0; ScopedI < ScopedCount; ++ScopedI)
    {
        auto ScopedNibble = [](char InValue) -> unsigned
        {
            if (InValue >= '0' && InValue <= '9')
            {
                return static_cast<unsigned>(InValue - '0');
            }
            return static_cast<unsigned>(InValue - 'a' + 10);
        };
        ScopedResult[ScopedI] = static_cast<std::byte>((ScopedNibble(InText[ScopedI * 2]) << 4) |
                                                       ScopedNibble(InText[ScopedI * 2 + 1]));
    }
    return ScopedResult;
}

bool EqualsHex(std::span<const std::byte> InBytes, const char* InHex)
{
    const std::vector<std::byte> ScopedExpected = FromHex(InHex);
    return InBytes.size() == ScopedExpected.size() &&
           std::memcmp(InBytes.data(), ScopedExpected.data(), InBytes.size()) == 0;
}
}

int main()
{
    using namespace Fby;
    using namespace Fby::Build;

    static_assert(OptimizationName(OptimizationLevel::Ofast) == "Ofast");
    FBY_CHECK(ParseOptimizationLevel("O3") == OptimizationLevel::O3);
    FBY_CHECK(ParseOptimizationLevel("wrong", OptimizationLevel::O1) == OptimizationLevel::O1);
    FBY_CHECK(OptimizationFlag(OptimizationLevel::Os) == "-Os");
    FBY_CHECK(OptimizationFlagMsvc(OptimizationLevel::Os) == "/O1");
    FBY_CHECK(RelaxesFloatingPoint(OptimizationLevel::Ofast));
    FBY_CHECK(!RelaxesFloatingPoint(OptimizationLevel::O2));
    FBY_CHECK(Recommend(OptimizationLevel::O2, true, false).shippingScore == 5);
    FBY_CHECK(Recommend(OptimizationLevel::Ofast, true, false).shippingScore < 3);
    static_assert(sizeof(BuildProfile) <= 8);

    Crypto::AeadKey ScopedChaChaKey{};
    const std::vector<std::byte> ScopedKeyBytes =
        FromHex("808182838485868788898a8b8c8d8e8f909192939495969798999a9b9c9d9e9f");
    std::memcpy(ScopedChaChaKey.bytes.data(), ScopedKeyBytes.data(), 32);
    Crypto::AeadNonce ScopedNonce{};
    const std::vector<std::byte> ScopedNonceBytes = FromHex("070000004041424344454647");
    std::memcpy(ScopedNonce.bytes.data(), ScopedNonceBytes.data(), 12);
    const std::vector<std::byte> ScopedAad = FromHex("50515253c0c1c2c3c4c5c6c7");
    const char* ScopedPlainText =
        "Ladies and Gentlemen of the class of '99: If I could offer you only one tip for the future, sunscreen would "
        "be it.";
    const std::span<const std::byte> ScopedPlain(reinterpret_cast<const std::byte*>(ScopedPlainText),
                                                 std::strlen(ScopedPlainText));
    std::vector<std::byte> ScopedCipher(ScopedPlain.size());
    Crypto::AeadTag ScopedTag{};
    FBY_CHECK(Crypto::ChaCha20Poly1305Encrypt(ScopedChaChaKey, ScopedNonce, ScopedAad, ScopedPlain, ScopedCipher,
                                              ScopedTag));
    FBY_CHECK(EqualsHex(std::span<const std::byte>(ScopedCipher).first(16), "d31a8d34648e60db7b86afbc53ef7ec2"));
    FBY_CHECK(EqualsHex(ScopedTag.bytes, "1ae10b594f09e26a7e902ecbd0600691"));
    std::vector<std::byte> ScopedDecrypted(ScopedPlain.size());
    FBY_CHECK(Crypto::ChaCha20Poly1305Decrypt(ScopedChaChaKey, ScopedNonce, ScopedAad, ScopedCipher, ScopedDecrypted,
                                              ScopedTag));
    FBY_CHECK(std::memcmp(ScopedDecrypted.data(), ScopedPlain.data(), ScopedPlain.size()) == 0);
    ScopedTag.bytes[0] ^= std::byte{1};
    FBY_CHECK(!Crypto::ChaCha20Poly1305Decrypt(ScopedChaChaKey, ScopedNonce, ScopedAad, ScopedCipher, ScopedDecrypted,
                                               ScopedTag));

    Crypto::AeadKey ScopedPolyKey{};
    const std::vector<std::byte> ScopedPolyBytes =
        FromHex("85d6be7857556d337f4452fe42d506a80103808afb0db2fd4abff6af4149f51b");
    std::memcpy(ScopedPolyKey.bytes.data(), ScopedPolyBytes.data(), 32);
    const char* ScopedPolyMessage = "Cryptographic Forum Research Group";
    std::array<std::byte, 16> ScopedPolyTag{};
    FBY_CHECK(Crypto::Poly1305Mac(ScopedPolyKey,
                                  std::span<const std::byte>(reinterpret_cast<const std::byte*>(ScopedPolyMessage),
                                                             std::strlen(ScopedPolyMessage)),
                                  ScopedPolyTag));
    FBY_CHECK(EqualsHex(ScopedPolyTag, "a8061dc1305136c6c22b8baf0c0127a9"));

    Crypto::AeadKey ScopedAesKey{};
    Crypto::AeadNonce ScopedAesNonce{};
    std::array<std::byte, 16> ScopedZeroPlain{};
    std::array<std::byte, 16> ScopedAesCipher{};
    Crypto::AeadTag ScopedAesTag{};
    FBY_CHECK(Crypto::Aes256GcmEncrypt(ScopedAesKey, ScopedAesNonce, {}, ScopedZeroPlain, ScopedAesCipher,
                                       ScopedAesTag));
    FBY_CHECK(EqualsHex(ScopedAesCipher, "cea7403d4d606b6e074ec5d3baf39d18"));
    FBY_CHECK(EqualsHex(ScopedAesTag.bytes, "d0d1c8a799996bf0265b98b5d48ab919"));

    std::string ScopedText;
    for (int ScopedI = 0; ScopedI < 300; ++ScopedI)
    {
        ScopedText += "FemboyEngine distribution payload ";
    }
    std::vector<std::byte> ScopedBig(ScopedText.size());
    std::memcpy(ScopedBig.data(), ScopedText.data(), ScopedText.size());
    std::vector<std::byte> ScopedScratch(1u << 18);
    std::vector<std::byte> ScopedCompressed(ScopedBig.size() + 1024);
    std::size_t ScopedCompressedSize = 0;
    FBY_CHECK(Assets::FbyLzCompress(ScopedBig, ScopedCompressed, ScopedScratch, &ScopedCompressedSize));
    FBY_CHECK(ScopedCompressedSize < ScopedBig.size());
    std::vector<std::byte> ScopedRoundTrip(ScopedBig.size());
    FBY_CHECK(Assets::FbyLzDecompress(std::span<const std::byte>(ScopedCompressed).first(ScopedCompressedSize),
                                      ScopedRoundTrip));
    FBY_CHECK(std::memcmp(ScopedRoundTrip.data(), ScopedBig.data(), ScopedBig.size()) == 0);

    Assets::ArchiveInput ScopedEntries[2] = {
        {"engine/assets.fempak", std::span<const std::byte>(ScopedBig)},
        {"manifest.txt", std::span<const std::byte>(ScopedBig).first(23)},
    };
    Assets::ArchiveBuildOptions ScopedOptions{};
    ScopedOptions.codec = Assets::ArchiveCodec::FbyLz;
    ScopedOptions.scratch = ScopedScratch;
    ScopedOptions.cipher = Assets::ArchiveCipher::Aes256Gcm;
    ScopedOptions.key = &ScopedAesKey;
    ScopedOptions.nonce = ScopedNonce;
    std::vector<std::byte> ScopedArchive(Assets::FemArchiver::MaxArchiveBytes(ScopedEntries,
                                                                              Assets::ArchiveCodec::FbyLz));
    std::size_t ScopedUsed = 0;
    FBY_CHECK(Assets::FemArchiver::Build(ScopedEntries, ScopedArchive, ScopedOptions, &ScopedUsed) ==
              Assets::ArchiveStatus::Ok);
    ScopedArchive.resize(ScopedUsed);
    std::vector<std::byte> ScopedBody(ScopedUsed);
    Assets::ArchiveReader ScopedReader;
    FBY_CHECK(Assets::FemArchiver::Open(ScopedArchive, &ScopedAesKey, ScopedBody, ScopedReader) ==
              Assets::ArchiveStatus::Ok);
    FBY_CHECK(ScopedReader.EntryCount() == 2);
    const std::uint32_t ScopedIndex = ScopedReader.Find("engine/assets.fempak");
    FBY_CHECK(ScopedIndex == 0);
    Assets::ArchiveEntryView ScopedView{};
    FBY_CHECK(ScopedReader.Entry(ScopedIndex, ScopedView) == Assets::ArchiveStatus::Ok);
    FBY_CHECK(ScopedView.codec == Assets::ArchiveCodec::FbyLz);
    std::vector<std::byte> ScopedExtracted(static_cast<std::size_t>(ScopedView.rawSize));
    FBY_CHECK(ScopedReader.Extract(ScopedIndex, ScopedExtracted, ScopedScratch) == Assets::ArchiveStatus::Ok);
    FBY_CHECK(ScopedExtracted == ScopedBig);
    ScopedArchive[160] ^= std::byte{0x20};
    FBY_CHECK(Assets::FemArchiver::Open(ScopedArchive, &ScopedAesKey, ScopedBody, ScopedReader) ==
              Assets::ArchiveStatus::AuthenticationFailed);

    if (GFailures == 0)
    {
        std::printf("sdk_crypto_test: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "sdk_crypto_test: %u failure(s)\n", GFailures);
    return 1;
}
