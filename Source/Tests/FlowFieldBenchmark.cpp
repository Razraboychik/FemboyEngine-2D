// FemboyEngine2D - flow field build benchmark.
#include "Engine/Navigation/FlowField.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>

int main()
{
    constexpr std::uint32_t kSide = 256;
    Fby::Navigation::FlowFieldDesc ScopedDesc;
    ScopedDesc.Width = kSide;
    ScopedDesc.Height = kSide;

    Fby::Navigation::FlowField ScopedField;
    if (!ScopedField.Configure(ScopedDesc))
    {
        std::fprintf(stderr, "flow_field_benchmark: configure failed\n");
        return 1;
    }
    // Deterministic obstacles: every eleventh cell is blocked.
    for (std::uint32_t ScopedY = 0; ScopedY < kSide; ++ScopedY)
    {
        for (std::uint32_t ScopedX = 0; ScopedX < kSide; ++ScopedX)
        {
            const std::uint32_t ScopedIndex = ScopedY * kSide + ScopedX;
            if (ScopedIndex % 11u == 0)
            {
                (void)ScopedField.SetCost(ScopedX, ScopedY, 0);
            }
        }
    }
    std::vector<std::uint32_t> ScopedGoals{kSide + 1u, kSide * 2u + 3u, kSide * 9u + 5u};
    const auto ScopedStart = std::chrono::steady_clock::now();
    ScopedField.Build(ScopedGoals);
    const auto ScopedEnd = std::chrono::steady_clock::now();
    const double ScopedMilliseconds = std::chrono::duration<double, std::milli>(ScopedEnd - ScopedStart).count();
    std::printf("flow_field_benchmark: %ux%u build %.3f ms, %u reachable of %u\n", kSide, kSide,
                ScopedMilliseconds, ScopedField.ReachableCount(), kSide * kSide);

    if (ScopedField.ReachableCount() == 0)
    {
        std::fprintf(stderr, "flow_field_benchmark: no reachable cells\n");
        return 1;
    }
    if (ScopedField.PotentialAt(2, 2) == Fby::Navigation::kFlowFieldInfinite)
    {
        std::fprintf(stderr, "flow_field_benchmark: expected (2,2) to be reachable\n");
        return 1;
    }

#if defined(NDEBUG)
    constexpr double kBuildBudgetMilliseconds = 12.0;
    if (ScopedMilliseconds > kBuildBudgetMilliseconds)
    {
        std::fprintf(stderr, "flow_field_benchmark: build over the %.1f ms budget\n", kBuildBudgetMilliseconds);
        return 1;
    }
#endif
    std::printf("flow_field_benchmark: OK\n");
    return 0;
}
