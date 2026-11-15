// FemboyEngine2D - Themed Palettes: seasonal and display 80-colour packs.
#include "Engine/Core/PluginAPI.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace
{

[[nodiscard]] constexpr std::uint32_t PackRgb(std::uint32_t InRed, std::uint32_t InGreen, std::uint32_t InBlue) noexcept
{
    return 0xFF000000u | (InBlue << 16u) | (InGreen << 8u) | InRed;
}

// Eight shades per seed family: four darker steps, the seed itself is in the
// middle, and the light steps blend toward white. Ten families fill the 80
// slots exactly.
[[nodiscard]] std::uint32_t Shade(std::uint32_t InColor, std::uint32_t InStep) noexcept
{
    const float ScopedT = static_cast<float>(InStep) / 7.0f;
    const float ScopedFactor = 0.35f + ScopedT * 1.20f;
    const auto ScopedChannel = [InColor, ScopedFactor](std::uint32_t InShift)
    {
        const float ScopedValue = static_cast<float>((InColor >> InShift) & 0xFFu);
        if (ScopedFactor <= 1.0f)
        {
            return static_cast<std::uint32_t>(ScopedValue * ScopedFactor);
        }
        return static_cast<std::uint32_t>(ScopedValue + (255.0f - ScopedValue) * (ScopedFactor - 1.0f) * 0.55f);
    };
    return PackRgb(std::min(ScopedChannel(0), 255u), std::min(ScopedChannel(8), 255u),
                   std::min(ScopedChannel(16), 255u));
}

struct ThemedPalette
{
    const char* Id;
    const char* Name;
    std::array<std::uint32_t, 10> Seeds;
};

[[nodiscard]] std::array<ThemedPalette, 6> MakePalettes() noexcept
{
    return {
        ThemedPalette{"Palette.Halloween",
                      "Halloween Palette",
                      {PackRgb(0xFF, 0x6A, 0x00), PackRgb(0xE8, 0xD8, 0xB0), PackRgb(0xA0, 0x10, 0x18),
                       PackRgb(0x4A, 0x6A, 0x2A), PackRgb(0xFF, 0xD0, 0x60), PackRgb(0x5A, 0x2A, 0x6A),
                       PackRgb(0x8A, 0x3A, 0x18), PackRgb(0x60, 0x60, 0x60), PackRgb(0xD0, 0x40, 0x10),
                       PackRgb(0x20, 0x18, 0x20)}},
        ThemedPalette{"Palette.Winter",
                      "Winter Palette",
                      {PackRgb(0xA8, 0xE0, 0xFF), PackRgb(0xF0, 0xF8, 0xFF), PackRgb(0x2A, 0x5A, 0x48),
                       PackRgb(0x3A, 0x4A, 0x7A), PackRgb(0x80, 0x90, 0xA0), PackRgb(0xD0, 0xE8, 0xF0),
                       PackRgb(0x10, 0x20, 0x40), PackRgb(0xC0, 0xC8, 0xD0), PackRgb(0x60, 0xFF, 0xC0),
                       PackRgb(0x10, 0x18, 0x20)}},
        ThemedPalette{"Palette.Autumn",
                      "Autumn Palette",
                      {PackRgb(0xFF, 0xB0, 0x20), PackRgb(0xB0, 0x50, 0x20), PackRgb(0xE0, 0xC0, 0x40),
                       PackRgb(0x60, 0x70, 0x30), PackRgb(0x70, 0x40, 0x20), PackRgb(0x70, 0x20, 0x30),
                       PackRgb(0xD0, 0xB0, 0x70), PackRgb(0x80, 0x80, 0x40), PackRgb(0x90, 0x80, 0x70),
                       PackRgb(0x30, 0x28, 0x20)}},
        ThemedPalette{"Palette.Spring",
                      "Spring Palette",
                      {PackRgb(0xFF, 0xB0, 0xC0), PackRgb(0x60, 0xC0, 0x60), PackRgb(0x80, 0xC0, 0xFF),
                       PackRgb(0xFF, 0xE0, 0x80), PackRgb(0xB0, 0x90, 0xE0), PackRgb(0x80, 0xE0, 0xC0),
                       PackRgb(0x70, 0x50, 0x30), PackRgb(0x70, 0x80, 0x90), PackRgb(0xFF, 0x80, 0x70),
                       PackRgb(0xF8, 0xF8, 0xF0)}},
        ThemedPalette{"Palette.Terminal",
                      "Terminal Palette",
                      {PackRgb(0x40, 0xFF, 0x60), PackRgb(0x10, 0x40, 0x10), PackRgb(0xA0, 0xFF, 0xA0),
                       PackRgb(0xFF, 0xB0, 0x00), PackRgb(0x80, 0x40, 0x00), PackRgb(0x40, 0xE0, 0xE0),
                       PackRgb(0xA0, 0xA0, 0xA0), PackRgb(0x08, 0x10, 0x08), PackRgb(0xC0, 0xFF, 0x40),
                       PackRgb(0xFF, 0x40, 0x40)}},
        ThemedPalette{"Palette.Paper",
                      "Paper Palette",
                      {PackRgb(0xF0, 0xE0, 0xC0), PackRgb(0x20, 0x20, 0x20), PackRgb(0xFF, 0xF8, 0xE0),
                       PackRgb(0xA0, 0x80, 0x60), PackRgb(0x70, 0x90, 0xA0), PackRgb(0x60, 0x40, 0x30),
                       PackRgb(0x80, 0x80, 0x80), PackRgb(0xC0, 0xA0, 0x40), PackRgb(0xC0, 0x80, 0x80),
                       PackRgb(0x10, 0x10, 0x10)}},
    };
}

FbyPaletteExtensionDesc MakeExtension(const ThemedPalette& InPalette) noexcept
{
    FbyPaletteExtensionDesc ScopedExtension{};
    ScopedExtension.struct_size = static_cast<std::uint32_t>(sizeof(FbyPaletteExtensionDesc));
    ScopedExtension.id = InPalette.Id;
    ScopedExtension.display_name = InPalette.Name;
    for (std::uint32_t ScopedFamily = 0; ScopedFamily < InPalette.Seeds.size(); ++ScopedFamily)
    {
        for (std::uint32_t ScopedStep = 0; ScopedStep < 8; ++ScopedStep)
        {
            ScopedExtension.colors[ScopedFamily * 8 + ScopedStep] = Shade(InPalette.Seeds[ScopedFamily], ScopedStep);
        }
    }
    return ScopedExtension;
}
}

FBY_PLUGIN_ENTRY const FbyPluginInfo* fby_plugin_info()
{
    static const FbyPluginInfo ScopedInfo{static_cast<std::uint32_t>(sizeof(FbyPluginInfo)),
                                          FBY_PLUGIN_ABI_VERSION,
                                          "Themed Palettes",
                                          FBY_ENGINE_VERSION,
                                          "FemboyEngine2D contributors",
                                          0,
                                          nullptr,
                                          "Seasonal and display 80-colour palette packs"};
    return &ScopedInfo;
}

FBY_PLUGIN_ENTRY int fby_plugin_load(FbyHostApi* InHost)
{
    if (InHost == nullptr || InHost->abi_version != FBY_PLUGIN_ABI_VERSION || InHost->struct_size < sizeof(FbyHostApi))
    {
        return 1;
    }
    if (InHost->accessibility == nullptr || InHost->accessibility->register_palette_extension == nullptr)
    {
        return 0;
    }
    const std::array<ThemedPalette, 6> ScopedPalettes = MakePalettes();
    for (const ThemedPalette& ScopedPalette : ScopedPalettes)
    {
        const FbyPaletteExtensionDesc ScopedExtension = MakeExtension(ScopedPalette);
        (void)InHost->accessibility->register_palette_extension(InHost->user, &ScopedExtension);
    }
    return 0;
}

FBY_PLUGIN_ENTRY void fby_plugin_unload()
{
}
