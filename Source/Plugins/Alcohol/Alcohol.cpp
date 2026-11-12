// FemboyEngine2D - alcohol/drunk vision effect pool.
#include "Engine/Core/PluginAPI.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace
{

void SetTag(char (&InTag)[64], const char* InText)
{
    std::snprintf(InTag, sizeof(InTag), "%s", InText);
}

FbyAlcoholEffectDesc Layer(std::uint32_t InEffect, float InIntensity, float InFrequency)
{
    return FbyAlcoholEffectDesc{InEffect, InIntensity, InFrequency};
}

FbyEffectDesc MakeProfile(const char* InId, const char* InName, float InIntensity,
                          const std::array<FbyAlcoholEffectDesc, FBY_ALCOHOL_EFFECTS>& InLayers,
                          std::uint32_t InCount)
{
    FbyEffectDesc ScopedEffect{};
    ScopedEffect.struct_size = static_cast<std::uint32_t>(sizeof(FbyEffectDesc));
    ScopedEffect.id = InId;
    ScopedEffect.display_name = InName;
    ScopedEffect.kind = 2;
    ScopedEffect.intensity = InIntensity;
    ScopedEffect.blur = 0.0f;
    ScopedEffect.sharpness = 0.0f;
    ScopedEffect.tint = 0xFFFFFFFFu;
    ScopedEffect.alcohol_effect_count = InCount;
    for (std::uint32_t ScopedI = 0; ScopedI < InCount; ++ScopedI)
    {
        ScopedEffect.alcohol_effects[ScopedI] = InLayers[ScopedI];
    }
    ScopedEffect.tag_count = 3;
    SetTag(ScopedEffect.tags[0], "State.Alcohol");
    SetTag(ScopedEffect.tags[1], InId);
    SetTag(ScopedEffect.tags[2], "Accessibility.Vision.Alcohol");
    return ScopedEffect;
}
}

FBY_PLUGIN_ENTRY const FbyPluginInfo* fby_plugin_info()
{
    static const FbyPluginInfo ScopedInfo{static_cast<std::uint32_t>(sizeof(FbyPluginInfo)), FBY_PLUGIN_ABI_VERSION,
                                          "Alcohol Vision", FBY_ENGINE_VERSION, "FemboyEngine2D contributors", 0,
                                          nullptr, "Alcohol double vision, sway, blur and vignette pool"};
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
    const std::array<FbyAlcoholEffectDesc, FBY_ALCOHOL_EFFECTS> ScopedTipsy = {
        Layer(0, 0.12f, 1.0f), Layer(1, 0.10f, 0.8f), Layer(2, 0.08f, 1.0f), Layer(3, 0.05f, 1.0f),
        Layer(4, 0.08f, 1.0f), {}, {}, {}, {}, {}};
    const std::array<FbyAlcoholEffectDesc, FBY_ALCOHOL_EFFECTS> ScopedDrunk = {
        Layer(0, 0.42f, 1.0f), Layer(1, 0.48f, 0.7f), Layer(2, 0.30f, 1.0f), Layer(3, 0.24f, 1.2f),
        Layer(4, 0.28f, 1.0f), Layer(5, 0.18f, 1.0f), Layer(6, 0.14f, 0.9f), {}, {}, {}};
    const std::array<FbyAlcoholEffectDesc, FBY_ALCOHOL_EFFECTS> ScopedWasted = {
        Layer(0, 0.82f, 1.1f), Layer(1, 0.90f, 0.6f), Layer(2, 0.62f, 1.0f), Layer(3, 0.52f, 1.3f),
        Layer(4, 0.45f, 1.0f), Layer(5, 0.42f, 1.0f), Layer(6, 0.38f, 0.8f), Layer(7, 0.34f, 0.7f),
        Layer(8, 0.30f, 0.5f), Layer(9, 0.28f, 0.4f)};
    const std::array<FbyAlcoholEffectDesc, FBY_ALCOHOL_EFFECTS> ScopedStalker = {
        Layer(0, 0.28f, 0.8f), Layer(1, 0.62f, 0.55f), Layer(2, 0.34f, 1.0f), Layer(3, 0.20f, 1.0f),
        Layer(4, 0.36f, 0.8f), Layer(5, 0.12f, 1.0f), Layer(7, 0.20f, 0.6f), {}, {}, {}};
    const std::array<FbyEffectDesc, 4> ScopedEffects = {
        MakeProfile("Alcohol.Tipsy", "Alcohol - tipsy", 0.35f, ScopedTipsy, 5),
        MakeProfile("Alcohol.Drunk", "Alcohol - drunk", 0.7f, ScopedDrunk, 7),
        MakeProfile("Alcohol.Wasted", "Alcohol - wasted", 1.0f, ScopedWasted, 10),
        MakeProfile("Alcohol.Stalker", "Alcohol - stalker sway", 0.8f, ScopedStalker, 7),
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
