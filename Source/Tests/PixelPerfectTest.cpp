#include "Engine/RHI/PixelPerfect.hpp"

#include <cmath>
#include <limits>
#include <cstdio>

namespace
{
std::uint32_t GFailures = 0;

void Check(bool InCondition, const char* InMessage)
{
    if (!InCondition)
    {
        std::fprintf(stderr, "FAIL: %s\n", InMessage);
        ++GFailures;
    }
}
}

int main()
{
    Fby::Camera2D ScopedCamera;
    ScopedCamera.Center = {10.4f, -3.2f};
    Fby::SnapCameraToPixelGrid(ScopedCamera, 1.0f);
    Check(ScopedCamera.Center.X == 10.0f && ScopedCamera.Center.Y == -3.0f, "one pixel per unit snaps to integers");

    ScopedCamera.Center = {10.4f, 10.4f};
    Fby::SnapCameraToPixelGrid(ScopedCamera, 16.0f);
    Check(ScopedCamera.Center.X == 10.375f && ScopedCamera.Center.Y == 10.375f, "pixel step uses 1/PPU");

    ScopedCamera.Center = {1.25f, 2.75f};
    Fby::SnapCameraToPixelGrid(ScopedCamera, 0.0f);
    Check(ScopedCamera.Center.X == 1.25f && ScopedCamera.Center.Y == 2.75f, "invalid PPU leaves the camera");
    Fby::SnapCameraToPixelGrid(ScopedCamera, std::numeric_limits<float>::quiet_NaN());
    Check(ScopedCamera.Center.X == 1.25f && ScopedCamera.Center.Y == 2.75f, "NaN PPU leaves the camera");

    for (std::uint32_t ScopedIteration = 0; ScopedIteration < 1000000; ++ScopedIteration)
    {
        ScopedCamera.Center = {static_cast<float>(ScopedIteration % 97) + 0.31f, static_cast<float>(ScopedIteration % 53) - 0.7f};
        Fby::SnapCameraToPixelGrid(ScopedCamera, 8.0f);
    }
    Check(std::isfinite(ScopedCamera.Center.X) && std::isfinite(ScopedCamera.Center.Y), "stress stays finite");

    if (GFailures == 0)
    {
        std::printf("pixel_perfect_test: all checks passed\n");
        return 0;
    }
    return 1;
}
