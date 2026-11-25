// FemboyEngine2D - registry view of the existing accessibility profile.
#include "Editor/Infrastructure/AccessibilitySettings.hpp"

namespace Fby::Editor
{
namespace
{
void SetFlagSetting(Settings::Registry& InRegistry, AccessibilityProfile& InProfile, const char* InId,
                    const char* InLabel, std::uint32_t InFlag, bool InDefault)
{
    InRegistry.Bool(InId)
        .Category("Accessibility")
        .Label(InLabel)
        .Default(Settings::Value::MakeBool(InDefault))
        .Scope(Settings::Scope::Project)
        .Apply(
            [&InProfile, InFlag](const Settings::Value& InValue)
            { AccessibilitySetFlag(&InProfile.runtime.flags, InFlag, InValue.BoolValue); });
}
}

void RegisterAccessibilitySettings(Settings::Registry& InRegistry, AccessibilityProfile& InProfile)
{
    SetFlagSetting(InRegistry, InProfile, "accessibility.deaf", "Deaf mode", AccessibilityDeafMode,
                   AccessibilityHasFlag(InProfile.runtime.flags, AccessibilityDeafMode));
    SetFlagSetting(InRegistry, InProfile, "accessibility.flash_guard", "Flash guard", AccessibilityFlashGuard,
                   AccessibilityHasFlag(InProfile.runtime.flags, AccessibilityFlashGuard));
    InRegistry.Bool("accessibility.subtitles")
        .Category("Accessibility")
        .Label("Subtitles")
        .Default(Settings::Value::MakeBool(InProfile.runtime.showSubtitles))
        .Scope(Settings::Scope::Project)
        .Apply([&InProfile](const Settings::Value& InValue) { InProfile.runtime.showSubtitles = InValue.BoolValue; });
    InRegistry.Float("accessibility.flash_threshold")
        .Category("Accessibility")
        .Label("Flash threshold")
        .Default(Settings::Value::MakeFloat(InProfile.runtime.flashThreshold))
        .Range(0.02, 0.45)
        .Scope(Settings::Scope::Project)
        .Apply([&InProfile](const Settings::Value& InValue)
               { InProfile.runtime.flashThreshold = static_cast<float>(InValue.FloatValue); });
    InRegistry.Float("accessibility.flash_gain")
        .Category("Accessibility")
        .Label("Flash residual gain")
        .Default(Settings::Value::MakeFloat(InProfile.runtime.flashGain))
        .Range(0.05, 1.0)
        .Scope(Settings::Scope::Project)
        .Apply([&InProfile](const Settings::Value& InValue)
               { InProfile.runtime.flashGain = static_cast<float>(InValue.FloatValue); });
    InRegistry.Float("accessibility.minimum_contrast")
        .Category("Accessibility")
        .Label("Minimum contrast")
        .Default(Settings::Value::MakeFloat(InProfile.presentation.minimumContrastRatio))
        .Range(1.0, 21.0)
        .Scope(Settings::Scope::Project)
        .Apply([&InProfile](const Settings::Value& InValue)
               { InProfile.presentation.minimumContrastRatio = static_cast<float>(InValue.FloatValue); });
    InRegistry.Float("accessibility.motion_scale")
        .Category("Accessibility")
        .Label("Motion scale")
        .Default(Settings::Value::MakeFloat(InProfile.presentation.motionScale))
        .Range(0.0, 1.0)
        .Scope(Settings::Scope::Project)
        .Apply([&InProfile](const Settings::Value& InValue)
               { InProfile.presentation.motionScale = static_cast<float>(InValue.FloatValue); });
    InRegistry.Bool("accessibility.oled_shift")
        .Category("Accessibility")
        .Label("OLED pixel shift")
        .Default(Settings::Value::MakeBool(InProfile.presentation.oledPixelShift))
        .Scope(Settings::Scope::Project)
        .Apply([&InProfile](const Settings::Value& InValue)
               { InProfile.presentation.oledPixelShift = InValue.BoolValue; });
}

}