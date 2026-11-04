// FemboyEngine2D - portable frame generation backend.
#pragma once

#include "Engine/RHI/FrameGeneration.hpp"
#include "Engine/RHI/FramePacing.hpp"

namespace Fby::Rhi
{

class CustomFrameGenerator final : public IFrameGenerator
{
public:
    [[nodiscard]] bool Initialize(RHI& InGpu, Extent2D InRenderExtent, Extent2D InOutputExtent) override;
    [[nodiscard]] bool Generate(RHI& InGpu, const FrameGenerationInput& InInput) override;
    void ResetHistory() noexcept override;
    void Shutdown(RHI& InGpu) noexcept override;

private:
    RenderTargetHandle MHistory{};
    RenderTargetHandle MDepthHistory{};
    Extent2D MExtent{};
    bool MHistoryValid = false;
    FramePacingConfig MPacingConfig{};
    FramePacingState MPacingState{};
};

}
