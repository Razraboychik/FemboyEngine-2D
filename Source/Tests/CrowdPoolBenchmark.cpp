// FemboyEngine2D - crowd pool benchmark.
#include "Engine/Navigation/CrowdPool.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>

int main()
{
    constexpr std::uint32_t kAgents = 16384;
    Fby::Navigation::CrowdPool ScopedPool;
    if (!ScopedPool.Configure(kAgents))
    {
        std::fprintf(stderr, "crowd_benchmark: configure failed\n");
        return 1;
    }
    std::uint32_t ScopedState = 12345u;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < kAgents; ++ScopedIndex)
    {
        ScopedState = ScopedState * 1664525u + 1013904223u;
        const float ScopedX = static_cast<float>(ScopedState % 2048u);
        ScopedState = ScopedState * 1664525u + 1013904223u;
        const float ScopedY = static_cast<float>(ScopedState % 2048u);
        if (ScopedPool.Add(Fby::Vector2D{ScopedX, ScopedY}, ScopedIndex % 4u, Fby::Vector2D{1024.0f, 1024.0f}) ==
            0xFFFFFFFFu)
        {
            std::fprintf(stderr, "crowd_benchmark: pool is full\n");
            return 1;
        }
    }

    Fby::Navigation::CrowdSettings ScopedSettings;
    ScopedSettings.PerceptionRadius = 24.0f;
    ScopedSettings.MaxNeighbours = 16;

    // Warm the grid and the steering buffers before the measured steps.
    ScopedPool.Step(1.0f / 60.0f, ScopedSettings);
    constexpr std::uint32_t kMeasuredSteps = 10;
    const auto ScopedStart = std::chrono::steady_clock::now();
    for (std::uint32_t ScopedStep = 0; ScopedStep < kMeasuredSteps; ++ScopedStep)
    {
        ScopedPool.Step(1.0f / 60.0f, ScopedSettings);
    }
    const auto ScopedEnd = std::chrono::steady_clock::now();
    const double ScopedMilliseconds =
        std::chrono::duration<double, std::milli>(ScopedEnd - ScopedStart).count() / kMeasuredSteps;
    std::printf("crowd_benchmark: %u agents, %.3f ms per step\n", ScopedPool.Count(), ScopedMilliseconds);

#if defined(NDEBUG)
    constexpr double kStepBudgetMilliseconds = 6.0;
    if (ScopedMilliseconds > kStepBudgetMilliseconds)
    {
        std::fprintf(stderr, "crowd_benchmark: step over the %.1f ms budget\n", kStepBudgetMilliseconds);
        return 1;
    }
#endif
    std::printf("crowd_benchmark: OK\n");
    return 0;
}
