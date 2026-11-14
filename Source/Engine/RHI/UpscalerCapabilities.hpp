// FemboyEngine2D - OS/vendor gating for upscaler and frame-generation backends.
#pragma once

#include "Engine/RHI/UpscalerBackend.hpp"

#include <cstdint>

namespace Fby::Rhi
{

enum class PlatformKind : std::uint8_t
{
    Windows = 0,
    Linux = 1,
    MacOs = 2,
    Other = 3
};

enum class GpuVendor : std::uint8_t
{
    Unknown = 0,
    Nvidia = 1,
    Amd = 2,
    Intel = 3,
    Apple = 4
};

struct BackendCapabilities
{
    PlatformKind Platform = PlatformKind::Other;
    GpuVendor Vendor = GpuVendor::Unknown;
    bool Fsr1 = true;
    bool Fsr2 = false;
    bool Fsr2Vendor = false;
    bool Dlss = false;
    bool MetalFx = false;
    bool XeSs = false;
    bool CustomFrameGeneration = false;
    bool VendorFrameGeneration = false;
    bool Fsr3FrameGeneration = false;
    bool DlssFrameGeneration = false;
    bool XeSsFrameGeneration = false;
    bool MotionVectors = false;
};

[[nodiscard]] constexpr bool BackendAvailable(const BackendCapabilities& InCaps,
                                              UpscalerBackend InBackend) noexcept
{
    switch (InBackend)
    {
    case UpscalerBackend::None:
        return true;
    case UpscalerBackend::Fsr1:
        return InCaps.Fsr1;
    case UpscalerBackend::Fsr2:
        return InCaps.Fsr2 && InCaps.MotionVectors;
    case UpscalerBackend::Dlss:
        return InCaps.Platform == PlatformKind::Windows && InCaps.Vendor == GpuVendor::Nvidia && InCaps.Dlss &&
               InCaps.MotionVectors;
    case UpscalerBackend::MetalFx:
        return InCaps.Platform == PlatformKind::MacOs && InCaps.Vendor == GpuVendor::Apple && InCaps.MetalFx;
    case UpscalerBackend::XeSs:
        return InCaps.XeSs && InCaps.MotionVectors;
    }
    return false;
}

[[nodiscard]] constexpr bool FrameGenAvailable(const BackendCapabilities& InCaps,
                                               FrameGenBackend InBackend) noexcept;

[[nodiscard]] constexpr UpscalerBackend ChooseUpscaler(const BackendCapabilities& InCaps,
                                                      UpscalerBackend InPreferred) noexcept
{
    if (BackendAvailable(InCaps, InPreferred))
    {
        return InPreferred;
    }
    for (UpscalerBackend ScopedBackend : {UpscalerBackend::Fsr2, UpscalerBackend::Fsr1})
    {
        if (BackendAvailable(InCaps, ScopedBackend))
        {
            return ScopedBackend;
        }
    }
    return UpscalerBackend::None;
}

[[nodiscard]] constexpr FrameGenBackend ChooseFrameGen(const BackendCapabilities& InCaps,
                                                      FrameGenBackend InPreferred) noexcept
{
    if (FrameGenAvailable(InCaps, InPreferred))
    {
        return InPreferred;
    }
    return InCaps.CustomFrameGeneration && InCaps.MotionVectors ? FrameGenBackend::Custom : FrameGenBackend::Off;
}

[[nodiscard]] constexpr bool FrameGenAvailable(const BackendCapabilities& InCaps,
                                               FrameGenBackend InBackend) noexcept
{
    switch (InBackend)
    {
    case FrameGenBackend::Off:
        return true;
    case FrameGenBackend::Custom:
        return InCaps.CustomFrameGeneration && InCaps.MotionVectors;
    case FrameGenBackend::Fsr3:
        return InCaps.Fsr3FrameGeneration && InCaps.MotionVectors;
    case FrameGenBackend::DlssFg:
        return InCaps.DlssFrameGeneration && InCaps.MotionVectors;
    case FrameGenBackend::XeSsFg:
        return InCaps.XeSsFrameGeneration && InCaps.MotionVectors;
    }
    return false;
}

// Human-readable gate for diagnostics. Returns nullptr when the backend is
// available; otherwise names the first missing capability.
[[nodiscard]] constexpr const char* UpscalerUnavailableReason(const BackendCapabilities& InCaps,
                                                              UpscalerBackend InBackend) noexcept
{
    if (BackendAvailable(InCaps, InBackend))
    {
        return nullptr;
    }
    switch (InBackend)
    {
    case UpscalerBackend::None:
        return "off";
    case UpscalerBackend::Fsr1:
        return "FSR1 spatial pass unavailable";
    case UpscalerBackend::Fsr2:
        return InCaps.MotionVectors ? "FSR2 temporal resolve unavailable" : "motion vectors unavailable";
    case UpscalerBackend::Dlss:
        if (InCaps.Platform != PlatformKind::Windows)
        {
            return "DLSS requires Windows";
        }
        if (InCaps.Vendor != GpuVendor::Nvidia)
        {
            return "DLSS requires an NVIDIA GPU";
        }
        if (!InCaps.Dlss)
        {
            return "DLSS runtime unavailable";
        }
        return "motion vectors unavailable";
    case UpscalerBackend::MetalFx:
        return "MetalFX requires macOS on Apple silicon";
    case UpscalerBackend::XeSs:
        return InCaps.MotionVectors ? "XeSS runtime unavailable" : "motion vectors unavailable";
    }
    return "unknown backend";
}

[[nodiscard]] constexpr const char* FrameGenUnavailableReason(const BackendCapabilities& InCaps,
                                                              FrameGenBackend InBackend) noexcept
{
    if (FrameGenAvailable(InCaps, InBackend))
    {
        return nullptr;
    }
    switch (InBackend)
    {
    case FrameGenBackend::Off:
        return "off";
    case FrameGenBackend::Custom:
        return InCaps.MotionVectors ? "custom frame generation disabled" : "motion vectors unavailable";
    case FrameGenBackend::Fsr3:
        return InCaps.MotionVectors ? "FSR3 frame generation runtime unavailable" : "motion vectors unavailable";
    case FrameGenBackend::DlssFg:
        return InCaps.MotionVectors ? "DLSS frame generation runtime unavailable" : "motion vectors unavailable";
    case FrameGenBackend::XeSsFg:
        return InCaps.MotionVectors ? "XeSS frame generation runtime unavailable" : "motion vectors unavailable";
    }
    return "unknown backend";
}

}
