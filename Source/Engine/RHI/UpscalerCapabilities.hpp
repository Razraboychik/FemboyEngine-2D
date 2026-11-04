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

}
