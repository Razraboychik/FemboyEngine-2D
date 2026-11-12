// FemboyEngine2D - SDF composer bake benchmark.
#include "Engine/Physics/SdfComposer.hpp"

#include <chrono>
#include <cstdio>
#include <vector>

int main()
{
    constexpr std::uint32_t kSide = 256;
    constexpr std::uint32_t kLayers = 64;
    Fby::SdfComposer ScopedComposer;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < kLayers; ++ScopedIndex)
    {
        Fby::SdfShape ScopedShape;
        ScopedShape.SetType(ScopedIndex % 2u == 0 ? Fby::SdfShapeType::Box : Fby::SdfShapeType::Circle);
        ScopedShape.HalfExtents = Fby::Vector2D{12.0f, 8.0f};
        ScopedShape.Radius = 6.0f;
        const Fby::SdfComposeOp ScopedOp =
            ScopedIndex % 3u == 0 ? Fby::SdfComposeOp::Subtract : Fby::SdfComposeOp::Union;
        if (!ScopedComposer.Add(ScopedOp, ScopedShape,
                                Fby::Vector2D{static_cast<float>(ScopedIndex * 13u % 200u),
                                              static_cast<float>(ScopedIndex * 29u % 200u)}))
        {
            std::fprintf(stderr, "sdf_composer_benchmark: add failed\n");
            return 1;
        }
    }

    std::vector<float> ScopedSamples;
    Fby::SdfField ScopedField;
    const auto ScopedStart = std::chrono::steady_clock::now();
    if (!ScopedComposer.Bake(Fby::Vector2D{0.0f, 0.0f}, Fby::Vector2D{1.0f, 1.0f}, kSide, kSide, &ScopedSamples,
                             &ScopedField))
    {
        std::fprintf(stderr, "sdf_composer_benchmark: bake failed\n");
        return 1;
    }
    const auto ScopedEnd = std::chrono::steady_clock::now();
    const double ScopedMilliseconds = std::chrono::duration<double, std::milli>(ScopedEnd - ScopedStart).count();
    std::printf("sdf_composer_benchmark: %u layers over %ux%u in %.3f ms\n", kLayers, kSide, kSide,
                ScopedMilliseconds);

#if defined(NDEBUG)
    // Shared CI runners are much slower than the reference workstation; keep
    // the budget high enough to catch regressions without failing on noise.
    constexpr double kBakeBudgetMilliseconds = 1000.0;
    if (ScopedMilliseconds > kBakeBudgetMilliseconds)
    {
        std::fprintf(stderr, "sdf_composer_benchmark: bake over the %.1f ms budget\n", kBakeBudgetMilliseconds);
        return 1;
    }
#endif
    std::printf("sdf_composer_benchmark: OK\n");
    return 0;
}
