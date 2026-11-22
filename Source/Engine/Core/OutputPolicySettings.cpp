// FemboyEngine2D - developer-facing render and audio policy settings.
#include "Engine/Core/OutputPolicySettings.hpp"

#include <array>
#include <string_view>

namespace Fby::Settings
{
namespace
{
constexpr std::array<std::string_view, 4> kRenderPolicies{"full", "no_world", "disabled", "headless"};
constexpr std::array<std::string_view, 3> kAudioPolicies{"full", "captions_only", "disabled"};
}

void RegisterOutputPolicySettings(Registry& InRegistry)
{
    InRegistry.Enum("render.policy")
        .Category("Accessibility")
        .Label("Render output")
        .LocalizationKey("settings.render.policy")
        .Default(Value::MakeEnum("full"))
        .Safe(Value::MakeEnum("full"))
        .Enum(kRenderPolicies)
        .Scope(Scope::User)
        .Dangerous("Changing render output can hide the interface. The recovery hotkey remains active.",
                   "Ctrl+Alt+F12")
        .Apply(
            [](const Value& InValue)
            {
                Output::RenderPolicy ScopedPolicy = Output::RenderPolicy::Full;
                if (InValue.EnumValue == "no_world")
                {
                    ScopedPolicy = Output::RenderPolicy::NoWorld;
                }
                else if (InValue.EnumValue == "disabled")
                {
                    ScopedPolicy = Output::RenderPolicy::Disabled;
                }
                else if (InValue.EnumValue == "headless")
                {
                    ScopedPolicy = Output::RenderPolicy::Headless;
                }
                Output::Policy::Instance().SetRenderPolicy(ScopedPolicy);
            });

    InRegistry.Enum("audio.policy")
        .Category("Accessibility")
        .Label("Audio output")
        .LocalizationKey("settings.audio.policy")
        .Default(Value::MakeEnum("full"))
        .Safe(Value::MakeEnum("full"))
        .Enum(kAudioPolicies)
        .Scope(Scope::User)
        .Dangerous("Disabling audio requires working captions or visual cues.",
                   "Ctrl+Alt+F12")
        .Apply(
            [](const Value& InValue)
            {
                Output::AudioPolicy ScopedPolicy = Output::AudioPolicy::Full;
                if (InValue.EnumValue == "captions_only")
                {
                    ScopedPolicy = Output::AudioPolicy::CaptionsOnly;
                }
                else if (InValue.EnumValue == "disabled")
                {
                    ScopedPolicy = Output::AudioPolicy::Disabled;
                }
                Output::Policy::Instance().SetAudioPolicy(ScopedPolicy);
            });
}

}