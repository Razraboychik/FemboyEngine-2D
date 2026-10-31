// FemboyEngine2D - fby_fontc, the Pink UI font cooker.
#include "Engine/UI/FontAtlas.hpp"

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace
{
struct Options
{
    std::string input;
    std::string output;
    std::vector<std::uint32_t> codepoints;
    float sizePixels = 32.0f;
    std::uint32_t fontId = 0;
    Fby::Ui::FontStyle style = Fby::Ui::FontStyle::Regular;
    std::uint32_t oversample = 1;
    bool verify = false;
    bool sdf = false;
};

void PrintUsage()
{
    std::printf("fby_fontc - compile a TTF into a FemboyEngine .fbyfont atlas\n"
                "  --input <font.ttf>     source font (required)\n"
                "  --output <font.fbyfont> destination (required)\n"
                "  --size <pixels>        rasterization height (default 32)\n"
                "  --font-id <id>         atlas font id (default 0)\n"
                "  --style <slot>         regular,bold,italic,bolditalic,mono (default regular)\n"
                "  --ranges <list>        ascii,latin,cyrillic,punctuation (default latin,cyrillic)\n"
                "  --oversample <1..4>    bitmap supersampling for coverage AA (default 1)\n"
                "  --sdf                  write an SdfR8 atlas for the SDF text shader\n"
                "  --verify               re-open the result and validate its header\n"
                "  --help                 print this text\n");
}

[[nodiscard]] bool ReadFile(const std::string& InPath, std::vector<unsigned char>& OutBytes)
{
    std::ifstream ScopedStream(InPath, std::ios::binary);
    if (!ScopedStream)
    {
        std::fprintf(stderr, "fby_fontc: cannot open '%s'\n", InPath.c_str());
        return false;
    }
    ScopedStream.seekg(0, std::ios::end);
    const std::streamoff ScopedSize = ScopedStream.tellg();
    if (ScopedSize <= 0)
    {
        std::fprintf(stderr, "fby_fontc: '%s' is empty\n", InPath.c_str());
        return false;
    }
    ScopedStream.seekg(0, std::ios::beg);
    OutBytes.resize(static_cast<std::size_t>(ScopedSize));
    ScopedStream.read(reinterpret_cast<char*>(OutBytes.data()), ScopedSize);
    if (!ScopedStream)
    {
        std::fprintf(stderr, "fby_fontc: short read on '%s'\n", InPath.c_str());
        return false;
    }
    return true;
}

void AppendRange(std::vector<std::uint32_t>& OutCodepoints, std::uint32_t InFirst, std::uint32_t InLast)
{
    for (std::uint32_t ScopedCode = InFirst; ScopedCode <= InLast; ++ScopedCode)
    {
        OutCodepoints.push_back(ScopedCode);
    }
}

void AppendRangeNamed(std::vector<std::uint32_t>& OutCodepoints, std::string_view InName)
{
    if (InName == "ascii")
    {
        AppendRange(OutCodepoints, 0x0020u, 0x007Eu);
    }
    else if (InName == "latin")
    {
        AppendRange(OutCodepoints, 0x00A0u, 0x00FFu);
        AppendRange(OutCodepoints, 0x2010u, 0x2015u);
        AppendRange(OutCodepoints, 0x2018u, 0x201Eu);
        AppendRange(OutCodepoints, 0x2026u, 0x2026u);
    }
    else if (InName == "cyrillic")
    {
        AppendRange(OutCodepoints, 0x0400u, 0x045Fu);
    }
    else if (InName == "punctuation")
    {
        AppendRange(OutCodepoints, 0x2010u, 0x203Au);
        OutCodepoints.push_back(0x2116u);
    }
    else
    {
        std::fprintf(stderr, "fby_fontc: unknown range '%s'\n", std::string(InName).c_str());
    }
}

[[nodiscard]] bool ParseRanges(std::string_view InSpec, std::vector<std::uint32_t>& OutCodepoints)
{
    std::size_t ScopedStart = 0;
    while (ScopedStart <= InSpec.size())
    {
        const std::size_t ScopedComma = InSpec.find(',', ScopedStart);
        const std::size_t ScopedEnd = ScopedComma == std::string_view::npos ? InSpec.size() : ScopedComma;
        std::string_view ScopedName = InSpec.substr(ScopedStart, ScopedEnd - ScopedStart);
        while (!ScopedName.empty() && ScopedName.front() == ' ')
        {
            ScopedName.remove_prefix(1);
        }
        while (!ScopedName.empty() && ScopedName.back() == ' ')
        {
            ScopedName.remove_suffix(1);
        }
        if (!ScopedName.empty())
        {
            AppendRangeNamed(OutCodepoints, ScopedName);
        }
        if (ScopedComma == std::string_view::npos)
        {
            break;
        }
        ScopedStart = ScopedComma + 1;
    }
    std::sort(OutCodepoints.begin(), OutCodepoints.end());
    OutCodepoints.erase(std::unique(OutCodepoints.begin(), OutCodepoints.end()), OutCodepoints.end());
    return !OutCodepoints.empty();
}

[[nodiscard]] bool ParseArguments(int InCount, char** InArguments, Options& OutOptions)
{
    if (InCount <= 1)
    {
        PrintUsage();
        return false;
    }
    for (int ScopedIndex = 1; ScopedIndex < InCount; ++ScopedIndex)
    {
        const std::string_view ScopedArgument(InArguments[ScopedIndex]);
        const auto ScopedNext = [&](std::string* OutValue) -> bool
        {
            if (ScopedIndex + 1 >= InCount)
            {
                std::fprintf(stderr, "fby_fontc: %s needs a value\n", std::string(ScopedArgument).c_str());
                return false;
            }
            *OutValue = InArguments[++ScopedIndex];
            return true;
        };
        if (ScopedArgument == "--help" || ScopedArgument == "-h")
        {
            PrintUsage();
            return false;
        }
        if (ScopedArgument == "--input")
        {
            if (!ScopedNext(&OutOptions.input))
            {
                return false;
            }
        }
        else if (ScopedArgument == "--output")
        {
            if (!ScopedNext(&OutOptions.output))
            {
                return false;
            }
        }
        else if (ScopedArgument == "--size")
        {
            std::string ScopedValue;
            if (!ScopedNext(&ScopedValue))
            {
                return false;
            }
            OutOptions.sizePixels = std::clamp(std::stof(ScopedValue), 6.0f, 256.0f);
        }
        else if (ScopedArgument == "--font-id")
        {
            std::string ScopedValue;
            if (!ScopedNext(&ScopedValue))
            {
                return false;
            }
            OutOptions.fontId = static_cast<std::uint32_t>(std::stoul(ScopedValue));
        }
        else if (ScopedArgument == "--style")
        {
            std::string ScopedValue;
            if (!ScopedNext(&ScopedValue))
            {
                return false;
            }
            if (ScopedValue == "regular")
            {
                OutOptions.style = Fby::Ui::FontStyle::Regular;
            }
            else if (ScopedValue == "bold")
            {
                OutOptions.style = Fby::Ui::FontStyle::Bold;
            }
            else if (ScopedValue == "italic")
            {
                OutOptions.style = Fby::Ui::FontStyle::Italic;
            }
            else if (ScopedValue == "bolditalic")
            {
                OutOptions.style = Fby::Ui::FontStyle::BoldItalic;
            }
            else if (ScopedValue == "mono")
            {
                OutOptions.style = Fby::Ui::FontStyle::Mono;
            }
            else
            {
                std::fprintf(stderr, "fby_fontc: unknown style '%s'\n", ScopedValue.c_str());
                return false;
            }
        }
        else if (ScopedArgument == "--ranges")
        {
            std::string ScopedValue;
            if (!ScopedNext(&ScopedValue))
            {
                return false;
            }
            if (!ParseRanges(ScopedValue, OutOptions.codepoints))
            {
                return false;
            }
        }
        else if (ScopedArgument == "--verify")
        {
            OutOptions.verify = true;
        }
        else if (ScopedArgument == "--oversample")
        {
            std::string ScopedValue;
            if (!ScopedNext(&ScopedValue))
            {
                return false;
            }
            const int ScopedOversample = std::atoi(ScopedValue.c_str());
            if (ScopedOversample < 1 || ScopedOversample > 4)
            {
                std::fprintf(stderr, "fby_fontc: --oversample must be 1..4\n");
                return false;
            }
            OutOptions.oversample = static_cast<std::uint32_t>(ScopedOversample);
        }
        else if (ScopedArgument == "--sdf")
        {
            OutOptions.sdf = true;
        }
        else
        {
            std::fprintf(stderr, "fby_fontc: unknown argument '%s'\n", std::string(ScopedArgument).c_str());
            return false;
        }
    }
    if (OutOptions.codepoints.empty())
    {
        if (!ParseRanges("latin,cyrillic", OutOptions.codepoints))
        {
            return false;
        }
    }
    if (OutOptions.input.empty() || OutOptions.output.empty())
    {
        std::fprintf(stderr, "fby_fontc: --input and --output are required\n");
        return false;
    }
    return true;
}

[[nodiscard]] bool WriteFileAtomically(const std::string& InPath, const std::vector<std::byte>& InBytes)
{
    const std::string ScopedTemporary = InPath + ".fbtmp";
    {
        std::ofstream ScopedStream(ScopedTemporary, std::ios::binary | std::ios::trunc);
        if (!ScopedStream)
        {
            std::fprintf(stderr, "fby_fontc: cannot open '%s' for writing\n", ScopedTemporary.c_str());
            return false;
        }
        ScopedStream.write(reinterpret_cast<const char*>(InBytes.data()),
                           static_cast<std::streamsize>(InBytes.size()));
        if (!ScopedStream)
        {
            std::fprintf(stderr, "fby_fontc: write failed for '%s'\n", ScopedTemporary.c_str());
            std::remove(ScopedTemporary.c_str());
            return false;
        }
    }
#if defined(_WIN32)
    std::remove(InPath.c_str());
#endif
    if (std::rename(ScopedTemporary.c_str(), InPath.c_str()) != 0)
    {
        std::fprintf(stderr, "fby_fontc: cannot replace '%s'\n", InPath.c_str());
        std::remove(ScopedTemporary.c_str());
        return false;
    }
    return true;
}

[[nodiscard]] std::vector<std::byte> BuildSdfAtlas(const stbtt_fontinfo& InFont,
                                                  const std::vector<std::uint32_t>& InCodepoints,
                                                  const Options& InOptions)
{
    using namespace Fby::Ui;
    constexpr std::uint32_t kAtlasWidth = 2048;
    constexpr std::uint32_t kAtlasHeight = 2048;
    std::vector<unsigned char> ScopedPixels(static_cast<std::size_t>(kAtlasWidth) * kAtlasHeight, 0);
    std::vector<GlyphRecord> ScopedGlyphs;
    ScopedGlyphs.reserve(InCodepoints.size());

    const float ScopedScale = stbtt_ScaleForPixelHeight(&InFont, InOptions.sizePixels);
    std::uint32_t ScopedCursorX = 0;
    std::uint32_t ScopedCursorY = 0;
    std::uint32_t ScopedRowHeight = 0;
    for (const std::uint32_t ScopedCodepoint : InCodepoints)
    {
        const int ScopedGlyphIndex = stbtt_FindGlyphIndex(&InFont, static_cast<int>(ScopedCodepoint));
        int ScopedAdvance = 0;
        int ScopedBearing = 0;
        stbtt_GetGlyphHMetrics(&InFont, ScopedGlyphIndex, &ScopedAdvance, &ScopedBearing);
        int ScopedWidth = 0;
        int ScopedHeight = 0;
        int ScopedXOffset = 0;
        int ScopedYOffset = 0;
        unsigned char* ScopedBitmap = stbtt_GetGlyphSDF(&InFont, ScopedScale, ScopedGlyphIndex, 6, 128, 8.0f,
                                                        &ScopedWidth, &ScopedHeight, &ScopedXOffset, &ScopedYOffset);
        GlyphRecord ScopedRecord;
        ScopedRecord.codepoint = ScopedCodepoint;
        ScopedRecord.glyphId = static_cast<std::uint32_t>(ScopedGlyphIndex);
        ScopedRecord.advance = static_cast<float>(ScopedAdvance) * ScopedScale;
        ScopedRecord.bearingX = static_cast<float>(ScopedXOffset);
        ScopedRecord.bearingY = static_cast<float>(-ScopedYOffset);
        ScopedRecord.width = static_cast<float>(ScopedWidth);
        ScopedRecord.height = static_cast<float>(ScopedHeight);
        if (ScopedWidth > 0 && ScopedHeight > 0)
        {
            if (ScopedCursorX + static_cast<std::uint32_t>(ScopedWidth) > kAtlasWidth)
            {
                ScopedCursorX = 0;
                ScopedCursorY += ScopedRowHeight + 1;
                ScopedRowHeight = 0;
            }
            if (ScopedCursorY + static_cast<std::uint32_t>(ScopedHeight) > kAtlasHeight)
            {
                stbtt_FreeSDF(ScopedBitmap, nullptr);
                return {};
            }
            for (int ScopedRow = 0; ScopedRow < ScopedHeight; ++ScopedRow)
            {
                unsigned char* ScopedTarget =
                    ScopedPixels.data() + static_cast<std::size_t>(ScopedCursorY + ScopedRow) * kAtlasWidth +
                    ScopedCursorX;
                std::memcpy(ScopedTarget, ScopedBitmap + static_cast<std::size_t>(ScopedRow) * ScopedWidth,
                            static_cast<std::size_t>(ScopedWidth));
            }
            ScopedRecord.u0 = static_cast<float>(ScopedCursorX) / static_cast<float>(kAtlasWidth);
            ScopedRecord.v0 = static_cast<float>(ScopedCursorY) / static_cast<float>(kAtlasHeight);
            ScopedRecord.u1 = static_cast<float>(ScopedCursorX + ScopedWidth) / static_cast<float>(kAtlasWidth);
            ScopedRecord.v1 = static_cast<float>(ScopedCursorY + ScopedHeight) / static_cast<float>(kAtlasHeight);
            ScopedCursorX += static_cast<std::uint32_t>(ScopedWidth) + 1;
            ScopedRowHeight = std::max(ScopedRowHeight, static_cast<std::uint32_t>(ScopedHeight));
        }
        stbtt_FreeSDF(ScopedBitmap, nullptr);
        ScopedGlyphs.push_back(ScopedRecord);
    }

    int ScopedAscent = 0;
    int ScopedDescent = 0;
    int ScopedLineGap = 0;
    stbtt_GetFontVMetrics(&InFont, &ScopedAscent, &ScopedDescent, &ScopedLineGap);
    FontAtlasHeader ScopedHeader{};
    std::memcpy(ScopedHeader.magic, kFontAtlasMagic, sizeof(ScopedHeader.magic));
    ScopedHeader.version = kFontAtlasVersion;
    ScopedHeader.headerBytes = sizeof(FontAtlasHeader);
    ScopedHeader.glyphCount = static_cast<std::uint32_t>(ScopedGlyphs.size());
    ScopedHeader.atlasWidth = kAtlasWidth;
    ScopedHeader.atlasHeight = kAtlasHeight;
    ScopedHeader.pixelFormat = static_cast<std::uint32_t>(FontPixelFormat::SdfR8);
    ScopedHeader.glyphTableOffset = sizeof(FontAtlasHeader);
    const std::uint32_t ScopedGlyphBytes = static_cast<std::uint32_t>(ScopedGlyphs.size() * sizeof(GlyphRecord));
    ScopedHeader.pixelOffset = (ScopedHeader.glyphTableOffset + ScopedGlyphBytes + 3u) & ~3u;
    ScopedHeader.lineHeight = static_cast<float>(ScopedAscent - ScopedDescent + ScopedLineGap) * ScopedScale;
    ScopedHeader.ascent = static_cast<float>(ScopedAscent) * ScopedScale;
    ScopedHeader.descent = static_cast<float>(-ScopedDescent) * ScopedScale;
    ScopedHeader.fontId = InOptions.fontId;
    ScopedHeader.reserved = static_cast<std::uint32_t>(InOptions.style);

    std::vector<std::byte> ScopedOutput(ScopedHeader.pixelOffset + ScopedPixels.size(), std::byte{0});
    std::memcpy(ScopedOutput.data(), &ScopedHeader, sizeof(ScopedHeader));
    std::memcpy(ScopedOutput.data() + ScopedHeader.glyphTableOffset, ScopedGlyphs.data(), ScopedGlyphBytes);
    std::memcpy(ScopedOutput.data() + ScopedHeader.pixelOffset, ScopedPixels.data(), ScopedPixels.size());
    return ScopedOutput;
}

[[nodiscard]] bool VerifyAtlas(const std::vector<std::byte>& InBytes)
{
    using namespace Fby::Ui;
    if (InBytes.size() < sizeof(FontAtlasHeader))
    {
        std::fprintf(stderr, "fby_fontc: verify failed: file is shorter than the header\n");
        return false;
    }
    FontAtlasHeader ScopedHeader{};
    std::memcpy(&ScopedHeader, InBytes.data(), sizeof(ScopedHeader));
    if (std::memcmp(ScopedHeader.magic, kFontAtlasMagic, sizeof(ScopedHeader.magic)) != 0)
    {
        std::fprintf(stderr, "fby_fontc: verify failed: bad magic\n");
        return false;
    }
    if (ScopedHeader.version != kFontAtlasVersion || ScopedHeader.headerBytes != sizeof(FontAtlasHeader))
    {
        std::fprintf(stderr, "fby_fontc: verify failed: unsupported header\n");
        return false;
    }
    const bool ScopedRgba = ScopedHeader.pixelFormat == static_cast<std::uint32_t>(FontPixelFormat::Rgba8);
    const bool ScopedR8 = ScopedHeader.pixelFormat == static_cast<std::uint32_t>(FontPixelFormat::R8);
    const bool ScopedSdf = ScopedHeader.pixelFormat == static_cast<std::uint32_t>(FontPixelFormat::SdfR8);
    if (ScopedHeader.glyphCount == 0 || ScopedHeader.atlasWidth == 0 || ScopedHeader.atlasHeight == 0 ||
        (!ScopedRgba && !ScopedR8 && !ScopedSdf))
    {
        std::fprintf(stderr, "fby_fontc: verify failed: empty atlas\n");
        return false;
    }
    const std::uint64_t ScopedGlyphBytes =
        static_cast<std::uint64_t>(ScopedHeader.glyphCount) * sizeof(GlyphRecord);
    const std::uint64_t ScopedPixelBytes = static_cast<std::uint64_t>(ScopedHeader.atlasWidth) *
                                           ScopedHeader.atlasHeight * (ScopedRgba ? 4u : 1u);
    if (ScopedHeader.glyphTableOffset + ScopedGlyphBytes > InBytes.size() ||
        ScopedHeader.pixelOffset + ScopedPixelBytes > InBytes.size())
    {
        std::fprintf(stderr, "fby_fontc: verify failed: truncated glyph table or pixels\n");
        return false;
    }
    const auto* ScopedGlyphs =
        reinterpret_cast<const GlyphRecord*>(InBytes.data() + ScopedHeader.glyphTableOffset);
    for (std::uint32_t ScopedIndex = 1; ScopedIndex < ScopedHeader.glyphCount; ++ScopedIndex)
    {
        if (ScopedGlyphs[ScopedIndex].codepoint <= ScopedGlyphs[ScopedIndex - 1].codepoint)
        {
            std::fprintf(stderr, "fby_fontc: verify failed: glyph table is not sorted at %u\n", ScopedIndex);
            return false;
        }
    }
    std::uint32_t ScopedOpaque = 0;
    const std::size_t ScopedPixelStep = ScopedRgba ? 4u : 1u;
    for (std::size_t ScopedIndex = ScopedHeader.pixelOffset;
         ScopedIndex + (ScopedRgba ? 3u : 0u) < InBytes.size(); ScopedIndex += ScopedPixelStep)
    {
        const std::uint8_t ScopedValue =
            static_cast<std::uint8_t>(InBytes[ScopedIndex + (ScopedRgba ? 3u : 0u)]);
        if (ScopedValue != 0u)
        {
            ++ScopedOpaque;
        }
    }
    if (ScopedOpaque == 0)
    {
        std::fprintf(stderr, "fby_fontc: verify failed: atlas has no visible pixels\n");
        return false;
    }
    std::printf("fby_fontc: verified %u glyphs, %ux%u %s, line height %.2f, %zu bytes\n", ScopedHeader.glyphCount,
                ScopedHeader.atlasWidth, ScopedHeader.atlasHeight,
                ScopedRgba ? "RGBA8" : (ScopedSdf ? "SdfR8" : "R8"),
                static_cast<double>(ScopedHeader.lineHeight), InBytes.size());
    return true;
}
}

int main(int InArgumentCount, char** InArguments)
{
    using namespace Fby::Ui;

    Options ScopedOptions;
    if (!ParseArguments(InArgumentCount, InArguments, ScopedOptions))
    {
        return 1;
    }

    std::vector<unsigned char> ScopedFontBytes;
    if (!ReadFile(ScopedOptions.input, ScopedFontBytes))
    {
        return 1;
    }
    const int ScopedFontOffset = stbtt_GetFontOffsetForIndex(ScopedFontBytes.data(), 0);
    if (ScopedFontOffset < 0)
    {
        std::fprintf(stderr, "fby_fontc: '%s' contains no font\n", ScopedOptions.input.c_str());
        return 1;
    }
    stbtt_fontinfo ScopedFont{};
    if (stbtt_InitFont(&ScopedFont, ScopedFontBytes.data(), ScopedFontOffset) == 0)
    {
        std::fprintf(stderr, "fby_fontc: stb_truetype rejected '%s'\n", ScopedOptions.input.c_str());
        return 1;
    }

    // Drop codepoints the face does not cover. The atlas has no fallback
    std::vector<std::uint32_t> ScopedCodepoints;
    ScopedCodepoints.reserve(ScopedOptions.codepoints.size());
    for (const std::uint32_t ScopedCodepoint : ScopedOptions.codepoints)
    {
        if (stbtt_FindGlyphIndex(&ScopedFont, static_cast<int>(ScopedCodepoint)) != 0)
        {
            ScopedCodepoints.push_back(ScopedCodepoint);
        }
    }
    if (ScopedCodepoints.empty())
    {
        std::fprintf(stderr, "fby_fontc: none of the requested ranges exist in the face\n");
        return 1;
    }

    if (ScopedOptions.sdf)
    {
        const std::vector<std::byte> ScopedOutput = BuildSdfAtlas(ScopedFont, ScopedCodepoints, ScopedOptions);
        if (ScopedOutput.empty())
        {
            std::fprintf(stderr, "fby_fontc: SDF glyphs do not fit in the atlas\n");
            return 1;
        }
        if (!WriteFileAtomically(ScopedOptions.output, ScopedOutput))
        {
            return 1;
        }
        if (ScopedOptions.verify && !VerifyAtlas(ScopedOutput))
        {
            return 1;
        }
        std::printf("fby_fontc: wrote %zu glyphs as SdfR8 -> '%s'\n", ScopedCodepoints.size(),
                    ScopedOptions.output.c_str());
        return 0;
    }

    int ScopedAscent = 0;
    int ScopedDescent = 0;
    int ScopedLineGap = 0;
    stbtt_GetFontVMetrics(&ScopedFont, &ScopedAscent, &ScopedDescent, &ScopedLineGap);
    const float ScopedScale = stbtt_ScaleForPixelHeight(&ScopedFont, ScopedOptions.sizePixels);

    std::vector<unsigned char> ScopedBitmap;
    std::vector<stbtt_packedchar> ScopedPacked(ScopedCodepoints.size());
    std::vector<stbtt_pack_range> ScopedRanges(ScopedCodepoints.size());
    std::uint32_t ScopedWidth = 512;
    std::uint32_t ScopedHeight = 512;
    bool ScopedRendered = false;
    for (std::uint32_t ScopedAttempt = 0; ScopedAttempt < 3 && !ScopedRendered; ++ScopedAttempt)
    {
        ScopedBitmap.assign(static_cast<std::size_t>(ScopedWidth) * ScopedHeight, 0u);
        stbtt_pack_context ScopedPack{};
        if (stbtt_PackBegin(&ScopedPack, ScopedBitmap.data(), static_cast<int>(ScopedWidth),
                            static_cast<int>(ScopedHeight), 0, 1, nullptr) == 0)
        {
            std::fprintf(stderr, "fby_fontc: cannot start the packer\n");
            return 1;
        }
        stbtt_PackSetOversampling(&ScopedPack, ScopedOptions.oversample, ScopedOptions.oversample);
        for (std::size_t ScopedIndex = 0; ScopedIndex < ScopedCodepoints.size(); ++ScopedIndex)
        {
            ScopedRanges[ScopedIndex].font_size = ScopedOptions.sizePixels;
            ScopedRanges[ScopedIndex].first_unicode_codepoint_in_range = static_cast<int>(ScopedCodepoints[ScopedIndex]);
            ScopedRanges[ScopedIndex].num_chars = 1;
            ScopedRanges[ScopedIndex].chardata_for_range = &ScopedPacked[ScopedIndex];
        }
        ScopedRendered = stbtt_PackFontRanges(&ScopedPack, ScopedFontBytes.data(), 0, ScopedRanges.data(),
                                              static_cast<int>(ScopedRanges.size())) != 0;
        stbtt_PackEnd(&ScopedPack);
        if (!ScopedRendered)
        {
            ScopedWidth *= 2;
            ScopedHeight *= 2;
        }
    }
    if (!ScopedRendered)
    {
        std::fprintf(stderr, "fby_fontc: %zu glyphs do not fit in a %ux%u atlas\n", ScopedCodepoints.size(),
                     ScopedWidth, ScopedHeight);
        return 1;
    }

    std::vector<GlyphRecord> ScopedGlyphs(ScopedCodepoints.size());
    for (std::size_t ScopedIndex = 0; ScopedIndex < ScopedCodepoints.size(); ++ScopedIndex)
    {
        const stbtt_packedchar& ScopedGlyph = ScopedPacked[ScopedIndex];
        GlyphRecord& ScopedRecord = ScopedGlyphs[ScopedIndex];
        ScopedRecord.codepoint = ScopedCodepoints[ScopedIndex];
        ScopedRecord.u0 = static_cast<float>(ScopedGlyph.x0) / static_cast<float>(ScopedWidth);
        ScopedRecord.v0 = static_cast<float>(ScopedGlyph.y0) / static_cast<float>(ScopedHeight);
        ScopedRecord.u1 = static_cast<float>(ScopedGlyph.x1) / static_cast<float>(ScopedWidth);
        ScopedRecord.v1 = static_cast<float>(ScopedGlyph.y1) / static_cast<float>(ScopedHeight);
        ScopedRecord.advance = ScopedGlyph.xadvance;
        ScopedRecord.bearingX = ScopedGlyph.xoff;
        // stb_truetype measures yoff downward from the baseline; Pink UI wants
        ScopedRecord.bearingY = -ScopedGlyph.yoff;
        ScopedRecord.width = static_cast<float>(ScopedGlyph.x1 - ScopedGlyph.x0);
        ScopedRecord.height = static_cast<float>(ScopedGlyph.y1 - ScopedGlyph.y0);
        ScopedRecord.glyphId = static_cast<std::uint32_t>(
            stbtt_FindGlyphIndex(&ScopedFont, static_cast<int>(ScopedCodepoints[ScopedIndex])));
    }

    FontAtlasHeader ScopedHeader{};
    std::memcpy(ScopedHeader.magic, kFontAtlasMagic, sizeof(ScopedHeader.magic));
    ScopedHeader.version = kFontAtlasVersion;
    ScopedHeader.headerBytes = sizeof(FontAtlasHeader);
    ScopedHeader.glyphCount = static_cast<std::uint32_t>(ScopedGlyphs.size());
    ScopedHeader.atlasWidth = ScopedWidth;
    ScopedHeader.atlasHeight = ScopedHeight;
    ScopedHeader.pixelFormat = static_cast<std::uint32_t>(FontPixelFormat::Rgba8);
    ScopedHeader.glyphTableOffset = sizeof(FontAtlasHeader);
    const std::uint32_t ScopedGlyphBytes =
        static_cast<std::uint32_t>(ScopedGlyphs.size() * sizeof(GlyphRecord));
    ScopedHeader.pixelOffset = (ScopedHeader.glyphTableOffset + ScopedGlyphBytes + 3u) & ~3u;
    ScopedHeader.lineHeight = static_cast<float>(ScopedAscent - ScopedDescent + ScopedLineGap) * ScopedScale;
    ScopedHeader.ascent = static_cast<float>(ScopedAscent) * ScopedScale;
    ScopedHeader.descent = static_cast<float>(-ScopedDescent) * ScopedScale;
    ScopedHeader.fontId = ScopedOptions.fontId;
    ScopedHeader.reserved = static_cast<std::uint32_t>(ScopedOptions.style);

    const std::size_t ScopedPixelBytes = static_cast<std::size_t>(ScopedWidth) * ScopedHeight * 4u;
    std::vector<std::byte> ScopedOutput(ScopedHeader.pixelOffset + ScopedPixelBytes, std::byte{0});
    std::memcpy(ScopedOutput.data(), &ScopedHeader, sizeof(ScopedHeader));
    std::memcpy(ScopedOutput.data() + ScopedHeader.glyphTableOffset, ScopedGlyphs.data(), ScopedGlyphBytes);
    // White RGB with the stb coverage mask in A: WidgetCanvasSystem tints the
    unsigned char* ScopedPixels = reinterpret_cast<unsigned char*>(ScopedOutput.data() + ScopedHeader.pixelOffset);
    for (std::size_t ScopedPixel = 0; ScopedPixel < static_cast<std::size_t>(ScopedWidth) * ScopedHeight;
         ++ScopedPixel)
    {
        ScopedPixels[ScopedPixel * 4 + 0] = 0xFFu;
        ScopedPixels[ScopedPixel * 4 + 1] = 0xFFu;
        ScopedPixels[ScopedPixel * 4 + 2] = 0xFFu;
        ScopedPixels[ScopedPixel * 4 + 3] = ScopedBitmap[ScopedPixel];
    }

    if (!WriteFileAtomically(ScopedOptions.output, ScopedOutput))
    {
        return 1;
    }
    std::printf("fby_fontc: %zu glyphs, %ux%u RGBA8, %.0f px from '%s' -> '%s' (%zu bytes)\n",
                ScopedGlyphs.size(), ScopedWidth, ScopedHeight, static_cast<double>(ScopedOptions.sizePixels),
                ScopedOptions.input.c_str(), ScopedOptions.output.c_str(), ScopedOutput.size());
    if (ScopedOptions.verify && !VerifyAtlas(ScopedOutput))
    {
        return 2;
    }
    return 0;
}
