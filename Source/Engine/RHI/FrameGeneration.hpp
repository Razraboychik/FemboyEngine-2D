// FemboyEngine2D - vendor-neutral frame generation contract.
#pragma once

#include "Engine/RHI/RHI.hpp"
#include "Engine/RHI/FrameGenerationPolicy.hpp"

namespace Fby::Rhi
{

struct FrameGenerationInput
{
    RenderTargetHandle Color{};
    RenderTargetHandle Depth{};
    RenderTargetHandle Motion{};
    RenderTargetHandle History{};
    RenderTargetHandle Output{};
    Extent2D RenderExtent{};
    Extent2D OutputExtent{};
    bool ResetHistory = false;
    bool HudSeparated = true;
    float Fps = 60.0f;
    float DeltaMs = 16.6667f;
    bool RealFrame = true;
    FrameGenerationPolicy Policy{};
};

class IFrameGenerator
{
public:
    virtual ~IFrameGenerator() = default;

    [[nodiscard]] virtual bool Initialize(RHI& InGpu, Extent2D InRenderExtent, Extent2D InOutputExtent) = 0;
    [[nodiscard]] virtual bool Generate(RHI& InGpu, const FrameGenerationInput& InInput) = 0;
    virtual void ResetHistory() noexcept = 0;
    virtual void Shutdown(RHI& InGpu) noexcept = 0;
};

}
