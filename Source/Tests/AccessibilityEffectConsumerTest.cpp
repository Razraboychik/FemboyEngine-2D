// FemboyEngine2D - effect resource to post-FX settings mapping.
#include "Engine/UI/AccessibilityEffectConsumer.hpp"

#include <cstdio>
#include <cstring>

namespace
{
int GFailures = 0;

void Check(bool InCondition, const char* InMessage)
{
    if (!InCondition)
    {
        std::printf("FAIL: %s\n", InMessage);
        ++GFailures;
    }
}
}

int main()
{
    using namespace Fby;
    Ui::AccessibilityEffectProfile ScopedNvg{};
    std::snprintf(ScopedNvg.Id, sizeof(ScopedNvg.Id), "%s", "Test.Nvg");
    ScopedNvg.Kind = Ui::AccessibilityEffectKind::Nvg;
    ScopedNvg.Intensity = 0.75f;
    ScopedNvg.Tint = Color32::FromBytes(0x40, 0xFF, 0x40);
    ScopedNvg.Pattern = Ui::NvgPattern::Scanlines;
    Rhi::PostFxSettings ScopedSettings{};
    Ui::ApplyAccessibilityEffect(ScopedNvg, ScopedSettings);
    Check(ScopedSettings.accessibility.kind == 1, "NVG maps to post-FX kind 1");
    Check((ScopedSettings.flags & Rhi::PostFxAccessibilityEffectEnabled) != 0, "NVG enables the composite");
    Check(ScopedSettings.accessibility.tint == ScopedNvg.Tint.rgba, "NVG tint is forwarded");

    Ui::AccessibilityEffectProfile ScopedThermal{};
    std::snprintf(ScopedThermal.Id, sizeof(ScopedThermal.Id), "%s", "Test.Thermal");
    ScopedThermal.Kind = Ui::AccessibilityEffectKind::Thermal;
    for (std::size_t ScopedI = 0; ScopedI < Ui::kAccessibilityTemperatureBands; ++ScopedI)
    {
        ScopedThermal.Temperatures[ScopedI].Color = Color32::FromBytes(ScopedI * 40, ScopedI * 30, 255 - ScopedI * 30);
    }
    Ui::ApplyAccessibilityEffect(ScopedThermal, ScopedSettings);
    Check(ScopedSettings.accessibility.kind == 2, "thermal maps to post-FX kind 2");
    Check(ScopedSettings.accessibility.palette[0] == ScopedThermal.Temperatures[0].Color.rgba,
          "thermal palette is forwarded");

    Ui::AccessibilityEffectProfile ScopedAlcohol{};
    std::snprintf(ScopedAlcohol.Id, sizeof(ScopedAlcohol.Id), "%s", "Test.Alcohol");
    ScopedAlcohol.Kind = Ui::AccessibilityEffectKind::Alcohol;
    ScopedAlcohol.AlcoholEffectCount = 2;
    ScopedAlcohol.AlcoholEffects[0].Effect = Ui::AlcoholEffect::DoubleVision;
    ScopedAlcohol.AlcoholEffects[0].Intensity = 0.5f;
    ScopedAlcohol.AlcoholEffects[1].Effect = Ui::AlcoholEffect::Wobble;
    ScopedAlcohol.AlcoholEffects[1].Intensity = 0.7f;
    ScopedAlcohol.AlcoholEffects[1].Frequency = 0.8f;
    Ui::ApplyAccessibilityEffect(ScopedAlcohol, ScopedSettings);
    Check(ScopedSettings.accessibility.kind == 3, "alcohol maps to post-FX kind 3");
    Check(ScopedSettings.accessibility.doubleVision == 0.5f, "double vision is forwarded");
    Check(ScopedSettings.accessibility.wobble == 0.7f, "wobble is forwarded");
    Check(ScopedSettings.accessibility.wobbleFrequency == 0.8f, "wobble frequency is forwarded");

    if (GFailures != 0)
    {
        std::printf("%d effect consumer failure(s)\n", GFailures);
        return 1;
    }
    std::printf("accessibility effect consumer tests passed\n");
    return 0;
}
