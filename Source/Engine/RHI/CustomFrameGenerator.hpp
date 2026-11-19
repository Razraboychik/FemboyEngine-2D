// FemboyEngine2D - portable frame generation backend.
#pragma once

#include "Engine/RHI/FrameGeneration.hpp"
#include "Engine/RHI/FramePacing.hpp"

namespace Fby::Rhi
{

class CustomFrameGenerator final : public IFrameGenerator
{
public:
    CustomFrameGenerator() = default;
    CustomFrameGenerator(const CustomFrameGenerator&) = delete;
    CustomFrameGenerator& operator=(const CustomFrameGenerator&) = delete;
    CustomFrameGenerator(CustomFrameGenerator&&) = delete;
    CustomFrameGenerator& operator=(CustomFrameGenerator&&) = delete;
    ~CustomFrameGenerator() override = default;
    [[nodiscard]] bool Initialize(RHI& InGpu, Extent2D InRenderExtent, Extent2D InOutputExtent) override;
    [[nodiscard]] bool Generate(RHI& InGpu, const FrameGenerationInput& InInput) override;
    void ResetHistory() noexcept override;
    void Shutdown(RHI& InGpu) noexcept override;

    // Optional pacing policy. TargetFps<=0 keeps the 60 Hz default.
    void SetPacingConfig(const FramePacingConfig& InConfig) noexcept
    {
        MPacingConfig = InConfig;
    }

private:
    RenderTargetHandle MHistory{};
    RenderTargetHandle MDepthHistory{};
    Extent2D MExtent{};
    Extent2D MOutputExtent{};
    bool MHistoryValid = false;
    bool MDepthHistoryValid = false;
    FramePacingConfig MPacingConfig{};
    FramePacingState MPacingState{};
};

}
