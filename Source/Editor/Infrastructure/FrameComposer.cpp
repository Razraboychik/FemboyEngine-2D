// FemboyEngine2D - frame packet composition implementation.
#include "Editor/Infrastructure/FrameComposer.hpp"

#include "Engine/ECS/Components.hpp"
#include "Engine/ECS/Lighting.hpp"
#include "Engine/ECS/Presentation.hpp"
#include "Engine/ECS/Skeleton.hpp"
#include "Engine/Physics/SdfCollision.hpp"
#include "Engine/RHI/SkinningBridge.hpp"

#include <algorithm>

namespace Fby::Editor
{

void FrameComposer::BuildFramePacket(Ecs::Registry& InRegistry, const Rhi::CameraData& InCamera, Color32 InClear,
                                     std::uint32_t InTextureArray,
                                     std::span<const Rhi::MeshRenderItem> InExtraMeshItems,
                                     std::span<const TileQuad> InTileQuads, const Rhi::PostFxSettings& InPostFx)
{
    MViewportPresenter.SetSdfField(MSdfField);
    MViewportPresenter.Prepare(InRegistry);
    const bool ScopedUnlit = Rhi::ViewportModeNeedsUnlit(MViewportPresenter.Mode());
    auto ScopedView = InRegistry.view<Transform2D, Sprite2D, DrawOrder>();
    auto ScopedSkinView = InRegistry.view<SkeletonComponent, SkinComponent, SkinnedSprite>();
    const std::size_t ScopedCount = ScopedView.size_hint() + InTileQuads.size();
    MItems.resize(ScopedCount);
    MKeys.resize(ScopedCount);
    MOrder.resize(ScopedCount);
    MSorted.resize(ScopedCount);
    MSortedKeys.resize(ScopedCount);

    // GPU-skinning path: bind vertices plus one final Mat4 palette per skinned
    // sprite. Both arrays are sized for the worst case, then trimmed by count.
    MMeshVertices.resize(ScopedSkinView.size_hint() * kMaxSkinVertices);
    MMeshPalette.resize(ScopedSkinView.size_hint() * kMaxBones);
    MMeshItems.resize(ScopedSkinView.size_hint() + InExtraMeshItems.size());
    std::size_t ScopedMeshIndex = 0;

    std::size_t ScopedIndex = 0;
    for (auto [ScopedEntity, ScopedTransform, ScopedSprite, ScopedOrder] : ScopedView.each())
    {
        if (!EntityAllowsRenderPass(InRegistry, ScopedEntity, RenderPassSprites) ||
            InRegistry.all_of<Hidden>(ScopedEntity))
        {
            continue;
        }
        // The lighting system writes its own result component; the extract
        // composes it with the authored sprite colour.
        std::uint32_t ScopedColor = ScopedSprite.Color.rgba;
        if (!ScopedUnlit)
        {
            if (const auto* ScopedLight = InRegistry.try_get<Light2DResultComponent>(ScopedEntity))
            {
                ScopedColor = LightingSystem::Compose(ScopedSprite.Color, ScopedLight->Color).rgba;
            }
        }
        std::uint32_t ScopedTint = ScopedColor;
        if (MViewportPresenter.TintForEntity(ScopedEntity, &ScopedTint))
        {
            ScopedColor = ScopedTint;
        }
        MItems[ScopedIndex] =
            Rhi::RenderItem{ScopedTransform.Position, ScopedSprite.Size * ScopedTransform.Scale,
                            ScopedSprite.UVMin,       ScopedSprite.UVMax,
                            ScopedSprite.Layer,       ScopedColor,
                            ScopedTransform.Rotation, ScopedSprite.Flags};
        MKeys[ScopedIndex] = Rhi::MakeRenderKey(ScopedOrder.Layer, ScopedOrder.Depth);
        MOrder[ScopedIndex] = static_cast<std::uint32_t>(ScopedIndex);
        ++ScopedIndex;
    }
    static constexpr std::uint16_t ScopedSkeletalQuadIndices[6] = {0, 1, 2, 0, 2, 3};
    for (auto [ScopedEntity, ScopedSkeleton, ScopedSkin, ScopedSprite] : ScopedSkinView.each())
    {
        if (ScopedSkin.VertexCount < 4 ||
            !EntityAllowsRenderPass(InRegistry, ScopedEntity, RenderPassSprites) ||
            InRegistry.all_of<Hidden>(ScopedEntity))
        {
            continue;
        }
        std::uint32_t ScopedSkinColor = ScopedSprite.Color.rgba;
        if (!ScopedUnlit)
        {
            if (const auto* ScopedLight = InRegistry.try_get<Light2DResultComponent>(ScopedEntity))
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
    // Tilemap quads: one item per visible tile, already keyed by the isometric
    // depth the tilemap system computed.
    for (const TileQuad& ScopedQuad : InTileQuads)
    {
        if (ScopedIndex >= MItems.size())
        {
            break;
        }
        MItems[ScopedIndex] = ScopedQuad.Item;
        MKeys[ScopedIndex] = ScopedQuad.Key;
        MOrder[ScopedIndex] = static_cast<std::uint32_t>(ScopedIndex);
        ++ScopedIndex;
    }
    MCount = static_cast<std::uint32_t>(ScopedIndex);

    MSorter.Sort(std::span<const Rhi::RenderKey>(MKeys.data(), MCount),
                 std::span<std::uint32_t>(MOrder.data(), MCount));
    for (std::uint32_t ScopedI = 0; ScopedI < MCount; ++ScopedI)
    {
        MSorted[ScopedI] = MItems[MOrder[ScopedI]];
        MSortedKeys[ScopedI] = MKeys[MOrder[ScopedI]];
    }

    MBatches.clear();
    std::uint32_t ScopedBegin = 0;
    for (std::uint32_t ScopedI = 0; ScopedI < MCount; ++ScopedI)
    {
        if (ScopedI > 0 && (MKeys[MOrder[ScopedI]] >> 48) != (MKeys[MOrder[ScopedI - 1]] >> 48))
        {
            MBatches.push_back(Rhi::DrawBatch{ScopedBegin, ScopedI - ScopedBegin, 0, 0});
            ScopedBegin = ScopedI;
        }
    }
    if (MCount > 0)
    {
        MBatches.push_back(Rhi::DrawBatch{ScopedBegin, MCount - ScopedBegin, 0, 0});
    }

    MPacket = Rhi::FramePacket{
        std::span<const Rhi::RenderItem>(MSorted.data(), MCount),
        std::span<const Rhi::RenderKey>(MSortedKeys.data(), MCount),
        std::span<const Rhi::DrawBatch>(MBatches.data(), MBatches.size()),
        InCamera,
        InClear,
        InTextureArray,
        std::span<const Rhi::MeshRenderItem>(MMeshItems.data(), ScopedMeshIndex),
        std::span<const Mat4>(MMeshPalette.data(), ScopedMeshIndex * kMaxBones),
    };
    MPacket.postFx = InPostFx;
    MPacket.postFxEnabled = InPostFx.flags != Rhi::PostFxNone;
}
}
