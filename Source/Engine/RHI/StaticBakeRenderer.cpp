// FemboyEngine2D - static bake renderer implementation.
#include "Engine/RHI/StaticBakeRenderer.hpp"

namespace Fby::Rhi
{

bool StaticBakeRenderer::Bake(RHI& InGpu, const FramePacket& InPacket, Extent2D InExtent)
{
    if (InExtent.width == 0 || InExtent.height == 0)
    {
        return false;
    }
    if (!MTarget.IsValid() || MExtent.width != InExtent.width || MExtent.height != InExtent.height)
    {
        Release(InGpu);
        RenderTargetDesc ScopedDesc;
        ScopedDesc.size = InExtent;
        MTarget = InGpu.CreateRenderTarget(ScopedDesc);
        if (!MTarget.IsValid())
        {
            return false;
        }
        MExtent = InExtent;
    }

    if (!InGpu.RenderToTarget(MTarget, InPacket))
    {
        return false;
    }
    return true;
}

bool StaticBakeRenderer::Finalize(RHI& InGpu)
{
    if (!MTarget.IsValid() || MExtent.width == 0 || MExtent.height == 0)
    {
        return false;
    }
    InGpu.WaitIdle();
    if (MTexture.IsValid())
    {
        InGpu.DestroyTextureArray(MTexture);
        MTexture = {};
    }
    MTexture = InGpu.CreateTextureArrayFromRenderTarget(MTarget);
    if (!MTexture.IsValid())
    {
        return false;
    }

    MOverlayItems[0] = RenderItem::Quad(
        Vector2D{static_cast<float>(MExtent.width) * 0.5f, static_cast<float>(MExtent.height) * 0.5f},
        Vector2D{static_cast<float>(MExtent.width), static_cast<float>(MExtent.height)});
    MOverlayItems[0].flags = EncodeDepth(MOverlayItems[0].flags, 0.5f, true);
    MOverlayKeys[0] = MakeRenderKey(0, 0.0f);
    MOverlayBatches[0] = DrawBatch{0, 1, 0, 0};

    MOverlay = FramePacket{};
    MOverlay.items = MOverlayItems;
    MOverlay.keys = MOverlayKeys;
    MOverlay.batches = MOverlayBatches;
    MOverlay.textureArray = MTexture.id;
    MOverlay.camera = CameraData{Vector2D{static_cast<float>(MExtent.width) * 0.5f,
                                          static_cast<float>(MExtent.height) * 0.5f},
                                 Vector2D{static_cast<float>(MExtent.width), static_cast<float>(MExtent.height)}, 1.0f};
    return true;
}

void StaticBakeRenderer::Release(RHI& InGpu) noexcept
{
    if (MTexture.IsValid())
    {
        InGpu.DestroyTextureArray(MTexture);
        MTexture = {};
    }
    if (MTarget.IsValid())
    {
        InGpu.DestroyRenderTarget(MTarget);
        MTarget = {};
    }
    MExtent = {};
}

}
