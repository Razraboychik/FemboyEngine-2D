// FemboyEngine2D - FrameComposer scale benchmark.
#include "Engine/RHI/FrameComposer.hpp"
#include "Engine/ECS/Components.hpp"

#include <cstdio>
#include <cstdlib>

namespace
{
void RunCase(std::uint32_t InCount)
{
    Fby::Ecs::Registry ScopedRegistry;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < InCount; ++ScopedIndex)
    {
        const Fby::Entity ScopedEntity = ScopedRegistry.create();
        const float ScopedX = static_cast<float>(ScopedIndex % 256u) * 8.0f;
        const float ScopedY = static_cast<float>(ScopedIndex / 256u) * 8.0f;
        ScopedRegistry.emplace<Fby::Transform2D>(ScopedEntity, Fby::Transform2D{Fby::Vector2D{ScopedX, ScopedY}});
        ScopedRegistry.emplace<Fby::Sprite2D>(ScopedEntity, Fby::Sprite2D{});
        ScopedRegistry.emplace<Fby::DrawOrder>(
            ScopedEntity, Fby::DrawOrder{static_cast<std::uint16_t>(ScopedIndex % 8u),
                                         static_cast<float>(ScopedIndex) * 0.001f});
    }

    Fby::Rhi::FrameComposer ScopedComposer;
    // Large view so both cases measure the full extract/sort path rather than
    // the offscreen-culling fast path.
    const Fby::Rhi::CameraData ScopedCamera{Fby::Vector2D{1024.0f, 2048.0f},
                                            Fby::Vector2D{100000.0f, 100000.0f}, 1.0f};
    // Warm caches and capacity before the measured frame.
    ScopedComposer.BuildFramePacket(ScopedRegistry, ScopedCamera, Fby::Color32::Black(), 0u);
    ScopedComposer.BuildFramePacket(ScopedRegistry, ScopedCamera, Fby::Color32::Black(), 0u);
    // Force the content-only path so gather is measured without a re-sort.
    auto ScopedTransformView = ScopedRegistry.view<Fby::Transform2D>();
    for (auto [ScopedEntity, ScopedTransform] : ScopedTransformView.each())
    {
        (void)ScopedEntity;
        ScopedTransform.Position.X += 1.0f;
    }
    ScopedComposer.BuildFramePacket(ScopedRegistry, ScopedCamera, Fby::Color32::Black(), 0u);

    const auto ScopedPlainStats = ScopedComposer.Stats();
    std::printf("frame_composer_benchmark: %u sprites, extract %.3f ms, sort %.3f ms, gather %.3f ms, culled %u\n",
                InCount, ScopedPlainStats.ExtractMilliseconds, ScopedPlainStats.SortMilliseconds,
                ScopedPlainStats.GatherMilliseconds, ScopedPlainStats.CulledSprites);
    if (ScopedComposer.Packet().items.size() + ScopedPlainStats.CulledSprites != InCount)
    {
        std::fprintf(stderr, "frame_composer_benchmark: item count mismatch\n");
        std::exit(1);
    }

    // Motion-vector path: temporal upscalers and frame generation consume it, so
    // the cost of building it is budgeted separately from the plain extract. The
    // first temporal frame seeds the history and the second one measures the
    // steady state where every sprite already has a record.
    ScopedComposer.SetTemporalState(true);
    ScopedComposer.BuildFramePacket(ScopedRegistry, ScopedCamera, Fby::Color32::Black(), 0u);
    ScopedComposer.BuildFramePacket(ScopedRegistry, ScopedCamera, Fby::Color32::Black(), 0u);
    const auto& ScopedMotionStats = ScopedComposer.Stats();
    const std::size_t ScopedMotionCount = ScopedComposer.Packet().motionItems.size();
    std::printf("frame_composer_benchmark: %u sprites, motion extract %.3f ms, motion items %zu\n", InCount,
                ScopedMotionStats.ExtractMilliseconds, ScopedMotionCount);
    if (ScopedMotionCount != InCount)
    {
        std::fprintf(stderr, "frame_composer_benchmark: motion item count mismatch\n");
        std::exit(1);
    }

#if defined(NDEBUG)
    if (InCount >= 100000u)
    {
        // Regression guard for optimized builds. The temporal path must stay
        // inside the frame budget; a hash map here once made extract ten times
        // slower. Unoptimized builds only check the item counts above.
        // Generous enough for a loaded CI machine, tight enough to catch the
        // 18 ms regression a per-sprite hash map once caused.
        constexpr double kExtractBudgetMilliseconds = 25.0;
        constexpr double kMotionBudgetMilliseconds = 50.0;
        if (ScopedPlainStats.ExtractMilliseconds > kExtractBudgetMilliseconds)
        {
            std::fprintf(stderr, "frame_composer_benchmark: extract %.3f ms over the %.1f ms budget\n",
                         ScopedPlainStats.ExtractMilliseconds, kExtractBudgetMilliseconds);
            std::exit(1);
        }
        if (ScopedMotionStats.ExtractMilliseconds > kMotionBudgetMilliseconds)
        {
            std::fprintf(stderr, "frame_composer_benchmark: motion extract %.3f ms over the %.1f ms budget\n",
                         ScopedMotionStats.ExtractMilliseconds, kMotionBudgetMilliseconds);
            std::exit(1);
        }
    }
#endif
}
}

int main()
{
    RunCase(10000u);
    RunCase(100000u);
    std::printf("frame_composer_benchmark: OK\n");
    return 0;
}
