// FemboyEngine2D - engine-side FSR2-compatible temporal + spatial chain.
#pragma once

#include "Engine/RHI/RHI.hpp"
#include "Engine/RHI/TemporalResolvePass.hpp"

namespace Fby::Rhi
{

class Fsr2Pass
{
public:
    [[nodiscard]] bool Ensure(RHI& InGpu, Extent2D InRenderExtent, Extent2D InOutputExtent);
    [[nodiscard]] bool Resolve(RHI& InGpu, RenderTargetHandle InCurrent, RenderTargetHandle InMotion,
                               RenderTargetHandle InOutput, bool InResetHistory, float InSharpness = 0.35f);
    void Release(RHI& InGpu) noexcept;

    [[nodiscard]] bool IsValid() const noexcept
    {
        return MTemporal.IsValid() && MIntermediate.IsValid();
    }

private:
    TemporalResolvePass MTemporal;
    RenderTargetHandle MIntermediate{};
    Extent2D MRenderExtent{};
    Extent2D MOutputExtent{};
};

}
