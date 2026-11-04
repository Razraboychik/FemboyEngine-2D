// FemboyEngine2D - CPU SDF GI probe tests.
#include "Engine/Physics/SdfGi.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
int GFailures = 0;

void Check(bool InCondition, const char* InMessage)
{
    if (!InCondition)
    {
        std::fprintf(stderr, "FAIL: %s\n", InMessage);
        ++GFailures;
    }
}

float BoxDistance(float InX, float InY, float InCenterX, float InCenterY, float InHalfX, float InHalfY)
{
    const float ScopedQx = std::fabs(InX - InCenterX) - InHalfX;
    const float ScopedQy = std::fabs(InY - InCenterY) - InHalfY;
    const float ScopedOutX = std::max(ScopedQx, 0.0f);
    const float ScopedOutY = std::max(ScopedQy, 0.0f);
    return std::sqrt(ScopedOutX * ScopedOutX + ScopedOutY * ScopedOutY) +
           std::min(std::max(ScopedQx, ScopedQy), 0.0f);
}
}

int main()
{
    using namespace Fby;

    constexpr std::uint32_t ScopedWidth = 64;
    constexpr std::uint32_t ScopedHeight = 64;
    constexpr float ScopedCell = 4.0f;
    std::vector<float> ScopedSamples(ScopedWidth * ScopedHeight, 0.0f);
    for (std::uint32_t ScopedY = 0; ScopedY < ScopedHeight; ++ScopedY)
    {
        for (std::uint32_t ScopedX = 0; ScopedX < ScopedWidth; ++ScopedX)
        {
            const float ScopedWorldX = (static_cast<float>(ScopedX) + 0.5f) * ScopedCell;
            const float ScopedWorldY = (static_cast<float>(ScopedY) + 0.5f) * ScopedCell;
            ScopedSamples[static_cast<std::size_t>(ScopedY) * ScopedWidth + ScopedX] =
                BoxDistance(ScopedWorldX, ScopedWorldY, 128.0f, 128.0f, 40.0f, 24.0f);
        }
    }
    SdfField ScopedField;
    ScopedField.samples = ScopedSamples.data();
    ScopedField.width = ScopedWidth;
    ScopedField.height = ScopedHeight;
    ScopedField.origin = Vector2D{0.0f, 0.0f};
    ScopedField.cellSize = Vector2D{ScopedCell, ScopedCell};
    ScopedField.outsideDistance = 1000.0f;

    SdfBrickMap ScopedMap;
    Check(ScopedMap.Build(ScopedField, 32.0f), "brick map builds");

    SdfGiVolume ScopedVolume;
    Check(ScopedVolume.Build(ScopedMap, Vector2D{0.0f, 0.0f}, Vector2D{16.0f, 16.0f}, 16, 16, 128.0f),
          "GI volume builds");
    Check(ScopedVolume.ProbeCount() == 256u, "GI volume has one probe per cell");

    const SdfGiProbe* ScopedOpen = ScopedVolume.ProbeAt(Vector2D{10.0f, 10.0f});
    const SdfGiProbe* ScopedWall = ScopedVolume.ProbeAt(Vector2D{80.0f, 128.0f});
    const SdfGiProbe* ScopedSolid = ScopedVolume.ProbeAt(Vector2D{128.0f, 128.0f});
    Check(ScopedOpen != nullptr && ScopedWall != nullptr && ScopedSolid != nullptr, "probes are addressable");
    if (ScopedOpen != nullptr && ScopedWall != nullptr && ScopedSolid != nullptr)
    {
        Check(ScopedOpen->Occlusion < ScopedWall->Occlusion, "the open probe is less occluded than the wall probe");
        Check(ScopedSolid->Occlusion > 0.99f, "a probe inside solid geometry is fully occluded");
        Check(ScopedOpen->Openness > 0.0f, "the open probe sees some sky");
    }
    Check(ScopedVolume.ProbeAt(Vector2D{-1.0f, -1.0f}) == nullptr, "outside probes return null");
    Check(ScopedVolume.Update(ScopedMap, 0u, 2u), "a temporal probe slice updates");
    Check(!ScopedVolume.Update(ScopedMap, 2u, 2u), "an invalid slice is rejected");
    std::vector<GpuGiProbe> ScopedGpuProbes;
    Check(ScopedVolume.PackGpu(ScopedGpuProbes), "GI probes pack for the GPU");
    Check(ScopedGpuProbes.size() == 256u, "GPU probe count matches the volume");
    if (!ScopedGpuProbes.empty())
    {
        Check(ScopedGpuProbes[0].PositionAndRadius.Z > 0.0f, "GPU probe has a radius");
    }

    if (GFailures == 0)
    {
        std::printf("sdf_gi: OK\n");
        return 0;
    }
    std::fprintf(stderr, "sdf_gi: %d failure(s)\n", GFailures);
    return 1;
}
