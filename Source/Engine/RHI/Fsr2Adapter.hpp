// FemboyEngine2D - FidelityFX FSR2 adapter contract.
#pragma once

#include <cstdint>

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
                                void* InReactive, void* InOutput, std::uint32_t InRenderWidth, std::uint32_t InRenderHeight,
                                std::uint32_t InOutputWidth, std::uint32_t InOutputHeight, float InJitterX,
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
