// FemboyEngine2D - render-scale controller driven by GPU frame time.
#pragma once

#include <cstdint>

namespace Fby::Rhi
{

struct DynamicResolutionSettings
{
    float minScale = 0.5f;
    float maxScale = 1.0f;
    float targetMilliseconds = 16.6f;
    float step = 0.05f;
    // Ignore one-frame spikes; require this many frames on the same side.
    std::uint32_t settleFrames = 3;
};

class DynamicResolution
{
public:
    void Configure(const DynamicResolutionSettings& InSettings) noexcept;
    [[nodiscard]] float Update(float InFrameMilliseconds) noexcept;
    [[nodiscard]] float Scale() const noexcept
    {
        return MScale;
    }
    [[nodiscard]] const DynamicResolutionSettings& Settings() const noexcept
    {
        return MSettings;
    }

private:
    DynamicResolutionSettings MSettings{};
    float MScale = 1.0f;
    std::uint32_t MSlowFrames = 0;
    std::uint32_t MFastFrames = 0;
};

}
