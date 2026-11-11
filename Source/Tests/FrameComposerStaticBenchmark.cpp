// FemboyEngine2D - static versus dynamic FrameComposer CPU cost.
#include "Engine/ECS/Components.hpp"
#include "Engine/Core/JobSystem.hpp"
#include "Engine/RHI/FrameComposer.hpp"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace
{
struct Sample
{
    double StaticMs = 0.0;
    double DynamicMs = 0.0;
    double StaticExtractMs = 0.0;
    double DynamicExtractMs = 0.0;
};

[[nodiscard]] Sample Measure(std::uint32_t InSpriteCount, std::uint32_t InFrames)
{
    Fby::Ecs::Registry ScopedRegistry;
    const std::uint32_t ScopedSide =
        static_cast<std::uint32_t>(std::ceil(std::sqrt(static_cast<double>(InSpriteCount))));
    for (std::uint32_t ScopedI = 0; ScopedI < InSpriteCount; ++ScopedI)
    {
        const Fby::Entity ScopedEntity = ScopedRegistry.create();
        Fby::Transform2D ScopedTransform;
        ScopedTransform.Position = Fby::Vector2D{static_cast<float>(ScopedI % ScopedSide) * 2.0f,
                                                 static_cast<float>(ScopedI / ScopedSide) * 2.0f};
        ScopedTransform.Scale = Fby::Vector2D{1.0f, 1.0f};
        ScopedRegistry.emplace<Fby::Transform2D>(ScopedEntity, ScopedTransform);
        ScopedRegistry.emplace<Fby::Sprite2D>(ScopedEntity, Fby::Sprite2D{});
        ScopedRegistry.emplace<Fby::DrawOrder>(ScopedEntity, Fby::DrawOrder{});
    }

    Fby::Rhi::CameraData ScopedCamera;
    ScopedCamera.center = Fby::Vector2D{static_cast<float>(ScopedSide), static_cast<float>(ScopedSide)};
    ScopedCamera.viewport = Fby::Vector2D{100000.0f, 100000.0f};
    ScopedCamera.zoom = 1.0f;

    Fby::Rhi::FrameComposer ScopedComposer;
    Fby::JobSystem ScopedJobs(8);
    ScopedComposer.SetJobSystem(&ScopedJobs);
    ScopedComposer.Reserve(InSpriteCount * 2u);
    (void)ScopedComposer.BuildFramePacket(ScopedRegistry, ScopedCamera, Fby::Color32::Black(), 0u);

    Sample ScopedSample;
    const auto ScopedStaticStart = std::chrono::steady_clock::now();
    for (std::uint32_t ScopedFrame = 0; ScopedFrame < InFrames; ++ScopedFrame)
    {
        (void)ScopedComposer.BuildFramePacket(ScopedRegistry, ScopedCamera, Fby::Color32::Black(), 0u);
    }
    const auto ScopedStaticEnd = std::chrono::steady_clock::now();
    ScopedSample.StaticMs =
        std::chrono::duration<double, std::milli>(ScopedStaticEnd - ScopedStaticStart).count() / InFrames;
    ScopedSample.StaticExtractMs = ScopedComposer.Stats().ExtractMilliseconds;

    auto ScopedDynamicView = ScopedRegistry.view<Fby::Transform2D>();
    const auto ScopedDynamicStart = std::chrono::steady_clock::now();
    for (std::uint32_t ScopedFrame = 0; ScopedFrame < InFrames; ++ScopedFrame)
    {
        for (auto [ScopedEntity, ScopedTransform] : ScopedDynamicView.each())
        {
            (void)ScopedEntity;
            ScopedTransform.Position.X += 0.5f;
        }
        (void)ScopedComposer.BuildFramePacket(ScopedRegistry, ScopedCamera, Fby::Color32::Black(), 0u);
    }
    const auto ScopedDynamicEnd = std::chrono::steady_clock::now();
    ScopedSample.DynamicMs =
        std::chrono::duration<double, std::milli>(ScopedDynamicEnd - ScopedDynamicStart).count() / InFrames;
    ScopedSample.DynamicExtractMs = ScopedComposer.Stats().ExtractMilliseconds;
    return ScopedSample;
}
}

int main()
{
    constexpr std::uint32_t kFrames = 20;
    for (const std::uint32_t ScopedCount : {10000u, 100000u})
    {
        const Sample ScopedSample = Measure(ScopedCount, kFrames);
        std::printf(
            "frame_composer_static: %u static=%.3f ms dynamic=%.3f ms static_extract=%.3f ms dynamic_extract=%.3f ms\n",
            ScopedCount, ScopedSample.StaticMs, ScopedSample.DynamicMs, ScopedSample.StaticExtractMs,
            ScopedSample.DynamicExtractMs);
    }
    return 0;
}
