// FemboyEngine2D - vendor-neutral upscaler and frame generator selection.
#pragma once

#include <cstdint>

namespace Fby::Rhi
{

enum class UpscalerBackend : std::uint8_t
{
    None = 0,
    Fsr1 = 1,
    Fsr2 = 2,
    Dlss = 3,
    MetalFx = 4,
    XeSs = 5
};

enum class FrameGenBackend : std::uint8_t
{
    Off = 0,
    Custom = 1,
    Fsr3 = 2,
    DlssFg = 3,
    XeSsFg = 4
};

struct BackendSelection
{
    UpscalerBackend Upscaler = UpscalerBackend::None;
    FrameGenBackend FrameGenerator = FrameGenBackend::Off;
};

// Exactly one upscaler and at most one frame generator may be active.
[[nodiscard]] constexpr bool IsSingleBackend(const BackendSelection& InSelection) noexcept
{
    if (InSelection.FrameGenerator == FrameGenBackend::Off)
    {
        return true;
    }
    return InSelection.FrameGenerator != FrameGenBackend::Custom ||
           InSelection.Upscaler != UpscalerBackend::None;
}

[[nodiscard]] constexpr BackendSelection ResolveFallback(const BackendSelection& InRequested,
                                                         UpscalerBackend InAvailableUpscaler,
                                                         FrameGenBackend InAvailableFrameGenerator) noexcept
{
    BackendSelection ScopedResult = InRequested;
    if (ScopedResult.Upscaler != UpscalerBackend::None && ScopedResult.Upscaler != InAvailableUpscaler)
    {
        ScopedResult.Upscaler = InAvailableUpscaler;
    }
    if (ScopedResult.FrameGenerator != FrameGenBackend::Off &&
        ScopedResult.FrameGenerator != InAvailableFrameGenerator)
    {
        ScopedResult.FrameGenerator = InAvailableFrameGenerator;
    }
    return ScopedResult;
}

}
