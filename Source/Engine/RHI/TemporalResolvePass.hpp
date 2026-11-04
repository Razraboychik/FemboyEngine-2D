// FemboyEngine2D - history ping-pong around the temporal resolve.
#pragma once

#include "Engine/RHI/RHI.hpp"

#include <array>

namespace Fby::Rhi
{

class TemporalResolvePass
{
public:
    [[nodiscard]] bool Ensure(RHI& InGpu, Extent2D InExtent);
    [[nodiscard]] bool Resolve(RHI& InGpu, RenderTargetHandle InCurrent, RenderTargetHandle InMotion,
                               bool InResetHistory, float InHistoryWeight = 0.9f);
    void Release(RHI& InGpu) noexcept;

    [[nodiscard]] RenderTargetHandle Current() const noexcept
    {
        return MHistory[MRead];
    }

    [[nodiscard]] bool IsValid() const noexcept
    {
        return MHistory[0].IsValid() && MHistory[1].IsValid();
    }

private:
    std::array<RenderTargetHandle, 2> MHistory{};
    RenderTargetHandle MDepthHistory{};
    RenderTargetHandle MCurrentDepth{};
    Extent2D MDepthExtent{};
    Extent2D MExtent{};
    std::uint32_t MRead = 0;
};

}
