// FemboyEngine2D - FidelityFX FSR2 adapter contract.
#pragma once

#include <cstdint>
#include <initializer_list>

namespace Fby::Rhi
{

enum class Fsr2QualityMode : std::uint8_t
{
    Quality = 0,
    Balanced = 1,
    Performance = 2,
    UltraPerformance = 3
};

[[nodiscard]] constexpr float Fsr2RenderScale(Fsr2QualityMode InMode) noexcept
{
    switch (InMode)
    {
    case Fsr2QualityMode::Quality:
        return 0.67f;
    case Fsr2QualityMode::Balanced:
        return 0.58f;
    case Fsr2QualityMode::Performance:
        return 0.5f;
    case Fsr2QualityMode::UltraPerformance:
        return 0.33f;
    }
    return 1.0f;
}

// The nearest standard FSR2 mode for the render/upscale extents. Used to forward
// an explicit quality choice and to validate that Dispatch receives sane sizes.
[[nodiscard]] constexpr Fsr2QualityMode Fsr2QualityForExtent(std::uint32_t InRenderWidth,
                                                             std::uint32_t InRenderHeight,
                                                             std::uint32_t InOutputWidth,
                                                             std::uint32_t InOutputHeight) noexcept
{
    if (InOutputWidth == 0 || InOutputHeight == 0)
    {
        return Fsr2QualityMode::Quality;
    }
    const float ScopedRatio =
        (static_cast<float>(InRenderWidth) / static_cast<float>(InOutputWidth) +
         static_cast<float>(InRenderHeight) / static_cast<float>(InOutputHeight)) *
        0.5f;
    float ScopedBest = 1.0f;
    Fsr2QualityMode ScopedMode = Fsr2QualityMode::Quality;
    for (const Fsr2QualityMode ScopedCandidate : {Fsr2QualityMode::Quality, Fsr2QualityMode::Balanced,
                                                  Fsr2QualityMode::Performance, Fsr2QualityMode::UltraPerformance})
    {
        const float ScopedDelta = ScopedRatio - Fsr2RenderScale(ScopedCandidate);
        const float ScopedDistance = ScopedDelta < 0.0f ? -ScopedDelta : ScopedDelta;
        if (ScopedDistance < ScopedBest)
        {
            ScopedBest = ScopedDistance;
            ScopedMode = ScopedCandidate;
        }
    }
    return ScopedMode;
}

// Tolerance absorbs rounding of odd extents (for example 1919 * 0.67).
[[nodiscard]] constexpr bool Fsr2RenderSizeMatches(Fsr2QualityMode InMode, std::uint32_t InRenderWidth,
                                                   std::uint32_t InRenderHeight, std::uint32_t InOutputWidth,
                                                   std::uint32_t InOutputHeight,
                                                   std::uint32_t InTolerance = 2) noexcept
{
    if (InRenderWidth == 0 || InRenderHeight == 0 || InOutputWidth == 0 || InOutputHeight == 0 ||
        InRenderWidth > InOutputWidth || InRenderHeight > InOutputHeight)
    {
        return false;
    }
    const float ScopedScale = Fsr2RenderScale(InMode);
    const float ScopedDeltaWidth =
        static_cast<float>(InRenderWidth) - static_cast<float>(InOutputWidth) * ScopedScale;
    const float ScopedDeltaHeight =
        static_cast<float>(InRenderHeight) - static_cast<float>(InOutputHeight) * ScopedScale;
    const float ScopedTolerance = static_cast<float>(InTolerance);
    return (ScopedDeltaWidth < 0.0f ? -ScopedDeltaWidth : ScopedDeltaWidth) <= ScopedTolerance &&
           (ScopedDeltaHeight < 0.0f ? -ScopedDeltaHeight : ScopedDeltaHeight) <= ScopedTolerance;
}

class Fsr2Adapter
{
public:
    [[nodiscard]] bool Initialize(void* InDevice, void* InPhysicalDevice, void* InDeviceProcAddr,
                                  std::uint32_t InMaxRenderWidth, std::uint32_t InMaxRenderHeight,
                                  std::uint32_t InMaxOutputWidth, std::uint32_t InMaxOutputHeight);
    [[nodiscard]] bool IsAvailable() const noexcept
    {
        return MAvailable;
    }
    [[nodiscard]] bool Dispatch(void* InCommandBuffer, void* InColor, void* InDepth, void* InMotion,
                                void* InReactive, void* InOutput, std::uint32_t InRenderWidth,
                                std::uint32_t InRenderHeight, std::uint32_t InOutputWidth,
                                std::uint32_t InOutputHeight, Fsr2QualityMode InQuality, float InJitterX,
                                float InJitterY, bool InReset);
    void Shutdown() noexcept;

private:
    bool MAvailable = false;
    void* MContext = nullptr;
    std::uint32_t MMaxRenderWidth = 0;
    std::uint32_t MMaxRenderHeight = 0;
    std::uint32_t MMaxOutputWidth = 0;
    std::uint32_t MMaxOutputHeight = 0;
};

}
