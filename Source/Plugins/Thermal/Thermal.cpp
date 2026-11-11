// FemboyEngine2D - thermal device palettes and five-band tag mapping.
#include "Engine/Core/PluginAPI.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace
{
FbyHostApi* GHost = nullptr;

void SetTag(char (&InTag)[64], const char* InText)
{
    std::snprintf(InTag, sizeof(InTag), "%s", InText);
}

FbyEffectDesc MakeThermal(const char* InId, const char* InName, float InBlur, float InSharpness,
                          const std::array<std::uint32_t, 5>& InColors)
{
    FbyEffectDesc ScopedEffect{};
    ScopedEffect.struct_size = static_cast<std::uint32_t>(sizeof(FbyEffectDesc));
    ScopedEffect.id = InId;
    ScopedEffect.display_name = InName;
    ScopedEffect.kind = 1;
    ScopedEffect.intensity = 1.0f;
    ScopedEffect.blur = InBlur;
    ScopedEffect.sharpness = InSharpness;
    ScopedEffect.pattern = 0;
    ScopedEffect.pattern_scale = 1.0f;
    ScopedEffect.generation = 1;
    ScopedEffect.tint = 0xFFFFFFFFu;
    ScopedEffect.tag_count = 2;
    std::snprintf(ScopedEffect.tags[0], sizeof(ScopedEffect.tags[0]), "Vision.Thermal.Device");
    std::snprintf(ScopedEffect.tags[1], sizeof(ScopedEffect.tags[1]), "Accessibility.Vision.Thermal");
    const std::array<const char*, 5> ScopedTags = {"Temperature.VeryCold", "Temperature.Cold", "Temperature.Neutral",
                                                    "Temperature.Hot", "Temperature.VeryHot"};
    for (std::size_t ScopedI = 0; ScopedI < 5; ++ScopedI)
    {
        SetTag(ScopedEffect.temperatures[ScopedI].tag, ScopedTags[ScopedI]);
        ScopedEffect.temperatures[ScopedI].color = InColors[ScopedI];
    }
    return ScopedEffect;
}
}

FBY_PLUGIN_ENTRY const FbyPluginInfo* fby_plugin_info()
{
    static const FbyPluginInfo ScopedInfo{static_cast<std::uint32_t>(sizeof(FbyPluginInfo)), FBY_PLUGIN_ABI_VERSION,
                                          "Thermal", "0.4.0-alpha.1", "FemboyEngine2D contributors", 0, nullptr,
                                          "Thermal palettes, five temperature tags and device blur"};
    return &ScopedInfo;
}

FBY_PLUGIN_ENTRY int fby_plugin_load(FbyHostApi* InHost)
{
    if (InHost == nullptr || InHost->abi_version != FBY_PLUGIN_ABI_VERSION || InHost->struct_size < sizeof(FbyHostApi))
    {
        return 1;
    }
    GHost = InHost;
    if (InHost->accessibility == nullptr || InHost->accessibility->register_effect == nullptr)
    {
        return 0;
    }
    const std::array<std::uint32_t, 5> ScopedStandard = {0xFF000000u, 0xFF203080u, 0xFFB0B0B0u, 0xFFFF8A20u,
                                                          0xFFFFFFFFu};
    const std::array<std::uint32_t, 5> ScopedHighContrast = {0xFF000000u, 0xFF00C8FFu, 0xFFFFFF00u, 0xFFFF6000u,
                                                              0xFFFFFFFFu};
    const std::array<std::uint32_t, 5> ScopedGen1 = {0xFF080808u, 0xFF203070u, 0xFF909090u, 0xFFFFC040u,
                                                      0xFFFFFFFFu};
    const std::array<std::uint32_t, 5> ScopedIronbow = {0xFF200000u, 0xFF602060u, 0xFFC04040u, 0xFFFFB020u,
                                                          0xFFFFFFFFu};
    const std::array<std::uint32_t, 5> ScopedRainbow = {0xFF202060u, 0xFF20A0C0u, 0xFF40D060u, 0xFFFFD030u,
                                                          0xFFFF4040u};
    const std::array<std::uint32_t, 5> ScopedWhiteHot = {0xFF000000u, 0xFF404040u, 0xFF909090u, 0xFFD0D0D0u,
                                                          0xFFFFFFFFu};
    const std::array<std::uint32_t, 5> ScopedBlackHot = {0xFFFFFFFFu, 0xFFB0B0B0u, 0xFF606060u, 0xFF202020u,
                                                          0xFF000000u};
    const std::array<FbyEffectDesc, 7> ScopedEffects = {
        MakeThermal("Thermal.Standard", "Thermal - standard", 1.5f, 1.0f, ScopedStandard),
        MakeThermal("Thermal.HighContrast", "Thermal - high contrast", 0.0f, 2.0f, ScopedHighContrast),
        MakeThermal("Thermal.Ironbow", "Thermal - ironbow", 0.8f, 1.4f, ScopedIronbow),
        MakeThermal("Thermal.Rainbow", "Thermal - rainbow", 0.8f, 1.4f, ScopedRainbow),
        MakeThermal("Thermal.WhiteHot", "Thermal - white hot", 1.2f, 1.2f, ScopedWhiteHot),
        MakeThermal("Thermal.BlackHot", "Thermal - black hot", 1.2f, 1.2f, ScopedBlackHot),
        MakeThermal("Thermal.Gen1", "Thermal - generation 1", 4.0f, 0.4f, ScopedGen1),
    };
    for (const FbyEffectDesc& ScopedEffect : ScopedEffects)
    {
        (void)InHost->accessibility->register_effect(InHost->user, &ScopedEffect);
    }
    return 0;
}

FBY_PLUGIN_ENTRY void fby_plugin_unload()
{
    GHost = nullptr;
}
