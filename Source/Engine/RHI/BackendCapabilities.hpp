// FemboyEngine2D - bridge from DeviceInfo to backend gating.
#pragma once

#include "Engine/RHI/RHI.hpp"
#include "Engine/RHI/DlssRuntime.hpp"
#include "Engine/RHI/FidelityFxRuntime.hpp"
#include "Engine/RHI/UpscalerCapabilities.hpp"

namespace Fby::Rhi
{

[[nodiscard]] constexpr BackendCapabilities CapabilitiesFromDevice(
    const DeviceInfo& InInfo, const DlssRuntimeAvailability& InDlssRuntime = {},
    const FidelityFxRuntimeAvailability& InFidelityFxRuntime = {}) noexcept
{
    BackendCapabilities ScopedCaps;
    switch (InInfo.platform)
    {
    case 0:
        ScopedCaps.Platform = PlatformKind::Windows;
        break;
    case 1:
        ScopedCaps.Platform = PlatformKind::Linux;
        break;
    case 2:
        ScopedCaps.Platform = PlatformKind::MacOs;
        break;
    default:
        ScopedCaps.Platform = PlatformKind::Other;
        break;
    }
    switch (InInfo.gpuVendor)
    {
    case 1:
        ScopedCaps.Vendor = GpuVendor::Nvidia;
        break;
    case 2:
        ScopedCaps.Vendor = GpuVendor::Amd;
        break;
    case 3:
        ScopedCaps.Vendor = GpuVendor::Intel;
        break;
    case 4:
        ScopedCaps.Vendor = GpuVendor::Apple;
        break;
    default:
        ScopedCaps.Vendor = GpuVendor::Unknown;
        break;
    }
    ScopedCaps.Fsr1 = InInfo.upscalers.fsr1;
    ScopedCaps.Fsr2 = InInfo.upscalers.fsr2;
    ScopedCaps.Fsr2Vendor = InFidelityFxRuntime.Upscaling && InInfo.upscalers.motionVectors;
    const bool ScopedNvidiaWindows =
        ScopedCaps.Platform == PlatformKind::Windows && ScopedCaps.Vendor == GpuVendor::Nvidia;
    ScopedCaps.Dlss = InInfo.backend == Backend::Vulkan &&
                      (InInfo.upscalers.dlss || (ScopedNvidiaWindows && InDlssRuntime.Upscaling));
    // The enum stays for cross-platform project files, but no MetalFX adapter
    // exists yet. Do not advertise it until the adapter is implemented.
    ScopedCaps.MetalFx = false;
    ScopedCaps.XeSs = false;
    ScopedCaps.CustomFrameGeneration =
        InInfo.upscalers.frameGeneration && InInfo.upscalers.motionVectors;
    ScopedCaps.Fsr3FrameGeneration =
        InFidelityFxRuntime.FrameGeneration && InInfo.upscalers.motionVectors;
    ScopedCaps.DlssFrameGeneration = ScopedNvidiaWindows && InDlssRuntime.FrameGeneration &&
                                     InInfo.upscalers.motionVectors;
    // XeSS FG in the vendored SDK exposes only the D3D12 swapchain API; the
    // engine has no D3D12 backend, so it must never be advertised on Vulkan.
    ScopedCaps.XeSsFrameGeneration = false;
    ScopedCaps.VendorFrameGeneration = ScopedCaps.Fsr3FrameGeneration || ScopedCaps.DlssFrameGeneration ||
                                      ScopedCaps.XeSsFrameGeneration;
    ScopedCaps.MotionVectors = InInfo.upscalers.motionVectors;
    return ScopedCaps;
}

}
