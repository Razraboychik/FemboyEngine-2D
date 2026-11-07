// FemboyEngine2D - Unicode boundary queries backed by libunibreak.
#pragma once

#include <cstdint>
#include <span>
#include <string_view>

namespace Fby::Text
{

inline constexpr std::uint32_t kMaxUnicodeBreakCodepoints = 4096;

enum class UnicodeBoundary : std::uint8_t
{
    None = 0,
    Allowed = 1,
    Mandatory = 2,
};

// The outputs are indexed by byte offset in the UTF-8 input. A boundary marks
// the byte immediately after the code point it belongs to.
[[nodiscard]] bool ComputeLineBreaks(std::string_view InText, std::span<UnicodeBoundary> OutBreaks) noexcept;
[[nodiscard]] bool ComputeWordBreaks(std::string_view InText, std::span<UnicodeBoundary> OutBreaks) noexcept;
[[nodiscard]] bool ComputeGraphemeBreaks(std::string_view InText, std::span<UnicodeBoundary> OutBreaks) noexcept;

// Returns the byte offset of the next boundary at or after InOffset, or the
// text size when no boundary remains. Useful for cursor and double-click code.
[[nodiscard]] std::size_t NextBoundary(std::span<const UnicodeBoundary> InBreaks, std::size_t InOffset) noexcept;
[[nodiscard]] std::size_t PreviousBoundary(std::span<const UnicodeBoundary> InBreaks, std::size_t InOffset) noexcept;

}
