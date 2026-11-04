// FemboyEngine2D - player-owned render/window settings.
#pragma once

#include "Editor/Infrastructure/ProjectConfig.hpp"
#include "Engine/RHI/DlssAdapter.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace Fby::Editor
{

// Player settings are intentionally separate from ProjectSettings: they live in
// a per-install file and never alter the project's art/pipeline policy.
struct UserSettings
{
    std::uint32_t windowWidth = 1600;
    std::uint32_t windowHeight = 900;
    // 0 windowed, 1 borderless, 2 exclusive
    std::uint8_t windowMode = 1;
    bool vsync = true;
    // 0 low, 1 medium, 2 high
    std::uint8_t qualityPreset = 2;
    float renderScale = 1.0f;
    UpscaleMode upscaleMode = UpscaleMode::Bilinear;
    TemporalUpscaleMode temporalUpscale = TemporalUpscaleMode::Off;
    float fsr1Sharpness = 0.35f;
    bool oitEnabled = true;
    float hudRenderScale = 1.0f;
    UpscaleMode hudUpscale = UpscaleMode::Nearest;
    HudTargetMode hudTarget = HudTargetMode::Auto;
    bool hudVisible = true;
    FrameGenerationMode frameGeneration = FrameGenerationMode::Off;
    FrameGenerationPolicySetting frameGenerationPolicy = FrameGenerationPolicySetting::Auto;
    UpscalerBackendSetting upscalerBackend = UpscalerBackendSetting::Auto;
    UpscalePolicy upscalePolicy = UpscalePolicy::Auto;
    Rhi::DlssQualityMode dlssQuality = Rhi::DlssQualityMode::Quality;

    [[nodiscard]] friend bool operator==(const UserSettings&, const UserSettings&) noexcept = default;
};

[[nodiscard]] bool LoadUserSettings(std::string_view InPath, UserSettings* OutSettings,
                                    std::string* InError = nullptr);
[[nodiscard]] bool SaveUserSettings(std::string_view InPath, const UserSettings& InSettings,
                                    std::string* InError = nullptr);

[[nodiscard]] UserSettings ClampUserSettings(UserSettings InSettings) noexcept;

}
