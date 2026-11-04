// FemboyEngine2D - engine-side FSR2-compatible temporal + spatial chain.
#include "Engine/RHI/Fsr2Pass.hpp"

namespace Fby::Rhi
{

bool Fsr2Pass::Ensure(RHI& InGpu, Extent2D InRenderExtent, Extent2D InOutputExtent)
{
    if (IsValid() && MRenderExtent.width == InRenderExtent.width && MRenderExtent.height == InRenderExtent.height &&
        MOutputExtent.width == InOutputExtent.width && MOutputExtent.height == InOutputExtent.height)
    {
        return true;
    }
    Release(InGpu);
    if (!MTemporal.Ensure(InGpu, InRenderExtent))
    {
        return false;
    }
    RenderTargetDesc ScopedDesc;
    ScopedDesc.size = InOutputExtent;
    ScopedDesc.srgb = false;
    MIntermediate = InGpu.CreateRenderTarget(ScopedDesc);
    if (!MIntermediate.IsValid())
    {
        Release(InGpu);
        return false;
    }
    MRenderExtent = InRenderExtent;
    MOutputExtent = InOutputExtent;
    return true;
}

bool Fsr2Pass::Resolve(RHI& InGpu, RenderTargetHandle InCurrent, RenderTargetHandle InMotion,
                       RenderTargetHandle InOutput, bool InResetHistory, float InSharpness)
{
    if (!IsValid() || !MTemporal.Resolve(InGpu, InCurrent, InMotion, InResetHistory))
    {
        return false;
    }
    if (!InGpu.UpscaleRenderTarget(MTemporal.Current(), MIntermediate, SpatialUpscaleMode::Fsr1))
    {
        return false;
    }
    return InGpu.RcasSharpen(MIntermediate, InOutput, InSharpness);
}

void Fsr2Pass::Release(RHI& InGpu) noexcept
{
    MTemporal.Release(InGpu);
    if (MIntermediate.IsValid())
    {
        InGpu.DestroyRenderTarget(MIntermediate);
    }
    MIntermediate = {};
    MRenderExtent = {};
    MOutputExtent = {};
}

}
