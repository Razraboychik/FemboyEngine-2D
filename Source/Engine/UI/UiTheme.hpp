// FemboyEngine2D - Pink UI theme and global palette.
#pragma once

#include "Engine/Core/Types.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>

namespace Fby::Ui
{

inline constexpr std::uint16_t kPaletteInherit = 0xFFFFu;
inline constexpr std::uint32_t kUiPaletteSlots = 18;

enum class UiPaletteSlot : std::uint16_t
{
    Primary = 0,
    Active = 1,
    Accent = 2,
    Text = 3,
    TextMuted = 4,
    Background = 5,
    Surface = 6,
    Border = 7,
    Warning = 8,
    Critical = 9,
    Success = 10,
    Shadow = 11,
    Highlight = 12,
    Disabled = 13,
    Focus = 14,
    Overlay = 15,
    // Optional per-state overrides. An alpha of zero falls back to tint scaling.
    Hover = 16,
    Pressed = 17,
};

struct UiPalette
{
    std::array<Color32, kUiPaletteSlots> colors{};
};

struct UiThemeAsset
{
    char name[48] = "Graphite";
    std::uint32_t version = 1;
    std::uint32_t flags = 0;
    UiPalette palette{};

    [[nodiscard]] Color32 Resolve(std::uint16_t InSlot, Color32 InFallback = Color32::White()) const noexcept
    {
        if (InSlot >= kUiPaletteSlots)
        {
            return InFallback;
        }
        const Color32 ScopedColor = palette.colors[InSlot];
        return ScopedColor.A() == 0 && InFallback.A() != 0 ? InFallback : ScopedColor;
    }
    [[nodiscard]] Color32 Resolve(UiPaletteSlot InSlot, Color32 InFallback = Color32::White()) const noexcept
    {
        return Resolve(static_cast<std::uint16_t>(InSlot), InFallback);
    }
};

static_assert(std::is_trivially_copyable_v<UiThemeAsset>);

[[nodiscard]] UiThemeAsset DefaultUiTheme() noexcept;
// Load-time import for tools. The shipping runtime copies a UiThemeAsset from a
[[nodiscard]] bool LoadUiThemeToml(std::string_view InText, UiThemeAsset& OutTheme, std::string* OutError = nullptr);
}
