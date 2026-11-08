// FemboyEngine2D - Unicode boundary queries backed by libunibreak.
#include "Engine/Text/UnicodeBreak.hpp"

#include <algorithm>
#include <array>
#include <mutex>

extern "C"
{
#include "graphemebreak.h"
#include "linebreak.h"
#include "wordbreak.h"
}

namespace Fby::Text
{
namespace
{
void EnsureInitialized() noexcept
{
    static std::once_flag ScopedFlag;
    std::call_once(ScopedFlag,
                   []()
                   {
                       init_linebreak();
                       init_wordbreak();
                       init_graphemebreak();
                   });
}

[[nodiscard]] std::uint32_t Utf8CodepointLength(std::string_view InText, std::size_t InOffset) noexcept
{
    if (InOffset >= InText.size())
    {
        return 0;
    }
    const auto ScopedFirst = static_cast<unsigned char>(InText[InOffset]);
    if ((ScopedFirst & 0x80u) == 0)
    {
        return 1;
    }
    if ((ScopedFirst & 0xE0u) == 0xC0u)
    {
        return 2;
    }
    if ((ScopedFirst & 0xF0u) == 0xE0u)
    {
        return 3;
    }
    if ((ScopedFirst & 0xF8u) == 0xF0u)
    {
        return 4;
    }
    return 1;
}

[[nodiscard]] bool Prepare(std::string_view InText, std::span<UnicodeBoundary> OutBreaks) noexcept
{
    if (OutBreaks.size() < InText.size())
    {
        return false;
    }
    std::fill(OutBreaks.begin(), OutBreaks.end(), UnicodeBoundary::None);
    return true;
}

[[nodiscard]] std::size_t CountCodepoints(std::string_view InText) noexcept
{
    std::size_t ScopedCount = 0;
    for (std::size_t ScopedOffset = 0; ScopedOffset < InText.size();)
    {
        ScopedOffset += Utf8CodepointLength(InText, ScopedOffset);
        ++ScopedCount;
    }
    return ScopedCount;
}
}

bool ComputeLineBreaks(std::string_view InText, std::span<UnicodeBoundary> OutBreaks) noexcept
{
    if (!Prepare(InText, OutBreaks))
    {
        return false;
    }
    if (InText.empty())
    {
        return true;
    }
    EnsureInitialized();
    const std::size_t ScopedCount = CountCodepoints(InText);
    if (ScopedCount > kMaxUnicodeBreakCodepoints)
    {
        return false;
    }
    std::array<char, kMaxUnicodeBreakCodepoints> ScopedValues{};
    const std::size_t ScopedWritten = set_linebreaks_utf8_per_code_point(
        reinterpret_cast<const utf8_t*>(InText.data()), InText.size(), "en", ScopedValues.data());
    if (ScopedWritten != ScopedCount)
    {
        return false;
    }
    std::size_t ScopedPointIndex = 0;
    for (std::size_t ScopedOffset = 0; ScopedOffset < InText.size();)
    {
        if (ScopedPointIndex < ScopedWritten && ScopedOffset < OutBreaks.size())
        {
            const char ScopedValue = ScopedValues[ScopedPointIndex];
            OutBreaks[ScopedOffset] = (ScopedValue == LINEBREAK_MUSTBREAK || ScopedValue == LINEBREAK_INDETERMINATE)
                                          ? UnicodeBoundary::Mandatory
                                          : (ScopedValue == LINEBREAK_ALLOWBREAK ? UnicodeBoundary::Allowed
                                                                                 : UnicodeBoundary::None);
        }
        ScopedOffset += Utf8CodepointLength(InText, ScopedOffset);
        ++ScopedPointIndex;
    }
    return true;
}

bool ComputeWordBreaks(std::string_view InText, std::span<UnicodeBoundary> OutBreaks) noexcept
{
    struct WordCall
    {
        static std::size_t Run(std::string_view InValue, char* OutValues) noexcept
        {
            set_wordbreaks_utf8(reinterpret_cast<const utf8_t*>(InValue.data()), InValue.size(), "en", OutValues);
            return InValue.size();
        }
        static UnicodeBoundary Boundary(char InValue) noexcept
        {
            return InValue == WORDBREAK_BREAK ? UnicodeBoundary::Allowed : UnicodeBoundary::None;
        }
    };
    if (!Prepare(InText, OutBreaks))
    {
        return false;
    }
    if (InText.empty())
    {
        return true;
    }
    EnsureInitialized();
    std::array<char, kMaxUnicodeBreakCodepoints * 4> ScopedValues{};
    if (InText.size() > ScopedValues.size())
    {
        return false;
    }
    (void)WordCall::Run(InText, ScopedValues.data());
    for (std::size_t ScopedOffset = 1; ScopedOffset < InText.size(); ++ScopedOffset)
    {
        OutBreaks[ScopedOffset] = WordCall::Boundary(ScopedValues[ScopedOffset]);
    }
    return true;
}

bool ComputeGraphemeBreaks(std::string_view InText, std::span<UnicodeBoundary> OutBreaks) noexcept
{
    struct GraphemeCall
    {
        static void Run(std::string_view InValue, char* OutValues) noexcept
        {
            set_graphemebreaks_utf8(reinterpret_cast<const utf8_t*>(InValue.data()), InValue.size(), "en", OutValues);
        }
        static UnicodeBoundary Boundary(char InValue) noexcept
        {
            return InValue == GRAPHEMEBREAK_BREAK ? UnicodeBoundary::Allowed : UnicodeBoundary::None;
        }
    };
    if (!Prepare(InText, OutBreaks))
    {
        return false;
    }
    if (InText.empty())
    {
        return true;
    }
    EnsureInitialized();
    std::array<char, kMaxUnicodeBreakCodepoints * 4> ScopedValues{};
    if (InText.size() > ScopedValues.size())
    {
        return false;
    }
    GraphemeCall::Run(InText, ScopedValues.data());
    for (std::size_t ScopedOffset = 1; ScopedOffset < InText.size(); ++ScopedOffset)
    {
        OutBreaks[ScopedOffset] = GraphemeCall::Boundary(ScopedValues[ScopedOffset]);
    }
    return true;
}

std::size_t NextBoundary(std::span<const UnicodeBoundary> InBreaks, std::size_t InOffset) noexcept
{
    for (std::size_t ScopedIndex = InOffset + 1; ScopedIndex < InBreaks.size(); ++ScopedIndex)
    {
        if (InBreaks[ScopedIndex] != UnicodeBoundary::None)
        {
            return ScopedIndex;
        }
    }
    return InBreaks.size();
}

std::size_t PreviousBoundary(std::span<const UnicodeBoundary> InBreaks, std::size_t InOffset) noexcept
{
    const std::size_t ScopedLimit = std::min(InOffset, InBreaks.size());
    for (std::size_t ScopedIndex = ScopedLimit; ScopedIndex > 0; --ScopedIndex)
    {
        if (InBreaks[ScopedIndex - 1] != UnicodeBoundary::None)
        {
            return ScopedIndex - 1;
        }
    }
    return 0;
}

}
