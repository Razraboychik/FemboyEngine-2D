// FemboyEngine2D - FidelityFX FSR3 frame generation backend.
#pragma once

#include "Engine/RHI/FrameGeneration.hpp"
#include "Engine/RHI/FramePacing.hpp"

namespace Fby::Rhi
{

class Fsr3FrameGenerator final : public IFrameGenerator
{
public:
    [[nodiscard]] bool Initialize(RHI& InGpu, Extent2D InRenderExtent, Extent2D InOutputExtent) override;
    [[nodiscard]] bool Generate(RHI& InGpu, const FrameGenerationInput& InInput) override;
    void ResetHistory() noexcept override;
    void Shutdown(RHI& InGpu) noexcept override;

private:
    void* MContext = nullptr;
    Extent2D MOutputExtent{};
    std::uint64_t MFrameId = 0;
    bool MReset = false;
    bool MDx12 = false;
    FramePacingConfig MPacingConfig{};
    FramePacingState MPacingState{};
};

}
