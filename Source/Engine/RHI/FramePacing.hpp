// FemboyEngine2D - frame-generation pacing budget.
#pragma once

#include <algorithm>
#include <cstdint>

namespace Fby::Rhi
{

struct FramePacingConfig
{
    float TargetFps = 60.0f;
    std::uint32_t MaxGeneratedPerReal = 1;
    float MaxLatencyMs = 16.0f;
};

struct FramePacingState
{
    float AccumulatedMs = 0.0f;
    std::uint32_t GeneratedThisReal = 0;
    bool AllowGenerated = false;
};

[[nodiscard]] constexpr FramePacingState UpdateFramePacing(const FramePacingConfig& InConfig,
                                                           FramePacingState InState, float InDeltaMs,
                                                           bool InRealFrame) noexcept
{
    const float ScopedInterval = InConfig.TargetFps > 0.0f ? 1000.0f / InConfig.TargetFps : 16.6667f;
    if (InRealFrame)
    {
        InState.AccumulatedMs = std::min(InState.AccumulatedMs + InDeltaMs, ScopedInterval * 2.0f);
        InState.GeneratedThisReal = 0;
        InState.AllowGenerated = InState.AccumulatedMs >= ScopedInterval &&
                                 InConfig.MaxLatencyMs <= ScopedInterval * 2.0f;
        return InState;
    }
    if (!InState.AllowGenerated || InState.GeneratedThisReal >= InConfig.MaxGeneratedPerReal)
    {
        InState.AllowGenerated = false;
        return InState;
    }
    InState.AccumulatedMs = std::max(InState.AccumulatedMs - ScopedInterval, 0.0f);
    ++InState.GeneratedThisReal;
    return InState;
}

}
