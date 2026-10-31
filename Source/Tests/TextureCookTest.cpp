// FemboyEngine2D - texture cooking tests.
#include "Engine/Assets/TextureCook.hpp"

#include "Engine/Assets/TextureFormat.hpp"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace
{

int GFailures = 0;

#define FBY_CHECK(condition)                                                                                           \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #condition);                                  \
            ++GFailures;                                                                                              \
        }                                                                                                              \
    } while (0)

using Bytes = std::vector<std::byte>;

// Synthetic sources
[[nodiscard]] std::vector<std::uint8_t> MakeGradient(std::uint32_t InWidth, std::uint32_t InHeight)
{
    std::vector<std::uint8_t> ScopedPixels(static_cast<std::size_t>(InWidth) * InHeight * 4);
    for (std::uint32_t ScopedY = 0; ScopedY < InHeight; ++ScopedY)
    {
        for (std::uint32_t ScopedX = 0; ScopedX < InWidth; ++ScopedX)
        {
            std::uint8_t* ScopedTexel =
                ScopedPixels.data() + (static_cast<std::size_t>(ScopedY) * InWidth + ScopedX) * 4;
            ScopedTexel[0] = static_cast<std::uint8_t>((ScopedX * 255u) / (InWidth > 1 ? InWidth - 1 : 1));
            ScopedTexel[1] = static_cast<std::uint8_t>((ScopedY * 255u) / (InHeight > 1 ? InHeight - 1 : 1));
            ScopedTexel[2] = static_cast<std::uint8_t>(64u + ((ScopedX + ScopedY) * 127u) / (InWidth + InHeight));
            ScopedTexel[3] = 255;
        }
    }
    return ScopedPixels;
}

void AppendToVector(void* InContext, void* InData, int InSize)
{
    auto* ScopedBytes = static_cast<std::vector<std::uint8_t>*>(InContext);
    const auto* ScopedSource = static_cast<const std::uint8_t*>(InData);
    ScopedBytes->insert(ScopedBytes->end(), ScopedSource, ScopedSource + InSize);
}

[[nodiscard]] Bytes MakePng(const std::vector<std::uint8_t>& InRgba, std::uint32_t InWidth, std::uint32_t InHeight)
{
    std::vector<std::uint8_t> ScopedEncoded;
    const int ScopedResult =
        stbi_write_png_to_func(&AppendToVector, &ScopedEncoded, static_cast<int>(InWidth), static_cast<int>(InHeight),
                               4, InRgba.data(), static_cast<int>(InWidth) * 4);
    FBY_CHECK(ScopedResult != 0);
    Bytes ScopedBytes(ScopedEncoded.size());
    std::memcpy(ScopedBytes.data(), ScopedEncoded.data(), ScopedEncoded.size());
    return ScopedBytes;
}

// Uncompressed TGA. `bpp` is 24 (BGR) or 32 (BGRA); the descriptor marks
[[nodiscard]] Bytes MakeTga(const std::vector<std::uint8_t>& InRgba, std::uint32_t InWidth, std::uint32_t InHeight,
                            std::uint32_t InBpp)
{
    const std::uint32_t ScopedBytesPerPixel = InBpp / 8;
    Bytes ScopedTga(18 + static_cast<std::size_t>(InWidth) * InHeight * ScopedBytesPerPixel, std::byte{0});
    auto ScopedPut = [&ScopedTga](std::size_t InIndex, std::uint8_t InValue)
    { ScopedTga[InIndex] = static_cast<std::byte>(InValue); };
    // Uncompressed true-colour
    ScopedPut(2, 2);
    ScopedPut(12, static_cast<std::uint8_t>(InWidth & 0xFF));
    ScopedPut(13, static_cast<std::uint8_t>((InWidth >> 8) & 0xFF));
    ScopedPut(14, static_cast<std::uint8_t>(InHeight & 0xFF));
    ScopedPut(15, static_cast<std::uint8_t>((InHeight >> 8) & 0xFF));
    ScopedPut(16, static_cast<std::uint8_t>(InBpp));
    // Alpha bits (32-bit) + top-left origin
    ScopedPut(17, InBpp == 32 ? 0x28 : 0x20);
    std::size_t ScopedCursor = 18;
    for (std::uint32_t ScopedY = 0; ScopedY < InHeight; ++ScopedY)
    {
        for (std::uint32_t ScopedX = 0; ScopedX < InWidth; ++ScopedX)
        {
            const std::uint8_t* ScopedTexel =
                InRgba.data() + (static_cast<std::size_t>(ScopedY) * InWidth + ScopedX) * 4;
            ScopedPut(ScopedCursor++, ScopedTexel[2]);
            ScopedPut(ScopedCursor++, ScopedTexel[1]);
            ScopedPut(ScopedCursor++, ScopedTexel[0]);
            if (ScopedBytesPerPixel == 4)
            {
                ScopedPut(ScopedCursor++, ScopedTexel[3]);
            }
        }
    }
    return ScopedTga;
}

[[nodiscard]] Bytes BytesOf(std::initializer_list<std::uint8_t> InValues)
{
    Bytes ScopedOut;
    for (const std::uint8_t ScopedValue : InValues)
    {
        ScopedOut.push_back(static_cast<std::byte>(ScopedValue));
    }
    return ScopedOut;
}

// A JPEG header: SOI + APP0 JFIF marker is enough for the magic sniff.
[[nodiscard]] Bytes MakeJpegMagic()
{
    return BytesOf({0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 'J', 'F', 'I', 'F', 0x00, 0x01, 0x01, 0x00, 0x00, 0x01});
}

// 1/2: PNG -> BC7 and TGA -> ASTC, header round-trip
void TestPngToBc7()
{
    constexpr std::uint32_t ScopedKSize = 32;
    const std::vector<std::uint8_t> ScopedRgba = MakeGradient(ScopedKSize, ScopedKSize);
    const Bytes ScopedPng = MakePng(ScopedRgba, ScopedKSize, ScopedKSize);

    // Desktop, sRGB, full mip chain
    Fby::Cook::TextureCookOptions ScopedOptions;
    Fby::Cook::CookedTexture ScopedCooked;
    std::string ScopedError;
    FBY_CHECK(Fby::Cook::CookTextureBytes(ScopedPng, "gradient.png", ScopedOptions, &ScopedCooked, &ScopedError));

    const Fby::Assets::TextureFormat ScopedExpected = Fby::Assets::TextureFormat::Bc7Srgb;
    const std::uint32_t ScopedExpectedMips = Fby::Assets::MaxMipLevels(ScopedKSize, ScopedKSize, ScopedExpected);
    FBY_CHECK(ScopedCooked.format == ScopedExpected);
    FBY_CHECK(ScopedCooked.width == ScopedKSize && ScopedCooked.height == ScopedKSize);
    FBY_CHECK(ScopedCooked.mipLevels == ScopedExpectedMips);
    // 32 -> 16 -> 8 -> 4, stops at one 4x4 block
    FBY_CHECK(ScopedExpectedMips == 4);

    // The payload must be a valid .ftex v2 blob by the runtime's own validator.
    const auto ScopedView = Fby::Assets::ParseCookedTexture(ScopedCooked.payload, &ScopedError);
    FBY_CHECK(ScopedView.has_value());
    FBY_CHECK(ScopedView->format == ScopedExpected);
    FBY_CHECK(ScopedView->width == ScopedKSize && ScopedView->height == ScopedKSize);
    FBY_CHECK(ScopedView->layers == 1);
    FBY_CHECK(ScopedView->mipLevels == ScopedExpectedMips);
    FBY_CHECK((ScopedView->flags & Fby::Assets::kTextureFlagSrgb) != 0);
    // PNG has an alpha channel
    FBY_CHECK((ScopedView->flags & Fby::Assets::kTextureFlagAlpha) != 0);
    FBY_CHECK(ScopedView->texels.size() ==
              Fby::Assets::LayerByteSize(ScopedExpected, ScopedKSize, ScopedKSize, ScopedExpectedMips));

    // Every mip is block compressed: mip 0 of a 32x32 BC7 image is 8x8 blocks.
    FBY_CHECK(Fby::Assets::MipByteSize(ScopedExpected, 32, 32) == 8u * 8u * 16u);
    FBY_CHECK(Fby::Assets::MipByteSize(ScopedExpected, 4, 4) == 16u);

    // Linear cook target asks for the UNORM variant and filters linearly.
    Fby::Cook::TextureCookOptions ScopedLinear = ScopedOptions;
    ScopedLinear.srgb = false;
    Fby::Cook::CookedTexture ScopedCookedLinear;
    FBY_CHECK(Fby::Cook::CookTextureBytes(ScopedPng, "gradient.png", ScopedLinear, &ScopedCookedLinear, &ScopedError));
    FBY_CHECK(ScopedCookedLinear.format == Fby::Assets::TextureFormat::Bc7Unorm);
    const auto ScopedLinearView = Fby::Assets::ParseCookedTexture(ScopedCookedLinear.payload, &ScopedError);
    FBY_CHECK(ScopedLinearView.has_value());
    FBY_CHECK((ScopedLinearView->flags & Fby::Assets::kTextureFlagSrgb) == 0);
}

void TestTgaToAstc()
{
    constexpr std::uint32_t ScopedKSize = 16;
    const std::vector<std::uint8_t> ScopedRgba = MakeGradient(ScopedKSize, ScopedKSize);
    const Bytes ScopedTga = MakeTga(ScopedRgba, ScopedKSize, ScopedKSize, 32);

    Fby::Cook::TextureCookOptions ScopedOptions;
    ScopedOptions.target = Fby::Cook::CookTarget::Mobile;
    ScopedOptions.astcBlock = Fby::Cook::AstcBlock::Block4x4;

    Fby::Cook::CookedTexture ScopedCooked;
    std::string ScopedError;
    FBY_CHECK(Fby::Cook::CookTextureBytes(ScopedTga, "gradient.tga", ScopedOptions, &ScopedCooked, &ScopedError));
    FBY_CHECK(ScopedCooked.format == Fby::Assets::TextureFormat::Astc4x4Srgb);

    const auto ScopedView = Fby::Assets::ParseCookedTexture(ScopedCooked.payload, &ScopedError);
    FBY_CHECK(ScopedView.has_value());
    FBY_CHECK(ScopedView->format == Fby::Assets::TextureFormat::Astc4x4Srgb);
    FBY_CHECK(ScopedView->mipLevels == Fby::Assets::MaxMipLevels(ScopedKSize, ScopedKSize, ScopedView->format));
    FBY_CHECK(ScopedView->texels.size() ==
              Fby::Assets::LayerByteSize(ScopedView->format, ScopedKSize, ScopedKSize, ScopedView->mipLevels));

    // The shipped ASTC tier emits valid LDR void-extent constant-colour blocks.
    std::uint8_t ScopedDecoded[4] = {};
    FBY_CHECK(Fby::Cook::DecodeAstcConstantBlock(std::span<const std::byte, 16>(ScopedView->texels.data(), 16),
                                                 ScopedDecoded));
    const Bytes& ScopedTexels = ScopedCooked.payload;
    constexpr std::size_t ScopedKHeaderBytes = sizeof(Fby::Assets::TextureHeader);
    FBY_CHECK(static_cast<std::uint8_t>(ScopedTexels[ScopedKHeaderBytes]) == 0xFC);
    FBY_CHECK(static_cast<std::uint8_t>(ScopedTexels[ScopedKHeaderBytes + 1]) == 0xFD);
    for (std::size_t ScopedIndex = 2; ScopedIndex < 8; ++ScopedIndex)
    {
        FBY_CHECK(static_cast<std::uint8_t>(ScopedTexels[ScopedKHeaderBytes + ScopedIndex]) == 0xFF);
    }
    std::uint32_t ScopedSums[4] = {};
    for (std::uint32_t ScopedY = 0; ScopedY < 4; ++ScopedY)
    {
        for (std::uint32_t ScopedX = 0; ScopedX < 4; ++ScopedX)
        {
            const std::uint8_t* ScopedTexel =
                ScopedRgba.data() + (static_cast<std::size_t>(ScopedY) * ScopedKSize + ScopedX) * 4;
            for (std::uint32_t ScopedChannel = 0; ScopedChannel < 4; ++ScopedChannel)
            {
                ScopedSums[ScopedChannel] += ScopedTexel[ScopedChannel];
            }
        }
    }
    for (std::uint32_t ScopedChannel = 0; ScopedChannel < 4; ++ScopedChannel)
    {
        const std::uint32_t ScopedAverage = (ScopedSums[ScopedChannel] + 8) / 16;
        FBY_CHECK(std::abs(static_cast<int>(ScopedDecoded[ScopedChannel]) - static_cast<int>(ScopedAverage)) <= 1);
    }

    // 6x6 target: still structurally valid, different block count/format.
    Fby::Cook::TextureCookOptions ScopedMobile66 = ScopedOptions;
    ScopedMobile66.astcBlock = Fby::Cook::AstcBlock::Block6x6;
    Fby::Cook::CookedTexture ScopedCooked66;
    FBY_CHECK(Fby::Cook::CookTextureBytes(ScopedTga, "gradient.tga", ScopedMobile66, &ScopedCooked66, &ScopedError));
    FBY_CHECK(ScopedCooked66.format == Fby::Assets::TextureFormat::Astc6x6Srgb);
    const auto ScopedView66 = Fby::Assets::ParseCookedTexture(ScopedCooked66.payload, &ScopedError);
    FBY_CHECK(ScopedView66.has_value());
    FBY_CHECK(ScopedView66->texels.size() ==
              Fby::Assets::LayerByteSize(ScopedView66->format, ScopedKSize, ScopedKSize, ScopedView66->mipLevels));
    FBY_CHECK(Fby::Assets::MipByteSize(ScopedView66->format, 16, 16) == 3u * 3u * 16u);
}

// 3/4: BC7 mode 6 error bounds
void TestBc7Mode6ErrorBound()
{
    // A smooth 4x4 RGBA gradient block.
    std::uint8_t ScopedSource[16 * 4] = {};
    for (std::uint32_t ScopedTexel = 0; ScopedTexel < 16; ++ScopedTexel)
    {
        // 0..240
        ScopedSource[ScopedTexel * 4 + 0] = static_cast<std::uint8_t>(16 * ScopedTexel);
        // 255..15
        ScopedSource[ScopedTexel * 4 + 1] = static_cast<std::uint8_t>(255 - 16 * ScopedTexel);
        // 64..184
        ScopedSource[ScopedTexel * 4 + 2] = static_cast<std::uint8_t>(64 + 8 * ScopedTexel);
        // 255..195
        ScopedSource[ScopedTexel * 4 + 3] = static_cast<std::uint8_t>(255 - 4 * ScopedTexel);
    }
    std::byte ScopedBlock[16] = {};
    Fby::Cook::EncodeBc7Mode6Block(ScopedSource, std::span<std::byte, 16>(ScopedBlock, 16));

    // Mode 6: the seven low bits are 0b1000000.
    FBY_CHECK((static_cast<std::uint8_t>(ScopedBlock[0]) & 0x7F) == 0x40);

    std::uint8_t ScopedDecoded[16 * 4] = {};
    FBY_CHECK(Fby::Cook::DecodeBc7Mode6Block(std::span<const std::byte, 16>(ScopedBlock, 16), ScopedDecoded));
    std::int32_t ScopedMaximumError = 0;
    std::int32_t ScopedTotalError = 0;
    for (std::uint32_t ScopedTexel = 0; ScopedTexel < 16; ++ScopedTexel)
    {
        for (std::uint32_t ScopedChannel = 0; ScopedChannel < 4; ++ScopedChannel)
        {
            const std::int32_t ScopedError =
                std::abs(static_cast<std::int32_t>(ScopedDecoded[ScopedTexel * 4 + ScopedChannel]) -
                         ScopedSource[ScopedTexel * 4 + ScopedChannel]);
            ScopedMaximumError = std::max(ScopedMaximumError, ScopedError);
            ScopedTotalError += ScopedError;
        }
    }
    FBY_CHECK(ScopedMaximumError <= 8);
    // Average error <= 2 per channel
    FBY_CHECK(ScopedTotalError <= 16 * 4 * 2);

    // A flat block must decode exactly (both endpoints coincide).
    std::uint8_t ScopedFlat[16 * 4];
    std::memset(ScopedFlat, 0, sizeof(ScopedFlat));
    for (std::uint32_t ScopedTexel = 0; ScopedTexel < 16; ++ScopedTexel)
    {
        ScopedFlat[ScopedTexel * 4 + 0] = 200;
        ScopedFlat[ScopedTexel * 4 + 1] = 100;
        ScopedFlat[ScopedTexel * 4 + 2] = 50;
        ScopedFlat[ScopedTexel * 4 + 3] = 255;
    }
    std::byte ScopedFlatBlock[16] = {};
    Fby::Cook::EncodeBc7Mode6Block(ScopedFlat, std::span<std::byte, 16>(ScopedFlatBlock, 16));
    std::uint8_t ScopedFlatDecoded[16 * 4] = {};
    FBY_CHECK(Fby::Cook::DecodeBc7Mode6Block(std::span<const std::byte, 16>(ScopedFlatBlock, 16), ScopedFlatDecoded));
    // Mode 6 stores one shared P-bit per endpoint, so an RGBA endpoint whose
    FBY_CHECK(std::abs(static_cast<int>(ScopedFlatDecoded[0]) - 200) <= 1);
    FBY_CHECK(std::abs(static_cast<int>(ScopedFlatDecoded[1]) - 100) <= 1);
    FBY_CHECK(std::abs(static_cast<int>(ScopedFlatDecoded[2]) - 50) <= 1);
    FBY_CHECK(std::abs(static_cast<int>(ScopedFlatDecoded[3]) - 255) <= 1);

    // A 1D ramp lies exactly on the mode-6 interpolation axis, so only the
    std::uint8_t ScopedRamp[16 * 4] = {};
    for (std::uint32_t ScopedTexel = 0; ScopedTexel < 16; ++ScopedTexel)
    {
        // 0..255
        ScopedRamp[ScopedTexel * 4 + 0] = static_cast<std::uint8_t>(17 * ScopedTexel);
        // 0..120
        ScopedRamp[ScopedTexel * 4 + 1] = static_cast<std::uint8_t>(8 * ScopedTexel);
        // 255..0
        ScopedRamp[ScopedTexel * 4 + 2] = static_cast<std::uint8_t>(255 - 17 * ScopedTexel);
        ScopedRamp[ScopedTexel * 4 + 3] = 255;
    }
    std::byte ScopedRampBlock[16] = {};
    Fby::Cook::EncodeBc7Mode6Block(ScopedRamp, std::span<std::byte, 16>(ScopedRampBlock, 16));
    std::uint8_t ScopedRampDecoded[16 * 4] = {};
    FBY_CHECK(Fby::Cook::DecodeBc7Mode6Block(std::span<const std::byte, 16>(ScopedRampBlock, 16), ScopedRampDecoded));
    std::int32_t ScopedRampMaximumError = 0;
    for (std::uint32_t ScopedTexel = 0; ScopedTexel < 16; ++ScopedTexel)
    {
        for (std::uint32_t ScopedChannel = 0; ScopedChannel < 4; ++ScopedChannel)
        {
            ScopedRampMaximumError =
                std::max(ScopedRampMaximumError,
                         std::abs(static_cast<std::int32_t>(ScopedRampDecoded[ScopedTexel * 4 + ScopedChannel]) -
                                  ScopedRamp[ScopedTexel * 4 + ScopedChannel]));
        }
    }
    FBY_CHECK(ScopedRampMaximumError <= 3);
}

void TestBc7TextureErrorBound()
{
    constexpr std::uint32_t ScopedKSize = 32;
    const std::vector<std::uint8_t> ScopedRgba = MakeGradient(ScopedKSize, ScopedKSize);
    const Bytes ScopedPng = MakePng(ScopedRgba, ScopedKSize, ScopedKSize);

    Fby::Cook::TextureCookOptions ScopedOptions;
    Fby::Cook::CookedTexture ScopedCooked;
    std::string ScopedError;
    FBY_CHECK(Fby::Cook::CookTextureBytes(ScopedPng, "gradient.png", ScopedOptions, &ScopedCooked, &ScopedError));
    const auto ScopedView = Fby::Assets::ParseCookedTexture(ScopedCooked.payload, &ScopedError);
    FBY_CHECK(ScopedView.has_value());

    const std::uint32_t ScopedBlocksPerRow = ScopedKSize / 4;
    std::uint64_t ScopedSquaredError = 0;
    std::uint32_t ScopedSamples = 0;
    std::int32_t ScopedMaximumError = 0;
    std::uint8_t ScopedDecoded[16 * 4] = {};
    for (std::uint32_t ScopedBlockY = 0; ScopedBlockY < ScopedBlocksPerRow; ++ScopedBlockY)
    {
        for (std::uint32_t ScopedBlockX = 0; ScopedBlockX < ScopedBlocksPerRow; ++ScopedBlockX)
        {
            const std::size_t ScopedOffset =
                (static_cast<std::size_t>(ScopedBlockY) * ScopedBlocksPerRow + ScopedBlockX) * 16;
            FBY_CHECK(Fby::Cook::DecodeBc7Mode6Block(
                std::span<const std::byte, 16>(ScopedView->texels.data() + ScopedOffset, 16), ScopedDecoded));
            for (std::uint32_t ScopedTexel = 0; ScopedTexel < 16; ++ScopedTexel)
            {
                const std::uint32_t ScopedX = ScopedBlockX * 4 + ScopedTexel % 4;
                const std::uint32_t ScopedY = ScopedBlockY * 4 + ScopedTexel / 4;
                const std::uint8_t* ScopedSource =
                    ScopedRgba.data() + (static_cast<std::size_t>(ScopedY) * ScopedKSize + ScopedX) * 4;
                for (std::uint32_t ScopedChannel = 0; ScopedChannel < 4; ++ScopedChannel)
                {
                    const std::int32_t ScopedErrorValue =
                        std::abs(static_cast<std::int32_t>(ScopedDecoded[ScopedTexel * 4 + ScopedChannel]) -
                                 ScopedSource[ScopedChannel]);
                    ScopedMaximumError = std::max(ScopedMaximumError, ScopedErrorValue);
                    ScopedSquaredError += static_cast<std::uint64_t>(ScopedErrorValue) * ScopedErrorValue;
                    ++ScopedSamples;
                }
            }
        }
    }
    const double ScopedRmse = std::sqrt(static_cast<double>(ScopedSquaredError) / std::max(ScopedSamples, 1u));
    // Quality tier of the mode-6 encoder: mode 6 is a SINGLE-subset linear
    if (ScopedMaximumError > 16 || ScopedRmse > 5.0)
    {
        std::fprintf(stderr, "  bc7 gradient: max error %d, rmse %.3f\n", ScopedMaximumError, ScopedRmse);
    }
    FBY_CHECK(ScopedMaximumError <= 16);
    FBY_CHECK(ScopedRmse <= 5.0);
}

// 5: JPEG rejection
void TestJpegRejection()
{
    const std::vector<std::uint8_t> ScopedRgba = MakeGradient(8, 8);
    const Bytes ScopedPng = MakePng(ScopedRgba, 8, 8);
    const Bytes ScopedJpeg = MakeJpegMagic();
    const Fby::Cook::TextureCookOptions ScopedOptions;
    Fby::Cook::CookedTexture ScopedCooked;
    std::string ScopedError;

    // Rejected by extension.
    FBY_CHECK(!Fby::Cook::CookTextureBytes(ScopedPng, "photo.jpg", ScopedOptions, &ScopedCooked, &ScopedError));
    FBY_CHECK(ScopedError.find("JPEG") != std::string::npos);
    ScopedError.clear();
    FBY_CHECK(!Fby::Cook::CookTextureBytes(ScopedPng, "photo.jpeg", ScopedOptions, &ScopedCooked, &ScopedError));
    FBY_CHECK(ScopedError.find("JPEG") != std::string::npos);

    // Rejected by magic even when renamed to a whitelisted extension.
    ScopedError.clear();
    FBY_CHECK(!Fby::Cook::CookTextureBytes(ScopedJpeg, "photo.png", ScopedOptions, &ScopedCooked, &ScopedError));
    FBY_CHECK(ScopedError.find("JPEG") != std::string::npos);
    ScopedError.clear();
    FBY_CHECK(!Fby::Cook::CookTextureBytes(ScopedJpeg, "photo.tga", ScopedOptions, &ScopedCooked, &ScopedError));
    FBY_CHECK(ScopedError.find("JPEG") != std::string::npos);

    // Other formats are outside the whitelist as well.
    ScopedError.clear();
    FBY_CHECK(!Fby::Cook::CookTextureBytes(ScopedPng, "photo.bmp", ScopedOptions, &ScopedCooked, &ScopedError));
    FBY_CHECK(ScopedError.find("PNG and TGA") != std::string::npos);

    // A PNG whose bytes claim .tga is refused instead of being decoded anyway.
    ScopedError.clear();
    FBY_CHECK(!Fby::Cook::CookTextureBytes(ScopedPng, "gradient.tga", ScopedOptions, &ScopedCooked, &ScopedError));
    FBY_CHECK(ScopedError.find("PNG") != std::string::npos);
}

// 6/7: odd-size mip arithmetic, sRGB-aware filtering, chain layout
void TestOddSizeMips()
{
    constexpr std::uint32_t ScopedKWidth = 13;
    constexpr std::uint32_t ScopedKHeight = 7;
    const std::vector<std::uint8_t> ScopedRgba = MakeGradient(ScopedKWidth, ScopedKHeight);
    const Bytes ScopedPng = MakePng(ScopedRgba, ScopedKWidth, ScopedKHeight);

    Fby::Cook::TextureCookOptions ScopedOptions;
    Fby::Cook::CookedTexture ScopedCooked;
    std::string ScopedError;
    FBY_CHECK(Fby::Cook::CookTextureBytes(ScopedPng, "odd.png", ScopedOptions, &ScopedCooked, &ScopedError));
    const Fby::Assets::TextureFormat ScopedFormat = Fby::Assets::TextureFormat::Bc7Srgb;
    FBY_CHECK(ScopedCooked.format == ScopedFormat);
    const std::uint32_t ScopedExpectedMips = Fby::Assets::MaxMipLevels(ScopedKWidth, ScopedKHeight, ScopedFormat);
    // 13x7 -> 6x3 -> 3x1
    FBY_CHECK(ScopedExpectedMips == 3);
    FBY_CHECK(ScopedCooked.mipLevels == ScopedExpectedMips);
    FBY_CHECK(Fby::Assets::MipExtent(ScopedKWidth, 0) == 13 && Fby::Assets::MipExtent(ScopedKHeight, 0) == 7);
    FBY_CHECK(Fby::Assets::MipExtent(ScopedKWidth, 1) == 6 && Fby::Assets::MipExtent(ScopedKHeight, 1) == 3);
    FBY_CHECK(Fby::Assets::MipExtent(ScopedKWidth, 2) == 3 && Fby::Assets::MipExtent(ScopedKHeight, 2) == 1);

    const std::uint64_t ScopedExpectedBytes =
        Fby::Assets::LayerByteSize(ScopedFormat, ScopedKWidth, ScopedKHeight, ScopedExpectedMips);
    // 128 + 32 + 16
    FBY_CHECK(ScopedExpectedBytes == (4 * 2 * 16) + (2 * 1 * 16) + (1 * 16));
    const auto ScopedView = Fby::Assets::ParseCookedTexture(ScopedCooked.payload, &ScopedError);
    FBY_CHECK(ScopedView.has_value());
    FBY_CHECK(ScopedView->texels.size() == ScopedExpectedBytes);

    // ASTC 6x6 with an odd, smaller-than-block dimension.
    Fby::Cook::TextureCookOptions ScopedMobile;
    ScopedMobile.target = Fby::Cook::CookTarget::Mobile;
    ScopedMobile.astcBlock = Fby::Cook::AstcBlock::Block6x6;
    Fby::Cook::CookedTexture ScopedCookedAstc;
    FBY_CHECK(Fby::Cook::CookTextureBytes(ScopedPng, "odd.png", ScopedMobile, &ScopedCookedAstc, &ScopedError));
    FBY_CHECK(ScopedCookedAstc.format == Fby::Assets::TextureFormat::Astc6x6Srgb);
    FBY_CHECK(ScopedCookedAstc.mipLevels ==
              Fby::Assets::MaxMipLevels(ScopedKWidth, ScopedKHeight, ScopedCookedAstc.format));
    // 13x7 -> 6x3 (one 6x6 block per mip)
    FBY_CHECK(ScopedCookedAstc.mipLevels == 2);
    const std::uint64_t ScopedAstcBytes =
        Fby::Assets::LayerByteSize(ScopedCookedAstc.format, ScopedKWidth, ScopedKHeight, ScopedCookedAstc.mipLevels);
    // 96 + 16
    FBY_CHECK(ScopedAstcBytes == (3u * 2u * 16u) + (1u * 1u * 16u));
    const auto ScopedAstcView = Fby::Assets::ParseCookedTexture(ScopedCookedAstc.payload, &ScopedError);
    FBY_CHECK(ScopedAstcView.has_value());
    FBY_CHECK(ScopedAstcView->texels.size() == ScopedAstcBytes);
}

void TestSrgbAwareMips()
{
    // A 2x2 black/white checker. Averaging in linear light yields ~188 in sRGB
    std::uint8_t ScopedChecker[2 * 2 * 4] = {0, 0, 0, 255, 255, 255, 255, 255, 0, 0, 0, 255, 255, 255, 255, 255};

    std::vector<std::uint8_t> ScopedSrgbChain;
    FBY_CHECK(Fby::Cook::BuildMipChain(ScopedChecker, 2, 2, 2, true, ScopedSrgbChain));
    FBY_CHECK(ScopedSrgbChain.size() == (2 * 2 * 4) + (1 * 1 * 4));
    FBY_CHECK(std::abs(static_cast<int>(ScopedSrgbChain[16]) - 188) <= 3);
    FBY_CHECK(ScopedSrgbChain[19] == 255);

    std::vector<std::uint8_t> ScopedLinearChain;
    FBY_CHECK(Fby::Cook::BuildMipChain(ScopedChecker, 2, 2, 2, false, ScopedLinearChain));
    FBY_CHECK(std::abs(static_cast<int>(ScopedLinearChain[16]) - 128) <= 3);

    // The top mip is the untouched source.
    FBY_CHECK(std::memcmp(ScopedLinearChain.data(), ScopedChecker, 16) == 0);

    // Full chain layout: every level's byte count matches MipExtent arithmetic.
    constexpr std::uint32_t ScopedKWidth = 13;
    constexpr std::uint32_t ScopedKHeight = 7;
    const std::vector<std::uint8_t> ScopedRgba = MakeGradient(ScopedKWidth, ScopedKHeight);
    // Caller-controlled; BuildMipChain honours it
    const std::uint32_t ScopedLevels = 4;
    std::vector<std::uint8_t> ScopedChain;
    FBY_CHECK(
        Fby::Cook::BuildMipChain(ScopedRgba.data(), ScopedKWidth, ScopedKHeight, ScopedLevels, true, ScopedChain));
    std::size_t ScopedExpectedSize = 0;
    std::uint32_t ScopedWidth = ScopedKWidth;
    std::uint32_t ScopedHeight = ScopedKHeight;
    for (std::uint32_t ScopedLevel = 0; ScopedLevel < ScopedLevels; ++ScopedLevel)
    {
        ScopedExpectedSize += static_cast<std::size_t>(ScopedWidth) * ScopedHeight * 4;
        ScopedWidth = ScopedWidth > 1 ? ScopedWidth / 2 : 1;
        ScopedHeight = ScopedHeight > 1 ? ScopedHeight / 2 : 1;
    }
    FBY_CHECK(ScopedChain.size() == ScopedExpectedSize);
}

// 8: 24-bit TGA has no alpha flag
void TestTgaWithoutAlpha()
{
    constexpr std::uint32_t ScopedKSize = 8;
    const std::vector<std::uint8_t> ScopedRgba = MakeGradient(ScopedKSize, ScopedKSize);
    const Bytes ScopedTga = MakeTga(ScopedRgba, ScopedKSize, ScopedKSize, 24);

    Fby::Cook::TextureCookOptions ScopedOptions;
    Fby::Cook::CookedTexture ScopedCooked;
    std::string ScopedError;
    FBY_CHECK(Fby::Cook::CookTextureBytes(ScopedTga, "opaque.tga", ScopedOptions, &ScopedCooked, &ScopedError));
    const auto ScopedView = Fby::Assets::ParseCookedTexture(ScopedCooked.payload, &ScopedError);
    FBY_CHECK(ScopedView.has_value());
    FBY_CHECK((ScopedView->flags & Fby::Assets::kTextureFlagAlpha) == 0);
    FBY_CHECK((ScopedView->flags & Fby::Assets::kTextureFlagSrgb) != 0);
}
}

int main()
{
    TestPngToBc7();
    TestTgaToAstc();
    TestBc7Mode6ErrorBound();
    TestBc7TextureErrorBound();
    TestJpegRejection();
    TestOddSizeMips();
    TestSrgbAwareMips();
    TestTgaWithoutAlpha();

    if (GFailures != 0)
    {
        std::fprintf(stderr, "texture_cook_test: %d failure(s)\n", GFailures);
        return 1;
    }
    std::printf("texture_cook_test: OK\n");
    return 0;
}
