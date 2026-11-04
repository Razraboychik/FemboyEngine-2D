// FemboyEngine2D - smoke step and GI attenuation benchmark.
#include "Engine/Physics/SdfSmoke.hpp"
#include "Engine/Physics/SdfWorld.hpp"

#include <chrono>
#include <cstdio>

int main()
{
    constexpr std::uint32_t kSide = 128;
    Fby::SdfSmokeDesc ScopedSmokeDesc;
    ScopedSmokeDesc.Origin = Fby::Vector2D{0.0f, 0.0f};
    ScopedSmokeDesc.CellSize = Fby::Vector2D{8.0f, 8.0f};
    ScopedSmokeDesc.Width = kSide;
    ScopedSmokeDesc.Height = kSide;
    ScopedSmokeDesc.Wind = Fby::Vector2D{24.0f, 0.0f};
    Fby::SdfSmokeField ScopedSmoke;
    if (!ScopedSmoke.Configure(ScopedSmokeDesc))
    {
        std::fprintf(stderr, "sdf_smoke_benchmark: configure failed\n");
        return 1;
    }
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < 16; ++ScopedIndex)
    {
        (void)ScopedSmoke.AddDensityWorld(
            Fby::Vector2D{static_cast<float>((ScopedIndex * 37u) % 1024u),
                          static_cast<float>((ScopedIndex * 71u + 13u) % 1024u)},
            48.0f, 1.0f);
    }

    const auto ScopedStart = std::chrono::steady_clock::now();
    constexpr std::uint32_t kSteps = 10;
    for (std::uint32_t ScopedStep = 0; ScopedStep < kSteps; ++ScopedStep)
    {
        ScopedSmoke.Step(1.0f / 60.0f);
    }
    const auto ScopedEnd = std::chrono::steady_clock::now();
    const double ScopedMilliseconds =
        std::chrono::duration<double, std::milli>(ScopedEnd - ScopedStart).count() / kSteps;
    std::printf("sdf_smoke_benchmark: %ux%u step %.3f ms\n", kSide, kSide, ScopedMilliseconds);

#if defined(NDEBUG)
    constexpr double kStepBudgetMilliseconds = 4.0;
    if (ScopedMilliseconds > kStepBudgetMilliseconds)
    {
        std::fprintf(stderr, "sdf_smoke_benchmark: step over the %.1f ms budget\n", kStepBudgetMilliseconds);
        return 1;
    }
#endif
    std::printf("sdf_smoke_benchmark: OK\n");
    return 0;
}
