// FemboyEngine2D - camera snapping for pixel-art rendering.
#pragma once

#include "Engine/ECS/CoreComponents.hpp"

namespace Fby
{
// Snaps the camera centre to the nearest 1/PPU world step. The caller keeps the
// integer zoom and render target; this only removes sub-pixel drift.
void SnapCameraToPixelGrid(Camera2D& InCamera, float InPixelsPerUnit) noexcept;
}
