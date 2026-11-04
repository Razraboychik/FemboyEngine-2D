// FemboyEngine2D - optional DLSS Super Resolution adapter.
#pragma once

#include "Engine/RHI/RHI.hpp"

namespace Fby::Rhi
{

enum class DlssQualityMode : std::uint8_t
{
    Performance = 0,
    Balanced = 1,
    Quality = 2,
    UltraPerformance = 3
};

[[nodiscard]] constexpr float DlssRenderScale(DlssQualityMode InMode) noexcept
{
    switch (InMode)
    {
    case DlssQualityMode::Performance:
        return 0.5f;
    case DlssQualityMode::Balanced:
        return 0.58f;
    case DlssQualityMode::Quality:
        return 0.67f;
    case DlssQualityMode::UltraPerformance:
        return 0.33f;
    }
    return 1.0f;
}

class DlssAdapter
{
public:
    [[nodiscard]] bool Initialize(RHI& InGpu);
    [[nodiscard]] bool CreateFeature(void* InCommandBuffer, void* InColor, void* InDepth, void* InMotion,
                                     void* InOutput, std::uint32_t InWidth, std::uint32_t InHeight,
                                     DlssQualityMode InQuality);
    void SetJitter(float InX, float InY) noexcept;
    [[nodiscard]] bool CreateFrameGeneration(void* InCommandBuffer, void* InBackbuffer, void* InDepth,
                                             void* InMotion, void* InHudless, void* InUi, void* InOutput,
                                             std::uint32_t InWidth, std::uint32_t InHeight);
    [[nodiscard]] bool EvaluateFrameGeneration(void* InCommandBuffer);
    [[nodiscard]] bool Evaluate(void* InCommandBuffer);
    [[nodiscard]] bool IsAvailable() const noexcept
    {
        return MAvailable;
    }
    [[nodiscard]] bool IsFrameGenerationAvailable() const noexcept
    {
        return MFgAvailable;
    }
    void Shutdown(RHI& InGpu) noexcept;

private:
    bool MAvailable = false;
    bool MFgAvailable = false;
    bool MInitialized = false;
    void* MParameters = nullptr;
    void* MFeature = nullptr;
    void* MFgFeature = nullptr;
    void* MDevice = nullptr;
};

}
