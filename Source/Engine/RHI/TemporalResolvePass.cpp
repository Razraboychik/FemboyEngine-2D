// FemboyEngine2D - history ping-pong around the temporal resolve.
#include "Engine/RHI/TemporalResolvePass.hpp"

namespace Fby::Rhi
{

bool TemporalResolvePass::Ensure(RHI& InGpu, Extent2D InExtent)
{
    if (IsValid() && MExtent.width == InExtent.width && MExtent.height == InExtent.height)
    {
        return true;
    }
    Release(InGpu);
    RenderTargetDesc ScopedDesc;
    ScopedDesc.size = InExtent;
    ScopedDesc.srgb = false;
    MDepthHistory = InGpu.CreateRenderTarget(ScopedDesc);
    if (!MDepthHistory.IsValid())
    {
        Release(InGpu);
        return false;
    }
    MDepthExtent = InExtent;
    for (RenderTargetHandle& ScopedHistory : MHistory)
    {
        ScopedHistory = InGpu.CreateRenderTarget(ScopedDesc);
        if (!ScopedHistory.IsValid())
        {
            Release(InGpu);
            return false;
        }
    }
    MExtent = InExtent;
    MRead = 0;
    return true;
}

bool TemporalResolvePass::Resolve(RHI& InGpu, RenderTargetHandle InCurrent, RenderTargetHandle InMotion,
                                  bool InResetHistory, float InHistoryWeight)
{
    if (!IsValid() || !InCurrent.IsValid() || !InMotion.IsValid())
    {
        return false;
    }
    const std::uint32_t ScopedWrite = 1u - MRead;
    if (!InGpu.TemporalResolve(InCurrent, MHistory[MRead], InMotion, InCurrent, MDepthHistory,
                               MHistory[ScopedWrite], InHistoryWeight, 1.0f, InResetHistory))
    {
        return false;
    }
    (void)InGpu.CopyDepth(InCurrent, MDepthHistory);
    MRead = ScopedWrite;
    return true;
}

void TemporalResolvePass::Release(RHI& InGpu) noexcept
{
    for (RenderTargetHandle& ScopedHistory : MHistory)
    {
        if (ScopedHistory.IsValid())
        {
            InGpu.DestroyRenderTarget(ScopedHistory);
        }
        ScopedHistory = {};
    }
    if (MDepthHistory.IsValid())
    {
        InGpu.DestroyRenderTarget(MDepthHistory);
    }
    MDepthHistory = {};
    MDepthExtent = {};
    MExtent = {};
    MRead = 0;
}

}
