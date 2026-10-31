// FemboyEngine2D - Pink UI theme loading.
#include "Engine/UI/UiTheme.hpp"

#include <toml.hpp>

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace Fby::Ui
{
namespace
{

constexpr const char* kSlotKeys[kUiPaletteSlots] = {
    "primary", "active", "accent", "text", "text_muted", "background", "surface", "border",
    "warning", "critical", "success", "shadow", "highlight", "disabled", "focus", "overlay",
    "hover", "pressed"};

[[nodiscard]] Color32 ParseColor(std::string_view InText, Color32 InFallback) noexcept
{
    if (InText.empty())
    {
        return InFallback;
    }
    if (InText.front() == '#')
    {
        InText.remove_prefix(1);
    }
    else if (InText.size() > 2 && InText[0] == '0' && (InText[1] == 'x' || InText[1] == 'X'))
    {
        InText.remove_prefix(2);
    }
    if (InText.size() != 6 && InText.size() != 8)
    {
        return InFallback;
    }
    char ScopedBuffer[9]{};
    std::memcpy(ScopedBuffer, InText.data(), InText.size());
    char* ScopedEnd = nullptr;
    const unsigned long ScopedValue = std::strtoul(ScopedBuffer, &ScopedEnd, 16);
    if (ScopedEnd == ScopedBuffer)
    {
        return InFallback;
    }
    if (InText.size() == 6)
    {
        return Color32::FromBytes(static_cast<std::uint8_t>((ScopedValue >> 16) & 0xFFu),
                                  static_cast<std::uint8_t>((ScopedValue >> 8) & 0xFFu),
                                  static_cast<std::uint8_t>(ScopedValue & 0xFFu));
    }
    // Eight hex digits are RRGGBBAA, the same order the theme docs use.
    return Color32::FromBytes(static_cast<std::uint8_t>((ScopedValue >> 24) & 0xFFu),
                              static_cast<std::uint8_t>((ScopedValue >> 16) & 0xFFu),
                              static_cast<std::uint8_t>((ScopedValue >> 8) & 0xFFu),
                              static_cast<std::uint8_t>(ScopedValue & 0xFFu));
}
}

UiThemeAsset DefaultUiTheme() noexcept
{
    UiThemeAsset ScopedTheme{};
    ScopedTheme.palette.colors[static_cast<std::uint32_t>(UiPaletteSlot::Primary)] = Color32::FromBytes(0xE8, 0x5D, 0x9E);
    ScopedTheme.palette.colors[static_cast<std::uint32_t>(UiPaletteSlot::Active)] = Color32::FromBytes(0xC7, 0x3D, 0x80);
    ScopedTheme.palette.colors[static_cast<std::uint32_t>(UiPaletteSlot::Accent)] = Color32::FromBytes(0x63, 0xD8, 0xFF);
    ScopedTheme.palette.colors[static_cast<std::uint32_t>(UiPaletteSlot::Text)] = Color32::FromBytes(0xF4, 0xF4, 0xF8);
    ScopedTheme.palette.colors[static_cast<std::uint32_t>(UiPaletteSlot::TextMuted)] =
        Color32::FromBytes(0x9A, 0x9A, 0xA8);
    ScopedTheme.palette.colors[static_cast<std::uint32_t>(UiPaletteSlot::Background)] =
        Color32::FromBytes(0x12, 0x12, 0x18);
    ScopedTheme.palette.colors[static_cast<std::uint32_t>(UiPaletteSlot::Surface)] = Color32::FromBytes(0x22, 0x22, 0x2C);
    ScopedTheme.palette.colors[static_cast<std::uint32_t>(UiPaletteSlot::Border)] = Color32::FromBytes(0x3A, 0x3A, 0x48);
    ScopedTheme.palette.colors[static_cast<std::uint32_t>(UiPaletteSlot::Warning)] =
        Color32::FromBytes(0xF2, 0xB8, 0x4E);
    ScopedTheme.palette.colors[static_cast<std::uint32_t>(UiPaletteSlot::Critical)] =
        Color32::FromBytes(0xF2, 0x4E, 0x5C);
    ScopedTheme.palette.colors[static_cast<std::uint32_t>(UiPaletteSlot::Success)] =
        Color32::FromBytes(0x5C, 0xD8, 0x8A);
    ScopedTheme.palette.colors[static_cast<std::uint32_t>(UiPaletteSlot::Shadow)] =
        Color32::FromBytes(0x00, 0x00, 0x00, 0x80);
    ScopedTheme.palette.colors[static_cast<std::uint32_t>(UiPaletteSlot::Highlight)] =
        Color32::FromBytes(0xFF, 0xFF, 0xFF, 0x22);
    ScopedTheme.palette.colors[static_cast<std::uint32_t>(UiPaletteSlot::Disabled)] =
        Color32::FromBytes(0x55, 0x55, 0x62);
    ScopedTheme.palette.colors[static_cast<std::uint32_t>(UiPaletteSlot::Focus)] =
        Color32::FromBytes(0x63, 0xD8, 0xFF);
    ScopedTheme.palette.colors[static_cast<std::uint32_t>(UiPaletteSlot::Overlay)] =
        Color32::FromBytes(0x00, 0x00, 0x00, 0xB0);
    // Alpha zero means "unset": SolidColor falls back to scaling the tint.
    ScopedTheme.palette.colors[static_cast<std::uint32_t>(UiPaletteSlot::Hover)] =
        Color32::FromBytes(0x00, 0x00, 0x00, 0x00);
    ScopedTheme.palette.colors[static_cast<std::uint32_t>(UiPaletteSlot::Pressed)] =
        Color32::FromBytes(0x00, 0x00, 0x00, 0x00);
    return ScopedTheme;
}

bool LoadUiThemeToml(std::string_view InText, UiThemeAsset& OutTheme, std::string* OutError)
{
    toml::table ScopedRoot;
    try
    {
        ScopedRoot = toml::parse(InText);
    }
    catch (const toml::parse_error& ScopedError)
    {
        if (OutError != nullptr)
        {
            *OutError = std::string("theme TOML error: ") + std::string(ScopedError.description());
        }
        return false;
    }
    OutTheme = DefaultUiTheme();
    const toml::table* ScopedTheme = ScopedRoot["theme"].as_table();
    if (ScopedTheme == nullptr)
    {
        ScopedTheme = &ScopedRoot;
    }
    if (const auto ScopedName = (*ScopedTheme)["name"].value<std::string>(); ScopedName.has_value())
    {
        std::memset(OutTheme.name, 0, sizeof(OutTheme.name));
        std::memcpy(OutTheme.name, ScopedName->data(), std::min(ScopedName->size(), sizeof(OutTheme.name) - 1));
    }
    const toml::table* ScopedPalette = (*ScopedTheme)["palette"].as_table();
    if (ScopedPalette == nullptr)
    {
        ScopedPalette = ScopedTheme;
    }
    for (std::uint32_t ScopedI = 0; ScopedI < kUiPaletteSlots; ++ScopedI)
    {
        const toml::node_view ScopedNode = (*ScopedPalette)[kSlotKeys[ScopedI]];
        if (const auto ScopedValue = ScopedNode.value<std::string>(); ScopedValue.has_value())
        {
            OutTheme.palette.colors[ScopedI] = ParseColor(*ScopedValue, OutTheme.palette.colors[ScopedI]);
        }
        else if (const auto ScopedInteger = ScopedNode.value<std::int64_t>(); ScopedInteger.has_value())
        {
            OutTheme.palette.colors[ScopedI] = Color32{static_cast<std::uint32_t>(*ScopedInteger)};
        }
    }
    return true;
}
}
