// FemboyEngine2D - reusable player render settings UI.
#pragma once

#include "Editor/Infrastructure/SettingsSchema.hpp"
#include "Engine/RHI/BackendCapabilities.hpp"
#include "Editor/Infrastructure/UserSettings.hpp"

#include <algorithm>
#include <imgui.h>

namespace Fby::Editor
{

// The same component is used by the editor preview and by a game settings menu.
// Values are player-owned; project policy is resolved separately.
class UserSettingsPanel
{
public:
    [[nodiscard]] static bool Draw(UserSettings& InSettings,
                                   const Rhi::BackendCapabilities* InCaps = nullptr)
    {
        bool ScopedChanged = false;
        ImGui::SeparatorText("Player rendering");

        int ScopedWindowMode = static_cast<int>(InSettings.windowMode);
        const char* ScopedWindowModes[3] = {"Windowed", "Borderless", "Exclusive fullscreen"};
        if (ImGui::Combo("Window mode", &ScopedWindowMode, ScopedWindowModes, 3))
        {
            InSettings.windowMode = static_cast<std::uint8_t>(ScopedWindowMode);
            ScopedChanged = true;
        }
        Tooltip("window_mode");

        int ScopedQuality = static_cast<int>(InSettings.qualityPreset);
        const char* ScopedQualities[3] = {"Low", "Medium", "High"};
        if (ImGui::Combo("Quality preset", &ScopedQuality, ScopedQualities, 3))
        {
            InSettings.qualityPreset = static_cast<std::uint8_t>(ScopedQuality);
            ScopedChanged = true;
        }
        Tooltip("quality_preset");

        if (ImGui::SliderFloat("FSR1 sharpness", &InSettings.fsr1Sharpness, 0.0f, 1.0f, "%.2f"))
        {
            ScopedChanged = true;
        }
        Tooltip("fsr1_sharpness");

        if (ImGui::SliderFloat("Render scale", &InSettings.renderScale, 0.25f, 1.0f, "%.2f"))
        {
            ScopedChanged = true;
        }
        Tooltip("render_scale");

        int ScopedPolicy = static_cast<int>(InSettings.upscalePolicy);
        const char* ScopedPolicies[3] = {"Auto", "Universal (FSR)", "Off"};
        if (ImGui::Combo("Upscaler policy", &ScopedPolicy, ScopedPolicies, 3))
        {
            InSettings.upscalePolicy = static_cast<UpscalePolicy>(ScopedPolicy);
            ScopedChanged = true;
        }
        Tooltip("upscale_policy");
        if (InCaps != nullptr && InCaps->Dlss &&
            InSettings.upscalePolicy == UpscalePolicy::Auto)
        {
            int ScopedDlssQuality = static_cast<int>(InSettings.dlssQuality);
            const char* ScopedDlssQualities[4] = {"Ultra Performance", "Performance", "Balanced", "Quality"};
            if (ImGui::Combo("DLSS quality", &ScopedDlssQuality, ScopedDlssQualities, 4))
            {
                InSettings.dlssQuality = static_cast<Rhi::DlssQualityMode>(ScopedDlssQuality);
                ScopedChanged = true;
            }
            Tooltip("dlss_quality");
        }

        if (ImGui::Checkbox("Order-independent transparency", &InSettings.oitEnabled))
        {
            ScopedChanged = true;
        }
        Tooltip("oit_enabled");

        int ScopedFrameGenerationPolicy = static_cast<int>(InSettings.frameGenerationPolicy);
        const char* ScopedFrameGenerationPolicies[3] = {"Auto", "Universal (FSR3/custom)", "Off"};
        if (ImGui::Combo("Frame generation policy", &ScopedFrameGenerationPolicy,
                         ScopedFrameGenerationPolicies, 3))
        {
            InSettings.frameGenerationPolicy =
                static_cast<FrameGenerationPolicySetting>(ScopedFrameGenerationPolicy);
            ScopedChanged = true;
        }
        Tooltip("frame_generation_policy");
        if (ImGui::Checkbox("VSync", &InSettings.vsync))
        {
            ScopedChanged = true;
        }

        ImGui::SeparatorText("HUD");
        if (ImGui::Checkbox("Show HUD", &InSettings.hudVisible))
        {
            ScopedChanged = true;
        }
        if (ImGui::SliderFloat("HUD render scale", &InSettings.hudRenderScale, 0.25f, 1.0f, "%.2f"))
        {
            ScopedChanged = true;
        }
        Tooltip("hud_render_scale");
        int ScopedHudTarget = static_cast<int>(InSettings.hudTarget);
        const char* ScopedHudTargets[3] = {"Direct", "Offscreen", "Auto"};
        if (ImGui::Combo("HUD target", &ScopedHudTarget, ScopedHudTargets, 3))
        {
            InSettings.hudTarget = static_cast<HudTargetMode>(ScopedHudTarget);
            ScopedChanged = true;
        }
        Tooltip("hud_target");
        int ScopedHudUpscale = static_cast<int>(InSettings.hudUpscale);
        const char* ScopedHudUpscales[2] = {"Nearest", "Bilinear"};
        if (ImGui::Combo("HUD upscale", &ScopedHudUpscale, ScopedHudUpscales, 2))
        {
            InSettings.hudUpscale = static_cast<UpscaleMode>(ScopedHudUpscale);
            ScopedChanged = true;
        }
        Tooltip("hud_upscale");

        if (ScopedChanged)
        {
            InSettings = ClampUserSettings(InSettings);
        }
        return ScopedChanged;
    }

private:
    static void Tooltip(std::string_view InKey)
    {
        const SettingDesc* ScopedDesc = FindSettingDesc(InKey);
        if (ScopedDesc != nullptr && ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("%.*s", static_cast<int>(ScopedDesc->tooltip.size()), ScopedDesc->tooltip.data());
        }
    }
};

}
