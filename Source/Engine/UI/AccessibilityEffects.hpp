// FemboyEngine2D - runtime vision-effect profiles (NVG, thermal and palettes).
#pragma once

#include "Engine/Core/Types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace Fby::Ui
{

inline constexpr std::size_t kAccessibilityExtensionColors = 80;
inline constexpr std::size_t kMaxAccessibilityExtensions = 16;
inline constexpr std::size_t kMaxAccessibilityEffects = 16;
inline constexpr std::size_t kAccessibilityTemperatureBands = 5;
inline constexpr std::size_t kAccessibilityEffectTags = 16;
inline constexpr std::size_t kMaxAlcoholEffects = 10;
inline constexpr std::size_t kAccessibilityEffectIdChars = 40;
inline constexpr std::size_t kAccessibilityEffectNameChars = 64;
inline constexpr std::size_t kAccessibilityTagChars = 64;

enum class AccessibilityEffectKind : std::uint8_t
{
    Nvg = 0,
    Thermal,
    Alcohol,
};

enum class NvgPattern : std::uint8_t
{
    Clean = 0,
    Scanlines,
    Grid,
    Noise,
    Hex,
};

struct AccessibilityPaletteExtension
{
    char Id[kAccessibilityEffectIdChars]{};
    char DisplayName[kAccessibilityEffectNameChars]{};
    std::array<Color32, kAccessibilityExtensionColors> Colors{};
};

struct AccessibilityTemperatureBand
{
    char Tag[kAccessibilityTagChars]{};
    Color32 Color = Color32::White();
};

// A resource, not a renderer command. The game decides which surface and how
// strongly this profile is applied; the plugin only supplies the look and the
// recommended metadata.
struct AccessibilityEffectTag
{
    char Tag[kAccessibilityTagChars]{};
};

enum class AlcoholEffect : std::uint8_t
{
    DoubleVision = 0,
    Wobble,
    Blur,
    ChromaticAberration,
    Vignette,
    Desaturation,
    Afterimage,
    Pulse,
    Sway,
    ScaleBreathing,
};

struct AlcoholEffectLayer
{
    AlcoholEffect Effect = AlcoholEffect::DoubleVision;
    float Intensity = 0.0f;
    float Frequency = 1.0f;
};

struct AccessibilityEffectProfile
{
    char Id[kAccessibilityEffectIdChars]{};
    char DisplayName[kAccessibilityEffectNameChars]{};
    AccessibilityEffectKind Kind = AccessibilityEffectKind::Nvg;
    float Intensity = 1.0f;
    float Blur = 0.0f;
    float Sharpness = 0.0f;
    NvgPattern Pattern = NvgPattern::Clean;
    float PatternScale = 1.0f;
    std::uint8_t Generation = 0;
    Color32 Tint = Color32::FromBytes(0x80, 0xFF, 0x80);
    std::array<AccessibilityEffectTag, kAccessibilityEffectTags> Tags{};
    std::uint8_t TagCount = 0;
    std::array<AlcoholEffectLayer, kMaxAlcoholEffects> AlcoholEffects{};
    std::uint8_t AlcoholEffectCount = 0;
    std::array<AccessibilityTemperatureBand, kAccessibilityTemperatureBands> Temperatures{};
};

class AccessibilityExtensionRegistry
{
public:
    [[nodiscard]] bool Register(const AccessibilityPaletteExtension& InExtension, const void* InOwner = nullptr,
                                std::string* InError = nullptr);
    void RemoveOwner(const void* InOwner) noexcept;
    [[nodiscard]] const AccessibilityPaletteExtension* Find(std::string_view InId) const noexcept;
    [[nodiscard]] std::span<const AccessibilityPaletteExtension> Extensions() const noexcept;
    [[nodiscard]] std::size_t Count() const noexcept
    {
        return MCount;
    }

private:
    std::array<AccessibilityPaletteExtension, kMaxAccessibilityExtensions> MExtensions{};
    std::array<const void*, kMaxAccessibilityExtensions> MOwners{};
    std::size_t MCount = 0;
};

class AccessibilityEffectRegistry
{
public:
    [[nodiscard]] bool Register(const AccessibilityEffectProfile& InEffect, const void* InOwner = nullptr,
                                std::string* InError = nullptr);
    [[nodiscard]] bool SetActive(std::string_view InId, std::string* InError = nullptr);
    void RemoveOwner(const void* InOwner) noexcept;
    void Clear() noexcept;
    [[nodiscard]] const AccessibilityEffectProfile* Find(std::string_view InId) const noexcept;
    [[nodiscard]] const AccessibilityEffectProfile* Active() const noexcept;
    [[nodiscard]] std::span<const AccessibilityEffectProfile> Effects() const noexcept;
    [[nodiscard]] std::size_t Count() const noexcept
    {
        return MCount;
    }

private:
    std::array<AccessibilityEffectProfile, kMaxAccessibilityEffects> MEffects{};
    std::array<const void*, kMaxAccessibilityEffects> MOwners{};
    std::size_t MCount = 0;
    std::size_t MActive = 0;
};
}
