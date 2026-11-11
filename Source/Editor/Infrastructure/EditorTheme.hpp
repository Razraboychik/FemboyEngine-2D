// FemboyEngine2D - editor visual language, shared by every panel.
#pragma once

#include <imgui.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <string_view>

namespace Fby::Editor
{

struct EditorSettings;

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

enum class EditorThemeId : std::uint8_t
{
    Graphite = 0,
    VeryDark,
    Midnight,
    Forest,
    Plum,
    Amber,
    Crimson,
};

enum class EditorSymbolRole : std::uint8_t
{
    AxisX = 0,
    AxisY,
    Grid,
    Play,
    Pause,
    Step,
    Stop,
    Rotate,
    Multiplayer,
    Plot0,
    Plot1,
    Plot2,
    Plot3,
};

// Per-symbol overrides. Zero means "use the selected theme's value"; this keeps
// the struct trivially copyable and serialisable without optional fields.
struct EditorSymbolColors
{
    std::uint32_t AxisX = 0;
    std::uint32_t AxisY = 0;
    std::uint32_t Grid = 0;
    std::uint32_t Play = 0;
    std::uint32_t Pause = 0;
    std::uint32_t Step = 0;
    std::uint32_t Stop = 0;
    std::uint32_t Rotate = 0;
    std::uint32_t Multiplayer = 0;
    std::array<std::uint32_t, 4> Plot{};

    [[nodiscard]] friend bool operator==(const EditorSymbolColors&, const EditorSymbolColors&) noexcept = default;
};

struct EditorPalette
{
    std::uint32_t Background = kThemeBackground;
    std::uint32_t Popup = kThemePopup;
    std::uint32_t Border = kThemeBorder;
    std::uint32_t Frame = kThemeFrame;
    std::uint32_t FrameHover = kThemeFrameHover;
    std::uint32_t FrameActive = kThemeFrameActive;
    std::uint32_t Header = kThemeHeader;
    std::uint32_t HeaderHover = kThemeHeaderHover;
    std::uint32_t HeaderActive = kThemeHeaderActive;
    std::uint32_t Text = kThemeText;
    std::uint32_t TextDisabled = kThemeTextDisabled;
    std::uint32_t Accent = kThemeAccent;
    std::uint32_t AccentActive = kThemeAccentActive;
    std::uint32_t Mono = kThemeMono;
    std::uint32_t AxisX = 0xE0A040;
    std::uint32_t AxisY = 0x60A0E0;
    std::uint32_t Grid = 0x788096;
    std::uint32_t Play = 0x50C878;
    std::uint32_t Pause = 0xE0C050;
    std::uint32_t Step = 0x60B0E0;
    std::uint32_t Stop = 0xD05050;
    std::uint32_t Rotate = 0xD080C0;
    std::uint32_t Multiplayer = 0x50C8C8;
    std::array<std::uint32_t, 4> Plot{0x8A8A93, 0x60A0E0, 0xE0A040, 0xC080D0};
};

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

[[nodiscard]] const char* EditorThemeName(EditorThemeId InTheme) noexcept;
[[nodiscard]] EditorThemeId ParseEditorTheme(std::string_view InName) noexcept;

// Palette construction is public for tests and for the settings preview. The
// active palette is the one used by panels, playback icons, axes and plots.
[[nodiscard]] EditorPalette BuildEditorPalette(EditorThemeId InTheme, float InTextContrast, std::uint32_t InTextColor,
                                               float InTextOpacity, bool InCustomSymbols,
                                               const EditorSymbolColors& InColors) noexcept;
[[nodiscard]] const EditorPalette& CurrentEditorPalette() noexcept;
[[nodiscard]] std::uint64_t EditorThemeGeneration() noexcept;

[[nodiscard]] ImVec4 SymbolColor(EditorSymbolRole InRole, float InAlpha = 1.0f) noexcept;
[[nodiscard]] ImU32 SymbolColorU32(EditorSymbolRole InRole, float InAlpha = 1.0f) noexcept;
[[nodiscard]] ImVec4 PlotColor(std::uint32_t InIndex, float InAlpha = 1.0f) noexcept;

// Applies the selected panel theme and the independent text colour, opacity and
// contrast to ImGui. The theme controls panel tints; text and symbols are
// authored separately, so a dark RGB theme does not force a text colour.
void ApplyEditorTheme(const EditorSettings& InSettings) noexcept;

}
