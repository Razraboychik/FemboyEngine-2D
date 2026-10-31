// FemboyEngine2D - conservative bidi/script run splitter.
#include "Engine/Text/TextBidi.hpp"

#include "Engine/Text/TextLayout.hpp"

#if defined(FBY_HAS_SHEENBIDI)
#include <SheenBidi/SheenBidi.h>
#endif

namespace Fby::Text
{

bool IsRtlCodepoint(std::uint32_t InCodepoint) noexcept
{
    return (InCodepoint >= 0x0590u && InCodepoint <= 0x05FFu) ||
           (InCodepoint >= 0x0600u && InCodepoint <= 0x06FFu) ||
           (InCodepoint >= 0x0750u && InCodepoint <= 0x077Fu) ||
           (InCodepoint >= 0x08A0u && InCodepoint <= 0x08FFu) ||
           (InCodepoint >= 0xFB50u && InCodepoint <= 0xFDFFu) ||
           (InCodepoint >= 0xFE70u && InCodepoint <= 0xFEFFu);
}

TextScript ScriptForCodepoint(std::uint32_t InCodepoint) noexcept
{
    if ((InCodepoint >= 0x0041u && InCodepoint <= 0x024Fu) ||
        (InCodepoint >= 0x1E00u && InCodepoint <= 0x1EFFu))
    {
        return TextScript::Latin;
    }
    if (InCodepoint >= 0x0400u && InCodepoint <= 0x04FFu)
    {
        return TextScript::Cyrillic;
    }
    if (InCodepoint >= 0x0370u && InCodepoint <= 0x03FFu)
    {
        return TextScript::Greek;
    }
    if ((InCodepoint >= 0x0600u && InCodepoint <= 0x06FFu) ||
        (InCodepoint >= 0x0750u && InCodepoint <= 0x077Fu) ||
        (InCodepoint >= 0x08A0u && InCodepoint <= 0x08FFu))
    {
        return TextScript::Arabic;
    }
    if (InCodepoint >= 0x0590u && InCodepoint <= 0x05FFu)
    {
        return TextScript::Hebrew;
    }
    if (InCodepoint >= 0x0900u && InCodepoint <= 0x097Fu)
    {
        return TextScript::Devanagari;
    }
    if (InCodepoint >= 0x0980u && InCodepoint <= 0x09FFu)
    {
        return TextScript::Bengali;
    }
    if (InCodepoint >= 0x0E00u && InCodepoint <= 0x0E7Fu)
    {
        return TextScript::Thai;
    }
    if ((InCodepoint >= 0x4E00u && InCodepoint <= 0x9FFFu) ||
        (InCodepoint >= 0x3400u && InCodepoint <= 0x4DBFu) ||
        (InCodepoint >= 0xF900u && InCodepoint <= 0xFAFFu) ||
        (InCodepoint >= 0x20000u && InCodepoint <= 0x2FA1Fu))
    {
        return TextScript::Han;
    }
    // CJK punctuation, ideographic space and the fullwidth forms belong to the
    // Han face: Inter has no glyph for them, so they must stay in a Han run.
    if (InCodepoint == 0x3000u || (InCodepoint >= 0x3001u && InCodepoint <= 0x303Fu) ||
        (InCodepoint >= 0xFE30u && InCodepoint <= 0xFE4Fu) ||
        (InCodepoint >= 0xFF01u && InCodepoint <= 0xFF60u) ||
        (InCodepoint >= 0xFFE0u && InCodepoint <= 0xFFE6u))
    {
        return TextScript::Han;
    }
    if (InCodepoint >= 0x3040u && InCodepoint <= 0x309Fu)
    {
        return TextScript::Hiragana;
    }
    if (InCodepoint >= 0x30A0u && InCodepoint <= 0x30FFu)
    {
        return TextScript::Katakana;
    }
    if (InCodepoint >= 0xAC00u && InCodepoint <= 0xD7AFu)
    {
        return TextScript::Hangul;
    }
    return TextScript::Unknown;
}

#if defined(FBY_HAS_SHEENBIDI)
namespace
{
// A bidi run can still mix scripts, for example a Latin word followed by a
// space and CJK text. This splits one bidi run at script changes; codepoints
// without a script of their own stay with the surrounding script so digits and
// spaces never produce a neutral run that a Latin face would shape.
[[nodiscard]] bool AppendScriptRuns(std::string_view InText, std::uint32_t InBegin, std::uint32_t InEnd,
                                    TextDirection InDirection, std::uint8_t InLevel, std::span<TextRun> OutRuns,
                                    std::uint32_t* OutCount) noexcept
{
    const std::string_view ScopedSlice = InText.substr(0, InEnd);
    std::size_t ScopedRunBegin = InBegin;
    std::size_t ScopedCursor = InBegin;
    TextScript ScopedRunScript = TextScript::Unknown;
    while (ScopedCursor < InEnd)
    {
        const std::size_t ScopedBefore = ScopedCursor;
        const std::uint32_t ScopedCodepoint = DecodeTextUtf8(ScopedSlice, ScopedCursor);
        const TextScript ScopedScript = ScriptForCodepoint(ScopedCodepoint);
        if (ScopedScript == TextScript::Unknown)
        {
            continue;
        }
        if (ScopedRunScript == TextScript::Unknown)
        {
            ScopedRunScript = ScopedScript;
            continue;
        }
        if (ScopedScript == ScopedRunScript)
        {
            continue;
        }
        if (*OutCount >= OutRuns.size())
        {
            return false;
        }
        TextRun& ScopedRun = OutRuns[(*OutCount)++];
        ScopedRun.Begin = static_cast<std::uint32_t>(ScopedRunBegin);
        ScopedRun.End = static_cast<std::uint32_t>(ScopedBefore);
        ScopedRun.Script = ScopedRunScript;
        ScopedRun.Direction = InDirection;
        ScopedRun.BidiLevel = InLevel;
        ScopedRunBegin = ScopedBefore;
        ScopedRunScript = ScopedScript;
    }
    if (ScopedRunBegin >= InEnd)
    {
        return true;
    }
    if (*OutCount >= OutRuns.size())
    {
        return false;
    }
    TextRun& ScopedRun = OutRuns[(*OutCount)++];
    ScopedRun.Begin = static_cast<std::uint32_t>(ScopedRunBegin);
    ScopedRun.End = InEnd;
    ScopedRun.Script = ScopedRunScript;
    ScopedRun.Direction = InDirection;
    ScopedRun.BidiLevel = InLevel;
    return true;
}
}

bool SplitRtlRuns(std::string_view InText, TextDirection InBaseDirection, std::span<TextRun> OutRuns,
                  std::uint32_t* OutCount) noexcept
{
    if (OutCount == nullptr || OutRuns.empty())
    {
        return false;
    }
    *OutCount = 0;
    const SBCodepointSequence ScopedSequence{SBStringEncodingUTF8, InText.data(),
                                             static_cast<SBUInteger>(InText.size())};
    SBAlgorithmRef ScopedAlgorithm = SBAlgorithmCreate(&ScopedSequence);
    if (ScopedAlgorithm == nullptr)
    {
        return false;
    }
    const SBLevel ScopedBaseLevel =
        InBaseDirection == TextDirection::RightToLeft ? 1 : 0;
    SBParagraphRef ScopedParagraph =
        SBAlgorithmCreateParagraph(ScopedAlgorithm, 0, static_cast<SBUInteger>(InText.size()), ScopedBaseLevel);
    if (ScopedParagraph == nullptr)
    {
        SBAlgorithmRelease(ScopedAlgorithm);
        return false;
    }
    SBLineRef ScopedLine = SBParagraphCreateLine(ScopedParagraph, 0, static_cast<SBUInteger>(InText.size()));
    if (ScopedLine == nullptr)
    {
        SBParagraphRelease(ScopedParagraph);
        SBAlgorithmRelease(ScopedAlgorithm);
        return false;
    }
    const SBUInteger ScopedRunCount = SBLineGetRunCount(ScopedLine);
    const SBRun* ScopedRuns = SBLineGetRunsPtr(ScopedLine);
    bool ScopedOk = true;
    for (SBUInteger ScopedIndex = 0; ScopedIndex < ScopedRunCount && ScopedOk; ++ScopedIndex)
    {
        const TextDirection ScopedDirection =
            (ScopedRuns[ScopedIndex].level & 1u) != 0 ? TextDirection::RightToLeft : TextDirection::LeftToRight;
        ScopedOk = AppendScriptRuns(InText, static_cast<std::uint32_t>(ScopedRuns[ScopedIndex].offset),
                                    static_cast<std::uint32_t>(ScopedRuns[ScopedIndex].offset +
                                                               ScopedRuns[ScopedIndex].length),
                                    ScopedDirection, ScopedRuns[ScopedIndex].level, OutRuns, OutCount);
    }
    SBLineRelease(ScopedLine);
    SBParagraphRelease(ScopedParagraph);
    SBAlgorithmRelease(ScopedAlgorithm);
    return ScopedOk;
}
#else
bool SplitRtlRuns(std::string_view InText, TextDirection InBaseDirection, std::span<TextRun> OutRuns,
                  std::uint32_t* OutCount) noexcept
{
    if (OutCount == nullptr || OutRuns.empty())
    {
        return false;
    }
    *OutCount = 0;
    std::size_t ScopedCursor = 0;
    std::size_t ScopedRunBegin = 0;
    bool ScopedRunRtl = false;
    bool ScopedRunOpen = false;
    TextScript ScopedRunScript = TextScript::Unknown;
    const auto ScopedFlush = [&](std::size_t InEnd)
    {
        if (!ScopedRunOpen)
        {
            return true;
        }
        if (*OutCount >= OutRuns.size())
        {
            return false;
        }
        TextRun& ScopedRun = OutRuns[(*OutCount)++];
        ScopedRun.Begin = static_cast<std::uint32_t>(ScopedRunBegin);
        ScopedRun.End = static_cast<std::uint32_t>(InEnd);
        ScopedRun.Script = ScopedRunScript;
        ScopedRun.Direction = ScopedRunRtl ? TextDirection::RightToLeft : TextDirection::LeftToRight;
        ScopedRun.BidiLevel = ScopedRunRtl ? 1u : 0u;
        return true;
    };

    while (ScopedCursor < InText.size())
    {
        const std::size_t ScopedBefore = ScopedCursor;
        const std::uint32_t ScopedCodepoint = DecodeTextUtf8(InText, ScopedCursor);
        const bool ScopedRtl = IsRtlCodepoint(ScopedCodepoint);
        const TextScript ScopedScript = ScriptForCodepoint(ScopedCodepoint);
        const bool ScopedChanged = !ScopedRunOpen || ScopedRtl != ScopedRunRtl ||
                                   (ScopedScript != TextScript::Unknown && ScopedScript != ScopedRunScript);
        if (ScopedChanged)
        {
            if (!ScopedFlush(ScopedBefore))
            {
                return false;
            }
            ScopedRunBegin = ScopedBefore;
            ScopedRunRtl = ScopedRtl;
            ScopedRunScript = ScopedScript;
            ScopedRunOpen = true;
        }
    }
    if (!ScopedFlush(InText.size()))
    {
        return false;
    }
    // The fallback splitter works logically. Relevel for the paragraph
    // direction and reorder with UAX 9 rule L2, so both backends return runs in
    // visual order and a caller never has to know which one produced them.
    if (InBaseDirection == TextDirection::RightToLeft)
    {
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < *OutCount; ++ScopedIndex)
        {
            const bool ScopedRtl = OutRuns[ScopedIndex].Direction == TextDirection::RightToLeft;
            OutRuns[ScopedIndex].BidiLevel = ScopedRtl ? std::uint8_t{1} : std::uint8_t{2};
        }
    }
    ReorderRunsVisual(OutRuns, *OutCount);
    return true;
}
#endif
}
