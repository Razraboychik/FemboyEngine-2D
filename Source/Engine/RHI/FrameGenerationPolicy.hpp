// FemboyEngine2D - frame-generation availability and fail-safe policy.
#pragma once

#include <cstdint>

namespace Fby::Rhi
{

enum class FrameGenerationStatus : std::uint8_t
{
    Ready = 0,
    Disabled = 1,
    Unsupported = 2,
    LowFps = 3,
    HudNotSeparated = 4,
    ResetHistory = 5
};

struct FrameGenerationPolicy
{
    // Permissive defaults: a host that never builds a policy still gets frame
    // generation when the backend/device support it. Opt out explicitly.
    bool ProjectAllowed = true;
    bool BackendCompiled = true;
    bool DeviceSupported = true;
    bool HudSeparated = true;
    float MinimumFps = 40.0f;
};

[[nodiscard]] constexpr FrameGenerationStatus EvaluateFrameGeneration(const FrameGenerationPolicy& InPolicy,
                                                                      float InFps,
                                                                      bool InResetHistory) noexcept
{
    if (!InPolicy.ProjectAllowed || !InPolicy.BackendCompiled)
    {
        return FrameGenerationStatus::Disabled;
    }
    if (!InPolicy.DeviceSupported)
    {
        return FrameGenerationStatus::Unsupported;
    }
    if (!InPolicy.HudSeparated)
    {
        return FrameGenerationStatus::HudNotSeparated;
    }
    if (!(InFps >= InPolicy.MinimumFps))
    {
        return FrameGenerationStatus::LowFps;
    }
    if (InResetHistory)
    {
        return FrameGenerationStatus::ResetHistory;
    }
    return FrameGenerationStatus::Ready;
}

}
