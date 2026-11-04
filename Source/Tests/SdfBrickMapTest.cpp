// FemboyEngine2D - sparse SDF brick map tests.
#include "Engine/Physics/SdfBrickMap.hpp"

#include <algorithm>
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

bool Near(float InA, float InB, float InTolerance = 0.01f)
{
    return std::fabs(InA - InB) <= InTolerance;
}

float BoxDistance(float InX, float InY, float InCenterX, float InCenterY, float InHalfX, float InHalfY)
{
    const float ScopedQx = std::fabs(InX - InCenterX) - InHalfX;
    const float ScopedQy = std::fabs(InY - InCenterY) - InHalfY;
    const float ScopedOutX = std::max(ScopedQx, 0.0f);
    const float ScopedOutY = std::max(ScopedQy, 0.0f);
    return std::sqrt(ScopedOutX * ScopedOutX + ScopedOutY * ScopedOutY) + std::min(std::max(ScopedQx, ScopedQy), 0.0f);
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
    Check(ScopedMap.Build(ScopedField, 32.0f), "the brick map builds");
    Check(ScopedMap.BricksX() == 4u && ScopedMap.BricksY() == 4u, "the brick grid matches the source");
    Check(ScopedMap.BrickCount() == 16u, "all brick headers exist");
    Check(ScopedMap.NarrowBrickCount() > 0u, "surface bricks are narrow");
    Check(ScopedMap.PayloadCount() == ScopedMap.NarrowBrickCount() * SdfBrickTexels,
          "the payload has one block per narrow brick");
    Check(ScopedMap.PayloadCount() < ScopedSamples.size() * 2u, "the sparse payload is bounded");

    Check(ScopedMap.Sample(Vector2D{128.0f, 128.0f}) < 0.0f, "the box centre is solid");
    Check(ScopedMap.Sample(Vector2D{10.0f, 10.0f}) > 0.0f, "empty space is positive");
    Check(Near(ScopedMap.Sample(Vector2D{128.0f, 128.0f}), ScopedField.Sample(Vector2D{128.0f, 128.0f}), 2.0f),
          "the solid sample matches the dense field");
    Check(Near(ScopedMap.Sample(Vector2D{80.0f, 128.0f}), ScopedField.Sample(Vector2D{80.0f, 128.0f}), 2.0f),
          "a surface sample matches the dense field");

    const float ScopedTrace = ScopedMap.Trace(Vector2D{10.0f, 128.0f}, Vector2D{1.0f, 0.0f}, 512.0f, 64u);
    Check(Near(ScopedTrace, 0.0f, 0.01f), "a ray toward the box hits the surface");

    const std::vector<SdfBrickGpu> ScopedGpuTable = ScopedMap.BuildGpuTable();
    Check(ScopedGpuTable.size() == ScopedMap.BrickCount(), "the GPU table mirrors the brick table");
    std::size_t ScopedGpuNarrow = 0;
    for (const SdfBrickGpu& ScopedBrick : ScopedGpuTable)
    {
        ScopedGpuNarrow += (ScopedBrick.StateAndFlags & 3u) == static_cast<std::uint32_t>(SdfBrickState::Narrow)
                               ? 1u
                               : 0u;
    }
    Check(ScopedGpuNarrow == ScopedMap.NarrowBrickCount(), "GPU brick states match the CPU classifier");
    const std::vector<float> ScopedGpuPayload = ScopedMap.BuildGpuPayload();
    Check(ScopedGpuPayload.size() == ScopedMap.PayloadCount(), "the GPU payload mirrors the sparse pool");

    if (GFailures == 0)
    {
        std::printf("sdf_brick_map: OK\n");
        return 0;
    }
    std::fprintf(stderr, "sdf_brick_map: %d failure(s)\n", GFailures);
    return 1;
}
