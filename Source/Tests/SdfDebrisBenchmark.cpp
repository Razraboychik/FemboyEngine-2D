// FemboyEngine2D - debris spawn benchmark.
#include "Engine/Physics/SdfDebris.hpp"

#include <chrono>
#include <cstdio>
#include <vector>

int main()
{
    Fby::SdfDestructibleDesc ScopedDesc;
    ScopedDesc.Origin = Fby::Vector2D{0.0f, 0.0f};
    ScopedDesc.CellSize = Fby::Vector2D{1.0f, 1.0f};
    ScopedDesc.Width = 128;
    ScopedDesc.Height = 128;
    ScopedDesc.BaseShape = Fby::SdfCarveShape::Box;
    ScopedDesc.HalfExtents = Fby::Vector2D{64.0f, 64.0f};
    ScopedDesc.NarrowBand = 8.0f;
    Fby::SdfDestructible ScopedObject;
    if (!ScopedObject.Configure(ScopedDesc))
    {
        std::fprintf(stderr, "sdf_debris_benchmark: configure failed\n");
        return 1;
    }
    Fby::SdfCarveOp ScopedHole;
    ScopedHole.Shape = Fby::SdfCarveShape::Circle;
    ScopedHole.Center = Fby::Vector2D{64.0f, 64.0f};
    ScopedHole.Radius = 24.0f;
    Fby::SdfDebrisSettings ScopedSettings;
    ScopedSettings.MaxPieces = 32;
    ScopedSettings.Stride = 2;
    std::vector<Fby::SdfDebrisPiece> ScopedPieces(64);

    constexpr std::uint32_t kIterations = 10000;
    const auto ScopedStart = std::chrono::steady_clock::now();
    std::uint32_t ScopedTotal = 0;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < kIterations; ++ScopedIndex)
    {
        ScopedTotal += Fby::SpawnDebris(ScopedObject, ScopedHole, ScopedSettings, ScopedPieces);
    }
    const auto ScopedEnd = std::chrono::steady_clock::now();
    const double ScopedMilliseconds = std::chrono::duration<double, std::milli>(ScopedEnd - ScopedStart).count();
    const double ScopedMicroseconds = ScopedMilliseconds * 1000.0 / static_cast<double>(kIterations);
    std::printf("sdf_debris_benchmark: %u spawns, %.2f us each, %u pieces\n", kIterations, ScopedMicroseconds,
                ScopedTotal / kIterations);
    if (ScopedTotal == 0)
    {
        std::fprintf(stderr, "sdf_debris_benchmark: no pieces spawned\n");
        return 1;
    }

#if defined(NDEBUG)
    constexpr double kSpawnBudgetMicroseconds = 500.0;
    if (ScopedMicroseconds > kSpawnBudgetMicroseconds)
    {
        std::fprintf(stderr, "sdf_debris_benchmark: spawn over the %.1f us budget\n", kSpawnBudgetMicroseconds);
        return 1;
    }
#endif
    std::printf("sdf_debris_benchmark: OK\n");
    return 0;
}
