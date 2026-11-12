// FemboyEngine2D - radiation screen-noise presets.
#include "Engine/Core/PluginAPI.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace
{

FbyEffectDesc Make(const char* InId, const char* InName, float InIntensity, float InNoiseScale, float InScanline,
                   std::uint32_t InTint)
{
    FbyEffectDesc ScopedEffect{};
    ScopedEffect.struct_size = static_cast<std::uint32_t>(sizeof(FbyEffectDesc));
    ScopedEffect.id = InId;
    ScopedEffect.display_name = InName;
    ScopedEffect.kind = 3;
    ScopedEffect.intensity = InIntensity;
    ScopedEffect.pattern_scale = InNoiseScale;
    ScopedEffect.blur = InScanline;
    ScopedEffect.tint = InTint;
    ScopedEffect.tag_count = 3;
    std::snprintf(ScopedEffect.tags[0], sizeof(ScopedEffect.tags[0]), "Vision.Radiation");
    std::snprintf(ScopedEffect.tags[1], sizeof(ScopedEffect.tags[1]), "Accessibility.Vision.Radiation");
    std::snprintf(ScopedEffect.tags[2], sizeof(ScopedEffect.tags[2]), "State.Radiation");
    return ScopedEffect;
}
}

FBY_PLUGIN_ENTRY const FbyPluginInfo* fby_plugin_info()
{
    static const FbyPluginInfo ScopedInfo{static_cast<std::uint32_t>(sizeof(FbyPluginInfo)), FBY_PLUGIN_ABI_VERSION,
                                          "Radiation", FBY_ENGINE_VERSION, "FemboyEngine2D contributors", 0, nullptr,
                                          "Radiation noise and scanline presets"};
    return &ScopedInfo;
}

FBY_PLUGIN_ENTRY int fby_plugin_load(FbyHostApi* InHost)
{
    if (InHost == nullptr || InHost->abi_version != FBY_PLUGIN_ABI_VERSION || InHost->struct_size < sizeof(FbyHostApi))
    {
        return 1;
    }
    if (InHost->accessibility == nullptr || InHost->accessibility->register_effect == nullptr)
    {
        return 0;
    }
    const std::array<FbyEffectDesc, 3> ScopedEffects = {
        Make("Radiation.Low", "Radiation - low dose", 0.35f, 1.0f, 0.25f, 0xFF60D0A0u),
        Make("Radiation.High", "Radiation - high dose", 0.8f, 2.0f, 0.6f, 0xFF60E0A0u),
        Make("Radiation.Critical", "Radiation - critical", 1.0f, 4.0f, 0.9f, 0xFF80FFB0u),
    };
    for (const FbyEffectDesc& ScopedEffect : ScopedEffects)
    {
        (void)InHost->accessibility->register_effect(InHost->user, &ScopedEffect);
    }
    return 0;
}

FBY_PLUGIN_ENTRY void fby_plugin_unload()
{
}
