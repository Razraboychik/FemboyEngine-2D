// FemboyEngine2D - SDF world bake benchmark.
#include "Engine/Physics/SdfWorld.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>

namespace
{
[[nodiscard]] double ElapsedMilliseconds(std::chrono::steady_clock::time_point InStart)
{
    const auto ScopedNow = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(ScopedNow - InStart).count();
}
}

int main()
{
    constexpr std::uint32_t kSize = 2048;
    Fby::SdfWorldDesc ScopedDesc;
    ScopedDesc.CellSize = Fby::Vector2D{4.0f, 4.0f};
    ScopedDesc.Width = kSize;
    ScopedDesc.Height = kSize;
    ScopedDesc.NarrowBand = 32.0f;
    ScopedDesc.OutsideDistance = 1000.0f;

    Fby::SdfWorld ScopedWorld;
    if (!ScopedWorld.Configure(ScopedDesc))
    {
        std::fprintf(stderr, "sdf_bake_benchmark: configure failed\n");
        return 1;
    }
    for (std::uint32_t ScopedY = 0; ScopedY < 8; ++ScopedY)
    {
        for (std::uint32_t ScopedX = 0; ScopedX < 8; ++ScopedX)
        {
            const Fby::Vector2D ScopedCenter{(static_cast<float>(ScopedX) + 0.5f) * 1024.0f,
                                             (static_cast<float>(ScopedY) + 0.5f) * 1024.0f};
            if (!ScopedWorld.AddBox(ScopedCenter, Fby::Vector2D{48.0f, 24.0f}))
            {
                std::fprintf(stderr, "sdf_bake_benchmark: source capacity exhausted\n");
                return 1;
            }
        }
    }

    const auto ScopedFullStart = std::chrono::steady_clock::now();
    if (!ScopedWorld.BakeAll())
    {
        std::fprintf(stderr, "sdf_bake_benchmark: full bake failed\n");
        return 1;
    }
    const double ScopedFullMilliseconds = ElapsedMilliseconds(ScopedFullStart);
    std::printf("sdf_bake_benchmark: full 2048x2048 bake %.3f ms (%u bricks, %zu narrow)\n", ScopedFullMilliseconds,
                static_cast<std::uint32_t>(ScopedWorld.Bricks().BrickCount()),
                ScopedWorld.Bricks().NarrowBrickCount());

    ScopedWorld.MarkDirtyRect(Fby::Vector2D{512.0f, 512.0f}, Fby::Vector2D{640.0f, 600.0f});
    const auto ScopedDirtyStart = std::chrono::steady_clock::now();
    if (!ScopedWorld.ApplyDirtyBake(4096))
    {
        std::fprintf(stderr, "sdf_bake_benchmark: dirty bake failed\n");
        return 1;
    }
    const double ScopedDirtyMilliseconds = ElapsedMilliseconds(ScopedDirtyStart);
    std::printf("sdf_bake_benchmark: dirty bake %.3f ms\n", ScopedDirtyMilliseconds);

#if defined(NDEBUG)
    // A 2048x2048 world must bake inside the full-bake budget, and an edit must
    // stay inside the per-frame budget.
    constexpr double kFullBudgetMilliseconds = 200.0;
    constexpr double kDirtyBudgetMilliseconds = 30.0;
    if (ScopedFullMilliseconds > kFullBudgetMilliseconds)
    {
        std::fprintf(stderr, "sdf_bake_benchmark: full bake over the %.1f ms budget\n", kFullBudgetMilliseconds);
        return 1;
    }
    if (ScopedDirtyMilliseconds > kDirtyBudgetMilliseconds)
    {
        std::fprintf(stderr, "sdf_bake_benchmark: dirty bake over the %.1f ms budget\n", kDirtyBudgetMilliseconds);
        return 1;
    }
#endif

    std::printf("sdf_bake_benchmark: OK\n");
    return 0;
}
