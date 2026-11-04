// FemboyEngine2D - OIT accumulation/reveal pass.
#include "Engine/RHI/OitPass.hpp"

namespace Fby::Rhi
{

bool OitPass::Render(RHI& InGpu, const FramePacket& InPacket, Extent2D InExtent)
{
    if (InExtent.width == 0 || InExtent.height == 0 || InPacket.items.empty())
    {
        return false;
    }
    if (!MAccumulation.IsValid() || MExtent.width != InExtent.width || MExtent.height != InExtent.height)
    {
        Release(InGpu);
        RenderTargetDesc ScopedAccumulationDesc;
        ScopedAccumulationDesc.size = InExtent;
        ScopedAccumulationDesc.blend = RenderBlendMode::Additive;
        MAccumulation = InGpu.CreateRenderTarget(ScopedAccumulationDesc);
        RenderTargetDesc ScopedRevealDesc;
        ScopedRevealDesc.size = InExtent;
        ScopedRevealDesc.blend = RenderBlendMode::Reveal;
        MRevealage = InGpu.CreateRenderTarget(ScopedRevealDesc);
        if (!MAccumulation.IsValid() || !MRevealage.IsValid())
        {
            Release(InGpu);
            return false;
        }
        MExtent = InExtent;
    }

    return InGpu.RenderToTarget(MAccumulation, InPacket) && InGpu.RenderToTarget(MRevealage, InPacket);
}

void OitPass::Release(RHI& InGpu) noexcept
{
    if (MAccumulation.IsValid())
    {
        InGpu.DestroyRenderTarget(MAccumulation);
        MAccumulation = {};
    }
    if (MRevealage.IsValid())
    {
        InGpu.DestroyRenderTarget(MRevealage);
        MRevealage = {};
    }
    MExtent = {};
}

}
