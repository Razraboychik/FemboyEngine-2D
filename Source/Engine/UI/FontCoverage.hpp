// FemboyEngine2D - font coverage mask and script fallback ranking.
//
// The text stack must pick a face that actually has the glyphs, not one that
// merely matches the script. A coverage mask is one bit per BMP codepoint,
// built once when a face is mounted, and the ranking walks it to order the faces
// of a script from the best cover to the worst.
#pragma once

#include <array>
#include <bit>
#include <cstdint>

namespace Fby::Ui
{

inline constexpr std::uint32_t kCoverageCodepoints = 0x10000u;
inline constexpr std::uint32_t kCoverageWords = kCoverageCodepoints / 64u;

// One bit per BMP codepoint. Supplementary planes are not tracked; a face that
// only covers them is reported as uncovered for those codepoints.
struct FontCoverage
{
    std::array<std::uint64_t, kCoverageWords> Words{};

    void Set(std::uint32_t InCodepoint) noexcept
    {
        if (InCodepoint >= kCoverageCodepoints)
        {
            return;
        }
        Words[InCodepoint / 64u] |= (1ull << (InCodepoint % 64u));
    }
    [[nodiscard]] bool Covers(std::uint32_t InCodepoint) const noexcept
    {
        if (InCodepoint >= kCoverageCodepoints)
        {
            return false;
        }
        return (Words[InCodepoint / 64u] & (1ull << (InCodepoint % 64u))) != 0;
    }
    [[nodiscard]] std::uint32_t CoveredCount() const noexcept
    {
        std::uint32_t ScopedCount = 0;
        for (const std::uint64_t ScopedWord : Words)
        {
            ScopedCount += static_cast<std::uint32_t>(std::popcount(ScopedWord));
        }
        return ScopedCount;
    }
};
}
