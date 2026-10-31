// FemboyEngine2D - editor visual language, shared by every panel.
#pragma once

#include <imgui.h>

#include <cmath>
#include <cstdint>

namespace Fby::Editor
{

inline constexpr std::uint32_t kThemeBackground = 0x101012;
inline constexpr std::uint32_t kThemePopup = 0x141416;
inline constexpr std::uint32_t kThemeBorder = 0x222226;
inline constexpr std::uint32_t kThemeFrame = 0x18181C;
inline constexpr std::uint32_t kThemeFrameHover = 0x22222A;
inline constexpr std::uint32_t kThemeFrameActive = 0x2C2C36;
inline constexpr std::uint32_t kThemeHeader = 0x1C1C22;
inline constexpr std::uint32_t kThemeHeaderHover = 0x262630;
inline constexpr std::uint32_t kThemeHeaderActive = 0x30303C;
inline constexpr std::uint32_t kThemeText = 0xEEEEEE;
inline constexpr std::uint32_t kThemeTextDisabled = 0x666666;
inline constexpr std::uint32_t kThemeAccent = 0xD05280;
inline constexpr std::uint32_t kThemeAccentActive = 0xE06290;
inline constexpr std::uint32_t kThemeMono = 0x8A8A93;

[[nodiscard]] inline float SrgbToLinear(float InChannel) noexcept
{
    return InChannel <= 0.04045f ? InChannel / 12.92f : std::pow((InChannel + 0.055f) / 1.055f, 2.4f);
}

// Authored hex -> linear ImGui colour. Keep every editor colour behind this.
[[nodiscard]] inline ImVec4 HexColor(std::uint32_t InRgb, float InAlpha = 1.0f) noexcept
{
    const auto ScopedChannel = [InRgb](std::uint32_t InShift)
    {
        const float ScopedSrgb = static_cast<float>((InRgb >> InShift) & 0xFFu) / 255.0f;
        return SrgbToLinear(ScopedSrgb);
    };
    return ImVec4(ScopedChannel(16), ScopedChannel(8), ScopedChannel(0), InAlpha);
}

// Small helper for status text: accent when a live animation is running,
[[nodiscard]] inline ImVec4 StatusColor(bool InActive) noexcept
{
    return HexColor(InActive ? kThemeAccent : kThemeTextDisabled);
}
}
