// FemboyEngine2D - CPU light grid tests.
#include "Engine/ECS/LightGrid.hpp"

#include <algorithm>
#include <cstdio>

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

bool Contains(const std::span<const std::uint32_t>& InIndices, std::uint32_t InValue)
{
    return std::find(InIndices.begin(), InIndices.end(), InValue) != InIndices.end();
}
}

int main()
{
    using namespace Fby;

    const LightGridInput ScopedLights[] = {
        LightGridInput{Vector2D{0.0f, 0.0f}, 20.0f},
        LightGridInput{Vector2D{100.0f, 0.0f}, 20.0f},
        LightGridInput{Vector2D{1000.0f, 0.0f}, 20.0f},
    };
    LightGrid2D ScopedGrid;
    ScopedGrid.Build(ScopedLights, 32.0f);
    Check(!ScopedGrid.Fallback(), "a compact grid does not fall back");
    Check(ScopedGrid.CellCount() > 0u, "the grid has cells");

    const auto ScopedNearFirst = ScopedGrid.Query(Vector2D{0.0f, 0.0f});
    Check(Contains(ScopedNearFirst, 0u), "the first light is in its own cell");
    Check(!Contains(ScopedNearFirst, 2u), "a distant light is not in the first cell");

    const auto ScopedNearSecond = ScopedGrid.Query(Vector2D{100.0f, 0.0f});
    Check(Contains(ScopedNearSecond, 1u), "the second light is in its own cell");
    Check(!Contains(ScopedNearSecond, 0u), "the first light does not leak into the second cell");

    Check(ScopedGrid.Query(Vector2D{500.0f, 0.0f}).empty(), "open space returns no lights");
    Check(ScopedGrid.Query(Vector2D{-1000.0f, -1000.0f}).empty(), "outside the grid returns no lights");

    const LightGridInput ScopedHuge[] = {
        LightGridInput{Vector2D{0.0f, 0.0f}, 20.0f},
        LightGridInput{Vector2D{100000.0f, 100000.0f}, 20.0f},
    };
    LightGrid2D ScopedFallback;
    ScopedFallback.Build(ScopedHuge, 1.0f);
    Check(ScopedFallback.Fallback(), "an oversized grid uses the fallback");
    Check(ScopedFallback.Query(Vector2D{0.0f, 0.0f}).size() == 2u, "the fallback returns every light");

    if (GFailures == 0)
    {
        std::printf("light_grid: OK\n");
        return 0;
    }
    std::fprintf(stderr, "light_grid: %d failure(s)\n", GFailures);
    return 1;
}
