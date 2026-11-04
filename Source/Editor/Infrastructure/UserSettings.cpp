// FemboyEngine2D - player settings persistence.
#include "Editor/Infrastructure/UserSettings.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <system_error>

#if defined(TOML_ENABLE_FORMATTERS)
#undef TOML_ENABLE_FORMATTERS
#endif
#define TOML_ENABLE_FORMATTERS 1
#include <toml.hpp>

namespace Fby::Editor
{
namespace
{
void SetError(std::string* InError, const std::string& InMessage)
{
    if (InError != nullptr)
    {
        *InError = InMessage;
    }
}

template <typename T>
[[nodiscard]] T ValueField(const toml::table& InTable, std::string_view InKey, T InFallback)
{
    const auto* ScopedNode = InTable.get(InKey);
    return ScopedNode != nullptr ? ScopedNode->value<T>().value_or(InFallback) : InFallback;
}
}

UserSettings ClampUserSettings(UserSettings InSettings) noexcept
{
    InSettings.windowWidth = std::clamp(InSettings.windowWidth, 320u, 16384u);
    InSettings.windowHeight = std::clamp(InSettings.windowHeight, 240u, 8640u);
    InSettings.windowMode = static_cast<std::uint8_t>(std::min<std::uint8_t>(InSettings.windowMode, 2u));
    InSettings.qualityPreset = static_cast<std::uint8_t>(std::min<std::uint8_t>(InSettings.qualityPreset, 2u));
    InSettings.renderScale = std::clamp(InSettings.renderScale, 0.25f, 1.0f);
    InSettings.upscaleMode = static_cast<UpscaleMode>(
        std::clamp(static_cast<std::uint8_t>(InSettings.upscaleMode), std::uint8_t{0}, std::uint8_t{4}));
    InSettings.temporalUpscale = static_cast<TemporalUpscaleMode>(
        std::clamp(static_cast<std::uint8_t>(InSettings.temporalUpscale), std::uint8_t{0}, std::uint8_t{3}));
    InSettings.fsr1Sharpness = std::clamp(InSettings.fsr1Sharpness, 0.0f, 1.0f);
    InSettings.hudRenderScale = std::clamp(InSettings.hudRenderScale, 0.25f, 1.0f);
    InSettings.hudUpscale = static_cast<UpscaleMode>(
        std::clamp(static_cast<std::uint8_t>(InSettings.hudUpscale), std::uint8_t{0}, std::uint8_t{1}));
    InSettings.hudTarget = static_cast<HudTargetMode>(
        std::clamp(static_cast<std::uint8_t>(InSettings.hudTarget), std::uint8_t{0}, std::uint8_t{2}));
    InSettings.frameGeneration = static_cast<FrameGenerationMode>(
        std::clamp(static_cast<std::uint8_t>(InSettings.frameGeneration), std::uint8_t{0}, std::uint8_t{4}));
    InSettings.upscalerBackend = static_cast<UpscalerBackendSetting>(
        std::clamp(static_cast<std::uint8_t>(InSettings.upscalerBackend), std::uint8_t{0}, std::uint8_t{5}));
    InSettings.frameGenerationPolicy = static_cast<FrameGenerationPolicySetting>(
        std::clamp(static_cast<std::uint8_t>(InSettings.frameGenerationPolicy), std::uint8_t{0}, std::uint8_t{2}));
    InSettings.upscalePolicy = static_cast<UpscalePolicy>(
        std::clamp(static_cast<std::uint8_t>(InSettings.upscalePolicy), std::uint8_t{0}, std::uint8_t{2}));
    InSettings.dlssQuality = static_cast<Rhi::DlssQualityMode>(
        std::clamp(static_cast<std::uint8_t>(InSettings.dlssQuality), std::uint8_t{0}, std::uint8_t{3}));
    return InSettings;
}

bool LoadUserSettings(std::string_view InPath, UserSettings* OutSettings, std::string* InError)
{
    if (OutSettings == nullptr)
    {
        SetError(InError, "UserSettings output is null");
        return false;
    }
    try
    {
        const toml::table ScopedTable = toml::parse_file(std::string(InPath));
        const toml::table* ScopedGraphics = ScopedTable["graphics"].as_table();
        if (ScopedGraphics == nullptr)
        {
            SetError(InError, "user settings have no [graphics] table");
            return false;
        }
        UserSettings ScopedSettings;
        ScopedSettings.windowWidth = static_cast<std::uint32_t>(
            ValueField<std::int64_t>(*ScopedGraphics, "window_width", ScopedSettings.windowWidth));
        ScopedSettings.windowHeight = static_cast<std::uint32_t>(
            ValueField<std::int64_t>(*ScopedGraphics, "window_height", ScopedSettings.windowHeight));
        ScopedSettings.windowMode = static_cast<std::uint8_t>(
            ValueField<std::int64_t>(*ScopedGraphics, "window_mode", ScopedSettings.windowMode));
        ScopedSettings.vsync = ValueField<bool>(*ScopedGraphics, "vsync", ScopedSettings.vsync);
        ScopedSettings.qualityPreset = static_cast<std::uint8_t>(
            ValueField<std::int64_t>(*ScopedGraphics, "quality_preset", ScopedSettings.qualityPreset));
        ScopedSettings.renderScale = static_cast<float>(
            ValueField<double>(*ScopedGraphics, "render_scale", ScopedSettings.renderScale));
        ScopedSettings.upscaleMode = static_cast<UpscaleMode>(
            ValueField<std::int64_t>(*ScopedGraphics, "upscale_mode", static_cast<std::int64_t>(ScopedSettings.upscaleMode)));
        ScopedSettings.temporalUpscale = static_cast<TemporalUpscaleMode>(ValueField<std::int64_t>(
            *ScopedGraphics, "temporal_upscale", static_cast<std::int64_t>(ScopedSettings.temporalUpscale)));
        ScopedSettings.oitEnabled =
            ValueField<bool>(*ScopedGraphics, "oit_enabled", ScopedSettings.oitEnabled);
        ScopedSettings.fsr1Sharpness = static_cast<float>(
            ValueField<double>(*ScopedGraphics, "fsr1_sharpness", ScopedSettings.fsr1Sharpness));
        if (const toml::table* ScopedHud = ScopedTable["hud"].as_table())
        {
            ScopedSettings.hudRenderScale = static_cast<float>(
                ValueField<double>(*ScopedHud, "render_scale", ScopedSettings.hudRenderScale));
            ScopedSettings.hudUpscale = static_cast<UpscaleMode>(ValueField<std::int64_t>(
                *ScopedHud, "upscale", static_cast<std::int64_t>(ScopedSettings.hudUpscale)));
            ScopedSettings.hudVisible = ValueField<bool>(*ScopedHud, "visible", ScopedSettings.hudVisible);
            ScopedSettings.hudTarget = static_cast<HudTargetMode>(ValueField<std::int64_t>(
                *ScopedHud, "target", static_cast<std::int64_t>(ScopedSettings.hudTarget)));
            ScopedSettings.frameGeneration = static_cast<FrameGenerationMode>(ValueField<std::int64_t>(
                *ScopedHud, "frame_generation", static_cast<std::int64_t>(ScopedSettings.frameGeneration)));
            ScopedSettings.upscalerBackend = static_cast<UpscalerBackendSetting>(ValueField<std::int64_t>(
                *ScopedHud, "upscaler_backend", static_cast<std::int64_t>(ScopedSettings.upscalerBackend)));
            ScopedSettings.frameGenerationPolicy = static_cast<FrameGenerationPolicySetting>(ValueField<std::int64_t>(
                *ScopedHud, "frame_generation_policy",
                static_cast<std::int64_t>(ScopedSettings.frameGenerationPolicy)));
            ScopedSettings.upscalePolicy = static_cast<UpscalePolicy>(ValueField<std::int64_t>(
                *ScopedHud, "upscale_policy", static_cast<std::int64_t>(ScopedSettings.upscalePolicy)));
            ScopedSettings.dlssQuality = static_cast<Rhi::DlssQualityMode>(ValueField<std::int64_t>(
                *ScopedHud, "dlss_quality", static_cast<std::int64_t>(ScopedSettings.dlssQuality)));
        }
        *OutSettings = ClampUserSettings(ScopedSettings);
        return true;
    }
    catch (const toml::parse_error& ScopedError)
    {
        SetError(InError, std::string("user settings parse error: ") + std::string(ScopedError.description()));
        return false;
    }
}

bool SaveUserSettings(std::string_view InPath, const UserSettings& InSettings, std::string* InError)
{
    const UserSettings ScopedSettings = ClampUserSettings(InSettings);
    try
    {
        const std::filesystem::path ScopedPath(InPath);
        if (ScopedPath.has_parent_path())
        {
            std::error_code ScopedError;
            std::filesystem::create_directories(ScopedPath.parent_path(), ScopedError);
            if (ScopedError)
            {
                SetError(InError, "cannot create user settings directory: " + ScopedError.message());
                return false;
            }
        }
        toml::table ScopedGraphics;
        ScopedGraphics.insert("window_width", static_cast<std::int64_t>(ScopedSettings.windowWidth));
        ScopedGraphics.insert("window_height", static_cast<std::int64_t>(ScopedSettings.windowHeight));
        ScopedGraphics.insert("window_mode", static_cast<std::int64_t>(ScopedSettings.windowMode));
        ScopedGraphics.insert("vsync", ScopedSettings.vsync);
        ScopedGraphics.insert("quality_preset", static_cast<std::int64_t>(ScopedSettings.qualityPreset));
        ScopedGraphics.insert("render_scale", static_cast<double>(ScopedSettings.renderScale));
        ScopedGraphics.insert("upscale_mode", static_cast<std::int64_t>(ScopedSettings.upscaleMode));
        ScopedGraphics.insert("temporal_upscale", static_cast<std::int64_t>(ScopedSettings.temporalUpscale));
        ScopedGraphics.insert("oit_enabled", ScopedSettings.oitEnabled);
        ScopedGraphics.insert("fsr1_sharpness", static_cast<double>(ScopedSettings.fsr1Sharpness));
        toml::table ScopedHud;
        ScopedHud.insert("render_scale", static_cast<double>(ScopedSettings.hudRenderScale));
        ScopedHud.insert("upscale", static_cast<std::int64_t>(ScopedSettings.hudUpscale));
        ScopedHud.insert("visible", ScopedSettings.hudVisible);
        ScopedHud.insert("target", static_cast<std::int64_t>(ScopedSettings.hudTarget));
        ScopedHud.insert("frame_generation", static_cast<std::int64_t>(ScopedSettings.frameGeneration));
        ScopedHud.insert("frame_generation_policy", static_cast<std::int64_t>(ScopedSettings.frameGenerationPolicy));
        ScopedHud.insert("upscaler_backend", static_cast<std::int64_t>(ScopedSettings.upscalerBackend));
        ScopedHud.insert("upscale_policy", static_cast<std::int64_t>(ScopedSettings.upscalePolicy));
        ScopedHud.insert("dlss_quality", static_cast<std::int64_t>(ScopedSettings.dlssQuality));
        toml::table ScopedRoot;
        ScopedRoot.insert("graphics", std::move(ScopedGraphics));
        ScopedRoot.insert("hud", std::move(ScopedHud));
        std::ofstream ScopedFile(ScopedPath, std::ios::trunc);
        if (!ScopedFile)
        {
            SetError(InError, "cannot open user settings for writing");
            return false;
        }
        ScopedFile << ScopedRoot;
        return static_cast<bool>(ScopedFile);
    }
    catch (const std::exception& ScopedError)
    {
        SetError(InError, std::string("user settings write error: ") + ScopedError.what());
        return false;
    }
}

}
