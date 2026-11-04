// FemboyEngine2D - portable frame generation backend.
#include "Engine/RHI/CustomFrameGenerator.hpp"

namespace Fby::Rhi
{

bool CustomFrameGenerator::Initialize(RHI& InGpu, Extent2D InRenderExtent, Extent2D InOutputExtent)
{
    (void)InOutputExtent;
    if (MHistory.IsValid() && MExtent.width == InRenderExtent.width && MExtent.height == InRenderExtent.height)
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
    MHistoryValid = false;
    return true;
}

bool CustomFrameGenerator::Generate(RHI& InGpu, const FrameGenerationInput& InInput)
{
    if (!MHistory.IsValid() || !InInput.Color.IsValid() || !InInput.Motion.IsValid() ||
        !InInput.Output.IsValid() || !InInput.HudSeparated)
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
    if (!InGpu.TemporalResolve(InInput.Color, MHistory, InInput.Motion, InInput.Color, MDepthHistory,
                               InInput.Output, 0.5f, 1.0f, InInput.ResetHistory || !MHistoryValid))
    {
        return false;
    }
    if (!InGpu.CopyColorImage(InInput.Color, MHistory))
    {
        return false;
    }
    if (InInput.Depth.IsValid())
    {
        (void)InGpu.CopyDepth(InInput.Color, MDepthHistory);
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
