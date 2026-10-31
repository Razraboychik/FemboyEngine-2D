// FemboyEngine2D - line wrapping for UTF-8 text.
#pragma once

#include "Engine/Text/TextBidi.hpp"
#include "Engine/Text/TextStyle.hpp"
#include "Engine/UI/FontAtlas.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace Fby::Text
{

inline constexpr std::uint32_t kMaxTextLines = 4096;
inline constexpr std::uint32_t kTextInlinePlaceholder = 0xFFFCu;

struct TextLine
{
    std::uint32_t Begin = 0;
    std::uint32_t End = 0;
    std::uint32_t GlyphCount = 0;
    float Width = 0.0f;
    float Height = 0.0f;
};

[[nodiscard]] constexpr Ui::FontStyle FontStyleForTextFlags(std::uint32_t InFlags) noexcept
{
    const bool ScopedBold = (InFlags & static_cast<std::uint32_t>(TextStyleFlags::Bold)) != 0;
    const bool ScopedItalic = (InFlags & static_cast<std::uint32_t>(TextStyleFlags::Italic)) != 0;
    if ((InFlags & static_cast<std::uint32_t>(TextStyleFlags::Monospace)) != 0)
    {
        return Ui::FontStyle::Mono;
    }
    if (ScopedBold && ScopedItalic)
    {
        return Ui::FontStyle::BoldItalic;
    }
    if (ScopedBold)
    {
        return Ui::FontStyle::Bold;
    }
    if (ScopedItalic)
    {
        return Ui::FontStyle::Italic;
    }
    return Ui::FontStyle::Regular;
}

struct TextLayoutParams
{
    float MaxWidth = 320.0f;
    float Advance = 8.0f;
    // Inline atoms are looked up by the byte offset of their U+FFFC placeholder.
    std::span<const TextInlineObject> Inlines{};
    // When set, per-codepoint advances come from the selected font style slot.
    const Ui::FontAtlasSet* Fonts = nullptr;
    std::uint32_t FontId = 0;
    float FontSize = 0.0f;
    std::span<const TextSpan> Spans{};
    std::span<const TextStyle> Styles{};
    // CJK/vertical mode: advances run down a column; glyphs stay upright.
    bool Vertical = false;
    // Mirror inline-object positions inside a line for RTL layout.
    bool RightToLeft = false;
};

struct TextLayoutResult
{
    std::array<TextLine, kMaxTextLines> Lines{};
    // Bidi runs of the whole block in visual order. Lines only carry byte
    // ranges, so hit-testing and any glyph walk reorder their output with these.
    std::array<TextRun, kMaxTextRuns> Runs{};
    std::uint32_t Count = 0;
    std::uint32_t RunCount = 0;
    bool Overflow = false;
};

// Width of one codepoint in layout space. Inline atoms reserve their own width
// and the font metrics win over the advance when an atlas is attached.
[[nodiscard]] inline float InlineAdvanceAt(const TextLayoutParams& InParams, std::size_t InOffset,
                                           std::uint32_t InCodepoint) noexcept
{
    const float ScopedFallback = InParams.Advance > 0.0f ? InParams.Advance : 1.0f;
    if (InCodepoint == kTextInlinePlaceholder)
    {
        for (const TextInlineObject& ScopedInline : InParams.Inlines)
        {
            if (ScopedInline.ByteOffset == InOffset && ScopedInline.Width > 0.0f)
            {
                return ScopedInline.Width;
            }
        }
    }
    if (InParams.Fonts != nullptr && InParams.FontSize > 0.0f)
    {
        float ScopedFontSize = InParams.FontSize;
        std::uint32_t ScopedFlags = 0;
        for (const TextSpan& ScopedSpan : InParams.Spans)
        {
            if (InOffset >= ScopedSpan.Begin && InOffset < ScopedSpan.End &&
                ScopedSpan.StyleIndex < InParams.Styles.size())
            {
                ScopedFlags = InParams.Styles[ScopedSpan.StyleIndex].Flags;
                if (InParams.Styles[ScopedSpan.StyleIndex].FontSize > 0)
                {
                    ScopedFontSize = static_cast<float>(InParams.Styles[ScopedSpan.StyleIndex].FontSize);
                }
                break;
            }
        }
        const Ui::FontStyle ScopedStyle = FontStyleForTextFlags(ScopedFlags);
        const Ui::FontAtlas* ScopedAtlas = InParams.Fonts->FindByFontIdAndStyle(InParams.FontId, ScopedStyle);
        if (ScopedAtlas == nullptr)
        {
            ScopedAtlas = InParams.Fonts->FindByFontIdAndStyle(InParams.FontId, Ui::FontStyle::Regular);
        }
        if (ScopedAtlas != nullptr && ScopedAtlas->IsOpen() && ScopedAtlas->LineHeight() > 0.0f)
        {
            const Ui::FontAtlas* ScopedResolved = ScopedAtlas;
            const Ui::GlyphRecord* ScopedGlyph =
                InParams.Fonts->ResolveGlyph(InParams.FontId, ScopedStyle, InCodepoint, &ScopedResolved);
            if (ScopedGlyph != nullptr && ScopedGlyph->advance > 0.0f && ScopedResolved != nullptr &&
                ScopedResolved->LineHeight() > 0.0f)
            {
                return ScopedGlyph->advance * (ScopedFontSize / ScopedResolved->LineHeight());
            }
        }
    }
    return ScopedFallback;
}

// UAX 9 rule L2: turns the logical run sequence of one paragraph into visual
// order by reversing every level band from the highest level down to the
// lowest odd level. Runs keep their byte ranges; only their order changes.
inline void ReorderRunsVisual(std::span<TextRun> InRuns, std::uint32_t InCount) noexcept
{
    if (InCount < 2)
    {
        return;
    }
    std::uint8_t ScopedMaxLevel = 0;
    std::uint8_t ScopedLowestOdd = 0xFFu;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < InCount; ++ScopedIndex)
    {
        const std::uint8_t ScopedLevel = InRuns[ScopedIndex].BidiLevel;
        ScopedMaxLevel = std::max(ScopedMaxLevel, ScopedLevel);
        if ((ScopedLevel & 1u) != 0)
        {
            ScopedLowestOdd = std::min<std::uint8_t>(ScopedLowestOdd, ScopedLevel);
        }
    }
    if (ScopedLowestOdd == 0xFFu)
    {
        return;
    }
    for (int ScopedLevel = ScopedMaxLevel; ScopedLevel >= static_cast<int>(ScopedLowestOdd); --ScopedLevel)
    {
        std::uint32_t ScopedIndex = 0;
        while (ScopedIndex < InCount)
        {
            if (InRuns[ScopedIndex].BidiLevel < ScopedLevel)
            {
                ++ScopedIndex;
                continue;
            }
            const std::uint32_t ScopedBegin = ScopedIndex;
            while (ScopedIndex < InCount && InRuns[ScopedIndex].BidiLevel >= ScopedLevel)
            {
                ++ScopedIndex;
            }
            std::reverse(InRuns.begin() + ScopedBegin, InRuns.begin() + ScopedIndex);
        }
    }
}

// Fills the visual runs of a block and returns their count. SplitRtlRuns
// returns visual order from both backends (SheenBidi emits the line runs in
// visual order, the fallback reorders with rule L2), so this only has to copy
// them and fall back to one run when the splitter fails.
[[nodiscard]] inline std::uint32_t BuildVisualRuns(std::string_view InText, TextDirection InBaseDirection,
                                                   std::span<TextRun> OutRuns) noexcept
{
    if (OutRuns.empty())
    {
        return 0;
    }
    OutRuns[0] = TextRun{0, static_cast<std::uint32_t>(InText.size()), TextScript::Unknown,
                         InBaseDirection == TextDirection::RightToLeft ? TextDirection::RightToLeft
                                                                       : TextDirection::LeftToRight,
                         InBaseDirection == TextDirection::RightToLeft ? std::uint8_t{1} : std::uint8_t{0}};
    std::uint32_t ScopedCount = 0;
    if (!SplitRtlRuns(InText, InBaseDirection, OutRuns, &ScopedCount) || ScopedCount == 0)
    {
        return 1;
    }
    return ScopedCount;
}

struct TextInlineHit
{
    std::uint32_t Id = 0;
    TextInlineKind Kind = TextInlineKind::Sprite;
    float X = 0.0f;
    float Y = 0.0f;
    float Width = 0.0f;
    float Height = 0.0f;
};

[[nodiscard]] inline std::uint32_t DecodeTextUtf8(std::string_view InText, std::size_t& InCursor) noexcept;

// Hit-tests inline atoms in layout space. Callers use this for button presses,
// tooltips and link hit regions without re-running the renderer. The walk
// follows the visual runs of the layout, so a mirrored right-to-left line and a
// mixed line report the same atoms at the same pixels a renderer would use.
[[nodiscard]] inline bool HitTestTextInline(std::string_view InText, const TextLayoutResult& InLayout,
                                            const TextLayoutParams& InParams, Vector2D InOrigin,
                                            Vector2D InPoint, TextInlineHit* OutHit = nullptr) noexcept
{
    // Probes one atom or one plain codepoint placed at InLeft with InWidth.
    const auto ScopedProbe = [&](std::size_t InOffset, std::uint32_t InCodepoint, float InLeft, float InWidth,
                                 float InTop, float InHeight) noexcept
    {
        if (InCodepoint != kTextInlinePlaceholder)
        {
            return false;
        }
        for (const TextInlineObject& ScopedInline : InParams.Inlines)
        {
            if (ScopedInline.ByteOffset != InOffset || ScopedInline.Width <= 0.0f)
            {
                continue;
            }
            const bool ScopedInside = InPoint.X >= InLeft && InPoint.X <= InLeft + InWidth &&
                                      InPoint.Y >= InTop && InPoint.Y <= InTop + InHeight;
            if (ScopedInside)
            {
                if (OutHit != nullptr)
                {
                    *OutHit = TextInlineHit{ScopedInline.Id, ScopedInline.Kind, InLeft, InTop, InWidth, InHeight};
                }
                return true;
            }
            return false;
        }
        return false;
    };

    float ScopedLineY = InOrigin.Y;
    for (std::uint32_t ScopedLineIndex = 0; ScopedLineIndex < InLayout.Count; ++ScopedLineIndex)
    {
        const TextLine& ScopedLine = InLayout.Lines[ScopedLineIndex];
        const float ScopedLineCenter = ScopedLineY;
        if (InLayout.RunCount == 0)
        {
            // Untracked layout: one logical run, mirrored when the caller asked
            // for right-to-left text.
            float ScopedPenX = InOrigin.X;
            std::size_t ScopedCursor = ScopedLine.Begin;
            while (ScopedCursor < ScopedLine.End && ScopedCursor < InText.size())
            {
                const std::size_t ScopedBefore = ScopedCursor;
                const std::uint32_t ScopedCodepoint = DecodeTextUtf8(InText, ScopedCursor);
                const float ScopedAdvance = InlineAdvanceAt(InParams, ScopedBefore, ScopedCodepoint);
                const float ScopedVisualX =
                    InParams.RightToLeft
                        ? InOrigin.X + ScopedLine.Width - (ScopedPenX - InOrigin.X) - ScopedAdvance
                        : ScopedPenX;
                const float ScopedTop = ScopedLineCenter;
                if (ScopedProbe(ScopedBefore, ScopedCodepoint, ScopedVisualX, ScopedAdvance, ScopedTop,
                                ScopedLine.Height))
                {
                    return true;
                }
                ScopedPenX += ScopedAdvance;
            }
            ScopedLineY += ScopedLine.Height;
            continue;
        }
        float ScopedPenX = InOrigin.X;
        for (std::uint32_t ScopedRunIndex = 0; ScopedRunIndex < InLayout.RunCount; ++ScopedRunIndex)
        {
            const TextRun& ScopedRun = InLayout.Runs[ScopedRunIndex];
            const std::uint32_t ScopedClipBegin = std::max(ScopedRun.Begin, ScopedLine.Begin);
            const std::uint32_t ScopedClipEnd = std::min(ScopedRun.End, ScopedLine.End);
            if (ScopedClipEnd <= ScopedClipBegin)
            {
                continue;
            }
            float ScopedRunWidth = 0.0f;
            std::size_t ScopedCursor = ScopedClipBegin;
            while (ScopedCursor < ScopedClipEnd && ScopedCursor < InText.size())
            {
                const std::size_t ScopedBefore = ScopedCursor;
                const std::uint32_t ScopedCodepoint = DecodeTextUtf8(InText, ScopedCursor);
                ScopedRunWidth += InlineAdvanceAt(InParams, ScopedBefore, ScopedCodepoint);
            }
            const bool ScopedRtl = ScopedRun.Direction == TextDirection::RightToLeft;
            // A right-to-left run is laid out from its own right edge, so its
            // first logical codepoint ends up in the rightmost cell.
            float ScopedCursorX = ScopedRtl ? ScopedPenX + ScopedRunWidth : ScopedPenX;
            ScopedCursor = ScopedClipBegin;
            while (ScopedCursor < ScopedClipEnd && ScopedCursor < InText.size())
            {
                const std::size_t ScopedBefore = ScopedCursor;
                const std::uint32_t ScopedCodepoint = DecodeTextUtf8(InText, ScopedCursor);
                const float ScopedAdvance = InlineAdvanceAt(InParams, ScopedBefore, ScopedCodepoint);
                const float ScopedLeft = ScopedRtl ? ScopedCursorX - ScopedAdvance : ScopedCursorX;
                if (ScopedProbe(ScopedBefore, ScopedCodepoint, ScopedLeft, ScopedAdvance, ScopedLineCenter,
                                ScopedLine.Height))
                {
                    return true;
                }
                ScopedCursorX = ScopedRtl ? ScopedLeft : ScopedCursorX + ScopedAdvance;
            }
            ScopedPenX += ScopedRunWidth;
        }
        ScopedLineY += ScopedLine.Height;
    }
    return false;
}

// Decodes one UTF-8 scalar and advances the cursor. Invalid bytes become U+FFFD.
[[nodiscard]] inline std::uint32_t DecodeTextUtf8(std::string_view InText, std::size_t& InCursor) noexcept
{
    if (InCursor >= InText.size())
    {
        return 0;
    }
    const auto ScopedByte = static_cast<unsigned char>(InText[InCursor]);
    if (ScopedByte < 0x80u)
    {
        ++InCursor;
        return ScopedByte;
    }
    std::uint32_t ScopedCodepoint = 0xFFFDu;
    std::uint32_t ScopedLength = 1;
    if ((ScopedByte & 0xE0u) == 0xC0u)
    {
        ScopedCodepoint = ScopedByte & 0x1Fu;
        ScopedLength = 2;
    }
    else if ((ScopedByte & 0xF0u) == 0xE0u)
    {
        ScopedCodepoint = ScopedByte & 0x0Fu;
        ScopedLength = 3;
    }
    else if ((ScopedByte & 0xF8u) == 0xF0u)
    {
        ScopedCodepoint = ScopedByte & 0x07u;
        ScopedLength = 4;
    }
    if (ScopedLength == 1 || InCursor + ScopedLength > InText.size())
    {
        ++InCursor;
        return 0xFFFDu;
    }
    for (std::uint32_t ScopedIndex = 1; ScopedIndex < ScopedLength; ++ScopedIndex)
    {
        const auto ScopedContinuation = static_cast<unsigned char>(InText[InCursor + ScopedIndex]);
        if ((ScopedContinuation & 0xC0u) != 0x80u)
        {
            ++InCursor;
            return 0xFFFDu;
        }
        ScopedCodepoint = (ScopedCodepoint << 6) | (ScopedContinuation & 0x3Fu);
    }
    InCursor += ScopedLength;
    return ScopedCodepoint;
}

[[nodiscard]] inline std::uint32_t CountTextGlyphs(std::string_view InText) noexcept
{
    std::size_t ScopedCursor = 0;
    std::uint32_t ScopedCount = 0;
    while (ScopedCursor < InText.size())
    {
        (void)DecodeTextUtf8(InText, ScopedCursor);
        ++ScopedCount;
    }
    return ScopedCount;
}

// Greedy word wrapping by pixel width. Inline atoms reserve their own Width and
// the output ranges still point into the original UTF-8 buffer.
[[nodiscard]] inline bool LayoutText(std::string_view InText, const TextLayoutParams& InParams,
                                     TextLayoutResult* OutResult) noexcept
{
    if (OutResult == nullptr)
    {
        return false;
    }
    OutResult->Count = 0;
    OutResult->Overflow = false;
    OutResult->RunCount = BuildVisualRuns(
        InText, InParams.RightToLeft ? TextDirection::RightToLeft : TextDirection::Auto,
        std::span<TextRun>(OutResult->Runs.data(), OutResult->Runs.size()));
    const float ScopedAdvance = InParams.Advance > 0.0f ? InParams.Advance : 1.0f;
    const float ScopedWidth = InParams.MaxWidth > 0.0f ? InParams.MaxWidth : ScopedAdvance;

    const auto ScopedEmit = [OutResult](std::size_t InBegin, std::size_t InEnd, std::uint32_t InGlyphs,
                                        float InWidth, float InHeight) noexcept
    {
        if (OutResult->Count >= kMaxTextLines)
        {
            OutResult->Overflow = true;
            return;
        }
        OutResult->Lines[OutResult->Count++] = TextLine{static_cast<std::uint32_t>(InBegin),
                                                        static_cast<std::uint32_t>(InEnd), InGlyphs, InWidth,
                                                        InHeight};
    };

    const auto ScopedWidthAt = [&InParams](std::size_t InOffset, std::uint32_t InCodepoint) noexcept
    {
        return InlineAdvanceAt(InParams, InOffset, InCodepoint);
    };

    const auto ScopedHeightAt = [&InParams, ScopedAdvance](std::size_t InOffset,
                                                           std::uint32_t InCodepoint) noexcept
    {
        const float ScopedBaseHeight = InParams.FontSize > 0.0f ? InParams.FontSize : ScopedAdvance;
        if (InCodepoint == kTextInlinePlaceholder)
        {
            for (const TextInlineObject& ScopedInline : InParams.Inlines)
            {
                if (ScopedInline.ByteOffset == InOffset && ScopedInline.Height > 0.0f)
                {
                    return std::max(ScopedBaseHeight, ScopedInline.Height);
                }
            }
        }
        for (const TextSpan& ScopedSpan : InParams.Spans)
        {
            if (InOffset >= ScopedSpan.Begin && InOffset < ScopedSpan.End &&
                ScopedSpan.StyleIndex < InParams.Styles.size() && InParams.Styles[ScopedSpan.StyleIndex].FontSize > 0)
            {
                return static_cast<float>(InParams.Styles[ScopedSpan.StyleIndex].FontSize);
            }
        }
        return ScopedBaseHeight;
    };

    std::size_t ScopedCursor = 0;
    std::size_t ScopedLineBegin = 0;
    std::uint32_t ScopedLineGlyphs = 0;
    float ScopedLineWidth = 0.0f;
    float ScopedLineHeight = 0.0f;
    while (ScopedCursor < InText.size())
    {
        const std::size_t ScopedWordBegin = ScopedCursor;
        std::uint32_t ScopedWordGlyphs = 0;
        float ScopedWordWidth = 0.0f;
        float ScopedWordHeight = 0.0f;
        while (ScopedCursor < InText.size())
        {
            const std::size_t ScopedBefore = ScopedCursor;
            const std::uint32_t ScopedCodepoint = DecodeTextUtf8(InText, ScopedCursor);
            if (ScopedCodepoint == ' ' || ScopedCodepoint == '\t' || ScopedCodepoint == '\n')
            {
                ScopedCursor = ScopedBefore;
                break;
            }
            ScopedWordWidth += ScopedWidthAt(ScopedBefore, ScopedCodepoint);
            ScopedWordHeight = std::max(ScopedWordHeight, ScopedHeightAt(ScopedBefore, ScopedCodepoint));
            ++ScopedWordGlyphs;
        }
        const std::size_t ScopedWordEnd = ScopedCursor;

        if (ScopedWordGlyphs != 0)
        {
            if (ScopedLineWidth > 0.0f && ScopedLineWidth + ScopedAdvance + ScopedWordWidth > ScopedWidth)
            {
                ScopedEmit(ScopedLineBegin, ScopedWordBegin, ScopedLineGlyphs, ScopedLineWidth, ScopedLineHeight);
                ScopedLineBegin = ScopedWordBegin;
                ScopedLineGlyphs = 0;
                ScopedLineWidth = 0.0f;
                ScopedLineHeight = 0.0f;
            }
            if (ScopedWordWidth > ScopedWidth && ScopedLineGlyphs == 0)
            {
                std::size_t ScopedChunkBegin = ScopedWordBegin;
                std::size_t ScopedChunkCursor = ScopedWordBegin;
                std::uint32_t ScopedChunkGlyphs = 0;
                float ScopedChunkWidth = 0.0f;
                float ScopedChunkHeight = 0.0f;
                while (ScopedChunkCursor < ScopedWordEnd)
                {
                    const std::size_t ScopedBefore = ScopedChunkCursor;
                    const std::uint32_t ScopedCodepoint = DecodeTextUtf8(InText, ScopedChunkCursor);
                    const float ScopedGlyphWidth = ScopedWidthAt(ScopedBefore, ScopedCodepoint);
                    if (ScopedChunkGlyphs != 0 && ScopedChunkWidth + ScopedGlyphWidth > ScopedWidth)
                    {
                        ScopedEmit(ScopedChunkBegin, ScopedBefore, ScopedChunkGlyphs, ScopedChunkWidth,
                                   ScopedChunkHeight);
                        ScopedChunkBegin = ScopedBefore;
                        ScopedChunkGlyphs = 0;
                        ScopedChunkWidth = 0.0f;
                        ScopedChunkHeight = 0.0f;
                    }
                    ScopedChunkWidth += ScopedGlyphWidth;
                    ScopedChunkHeight = std::max(ScopedChunkHeight, ScopedHeightAt(ScopedBefore, ScopedCodepoint));
                    ++ScopedChunkGlyphs;
                }
                ScopedLineBegin = ScopedChunkBegin;
                ScopedLineGlyphs = ScopedChunkGlyphs;
                ScopedLineWidth = ScopedChunkWidth;
                ScopedLineHeight = ScopedChunkHeight;
            }
            else
            {
                ScopedLineGlyphs += ScopedWordGlyphs;
                ScopedLineWidth += ScopedWordWidth;
                ScopedLineHeight = std::max(ScopedLineHeight, ScopedWordHeight);
            }
            ScopedCursor = ScopedWordEnd;
        }

        if (ScopedCursor < InText.size())
        {
            const std::size_t ScopedSeparator = ScopedCursor;
            const std::uint32_t ScopedCodepoint = DecodeTextUtf8(InText, ScopedCursor);
            if (ScopedCodepoint == '\n')
            {
                ScopedEmit(ScopedLineBegin, ScopedSeparator, ScopedLineGlyphs, ScopedLineWidth, ScopedLineHeight);
                ScopedLineBegin = ScopedCursor;
                ScopedLineGlyphs = 0;
                ScopedLineWidth = 0.0f;
                ScopedLineHeight = 0.0f;
            }
            else
            {
                ScopedLineWidth += ScopedAdvance;
                ScopedLineHeight = std::max(ScopedLineHeight, ScopedHeightAt(ScopedSeparator, ScopedCodepoint));
                ++ScopedLineGlyphs;
            }
        }
    }
    if (ScopedLineBegin < InText.size() || OutResult->Count == 0)
    {
        ScopedEmit(ScopedLineBegin, InText.size(), ScopedLineGlyphs, ScopedLineWidth, ScopedLineHeight);
    }
    return !OutResult->Overflow;
}
}
