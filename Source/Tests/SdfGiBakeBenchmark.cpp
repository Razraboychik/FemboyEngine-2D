// FemboyEngine2D - baked GI probe grid benchmark.
#include "Engine/Physics/SdfGiBake.hpp"
#include "Engine/Physics/SdfWorld.hpp"

#include <chrono>
#include <cstdio>

int main()
{
    constexpr std::uint32_t kSide = 128;
    Fby::SdfWorldDesc ScopedWorldDesc;
    ScopedWorldDesc.Origin = Fby::Vector2D{0.0f, 0.0f};
    ScopedWorldDesc.CellSize = Fby::Vector2D{4.0f, 4.0f};
    ScopedWorldDesc.Width = 512;
    ScopedWorldDesc.Height = 512;
    ScopedWorldDesc.NarrowBand = 16.0f;
    ScopedWorldDesc.OutsideDistance = 2000.0f;
    Fby::SdfWorld ScopedWorld;
    if (!ScopedWorld.Configure(ScopedWorldDesc))
    {
        std::fprintf(stderr, "sdf_gi_benchmark: configure failed\n");
        return 1;
    }
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < 64; ++ScopedIndex)
    {
        const float ScopedX = static_cast<float>((ScopedIndex * 7u) % 512u) * 4.0f;
        const float ScopedY = static_cast<float>((ScopedIndex * 13u + 5u) % 512u) * 4.0f;
        (void)ScopedWorld.AddBox(Fby::Vector2D{ScopedX, ScopedY}, Fby::Vector2D{12.0f, 12.0f});
    }
    if (!ScopedWorld.BakeAll())
    {
        std::fprintf(stderr, "sdf_gi_benchmark: bake failed\n");
        return 1;
    }

    Fby::SdfGiBakeSettings ScopedSettings;
    ScopedSettings.SunDirection = Fby::Vector2D{0.6f, 0.8f};
    Fby::SdfGiBaked ScopedBake;
    const auto ScopedStart = std::chrono::steady_clock::now();
    const bool ScopedOk = ScopedBake.Build(ScopedWorld.Bricks(), Fby::Vector2D{0.0f, 0.0f}, Fby::Vector2D{16.0f, 16.0f},
                                           kSide, kSide, ScopedSettings);
    const auto ScopedEnd = std::chrono::steady_clock::now();
    if (!ScopedOk)
    {
        std::fprintf(stderr, "sdf_gi_benchmark: probe bake failed\n");
        return 1;
    }
    const double ScopedMilliseconds = std::chrono::duration<double, std::milli>(ScopedEnd - ScopedStart).count();
    std::printf("sdf_gi_benchmark: %zu probes, %.3f ms\n", ScopedBake.ProbeCount(), ScopedMilliseconds);

#if defined(NDEBUG)
    constexpr double kBakeBudgetMilliseconds = 40.0;
    if (ScopedMilliseconds > kBakeBudgetMilliseconds)
    {
        std::fprintf(stderr, "sdf_gi_benchmark: bake over the %.1f ms budget\n", kBakeBudgetMilliseconds);
        return 1;
    }
#endif
    std::printf("sdf_gi_benchmark: OK\n");
    return 0;
}
