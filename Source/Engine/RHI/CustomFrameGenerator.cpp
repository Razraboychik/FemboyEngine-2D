// FemboyEngine2D - portable frame generation backend.
#include "Engine/RHI/CustomFrameGenerator.hpp"

namespace Fby::Rhi
{

bool CustomFrameGenerator::Initialize(RHI& InGpu, Extent2D InRenderExtent, Extent2D InOutputExtent)
{
    if (MHistory.IsValid() && MExtent.width == InRenderExtent.width &&
        MExtent.height == InRenderExtent.height && MOutputExtent.width == InOutputExtent.width &&
        MOutputExtent.height == InOutputExtent.height)
    {
        return true;
    }
    Shutdown(InGpu);
    RenderTargetDesc ScopedDesc;
    ScopedDesc.size = InRenderExtent;
    ScopedDesc.srgb = false;
    MHistory = InGpu.CreateRenderTarget(ScopedDesc);
    MDepthHistory = InGpu.CreateRenderTarget(ScopedDesc);
    if (!MHistory.IsValid() || !MDepthHistory.IsValid())
    {
        Shutdown(InGpu);
        return false;
    }
    MExtent = InRenderExtent;
    MOutputExtent = InOutputExtent;
    MHistoryValid = false;
    MDepthHistoryValid = false;
    return true;
}

bool CustomFrameGenerator::Generate(RHI& InGpu, const FrameGenerationInput& InInput)
{
    if (!MHistory.IsValid() || !InInput.Color.IsValid() || !InInput.Motion.IsValid() ||
        !InInput.Output.IsValid() || !InInput.HudSeparated ||
        InInput.RenderExtent.width != MExtent.width || InInput.RenderExtent.height != MExtent.height ||
        InInput.OutputExtent.width != MOutputExtent.width || InInput.OutputExtent.height != MOutputExtent.height)
    {
        return false;
    }
    const FrameGenerationStatus ScopedStatus =
        EvaluateFrameGeneration(InInput.Policy, InInput.Fps, InInput.ResetHistory);
    if (ScopedStatus != FrameGenerationStatus::Ready && ScopedStatus != FrameGenerationStatus::ResetHistory)
    {
        return false;
    }
    MPacingState = UpdateFramePacing(MPacingConfig, MPacingState, InInput.DeltaMs, InInput.RealFrame);
    if (!MPacingState.AllowGenerated && !InInput.ResetHistory)
    {
        return false;
    }
    const bool ScopedReset = InInput.ResetHistory || !MHistoryValid || !MDepthHistoryValid;
    if (!InGpu.TemporalResolve(InInput.Color, MHistory, InInput.Motion, InInput.Depth, MDepthHistory,
                               InInput.Output, 0.5f, 1.0f, ScopedReset))
    {
        return false;
    }
    if (!InGpu.CopyColorImage(InInput.Color, MHistory))
    {
        return false;
    }
    if (InInput.Depth.IsValid())
    {
        if (!InGpu.CopyDepth(InInput.Depth, MDepthHistory))
        {
            MHistoryValid = false;
            MDepthHistoryValid = false;
            return false;
        }
        MDepthHistoryValid = true;
    }
    else
    {
        MDepthHistoryValid = false;
    }
    MHistoryValid = true;
    return true;
}

void CustomFrameGenerator::ResetHistory() noexcept
{
    MHistoryValid = false;
}

void CustomFrameGenerator::Shutdown(RHI& InGpu) noexcept
{
    if (MHistory.IsValid())
    {
        InGpu.DestroyRenderTarget(MHistory);
    }
    if (MDepthHistory.IsValid())
    {
        InGpu.DestroyRenderTarget(MDepthHistory);
    }
    MHistory = {};
    MDepthHistory = {};
    MExtent = {};
    MHistoryValid = false;
}

}
