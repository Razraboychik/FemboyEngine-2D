// FemboyEngine2D - in-engine TTF/OTF -> .fbyfont builder.
#include "Engine/UI/FontBuilder.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

namespace Fby::Ui
{
namespace
{

void SetError(std::string* InError, std::string InText)
{
    if (InError != nullptr)
    {
        *InError = std::move(InText);
    }
}

[[nodiscard]] std::uint32_t Align4(std::uint32_t InValue) noexcept
{
    return (InValue + 3u) & ~3u;
}

[[nodiscard]] std::vector<std::uint32_t> NormalizeCodepoints(std::span<const std::uint32_t> InCodepoints)
{
    std::vector<std::uint32_t> ScopedResult(InCodepoints.begin(), InCodepoints.end());
    std::sort(ScopedResult.begin(), ScopedResult.end());
    ScopedResult.erase(std::unique(ScopedResult.begin(), ScopedResult.end()), ScopedResult.end());
    return ScopedResult;
}

[[nodiscard]] bool BuildBitmapAtlas(const stbtt_fontinfo& InFont, std::span<const std::byte> InTtf,
                                    const FontBuildOptions& InOptions, const std::vector<std::uint32_t>& InCodepoints,
                                    std::vector<std::byte>* OutCooked, std::string* InError)
{
    const float ScopedPixelSize = std::clamp(InOptions.pixelSize, 4.0f, 256.0f);
    const std::uint32_t ScopedOversample = std::clamp(InOptions.oversample, 1u, 4u);
    const float ScopedScale = stbtt_ScaleForPixelHeight(&InFont, ScopedPixelSize);
    int ScopedAscent = 0;
    int ScopedDescent = 0;
    int ScopedLineGap = 0;
    stbtt_GetFontVMetrics(&InFont, &ScopedAscent, &ScopedDescent, &ScopedLineGap);

    const std::uint32_t ScopedPinnedSize = std::clamp(InOptions.atlasSize, 0u, 8192u);
    std::uint32_t ScopedWidth = ScopedPinnedSize != 0 ? ScopedPinnedSize : 512;
    std::uint32_t ScopedHeight = ScopedWidth;
    std::vector<unsigned char> ScopedBitmap;
    std::vector<stbtt_packedchar> ScopedPacked(InCodepoints.size());
    std::vector<stbtt_pack_range> ScopedRanges(InCodepoints.size());
    bool ScopedRendered = false;
    for (std::uint32_t ScopedAttempt = 0; ScopedAttempt < 4 && !ScopedRendered; ++ScopedAttempt)
    {
        ScopedBitmap.assign(static_cast<std::size_t>(ScopedWidth) * ScopedHeight, 0u);
        stbtt_pack_context ScopedPack{};
        if (stbtt_PackBegin(&ScopedPack, ScopedBitmap.data(), static_cast<int>(ScopedWidth),
                            static_cast<int>(ScopedHeight), 0, 1, nullptr) == 0)
        {
            SetError(InError, "stb_truetype could not start the packer");
            return false;
        }
        stbtt_PackSetOversampling(&ScopedPack, ScopedOversample, ScopedOversample);
        for (std::size_t ScopedIndex = 0; ScopedIndex < InCodepoints.size(); ++ScopedIndex)
        {
            ScopedRanges[ScopedIndex].font_size = ScopedPixelSize;
            ScopedRanges[ScopedIndex].first_unicode_codepoint_in_range =
                static_cast<int>(InCodepoints[ScopedIndex]);
            ScopedRanges[ScopedIndex].num_chars = 1;
            ScopedRanges[ScopedIndex].chardata_for_range = &ScopedPacked[ScopedIndex];
        }
        ScopedRendered = stbtt_PackFontRanges(
                             &ScopedPack, reinterpret_cast<const unsigned char*>(InTtf.data()), 0,
                             ScopedRanges.data(), static_cast<int>(ScopedRanges.size())) != 0;
        stbtt_PackEnd(&ScopedPack);
        if (!ScopedRendered)
        {
            if (ScopedPinnedSize != 0)
            {
                break;
            }
            ScopedWidth *= 2;
            ScopedHeight *= 2;
        }
    }
    if (!ScopedRendered)
    {
        SetError(InError, "glyphs do not fit in the bitmap atlas");
        return false;
    }

    std::vector<GlyphRecord> ScopedGlyphs(InCodepoints.size());
    for (std::size_t ScopedIndex = 0; ScopedIndex < InCodepoints.size(); ++ScopedIndex)
    {
        const stbtt_packedchar& ScopedGlyph = ScopedPacked[ScopedIndex];
        GlyphRecord& ScopedRecord = ScopedGlyphs[ScopedIndex];
        ScopedRecord.codepoint = InCodepoints[ScopedIndex];
        ScopedRecord.u0 = static_cast<float>(ScopedGlyph.x0) / static_cast<float>(ScopedWidth);
        ScopedRecord.v0 = static_cast<float>(ScopedGlyph.y0) / static_cast<float>(ScopedHeight);
        ScopedRecord.u1 = static_cast<float>(ScopedGlyph.x1) / static_cast<float>(ScopedWidth);
        ScopedRecord.v1 = static_cast<float>(ScopedGlyph.y1) / static_cast<float>(ScopedHeight);
        ScopedRecord.advance = ScopedGlyph.xadvance;
        ScopedRecord.bearingX = ScopedGlyph.xoff;
        ScopedRecord.bearingY = -ScopedGlyph.yoff;
        ScopedRecord.width = static_cast<float>(ScopedGlyph.x1 - ScopedGlyph.x0);
        ScopedRecord.height = static_cast<float>(ScopedGlyph.y1 - ScopedGlyph.y0);
        ScopedRecord.glyphId = static_cast<std::uint32_t>(
            stbtt_FindGlyphIndex(&InFont, static_cast<int>(InCodepoints[ScopedIndex])));
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
    ScopedHeader.pixelOffset = Align4(ScopedHeader.glyphTableOffset + ScopedGlyphBytes);
    ScopedHeader.lineHeight = static_cast<float>(ScopedAscent - ScopedDescent + ScopedLineGap) * ScopedScale;
    ScopedHeader.ascent = static_cast<float>(ScopedAscent) * ScopedScale;
    ScopedHeader.descent = static_cast<float>(-ScopedDescent) * ScopedScale;
    ScopedHeader.fontId = InOptions.fontId;
    ScopedHeader.reserved = static_cast<std::uint32_t>(InOptions.style);

    const std::size_t ScopedPixelCount = static_cast<std::size_t>(ScopedWidth) * ScopedHeight;
    OutCooked->assign(ScopedHeader.pixelOffset + ScopedPixelCount * 4u, std::byte{0});
    std::memcpy(OutCooked->data(), &ScopedHeader, sizeof(ScopedHeader));
    std::memcpy(OutCooked->data() + ScopedHeader.glyphTableOffset, ScopedGlyphs.data(), ScopedGlyphBytes);
    std::byte* ScopedPixels = OutCooked->data() + ScopedHeader.pixelOffset;
    for (std::size_t ScopedTexel = 0; ScopedTexel < ScopedPixelCount; ++ScopedTexel)
    {
        const unsigned char ScopedAlpha = ScopedBitmap[ScopedTexel];
        ScopedPixels[ScopedTexel * 4u + 0u] = std::byte{0xFF};
        ScopedPixels[ScopedTexel * 4u + 1u] = std::byte{0xFF};
        ScopedPixels[ScopedTexel * 4u + 2u] = std::byte{0xFF};
        ScopedPixels[ScopedTexel * 4u + 3u] = static_cast<std::byte>(ScopedAlpha);
    }
    return true;
}

[[nodiscard]] bool BuildSdfAtlas(const stbtt_fontinfo& InFont, const FontBuildOptions& InOptions,
                                 const std::vector<std::uint32_t>& InCodepoints,
                                 std::vector<std::byte>* OutCooked, std::string* InError)
{
    const float ScopedPixelSize = std::clamp(InOptions.pixelSize, 4.0f, 256.0f);
    const float ScopedScale = stbtt_ScaleForPixelHeight(&InFont, ScopedPixelSize);
    int ScopedAscent = 0;
    int ScopedDescent = 0;
    int ScopedLineGap = 0;
    stbtt_GetFontVMetrics(&InFont, &ScopedAscent, &ScopedDescent, &ScopedLineGap);

    const std::array<std::uint32_t, 3> ScopedAutoEdges{1024u, 2048u, 4096u};
    const std::uint32_t ScopedPinnedSize = std::clamp(InOptions.atlasSize, 0u, 8192u);
    const std::span<const std::uint32_t> ScopedEdges =
        ScopedPinnedSize != 0 ? std::span<const std::uint32_t>(&ScopedPinnedSize, 1)
                              : std::span<const std::uint32_t>(ScopedAutoEdges);
    std::vector<std::uint32_t> ScopedGlyphIds;
    std::vector<std::uint32_t> ScopedGlyphCodepoints;
    if (InOptions.allGlyphs)
    {
        const std::uint32_t ScopedPageSize = InOptions.glyphPageSize;
        const std::uint32_t ScopedFirst = ScopedPageSize != 0 ? InOptions.glyphPageIndex * ScopedPageSize : 0u;
        const std::uint32_t ScopedLast =
            ScopedPageSize != 0
                ? std::min(static_cast<std::uint32_t>(std::max(InFont.numGlyphs, 0)), ScopedFirst + ScopedPageSize)
                : static_cast<std::uint32_t>(std::max(InFont.numGlyphs, 0));
        if (ScopedFirst >= ScopedLast)
        {
            SetError(InError, "font page is past the last glyph");
            return false;
        }
        ScopedGlyphIds.reserve(static_cast<std::size_t>(ScopedLast - ScopedFirst));
        ScopedGlyphCodepoints.reserve(ScopedGlyphIds.capacity());
        for (std::uint32_t ScopedGlyphIndex = ScopedFirst; ScopedGlyphIndex < ScopedLast; ++ScopedGlyphIndex)
        {
            ScopedGlyphIds.push_back(ScopedGlyphIndex);
            ScopedGlyphCodepoints.push_back(ScopedGlyphIndex);
        }
    }
    else
    {
        ScopedGlyphIds.reserve(InCodepoints.size());
        ScopedGlyphCodepoints.reserve(InCodepoints.size());
        for (const std::uint32_t ScopedCodepoint : InCodepoints)
        {
            ScopedGlyphIds.push_back(
                static_cast<std::uint32_t>(stbtt_FindGlyphIndex(&InFont, static_cast<int>(ScopedCodepoint))));
            ScopedGlyphCodepoints.push_back(ScopedCodepoint);
        }
    }
    for (const std::uint32_t ScopedAtlasEdge : ScopedEdges)
    {
        std::vector<unsigned char> ScopedPixels(static_cast<std::size_t>(ScopedAtlasEdge) * ScopedAtlasEdge, 0u);
        std::vector<GlyphRecord> ScopedGlyphs;
        ScopedGlyphs.reserve(ScopedGlyphIds.size());
        std::uint32_t ScopedCursorX = 0;
        std::uint32_t ScopedCursorY = 0;
        std::uint32_t ScopedRowHeight = 0;
        bool ScopedFits = true;
        for (std::size_t ScopedSourceIndex = 0; ScopedSourceIndex < ScopedGlyphIds.size(); ++ScopedSourceIndex)
        {
            const int ScopedGlyphIndex = static_cast<int>(ScopedGlyphIds[ScopedSourceIndex]);
            const std::uint32_t ScopedCodepoint = ScopedGlyphCodepoints[ScopedSourceIndex];
            int ScopedAdvance = 0;
            int ScopedBearing = 0;
            stbtt_GetGlyphHMetrics(&InFont, ScopedGlyphIndex, &ScopedAdvance, &ScopedBearing);
            int ScopedWidth = 0;
            int ScopedHeight = 0;
            int ScopedXOffset = 0;
            int ScopedYOffset = 0;
            unsigned char* ScopedSdf =
                stbtt_GetGlyphSDF(&InFont, ScopedScale, ScopedGlyphIndex, 6, 128, 8.0f, &ScopedWidth,
                                  &ScopedHeight, &ScopedXOffset, &ScopedYOffset);
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
                if (ScopedCursorX + static_cast<std::uint32_t>(ScopedWidth) > ScopedAtlasEdge)
                {
                    ScopedCursorX = 0;
                    ScopedCursorY += ScopedRowHeight + 1u;
                    ScopedRowHeight = 0;
                }
                if (ScopedCursorY + static_cast<std::uint32_t>(ScopedHeight) > ScopedAtlasEdge)
                {
                    ScopedFits = false;
                }
                else
                {
                    for (int ScopedRow = 0; ScopedRow < ScopedHeight; ++ScopedRow)
                    {
                        std::memcpy(ScopedPixels.data() +
                                        static_cast<std::size_t>(ScopedCursorY + static_cast<std::uint32_t>(ScopedRow)) *
                                            ScopedAtlasEdge +
                                        ScopedCursorX,
                                    ScopedSdf + static_cast<std::size_t>(ScopedRow) * ScopedWidth,
                                    static_cast<std::size_t>(ScopedWidth));
                    }
                    ScopedRecord.u0 = static_cast<float>(ScopedCursorX) / static_cast<float>(ScopedAtlasEdge);
                    ScopedRecord.v0 = static_cast<float>(ScopedCursorY) / static_cast<float>(ScopedAtlasEdge);
                    ScopedRecord.u1 =
                        static_cast<float>(ScopedCursorX + static_cast<std::uint32_t>(ScopedWidth)) /
                        static_cast<float>(ScopedAtlasEdge);
                    ScopedRecord.v1 =
                        static_cast<float>(ScopedCursorY + static_cast<std::uint32_t>(ScopedHeight)) /
                        static_cast<float>(ScopedAtlasEdge);
                    ScopedCursorX += static_cast<std::uint32_t>(ScopedWidth) + 1u;
                    ScopedRowHeight = std::max(ScopedRowHeight, static_cast<std::uint32_t>(ScopedHeight));
                }
            }
            stbtt_FreeSDF(ScopedSdf, nullptr);
            ScopedGlyphs.push_back(ScopedRecord);
            if (!ScopedFits)
            {
                break;
            }
        }
        if (!ScopedFits)
        {
            continue;
        }

        FontAtlasHeader ScopedHeader{};
        std::memcpy(ScopedHeader.magic, kFontAtlasMagic, sizeof(ScopedHeader.magic));
        ScopedHeader.version = kFontAtlasVersion;
        ScopedHeader.headerBytes = sizeof(FontAtlasHeader);
        ScopedHeader.glyphCount = static_cast<std::uint32_t>(ScopedGlyphs.size());
        ScopedHeader.atlasWidth = ScopedAtlasEdge;
        ScopedHeader.atlasHeight = ScopedAtlasEdge;
        ScopedHeader.pixelFormat = static_cast<std::uint32_t>(FontPixelFormat::SdfR8);
        ScopedHeader.glyphTableOffset = sizeof(FontAtlasHeader);
        const std::uint32_t ScopedGlyphBytes =
            static_cast<std::uint32_t>(ScopedGlyphs.size() * sizeof(GlyphRecord));
        ScopedHeader.pixelOffset = Align4(ScopedHeader.glyphTableOffset + ScopedGlyphBytes);
        ScopedHeader.lineHeight = static_cast<float>(ScopedAscent - ScopedDescent + ScopedLineGap) * ScopedScale;
        ScopedHeader.ascent = static_cast<float>(ScopedAscent) * ScopedScale;
        ScopedHeader.descent = static_cast<float>(-ScopedDescent) * ScopedScale;
        ScopedHeader.fontId = InOptions.fontId;
        ScopedHeader.reserved = static_cast<std::uint32_t>(InOptions.style);

        OutCooked->assign(ScopedHeader.pixelOffset + ScopedPixels.size(), std::byte{0});
        std::memcpy(OutCooked->data(), &ScopedHeader, sizeof(ScopedHeader));
        std::memcpy(OutCooked->data() + ScopedHeader.glyphTableOffset, ScopedGlyphs.data(), ScopedGlyphBytes);
        std::memcpy(OutCooked->data() + ScopedHeader.pixelOffset, ScopedPixels.data(), ScopedPixels.size());
        return true;
    }
    SetError(InError, "glyphs do not fit in the SDF atlas");
    return false;
}
}

std::vector<std::uint32_t> DefaultFontCodepoints()
{
    std::vector<std::uint32_t> ScopedCodepoints;
    for (std::uint32_t ScopedCodepoint = 0x20; ScopedCodepoint <= 0x7E; ++ScopedCodepoint)
    {
        ScopedCodepoints.push_back(ScopedCodepoint);
    }
    for (std::uint32_t ScopedCodepoint = 0xA0; ScopedCodepoint <= 0xFF; ++ScopedCodepoint)
    {
        ScopedCodepoints.push_back(ScopedCodepoint);
    }
    for (std::uint32_t ScopedCodepoint = 0x400; ScopedCodepoint <= 0x4FF; ++ScopedCodepoint)
    {
        ScopedCodepoints.push_back(ScopedCodepoint);
    }
    return ScopedCodepoints;
}

bool BuildFontAtlasFromTtf(std::span<const std::byte> InTtf, const FontBuildOptions& InOptions,
                           std::vector<std::byte>* OutCooked, std::string* InError)
{
    if (OutCooked == nullptr || InTtf.empty())
    {
        SetError(InError, "TTF input is empty");
        return false;
    }
    OutCooked->clear();
    const int ScopedFontOffset = stbtt_GetFontOffsetForIndex(
        reinterpret_cast<const unsigned char*>(InTtf.data()), 0);
    if (ScopedFontOffset < 0)
    {
        SetError(InError, "TTF contains no supported font");
        return false;
    }
    stbtt_fontinfo ScopedFont{};
    if (stbtt_InitFont(&ScopedFont, reinterpret_cast<const unsigned char*>(InTtf.data()), ScopedFontOffset) == 0)
    {
        SetError(InError, "stb_truetype rejected the TTF");
        return false;
    }

    std::vector<std::uint32_t> ScopedCodepoints;
    if (InOptions.codepoints.empty())
    {
        ScopedCodepoints = DefaultFontCodepoints();
    }
    else
    {
        ScopedCodepoints = NormalizeCodepoints(InOptions.codepoints);
    }
    std::vector<std::uint32_t> ScopedExisting;
    ScopedExisting.reserve(ScopedCodepoints.size());
    for (const std::uint32_t ScopedCodepoint : ScopedCodepoints)
    {
        if (stbtt_FindGlyphIndex(&ScopedFont, static_cast<int>(ScopedCodepoint)) != 0)
        {
            ScopedExisting.push_back(ScopedCodepoint);
        }
    }
    if (ScopedExisting.empty())
    {
        SetError(InError, "the font covers none of the requested codepoints");
        return false;
    }

    if (InOptions.sdf)
    {
        return BuildSdfAtlas(ScopedFont, InOptions, ScopedExisting, OutCooked, InError);
    }
    return BuildBitmapAtlas(ScopedFont, InTtf, InOptions, ScopedExisting, OutCooked, InError);
}

bool BuildFontCoverageFromTtf(std::span<const std::byte> InTtf, FontCoverage* OutCoverage)
{
    if (OutCoverage == nullptr || InTtf.empty())
    {
        return false;
    }
    const int ScopedFontOffset =
        stbtt_GetFontOffsetForIndex(reinterpret_cast<const unsigned char*>(InTtf.data()), 0);
    if (ScopedFontOffset < 0)
    {
        return false;
    }
    stbtt_fontinfo ScopedFont{};
    if (stbtt_InitFont(&ScopedFont, reinterpret_cast<const unsigned char*>(InTtf.data()), ScopedFontOffset) == 0)
    {
        return false;
    }
    *OutCoverage = FontCoverage{};
    for (std::uint32_t ScopedCodepoint = 0; ScopedCodepoint < kCoverageCodepoints; ++ScopedCodepoint)
    {
        if (stbtt_FindGlyphIndex(&ScopedFont, static_cast<int>(ScopedCodepoint)) != 0)
        {
            OutCoverage->Set(ScopedCodepoint);
        }
    }
    return true;
}
}
