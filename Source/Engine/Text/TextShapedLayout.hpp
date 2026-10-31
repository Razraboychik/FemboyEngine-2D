// FemboyEngine2D - greedy line layout over already-shaped glyphs.
#pragma once

#include "Engine/Text/TextLayout.hpp"
#include "Engine/Text/TextShaper.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string_view>

namespace Fby::Text
{

inline constexpr std::uint32_t kMaxShapedLines = 512;

struct ShapedLine
{
    std::uint32_t BeginGlyph = 0;
    std::uint32_t EndGlyph = 0;
    float Width = 0.0f;
    float Height = 0.0f;
};

struct ShapedLayoutResult
{
    std::array<ShapedLine, kMaxShapedLines> Lines{};
    std::uint32_t Count = 0;
    bool Overflow = false;
};

// Shaped glyphs are already in visual order for their run. This pass only
// performs greedy line breaking and preserves the shaper order inside a line.
[[nodiscard]] inline bool LayoutShapedGlyphs(std::string_view InText, const TextShapeResult& InShaped,
                                             const TextLayoutParams& InParams,
                                             ShapedLayoutResult* OutResult) noexcept
{
    if (OutResult == nullptr)
    {
        return false;
    }
    OutResult->Count = 0;
    OutResult->Overflow = false;
    const float ScopedMaxWidth = InParams.MaxWidth > 0.0f ? InParams.MaxWidth : 320.0f;
    const float ScopedHeight = InParams.FontSize > 0.0f ? InParams.FontSize : 16.0f;
    if (InParams.Vertical)
    {
        std::uint32_t ScopedColumnBegin = 0;
        float ScopedColumnWidth = 0.0f;
        float ScopedColumnHeight = 0.0f;
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < InShaped.Count; ++ScopedIndex)
        {
            const ShapedGlyph& ScopedGlyph = InShaped.Glyphs[ScopedIndex];
            float ScopedAdvance = std::abs(ScopedGlyph.AdvanceY);
            float ScopedGlyphWidth = std::abs(ScopedGlyph.AdvanceX);
            for (const TextInlineObject& ScopedInline : InParams.Inlines)
            {
                if (ScopedInline.ByteOffset == ScopedGlyph.Cluster && ScopedInline.Width > 0.0f &&
                    ScopedInline.Height > 0.0f)
                {
                    ScopedGlyphWidth = std::max(ScopedGlyphWidth, ScopedInline.Width);
                    ScopedAdvance = std::max(ScopedAdvance, ScopedInline.Height);
                    break;
                }
            }
            if (ScopedIndex > ScopedColumnBegin && ScopedColumnHeight + ScopedAdvance > ScopedMaxWidth)
            {
                if (OutResult->Count >= OutResult->Lines.size())
                {
                    OutResult->Overflow = true;
                    return false;
                }
                OutResult->Lines[OutResult->Count++] =
                    ShapedLine{ScopedColumnBegin, ScopedIndex, ScopedColumnWidth, ScopedColumnHeight};
                ScopedColumnBegin = ScopedIndex;
                ScopedColumnWidth = 0.0f;
                ScopedColumnHeight = 0.0f;
            }
            // A vertical glyph carries no horizontal advance, so the column is
            // at least one em wide; otherwise columns would overlap.
            ScopedColumnWidth = std::max(ScopedColumnWidth, std::max(ScopedGlyphWidth, ScopedHeight));
            ScopedColumnHeight += ScopedAdvance;
        }
        if (ScopedColumnBegin < InShaped.Count)
        {
            if (OutResult->Count >= OutResult->Lines.size())
            {
                OutResult->Overflow = true;
                return false;
            }
            OutResult->Lines[OutResult->Count++] =
                ShapedLine{ScopedColumnBegin, InShaped.Count, ScopedColumnWidth, ScopedColumnHeight};
        }
        return true;
    }
    std::uint32_t ScopedLineBegin = 0;
    float ScopedLineWidth = 0.0f;
    const auto ScopedEmit = [&](std::uint32_t InEnd)
    {
        if (OutResult->Count >= OutResult->Lines.size())
        {
            OutResult->Overflow = true;
            return false;
        }
        OutResult->Lines[OutResult->Count++] =
            ShapedLine{ScopedLineBegin, InEnd, ScopedLineWidth, ScopedHeight};
        ScopedLineBegin = InEnd;
        ScopedLineWidth = 0.0f;
        return true;
    };
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < InShaped.Count; ++ScopedIndex)
    {
        const ShapedGlyph& ScopedGlyph = InShaped.Glyphs[ScopedIndex];
        const bool ScopedNewline =
            ScopedGlyph.Cluster < InText.size() && InText[ScopedGlyph.Cluster] == '\n';
        if (ScopedNewline)
        {
            if (!ScopedEmit(ScopedIndex))
            {
                return false;
            }
            ScopedLineBegin = ScopedIndex + 1u;
            continue;
        }
        // An inline placeholder wraps by the atom width, not by the fallback
        // glyph advance, so a wide icon moves to the next line like TextLayout.
        float ScopedAdvance = ScopedGlyph.AdvanceX;
        for (const TextInlineObject& ScopedInline : InParams.Inlines)
        {
            if (ScopedInline.ByteOffset == ScopedGlyph.Cluster && ScopedInline.Width > 0.0f)
            {
                ScopedAdvance = ScopedInline.Width;
                break;
            }
        }
        if (ScopedLineWidth > 0.0f && ScopedLineWidth + ScopedAdvance > ScopedMaxWidth)
        {
            if (!ScopedEmit(ScopedIndex))
            {
                return false;
            }
        }
        ScopedLineWidth += ScopedAdvance;
    }
    if (ScopedLineBegin < InShaped.Count && !ScopedEmit(InShaped.Count))
    {
        return false;
    }
    return true;
}

struct ShapedGlyphHit
{
    std::uint32_t GlyphIndex = 0xFFFFFFFFu;
    std::uint32_t Cluster = 0;
};

// Coarse glyph hit-testing from shaped advances. It is sufficient for buttons,
// links and vertical CJK columns; pixel-exact SDF coverage comes later.
[[nodiscard]] inline bool HitTestShapedGlyph(std::span<const ShapedGlyph> InGlyphs,
                                             std::span<const ShapedLine> InLines, Vector2D InOrigin,
                                             float InFontSize, bool InVertical, Vector2D InPoint,
                                             ShapedGlyphHit* OutHit = nullptr) noexcept
{
    float ScopedLineY = InOrigin.Y;
    float ScopedLineX = InOrigin.X;
    for (const ShapedLine& ScopedLine : InLines)
    {
        float ScopedPenX = InOrigin.X;
        float ScopedPenY = InOrigin.Y;
        for (std::uint32_t ScopedIndex = ScopedLine.BeginGlyph;
             ScopedIndex < ScopedLine.EndGlyph && ScopedIndex < InGlyphs.size(); ++ScopedIndex)
        {
            const ShapedGlyph& ScopedGlyph = InGlyphs[ScopedIndex];
            const float ScopedAdvance = InVertical ? std::abs(ScopedGlyph.AdvanceY)
                                                   : std::abs(ScopedGlyph.AdvanceX);
            const bool ScopedInside = InVertical
                                          ? (InPoint.X >= ScopedLineX && InPoint.X <= ScopedLineX + InFontSize &&
                                             InPoint.Y >= ScopedPenY && InPoint.Y <= ScopedPenY + ScopedAdvance)
                                          : (InPoint.X >= ScopedPenX && InPoint.X <= ScopedPenX + ScopedAdvance &&
                                             InPoint.Y >= ScopedLineY && InPoint.Y <= ScopedLineY + InFontSize);
            if (ScopedInside)
            {
                if (OutHit != nullptr)
                {
                    *OutHit = ShapedGlyphHit{ScopedIndex, ScopedGlyph.Cluster};
                }
                return true;
            }
            ScopedPenX += ScopedAdvance;
            ScopedPenY += ScopedAdvance;
        }
        ScopedLineY += ScopedLine.Height;
        ScopedLineX += ScopedLine.Width + InFontSize;
    }
    return false;
}
}
