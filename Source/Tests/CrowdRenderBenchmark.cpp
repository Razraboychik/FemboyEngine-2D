// FemboyEngine2D - crowd render extract benchmark.
#include "Engine/Navigation/CrowdRender.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>

int main()
{
    constexpr std::uint32_t kAgents = 100000;
    Fby::Navigation::CrowdPool ScopedPool;
    if (!ScopedPool.Configure(kAgents))
    {
        std::fprintf(stderr, "crowd_render_benchmark: configure failed\n");
        return 1;
    }
    std::uint32_t ScopedState = 777u;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < kAgents; ++ScopedIndex)
    {
        ScopedState = ScopedState * 1664525u + 1013904223u;
        const float ScopedX = static_cast<float>(ScopedState % 4096u);
        ScopedState = ScopedState * 1664525u + 1013904223u;
        const float ScopedY = static_cast<float>(ScopedState % 4096u);
        if (ScopedPool.Add(Fby::Vector2D{ScopedX, ScopedY}, ScopedIndex % 4u,
                           Fby::Vector2D{2048.0f, 2048.0f}) == 0xFFFFFFFFu)
        {
            std::fprintf(stderr, "crowd_render_benchmark: pool is full\n");
            return 1;
        }
    }

    std::vector<Fby::Rhi::RenderItem> ScopedItems(kAgents);
    Fby::Navigation::CrowdRenderSettings ScopedSettings;
    const auto ScopedStart = std::chrono::steady_clock::now();
    const std::uint32_t ScopedWritten =
        Fby::Navigation::BuildCrowdRenderItems(ScopedPool, ScopedSettings, ScopedItems);
    const auto ScopedEnd = std::chrono::steady_clock::now();
    const double ScopedMilliseconds = std::chrono::duration<double, std::milli>(ScopedEnd - ScopedStart).count();
    std::printf("crowd_render_benchmark: %u items, %.3f ms\n", ScopedWritten, ScopedMilliseconds);
    if (ScopedWritten != kAgents)
    {
        std::fprintf(stderr, "crowd_render_benchmark: expected %u items\n", kAgents);
        return 1;
    }

#if defined(NDEBUG)
    constexpr double kExtractBudgetMilliseconds = 4.0;
    if (ScopedMilliseconds > kExtractBudgetMilliseconds)
    {
        std::fprintf(stderr, "crowd_render_benchmark: extract over the %.1f ms budget\n",
                     kExtractBudgetMilliseconds);
        return 1;
    }
#endif
    std::printf("crowd_render_benchmark: OK\n");
    return 0;
}
