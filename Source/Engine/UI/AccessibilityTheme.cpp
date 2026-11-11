// FemboyEngine2D - runtime accessibility palette registry.
#include "Engine/UI/AccessibilityTheme.hpp"

#include <algorithm>
#include <cstring>

namespace Fby::Ui
{
namespace
{
void SetText(char* InDestination, std::size_t InCapacity, std::string_view InText) noexcept
{
    if (InDestination == nullptr || InCapacity == 0)
    {
        return;
    }
    const std::size_t ScopedLength = std::min(InText.size(), InCapacity - 1);
    std::memcpy(InDestination, InText.data(), ScopedLength);
    InDestination[ScopedLength] = '\0';
}

[[nodiscard]] AccessibilityTheme MakeBaseTheme(std::string_view InId, std::string_view InName) noexcept
{
    AccessibilityTheme ScopedTheme{};
    SetText(ScopedTheme.Id, sizeof(ScopedTheme.Id), InId);
    SetText(ScopedTheme.DisplayName, sizeof(ScopedTheme.DisplayName), InName);
    ScopedTheme.Text.Color = Color32::FromBytes(0xF4, 0xF4, 0xF8);
    ScopedTheme.Text.Outline = Color32::Black();
    ScopedTheme.Text.Opacity = 1.0f;
    ScopedTheme.Text.Contrast = 1.0f;
    ScopedTheme.Text.OutlineSize = 0.0f;
    ScopedTheme.Text.FontScale = 1.0f;
    return ScopedTheme;
}

void SetSlot(AccessibilityTheme& InDestination, AccessibilitySlot InSlot, Color32 InColor) noexcept
{
    InDestination.Colors[static_cast<std::size_t>(InSlot)] = InColor;
}
}

AccessibilityTheme DefaultAccessibilityTheme() noexcept
{
    AccessibilityTheme ScopedTheme = MakeBaseTheme("Default", "Default");
    SetSlot(ScopedTheme, AccessibilitySlot::Background, Color32::FromBytes(0x12, 0x12, 0x18));
    SetSlot(ScopedTheme, AccessibilitySlot::Surface, Color32::FromBytes(0x22, 0x22, 0x2C));
    SetSlot(ScopedTheme, AccessibilitySlot::SurfaceRaised, Color32::FromBytes(0x2C, 0x2C, 0x38));
    SetSlot(ScopedTheme, AccessibilitySlot::Border, Color32::FromBytes(0x3A, 0x3A, 0x48));
    SetSlot(ScopedTheme, AccessibilitySlot::Text, Color32::FromBytes(0xF4, 0xF4, 0xF8));
    SetSlot(ScopedTheme, AccessibilitySlot::TextMuted, Color32::FromBytes(0x9A, 0x9A, 0xA8));
    SetSlot(ScopedTheme, AccessibilitySlot::Primary, Color32::FromBytes(0xE8, 0x5D, 0x9E));
    SetSlot(ScopedTheme, AccessibilitySlot::Secondary, Color32::FromBytes(0xC7, 0x3D, 0x80));
    SetSlot(ScopedTheme, AccessibilitySlot::Accent, Color32::FromBytes(0x63, 0xD8, 0xFF));
    SetSlot(ScopedTheme, AccessibilitySlot::Success, Color32::FromBytes(0x5C, 0xD8, 0x8A));
    SetSlot(ScopedTheme, AccessibilitySlot::Warning, Color32::FromBytes(0xF2, 0xB8, 0x4E));
    SetSlot(ScopedTheme, AccessibilitySlot::Danger, Color32::FromBytes(0xF2, 0x4E, 0x5C));
    SetSlot(ScopedTheme, AccessibilitySlot::Info, Color32::FromBytes(0x63, 0xD8, 0xFF));
    SetSlot(ScopedTheme, AccessibilitySlot::AxisX, Color32::FromBytes(0xE0, 0xA0, 0x40));
    SetSlot(ScopedTheme, AccessibilitySlot::AxisY, Color32::FromBytes(0x60, 0xA0, 0xE0));
    SetSlot(ScopedTheme, AccessibilitySlot::Highlight, Color32::FromBytes(0xFF, 0xFF, 0xFF));
    return ScopedTheme;
}

AccessibilityTheme MakeNvgTheme() noexcept
{
    AccessibilityTheme ScopedTheme = MakeBaseTheme("Rainbow.Nvg", "NVG");
    for (std::size_t ScopedI = 0; ScopedI < kAccessibilityPaletteSlots; ++ScopedI)
    {
        const std::uint8_t ScopedLevel = static_cast<std::uint8_t>(24 + ScopedI * 12);
        ScopedTheme.Colors[ScopedI] = Color32::FromBytes(static_cast<std::uint8_t>(ScopedLevel / 4),
                                                         ScopedLevel, static_cast<std::uint8_t>(ScopedLevel / 5));
    }
    ScopedTheme.Text.Color = Color32::FromBytes(0xD8, 0xFF, 0xD8);
    ScopedTheme.Text.Outline = Color32::Black();
    return ScopedTheme;
}

AccessibilityTheme MakeThermalTheme() noexcept
{
    AccessibilityTheme ScopedTheme = MakeBaseTheme("Rainbow.Thermal", "Thermal");
    constexpr Color32 kRamp[] = {
        Color32::FromBytes(0x00, 0x00, 0x20), Color32::FromBytes(0x20, 0x00, 0x60),
        Color32::FromBytes(0x60, 0x00, 0x80), Color32::FromBytes(0xA0, 0x20, 0x40),
        Color32::FromBytes(0xE0, 0x60, 0x10), Color32::FromBytes(0xFF, 0xC0, 0x20),
        Color32::FromBytes(0xFF, 0xFF, 0xE0)};
    for (std::size_t ScopedI = 0; ScopedI < kAccessibilityPaletteSlots; ++ScopedI)
    {
        ScopedTheme.Colors[ScopedI] = kRamp[std::min<std::size_t>(ScopedI, std::size(kRamp) - 1)];
    }
    ScopedTheme.Text.Color = Color32::FromBytes(0xFF, 0xFF, 0xE8);
    return ScopedTheme;
}

AccessibilityTheme MakeTrafficTheme(bool InProtanopia, bool InDeuteranopia, bool InTritanopia) noexcept
{
    const char* ScopedId = "Rainbow.Traffic";
    const char* ScopedName = "Traffic light";
    if (InProtanopia)
    {
        ScopedId = "Rainbow.Traffic.Protan";
        ScopedName = "Traffic light - protanopia";
    }
    else if (InDeuteranopia)
    {
        ScopedId = "Rainbow.Traffic.Deutan";
        ScopedName = "Traffic light - deuteranopia";
    }
    else if (InTritanopia)
    {
        ScopedId = "Rainbow.Traffic.Tritan";
        ScopedName = "Traffic light - tritanopia";
    }
    AccessibilityTheme ScopedTheme = MakeBaseTheme(ScopedId, ScopedName);
    SetSlot(ScopedTheme, AccessibilitySlot::Background, Color32::FromBytes(0x18, 0x18, 0x1C));
    SetSlot(ScopedTheme, AccessibilitySlot::Surface, Color32::FromBytes(0x28, 0x28, 0x30));
    SetSlot(ScopedTheme, AccessibilitySlot::Border, Color32::FromBytes(0x70, 0x70, 0x78));
    SetSlot(ScopedTheme, AccessibilitySlot::Text, Color32::FromBytes(0xFF, 0xFF, 0xFF));
    SetSlot(ScopedTheme, AccessibilitySlot::TextMuted, Color32::FromBytes(0xC0, 0xC0, 0xC8));
    SetSlot(ScopedTheme, AccessibilitySlot::Success, Color32::FromBytes(0x28, 0xE0, 0x60));
    SetSlot(ScopedTheme, AccessibilitySlot::Warning, Color32::FromBytes(0xFF, 0xD0, 0x30));
    SetSlot(ScopedTheme, AccessibilitySlot::Danger, Color32::FromBytes(0xFF, 0x40, 0x40));
    SetSlot(ScopedTheme, AccessibilitySlot::Info, Color32::FromBytes(0x60, 0xB0, 0xFF));
    if (InProtanopia)
    {
        SetSlot(ScopedTheme, AccessibilitySlot::Success, Color32::FromBytes(0x00, 0xC8, 0xFF));
        SetSlot(ScopedTheme, AccessibilitySlot::Danger, Color32::FromBytes(0xFF, 0x9A, 0x20));
    }
    else if (InDeuteranopia)
    {
        SetSlot(ScopedTheme, AccessibilitySlot::Success, Color32::FromBytes(0x00, 0xD0, 0xFF));
        SetSlot(ScopedTheme, AccessibilitySlot::Danger, Color32::FromBytes(0xFF, 0xB0, 0x30));
    }
    else if (InTritanopia)
    {
        SetSlot(ScopedTheme, AccessibilitySlot::Success, Color32::FromBytes(0xFF, 0x70, 0xB0));
        SetSlot(ScopedTheme, AccessibilitySlot::Danger, Color32::FromBytes(0xFF, 0xE0, 0x40));
    }
    return ScopedTheme;
}

AccessibilityTheme MakeExtendedTrafficTheme() noexcept
{
    AccessibilityTheme ScopedTheme = MakeTrafficTheme(false, false, false);
    SetText(ScopedTheme.Id, sizeof(ScopedTheme.Id), "Rainbow.Traffic.Extended");
    SetText(ScopedTheme.DisplayName, sizeof(ScopedTheme.DisplayName), "Traffic light - extended");
    // Five ordered meanings: neutral, safe, caution, hazard, critical.
    SetSlot(ScopedTheme, AccessibilitySlot::Success, Color32::FromBytes(0x40, 0xE0, 0x70));
    SetSlot(ScopedTheme, AccessibilitySlot::Warning, Color32::FromBytes(0xFF, 0xB0, 0x30));
    SetSlot(ScopedTheme, AccessibilitySlot::Danger, Color32::FromBytes(0xFF, 0x30, 0x50));
    SetSlot(ScopedTheme, AccessibilitySlot::Info, Color32::FromBytes(0xFF, 0x80, 0x20));
    return ScopedTheme;
}

bool AccessibilityThemeRegistry::Register(const AccessibilityTheme& InTheme, const void* InOwner, std::string* InError)
{
    if (InTheme.Id[0] == '\0')
    {
        if (InError != nullptr)
        {
            *InError = "theme id is empty";
        }
        return false;
    }
    if (MCount >= kMaxAccessibilityThemes)
    {
        if (InError != nullptr)
        {
            *InError = "theme registry is full";
        }
        return false;
    }
    if (Find(InTheme.Id) != nullptr)
    {
        if (InError != nullptr)
        {
            *InError = "theme id already registered";
        }
        return false;
    }
    MThemes[MCount] = InTheme;
    MOwners[MCount] = InOwner;
    ++MCount;
    if (MCount == 1)
    {
        MActive = 0;
    }
    return true;
}

bool AccessibilityThemeRegistry::SetActive(std::string_view InId, std::string* InError)
{
    for (std::size_t ScopedI = 0; ScopedI < MCount; ++ScopedI)
    {
        if (std::string_view(MThemes[ScopedI].Id) == InId)
        {
            MActive = ScopedI;
            return true;
        }
    }
    if (InError != nullptr)
    {
        *InError = "unknown accessibility theme";
    }
    return false;
}

void AccessibilityThemeRegistry::RemoveOwner(const void* InOwner) noexcept
{
    std::size_t ScopedWrite = 0;
    for (std::size_t ScopedI = 0; ScopedI < MCount; ++ScopedI)
    {
        if (MOwners[ScopedI] == InOwner)
        {
            continue;
        }
        if (ScopedWrite != ScopedI)
        {
            MThemes[ScopedWrite] = MThemes[ScopedI];
            MOwners[ScopedWrite] = MOwners[ScopedI];
        }
        ++ScopedWrite;
    }
    MCount = ScopedWrite;
    if (MCount == 0)
    {
        MActive = 0;
    }
    else if (MActive >= MCount)
    {
        MActive = MCount - 1;
    }
}

void AccessibilityThemeRegistry::Clear() noexcept
{
    MCount = 0;
    MActive = 0;
}

const AccessibilityTheme* AccessibilityThemeRegistry::Find(std::string_view InId) const noexcept
{
    for (std::size_t ScopedI = 0; ScopedI < MCount; ++ScopedI)
    {
        if (std::string_view(MThemes[ScopedI].Id) == InId)
        {
            return &MThemes[ScopedI];
        }
    }
    return nullptr;
}

const AccessibilityTheme* AccessibilityThemeRegistry::Active() const noexcept
{
    return MCount == 0 ? nullptr : &MThemes[MActive];
}

std::span<const AccessibilityTheme> AccessibilityThemeRegistry::Themes() const noexcept
{
    return std::span<const AccessibilityTheme>(MThemes.data(), MCount);
}

UiThemeAsset AccessibilityThemeRegistry::BuildUiTheme() const noexcept
{
    UiThemeAsset ScopedTheme = DefaultUiTheme();
    ApplyToUiTheme(ScopedTheme);
    return ScopedTheme;
}

void AccessibilityThemeRegistry::ApplyToUiTheme(UiThemeAsset& InDestination) const noexcept
{
    const AccessibilityTheme* ScopedActive = Active();
    if (ScopedActive == nullptr)
    {
        return;
    }
    SetText(InDestination.name, sizeof(InDestination.name), ScopedActive->DisplayName);
    InDestination.palette.colors[static_cast<std::size_t>(UiPaletteSlot::Background)] =
        ScopedActive->Resolve(AccessibilitySlot::Background);
    InDestination.palette.colors[static_cast<std::size_t>(UiPaletteSlot::Surface)] =
        ScopedActive->Resolve(AccessibilitySlot::Surface);
    InDestination.palette.colors[static_cast<std::size_t>(UiPaletteSlot::Border)] =
        ScopedActive->Resolve(AccessibilitySlot::Border);
    InDestination.palette.colors[static_cast<std::size_t>(UiPaletteSlot::Text)] =
        ScopedActive->Resolve(AccessibilitySlot::Text);
    InDestination.palette.colors[static_cast<std::size_t>(UiPaletteSlot::TextMuted)] =
        ScopedActive->Resolve(AccessibilitySlot::TextMuted);
    InDestination.palette.colors[static_cast<std::size_t>(UiPaletteSlot::Primary)] =
        ScopedActive->Resolve(AccessibilitySlot::Primary);
    InDestination.palette.colors[static_cast<std::size_t>(UiPaletteSlot::Active)] =
        ScopedActive->Resolve(AccessibilitySlot::Secondary);
    InDestination.palette.colors[static_cast<std::size_t>(UiPaletteSlot::Accent)] =
        ScopedActive->Resolve(AccessibilitySlot::Accent);
    InDestination.palette.colors[static_cast<std::size_t>(UiPaletteSlot::Success)] =
        ScopedActive->Resolve(AccessibilitySlot::Success);
    InDestination.palette.colors[static_cast<std::size_t>(UiPaletteSlot::Warning)] =
        ScopedActive->Resolve(AccessibilitySlot::Warning);
    InDestination.palette.colors[static_cast<std::size_t>(UiPaletteSlot::Critical)] =
        ScopedActive->Resolve(AccessibilitySlot::Danger);
    InDestination.palette.colors[static_cast<std::size_t>(UiPaletteSlot::Highlight)] =
        ScopedActive->Resolve(AccessibilitySlot::Highlight);
    InDestination.palette.colors[static_cast<std::size_t>(UiPaletteSlot::Focus)] =
        ScopedActive->Resolve(AccessibilitySlot::Info);
}
}
