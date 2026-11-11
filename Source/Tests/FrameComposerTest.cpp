// FemboyEngine2D - retained frame composer ordering tests.
#include "Engine/RHI/FrameComposer.hpp"
#include "Engine/ECS/Components.hpp"
#include "Engine/ECS/Lighting.hpp"
#include "Engine/ECS/Presentation.hpp"
#include "Engine/Physics/SdfCharacter.hpp"
#include "Engine/RHI/SdfCharacterRender.hpp"
#include "Engine/Core/JobSystem.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <span>

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
    Check(Near(Rhi::DecodeDepth(ScopedComposer.Packet().items[1].flags), 0.5f, 0.01f), "the depth byte round-trips");
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
    Check(Near(ScopedComposer.Packet().items[0].position.X, 40.0f),
          "the lower layer stays first after a content-only update");
    Check(Near(ScopedComposer.Packet().items[1].position.X, 30.0f),
          "the upper layer stays second after a content-only update");

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
            ScopedFrontRegistry.emplace<DrawOrder>(ScopedEntity, DrawOrder{0u, ScopedLayer == 0 ? 0.0f : 0.5f});
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
    bool ScopedHasMotion = false;
    for (const Rhi::MotionVectorItem& ScopedMotion : ScopedComposer.Packet().motionItems)
    {
        // The storage backend controls entity order, so the moved sprite is not
        // guaranteed to be the first record.
        if (ScopedMotion.previousPosition.X != ScopedMotion.position.X && ScopedMotion.screenDelta.X > 0.0f)
        {
            ScopedHasMotion = true;
            Check(!ScopedMotion.IsReactive(), "opaque sprite is not reactive");
            break;
        }
    }
    Check(ScopedHasMotion, "motion vector keeps the previous sprite position and screen delta");

    Fby::SdfCharacterMeshData ScopedCharacterMesh;
    ScopedCharacterMesh.Positions = {Fby::Vector2D{0.0f, 0.0f}, Fby::Vector2D{8.0f, 0.0f}, Fby::Vector2D{8.0f, 8.0f},
                                     Fby::Vector2D{0.0f, 8.0f}};
    ScopedCharacterMesh.Indices = {0, 1, 2, 0, 2, 3};
    const std::array<Fby::SdfCharacterMeshData, 1> ScopedCharacterMeshes{ScopedCharacterMesh};
    ScopedComposer.BuildFramePacket(ScopedRegistry, ScopedCamera, Color32::Black(), 0u, {}, {},
                                    Fby::Rhi::PostFxDisabled(), ScopedCharacterMeshes);
    Check(ScopedComposer.Packet().meshItems.size() == 1u, "SDF character mesh reaches the packet");
    if (!ScopedComposer.Packet().meshItems.empty())
    {
        const Rhi::MeshRenderItem& ScopedItem = ScopedComposer.Packet().meshItems[0];
        Check(ScopedItem.vertices.size() == 4u && ScopedItem.indices.size() == 6u,
              "SDF character mesh keeps its topology");
        Check((ScopedItem.flags & Rhi::MeshItemUnskinned) != 0, "SDF character mesh is unskinned");
    }

    Fby::Ecs::Registry ScopedSdfRegistry;
    const Fby::Entity ScopedSdfEntity = ScopedSdfRegistry.create();
    Fby::SkeletonComponent ScopedSkeleton;
    Fby::SkeletonReset(ScopedSkeleton);
    (void)Fby::SkeletonAddBone(ScopedSkeleton, "root", -1, Fby::Vector2D{0.0f, 0.0f}, 0.0f);
    Fby::SkeletonBindPose(ScopedSkeleton);
    (void)ScopedSdfRegistry.emplace<Fby::SkeletonComponent>(ScopedSdfEntity, ScopedSkeleton);
    Fby::SdfCharacterComponent ScopedCharacter;
    ScopedCharacter.PartCount = 1;
    ScopedCharacter.Parts[0].Shape = Fby::MakeSdfShape(Fby::SdfShapeType::Circle);
    ScopedCharacter.Parts[0].Shape.Radius = 8.0f;
    ScopedCharacter.Parts[0].BoneIndex = 0;
    (void)ScopedSdfRegistry.emplace<Fby::SdfCharacterComponent>(ScopedSdfEntity, ScopedCharacter);
    Fby::Rhi::SdfCharacterRenderCache ScopedSdfCache;
    ScopedSdfCache.Build(ScopedSdfRegistry, 24);
    Check(!ScopedSdfCache.Meshes().empty(), "the SDF character cache bakes ECS entities");
    if (!ScopedSdfCache.Meshes().empty())
    {
        Fby::Rhi::FrameComposer ScopedSdfComposer;
        ScopedSdfComposer.BuildFramePacket(ScopedSdfRegistry, ScopedCamera, Color32::Black(), 0u, {}, {},
                                           Fby::Rhi::PostFxDisabled(), ScopedSdfCache.Meshes());
        Check(!ScopedSdfComposer.Packet().meshItems.empty(), "the cached SDF character reaches the composed packet");
    }

    // The chunk-prefix extract must produce exactly the same packet as the
    // deterministic serial fallback.
    {
        Fby::Ecs::Registry ScopedParallelRegistry;
        Fby::Entity ScopedParallelFirst = Fby::kNullEntity;
        for (std::uint32_t ScopedI = 0; ScopedI < 5000; ++ScopedI)
        {
            const Fby::Entity ScopedEntity = ScopedParallelRegistry.create();
            if (ScopedI == 0)
            {
                ScopedParallelFirst = ScopedEntity;
            }
            ScopedParallelRegistry.emplace<Fby::Transform2D>(
                ScopedEntity, Fby::Transform2D{Fby::Vector2D{static_cast<float>(ScopedI % 100u) * 2.0f,
                                                             static_cast<float>(ScopedI / 100u) * 2.0f},
                                               Fby::Vector2D{1.0f, 1.0f}, 0.0f});
            ScopedParallelRegistry.emplace<Fby::Sprite2D>(ScopedEntity, Fby::Sprite2D{});
            ScopedParallelRegistry.emplace<Fby::DrawOrder>(
                ScopedEntity, Fby::DrawOrder{static_cast<std::uint16_t>(ScopedI % 3u), 0.0f});
        }
        const Fby::Rhi::CameraData ScopedParallelCamera{Fby::Vector2D{100.0f, 100.0f},
                                                        Fby::Vector2D{100000.0f, 100000.0f}, 1.0f};
        Fby::JobSystem ScopedParallelJobs(8);
        Fby::Rhi::FrameComposer ScopedParallelComposer;
        ScopedParallelComposer.SetJobSystem(&ScopedParallelJobs);
        Fby::Rhi::FrameComposer ScopedSerialComposer;
        ScopedParallelComposer.BuildFramePacket(ScopedParallelRegistry, ScopedParallelCamera, Fby::Color32::Black(),
                                                0u);
        ScopedSerialComposer.BuildFramePacket(ScopedParallelRegistry, ScopedParallelCamera, Fby::Color32::Black(), 0u);
        const Fby::Rhi::FramePacket& ScopedParallelPacket = ScopedParallelComposer.Packet();
        const Fby::Rhi::FramePacket& ScopedSerialPacket = ScopedSerialComposer.Packet();
        Check(ScopedParallelPacket.items.size() == ScopedSerialPacket.items.size(),
              "parallel extract item count matches serial");
        Check(ScopedParallelPacket.batches.size() == ScopedSerialPacket.batches.size(),
              "parallel extract batch count matches serial");
        Check(ScopedParallelPacket.items.size() == 5000u, "parallel extract keeps every visible sprite");
        Check(ScopedParallelPacket.items.size() == 5000u &&
                  std::memcmp(ScopedParallelPacket.items.data(), ScopedSerialPacket.items.data(),
                              ScopedParallelPacket.items.size() * sizeof(Fby::Rhi::RenderItem)) == 0,
              "parallel extract item order matches serial");

        ScopedParallelComposer.SetTemporalState(true);
        ScopedSerialComposer.SetTemporalState(true);
        ScopedParallelComposer.BuildFramePacket(ScopedParallelRegistry, ScopedParallelCamera, Fby::Color32::Black(),
                                                0u);
        ScopedSerialComposer.BuildFramePacket(ScopedParallelRegistry, ScopedParallelCamera, Fby::Color32::Black(), 0u);
        ScopedParallelComposer.BuildFramePacket(ScopedParallelRegistry, ScopedParallelCamera, Fby::Color32::Black(),
                                                0u);
        ScopedSerialComposer.BuildFramePacket(ScopedParallelRegistry, ScopedParallelCamera, Fby::Color32::Black(), 0u);
        Check(ScopedParallelComposer.Packet().motionItems.size() == 5000u,
              "parallel temporal extract keeps every motion item");
        Check(ScopedParallelComposer.Packet().motionItems.size() == ScopedSerialComposer.Packet().motionItems.size() &&
                  std::memcmp(ScopedParallelComposer.Packet().motionItems.data(),
                              ScopedSerialComposer.Packet().motionItems.data(),
                              ScopedParallelComposer.Packet().motionItems.size() *
                                  sizeof(Fby::Rhi::MotionVectorItem)) == 0,
              "parallel temporal motion order matches serial");

        ScopedParallelComposer.SetInstanceDeltasEnabled(true);
        ScopedParallelComposer.BuildFramePacket(ScopedParallelRegistry, ScopedParallelCamera, Fby::Color32::Black(),
                                                0u);
        if (ScopedParallelFirst != Fby::kNullEntity)
        {
            auto& ScopedMoved = ScopedParallelRegistry.get<Fby::Transform2D>(ScopedParallelFirst);
            ScopedMoved.Position.X += 4.0f;
        }
        ScopedParallelComposer.BuildFramePacket(ScopedParallelRegistry, ScopedParallelCamera, Fby::Color32::Black(),
                                                0u);
        Check(ScopedParallelComposer.Packet().instanceRangesAreDeltas,
              "instance deltas are emitted when the sort order is unchanged");
        Check(!ScopedParallelComposer.Packet().instanceRanges.empty(),
              "a changed transform produces a non-empty instance range");

        std::array<Fby::TileQuad, 2> ScopedTiles{};
        for (std::uint32_t ScopedI = 0; ScopedI < ScopedTiles.size(); ++ScopedI)
        {
            ScopedTiles[ScopedI].Item.position = Fby::Vector2D{static_cast<float>(ScopedI) * 4.0f, 0.0f};
            ScopedTiles[ScopedI].Item.size = Fby::Vector2D{4.0f, 4.0f};
            ScopedTiles[ScopedI].Key = ScopedI;
        }
        const std::span<const Fby::TileQuad> ScopedTileSpan(ScopedTiles.data(), ScopedTiles.size());
        ScopedParallelComposer.BuildFramePacket(ScopedParallelRegistry, ScopedParallelCamera, Fby::Color32::Black(), 0u,
                                                {}, ScopedTileSpan);
        ScopedSerialComposer.BuildFramePacket(ScopedParallelRegistry, ScopedParallelCamera, Fby::Color32::Black(), 0u,
                                              {}, ScopedTileSpan);
        Check(ScopedParallelComposer.Packet().items.size() == ScopedSerialComposer.Packet().items.size(),
              "parallel tile append matches serial item count");
        Check(ScopedParallelComposer.Packet().items.size() == ScopedSerialComposer.Packet().items.size() &&
                  std::memcmp(ScopedParallelComposer.Packet().items.data(), ScopedSerialComposer.Packet().items.data(),
                              ScopedParallelComposer.Packet().items.size() * sizeof(Fby::Rhi::RenderItem)) == 0,
              "parallel tile append keeps deterministic order");
    }

    // Optional render filters take the same parallel path only when their
    // per-entity checks are applied; the serial packet is the reference.
    {
        Fby::Ecs::Registry ScopedFilteredRegistry;
        for (std::uint32_t ScopedI = 0; ScopedI < 4096; ++ScopedI)
        {
            const Fby::Entity ScopedEntity = ScopedFilteredRegistry.create();
            ScopedFilteredRegistry.emplace<Fby::Transform2D>(
                ScopedEntity,
                Fby::Transform2D{Fby::Vector2D{static_cast<float>(ScopedI % 64u), static_cast<float>(ScopedI / 64u)},
                                 Fby::Vector2D{1.0f, 1.0f}, 0.0f});
            ScopedFilteredRegistry.emplace<Fby::Sprite2D>(ScopedEntity, Fby::Sprite2D{});
            ScopedFilteredRegistry.emplace<Fby::DrawOrder>(ScopedEntity, Fby::DrawOrder{0u, 0.0f});
            if (ScopedI % 10u == 0u)
            {
                ScopedFilteredRegistry.emplace<Fby::Hidden>(ScopedEntity, Fby::Hidden{});
            }
            if (ScopedI % 7u == 0u)
            {
                Fby::RenderMaskComponent ScopedMask;
                ScopedMask.ExcludePass(Fby::RenderPassSprites);
                ScopedFilteredRegistry.emplace<Fby::RenderMaskComponent>(ScopedEntity, ScopedMask);
            }
            if (ScopedI % 13u == 0u)
            {
                Fby::SpriteOverlay2D ScopedOverlay;
                ScopedOverlay.UVMin = Fby::Vector2D{0.25f, 0.25f};
                ScopedOverlay.UVMax = Fby::Vector2D{0.75f, 0.75f};
                ScopedOverlay.Layer = 1u;
                ScopedOverlay.Weight = 0.5f;
                ScopedFilteredRegistry.emplace<Fby::SpriteOverlay2D>(ScopedEntity, ScopedOverlay);
            }
        }
        const Fby::Rhi::CameraData ScopedFilteredCamera{Fby::Vector2D{32.0f, 32.0f},
                                                        Fby::Vector2D{100000.0f, 100000.0f}, 1.0f};
        Fby::JobSystem ScopedFilteredJobs(8);
        Fby::Rhi::FrameComposer ScopedFilteredParallel;
        ScopedFilteredParallel.SetJobSystem(&ScopedFilteredJobs);
        Fby::Rhi::FrameComposer ScopedFilteredSerial;
        ScopedFilteredParallel.BuildFramePacket(ScopedFilteredRegistry, ScopedFilteredCamera, Fby::Color32::Black(),
                                                0u);
        ScopedFilteredSerial.BuildFramePacket(ScopedFilteredRegistry, ScopedFilteredCamera, Fby::Color32::Black(), 0u);
        Check(!ScopedFilteredParallel.Packet().items.empty(), "optional filters leave visible sprites");
        Check(ScopedFilteredParallel.Packet().items.size() == ScopedFilteredSerial.Packet().items.size(),
              "optional-filter parallel item count matches serial");
        Check(ScopedFilteredParallel.Packet().items.size() == ScopedFilteredSerial.Packet().items.size() &&
                  std::memcmp(ScopedFilteredParallel.Packet().items.data(), ScopedFilteredSerial.Packet().items.data(),
                              ScopedFilteredParallel.Packet().items.size() * sizeof(Fby::Rhi::RenderItem)) == 0,
              "optional-filter parallel item order matches serial");
    }

    // A recycled entity index must not inherit the previous entity's motion
    // history; generation is part of the history key.
    {
        Fby::Ecs::Registry ScopedRecycleRegistry;
        const Fby::Entity ScopedOriginal = ScopedRecycleRegistry.create();
        ScopedRecycleRegistry.emplace<Fby::Transform2D>(ScopedOriginal, Fby::Transform2D{});
        ScopedRecycleRegistry.emplace<Fby::Sprite2D>(ScopedOriginal, Fby::Sprite2D{});
        ScopedRecycleRegistry.emplace<Fby::DrawOrder>(ScopedOriginal, Fby::DrawOrder{0u, 0.0f});
        const Fby::Rhi::CameraData ScopedRecycleCamera{Fby::Vector2D{0.0f, 0.0f}, Fby::Vector2D{1000.0f, 1000.0f},
                                                       1.0f};
        Fby::Rhi::FrameComposer ScopedRecycleComposer;
        ScopedRecycleComposer.SetTemporalState(true);
        ScopedRecycleComposer.BuildFramePacket(ScopedRecycleRegistry, ScopedRecycleCamera, Fby::Color32::Black(), 0u);
        ScopedRecycleComposer.BuildFramePacket(ScopedRecycleRegistry, ScopedRecycleCamera, Fby::Color32::Black(), 0u);
        Check(ScopedRecycleComposer.Packet().motionItems.size() == 1u, "temporal history seeds once");

        ScopedRecycleRegistry.destroy(ScopedOriginal);
        const Fby::Entity ScopedRecycled = ScopedRecycleRegistry.create();
        Check((Fby::ToIntegral(ScopedRecycled) & 0xFFFFFu) == (Fby::ToIntegral(ScopedOriginal) & 0xFFFFFu),
              "the test reuses the same entity index");
        ScopedRecycleRegistry.emplace<Fby::Transform2D>(ScopedRecycled, Fby::Transform2D{});
        ScopedRecycleRegistry.emplace<Fby::Sprite2D>(ScopedRecycled, Fby::Sprite2D{});
        ScopedRecycleRegistry.emplace<Fby::DrawOrder>(ScopedRecycled, Fby::DrawOrder{0u, 0.0f});
        ScopedRecycleComposer.BuildFramePacket(ScopedRecycleRegistry, ScopedRecycleCamera, Fby::Color32::Black(), 0u);
        Check(ScopedRecycleComposer.Packet().motionItems.empty(),
              "recycled entity generation does not inherit old motion history");
    }

    if (GFailures == 0)
    {
        std::printf("frame_composer: OK\n");
        return 0;
    }
    std::fprintf(stderr, "frame_composer: %d failure(s)\n", GFailures);
    return 1;
}
