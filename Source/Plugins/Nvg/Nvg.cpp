// FemboyEngine2D - NVG generation/pattern accessibility profile pack.
#include "Engine/Core/PluginAPI.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace
{

FbyEffectDesc MakeNvg(const char* InId, const char* InName, std::uint32_t InPattern, float InIntensity,
                      float InPatternScale, std::uint32_t InGeneration)
{
    FbyEffectDesc ScopedEffect{};
    ScopedEffect.struct_size = static_cast<std::uint32_t>(sizeof(FbyEffectDesc));
    ScopedEffect.id = InId;
    ScopedEffect.display_name = InName;
    ScopedEffect.kind = 0;
    ScopedEffect.intensity = InIntensity;
    ScopedEffect.pattern = InPattern;
    ScopedEffect.pattern_scale = InPatternScale;
    ScopedEffect.generation = InGeneration;
    ScopedEffect.blur = 0.2f;
    ScopedEffect.sharpness = 1.0f;
    ScopedEffect.tint = 0xFF40FF40u;
    ScopedEffect.tag_count = 3;
    std::snprintf(ScopedEffect.tags[0], sizeof(ScopedEffect.tags[0]), "Vision.Nvg.Generation%u", InGeneration);
    std::snprintf(ScopedEffect.tags[1], sizeof(ScopedEffect.tags[1]), "Vision.Nvg.Pattern%u", InPattern);
    std::snprintf(ScopedEffect.tags[2], sizeof(ScopedEffect.tags[2]), "Accessibility.Vision.LowLight");
    return ScopedEffect;
}
}

FBY_PLUGIN_ENTRY const FbyPluginInfo* fby_plugin_info()
{
    static const FbyPluginInfo ScopedInfo{static_cast<std::uint32_t>(sizeof(FbyPluginInfo)), FBY_PLUGIN_ABI_VERSION,
                                          "NVG", FBY_ENGINE_VERSION, "FemboyEngine2D contributors", 0, nullptr,
                                          "NVG generations, patterns and intensity presets"};
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
    const std::array<FbyEffectDesc, 4> ScopedEffects = {
        MakeNvg("Nvg.Gen1.Scanlines", "NVG generation 1 - scanlines", 1, 0.72f, 1.0f, 1),
        MakeNvg("Nvg.Gen2.Grid", "NVG generation 2 - grid", 2, 0.84f, 1.4f, 2),
        MakeNvg("Nvg.Gen3.Noise", "NVG generation 3 - noise", 3, 0.94f, 0.8f, 3),
        MakeNvg("Nvg.Gen4.Clean", "NVG generation 4 - clean", 0, 1.0f, 1.0f, 4),
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
