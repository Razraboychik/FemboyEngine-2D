// FemboyEngine2D - retained frame composer ordering tests.
#include "Engine/RHI/FrameComposer.hpp"
#include "Engine/ECS/Components.hpp"
#include "Engine/ECS/Lighting.hpp"

#include <array>
#include <cmath>
#include <cstdio>

namespace
{
int GFailures = 0;

void Check(bool InCondition, const char* InMessage)
{
    if (!InCondition)
    {
        std::fprintf(stderr, "FAIL: %s\n", InMessage);
        ++GFailures;
    }
}

bool Near(float InA, float InB, float InTolerance = 0.001f)
{
    return std::fabs(InA - InB) < InTolerance;
}
}

int main()
{
    using namespace Fby;

    Ecs::Registry ScopedRegistry;
    const Entity ScopedUpper = ScopedRegistry.create();
    ScopedRegistry.emplace<Transform2D>(ScopedUpper, Transform2D{Vector2D{10.0f, 0.0f}});
    ScopedRegistry.emplace<Sprite2D>(ScopedUpper, Sprite2D{});
    ScopedRegistry.emplace<DrawOrder>(ScopedUpper, DrawOrder{1u, 0.5f});

    const Entity ScopedLower = ScopedRegistry.create();
    ScopedRegistry.emplace<Transform2D>(ScopedLower, Transform2D{Vector2D{20.0f, 0.0f}});
    ScopedRegistry.emplace<Sprite2D>(ScopedLower, Sprite2D{});
    ScopedRegistry.emplace<DrawOrder>(ScopedLower, DrawOrder{0u, 0.0f});

    const Entity ScopedOutside = ScopedRegistry.create();
    ScopedRegistry.emplace<Transform2D>(ScopedOutside, Transform2D{Vector2D{10000.0f, 0.0f}});
    ScopedRegistry.emplace<Sprite2D>(ScopedOutside, Sprite2D{});
    ScopedRegistry.emplace<DrawOrder>(ScopedOutside, DrawOrder{2u, 0.0f});

    const Entity ScopedLamp = ScopedRegistry.create();
    ScopedRegistry.emplace<Transform2D>(ScopedLamp, Transform2D{Vector2D{10.0f, 0.0f}});
    Light2DComponent ScopedLight{};
    ScopedLight.Flags = kLight2DFlagEnabled;
    ScopedLight.Radius = 100.0f;
    ScopedLight.Intensity = 1.0f;
    ScopedRegistry.emplace<Light2DComponent>(ScopedLamp, ScopedLight);

    Rhi::FrameComposer ScopedComposer;
    const Rhi::CameraData ScopedCamera{Vector2D{0.0f, 0.0f}, Vector2D{1280.0f, 720.0f}, 1.0f};
    ScopedComposer.BuildFramePacket(ScopedRegistry, ScopedCamera, Color32::Black(), 0u);
    Check(ScopedComposer.Packet().items.size() == 2u, "the offscreen sprite is culled");
    Check(!ScopedComposer.Packet().sceneStatic, "the first frame is not static");
    ScopedComposer.BuildFramePacket(ScopedRegistry, ScopedCamera, Color32::Black(), 0u);
    Check(ScopedComposer.Packet().sceneStatic, "an unchanged frame reuses previous depth state");
    const std::uint32_t ScopedStaticVersion = ScopedComposer.Packet().sceneVersion;
    ScopedComposer.BuildFramePacket(ScopedRegistry, ScopedCamera, Color32::Black(), 0u);
    Check(ScopedComposer.Packet().sceneVersion == ScopedStaticVersion, "a static frame keeps its scene version");
    ScopedRegistry.get<Light2DComponent>(ScopedLamp).Intensity = 0.5f;
    ScopedComposer.BuildFramePacket(ScopedRegistry, ScopedCamera, Color32::Black(), 0u);
    Check(!ScopedComposer.Packet().sceneStatic, "a changed light invalidates the static scene cache");
    ScopedRegistry.get<Light2DComponent>(ScopedLamp).Intensity = 1.0f;
    ScopedComposer.BuildFramePacket(ScopedRegistry, ScopedCamera, Color32::Black(), 0u);

    ScopedRegistry.get<Sprite2D>(ScopedLower).Flags |= static_cast<std::uint32_t>(Rhi::RenderFlags::Additive);
    ScopedComposer.BuildFramePacket(ScopedRegistry, ScopedCamera, Color32::Black(), 0u);
    Check(ScopedComposer.OitPacket().items.size() == 1u, "the transparent packet contains only non-depth sprites");
    ScopedRegistry.get<Sprite2D>(ScopedLower).Flags &= ~static_cast<std::uint32_t>(Rhi::RenderFlags::Additive);
    ScopedComposer.BuildFramePacket(ScopedRegistry, ScopedCamera, Color32::Black(), 0u);

    ScopedRegistry.get<Sprite2D>(ScopedUpper).Flags |= static_cast<std::uint32_t>(Rhi::RenderFlags::Oit);
    ScopedComposer.BuildFramePacket(ScopedRegistry, ScopedCamera, Color32::Black(), 0u);
    Check(ScopedComposer.Stats().OitItems == 1u, "the OIT per-item flag reaches composer stats");
    ScopedRegistry.get<Sprite2D>(ScopedUpper).Flags &= ~static_cast<std::uint32_t>(Rhi::RenderFlags::Oit);
    ScopedComposer.BuildFramePacket(ScopedRegistry, ScopedCamera, Color32::Black(), 0u);

    ScopedRegistry.get<Transform2D>(ScopedUpper).Position.X = 11.0f;
    ScopedComposer.BuildFramePacket(ScopedRegistry, ScopedCamera, Color32::Black(), 0u);
    Check(ScopedComposer.Packet().sceneVersion > ScopedStaticVersion, "a changed frame advances the scene version");
    ScopedRegistry.get<Transform2D>(ScopedUpper).Position.X = 10.0f;
    Check(Near(ScopedComposer.Packet().items[0].position.X, 20.0f), "the lower layer is first");
    Check((ScopedComposer.Packet().items[1].flags & Rhi::DepthWriteMask) != 0u, "depth write is encoded");
    Check(ScopedComposer.Packet().depthBatches.size() == 1u, "opaque sprites form one depth batch");
    Check(ScopedComposer.Stats().DepthBatches == 1u, "depth stats count the batch");
    Check(ScopedComposer.Stats().RenderEcsSprites == 2u, "packed render ECS mirrors the gathered sprites");
    Check(ScopedComposer.Stats().DepthItems == 2u, "depth stats count both opaque sprites");
    ScopedComposer.SetGpuLighting(true);
    ScopedComposer.BuildFramePacket(ScopedRegistry, ScopedCamera, Color32::Black(), 0u);
    Check(ScopedComposer.Packet().gpuLighting, "GPU lighting flag reaches the packet");
    ScopedComposer.SetGpuLighting(false);
    Check(Near(Rhi::DecodeDepth(ScopedComposer.Packet().items[1].flags), 0.5f, 0.01f),
          "the depth byte round-trips");
    Check(ScopedComposer.Packet().lights.size() == 1u, "the light is packed for the cluster pass");
    Check(Near(ScopedComposer.Packet().lights[0].PositionAndRadius.X, 650.0f), "the light is in screen space");
    Check(Near(ScopedComposer.Packet().lights[0].PositionAndRadius.Y, 360.0f), "screen Y follows the viewport");
    Check(Near(ScopedComposer.Packet().lights[0].PositionAndRadius.Z, 100.0f), "the radius is scaled by zoom");
    Check(ScopedComposer.Stats().CulledSprites == 1u, "the stats count the culled sprite");
    Check(ScopedComposer.Stats().PackedLights == 1u, "the stats count packed lights");
    Check(ScopedComposer.Stats().ExtractMilliseconds >= 0.0, "extract timing is recorded");

    // Change positions without touching the sort keys. The retained composer
    // must reuse the previous order instead of the extraction order.
    ScopedRegistry.get<Transform2D>(ScopedUpper).Position.X = 30.0f;
    ScopedRegistry.get<Transform2D>(ScopedLower).Position.X = 40.0f;
    ScopedComposer.BuildFramePacket(ScopedRegistry, ScopedCamera, Color32::Black(), 0u);
    Check(ScopedComposer.Packet().items.size() == 2u, "the count stays stable");
    Check(Near(ScopedComposer.Packet().items[0].position.X, 40.0f), "the lower layer stays first after a content-only update");
    Check(Near(ScopedComposer.Packet().items[1].position.X, 30.0f), "the upper layer stays second after a content-only update");

    // Change a key: the composer must rebuild the order and the batches.
    ScopedRegistry.get<DrawOrder>(ScopedUpper).Layer = 0u;
    ScopedRegistry.get<DrawOrder>(ScopedLower).Layer = 2u;
    ScopedComposer.BuildFramePacket(ScopedRegistry, ScopedCamera, Color32::Black(), 0u);
    Check(Near(ScopedComposer.Packet().items[0].position.X, 30.0f), "a key change re-sorts the upper sprite first");
    Check(Near(ScopedComposer.Packet().items[1].position.X, 40.0f), "a key change re-sorts the lower sprite second");

    const Rhi::CameraData ScopedFarCamera{Vector2D{10000.0f, 0.0f}, Vector2D{1280.0f, 720.0f}, 1.0f};
    ScopedComposer.BuildFramePacket(ScopedRegistry, ScopedFarCamera, Color32::Black(), 0u);
    Check(ScopedComposer.Packet().items.size() == 1u, "the far camera keeps only the offscreen sprite");
    Check(Near(ScopedComposer.Packet().items[0].position.X, 10000.0f), "the far sprite is extracted");

    Ecs::Registry ScopedSingleRegistry;
    const Entity ScopedSingle = ScopedSingleRegistry.create();
    ScopedSingleRegistry.emplace<Transform2D>(ScopedSingle, Transform2D{Vector2D{0.0f, 0.0f}});
    ScopedSingleRegistry.emplace<Sprite2D>(ScopedSingle, Sprite2D{});
    ScopedSingleRegistry.emplace<DrawOrder>(ScopedSingle, DrawOrder{0u, 0.0f});
    Rhi::FrameComposer ScopedSingleComposer;
    ScopedSingleComposer.BuildFramePacket(ScopedSingleRegistry, ScopedCamera, Color32::Black(), 0u);
    Check(ScopedSingleComposer.Packet().batches.size() == 1u, "single batch");
    Check(ScopedSingleComposer.Stats().DepthItems == 1u, "single depth item");
    Check(ScopedSingleComposer.Packet().gpuOpaqueMain, "one opaque base batch enables the GPU main path");

    const Entity ScopedFade = ScopedSingleRegistry.create();
    ScopedSingleRegistry.emplace<Transform2D>(ScopedFade, Transform2D{Vector2D{8.0f, 0.0f}});
    Sprite2D ScopedFadeSprite;
    ScopedFadeSprite.Color = Color32::FromBytes(255, 255, 255, 0);
    ScopedSingleRegistry.emplace<Sprite2D>(ScopedFade, ScopedFadeSprite);
    ScopedSingleRegistry.emplace<DrawOrder>(ScopedFade, DrawOrder{1u, 0.0f});
    ScopedSingleComposer.BuildFramePacket(ScopedSingleRegistry, ScopedCamera, Color32::Black(), 0u);
    Check(ScopedSingleComposer.Packet().gpuCulledBatchCount == 1u, "only the opaque prefix is GPU-culled");

    // Material buckets split a layer into contiguous pipeline runs, and the
    // batch carries the corresponding material pipeline id.
    Ecs::Registry ScopedMaterialRegistry;
    for (std::uint32_t ScopedBucket = 0; ScopedBucket < 3u; ++ScopedBucket)
    {
        const Entity ScopedEntity = ScopedMaterialRegistry.create();
        ScopedMaterialRegistry.emplace<Transform2D>(
            ScopedEntity, Transform2D{Vector2D{static_cast<float>(ScopedBucket) * 10.0f, 0.0f}});
        ScopedMaterialRegistry.emplace<Sprite2D>(ScopedEntity, Sprite2D{});
        ScopedMaterialRegistry.emplace<DrawOrder>(ScopedEntity, DrawOrder{0u, 0.0f, 2u - ScopedBucket});
    }
    Rhi::FrameComposer ScopedMaterialComposer;
    const std::array<std::uint32_t, 3> ScopedPipelines{0u, 10u, 20u};
    ScopedMaterialComposer.SetMaterialPipelines(ScopedPipelines);
    ScopedMaterialComposer.BuildFramePacket(ScopedMaterialRegistry, ScopedCamera, Color32::Black(), 0u);
    Check(ScopedMaterialComposer.Packet().items.size() == 3u, "three material sprites are extracted");
    Check(ScopedMaterialComposer.Packet().batches.size() == 3u, "three material buckets form three batches");
    Check(ScopedMaterialComposer.Packet().gpuOpaqueMain, "three opaque batches keep the GPU main path");
    if (ScopedMaterialComposer.Packet().batches.size() == 3u)
    {
        Check(ScopedMaterialComposer.Packet().batches[0].pipeline == 0u, "bucket zero uses the engine pipeline");
        Check(ScopedMaterialComposer.Packet().batches[1].pipeline == 10u, "bucket one resolves its pipeline");
        Check(ScopedMaterialComposer.Packet().batches[2].pipeline == 20u, "bucket two resolves its pipeline");
    }

    // Transparent sprites must not write depth in the prepass.
    Ecs::Registry ScopedTransparentRegistry;
    const Entity ScopedTransparent = ScopedTransparentRegistry.create();
    ScopedTransparentRegistry.emplace<Transform2D>(ScopedTransparent, Transform2D{Vector2D{0.0f, 0.0f}});
    Sprite2D ScopedTransparentSprite;
    ScopedTransparentSprite.Color = Color32::FromBytes(255, 255, 255, 128);
    ScopedTransparentRegistry.emplace<Sprite2D>(ScopedTransparent, ScopedTransparentSprite);
    ScopedTransparentRegistry.emplace<DrawOrder>(ScopedTransparent, DrawOrder{0u, 0.5f});
    Rhi::FrameComposer ScopedTransparentComposer;
    ScopedTransparentComposer.BuildFramePacket(ScopedTransparentRegistry, ScopedCamera, Color32::Black(), 0u);
    Check(ScopedTransparentComposer.Packet().items.size() == 1u, "the transparent sprite is extracted");
    Check((ScopedTransparentComposer.Packet().items[0].flags & Rhi::DepthWriteMask) == 0u,
          "transparent sprites skip depth-write");
    Check(ScopedTransparentComposer.Packet().depthBatches.empty(), "transparent sprites form no depth batches");

    // Front-to-back opaque ordering must be stable across repeated frames: the
    // permutation is applied to the gathered copy, so a static frame may not flip
    // the order or detach keys from items.
    {
        Ecs::Registry ScopedFrontRegistry;
        for (std::uint16_t ScopedLayer = 0; ScopedLayer < 2; ++ScopedLayer)
        {
            const Entity ScopedEntity = ScopedFrontRegistry.create();
            ScopedFrontRegistry.emplace<Transform2D>(
                ScopedEntity, Transform2D{Vector2D{static_cast<float>(ScopedLayer) * 10.0f, 0.0f}});
            ScopedFrontRegistry.emplace<Sprite2D>(ScopedEntity, Sprite2D{});
            ScopedFrontRegistry.emplace<DrawOrder>(ScopedEntity,
                                                   DrawOrder{0u, ScopedLayer == 0 ? 0.0f : 0.5f});
        }
        Rhi::FrameComposer ScopedFrontComposer;
        ScopedFrontComposer.SetOpaqueFrontToBack(true);
        ScopedFrontComposer.SetForceOpaqueCutout(true);
        ScopedFrontComposer.BuildFramePacket(ScopedFrontRegistry, ScopedCamera, Color32::Black(), 0u);
        Check(ScopedFrontComposer.Packet().items.size() == 2u, "front-to-back frame has two items");
        const float ScopedFirstFrameX = ScopedFrontComposer.Packet().items[0].position.X;
        const Rhi::RenderKey ScopedFirstKey = ScopedFrontComposer.Packet().keys[0];
        Check(ScopedFrontComposer.Packet().keys[0] > ScopedFrontComposer.Packet().keys[1],
              "front-to-back reverses the layer order");
        ScopedFrontComposer.BuildFramePacket(ScopedFrontRegistry, ScopedCamera, Color32::Black(), 0u);
        Check(Near(ScopedFrontComposer.Packet().items[0].position.X, ScopedFirstFrameX),
              "a repeated front-to-back frame keeps the item order");
        Check(ScopedFrontComposer.Packet().keys[0] == ScopedFirstKey,
              "a repeated front-to-back frame keeps keys aligned with items");
    }

    ScopedComposer.SetTemporalState(true);
    ScopedComposer.BuildFramePacket(ScopedRegistry, ScopedCamera, Color32::Black(), 0u);
    Check(!ScopedComposer.Packet().historyValid, "first temporal frame has no history");
    Check(ScopedComposer.Packet().jitterX != 0.0f || ScopedComposer.Packet().jitterY != 0.0f,
          "temporal frame carries sub-pixel jitter");
    ScopedComposer.BuildFramePacket(ScopedRegistry, ScopedCamera, Color32::Black(), 0u);
    Check(ScopedComposer.Packet().historyValid, "second temporal frame reuses history");
    ScopedComposer.ResetTemporalHistory();
    ScopedComposer.BuildFramePacket(ScopedRegistry, ScopedCamera, Color32::Black(), 0u);
    Check(!ScopedComposer.Packet().historyValid, "reset clears temporal history");
    Check(ScopedComposer.Packet().motionItems.empty(), "reset drops previous motion");

    if (auto* ScopedMoved = ScopedRegistry.try_get<Transform2D>(ScopedLower))
    {
        ScopedMoved->Position.X += 8.0f;
    }
    ScopedComposer.BuildFramePacket(ScopedRegistry, ScopedCamera, Color32::Black(), 0u);
    Check(!ScopedComposer.Packet().motionItems.empty(), "second frame emits motion vectors");
    if (!ScopedComposer.Packet().motionItems.empty())
    {
        const Rhi::MotionVectorItem& ScopedMotion = ScopedComposer.Packet().motionItems[0];
        Check(ScopedMotion.previousPosition.X != ScopedMotion.position.X,
              "motion vector keeps the previous sprite position");
        Check(ScopedMotion.screenDelta.X > 0.0f, "motion vector has a screen-space delta");
        Check(!ScopedMotion.IsReactive(), "opaque sprite is not reactive");
    }

    if (GFailures == 0)
    {
        std::printf("frame_composer: OK\n");
        return 0;
    }
    std::fprintf(stderr, "frame_composer: %d failure(s)\n", GFailures);
    return 1;
}
