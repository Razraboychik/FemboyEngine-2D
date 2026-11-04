// FemboyEngine2D - project policy plus player choice resolved per frame.
#pragma once

#include "Editor/Infrastructure/ProjectConfig.hpp"
#include "Editor/Infrastructure/UserSettings.hpp"
#include "Engine/RHI/BackendCapabilities.hpp"
#include "Engine/RHI/RHI.hpp"

#include <cstdint>

namespace Fby::Editor
{

struct ResolvedRenderSettings
{
    std::uint32_t windowWidth = 1600;
    std::uint32_t windowHeight = 900;
    std::uint8_t windowMode = 1;
    bool vsync = true;
    std::uint8_t qualityPreset = 2;
    float renderScale = 1.0f;
    UpscaleMode upscaleMode = UpscaleMode::Bilinear;
    UpscalerBackendSetting upscalerBackend = UpscalerBackendSetting::Auto;
    UpscalePolicy upscalePolicy = UpscalePolicy::Auto;
    TemporalUpscaleMode temporalUpscale = TemporalUpscaleMode::Off;
    FrameGenerationMode frameGeneration = FrameGenerationMode::Off;
    FrameGenerationPolicySetting frameGenerationPolicy = FrameGenerationPolicySetting::Auto;
    OitMode oitMode = OitMode::Off;
    bool staticBakeEnabled = false;
    OpaqueRenderMode opaqueMode = OpaqueRenderMode::Auto;
    bool opaqueArtCutout = false;
    float opaqueAlphaCutoff = 0.5f;
};

struct ResolvedHudSettings
{
    float renderScale = 1.0f;
    UpscaleMode upscaleMode = UpscaleMode::Nearest;
    HudTargetMode target = HudTargetMode::Auto;
    bool visible = true;
};

[[nodiscard]] constexpr ResolvedHudSettings ResolveHudSettings(const UserSettings& InUser) noexcept
{
    return ResolvedHudSettings{InUser.hudRenderScale, InUser.hudUpscale, InUser.hudTarget, InUser.hudVisible};
}

[[nodiscard]] constexpr UpscaleMode ResolveBackendMode(UpscalerBackendSetting InBackend,
                                                       UpscaleMode InMode,
                                                       const Rhi::BackendCapabilities& InCaps) noexcept
{
    switch (InBackend)
    {
    case UpscalerBackendSetting::Fsr1:
        return InCaps.Fsr1 ? UpscaleMode::Fsr1 : UpscaleMode::Bilinear;
    case UpscalerBackendSetting::Fsr2:
        return InCaps.Fsr2 && InCaps.MotionVectors ? UpscaleMode::Fsr2 : UpscaleMode::Fsr1;
    case UpscalerBackendSetting::Dlss:
    case UpscalerBackendSetting::MetalFx:
    case UpscalerBackendSetting::XeSs:
        // A vendor backend without its runtime uses the spatial fallback.
        return InCaps.Fsr1 ? UpscaleMode::Fsr1 : UpscaleMode::Bilinear;
    case UpscalerBackendSetting::Auto:
    default:
        return InMode;
    }
}

[[nodiscard]] constexpr UpscaleMode ChooseUpscaleMode(UpscaleMode InMode,
                                                      const Rhi::BackendCapabilities& InCaps) noexcept
{
    if (InMode == UpscaleMode::Fsr2 && (!InCaps.Fsr2 || !InCaps.MotionVectors))
    {
        return InCaps.Fsr1 ? UpscaleMode::Fsr1 : UpscaleMode::Bilinear;
    }
    if (InMode == UpscaleMode::Fsr1 && !InCaps.Fsr1)
    {
        return UpscaleMode::Bilinear;
    }
    return InMode;
}

[[nodiscard]] constexpr HudTargetMode ResolveHudTarget(HudTargetMode InMode, bool InFrameGeneration) noexcept
{
    if (InMode == HudTargetMode::Auto)
    {
        return InFrameGeneration ? HudTargetMode::Offscreen : HudTargetMode::Direct;
    }
    return InMode;
}

[[nodiscard]] constexpr bool HasTemporalUpscaler(const Rhi::UpscalerCaps& InCaps) noexcept
{
    return InCaps.motionVectors && (InCaps.fsr2 || InCaps.dlss);
}

[[nodiscard]] inline ResolvedRenderSettings ResolveRenderSettings(
    const ProjectSettings& InProject, const UserSettings& InUser,
    const Rhi::UpscalerCaps& InCaps = Rhi::UpscalerCaps{},
    const Rhi::BackendCapabilities* InBackendCaps = nullptr) noexcept
{
    ResolvedRenderSettings ScopedResult;
    ScopedResult.windowWidth = InUser.windowWidth;
    ScopedResult.windowHeight = InUser.windowHeight;
    ScopedResult.windowMode = InUser.windowMode;
    ScopedResult.vsync = InUser.vsync;
    ScopedResult.qualityPreset = InUser.qualityPreset;
    ScopedResult.renderScale = InUser.renderScale;
    const UpscalePolicy ScopedPolicy = InUser.upscalePolicy == UpscalePolicy::Auto
                                           ? InProject.upscalePolicy
                                           : InUser.upscalePolicy;
    ScopedResult.upscalePolicy = ScopedPolicy;
    if (InBackendCaps == nullptr)
    {
        ScopedResult.upscaleMode = InUser.upscaleMode;
        ScopedResult.upscalerBackend = InUser.upscalerBackend;
    }
    else if (ScopedPolicy == UpscalePolicy::Off)
    {
        ScopedResult.upscaleMode = UpscaleMode::Bilinear;
        ScopedResult.upscalerBackend = UpscalerBackendSetting::Auto;
    }
    else if (ScopedPolicy == UpscalePolicy::Universal)
    {
        const bool ScopedFsr2 = InBackendCaps->Fsr2 && InBackendCaps->MotionVectors;
        ScopedResult.upscalerBackend = ScopedFsr2 ? UpscalerBackendSetting::Fsr2 : UpscalerBackendSetting::Fsr1;
        ScopedResult.upscaleMode = ScopedFsr2 ? UpscaleMode::Fsr2 : UpscaleMode::Fsr1;
    }
    else if (InUser.upscalerBackend != UpscalerBackendSetting::Auto)
    {
        // Advanced override retained for old project files. The normal UI only
        // exposes the Auto/Universal/Off policy.
        ScopedResult.upscalerBackend = InUser.upscalerBackend;
        ScopedResult.upscaleMode =
            ResolveBackendMode(InUser.upscalerBackend, InUser.upscaleMode, *InBackendCaps);
    }
    else
    {
        // Auto: best vendor path first, then the universal FSR chain.
        if (InBackendCaps->Dlss && InBackendCaps->MotionVectors)
        {
            ScopedResult.upscalerBackend = UpscalerBackendSetting::Dlss;
            ScopedResult.upscaleMode = UpscaleMode::Fsr2;
        }
        else
        {
            const bool ScopedFsr2 = InBackendCaps->Fsr2 && InBackendCaps->MotionVectors;
            ScopedResult.upscalerBackend = ScopedFsr2 ? UpscalerBackendSetting::Fsr2 : UpscalerBackendSetting::Fsr1;
            ScopedResult.upscaleMode = ScopedFsr2 ? UpscaleMode::Fsr2 : UpscaleMode::Fsr1;
        }
    }
    if (InProject.allowTemporalUpscale && InProject.temporalUpscale != TemporalUpscaleMode::Off)
    {
        TemporalUpscaleMode ScopedCandidate = InUser.temporalUpscale;
        if (ScopedCandidate == TemporalUpscaleMode::Auto)
        {
            ScopedCandidate = InCaps.fsr2 ? TemporalUpscaleMode::Fsr2
                                         : (InCaps.dlss ? TemporalUpscaleMode::Dlss : TemporalUpscaleMode::Off);
        }
        if ((ScopedCandidate == TemporalUpscaleMode::Fsr2 && InCaps.fsr2 && InCaps.motionVectors) ||
            (ScopedCandidate == TemporalUpscaleMode::Dlss && InCaps.dlss && InCaps.motionVectors))
        {
            ScopedResult.temporalUpscale = ScopedCandidate;
        }
    }
    const FrameGenerationPolicySetting ScopedFgPolicy =
        InUser.frameGenerationPolicy == FrameGenerationPolicySetting::Auto
            ? InProject.frameGenerationPolicy
            : InUser.frameGenerationPolicy;
    ScopedResult.frameGenerationPolicy = ScopedFgPolicy;
    if (InProject.allowFrameGeneration && ScopedFgPolicy != FrameGenerationPolicySetting::Off)
    {
        if (InBackendCaps == nullptr)
        {
            ScopedResult.frameGeneration = InUser.frameGeneration;
        }
        else if (ScopedFgPolicy == FrameGenerationPolicySetting::Universal)
        {
            if (InBackendCaps->Fsr3FrameGeneration && InBackendCaps->MotionVectors)
            {
                ScopedResult.frameGeneration = FrameGenerationMode::Fsr3;
            }
            else if (InBackendCaps->CustomFrameGeneration && InBackendCaps->MotionVectors)
            {
                ScopedResult.frameGeneration = FrameGenerationMode::Custom;
            }
        }
        else if (InBackendCaps->DlssFrameGeneration && InBackendCaps->MotionVectors)
        {
            ScopedResult.frameGeneration = FrameGenerationMode::Dlss;
        }
        else if (InBackendCaps->Fsr3FrameGeneration && InBackendCaps->MotionVectors)
        {
            ScopedResult.frameGeneration = FrameGenerationMode::Fsr3;
        }
        else if (InBackendCaps->CustomFrameGeneration && InBackendCaps->MotionVectors)
        {
            ScopedResult.frameGeneration = FrameGenerationMode::Custom;
        }
    }
    ScopedResult.oitMode = InUser.oitEnabled ? InProject.oitMode : OitMode::Off;
    ScopedResult.staticBakeEnabled = InProject.staticBakeEnabled;
    ScopedResult.opaqueMode = InProject.opaqueMode;
    ScopedResult.opaqueArtCutout = InProject.opaqueArtCutout;
    ScopedResult.opaqueAlphaCutoff = InProject.opaqueAlphaCutoff;
    return ScopedResult;
}

}
