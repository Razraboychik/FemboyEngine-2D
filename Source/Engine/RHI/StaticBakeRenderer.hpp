// FemboyEngine2D - bakes a static-layer packet into a texture array.
#pragma once

#include "Engine/RHI/RHI.hpp"

#include <array>
#include <cstdint>

namespace Fby::Rhi
{

// Owns one offscreen target and the one-layer texture array that samples it.
// Baking is explicit: the caller decides when the static layer is dirty.
class StaticBakeRenderer
{
public:
    StaticBakeRenderer() = default;
    ~StaticBakeRenderer() = default;

    StaticBakeRenderer(const StaticBakeRenderer&) = delete;
    StaticBakeRenderer& operator=(const StaticBakeRenderer&) = delete;

    // Records the bake into the current frame. Call after EndFrame, once the
    // GPU has finished, then call Finalize to create the sampling descriptor.
    [[nodiscard]] bool Bake(RHI& InGpu, const FramePacket& InPacket, Extent2D InExtent);
    [[nodiscard]] bool Finalize(RHI& InGpu);
    void Release(RHI& InGpu) noexcept;

    [[nodiscard]] bool IsReady() const noexcept
    {
        return MTexture.IsValid();
    }
    [[nodiscard]] TextureArrayHandle Texture() const noexcept
    {
        return MTexture;
    }
    [[nodiscard]] RenderTargetHandle Target() const noexcept
    {
        return MTarget;
    }
    [[nodiscard]] const FramePacket& OverlayPacket() const noexcept
    {
        return MOverlay;
    }

private:
    RenderTargetHandle MTarget{};
    TextureArrayHandle MTexture{};
    Extent2D MExtent{};
    std::array<RenderItem, 1> MOverlayItems{};
    std::array<RenderKey, 1> MOverlayKeys{};
    std::array<DrawBatch, 1> MOverlayBatches{};
    FramePacket MOverlay{};
};

}
