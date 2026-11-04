// FemboyEngine2D - weighted blended OIT accumulation/reveal targets.
#pragma once

#include "Engine/RHI/RHI.hpp"

namespace Fby::Rhi
{

// Records the transparent packet into two OIT targets. The resolve/composite
// step is separate so it can be shared by the swapchain and editor viewports.
class OitPass
{
public:
    [[nodiscard]] bool Render(RHI& InGpu, const FramePacket& InPacket, Extent2D InExtent);
    void Release(RHI& InGpu) noexcept;

    [[nodiscard]] RenderTargetHandle Accumulation() const noexcept
    {
        return MAccumulation;
    }
    [[nodiscard]] RenderTargetHandle Revealage() const noexcept
    {
        return MRevealage;
    }
    [[nodiscard]] bool IsReady() const noexcept
    {
        return MAccumulation.IsValid() && MRevealage.IsValid();
    }

private:
    RenderTargetHandle MAccumulation{};
    RenderTargetHandle MRevealage{};
    Extent2D MExtent{};
};

}
