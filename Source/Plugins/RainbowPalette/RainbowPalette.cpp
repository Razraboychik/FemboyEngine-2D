// FemboyEngine2D - Rainbow Palette: 48-colour extension pack.
#include "Engine/Core/PluginAPI.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace
{
FbyHostApi* GHost = nullptr;

FbyPaletteExtensionDesc MakePalette()
{
    FbyPaletteExtensionDesc ScopedPalette{};
    ScopedPalette.struct_size = static_cast<std::uint32_t>(sizeof(FbyPaletteExtensionDesc));
    ScopedPalette.id = "Rainbow.Palette80";
    ScopedPalette.display_name = "Rainbow Palette 80";
    // 8 hue families x 8 luminance steps, then 8 light and 8 dark neutral
    // steps including white and black: 64 + 16 = 80 colours.
    constexpr std::array<std::uint32_t, 8> kHues = {0xFF4040u, 0xFF8A30u, 0xFFD040u, 0x40D070u,
                                                     0x40D0D0u, 0x4070FFu, 0xA050FFu, 0xFF50A0u};
    for (std::size_t ScopedHue = 0; ScopedHue < kHues.size(); ++ScopedHue)
    {
        const std::uint32_t ScopedBase = kHues[ScopedHue];
        for (std::size_t ScopedStep = 0; ScopedStep < 8; ++ScopedStep)
        {
            const std::uint32_t ScopedScale = static_cast<std::uint32_t>(28 + ScopedStep * 28);
            const auto ScopedChannel = [ScopedBase, ScopedScale](std::uint32_t InShift)
            {
                const std::uint32_t ScopedValue = (ScopedBase >> InShift) & 0xFFu;
                return std::min<std::uint32_t>(255u, (ScopedValue * ScopedScale) / 220u);
            };
            const std::uint32_t ScopedColor = 0xFF000000u | (ScopedChannel(16) << 16u) |
                                              (ScopedChannel(8) << 8u) | ScopedChannel(0);
            ScopedPalette.colors[ScopedHue * 8 + ScopedStep] = ScopedColor;
        }
    }
    for (std::size_t ScopedStep = 0; ScopedStep < 8; ++ScopedStep)
    {
        const std::uint32_t ScopedValue = 255u - static_cast<std::uint32_t>(ScopedStep * 30);
        ScopedPalette.colors[64 + ScopedStep] = 0xFF000000u | (ScopedValue << 16u) | (ScopedValue << 8u) | ScopedValue;
        const std::uint32_t ScopedDark = 210u - static_cast<std::uint32_t>(ScopedStep * 30);
        ScopedPalette.colors[72 + ScopedStep] = 0xFF000000u | (ScopedDark << 16u) | (ScopedDark << 8u) | ScopedDark;
    }
    return ScopedPalette;
}
}

FBY_PLUGIN_ENTRY const FbyPluginInfo* fby_plugin_info()
{
    static const FbyPluginInfo ScopedInfo{static_cast<std::uint32_t>(sizeof(FbyPluginInfo)), FBY_PLUGIN_ABI_VERSION,
                                          "Rainbow Palette", "0.4.0-alpha.1", "FemboyEngine2D contributors", 0, nullptr,
                                          "48-colour accessibility extension"};
    return &ScopedInfo;
}

FBY_PLUGIN_ENTRY int fby_plugin_load(FbyHostApi* InHost)
{
    if (InHost == nullptr || InHost->abi_version != FBY_PLUGIN_ABI_VERSION || InHost->struct_size < sizeof(FbyHostApi))
    {
        return 1;
    }
    GHost = InHost;
    if (InHost->accessibility == nullptr || InHost->accessibility->register_palette_extension == nullptr)
    {
        return 0;
    }
    const FbyPaletteExtensionDesc ScopedPalette = MakePalette();
    (void)InHost->accessibility->register_palette_extension(InHost->user, &ScopedPalette);
    return 0;
}

FBY_PLUGIN_ENTRY void fby_plugin_unload()
{
    GHost = nullptr;
}
