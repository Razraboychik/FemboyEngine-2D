// FemboyEngine2D - runtime accessibility palettes and text presentation.
#pragma once

#include "Engine/Core/Types.hpp"
#include "Engine/UI/UiTheme.hpp"

#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace Fby::Ui
{

inline constexpr std::size_t kAccessibilityPaletteSlots = 16;
inline constexpr std::size_t kMaxAccessibilityThemes = 32;
inline constexpr std::size_t kAccessibilityThemeIdChars = 40;
inline constexpr std::size_t kAccessibilityThemeNameChars = 64;

// Resource ids and gameplay tags use dotted PascalCase segments. This is the
// single validator shared by the theme, extension and effect registries.
[[nodiscard]] inline bool IsPascalCasePath(std::string_view InPath) noexcept
{
    if (InPath.empty())
    {
        return false;
    }
    bool ScopedSegmentStart = true;
    for (const char ScopedChar : InPath)
    {
        const auto ScopedByte = static_cast<unsigned char>(ScopedChar);
        if (ScopedChar == '.')
        {
            if (ScopedSegmentStart)
            {
                return false;
            }
            ScopedSegmentStart = true;
            continue;
        }
        if (ScopedSegmentStart)
        {
            if (!std::isupper(ScopedByte))
            {
                return false;
            }
            ScopedSegmentStart = false;
            continue;
        }
        if (!std::isalnum(ScopedByte) && ScopedChar != '_')
        {
            return false;
        }
    }
    return !ScopedSegmentStart;
}

enum class AccessibilitySlot : std::uint8_t
{
    Background = 0,
    Surface,
    SurfaceRaised,
    Border,
    Text,
    TextMuted,
    Primary,
    Secondary,
    Accent,
    Success,
    Warning,
    Danger,
    Info,
    AxisX,
    AxisY,
    Highlight,
};

// Text is independent from the panel/world palette. Outline size 0 disables it.
struct AccessibilityTextStyle
{
    Color32 Color = Color32::White();
    Color32 Outline = Color32::Black();
    float Opacity = 1.0f;
    float Contrast = 1.0f;
    float OutlineSize = 0.0f;
    float FontScale = 1.0f;
};

struct AccessibilityTheme
{
    char Id[kAccessibilityThemeIdChars]{};
    char DisplayName[kAccessibilityThemeNameChars]{};
    std::array<Color32, kAccessibilityPaletteSlots> Colors{};
    AccessibilityTextStyle Text{};

    [[nodiscard]] Color32 Resolve(AccessibilitySlot InSlot) const noexcept
    {
        const auto ScopedIndex = static_cast<std::size_t>(InSlot);
        return ScopedIndex < Colors.size() ? Colors[ScopedIndex] : Color32::White();
    }
};

static_assert(std::is_trivially_copyable_v<AccessibilityTheme>);

[[nodiscard]] AccessibilityTheme DefaultAccessibilityTheme() noexcept;
[[nodiscard]] AccessibilityTheme MakeNvgTheme() noexcept;
[[nodiscard]] AccessibilityTheme MakeThermalTheme() noexcept;
[[nodiscard]] AccessibilityTheme MakeTrafficTheme(bool InProtanopia, bool InDeuteranopia, bool InTritanopia) noexcept;
[[nodiscard]] AccessibilityTheme MakeExtendedTrafficTheme() noexcept;

// Project/game-scoped registry. The editor owns its own palette; this registry
// is for runtime HUD, subtitles, world palette and the Rainbow plugin.
class AccessibilityThemeRegistry
{
public:
    // `owner` is the plugin host's token (or nullptr for engine/game themes).
    [[nodiscard]] bool Register(const AccessibilityTheme& InTheme, const void* InOwner = nullptr,
                                std::string* InError = nullptr);
    [[nodiscard]] bool SetActive(std::string_view InId, std::string* InError = nullptr);
    void RemoveOwner(const void* InOwner) noexcept;
    void Clear() noexcept;

    [[nodiscard]] const AccessibilityTheme* Find(std::string_view InId) const noexcept;
    [[nodiscard]] const AccessibilityTheme* Active() const noexcept;
    [[nodiscard]] std::span<const AccessibilityTheme> Themes() const noexcept;
    [[nodiscard]] std::size_t Count() const noexcept
    {
        return MCount;
    }

    // Maps the 16 semantic slots to the existing 18-slot PinkUI theme. Text
    // style stays in AccessibilityTextStyle because UiThemeAsset has no outline.
    [[nodiscard]] UiThemeAsset BuildUiTheme() const noexcept;
    void ApplyToUiTheme(UiThemeAsset& InDestination) const noexcept;

private:
    std::array<AccessibilityTheme, kMaxAccessibilityThemes> MThemes{};
    std::array<const void*, kMaxAccessibilityThemes> MOwners{};
    std::size_t MCount = 0;
    std::size_t MActive = 0;
};

}
