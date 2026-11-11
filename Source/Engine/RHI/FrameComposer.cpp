// FemboyEngine2D - frame packet composition implementation.
#include "Engine/RHI/FrameComposer.hpp"
#include "Engine/Physics/SdfGi.hpp"

#include "Engine/ECS/Components.hpp"
#include "Engine/ECS/LightSource.hpp"
#include "Engine/ECS/Lighting.hpp"
#include "Engine/ECS/Presentation.hpp"
#include "Engine/ECS/Skeleton.hpp"
#include "Engine/Physics/SdfCharacter.hpp"
#include "Engine/Physics/SdfCollision.hpp"
#include "Engine/RHI/SkinningBridge.hpp"
#include "Engine/Core/JobSystem.hpp"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstring>
#include <numeric>

namespace Fby::Rhi
{
namespace
{
[[nodiscard]] std::uint64_t NextSceneId() noexcept
{
    static std::atomic<std::uint64_t> Counter{1};
    return Counter.fetch_add(1, std::memory_order_relaxed);
}
}
namespace
{
// Conservative rotated AABB test. It rejects whole sprites before they reach
// the sort, the instance upload and the GPU.
[[nodiscard]] bool IsSpriteVisible(const Transform2D& InTransform, const Sprite2D& InSprite, const Vector2D& InViewMin,
                                   const Vector2D& InViewMax) noexcept
{
    const Vector2D ScopedHalf{std::fabs(InSprite.Size.X * InTransform.Scale.X) * 0.5f,
                              std::fabs(InSprite.Size.Y * InTransform.Scale.Y) * 0.5f};
    const float ScopedCosine = std::fabs(std::cos(InTransform.Rotation));
    const float ScopedSine = std::fabs(std::sin(InTransform.Rotation));
    const Vector2D ScopedExtent{ScopedCosine * ScopedHalf.X + ScopedSine * ScopedHalf.Y,
                                ScopedSine * ScopedHalf.X + ScopedCosine * ScopedHalf.Y};
    return InTransform.Position.X + ScopedExtent.X >= InViewMin.X &&
           InTransform.Position.X - ScopedExtent.X <= InViewMax.X &&
           InTransform.Position.Y + ScopedExtent.Y >= InViewMin.Y &&
           InTransform.Position.Y - ScopedExtent.Y <= InViewMax.Y;
}

[[nodiscard]] std::uint32_t PipelineForBucket(std::span<const std::uint32_t> InPipelines,
                                              std::uint32_t InBucket) noexcept
{
    if (InBucket == 0u || InBucket >= InPipelines.size())
    {
        return 0u;
    }
    return InPipelines[InBucket];
}
}

void FrameComposer::BuildFramePacket(Ecs::Registry& InRegistry, const Rhi::CameraData& InCamera, Color32 InClear,
                                     std::uint32_t InTextureArray,
                                     std::span<const Rhi::MeshRenderItem> InExtraMeshItems,
                                     std::span<const TileQuad> InTileQuads, const Rhi::PostFxSettings& InPostFx,
                                     std::span<const SdfCharacterMeshData> InSdfCharacterMeshes)
{
    const auto ScopedStart = std::chrono::steady_clock::now();
#if !defined(NDEBUG)
    // A FrameComposer owns mutable scratch buffers; concurrent builds would race
    // even when the job pool itself is thread-safe.
    const std::thread::id ScopedBuildThread = std::this_thread::get_id();
    if (MBuildThread == std::thread::id{})
    {
        MBuildThread = ScopedBuildThread;
    }
    assert(MBuildThread == ScopedBuildThread && "FrameComposer must be built from one thread");
#endif
    if (MSceneId == 0)
    {
        MSceneId = NextSceneId();
    }
    MStats = FrameComposerStats{};
    const bool ScopedFastCamera = MHasLastCamera && MLastCamera.center.X == InCamera.center.X &&
                                  MLastCamera.center.Y == InCamera.center.Y &&
                                  MLastCamera.viewport.X == InCamera.viewport.X &&
                                  MLastCamera.viewport.Y == InCamera.viewport.Y && MLastCamera.zoom == InCamera.zoom;
    const bool ScopedFastMode = MViewportPresenter.Mode() == MLastViewportMode;
    const Ecs::Registry& ScopedRegistry = InRegistry;
    const bool ScopedFastScene = MHasLastFrame && ScopedRegistry.Data().Revision() == MLastSceneRevision &&
                                 InTextureArray == MLastTextureArray && ScopedFastCamera && ScopedFastMode &&
                                 InExtraMeshItems.empty() && InTileQuads.empty() && InSdfCharacterMeshes.empty() &&
                                 !MTemporalEnabled && MSdfField == nullptr && MSdfGiVolume == nullptr;
    if (ScopedFastScene)
    {
        // Nothing the extract reads changed and the camera is identical. Reuse
        // the previous item/key/batch arrays and tell the RHI that no instance
        // range is dirty; the previous slice is already valid for this scene.
        MPacket.camera = InCamera;
        MPacket.clear = InClear;
        MPacket.textureArray = InTextureArray;
        MPacket.postFx = InPostFx;
        MPacket.postFxEnabled = InPostFx.flags != Rhi::PostFxNone;
        MPacket.sceneId = MSceneId;
        MPacket.sceneStatic = true;
        return;
    }
    MViewportPresenter.SetSdfField(MSdfField);
    const bool ScopedCameraJump = MHasLastCamera && (std::abs(InCamera.center.X - MLastCamera.center.X) > 256.0f ||
                                                     std::abs(InCamera.center.Y - MLastCamera.center.Y) > 256.0f ||
                                                     std::abs(InCamera.zoom - MLastCamera.zoom) > 0.25f);
    if (MResetTemporalHistory || ScopedCameraJump)
    {
        // Reset before the extract walk: clearing after it would discard the
        // history that the next frame needs for its motion vectors.
        MMotionHistory.Clear();
        ++MHistoryGeneration;
        MResetTemporalHistory = false;
    }
    const Rhi::ViewportMode ScopedMode = MViewportPresenter.Mode();
    if (ScopedMode != Rhi::ViewportMode::Standard && ScopedMode != Rhi::ViewportMode::UnlitWireframe)
    {
        MViewportPresenter.Prepare(ScopedRegistry);
    }
    const bool ScopedUnlit = Rhi::ViewportModeNeedsUnlit(MViewportPresenter.Mode());
    const float ScopedZoom = std::max(InCamera.zoom, 0.0001f);
    const bool ScopedCull = InCamera.viewport.X > 0.0f && InCamera.viewport.Y > 0.0f;
    const Vector2D ScopedViewHalf{InCamera.viewport.X * 0.5f / ScopedZoom, InCamera.viewport.Y * 0.5f / ScopedZoom};
    const Vector2D ScopedViewMin = InCamera.center - ScopedViewHalf;
    const Vector2D ScopedViewMax = InCamera.center + ScopedViewHalf;
    auto ScopedView = ScopedRegistry.view<Transform2D, Sprite2D, DrawOrder>();
    auto ScopedSkinView = ScopedRegistry.view<SkeletonComponent, SkinComponent, SkinnedSprite>();
    // Most scenes have none of these optional filters. Checking the archetype
    // once avoids four per-sprite component lookups in the hot loop.
    const bool ScopedHasRenderMask = !ScopedRegistry.view<RenderMaskComponent>().empty();
    const bool ScopedHasHidden = !ScopedRegistry.view<Hidden>().empty();
    const bool ScopedHasLightResult = !ScopedRegistry.view<Light2DResultComponent>().empty();
    const bool ScopedHasOverlay = !ScopedRegistry.view<SpriteOverlay2D>().empty();
    // Every sprite may emit a crossfade overlay, so the item arrays reserve two
    // slots per entity before the tile quads.
    const std::size_t ScopedSpriteCount = ScopedView.size_hint();
    const std::size_t ScopedCount = ScopedSpriteCount * (ScopedHasOverlay ? 2u : 1u) + InTileQuads.size();
    MItems.resize(ScopedCount);
    MKeys.resize(ScopedCount);
    MOrder.resize(ScopedCount);
    MSorted.resize(ScopedCount);
    MSortedKeys.resize(ScopedCount);
    MMotionItems.clear();
    if (MTemporalEnabled)
    {
        MMotionItems.reserve(ScopedSpriteCount);
        const std::size_t ScopedRecords = ScopedRegistry.Data().RecordCount();
        if (MMotionHistory.Size() < ScopedRecords)
        {
            MMotionHistory.Resize(ScopedRecords);
        }
    }

    // GPU-skinning path: bind vertices plus one final Mat4 palette per skinned
    // sprite. Both arrays are sized for the worst case, then trimmed by count.
    const std::size_t ScopedSkinCount = ScopedSkinView.size_hint();
    MMeshVertices.resize(ScopedSkinCount * kMaxSkinVertices);
    MMeshItems.resize(ScopedSkinCount + InExtraMeshItems.size() + InSdfCharacterMeshes.size());
    MMeshPalette.resize(MMeshItems.size() * kMaxBones);
    std::size_t ScopedMeshIndex = 0;

    std::size_t ScopedIndex = 0;
    const bool ScopedParallelEligible = MJobs != nullptr && MJobs->ThreadCount() > 0 && ScopedSpriteCount >= 4096 &&
                                        ScopedMode == Rhi::ViewportMode::Standard;
    if (ScopedParallelEligible)
    {
        Fcs::Signature ScopedMask;
        (ScopedMask.set(Fcs::Detail::ComponentIdOf<Transform2D>()),
         ScopedMask.set(Fcs::Detail::ComponentIdOf<Sprite2D>()),
         ScopedMask.set(Fcs::Detail::ComponentIdOf<DrawOrder>()));
        Fcs::Signature ScopedHiddenMask;
        ScopedHiddenMask.set(Fcs::Detail::ComponentIdOf<Hidden>());
        Fcs::Signature ScopedRenderMaskMask;
        ScopedRenderMaskMask.set(Fcs::Detail::ComponentIdOf<RenderMaskComponent>());
        Fcs::Signature ScopedLightResultMask;
        ScopedLightResultMask.set(Fcs::Detail::ComponentIdOf<Light2DResultComponent>());
        const Fcs::World& ScopedWorld = ScopedRegistry.Data();
        MParallelChunks.clear();
        MParallelChunks.reserve(ScopedWorld.ArchetypeCount());
        for (std::uint32_t ScopedArchetype = 0; ScopedArchetype < ScopedWorld.ArchetypeCount(); ++ScopedArchetype)
        {
            const Fcs::World::Archetype& ScopedType = ScopedWorld.ArchetypeAt(ScopedArchetype);
            if ((ScopedType.Type & ScopedMask) != ScopedMask)
            {
                continue;
            }
            for (std::uint32_t ScopedChunk = 0; ScopedChunk < ScopedType.Chunks.size(); ++ScopedChunk)
            {
                if (ScopedType.Chunks[ScopedChunk].Count > 0)
                {
                    MParallelChunks.push_back(ParallelChunkRef{ScopedArchetype, ScopedChunk});
                }
            }
        }

        MParallelOffsets.assign(MParallelChunks.size() + 1, 0);
        std::uint32_t ScopedCulled = 0;
        std::uint32_t ScopedOit = 0;
        for (std::size_t ScopedChunkIndex = 0; ScopedChunkIndex < MParallelChunks.size(); ++ScopedChunkIndex)
        {
            const ParallelChunkRef& ScopedRef = MParallelChunks[ScopedChunkIndex];
            const Fcs::World::Archetype& ScopedType = ScopedWorld.ArchetypeAt(ScopedRef.Archetype);
            const Fcs::World::Chunk& ScopedChunk = ScopedType.Chunks[ScopedRef.Chunk];
            if ((ScopedType.Type & ScopedHiddenMask) == ScopedHiddenMask)
            {
                continue;
            }
            const bool ScopedChunkHasRenderMask = (ScopedType.Type & ScopedRenderMaskMask) == ScopedRenderMaskMask;
            for (std::uint32_t ScopedRow = 0; ScopedRow < ScopedChunk.Count; ++ScopedRow)
            {
                const Transform2D* ScopedTransform =
                    ScopedWorld.ComponentAt<Transform2D>(ScopedRef.Archetype, ScopedRef.Chunk, ScopedRow);
                const Sprite2D* ScopedSprite =
                    ScopedWorld.ComponentAt<Sprite2D>(ScopedRef.Archetype, ScopedRef.Chunk, ScopedRow);
                const Entity ScopedEntity = Fcs::ToEngineEntity(ScopedChunk.Entities[ScopedRow]);
                if (ScopedHasRenderMask && ScopedChunkHasRenderMask &&
                    !EntityAllowsRenderPass(InRegistry, ScopedEntity, RenderPassSprites))
                {
                    continue;
                }
                if (ScopedCull && !IsSpriteVisible(*ScopedTransform, *ScopedSprite, ScopedViewMin, ScopedViewMax))
                {
                    ++ScopedCulled;
                    continue;
                }
                if ((ScopedSprite->Flags & static_cast<std::uint32_t>(Rhi::RenderFlags::Oit)) != 0u)
                {
                    ++ScopedOit;
                }
                ++MParallelOffsets[ScopedChunkIndex + 1];
                if (ScopedHasOverlay)
                {
                    if (const auto* ScopedOverlay = ScopedRegistry.try_get<SpriteOverlay2D>(ScopedEntity);
                        ScopedOverlay != nullptr && ScopedOverlay->Weight > 0.0f)
                    {
                        ++MParallelOffsets[ScopedChunkIndex + 1];
                    }
                }
            }
        }
        for (std::size_t ScopedI = 1; ScopedI < MParallelOffsets.size(); ++ScopedI)
        {
            MParallelOffsets[ScopedI] += MParallelOffsets[ScopedI - 1];
        }
        const std::size_t ScopedTotal = MParallelOffsets.back();
        MItems.resize(ScopedTotal + InTileQuads.size());
        MKeys.resize(ScopedTotal + InTileQuads.size());
        if (MTemporalEnabled)
        {
            MMotionItems.resize(ScopedTotal);
            if (MMotionStamp.size() < ScopedTotal)
            {
                MMotionStamp.resize(ScopedTotal);
            }
            ++MMotionStampValue;
            if (MMotionStampValue == 0)
            {
                std::fill(MMotionStamp.begin(), MMotionStamp.end(), 0);
                MMotionStampValue = 1;
            }
        }
        MStats.CulledSprites += ScopedCulled;
        MStats.OitItems += ScopedOit;

        MJobs->ParallelFor(
            static_cast<std::uint32_t>(MParallelChunks.size()), 1,
            [&](std::uint32_t InBegin, std::uint32_t InEnd)
            {
                for (std::uint32_t ScopedChunkIndex = InBegin; ScopedChunkIndex < InEnd; ++ScopedChunkIndex)
                {
                    const ParallelChunkRef& ScopedRef = MParallelChunks[ScopedChunkIndex];
                    const Fcs::World::Archetype& ScopedType = ScopedWorld.ArchetypeAt(ScopedRef.Archetype);
                    const Fcs::World::Chunk& ScopedChunk = ScopedType.Chunks[ScopedRef.Chunk];
                    if ((ScopedType.Type & ScopedHiddenMask) == ScopedHiddenMask)
                    {
                        continue;
                    }
                    const bool ScopedChunkHasRenderMask =
                        (ScopedType.Type & ScopedRenderMaskMask) == ScopedRenderMaskMask;
                    const bool ScopedChunkHasLightResult =
                        (ScopedType.Type & ScopedLightResultMask) == ScopedLightResultMask;
                    std::size_t ScopedWrite = MParallelOffsets[ScopedChunkIndex];
                    for (std::uint32_t ScopedRow = 0; ScopedRow < ScopedChunk.Count; ++ScopedRow)
                    {
                        const Transform2D* ScopedTransform =
                            ScopedWorld.ComponentAt<Transform2D>(ScopedRef.Archetype, ScopedRef.Chunk, ScopedRow);
                        const Sprite2D* ScopedSprite =
                            ScopedWorld.ComponentAt<Sprite2D>(ScopedRef.Archetype, ScopedRef.Chunk, ScopedRow);
                        const DrawOrder* ScopedOrder =
                            ScopedWorld.ComponentAt<DrawOrder>(ScopedRef.Archetype, ScopedRef.Chunk, ScopedRow);
                        const Entity ScopedEntity = Fcs::ToEngineEntity(ScopedChunk.Entities[ScopedRow]);
                        if (ScopedHasRenderMask && ScopedChunkHasRenderMask &&
                            !EntityAllowsRenderPass(InRegistry, ScopedEntity, RenderPassSprites))
                        {
                            continue;
                        }
                        if (ScopedCull &&
                            !IsSpriteVisible(*ScopedTransform, *ScopedSprite, ScopedViewMin, ScopedViewMax))
                        {
                            continue;
                        }
                        std::uint32_t ScopedColor = ScopedSprite->Color.rgba;
                        if (!ScopedUnlit && !MGpuLighting && ScopedHasLightResult && ScopedChunkHasLightResult)
                        {
                            if (const auto* ScopedLight = ScopedRegistry.try_get<Light2DResultComponent>(ScopedEntity))
                            {
                                ScopedColor = LightingSystem::Compose(ScopedSprite->Color, ScopedLight->Color).rgba;
                            }
                        }
                        const std::uint32_t ScopedBlendBits =
                            static_cast<std::uint32_t>(Rhi::RenderFlags::Additive) |
                            static_cast<std::uint32_t>(Rhi::RenderFlags::Premultiplied);
                        const bool ScopedOpaque =
                            ScopedSprite->Color.A() == 255u && (ScopedSprite->Flags & ScopedBlendBits) == 0u;
                        const std::uint32_t ScopedBucket =
                            MMaterialPipelines.empty()
                                ? 0u
                                : std::min(ScopedOrder->MaterialBucket, Rhi::kMaxMaterialBuckets - 1u);
                        MItems[ScopedWrite] =
                            Rhi::RenderItem{ScopedTransform->Position,
                                            ScopedSprite->Size * ScopedTransform->Scale,
                                            ScopedSprite->UVMin,
                                            ScopedSprite->UVMax,
                                            ScopedSprite->Layer,
                                            ScopedColor,
                                            ScopedTransform->Rotation,
                                            Rhi::EncodeDepth(ScopedSprite->Flags, ScopedOrder->Depth, ScopedOpaque)};
                        MKeys[ScopedWrite] = Rhi::MakeMaterialRenderKey(static_cast<std::uint8_t>(ScopedBucket),
                                                                        ScopedOrder->Layer, ScopedOrder->Depth);
                        if (MTemporalEnabled)
                        {
                            const std::size_t ScopedHistoryIndex = static_cast<std::size_t>(
                                Fby::ToIntegral(ScopedEntity) & ((std::uint32_t{1} << 20u) - 1u));
                            if (ScopedHistoryIndex < MMotionHistory.Size())
                            {
                                const Vector2D ScopedSize = ScopedSprite->Size * ScopedTransform->Scale;
                                const std::uint32_t ScopedGeneration = Fby::ToIntegral(ScopedEntity) >> 20u;
                                if (MMotionHistory.Epoch[ScopedHistoryIndex] == MMotionEpoch &&
                                    MMotionHistory.Generation[ScopedHistoryIndex] == ScopedGeneration)
                                {
                                    Rhi::MotionVectorItem ScopedVector;
                                    ScopedVector.position = ScopedTransform->Position;
                                    ScopedVector.size = ScopedSize;
                                    ScopedVector.previousPosition =
                                        Vector2D{MMotionHistory.PositionX[ScopedHistoryIndex],
                                                 MMotionHistory.PositionY[ScopedHistoryIndex]};
                                    ScopedVector.previousSize = Vector2D{MMotionHistory.SizeX[ScopedHistoryIndex],
                                                                         MMotionHistory.SizeY[ScopedHistoryIndex]};
                                    ScopedVector.rotation = ScopedTransform->Rotation;
                                    ScopedVector.previousRotation = MMotionHistory.Rotation[ScopedHistoryIndex];
                                    ScopedVector.screenDelta =
                                        (ScopedTransform->Position - ScopedVector.previousPosition) * ScopedZoom;
                                    ScopedVector.flags = ScopedSprite->Flags;
                                    MMotionItems[ScopedWrite] = ScopedVector;
                                    MMotionStamp[ScopedWrite] = MMotionStampValue;
                                }
                                MMotionHistory.PositionX[ScopedHistoryIndex] = ScopedTransform->Position.X;
                                MMotionHistory.PositionY[ScopedHistoryIndex] = ScopedTransform->Position.Y;
                                MMotionHistory.SizeX[ScopedHistoryIndex] = ScopedSize.X;
                                MMotionHistory.SizeY[ScopedHistoryIndex] = ScopedSize.Y;
                                MMotionHistory.Rotation[ScopedHistoryIndex] = ScopedTransform->Rotation;
                                MMotionHistory.Epoch[ScopedHistoryIndex] = MMotionEpoch;
                                MMotionHistory.Generation[ScopedHistoryIndex] = ScopedGeneration;
                            }
                        }
                        const auto* ScopedOverlay =
                            ScopedHasOverlay ? ScopedRegistry.try_get<SpriteOverlay2D>(ScopedEntity) : nullptr;
                        if (ScopedOverlay != nullptr && ScopedOverlay->Weight > 0.0f)
                        {
                            const float ScopedWeight = std::clamp(ScopedOverlay->Weight, 0.0f, 1.0f);
                            Color32 ScopedOverlayColor = ScopedOverlay->Color;
                            const std::uint8_t ScopedAlpha = static_cast<std::uint8_t>(std::clamp(
                                static_cast<float>(ScopedOverlayColor.A()) * ScopedWeight + 0.5f, 0.0f, 255.0f));
                            ScopedOverlayColor = Color32::FromBytes(ScopedOverlayColor.R(), ScopedOverlayColor.G(),
                                                                    ScopedOverlayColor.B(), ScopedAlpha);
                            std::uint32_t ScopedOverlayPacked = ScopedOverlayColor.rgba;
                            if (!ScopedUnlit && !MGpuLighting && ScopedHasLightResult && ScopedChunkHasLightResult)
                            {
                                if (const auto* ScopedLight =
                                        ScopedRegistry.try_get<Light2DResultComponent>(ScopedEntity))
                                {
                                    ScopedOverlayPacked =
                                        LightingSystem::Compose(ScopedOverlayColor, ScopedLight->Color).rgba;
                                }
                            }
                            ++ScopedWrite;
                            const float ScopedOverlayDepth = ScopedOrder->Depth + ScopedOverlay->DepthBias;
                            MItems[ScopedWrite] =
                                Rhi::RenderItem{ScopedTransform->Position,
                                                ScopedSprite->Size * ScopedTransform->Scale,
                                                ScopedOverlay->UVMin,
                                                ScopedOverlay->UVMax,
                                                ScopedOverlay->Layer,
                                                ScopedOverlayPacked,
                                                ScopedTransform->Rotation,
                                                Rhi::EncodeDepth(ScopedOverlay->Flags, ScopedOverlayDepth, false)};
                            MKeys[ScopedWrite] = Rhi::MakeMaterialRenderKey(static_cast<std::uint8_t>(ScopedBucket),
                                                                            ScopedOrder->Layer, ScopedOverlayDepth);
                        }
                        ++ScopedWrite;
                    }
                    assert(ScopedWrite == MParallelOffsets[ScopedChunkIndex + 1] &&
                           "parallel chunk count disagrees with its prefix offset");
                }
            });
        if (MTemporalEnabled)
        {
            std::size_t ScopedMotionWrite = 0;
            for (std::size_t ScopedI = 0; ScopedI < ScopedTotal; ++ScopedI)
            {
                if (MMotionStamp[ScopedI] == MMotionStampValue)
                {
                    if (ScopedMotionWrite != ScopedI)
                    {
                        MMotionItems[ScopedMotionWrite] = MMotionItems[ScopedI];
                    }
                    ++ScopedMotionWrite;
                }
            }
            MMotionItems.resize(ScopedMotionWrite);
        }
        ScopedIndex = ScopedTotal;
    }
    else
    {
        for (auto [ScopedEntity, ScopedTransform, ScopedSprite, ScopedOrder] : ScopedView.each())
        {
            if ((ScopedHasRenderMask && !EntityAllowsRenderPass(InRegistry, ScopedEntity, RenderPassSprites)) ||
                (ScopedHasHidden && ScopedRegistry.all_of<Hidden>(ScopedEntity)))
            {
                continue;
            }
            if (ScopedCull && !IsSpriteVisible(ScopedTransform, ScopedSprite, ScopedViewMin, ScopedViewMax))
            {
                ++MStats.CulledSprites;
                continue;
            }
            // The lighting system writes its own result component; the extract
            // composes it with the authored sprite colour.
            std::uint32_t ScopedColor = ScopedSprite.Color.rgba;
            if (!ScopedUnlit && !MGpuLighting && ScopedHasLightResult)
            {
                if (const auto* ScopedLight = ScopedRegistry.try_get<Light2DResultComponent>(ScopedEntity))
                {
                    ScopedColor = LightingSystem::Compose(ScopedSprite.Color, ScopedLight->Color).rgba;
                }
            }
            std::uint32_t ScopedTint = ScopedColor;
            if (MViewportPresenter.TintForEntity(ScopedEntity, &ScopedTint))
            {
                ScopedColor = ScopedTint;
            }
            const std::uint32_t ScopedBlendBits = static_cast<std::uint32_t>(Rhi::RenderFlags::Additive) |
                                                  static_cast<std::uint32_t>(Rhi::RenderFlags::Premultiplied);
            const bool ScopedOpaque = ScopedSprite.Color.A() == 255u && (ScopedSprite.Flags & ScopedBlendBits) == 0u;
            if ((ScopedSprite.Flags & static_cast<std::uint32_t>(Rhi::RenderFlags::Oit)) != 0u)
            {
                ++MStats.OitItems;
            }
            MItems[ScopedIndex] = Rhi::RenderItem{
                ScopedTransform.Position, ScopedSprite.Size * ScopedTransform.Scale,
                ScopedSprite.UVMin,       ScopedSprite.UVMax,
                ScopedSprite.Layer,       ScopedColor,
                ScopedTransform.Rotation, Rhi::EncodeDepth(ScopedSprite.Flags, ScopedOrder.Depth, ScopedOpaque)};
            if (MTemporalEnabled)
            {
                const std::size_t ScopedHistoryIndex =
                    static_cast<std::size_t>(Fby::ToIntegral(ScopedEntity) & ((std::uint32_t{1} << 20u) - 1u));
                if (ScopedHistoryIndex < MMotionHistory.Size())
                {
                    const Vector2D ScopedSize = ScopedSprite.Size * ScopedTransform.Scale;
                    const std::uint32_t ScopedGeneration = Fby::ToIntegral(ScopedEntity) >> 20u;
                    if (MMotionHistory.Epoch[ScopedHistoryIndex] == MMotionEpoch &&
                        MMotionHistory.Generation[ScopedHistoryIndex] == ScopedGeneration)
                    {
                        Rhi::MotionVectorItem ScopedMotion;
                        ScopedMotion.position = ScopedTransform.Position;
                        ScopedMotion.size = ScopedSize;
                        ScopedMotion.previousPosition = Vector2D{MMotionHistory.PositionX[ScopedHistoryIndex],
                                                                 MMotionHistory.PositionY[ScopedHistoryIndex]};
                        ScopedMotion.previousSize = Vector2D{MMotionHistory.SizeX[ScopedHistoryIndex],
                                                             MMotionHistory.SizeY[ScopedHistoryIndex]};
                        ScopedMotion.rotation = ScopedTransform.Rotation;
                        ScopedMotion.previousRotation = MMotionHistory.Rotation[ScopedHistoryIndex];
                        ScopedMotion.screenDelta =
                            (ScopedTransform.Position - ScopedMotion.previousPosition) * ScopedZoom;
                        ScopedMotion.flags = ScopedSprite.Flags;
                        MMotionItems.push_back(ScopedMotion);
                    }
                    MMotionHistory.PositionX[ScopedHistoryIndex] = ScopedTransform.Position.X;
                    MMotionHistory.PositionY[ScopedHistoryIndex] = ScopedTransform.Position.Y;
                    MMotionHistory.SizeX[ScopedHistoryIndex] = ScopedSize.X;
                    MMotionHistory.SizeY[ScopedHistoryIndex] = ScopedSize.Y;
                    MMotionHistory.Rotation[ScopedHistoryIndex] = ScopedTransform.Rotation;
                    MMotionHistory.Epoch[ScopedHistoryIndex] = MMotionEpoch;
                    MMotionHistory.Generation[ScopedHistoryIndex] = ScopedGeneration;
                }
            }
            const std::uint32_t ScopedBucket =
                MMaterialPipelines.empty() ? 0u : std::min(ScopedOrder.MaterialBucket, Rhi::kMaxMaterialBuckets - 1u);
            MKeys[ScopedIndex] = Rhi::MakeMaterialRenderKey(static_cast<std::uint8_t>(ScopedBucket), ScopedOrder.Layer,
                                                            ScopedOrder.Depth);
            ++ScopedIndex;

            // The overlay shares the base transform and size; only the atlas cell
            // and the alpha differ, so a flipbook crossfade stays one entity.
            const auto* ScopedOverlay =
                ScopedHasOverlay ? ScopedRegistry.try_get<SpriteOverlay2D>(ScopedEntity) : nullptr;
            if (ScopedOverlay != nullptr && ScopedOverlay->Weight > 0.0f)
            {
                if (ScopedIndex < MItems.size())
                {
                    const float ScopedWeight = std::clamp(ScopedOverlay->Weight, 0.0f, 1.0f);
                    Color32 ScopedOverlayColor = ScopedOverlay->Color;
                    const std::uint8_t ScopedAlpha = static_cast<std::uint8_t>(
                        std::clamp(static_cast<float>(ScopedOverlayColor.A()) * ScopedWeight + 0.5f, 0.0f, 255.0f));
                    ScopedOverlayColor = Color32::FromBytes(ScopedOverlayColor.R(), ScopedOverlayColor.G(),
                                                            ScopedOverlayColor.B(), ScopedAlpha);
                    std::uint32_t ScopedOverlayPacked = ScopedOverlayColor.rgba;
                    if (!ScopedUnlit && !MGpuLighting && ScopedHasLightResult)
                    {
                        if (const auto* ScopedLight = ScopedRegistry.try_get<Light2DResultComponent>(ScopedEntity))
                        {
                            ScopedOverlayPacked = LightingSystem::Compose(ScopedOverlayColor, ScopedLight->Color).rgba;
                        }
                    }
                    std::uint32_t ScopedOverlayTint = ScopedOverlayPacked;
                    if (MViewportPresenter.TintForEntity(ScopedEntity, &ScopedOverlayTint))
                    {
                        ScopedOverlayPacked = ScopedOverlayTint;
                    }
                    const float ScopedOverlayDepth = ScopedOrder.Depth + ScopedOverlay->DepthBias;
                    MItems[ScopedIndex] = Rhi::RenderItem{
                        ScopedTransform.Position, ScopedSprite.Size * ScopedTransform.Scale,
                        ScopedOverlay->UVMin,     ScopedOverlay->UVMax,
                        ScopedOverlay->Layer,     ScopedOverlayPacked,
                        ScopedTransform.Rotation, Rhi::EncodeDepth(ScopedOverlay->Flags, ScopedOverlayDepth, false)};
                    MKeys[ScopedIndex] = Rhi::MakeMaterialRenderKey(static_cast<std::uint8_t>(ScopedBucket),
                                                                    ScopedOrder.Layer, ScopedOverlayDepth);
                    ++ScopedIndex;
                }
            }
        }
    }
    static constexpr std::uint16_t ScopedSkeletalQuadIndices[6] = {0, 1, 2, 0, 2, 3};
    for (auto [ScopedEntity, ScopedSkeleton, ScopedSkin, ScopedSprite] : ScopedSkinView.each())
    {
        if (ScopedSkin.VertexCount < 4 ||
            (ScopedHasRenderMask && !EntityAllowsRenderPass(InRegistry, ScopedEntity, RenderPassSprites)) ||
            (ScopedHasHidden && ScopedRegistry.all_of<Hidden>(ScopedEntity)))
        {
            continue;
        }
        std::uint32_t ScopedSkinColor = ScopedSprite.Color.rgba;
        if (!ScopedUnlit && ScopedHasLightResult)
        {
            if (const auto* ScopedLight = ScopedRegistry.try_get<Light2DResultComponent>(ScopedEntity))
            {
                ScopedSkinColor = LightingSystem::Compose(ScopedSprite.Color, ScopedLight->Color).rgba;
            }
        }
        std::uint32_t ScopedTint = ScopedSkinColor;
        if (MViewportPresenter.TintForEntity(ScopedEntity, &ScopedTint))
        {
            ScopedSkinColor = ScopedTint;
        }
        Rhi::MeshVertex* ScopedVertices =
            MMeshVertices.data() + ScopedMeshIndex * static_cast<std::size_t>(kMaxSkinVertices);
        std::array<Vector2D, 4> ScopedUvs{};
        ScopedUvs[0] = ScopedSprite.UVMin;
        ScopedUvs[1] = Vector2D{ScopedSprite.UVMax.X, ScopedSprite.UVMin.Y};
        ScopedUvs[2] = ScopedSprite.UVMax;
        ScopedUvs[3] = Vector2D{ScopedSprite.UVMin.X, ScopedSprite.UVMax.Y};
        Render::BuildMeshVertices(ScopedSkin, ScopedUvs, ScopedVertices, ScopedSkinColor);

        Mat4* ScopedPalette = MMeshPalette.data() + ScopedMeshIndex * static_cast<std::size_t>(kMaxBones);
        const auto ScopedSourcePalette = Render::SkeletonPalette(ScopedSkeleton);
        std::copy(ScopedSourcePalette.begin(), ScopedSourcePalette.end(), ScopedPalette);

        Rhi::MeshRenderItem ScopedItem;
        ScopedItem.vertices = {ScopedVertices, ScopedSkin.VertexCount};
        ScopedItem.indices = {ScopedSkeletalQuadIndices, 6};
        ScopedItem.boneOffset = static_cast<std::uint32_t>(ScopedMeshIndex * kMaxBones);
        ScopedItem.boneCount = static_cast<std::uint32_t>(ScopedSourcePalette.size());
        ScopedItem.textureArray = InTextureArray;
        ScopedItem.layer = ScopedSprite.Layer;
        ScopedItem.model = Mat4::Identity();
        ScopedItem.sortLayer = ScopedSprite.SortLayer;
        ScopedItem.depth = ScopedSprite.Depth;
        MMeshItems[ScopedMeshIndex++] = ScopedItem;
    }

    // Cloth bridge output. The spans point into ClothSystem's stable vertex
    // storage, which outlives the packet for this frame.
    for (const Rhi::MeshRenderItem& ScopedExtra : InExtraMeshItems)
    {
        if (ScopedMeshIndex >= MMeshItems.size())
        {
            break;
        }
        Rhi::MeshRenderItem ScopedItem = ScopedExtra;
        if (ScopedItem.textureArray == 0xFFFFFFFFu)
        {
            ScopedItem.textureArray = InTextureArray;
        }
        ScopedItem.flags |= Rhi::MeshItemUnskinned;
        MMeshItems[ScopedMeshIndex++] = ScopedItem;
    }

    // SDF character mesh bridge: the caller baked the field, this composer owns
    // the copied vertex/index storage the packet points into. There is no
    // distance LOD: every part uses the same field resolution.
    MSdfCharacterVertices.clear();
    MSdfCharacterIndices.clear();
    if (!InSdfCharacterMeshes.empty())
    {
        std::size_t ScopedTotalVertices = 0;
        std::size_t ScopedTotalIndices = 0;
        for (const SdfCharacterMeshData& ScopedMesh : InSdfCharacterMeshes)
        {
            ScopedTotalVertices += ScopedMesh.Positions.size();
            ScopedTotalIndices += ScopedMesh.Indices.size();
        }
        MSdfCharacterVertices.reserve(ScopedTotalVertices);
        MSdfCharacterIndices.reserve(ScopedTotalIndices);
        std::vector<std::uint32_t> ScopedOffsets;
        ScopedOffsets.reserve(InSdfCharacterMeshes.size());
        for (const SdfCharacterMeshData& ScopedMesh : InSdfCharacterMeshes)
        {
            ScopedOffsets.push_back(static_cast<std::uint32_t>(MSdfCharacterVertices.size()));
            const std::uint16_t ScopedBase = static_cast<std::uint16_t>(MSdfCharacterVertices.size());
            for (const Vector2D ScopedPosition : ScopedMesh.Positions)
            {
                Rhi::MeshVertex ScopedVertex;
                ScopedVertex.position = ScopedPosition;
                ScopedVertex.color = 0xFFFFFFFFu;
                MSdfCharacterVertices.push_back(ScopedVertex);
            }
            for (const std::uint16_t ScopedIndex : ScopedMesh.Indices)
            {
                MSdfCharacterIndices.push_back(static_cast<std::uint16_t>(ScopedBase + ScopedIndex));
            }
        }
        for (std::size_t ScopedMeshIndex2 = 0; ScopedMeshIndex2 < InSdfCharacterMeshes.size(); ++ScopedMeshIndex2)
        {
            if (ScopedMeshIndex >= MMeshItems.size())
            {
                break;
            }
            const SdfCharacterMeshData& ScopedMesh = InSdfCharacterMeshes[ScopedMeshIndex2];
            Rhi::MeshRenderItem ScopedItem;
            ScopedItem.vertices = std::span<const Rhi::MeshVertex>(
                MSdfCharacterVertices.data() + ScopedOffsets[ScopedMeshIndex2], ScopedMesh.Positions.size());
            ScopedItem.indices = std::span<const std::uint16_t>(
                MSdfCharacterIndices.data() + ScopedOffsets[ScopedMeshIndex2], ScopedMesh.Indices.size());
            ScopedItem.textureArray = InTextureArray;
            ScopedItem.color = Color32::White();
            ScopedItem.model = Mat4::Identity();
            ScopedItem.flags = Rhi::MeshItemUnskinned;
            MMeshItems[ScopedMeshIndex++] = ScopedItem;
        }
    }
    // Tilemap quads: one item per visible tile, already keyed by the isometric
    // depth the tilemap system computed.
    for (const TileQuad& ScopedQuad : InTileQuads)
    {
        if (ScopedIndex >= MItems.size())
        {
            break;
        }
        MItems[ScopedIndex] = ScopedQuad.Item;
        // Tile quads are opaque geometry: include them in the depth prepass.
        MItems[ScopedIndex].flags = Rhi::EncodeDepth(MItems[ScopedIndex].flags, 0.5f, true);
        MKeys[ScopedIndex] = ScopedQuad.Key;
        ++ScopedIndex;
    }
    MCount = static_cast<std::uint32_t>(ScopedIndex);
    MStats.ExtractMilliseconds =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - ScopedStart).count();
    const auto ScopedSortStart = std::chrono::steady_clock::now();

    // A static scene keeps the previous order and the previous sorted array.
    // When only item contents change, the order is still valid and only the
    // gather pass runs; a sort happens only when a key or the count changes.
    const bool ScopedSameCount = MHasLastFrame && MCount == MLastCount;
    const bool ScopedSameKeys =
        ScopedSameCount && (MCount == 0u || std::equal(MKeys.begin(), MKeys.begin() + MCount, MLastKeys.begin()));
    const bool ScopedSameItems =
        ScopedSameCount &&
        (MCount == 0u || std::memcmp(MItems.data(), MLastItems.data(),
                                     static_cast<std::size_t>(MCount) * sizeof(Rhi::RenderItem)) == 0);
    // The OIT and opaque splits depend only on the gathered order and flags, so
    // an unchanged frame reuses the arrays instead of copying every item again.
    const bool ScopedOutputsReusable = MHasLastFrame && ScopedSameKeys && ScopedSameItems;

    if (!ScopedSameKeys)
    {
        std::iota(MOrder.begin(), MOrder.begin() + MCount, 0u);
        MSorter.Sort(std::span<const Rhi::RenderKey>(MKeys.data(), MCount),
                     std::span<std::uint32_t>(MOrder.data(), MCount));
        MBatches.clear();
        std::uint32_t ScopedBegin = 0;
        for (std::uint32_t ScopedI = 0; ScopedI < MCount; ++ScopedI)
        {
            if (ScopedI > 0 && (MKeys[MOrder[ScopedI]] >> Rhi::kMaterialKeyShift) !=
                                   (MKeys[MOrder[ScopedI - 1]] >> Rhi::kMaterialKeyShift))
            {
                const std::uint32_t ScopedBucket =
                    static_cast<std::uint32_t>((MKeys[MOrder[ScopedI - 1]] >> Rhi::kMaterialKeyShift) & 0xFFu);
                MBatches.push_back(Rhi::DrawBatch{ScopedBegin, ScopedI - ScopedBegin,
                                                  PipelineForBucket(MMaterialPipelines, ScopedBucket), 0});
                ScopedBegin = ScopedI;
            }
        }
        if (MCount > 0)
        {
            const std::uint32_t ScopedBucket =
                static_cast<std::uint32_t>((MKeys[MOrder[MCount - 1]] >> Rhi::kMaterialKeyShift) & 0xFFu);
            MBatches.push_back(Rhi::DrawBatch{ScopedBegin, MCount - ScopedBegin,
                                              PipelineForBucket(MMaterialPipelines, ScopedBucket), 0});
        }
    }

    MStats.SortMilliseconds =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - ScopedSortStart).count();
    const auto ScopedGatherStart = std::chrono::steady_clock::now();

    const bool ScopedGathered = !ScopedSameKeys || !ScopedSameItems;
    if (ScopedGathered)
    {
        for (std::uint32_t ScopedI = 0; ScopedI < MCount; ++ScopedI)
        {
            MSorted[ScopedI] = MItems[MOrder[ScopedI]];
            MSortedKeys[ScopedI] = MKeys[MOrder[ScopedI]];
        }
        MLastItems.resize(MCount);
        MLastKeys.resize(MCount);
        std::memcpy(MLastItems.data(), MItems.data(), static_cast<std::size_t>(MCount) * sizeof(Rhi::RenderItem));
        std::memcpy(MLastKeys.data(), MKeys.data(), static_cast<std::size_t>(MCount) * sizeof(Rhi::RenderKey));
    }

    MStats.GatherMilliseconds =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - ScopedGatherStart).count();

    // Fully opaque frames can be drawn front-to-back: the depth test then
    // rejects fragments hidden behind a nearer sprite. Multiple material
    // buckets keep their original order because batch ranges are keyed.
    const bool ScopedLayerModesKnown = MForceOpaqueCutout || !MOpaqueLayerModes.empty();
    if (MOpaqueFrontToBack && ScopedLayerModesKnown && MCount > 0 && MBatches.size() == 1)
    {
        bool ScopedAllOpaque = true;
        for (std::uint32_t ScopedI = 0; ScopedI < MCount; ++ScopedI)
        {
            const std::uint32_t ScopedLayer = MSorted[ScopedI].layer;
            const bool ScopedLayerCutout =
                MForceOpaqueCutout || (static_cast<std::size_t>(ScopedLayer) < MOpaqueLayerModes.size() &&
                                       MOpaqueLayerModes[ScopedLayer] != Assets::TextureAlphaMode::Blend);
            if (!ScopedLayerCutout || (MSorted[ScopedI].flags & Rhi::DepthWriteMask) == 0u)
            {
                ScopedAllOpaque = false;
                break;
            }
        }
        if (ScopedAllOpaque)
        {
            // The gather pass is skipped on a static frame, and this block rewrites
            // the sorted copy. Rebuild it from the cached items first so the order
            // and the keys stay aligned and do not flip on every repeated frame.
            if (!ScopedGathered)
            {
                for (std::uint32_t ScopedI = 0; ScopedI < MCount; ++ScopedI)
                {
                    MSorted[ScopedI] = MItems[MOrder[ScopedI]];
                    MSortedKeys[ScopedI] = MKeys[MOrder[ScopedI]];
                }
            }
            std::reverse(MSorted.begin(), MSorted.begin() + MCount);
            std::reverse(MSortedKeys.begin(), MSortedKeys.begin() + MCount);
            for (std::uint32_t ScopedI = 0; ScopedI < MCount; ++ScopedI)
            {
                const float ScopedRatio =
                    MCount > 1u ? static_cast<float>(ScopedI) / static_cast<float>(MCount - 1u) : 0.0f;
                MSorted[ScopedI].flags = Rhi::EncodeDepth(MSorted[ScopedI].flags, 0.01f + 0.98f * ScopedRatio, true);
            }
            MBatches[0].firstItem = 0;
            MBatches[0].itemCount = MCount;
        }
    }

    // The prepass only needs opaque, depth-writing runs. Keeping them separate
    // avoids issuing draw calls for transparent sprites that write z = 1.
    if (!ScopedSameKeys || !ScopedSameItems)
    {
        MDepthBatches.clear();
        std::uint32_t ScopedDepthBegin = 0;
        bool ScopedDepthRun = false;
        for (std::uint32_t ScopedI = 0; ScopedI < MCount; ++ScopedI)
        {
            const bool ScopedOpaque = (MSorted[ScopedI].flags & Rhi::DepthWriteMask) != 0u;
            if (ScopedOpaque && !ScopedDepthRun)
            {
                ScopedDepthBegin = ScopedI;
                ScopedDepthRun = true;
            }
            else if (!ScopedOpaque && ScopedDepthRun)
            {
                MDepthBatches.push_back(Rhi::DrawBatch{ScopedDepthBegin, ScopedI - ScopedDepthBegin, 0, 0});
                ScopedDepthRun = false;
            }
        }
        if (ScopedDepthRun)
        {
            MDepthBatches.push_back(Rhi::DrawBatch{ScopedDepthBegin, MCount - ScopedDepthBegin, 0, 0});
        }
    }
    MStats.DepthBatches = static_cast<std::uint32_t>(MDepthBatches.size());
    MStats.DepthItems = 0;
    for (const Rhi::DrawBatch& ScopedBatch : MDepthBatches)
    {
        MStats.DepthItems += ScopedBatch.itemCount;
    }

    const bool ScopedPreviousCameraValid = MHasLastCamera;
    const Rhi::CameraData ScopedPreviousCamera = MLastCamera;
    const bool ScopedCameraSame = MHasLastCamera && MLastCamera.center.X == InCamera.center.X &&
                                  MLastCamera.center.Y == InCamera.center.Y &&
                                  MLastCamera.viewport.X == InCamera.viewport.X &&
                                  MLastCamera.viewport.Y == InCamera.viewport.Y && MLastCamera.zoom == InCamera.zoom;
    const bool ScopedSceneStatic = MHasLastFrame && ScopedSameKeys && ScopedSameItems && ScopedCameraSame;
    if (!ScopedSceneStatic)
    {
        ++MSceneVersion;
    }
    MLastCamera = InCamera;
    MHasLastCamera = true;

    // Mirror the gathered instances into the packed render ECS. Static frames
    // reuse the previous packed chunks; only order/content changes repack. The
    // mirror is a diagnostic view, so a viewport may switch it off and skip the
    // per-sprite pack and trigonometry.
    const bool ScopedMirrorDirty =
        MRenderEcsMirror && (!ScopedSameKeys || !ScopedSameItems || MRenderEcsEntities.size() != MCount);
    if (ScopedMirrorDirty)
    {
        if (MRenderEcsEntities.size() > MCount)
        {
            for (std::size_t ScopedRemove = MCount; ScopedRemove < MRenderEcsEntities.size(); ++ScopedRemove)
            {
                MRenderEcs.RemoveSprite(MRenderEcsEntities[ScopedRemove]);
            }
            MRenderEcsEntities.resize(MCount);
        }
        for (std::uint32_t ScopedRenderIndex = 0; ScopedRenderIndex < MCount; ++ScopedRenderIndex)
        {
            if (ScopedRenderIndex >= MRenderEcsEntities.size())
            {
                MRenderEcsEntities.push_back(MRenderEcs.AddSprite(MItems[ScopedRenderIndex]));
            }
            else
            {
                (void)MRenderEcs.UpdateSprite(MRenderEcsEntities[ScopedRenderIndex], MItems[ScopedRenderIndex]);
            }
        }
    }
    if (MRenderEcsMirror)
    {
        MStats.RenderEcsSprites = MRenderEcs.ActiveCount();
        MStats.RenderEcsChunks = static_cast<std::uint32_t>(MRenderEcs.ChunkCount());
    }

    MLastCount = MCount;
    MHasLastFrame = true;

    MLights.clear();
    MLights.reserve(256u);
    const auto ScopedScreenX = [&InCamera, ScopedZoom](float InX)
    {
        return (InX - InCamera.center.X) * ScopedZoom + InCamera.viewport.X * 0.5f;
    };
    const auto ScopedScreenY = [&InCamera, ScopedZoom](float InY)
    {
        return InCamera.viewport.Y * 0.5f - (InY - InCamera.center.Y) * ScopedZoom;
    };
    const auto ScopedPushLight = [this](const Transform2D& InTransform, Color32 InColor, float InRadius,
                                        float InIntensity, std::uint32_t InType, float InOuterCone, float InInnerCone)
    {
        if (MLights.size() >= 1024u)
        {
            return;
        }
        Rhi::GpuLight ScopedLight{};
        ScopedLight.PositionAndRadius =
            Vector4D{InTransform.Position.X, InTransform.Position.Y, InRadius, static_cast<float>(InType)};
        ScopedLight.ColorAndIntensity =
            Vector4D{static_cast<float>(InColor.R()) / 255.0f, static_cast<float>(InColor.G()) / 255.0f,
                     static_cast<float>(InColor.B()) / 255.0f, InIntensity};
        ScopedLight.DirectionAndCone =
            Vector4D{std::cos(InTransform.Rotation), -std::sin(InTransform.Rotation), InOuterCone, InInnerCone};
        ScopedLight.Type = InType;
        MLights.push_back(ScopedLight);
    };
    auto ScopedLightView = ScopedRegistry.view<Transform2D, Light2DComponent>();
    for (auto [ScopedEntity, ScopedTransform, ScopedComponent] : ScopedLightView.each())
    {
        (void)ScopedEntity;
        if ((ScopedComponent.Flags & kLight2DFlagEnabled) == 0u || ScopedComponent.Radius <= 0.0f)
        {
            continue;
        }
        ScopedPushLight(ScopedTransform, ScopedComponent.Color, ScopedComponent.Radius, ScopedComponent.Intensity, 1u,
                        -1.0f, -1.0f);
    }
    auto ScopedSourceView = ScopedRegistry.view<Transform2D, LightSourceComponent>();
    for (auto [ScopedEntity, ScopedTransform, ScopedComponent] : ScopedSourceView.each())
    {
        (void)ScopedEntity;
        if ((ScopedComponent.Flags & LightSourceEnabled) == 0u || ScopedComponent.Radius <= 0.0f)
        {
            continue;
        }
        const float ScopedHalfCone = ScopedComponent.ConeAngle * 0.5f;
        const float ScopedOuter = ScopedHalfCone <= 0.0f ? -1.0f : std::cos(ScopedHalfCone);
        const float ScopedInner =
            ScopedHalfCone <= 0.0f ? -1.0f : std::cos(std::max(ScopedHalfCone - ScopedComponent.ConeSoftness, 0.0f));
        ScopedPushLight(ScopedTransform, ScopedComponent.Color, ScopedComponent.Radius, ScopedComponent.Intensity,
                        ScopedComponent.Type, ScopedOuter, ScopedInner);
    }
    // The GPU consumes screen-space lights; convert once here so the cluster
    // passes never need the camera.
    for (Rhi::GpuLight& ScopedLight : MLights)
    {
        ScopedLight.PositionAndRadius.X = ScopedScreenX(ScopedLight.PositionAndRadius.X);
        ScopedLight.PositionAndRadius.Y = ScopedScreenY(ScopedLight.PositionAndRadius.Y);
        ScopedLight.PositionAndRadius.Z *= ScopedZoom;
    }
    MStats.PackedLights = static_cast<std::uint32_t>(MLights.size());
    const bool ScopedLightsChanged = MLights.size() != MLastLights.size() ||
                                     (!MLights.empty() && std::memcmp(MLights.data(), MLastLights.data(),
                                                                      MLights.size() * sizeof(Rhi::GpuLight)) != 0);
    if (ScopedLightsChanged)
    {
        ++MLightVersion;
        MLastLights = MLights;
    }

    MPacket = Rhi::FramePacket{};
    MPacket.opaqueFrontToBack = MOpaqueFrontToBack;
    if (MInstanceDeltasEnabled)
    {
        const bool ScopedSameOrder =
            MHasLastSorted && MLastSorted.size() == MCount && MLastSortedKeys.size() == MCount &&
            (MCount == 0u || std::equal(MSortedKeys.begin(), MSortedKeys.begin() + MCount, MLastSortedKeys.begin()));
        if (ScopedSameOrder)
        {
            MDirtyRanges.clear();
            std::uint32_t ScopedRunFirst = 0;
            std::uint32_t ScopedRunCount = 0;
            for (std::uint32_t ScopedI = 0; ScopedI < MCount; ++ScopedI)
            {
                const bool ScopedChanged =
                    std::memcmp(&MSorted[ScopedI], &MLastSorted[ScopedI], sizeof(Rhi::RenderItem)) != 0;
                if (ScopedChanged)
                {
                    if (ScopedRunCount == 0)
                    {
                        ScopedRunFirst = ScopedI;
                    }
                    ++ScopedRunCount;
                }
                else if (ScopedRunCount != 0)
                {
                    MDirtyRanges.push_back(Rhi::DirtyRange{ScopedRunFirst, ScopedRunCount});
                    ScopedRunCount = 0;
                }
            }
            if (ScopedRunCount != 0)
            {
                MDirtyRanges.push_back(Rhi::DirtyRange{ScopedRunFirst, ScopedRunCount});
            }
            MPacket.instanceRanges = std::span<const Rhi::DirtyRange>(MDirtyRanges.data(), MDirtyRanges.size());
            MPacket.instanceRangesAreDeltas = true;
        }
        MLastSorted.assign(MSorted.begin(), MSorted.begin() + MCount);
        MLastSortedKeys.assign(MSortedKeys.begin(), MSortedKeys.begin() + MCount);
        MHasLastSorted = true;
    }
    MPacket.items = std::span<const Rhi::RenderItem>(MSorted.data(), MCount);
    MPacket.motionItems = std::span<const Rhi::MotionVectorItem>(MMotionItems.data(), MMotionItems.size());
    MPacket.keys = std::span<const Rhi::RenderKey>(MSortedKeys.data(), MCount);
    MPacket.batches = std::span<const Rhi::DrawBatch>(MBatches.data(), MBatches.size());
    MPacket.depthBatches = std::span<const Rhi::DrawBatch>(MDepthBatches.data(), MDepthBatches.size());
    MPacket.gpuLighting = MGpuLighting;
    MPacket.oitMode = MOitMode;
    std::uint32_t ScopedCulledBatches = 0;
    for (const Rhi::DrawBatch& ScopedBatch : MBatches)
    {
        bool ScopedAllOpaque = ScopedBatch.itemCount > 0;
        for (std::uint32_t ScopedItemIndex = ScopedBatch.firstItem;
             ScopedAllOpaque && ScopedItemIndex < ScopedBatch.firstItem + ScopedBatch.itemCount; ++ScopedItemIndex)
        {
            ScopedAllOpaque = (MSorted[ScopedItemIndex].flags & Rhi::DepthWriteMask) != 0u;
        }
        if (!ScopedAllOpaque)
        {
            break;
        }
        ++ScopedCulledBatches;
    }
    MPacket.gpuCulledBatchCount = MBatches.size() <= 64u ? ScopedCulledBatches : 0u;
    MPacket.gpuOpaqueMain = MPacket.gpuCulledBatchCount > 0u;
    MPacket.sceneStatic = ScopedSceneStatic && !ScopedLightsChanged;
    MPacket.sceneId = MSceneId;
    MPacket.sceneVersion = MSceneVersion;
    MPacket.lightVersion = MLightVersion;
    if (MSdfGiVolume != nullptr && MSdfGiVolume->PackGpu(MGiProbes))
    {
        MGiHeader.OriginAndCell = Vector4D{MSdfGiVolume->Origin().X, MSdfGiVolume->Origin().Y,
                                           MSdfGiVolume->CellSize().X, MSdfGiVolume->CellSize().Y};
        MGiHeader.Extent =
            Vector4D{static_cast<float>(MSdfGiVolume->Width()), static_cast<float>(MSdfGiVolume->Height()),
                     MSdfGiVolume->MaxDistance(), std::clamp(MSdfGiStrength, 0.0f, 1.0f)};
        MPacket.giProbes = std::span<const Rhi::GpuGiProbe>(MGiProbes.data(), MGiProbes.size());
        MPacket.giHeader = MGiHeader;
    }
    else
    {
        MGiProbes.clear();
        MPacket.giProbes = {};
        MPacket.giHeader = {};
    }
    const bool ScopedGiChanged =
        MGiProbes.size() != MLastGiProbes.size() || std::memcmp(&MGiHeader, &MLastGiHeader, sizeof(MGiHeader)) != 0 ||
        (!MGiProbes.empty() &&
         std::memcmp(MGiProbes.data(), MLastGiProbes.data(), MGiProbes.size() * sizeof(Rhi::GpuGiProbe)) != 0);
    if (ScopedGiChanged)
    {
        ++MGiVersion;
        MLastGiProbes = MGiProbes;
        MLastGiHeader = MGiHeader;
    }
    if (ScopedGiChanged)
    {
        MPacket.sceneStatic = false;
    }
    MPacket.giVersion = MGiVersion;
    Rhi::CameraData ScopedPacketCamera = InCamera;
    MPacket.historyValid = MTemporalEnabled && ScopedPreviousCameraValid && !ScopedCameraJump;
    MPacket.historyGeneration = MHistoryGeneration;
    MPacket.jitterX = 0.0f;
    MPacket.jitterY = 0.0f;
    MPacket.previousJitterX = ScopedPreviousCameraValid ? MPreviousJitterX : 0.0f;
    MPacket.previousJitterY = ScopedPreviousCameraValid ? MPreviousJitterY : 0.0f;
    if (MTemporalEnabled)
    {
        ++MFrameIndex;
        const float ScopedJitterX = (MFrameIndex % 2u == 0u ? 0.25f : -0.25f);
        const float ScopedJitterY = (MFrameIndex % 3u == 0u ? -0.125f : 0.125f);
        MPacket.jitterX = ScopedJitterX;
        MPacket.jitterY = ScopedJitterY;
        ScopedPacketCamera.center.X += ScopedJitterX / std::max(InCamera.zoom, 0.05f);
        ScopedPacketCamera.center.Y += ScopedJitterY / std::max(InCamera.zoom, 0.05f);
    }
    MPacket.camera = ScopedPacketCamera;
    MPacket.previousCamera = ScopedPreviousCameraValid ? ScopedPreviousCamera : ScopedPacketCamera;
    MPacket.clear = InClear;
    MPacket.textureArray = InTextureArray;
    MPacket.lights = std::span<const Rhi::GpuLight>(MLights.data(), MLights.size());
    MPacket.meshItems = std::span<const Rhi::MeshRenderItem>(MMeshItems.data(), ScopedMeshIndex);
    MPacket.boneMatrices = std::span<const Mat4>(MMeshPalette.data(), ScopedMeshIndex * kMaxBones);
    MPacket.postFx = InPostFx;
    MPacket.postFxEnabled = InPostFx.flags != Rhi::PostFxNone;

    if (!ScopedOutputsReusable)
    {
        MOitItems.clear();
        MOitKeys.clear();
        MOitBatches.clear();
        for (std::uint32_t ScopedI = 0; ScopedI < MCount; ++ScopedI)
        {
            if ((MSorted[ScopedI].flags & Rhi::DepthWriteMask) == 0u)
            {
                MOitItems.push_back(MSorted[ScopedI]);
                MOitKeys.push_back(MSortedKeys[ScopedI]);
            }
        }
        if (!MOitItems.empty())
        {
            MOitBatches.push_back(Rhi::DrawBatch{0, static_cast<std::uint32_t>(MOitItems.size()), 0, 0});
        }
    }
    MOitPacket = Rhi::FramePacket{};
    MOitPacket.items = std::span<const Rhi::RenderItem>(MOitItems.data(), MOitItems.size());
    MOitPacket.keys = std::span<const Rhi::RenderKey>(MOitKeys.data(), MOitKeys.size());
    MOitPacket.batches = std::span<const Rhi::DrawBatch>(MOitBatches.data(), MOitBatches.size());
    MOitPacket.camera = InCamera;
    MOitPacket.clear = InClear;
    MOitPacket.textureArray = InTextureArray;
    MOitPacket.oitMode = MOitMode;
    MOitPacket.gpuLighting = MGpuLighting;
    MOitPacket.lights = std::span<const Rhi::GpuLight>(MLights.data(), MLights.size());

    if (MOitMode != 0u)
    {
        if (!ScopedOutputsReusable)
        {
            MOpaqueItems.clear();
            MOpaqueKeys.clear();
            MOpaqueBatches.clear();
            for (std::uint32_t ScopedI = 0; ScopedI < MCount; ++ScopedI)
            {
                if ((MSorted[ScopedI].flags & Rhi::DepthWriteMask) != 0u)
                {
                    MOpaqueItems.push_back(MSorted[ScopedI]);
                    MOpaqueKeys.push_back(MSortedKeys[ScopedI]);
                }
            }
            if (!MOpaqueItems.empty())
            {
                MOpaqueBatches.push_back(Rhi::DrawBatch{0, static_cast<std::uint32_t>(MOpaqueItems.size()), 0, 0});
            }
        }
        MPacket.items = std::span<const Rhi::RenderItem>(MOpaqueItems.data(), MOpaqueItems.size());
        MPacket.keys = std::span<const Rhi::RenderKey>(MOpaqueKeys.data(), MOpaqueKeys.size());
        MPacket.batches = std::span<const Rhi::DrawBatch>(MOpaqueBatches.data(), MOpaqueBatches.size());
        MPacket.gpuCulledBatchCount = 0;
        MPacket.gpuOpaqueMain = false;
    }
    MHasLastCamera = true;
    MLastCamera = InCamera;
    MLastSceneRevision = ScopedRegistry.Data().Revision();
    MLastTextureArray = InTextureArray;
    MLastViewportMode = MViewportPresenter.Mode();
    MPreviousJitterX = MPacket.jitterX;
    MPreviousJitterY = MPacket.jitterY;
}

void FrameComposer::Reserve(std::uint32_t InMaxItems)
{
    const std::size_t ScopedItems = InMaxItems;
    MItems.reserve(ScopedItems);
    MKeys.reserve(ScopedItems);
    MOrder.reserve(ScopedItems);
    MSorted.reserve(ScopedItems);
    MSortedKeys.reserve(ScopedItems);
    MLastItems.reserve(ScopedItems);
    MLastKeys.reserve(ScopedItems);
    MOitItems.reserve(ScopedItems);
    MOitKeys.reserve(ScopedItems);
    MOpaqueItems.reserve(ScopedItems);
    MOpaqueKeys.reserve(ScopedItems);
    MMotionItems.reserve(ScopedItems);
    MRenderEcsEntities.reserve(ScopedItems);
    MBatches.reserve(std::min<std::size_t>(ScopedItems, 4096u));
}
}
