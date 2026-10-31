// FemboyEngine2D - compile-time-style rich text markup.
#pragma once

#include "Engine/Text/TextStyle.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace Fby::Text
{

enum class TextMarkupStatus : std::uint8_t
{
    Ok = 0,
    InvalidMarkup,
    UnsupportedTag,
    TooManySpans,
    TooManyStyles,
    TooManyActions,
    TooManyInlines,
    TextTooLong,
};

// A fixed-buffer rich text block. Tags are removed from Text; Spans refer to the
// plain UTF-8 bytes and Styles is the span style table.
struct TextStyledBlock
{
    static constexpr std::uint32_t kMaxTextBytes = 65536;
    static constexpr std::uint32_t kMaxSpans = 1024;
    static constexpr std::uint32_t kMaxStyles = 64;
    static constexpr std::uint32_t kMaxActions = 256;
    static constexpr std::uint32_t kMaxInlines = 64;
    static constexpr std::uint32_t kMaxNesting = 16;

    std::array<char, kMaxTextBytes> Text{};
    std::array<TextSpan, kMaxSpans> Spans{};
    std::array<TextStyle, kMaxStyles> Styles{};
    std::array<TextAction, kMaxActions> Actions{};
    std::array<TextInlineObject, kMaxInlines> Inlines{};
    std::uint32_t TextLength = 0;
    std::uint32_t SpanCount = 0;
    std::uint32_t StyleCount = 0;
    std::uint32_t ActionCount = 0;
    std::uint32_t InlineCount = 0;

    [[nodiscard]] std::string_view View() const noexcept
    {
        return std::string_view(Text.data(), TextLength);
    }

    [[nodiscard]] const TextAction* ActionAt(std::uint32_t InByteOffset) const noexcept
    {
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < ActionCount; ++ScopedIndex)
        {
            if (InByteOffset >= Actions[ScopedIndex].Begin && InByteOffset < Actions[ScopedIndex].End)
            {
                return &Actions[ScopedIndex];
            }
        }
        return nullptr;
    }
};

namespace Detail
{

struct MarkupStackEntry
{
    std::uint32_t StyleIndex = 0;
    std::uint32_t ActionBegin = 0;
    std::uint32_t ActionId = 0;
    TextActionKind ActionKind = TextActionKind::None;
    bool ActionActive = false;
    bool ActionStarted = false;
};

[[nodiscard]] inline bool ParseMarkupColor(std::string_view InValue, Color32* OutColor) noexcept
{
    if (OutColor == nullptr || InValue.empty() || InValue[0] != '#' ||
        (InValue.size() != 7 && InValue.size() != 9))
    {
        return false;
    }
    std::uint32_t ScopedChannels[4] = {0, 0, 0, 255};
    const std::size_t ScopedCount = (InValue.size() - 1) / 2;
    for (std::size_t ScopedChannel = 0; ScopedChannel < ScopedCount; ++ScopedChannel)
    {
        std::uint32_t ScopedValue = 0;
        for (std::size_t ScopedDigit = 0; ScopedDigit < 2; ++ScopedDigit)
        {
            const char ScopedCharacter = InValue[1 + ScopedChannel * 2 + ScopedDigit];
            std::uint32_t ScopedNibble = 0;
            if (ScopedCharacter >= '0' && ScopedCharacter <= '9')
            {
                ScopedNibble = static_cast<std::uint32_t>(ScopedCharacter - '0');
            }
            else if (ScopedCharacter >= 'a' && ScopedCharacter <= 'f')
            {
                ScopedNibble = static_cast<std::uint32_t>(ScopedCharacter - 'a' + 10);
            }
            else if (ScopedCharacter >= 'A' && ScopedCharacter <= 'F')
            {
                ScopedNibble = static_cast<std::uint32_t>(ScopedCharacter - 'A' + 10);
            }
            else
            {
                return false;
            }
            ScopedValue = ScopedValue * 16 + ScopedNibble;
        }
        ScopedChannels[ScopedChannel] = ScopedValue;
    }
    *OutColor = Color32::FromBytes(static_cast<std::uint8_t>(ScopedChannels[0]),
                                   static_cast<std::uint8_t>(ScopedChannels[1]),
                                   static_cast<std::uint8_t>(ScopedChannels[2]),
                                   static_cast<std::uint8_t>(ScopedChannels[3]));
    return true;
}
}

[[nodiscard]] inline TextMarkupStatus FilterMarkup(std::string_view InMarkup, std::uint64_t InConditions,
                                                   std::span<char> OutBuffer,
                                                   std::size_t* OutSize) noexcept
{
    std::array<bool, TextStyledBlock::kMaxNesting> ScopedSkipStack{};
    std::uint32_t ScopedDepth = 0;
    std::uint32_t ScopedSkipping = 0;
    std::size_t ScopedCursor = 0;
    std::size_t ScopedWritten = 0;
    while (ScopedCursor < InMarkup.size())
    {
        if (InMarkup[ScopedCursor] != '[')
        {
            if (ScopedSkipping == 0)
            {
                if (ScopedWritten >= OutBuffer.size())
                {
                    return TextMarkupStatus::TextTooLong;
                }
                OutBuffer[ScopedWritten++] = InMarkup[ScopedCursor];
            }
            ++ScopedCursor;
            continue;
        }
        const std::size_t ScopedClose = InMarkup.find(']', ScopedCursor + 1);
        if (ScopedClose == std::string_view::npos || ScopedClose == ScopedCursor + 1)
        {
            return TextMarkupStatus::InvalidMarkup;
        }
        const std::size_t ScopedTagBegin = ScopedCursor;
        const std::string_view ScopedTag = InMarkup.substr(ScopedCursor + 1, ScopedClose - ScopedCursor - 1);
        ScopedCursor = ScopedClose + 1;
        if (ScopedTag == "/if")
        {
            if (ScopedDepth == 0)
            {
                return TextMarkupStatus::InvalidMarkup;
            }
            --ScopedDepth;
            if (ScopedSkipStack[ScopedDepth])
            {
                --ScopedSkipping;
            }
            continue;
        }
        const bool ScopedIf = ScopedTag.starts_with("if=");
        const bool ScopedIfNot = ScopedTag.starts_with("ifnot=");
        if (ScopedIf || ScopedIfNot)
        {
            if (ScopedDepth >= TextStyledBlock::kMaxNesting)
            {
                return TextMarkupStatus::InvalidMarkup;
            }
            const std::string_view ScopedValue =
                ScopedTag.substr(ScopedIf ? 3 : 6);
            if (ScopedValue.empty())
            {
                return TextMarkupStatus::InvalidMarkup;
            }
            std::uint32_t ScopedBit = 0;
            for (const char ScopedCharacter : ScopedValue)
            {
                if (ScopedCharacter < '0' || ScopedCharacter > '9')
                {
                    return TextMarkupStatus::InvalidMarkup;
                }
                ScopedBit = ScopedBit * 10 + static_cast<std::uint32_t>(ScopedCharacter - '0');
                if (ScopedBit >= 64)
                {
                    return TextMarkupStatus::InvalidMarkup;
                }
            }
            const bool ScopedSet = (InConditions & (1ull << ScopedBit)) != 0;
            const bool ScopedSkip = ScopedSkipping != 0 || (ScopedIf ? !ScopedSet : ScopedSet);
            ScopedSkipStack[ScopedDepth++] = ScopedSkip;
            if (ScopedSkip)
            {
                ++ScopedSkipping;
            }
            continue;
        }
        if (ScopedSkipping == 0)
        {
            const std::size_t ScopedRawTagSize = ScopedClose - ScopedTagBegin + 1;
            if (ScopedWritten + ScopedRawTagSize > OutBuffer.size())
            {
                return TextMarkupStatus::TextTooLong;
            }
            for (std::size_t ScopedIndex = ScopedTagBegin; ScopedIndex <= ScopedClose; ++ScopedIndex)
            {
                OutBuffer[ScopedWritten++] = InMarkup[ScopedIndex];
            }
        }
    }
    if (ScopedDepth != 0)
    {
        return TextMarkupStatus::InvalidMarkup;
    }
    *OutSize = ScopedWritten;
    return TextMarkupStatus::Ok;
}

[[nodiscard]] inline TextMarkupStatus CompileTextMarkup(std::string_view InMarkup,
                                                        TextStyledBlock* OutBlock,
                                                        std::uint64_t InConditions = ~0ull) noexcept
{
    if (OutBlock == nullptr)
    {
        return TextMarkupStatus::InvalidMarkup;
    }
    std::array<char, TextStyledBlock::kMaxTextBytes> ScopedFiltered{};
    std::size_t ScopedFilteredSize = 0;
    const TextMarkupStatus ScopedFilterStatus =
        FilterMarkup(InMarkup, InConditions, std::span<char>(ScopedFiltered), &ScopedFilteredSize);
    if (ScopedFilterStatus != TextMarkupStatus::Ok)
    {
        return ScopedFilterStatus;
    }
    InMarkup = std::string_view(ScopedFiltered.data(), ScopedFilteredSize);
    OutBlock->TextLength = 0;
    OutBlock->SpanCount = 0;
    OutBlock->StyleCount = 0;
    OutBlock->ActionCount = 0;
    OutBlock->InlineCount = 0;

    TextStyle ScopedBase;
    OutBlock->Styles[OutBlock->StyleCount++] = ScopedBase;
    std::uint32_t ScopedCurrentStyle = 0;
    std::uint32_t ScopedActionBegin = 0;
    std::uint32_t ScopedActionId = 0;
    TextActionKind ScopedActionKind = TextActionKind::None;
    bool ScopedActionActive = false;
    bool ScopedActionStarted = false;
    std::array<Detail::MarkupStackEntry, TextStyledBlock::kMaxNesting> ScopedStack{};
    std::uint32_t ScopedDepth = 0;
    std::uint32_t ScopedRunBegin = 0;
    bool ScopedRunOpen = false;

    const auto ScopedFlush = [OutBlock, &ScopedRunBegin, &ScopedRunOpen, &ScopedCurrentStyle]() noexcept
    {
        if (!ScopedRunOpen || OutBlock->TextLength <= ScopedRunBegin)
        {
            ScopedRunOpen = false;
            ScopedRunBegin = OutBlock->TextLength;
            return true;
        }
        if (OutBlock->SpanCount >= TextStyledBlock::kMaxSpans)
        {
            return false;
        }
        OutBlock->Spans[OutBlock->SpanCount++] = TextSpan{ScopedRunBegin, OutBlock->TextLength,
                                                          static_cast<std::uint16_t>(ScopedCurrentStyle), 0};
        ScopedRunBegin = OutBlock->TextLength;
        return true;
    };

    const auto ScopedAddStyle = [OutBlock](const TextStyle& InStyle, std::uint32_t* OutIndex) noexcept
    {
        if (OutBlock->StyleCount >= TextStyledBlock::kMaxStyles)
        {
            return false;
        }
        *OutIndex = OutBlock->StyleCount;
        OutBlock->Styles[OutBlock->StyleCount++] = InStyle;
        return true;
    };

    const auto ScopedAddAction = [OutBlock](std::uint32_t InBegin, std::uint32_t InEnd, std::uint32_t InId,
                                            TextActionKind InKind) noexcept
    {
        if (OutBlock->ActionCount >= TextStyledBlock::kMaxActions)
        {
            return false;
        }
        OutBlock->Actions[OutBlock->ActionCount++] = TextAction{InBegin, InEnd, InId, InKind, 0};
        return true;
    };

    std::size_t ScopedCursor = 0;
    while (ScopedCursor < InMarkup.size())
    {
        if (InMarkup[ScopedCursor] != '[')
        {
            if (!ScopedRunOpen)
            {
                ScopedRunBegin = OutBlock->TextLength;
                ScopedRunOpen = true;
            }
            if (OutBlock->TextLength >= TextStyledBlock::kMaxTextBytes)
            {
                return TextMarkupStatus::TextTooLong;
            }
            OutBlock->Text[OutBlock->TextLength++] = InMarkup[ScopedCursor++];
            continue;
        }
        const std::size_t ScopedClose = InMarkup.find(']', ScopedCursor + 1);
        if (ScopedClose == std::string_view::npos || ScopedClose == ScopedCursor + 1)
        {
            return TextMarkupStatus::InvalidMarkup;
        }
        const std::string_view ScopedTag = InMarkup.substr(ScopedCursor + 1, ScopedClose - ScopedCursor - 1);
        ScopedCursor = ScopedClose + 1;
        if (!ScopedTag.empty() && ScopedTag.front() == '/')
        {
            if (!ScopedFlush())
            {
                return TextMarkupStatus::TooManySpans;
            }
            if (ScopedDepth == 0)
            {
                return TextMarkupStatus::InvalidMarkup;
            }
            if (ScopedActionActive && ScopedActionStarted && OutBlock->TextLength > ScopedActionBegin)
            {
                if (!ScopedAddAction(ScopedActionBegin, OutBlock->TextLength, ScopedActionId, ScopedActionKind))
                {
                    return TextMarkupStatus::TooManyActions;
                }
            }
            --ScopedDepth;
            ScopedCurrentStyle = ScopedStack[ScopedDepth].StyleIndex;
            ScopedActionBegin = ScopedStack[ScopedDepth].ActionBegin;
            ScopedActionId = ScopedStack[ScopedDepth].ActionId;
            ScopedActionKind = ScopedStack[ScopedDepth].ActionKind;
            ScopedActionActive = ScopedStack[ScopedDepth].ActionActive;
            ScopedActionStarted = ScopedStack[ScopedDepth].ActionStarted;
            ScopedRunOpen = false;
            continue;
        }

        if (ScopedDepth >= TextStyledBlock::kMaxNesting)
        {
            return TextMarkupStatus::InvalidMarkup;
        }
        if (!ScopedFlush())
        {
            return TextMarkupStatus::TooManySpans;
        }
        const std::size_t ScopedEquals = ScopedTag.find('=');
        const std::string_view ScopedName = ScopedTag.substr(0, ScopedEquals);
        const std::string_view ScopedValue =
            ScopedEquals == std::string_view::npos ? std::string_view{} : ScopedTag.substr(ScopedEquals + 1);
        if (ScopedName.empty())
        {
            return TextMarkupStatus::UnsupportedTag;
        }
        const bool ScopedIsIcon = ScopedName == "icon";
        const bool ScopedIsBorder = ScopedName == "border";
        const bool ScopedIsProgress = ScopedName == "progress";
        const bool ScopedIsSeparator = ScopedName == "separator";
        const bool ScopedIsButton = ScopedName == "button";
        if (ScopedIsIcon || ScopedIsBorder || ScopedIsProgress || ScopedIsSeparator || ScopedIsButton)
        {
            const std::uint32_t ScopedExpectedParts =
                (ScopedIsIcon || ScopedIsProgress || ScopedIsButton) ? 3u : 2u;
            std::uint32_t ScopedValues[3] = {0, 0, 0};
            std::uint32_t ScopedPart = 0;
            bool ScopedHasDigit = false;
            for (const char ScopedCharacter : ScopedValue)
            {
                if (ScopedCharacter == ':')
                {
                    if (!ScopedHasDigit || ScopedPart + 1 >= ScopedExpectedParts)
                    {
                        return TextMarkupStatus::InvalidMarkup;
                    }
                    ++ScopedPart;
                    ScopedHasDigit = false;
                    continue;
                }
                if (ScopedCharacter < '0' || ScopedCharacter > '9')
                {
                    return TextMarkupStatus::InvalidMarkup;
                }
                ScopedValues[ScopedPart] = ScopedValues[ScopedPart] * 10 +
                                           static_cast<std::uint32_t>(ScopedCharacter - '0');
                ScopedHasDigit = true;
            }
            if (!ScopedHasDigit || ScopedPart + 1 != ScopedExpectedParts)
            {
                return TextMarkupStatus::InvalidMarkup;
            }
            TextInlineKind ScopedKind = TextInlineKind::Sprite;
            std::uint32_t ScopedId = 0;
            std::uint32_t ScopedWidth = 0;
            std::uint32_t ScopedHeight = 0;
            float ScopedProgress = 0.0f;
            if (ScopedIsIcon)
            {
                ScopedId = ScopedValues[0];
                ScopedWidth = ScopedValues[1];
                ScopedHeight = ScopedValues[2];
            }
            else if (ScopedIsButton)
            {
                ScopedKind = TextInlineKind::Button;
                ScopedId = ScopedValues[0];
                ScopedWidth = ScopedValues[1];
                ScopedHeight = ScopedValues[2];
            }
            else if (ScopedIsProgress)
            {
                ScopedKind = TextInlineKind::Progress;
                ScopedProgress = static_cast<float>(std::min(ScopedValues[0], 100u)) / 100.0f;
                ScopedWidth = ScopedValues[1];
                ScopedHeight = ScopedValues[2];
            }
            else if (ScopedIsBorder)
            {
                ScopedKind = TextInlineKind::Border;
                ScopedWidth = ScopedValues[0];
                ScopedHeight = ScopedValues[1];
            }
            else
            {
                ScopedKind = TextInlineKind::Separator;
                ScopedWidth = ScopedValues[0];
                ScopedHeight = ScopedValues[1];
            }
            if (ScopedWidth == 0 || ScopedHeight == 0)
            {
                return TextMarkupStatus::InvalidMarkup;
            }
            if (OutBlock->InlineCount >= TextStyledBlock::kMaxInlines)
            {
                return TextMarkupStatus::TooManyInlines;
            }
            if (OutBlock->TextLength + 3 > TextStyledBlock::kMaxTextBytes)
            {
                return TextMarkupStatus::TextTooLong;
            }
            if (!ScopedRunOpen)
            {
                ScopedRunBegin = OutBlock->TextLength;
                ScopedRunOpen = true;
            }
            OutBlock->Inlines[OutBlock->InlineCount++] =
                TextInlineObject{OutBlock->TextLength, static_cast<float>(ScopedWidth),
                                 static_cast<float>(ScopedHeight), ScopedId, ScopedKind, ScopedProgress, 0};
            OutBlock->Text[OutBlock->TextLength++] = static_cast<char>(0xEF);
            OutBlock->Text[OutBlock->TextLength++] = static_cast<char>(0xBF);
            OutBlock->Text[OutBlock->TextLength++] = static_cast<char>(0xBC);
            continue;
        }
        bool ScopedStartsAction = false;
        std::uint32_t ScopedNewActionId = ScopedActionId;
        TextActionKind ScopedNewActionKind = ScopedActionKind;
        TextStyle ScopedNext = OutBlock->Styles[ScopedCurrentStyle];
        if (ScopedName == "b")
        {
            ScopedNext.Flags |= static_cast<std::uint16_t>(TextStyleFlags::Bold);
        }
        else if (ScopedName == "i")
        {
            ScopedNext.Flags |= static_cast<std::uint16_t>(TextStyleFlags::Italic);
        }
        else if (ScopedName == "u")
        {
            ScopedNext.Flags |= static_cast<std::uint16_t>(TextStyleFlags::Underline);
        }
        else if (ScopedName == "s")
        {
            ScopedNext.Flags |= static_cast<std::uint16_t>(TextStyleFlags::Strike);
        }
        else if (ScopedName == "code")
        {
            ScopedNext.Flags |= static_cast<std::uint16_t>(TextStyleFlags::Monospace);
        }
        else if (ScopedName == "color")
        {
            Color32 ScopedColor;
            if (!Detail::ParseMarkupColor(ScopedValue, &ScopedColor))
            {
                return TextMarkupStatus::InvalidMarkup;
            }
            ScopedNext.Color = ScopedColor;
        }
        else if (ScopedName == "link" || ScopedName == "action")
        {
            if (ScopedValue.empty())
            {
                return TextMarkupStatus::InvalidMarkup;
            }
            std::uint32_t ScopedParsed = 0;
            for (const char ScopedCharacter : ScopedValue)
            {
                if (ScopedCharacter < '0' || ScopedCharacter > '9')
                {
                    return TextMarkupStatus::InvalidMarkup;
                }
                ScopedParsed = ScopedParsed * 10 + static_cast<std::uint32_t>(ScopedCharacter - '0');
            }
            ScopedNewActionId = ScopedParsed;
            ScopedNewActionKind = ScopedName == "link" ? TextActionKind::Link : TextActionKind::Command;
            ScopedStartsAction = true;
        }
        else if (ScopedName == "size")
        {
            std::uint32_t ScopedSize = 0;
            if (ScopedValue.empty())
            {
                return TextMarkupStatus::InvalidMarkup;
            }
            for (const char ScopedCharacter : ScopedValue)
            {
                if (ScopedCharacter < '0' || ScopedCharacter > '9')
                {
                    return TextMarkupStatus::InvalidMarkup;
                }
                ScopedSize = ScopedSize * 10 + static_cast<std::uint32_t>(ScopedCharacter - '0');
                if (ScopedSize > 512)
                {
                    return TextMarkupStatus::InvalidMarkup;
                }
            }
            if (ScopedSize == 0)
            {
                return TextMarkupStatus::InvalidMarkup;
            }
            ScopedNext.FontSize = static_cast<std::uint16_t>(ScopedSize);
        }
        else
        {
            return TextMarkupStatus::UnsupportedTag;
        }

        std::uint32_t ScopedNextIndex = 0;
        if (!ScopedAddStyle(ScopedNext, &ScopedNextIndex))
        {
            return TextMarkupStatus::TooManyStyles;
        }
        ScopedStack[ScopedDepth++] = Detail::MarkupStackEntry{ScopedCurrentStyle, ScopedActionBegin, ScopedActionId,
                                                               ScopedActionKind, ScopedActionActive,
                                                               ScopedActionStarted};
        ScopedCurrentStyle = ScopedNextIndex;
        if (ScopedStartsAction)
        {
            ScopedActionBegin = OutBlock->TextLength;
            ScopedActionId = ScopedNewActionId;
            ScopedActionKind = ScopedNewActionKind;
            ScopedActionActive = true;
            ScopedActionStarted = true;
        }
        else
        {
            ScopedActionStarted = false;
        }
        ScopedRunOpen = false;
    }
    if (ScopedDepth != 0)
    {
        return TextMarkupStatus::InvalidMarkup;
    }
    if (!ScopedFlush())
    {
        return TextMarkupStatus::TooManySpans;
    }
    return TextMarkupStatus::Ok;
}
}
