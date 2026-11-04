// FemboyEngine2D - temporal upscaler contract. No backend is built in.
#pragma once

#include "Engine/RHI/RHI.hpp"

#include <cstdint>

namespace Fby::Rhi
{

struct TemporalUpscaleInput
{
    NativeTextureView color{};
    NativeTextureView depth{};
    NativeTextureView motionVectors{};
    NativeTextureView output{};
    Extent2D renderExtent{};
    Extent2D outputExtent{};
    // Current jitter offset in render pixels and the camera delta used by 2D
    // motion vectors.
    Vector2D jitter{};
    Vector2D cameraDelta{};
    float exposure = 1.0f;
    bool historyValid = false;
    bool resetHistory = false;
};

class ITemporalUpscaler
{
public:
    virtual ~ITemporalUpscaler() = default;

    [[nodiscard]] virtual bool Initialize(RHI& InGpu, Extent2D InOutputExtent, Extent2D InRenderExtent) = 0;
    [[nodiscard]] virtual bool Evaluate(const TemporalUpscaleInput& InInput) = 0;
    virtual void ResetHistory() noexcept = 0;
    virtual void Shutdown(RHI& InGpu) noexcept = 0;
};

}
