// FemboyEngine2D - project/user render settings tests.
#include "Editor/Infrastructure/ResolvedRenderSettings.hpp"
#include "Editor/Infrastructure/SettingsSchema.hpp"
#include "Editor/Infrastructure/UserSettings.hpp"
#include "Engine/RHI/FrameGenerationPolicy.hpp"
#include "Engine/RHI/FramePacing.hpp"
#include "Engine/RHI/UpscalerCapabilities.hpp"
#include "Engine/RHI/BackendCapabilities.hpp"
#include "Engine/RHI/DlssAdapter.hpp"
#include "Engine/RHI/FidelityFxRuntime.hpp"
#include "Engine/RHI/Fsr2Adapter.hpp"
#include "Engine/RHI/DlssRuntime.hpp"
#include "Engine/RHI/DlssRuntimeProbe.hpp"

#include <cstdio>
#include <filesystem>
#include <string>

namespace
{
int GFailures = 0;

#define FBY_CHECK(condition)                                                                                           \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #condition);                                  \
            ++GFailures;                                                                                               \
        }                                                                                                              \
    } while (0)
}

int main()
{
    using namespace Fby::Editor;

    for (const SettingDesc& ScopedSetting : kRenderSettingsSchema)
    {
        FBY_CHECK(!ScopedSetting.key.empty());
        FBY_CHECK(!ScopedSetting.label.empty());
        FBY_CHECK(!ScopedSetting.tooltip.empty());
        for (const SettingDesc& ScopedOther : kRenderSettingsSchema)
        {
            if (&ScopedSetting != &ScopedOther)
            {
                FBY_CHECK(ScopedSetting.key != ScopedOther.key);
            }
        }
    }
    FBY_CHECK(FindSettingDesc("render_scale") != nullptr);
    FBY_CHECK(FindSettingDesc("missing") == nullptr);

    ProjectSettings ScopedProject;
    ScopedProject.oitMode = OitMode::Weighted;
    ScopedProject.staticBakeEnabled = true;
    ScopedProject.opaqueMode = OpaqueRenderMode::On;
    ScopedProject.opaqueAlphaCutoff = 0.4f;

    UserSettings ScopedUser;
    ScopedUser.renderScale = 0.75f;
    ScopedUser.upscaleMode = UpscaleMode::Fsr1;
    ScopedUser.oitEnabled = true;
    ScopedUser.qualityPreset = 1;

    const ResolvedRenderSettings ScopedResolved = ResolveRenderSettings(ScopedProject, ScopedUser);
    FBY_CHECK(ScopedResolved.oitMode == OitMode::Weighted);
    FBY_CHECK(ScopedResolved.renderScale == 0.75f);
    FBY_CHECK(ScopedResolved.upscaleMode == UpscaleMode::Fsr1);
    FBY_CHECK(ScopedResolved.staticBakeEnabled);
    FBY_CHECK(ScopedResolved.opaqueMode == OpaqueRenderMode::On);

    ScopedUser.oitEnabled = false;
    FBY_CHECK(ResolveRenderSettings(ScopedProject, ScopedUser).oitMode == OitMode::Off);
    ScopedUser.oitEnabled = true;

    Fby::Rhi::FrameGenerationPolicy ScopedPolicy;
    FBY_CHECK(Fby::Rhi::EvaluateFrameGeneration(ScopedPolicy, 60.0f, false) ==
              Fby::Rhi::FrameGenerationStatus::Disabled);
    ScopedPolicy.ProjectAllowed = true;
    ScopedPolicy.BackendCompiled = true;
    ScopedPolicy.DeviceSupported = true;
    ScopedPolicy.HudSeparated = true;
    FBY_CHECK(Fby::Rhi::EvaluateFrameGeneration(ScopedPolicy, 30.0f, false) == Fby::Rhi::FrameGenerationStatus::LowFps);
    FBY_CHECK(Fby::Rhi::EvaluateFrameGeneration(ScopedPolicy, 60.0f, true) ==
              Fby::Rhi::FrameGenerationStatus::ResetHistory);
    FBY_CHECK(Fby::Rhi::EvaluateFrameGeneration(ScopedPolicy, 60.0f, false) == Fby::Rhi::FrameGenerationStatus::Ready);

    const std::filesystem::path ScopedDlssDir = "build-audit/fby_dlss_test";
    std::error_code ScopedDlssCreateError;
    std::filesystem::create_directories(ScopedDlssDir, ScopedDlssCreateError);
    const std::string ScopedDlssFile = (ScopedDlssDir / "nvngx_dlss.dll").string();
    if (std::FILE* ScopedDlssHandle = std::fopen(ScopedDlssFile.c_str(), "wb"))
    {
        std::fputc('x', ScopedDlssHandle);
        std::fclose(ScopedDlssHandle);
    }
    const Fby::Rhi::DlssRuntimeAvailability ScopedDlss = Fby::Rhi::DetectDlssRuntime(ScopedDlssDir);
    FBY_CHECK(ScopedDlss.Upscaling);
    FBY_CHECK(!ScopedDlss.FrameGeneration);

    Fby::Rhi::DeviceInfo ScopedDevice;
    ScopedDevice.backend = Fby::Rhi::Backend::Vulkan;
    ScopedDevice.platform = 0;
    ScopedDevice.gpuVendor = 1;
    ScopedDevice.upscalers.dlss = true;
    ScopedDevice.upscalers.motionVectors = true;
    const Fby::Rhi::BackendCapabilities ScopedDeviceCaps = Fby::Rhi::CapabilitiesFromDevice(ScopedDevice);
    FBY_CHECK(ScopedDeviceCaps.Vendor == Fby::Rhi::GpuVendor::Nvidia);
    FBY_CHECK(ScopedDeviceCaps.Platform == Fby::Rhi::PlatformKind::Windows);
    Fby::Rhi::DeviceInfo ScopedMacInfo;
    ScopedMacInfo.backend = Fby::Rhi::Backend::Metal;
    ScopedMacInfo.platform = 2;
    ScopedMacInfo.gpuVendor = 4;
    FBY_CHECK(!Fby::Rhi::CapabilitiesFromDevice(ScopedMacInfo).MetalFx);
    FBY_CHECK(Fby::Rhi::BackendAvailable(ScopedDeviceCaps, Fby::Rhi::UpscalerBackend::Dlss));
    const std::filesystem::path ScopedFfxDir = "build-audit/fby_ffx_test";
    std::error_code ScopedFfxCreateError;
    std::filesystem::create_directories(ScopedFfxDir, ScopedFfxCreateError);
    const std::string ScopedFfxFile = (ScopedFfxDir / "amd_fidelityfx_upscaler_vk.dll").string();
    if (std::FILE* ScopedFfxHandle = std::fopen(ScopedFfxFile.c_str(), "wb"))
    {
        std::fputc('x', ScopedFfxHandle);
        std::fclose(ScopedFfxHandle);
    }
    const Fby::Rhi::FidelityFxRuntimeAvailability ScopedFfx = Fby::Rhi::DetectFidelityFxRuntime(ScopedFfxDir);
    FBY_CHECK(ScopedFfx.Upscaling);
    FBY_CHECK(!ScopedFfx.FrameGeneration);
    Fby::Rhi::DeviceInfo ScopedFfxInfo;
    ScopedFfxInfo.upscalers.motionVectors = true;
    FBY_CHECK(Fby::Rhi::CapabilitiesFromDevice(ScopedFfxInfo, {}, ScopedFfx).Fsr2Vendor);
    Fby::Rhi::FidelityFxRuntimeAvailability ScopedFfxFg;
    ScopedFfxFg.FrameGeneration = true;
    ScopedFfxInfo.upscalers.frameGeneration = true;
    FBY_CHECK(Fby::Rhi::CapabilitiesFromDevice(ScopedFfxInfo, {}, ScopedFfxFg).Fsr3FrameGeneration);
    Fby::Rhi::BackendCapabilities ScopedFgCaps;
    ScopedFgCaps.MotionVectors = true;
    ScopedFgCaps.Fsr3FrameGeneration = true;
    FBY_CHECK(Fby::Rhi::FrameGenAvailable(ScopedFgCaps, Fby::Rhi::FrameGenBackend::Fsr3));
    FBY_CHECK(!Fby::Rhi::FrameGenAvailable(ScopedFgCaps, Fby::Rhi::FrameGenBackend::DlssFg));
    Fby::Rhi::DeviceInfo ScopedVendorFgInfo;
    ScopedVendorFgInfo.backend = Fby::Rhi::Backend::Vulkan;
    ScopedVendorFgInfo.platform = 0;
    ScopedVendorFgInfo.gpuVendor = 1;
    ScopedVendorFgInfo.upscalers.motionVectors = true;
    Fby::Rhi::DlssRuntimeAvailability ScopedDlssFg;
    ScopedDlssFg.FrameGeneration = true;
    const Fby::Rhi::BackendCapabilities ScopedVendorFgCaps =
        Fby::Rhi::CapabilitiesFromDevice(ScopedVendorFgInfo, ScopedDlssFg, {});
    FBY_CHECK(ScopedVendorFgCaps.DlssFrameGeneration);
    FBY_CHECK(!ScopedVendorFgCaps.Fsr3FrameGeneration);
    std::error_code ScopedFfxCleanup;
    std::filesystem::remove_all(ScopedFfxDir, ScopedFfxCleanup);

    FBY_CHECK(Fby::Rhi::Fsr2RenderScale(Fby::Rhi::Fsr2QualityMode::Quality) > 0.5f);
    FBY_CHECK(Fby::Rhi::Fsr2RenderScale(Fby::Rhi::Fsr2QualityMode::UltraPerformance) < 0.5f);
    FBY_CHECK(Fby::Rhi::DlssRenderScale(Fby::Rhi::DlssQualityMode::Quality) > 0.5f);
    FBY_CHECK(Fby::Rhi::DlssRenderScale(Fby::Rhi::DlssQualityMode::UltraPerformance) < 0.5f);

    const Fby::Rhi::DlssRuntimeProbe ScopedDlssProbe = Fby::Rhi::ProbeDlssRuntime("build-audit/fby_dlss_test");
    FBY_CHECK(ScopedDlssProbe.Present);
    FBY_CHECK(!ScopedDlssProbe.Loadable);
    std::error_code ScopedDlssCleanup;
    std::filesystem::remove_all(ScopedDlssDir, ScopedDlssCleanup);

    Fby::Rhi::DlssRuntimeAvailability ScopedDlssRuntime;
    ScopedDlssRuntime.Upscaling = true;
    const Fby::Rhi::BackendCapabilities ScopedRuntimeCaps =
        Fby::Rhi::CapabilitiesFromDevice(ScopedDevice, ScopedDlssRuntime);
    FBY_CHECK(Fby::Rhi::BackendAvailable(ScopedRuntimeCaps, Fby::Rhi::UpscalerBackend::Dlss));

    Fby::Rhi::BackendCapabilities ScopedCaps;
    ScopedCaps.Platform = Fby::Rhi::PlatformKind::Windows;
    ScopedCaps.Vendor = Fby::Rhi::GpuVendor::Nvidia;
    ScopedCaps.Dlss = true;
    ScopedCaps.MotionVectors = true;
    FBY_CHECK(Fby::Rhi::BackendAvailable(ScopedCaps, Fby::Rhi::UpscalerBackend::Dlss));

    Fby::Rhi::BackendCapabilities ScopedPolicyCaps;
    ScopedPolicyCaps.Dlss = true;
    ScopedPolicyCaps.Fsr1 = true;
    ScopedPolicyCaps.Fsr2 = true;
    ScopedPolicyCaps.MotionVectors = true;
    ScopedPolicyCaps.CustomFrameGeneration = true;
    ScopedPolicyCaps.DlssFrameGeneration = true;
    ScopedPolicyCaps.Fsr3FrameGeneration = true;
    ScopedUser.upscalerBackend = UpscalerBackendSetting::Auto;
    ScopedUser.upscalePolicy = UpscalePolicy::Auto;
    const Fby::Editor::ResolvedRenderSettings ScopedAutoPolicy =
        ResolveRenderSettings(ScopedProject, ScopedUser, {}, &ScopedPolicyCaps);
    FBY_CHECK(ScopedAutoPolicy.upscalerBackend == UpscalerBackendSetting::Dlss);
    FBY_CHECK(ScopedAutoPolicy.upscaleMode == UpscaleMode::Fsr2);
    ScopedUser.upscalePolicy = UpscalePolicy::Universal;
    const Fby::Editor::ResolvedRenderSettings ScopedUniversalPolicy =
        ResolveRenderSettings(ScopedProject, ScopedUser, {}, &ScopedPolicyCaps);
    FBY_CHECK(ScopedUniversalPolicy.upscalerBackend == UpscalerBackendSetting::Fsr2);
    ScopedUser.upscalePolicy = UpscalePolicy::Off;
    FBY_CHECK(ResolveRenderSettings(ScopedProject, ScopedUser, {}, &ScopedPolicyCaps).upscaleMode ==
              UpscaleMode::Bilinear);
    ScopedUser.upscalePolicy = UpscalePolicy::Auto;
    ScopedProject.allowFrameGeneration = true;
    ScopedUser.frameGenerationPolicy = FrameGenerationPolicySetting::Auto;
    FBY_CHECK(ResolveRenderSettings(ScopedProject, ScopedUser, {}, &ScopedPolicyCaps).frameGeneration ==
              FrameGenerationMode::Dlss);
    ScopedUser.frameGenerationPolicy = FrameGenerationPolicySetting::Universal;
    FBY_CHECK(ResolveRenderSettings(ScopedProject, ScopedUser, {}, &ScopedPolicyCaps).frameGeneration ==
              FrameGenerationMode::Fsr3);
    FBY_CHECK(!Fby::Rhi::BackendAvailable(ScopedCaps, Fby::Rhi::UpscalerBackend::MetalFx));
    ScopedCaps.Platform = Fby::Rhi::PlatformKind::MacOs;
    ScopedCaps.Vendor = Fby::Rhi::GpuVendor::Apple;
    FBY_CHECK(!Fby::Rhi::BackendAvailable(ScopedCaps, Fby::Rhi::UpscalerBackend::Dlss));
    ScopedCaps.MetalFx = true;
    FBY_CHECK(Fby::Rhi::BackendAvailable(ScopedCaps, Fby::Rhi::UpscalerBackend::MetalFx));
    FBY_CHECK(Fby::Rhi::ChooseUpscaler(ScopedCaps, Fby::Rhi::UpscalerBackend::Dlss) == Fby::Rhi::UpscalerBackend::Fsr1);
    ScopedCaps.Fsr2 = true;
    ScopedCaps.MotionVectors = true;
    FBY_CHECK(Fby::Rhi::ChooseUpscaler(ScopedCaps, Fby::Rhi::UpscalerBackend::Dlss) == Fby::Rhi::UpscalerBackend::Fsr2);
    FBY_CHECK(Fby::Rhi::ChooseFrameGen(ScopedCaps, Fby::Rhi::FrameGenBackend::DlssFg) ==
              Fby::Rhi::FrameGenBackend::Off);
    ScopedCaps.Fsr1 = true;
    ScopedCaps.Fsr2 = false;
    FBY_CHECK(ChooseUpscaleMode(UpscaleMode::Fsr2, ScopedCaps) == UpscaleMode::Fsr1);
    ScopedCaps.Fsr1 = false;
    FBY_CHECK(ChooseUpscaleMode(UpscaleMode::Fsr1, ScopedCaps) == UpscaleMode::Bilinear);
    ScopedCaps.Fsr1 = true;
    ScopedUser.upscalerBackend = UpscalerBackendSetting::Fsr1;
    FBY_CHECK(ResolveRenderSettings(ScopedProject, ScopedUser, {}, &ScopedCaps).upscaleMode == UpscaleMode::Fsr1);
    ScopedUser.upscalerBackend = UpscalerBackendSetting::Dlss;
    FBY_CHECK(ResolveRenderSettings(ScopedProject, ScopedUser, {}, &ScopedCaps).upscaleMode == UpscaleMode::Fsr1);
    ScopedUser.upscalerBackend = UpscalerBackendSetting::Auto;

    Fby::Rhi::FramePacingConfig ScopedPacing;
    Fby::Rhi::FramePacingState ScopedPacingState;
    ScopedPacingState = Fby::Rhi::UpdateFramePacing(ScopedPacing, ScopedPacingState, 17.0f, true);
    FBY_CHECK(ScopedPacingState.AllowGenerated);
    ScopedPacingState = Fby::Rhi::UpdateFramePacing(ScopedPacing, ScopedPacingState, 0.0f, false);
    FBY_CHECK(ScopedPacingState.GeneratedThisReal == 1u);
    ScopedPacingState = Fby::Rhi::UpdateFramePacing(ScopedPacing, ScopedPacingState, 0.0f, false);
    FBY_CHECK(ScopedPacingState.GeneratedThisReal == 1u);

    ScopedUser.upscalerBackend = UpscalerBackendSetting::Dlss;
    ScopedUser.dlssQuality = Fby::Rhi::DlssQualityMode::Performance;
    FBY_CHECK(ResolveRenderSettings(ScopedProject, ScopedUser).upscalerBackend == UpscalerBackendSetting::Dlss);
    FBY_CHECK(ScopedUser.dlssQuality == Fby::Rhi::DlssQualityMode::Performance);
    ScopedUser.upscalerBackend = UpscalerBackendSetting::Auto;

    ScopedProject.allowFrameGeneration = false;
    ScopedUser.frameGeneration = FrameGenerationMode::Custom;
    FBY_CHECK(ResolveRenderSettings(ScopedProject, ScopedUser).frameGeneration == FrameGenerationMode::Off);
    ScopedProject.allowFrameGeneration = true;
    FBY_CHECK(ResolveRenderSettings(ScopedProject, ScopedUser).frameGeneration == FrameGenerationMode::Custom);
    Fby::Rhi::BackendCapabilities ScopedNoFgCaps;
    FBY_CHECK(ResolveRenderSettings(ScopedProject, ScopedUser, {}, &ScopedNoFgCaps).frameGeneration ==
              FrameGenerationMode::Off);
    ScopedNoFgCaps.MotionVectors = true;
    ScopedNoFgCaps.CustomFrameGeneration = true;
    FBY_CHECK(ResolveRenderSettings(ScopedProject, ScopedUser, {}, &ScopedNoFgCaps).frameGeneration ==
              FrameGenerationMode::Custom);

    ScopedProject.allowTemporalUpscale = true;
    ScopedProject.temporalUpscale = TemporalUpscaleMode::Fsr2;
    ScopedUser.temporalUpscale = TemporalUpscaleMode::Fsr2;
    Fby::Rhi::UpscalerCaps ScopedUpscalerCaps;
    FBY_CHECK(!HasTemporalUpscaler(ScopedUpscalerCaps));
    ScopedUpscalerCaps.fsr2 = true;
    FBY_CHECK(!HasTemporalUpscaler(ScopedUpscalerCaps));
    ScopedUpscalerCaps.motionVectors = true;
    FBY_CHECK(HasTemporalUpscaler(ScopedUpscalerCaps));
    ScopedUpscalerCaps.motionVectors = false;
    FBY_CHECK(ResolveRenderSettings(ScopedProject, ScopedUser, ScopedUpscalerCaps).temporalUpscale ==
              TemporalUpscaleMode::Off);
    ScopedUpscalerCaps.fsr2 = true;
    ScopedUpscalerCaps.motionVectors = true;
    FBY_CHECK(ResolveRenderSettings(ScopedProject, ScopedUser, ScopedUpscalerCaps).temporalUpscale ==
              TemporalUpscaleMode::Fsr2);
    ScopedUser.temporalUpscale = TemporalUpscaleMode::Auto;
    FBY_CHECK(ResolveRenderSettings(ScopedProject, ScopedUser, ScopedUpscalerCaps).temporalUpscale ==
              TemporalUpscaleMode::Fsr2);
    ScopedProject.allowTemporalUpscale = false;
    FBY_CHECK(ResolveRenderSettings(ScopedProject, ScopedUser, ScopedUpscalerCaps).temporalUpscale ==
              TemporalUpscaleMode::Off);

    ScopedUser.hudRenderScale = 0.5f;
    ScopedUser.hudUpscale = UpscaleMode::Bilinear;
    ScopedUser.hudVisible = false;
    ScopedUser.hudTarget = HudTargetMode::Offscreen;
    const ResolvedHudSettings ScopedHud = ResolveHudSettings(ScopedUser);
    FBY_CHECK(ScopedHud.renderScale == 0.5f);
    FBY_CHECK(ScopedHud.upscaleMode == UpscaleMode::Bilinear);
    FBY_CHECK(ScopedHud.target == HudTargetMode::Offscreen);
    FBY_CHECK(!ScopedHud.visible);
    FBY_CHECK(ResolveHudTarget(HudTargetMode::Auto, false) == HudTargetMode::Direct);
    FBY_CHECK(ResolveHudTarget(HudTargetMode::Auto, true) == HudTargetMode::Offscreen);
    FBY_CHECK(ResolveHudTarget(HudTargetMode::Direct, true) == HudTargetMode::Direct);
    FBY_CHECK(ResolveHudTarget(HudTargetMode::Offscreen, false) == HudTargetMode::Offscreen);

    ScopedUser.fsr1Sharpness = 2.0f;
    FBY_CHECK(ClampUserSettings(ScopedUser).fsr1Sharpness <= 1.0f);
    ScopedUser.fsr1Sharpness = 0.35f;
    ScopedUser.windowWidth = 99999;
    ScopedUser.renderScale = 4.0f;
    ScopedUser.hudRenderScale = 4.0f;
    std::snprintf(ScopedUser.voicePresetId, sizeof(ScopedUser.voicePresetId), "%s", "Fby.Voice.Underwater");
    ScopedUser.voicePresetIntensity = 0.6f;
    ScopedUser.voiceModerationMumble = false;
    ScopedUser.voiceReactiveSfx = false;
    ScopedUser.voiceReactiveSfxGain = 0.3f;
    const UserSettings ScopedClamped = ClampUserSettings(ScopedUser);
    FBY_CHECK(ScopedClamped.windowWidth <= 16384u);
    FBY_CHECK(ScopedClamped.renderScale <= 1.0f);
    FBY_CHECK(ScopedClamped.hudRenderScale <= 1.0f);

    const std::filesystem::path ScopedPath = "build-audit/settings_test.toml";
    std::string ScopedError;
    FBY_CHECK(SaveUserSettings(ScopedPath.string(), ScopedClamped, &ScopedError));
    UserSettings ScopedLoaded;
    FBY_CHECK(LoadUserSettings(ScopedPath.string(), &ScopedLoaded, &ScopedError));
    FBY_CHECK(ScopedLoaded == ScopedClamped);
    std::error_code ScopedRemoveError;
    std::filesystem::remove(ScopedPath, ScopedRemoveError);

    if (GFailures != 0)
    {
        std::fprintf(stderr, "settings_test: %d failure(s)\n", GFailures);
        return 1;
    }
    std::printf("settings_test: OK\n");
    return 0;
}
