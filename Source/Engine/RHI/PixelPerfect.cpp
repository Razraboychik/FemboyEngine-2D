#include "Engine/RHI/PixelPerfect.hpp"

#include <cmath>

namespace Fby
{
void SnapCameraToPixelGrid(Camera2D& InCamera, float InPixelsPerUnit) noexcept
{
    if (!(InPixelsPerUnit > 0.0f) || !std::isfinite(InPixelsPerUnit))
    {
        return;
    }
    const float ScopedStep = 1.0f / InPixelsPerUnit;
    if (!(ScopedStep > 0.0f) || !std::isfinite(ScopedStep))
    {
        return;
    }
    InCamera.Center.X = std::round(InCamera.Center.X / ScopedStep) * ScopedStep;
    InCamera.Center.Y = std::round(InCamera.Center.Y / ScopedStep) * ScopedStep;
}
}
