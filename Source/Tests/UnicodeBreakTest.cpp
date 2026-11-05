// FemboyEngine2D - Unicode line, word and grapheme boundary queries.
#include "Engine/Text/UnicodeBreak.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <string_view>

using namespace Fby::Text;

namespace
{
void Check(bool InCondition, const char* InMessage)
{
    if (!InCondition)
    {
        std::fprintf(stderr, "FAIL: %s\n", InMessage);
        std::exit(1);
    }
}
}

int main()
{
    constexpr std::string_view ScopedCjk = "你好世界";
    std::array<UnicodeBoundary, 64> ScopedBreaks{};
    Check(ComputeLineBreaks(ScopedCjk, std::span<UnicodeBoundary>(ScopedBreaks.data(), ScopedCjk.size())),
          "compute CJK line breaks");
    std::uint32_t ScopedAllowed = 0;
    for (std::size_t ScopedOffset = 1; ScopedOffset < ScopedCjk.size(); ++ScopedOffset)
    {
        if (ScopedBreaks[ScopedOffset] != UnicodeBoundary::None)
        {
            ++ScopedAllowed;
        }
    }
    Check(ScopedAllowed >= 3, "CJK allows breaks between ideographs");
    Check(NextBoundary(std::span<const UnicodeBoundary>(ScopedBreaks.data(), ScopedCjk.size()), 0) <= ScopedCjk.size(),
          "next boundary stays inside the text");

    constexpr std::string_view ScopedWords = "hello world";
    std::array<UnicodeBoundary, 64> ScopedWordBreaks{};
    Check(ComputeWordBreaks(ScopedWords, std::span<UnicodeBoundary>(ScopedWordBreaks.data(), ScopedWords.size())),
          "compute word breaks");
    Check(ScopedWordBreaks[5] != UnicodeBoundary::None || ScopedWordBreaks[6] != UnicodeBoundary::None,
          "word boundary lands around the separator");

    constexpr std::string_view ScopedEmoji = "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9";
    std::array<UnicodeBoundary, 64> ScopedGraphemeBreaks{};
    Check(ComputeGraphemeBreaks(ScopedEmoji,
                                std::span<UnicodeBoundary>(ScopedGraphemeBreaks.data(), ScopedEmoji.size())),
          "compute grapheme breaks");
    Check(PreviousBoundary(std::span<const UnicodeBoundary>(ScopedGraphemeBreaks.data(), ScopedEmoji.size()),
                           ScopedEmoji.size()) <= ScopedEmoji.size(),
          "previous grapheme boundary is valid");

    std::printf("unicode_break: ok\n");
    return 0;
}
