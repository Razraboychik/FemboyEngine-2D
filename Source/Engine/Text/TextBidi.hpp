// FemboyEngine2D - bidi run extraction boundary.
//
// This is deliberately an interface plus a conservative splitter: full UAX #9
// levels are supplied by the SheenBidi/FriBidi backend in the next phase.
#pragma once

#include "Engine/Text/TextShaper.hpp"

#include <cstddef>
#include <span>
#include <string_view>

namespace Fby::Text
{

// Fills runs in visual order: each run carries the script of its own text, a
// level and a direction, and the sequence follows UAX 9 rule L2, so a renderer
// can walk the array from left to right. Returns false when the output span
// cannot hold every run.
[[nodiscard]] bool SplitRtlRuns(std::string_view InText, TextDirection InBaseDirection,
                                std::span<TextRun> OutRuns, std::uint32_t* OutCount = nullptr) noexcept;

[[nodiscard]] bool IsRtlCodepoint(std::uint32_t InCodepoint) noexcept;
[[nodiscard]] TextScript ScriptForCodepoint(std::uint32_t InCodepoint) noexcept;
}
