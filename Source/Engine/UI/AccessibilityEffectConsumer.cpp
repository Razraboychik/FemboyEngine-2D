// FemboyEngine2D - accessibility effect consumer.
#include "Engine/UI/AccessibilityEffectConsumer.hpp"

#include <algorithm>

namespace Fby::Ui
{
void ApplyAccessibilityEffect(const AccessibilityEffectProfile& InEffect, Rhi::PostFxSettings& InFrameSettings) noexcept
{
    Rhi::PostFxAccessibilityEffect ScopedEffect{};
    ScopedEffect.intensity = std::clamp(InEffect.Intensity, 0.0f, 1.0f);
    ScopedEffect.blur = std::clamp(InEffect.Blur, 0.0f, 16.0f);
    ScopedEffect.sharpness = std::clamp(InEffect.Sharpness, 0.0f, 4.0f);
    ScopedEffect.patternScale = std::clamp(InEffect.PatternScale, 0.1f, 8.0f);
    ScopedEffect.pattern = static_cast<std::uint32_t>(InEffect.Pattern);
    ScopedEffect.tint = InEffect.Tint.rgba;
    switch (InEffect.Kind)
    {
    case AccessibilityEffectKind::Nvg:
        ScopedEffect.kind = 1;
        break;
    case AccessibilityEffectKind::Thermal:
        ScopedEffect.kind = 2;
        for (std::size_t ScopedI = 0; ScopedI < ScopedEffect.palette.size(); ++ScopedI)
        {
            ScopedEffect.palette[ScopedI] = InEffect.Temperatures[ScopedI].Color.rgba;
        }
        break;
    case AccessibilityEffectKind::Alcohol:
        ScopedEffect.kind = 3;
        for (std::size_t ScopedI = 0; ScopedI < InEffect.AlcoholEffectCount; ++ScopedI)
        {
            const AlcoholEffectLayer& ScopedLayer = InEffect.AlcoholEffects[ScopedI];
            const float ScopedValue = std::clamp(ScopedLayer.Intensity, 0.0f, 1.0f);
            switch (ScopedLayer.Effect)
            {
            case AlcoholEffect::DoubleVision:
                ScopedEffect.doubleVision = std::max(ScopedEffect.doubleVision, ScopedValue);
                break;
            case AlcoholEffect::Wobble:
                ScopedEffect.wobble = std::max(ScopedEffect.wobble, ScopedValue);
                ScopedEffect.wobbleFrequency = std::max(ScopedEffect.wobbleFrequency, ScopedLayer.Frequency);
                break;
            case AlcoholEffect::Blur:
                ScopedEffect.blur = std::max(ScopedEffect.blur, ScopedValue * 8.0f);
                break;
            case AlcoholEffect::ChromaticAberration:
                ScopedEffect.chromatic = std::max(ScopedEffect.chromatic, ScopedValue);
                break;
            case AlcoholEffect::Vignette:
                ScopedEffect.vignette = std::max(ScopedEffect.vignette, ScopedValue);
                break;
            case AlcoholEffect::Desaturation:
                ScopedEffect.desaturation = std::max(ScopedEffect.desaturation, ScopedValue);
                break;
            case AlcoholEffect::Afterimage:
                ScopedEffect.afterimage = std::max(ScopedEffect.afterimage, ScopedValue);
                break;
            case AlcoholEffect::Pulse:
                ScopedEffect.pulse = std::max(ScopedEffect.pulse, ScopedValue);
                break;
            case AlcoholEffect::Sway:
                ScopedEffect.sway = std::max(ScopedEffect.sway, ScopedValue);
                break;
            case AlcoholEffect::ScaleBreathing:
                ScopedEffect.scaleBreathing = std::max(ScopedEffect.scaleBreathing, ScopedValue);
                break;
            }
        }
        break;
    }
    InFrameSettings.accessibility = ScopedEffect;
    if (ScopedEffect.kind != 0)
    {
        InFrameSettings.flags |= Rhi::PostFxAccessibilityEffectEnabled;
    }
    else
    {
        InFrameSettings.flags &= ~static_cast<std::uint32_t>(Rhi::PostFxAccessibilityEffectEnabled);
    }
}
}
