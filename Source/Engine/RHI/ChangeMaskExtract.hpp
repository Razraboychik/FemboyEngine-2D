// FemboyEngine2D - change-mask-driven render extract.
#pragma once

#include "Engine/Core/Definitions.hpp"
#include "Engine/ECS/ChangeMask.hpp"
#include "Engine/RHI/RenderItem.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

namespace Fby::Rhi
{

inline constexpr std::uint32_t kInvalidExtractSlot = 0xFFFFFFFFu;

struct ExtractStats
{
    std::uint32_t activeItems = 0;
    std::uint32_t recomputed = 0;
    std::uint32_t skippedStatic = 0;
    std::uint32_t dirtyRanges = 0;
    std::uint32_t dirtyBytes = 0;
    std::uint32_t structuralRebuilds = 0;
    std::uint32_t overflowed = 0;
};

// `Capacity` is the maximum number of render-visible entities in the world. The
template <std::uint32_t Capacity = 4096> class ChangeMaskRenderExtract
{
public:
    static_assert(Capacity > 0 && Capacity < 0x3FFFFFFFu, "Capacity must leave room for sentinel slots");

    ChangeMaskRenderExtract() noexcept
    {
        Reset();
    }

    ChangeMaskRenderExtract(const ChangeMaskRenderExtract&) = delete;
    ChangeMaskRenderExtract& operator=(const ChangeMaskRenderExtract&) = delete;

    // Drops every slot and rebuilds the free list. Does not touch the registry.
    void Reset() noexcept
    {
        for (std::uint32_t ScopedSlot = 0; ScopedSlot < Capacity; ++ScopedSlot)
        {
            MEntities[ScopedSlot] = entt::null;
            MItems[ScopedSlot] = RenderItem{};
            MKeys[ScopedSlot] = 0;
            MOutputIndex[ScopedSlot] = 0;
            MActive[ScopedSlot] = 0;
            MFreeNext[ScopedSlot] = ScopedSlot + 1u < Capacity ? ScopedSlot + 1u : kInvalidExtractSlot;
        }
        MFreeHead = 0;
        MUsedSlots = 0;
        MFrame = 0;
        MOutputValid = false;
        MOutputCount = 0;
        MBatchCount = 0;
        MDirtyRangeCount = 0;
        MChangedCount = 0;
        MLookupCount = 0;
        for (LookupEntry& ScopedEntry : MLookup)
        {
            ScopedEntry = LookupEntry{};
        }
        MStats = ExtractStats{};
    }

    // Runs one render-extract pass. The registry is read through a view of
    [[nodiscard]] bool Extract(Ecs::Registry& InRegistry) noexcept
    {
        ++MFrame;
        MStats = ExtractStats{};
        MChangedCount = 0;
        bool ScopedStructural = false;
        bool ScopedOk = true;

        // 1. Drop slots whose entity died or lost a render component.
        for (std::uint32_t ScopedSlot = 0; ScopedSlot < Capacity; ++ScopedSlot)
        {
            if (MActive[ScopedSlot] == 0)
            {
                continue;
            }
            const Entity ScopedEntity = MEntities[ScopedSlot];
            if (!InRegistry.valid(ScopedEntity) || InRegistry.try_get<Transform2D>(ScopedEntity) == nullptr ||
                InRegistry.try_get<Sprite2D>(ScopedEntity) == nullptr ||
                InRegistry.try_get<ComponentChangeMask>(ScopedEntity) == nullptr)
            {
                ReleaseSlot(ScopedSlot);
                ScopedStructural = true;
            }
        }

        // 2. Walk masked entities. A static entity reaches the (mask == 0) branch
        auto ScopedView = InRegistry.view<ComponentChangeMask, Transform2D, Sprite2D>();
        for (auto [ScopedEntity, ScopedMask, ScopedTransform, ScopedSprite] : ScopedView.each())
        {
            (void)ScopedTransform;
            (void)ScopedSprite;
            const ChangeMask ScopedBits = Change::Peek(ScopedMask);
            std::uint32_t ScopedSlot = FindSlot(ScopedEntity);
            if (ScopedBits == 0 && ScopedSlot != kInvalidExtractSlot)
            {
                ++MStats.skippedStatic;
                continue;
            }
            // Consume before recomputing: a mutation that lands between the peek
            const ChangeMask ScopedConsumed = Change::Consume(ScopedMask);
            const bool ScopedNew = ScopedSlot == kInvalidExtractSlot;
            if (ScopedNew)
            {
                ScopedSlot = AcquireSlot(ScopedEntity);
                if (ScopedSlot == kInvalidExtractSlot)
                {
                    ++MStats.overflowed;
                    ScopedOk = false;
                    continue;
                }
                // First sight of an entity is always a full extraction, even if
                ScopedStructural = true;
            }
            if (!ScopedNew &&
                (ScopedConsumed & (ChangeMaskOf(ChangeMaskBit::Transform2D) | ChangeMaskOf(ChangeMaskBit::Sprite2D) |
                                   ChangeMaskOf(ChangeMaskBit::DrawOrder))) == 0)
            {
                // Audio/animation/physics bits were consumed; the RenderItem is
                ++MStats.skippedStatic;
                continue;
            }

            RenderItem ScopedItem{};
            RenderKey ScopedKey = 0;
            BuildItem(InRegistry, ScopedEntity, &ScopedItem, &ScopedKey);
            const RenderKey ScopedOldKey = MKeys[ScopedSlot];
            if (!ScopedNew && ScopedOldKey != ScopedKey)
            {
                ScopedStructural = true;
            }
            MItems[ScopedSlot] = ScopedItem;
            MKeys[ScopedSlot] = ScopedKey;
            ++MStats.recomputed;
            if (!ScopedStructural && MOutputValid)
            {
                MChangedScratch[MChangedCount++] = MOutputIndex[ScopedSlot];
            }
        }

        if (ScopedStructural || !MOutputValid)
        {
            RebuildOutput();
            ++MStats.structuralRebuilds;
            MDirtyRangeCount = MOutputCount != 0 ? 1u : 0u;
            if (MDirtyRangeCount != 0)
            {
                MDirtyRanges[0] = DirtyRange{0, MOutputCount};
            }
        }
        else
        {
            // In-place patch: the sort order did not move, so only the changed
            for (std::uint32_t ScopedIndex = 0; ScopedIndex < MChangedCount; ++ScopedIndex)
            {
                const std::uint32_t ScopedOutput = MChangedScratch[ScopedIndex];
                const std::uint32_t ScopedSlot = MOutputOrder[ScopedOutput];
                MOutputItems[ScopedOutput] = MItems[ScopedSlot];
            }
            BuildDirtyRanges();
        }

        MStats.activeItems = MOutputCount;
        MStats.dirtyRanges = MDirtyRangeCount;
        std::uint32_t ScopedDirtyBytes = 0;
        for (std::uint32_t ScopedRange = 0; ScopedRange < MDirtyRangeCount; ++ScopedRange)
        {
            ScopedDirtyBytes += MDirtyRanges[ScopedRange].ByteCount();
        }
        MStats.dirtyBytes = ScopedDirtyBytes;
        return ScopedOk;
    }

    // Fills the existing RHI frame contract from the cached sorted arrays.
    void FillPacket(FramePacket& OutPacket, const CameraData& InCamera, Color32 InClear,
                    std::uint32_t InTextureArray = 0xFFFFFFFFu) const noexcept
    {
        OutPacket.items = std::span<const RenderItem>(MOutputItems.data(), MOutputCount);
        OutPacket.keys = std::span<const RenderKey>(MOutputKeys.data(), MOutputCount);
        OutPacket.batches = std::span<const DrawBatch>(MBatches.data(), MBatchCount);
        OutPacket.camera = InCamera;
        OutPacket.clear = InClear;
        OutPacket.textureArray = InTextureArray;
        OutPacket.meshItems = {};
        OutPacket.boneMatrices = {};
        OutPacket.instanceRanges = DirtyRanges();
        OutPacket.instanceRangesAreDeltas = MOutputValid;
    }

    // Bucket -> material pipeline id for sprite batches. The ids come from
    // MaterialPipelineCache; an empty table keeps every batch on the quad pipeline.
    void SetMaterialPipelines(std::span<const std::uint32_t> InPipelines) noexcept
    {
        MMaterialPipelines = InPipelines;
        // The cached batches carry pipeline ids, so a new table invalidates them.
        MOutputValid = false;
    }

    [[nodiscard]] std::span<const RenderItem> Items() const noexcept
    {
        return {MOutputItems.data(), MOutputCount};
    }
    [[nodiscard]] std::span<const RenderKey> Keys() const noexcept
    {
        return {MOutputKeys.data(), MOutputCount};
    }
    [[nodiscard]] std::span<const DrawBatch> Batches() const noexcept
    {
        return {MBatches.data(), MBatchCount};
    }
    [[nodiscard]] std::span<const DirtyRange> DirtyRanges() const noexcept
    {
        return {MDirtyRanges.data(), MDirtyRangeCount};
    }
    // Stable-slot view. A backend that owns a persistent SSBO can index this
    [[nodiscard]] std::span<const RenderItem> SlotItems() const noexcept
    {
        return {MItems.data(), Capacity};
    }
    [[nodiscard]] std::span<const std::uint32_t> OutputOrder() const noexcept
    {
        return {MOutputOrder.data(), MOutputCount};
    }
    [[nodiscard]] const ExtractStats& Stats() const noexcept
    {
        return MStats;
    }
    [[nodiscard]] std::uint32_t ActiveCount() const noexcept
    {
        return MOutputCount;
    }
    [[nodiscard]] std::uint32_t FrameNumber() const noexcept
    {
        return MFrame;
    }
    [[nodiscard]] bool OutputValid() const noexcept
    {
        return MOutputValid;
    }

private:
    struct LookupEntry
    {
        std::uint32_t key = 0xFFFFFFFFu;
        std::uint32_t slot = kInvalidExtractSlot;
    };

    static constexpr std::uint32_t kLookupCapacity = std::bit_ceil(Capacity * 2u);
    static constexpr std::uint32_t kLookupMask = kLookupCapacity - 1u;
    static constexpr std::uint32_t kEmptyKey = 0xFFFFFFFFu;
    static constexpr std::uint32_t kTombstoneKey = 0xFFFFFFFEu;

    [[nodiscard]] static std::uint32_t EntityKey(Entity InEntity) noexcept
    {
        return static_cast<std::uint32_t>(entt::to_integral(InEntity));
    }

    [[nodiscard]] static std::uint8_t MaterialBucketOf(RenderKey InKey) noexcept
    {
        return static_cast<std::uint8_t>((InKey >> kMaterialKeyShift) & 0xFFu);
    }

    [[nodiscard]] std::uint32_t PipelineForBucket(std::uint8_t InBucket) const noexcept
    {
        if (InBucket == 0 || static_cast<std::size_t>(InBucket) >= MMaterialPipelines.size())
        {
            return 0;
        }
        return MMaterialPipelines[InBucket];
    }

    [[nodiscard]] static std::uint32_t HashKey(std::uint32_t InKey) noexcept
    {
        InKey ^= InKey >> 16u;
        InKey *= 0x7FEB352Du;
        InKey ^= InKey >> 15u;
        InKey *= 0x846CA68Bu;
        InKey ^= InKey >> 16u;
        return InKey;
    }

    [[nodiscard]] std::uint32_t FindSlot(Entity InEntity) const noexcept
    {
        const std::uint32_t ScopedKey = EntityKey(InEntity);
        std::uint32_t ScopedIndex = HashKey(ScopedKey) & kLookupMask;
        for (std::uint32_t ScopedProbe = 0; ScopedProbe < kLookupCapacity; ++ScopedProbe)
        {
            const LookupEntry& ScopedEntry = MLookup[ScopedIndex];
            if (ScopedEntry.key == kEmptyKey)
            {
                return kInvalidExtractSlot;
            }
            if (ScopedEntry.key == ScopedKey)
            {
                return ScopedEntry.slot;
            }
            ScopedIndex = (ScopedIndex + 1u) & kLookupMask;
        }
        return kInvalidExtractSlot;
    }

    void InsertLookup(Entity InEntity, std::uint32_t InSlot) noexcept
    {
        const std::uint32_t ScopedKey = EntityKey(InEntity);
        std::uint32_t ScopedIndex = HashKey(ScopedKey) & kLookupMask;
        std::uint32_t ScopedTombstone = kInvalidExtractSlot;
        for (std::uint32_t ScopedProbe = 0; ScopedProbe < kLookupCapacity; ++ScopedProbe)
        {
            LookupEntry& ScopedEntry = MLookup[ScopedIndex];
            if (ScopedEntry.key == kEmptyKey)
            {
                LookupEntry& ScopedTarget =
                    ScopedTombstone != kInvalidExtractSlot ? MLookup[ScopedTombstone] : ScopedEntry;
                ScopedTarget.key = ScopedKey;
                ScopedTarget.slot = InSlot;
                ++MLookupCount;
                return;
            }
            if (ScopedEntry.key == kTombstoneKey && ScopedTombstone == kInvalidExtractSlot)
            {
                ScopedTombstone = ScopedIndex;
            }
            else if (ScopedEntry.key == ScopedKey)
            {
                ScopedEntry.slot = InSlot;
                return;
            }
            ScopedIndex = (ScopedIndex + 1u) & kLookupMask;
        }
    }

    void RemoveLookup(Entity InEntity) noexcept
    {
        const std::uint32_t ScopedKey = EntityKey(InEntity);
        std::uint32_t ScopedIndex = HashKey(ScopedKey) & kLookupMask;
        for (std::uint32_t ScopedProbe = 0; ScopedProbe < kLookupCapacity; ++ScopedProbe)
        {
            LookupEntry& ScopedEntry = MLookup[ScopedIndex];
            if (ScopedEntry.key == kEmptyKey)
            {
                return;
            }
            if (ScopedEntry.key == ScopedKey)
            {
                ScopedEntry.key = kTombstoneKey;
                ScopedEntry.slot = kInvalidExtractSlot;
                if (MLookupCount != 0)
                {
                    --MLookupCount;
                }
                return;
            }
            ScopedIndex = (ScopedIndex + 1u) & kLookupMask;
        }
    }

    [[nodiscard]] std::uint32_t AcquireSlot(Entity InEntity) noexcept
    {
        if (MFreeHead == kInvalidExtractSlot)
        {
            return kInvalidExtractSlot;
        }
        const std::uint32_t ScopedSlot = MFreeHead;
        MFreeHead = MFreeNext[ScopedSlot];
        MEntities[ScopedSlot] = InEntity;
        MActive[ScopedSlot] = 1;
        ++MUsedSlots;
        InsertLookup(InEntity, ScopedSlot);
        return ScopedSlot;
    }

    void ReleaseSlot(std::uint32_t InSlot) noexcept
    {
        if (InSlot >= Capacity || MActive[InSlot] == 0)
        {
            return;
        }
        RemoveLookup(MEntities[InSlot]);
        MActive[InSlot] = 0;
        MEntities[InSlot] = entt::null;
        MItems[InSlot] = RenderItem{};
        MKeys[InSlot] = 0;
        MOutputIndex[InSlot] = 0;
        MFreeNext[InSlot] = MFreeHead;
        MFreeHead = InSlot;
        --MUsedSlots;
    }

    static void BuildItem(Ecs::Registry& InRegistry, Entity InEntity, RenderItem* OutItem, RenderKey* OutKey) noexcept
    {
        const auto* ScopedTransform = InRegistry.try_get<Transform2D>(InEntity);
        const auto* ScopedSprite = InRegistry.try_get<Sprite2D>(InEntity);
        if (ScopedTransform == nullptr || ScopedSprite == nullptr)
        {
            *OutItem = RenderItem{};
            *OutKey = 0;
            return;
        }
        const auto* ScopedOrder = InRegistry.try_get<DrawOrder>(InEntity);
        const std::uint16_t ScopedLayer = ScopedOrder != nullptr ? ScopedOrder->Layer : 0u;
        const float ScopedDepth = ScopedOrder != nullptr ? ScopedOrder->Depth : 0.0f;

        RenderItem ScopedItem;
        ScopedItem.position = ScopedTransform->Position;
        ScopedItem.size = ScopedSprite->Size * ScopedTransform->Scale;
        ScopedItem.uvMin = ScopedSprite->UVMin;
        ScopedItem.uvMax = ScopedSprite->UVMax;
        ScopedItem.layer = ScopedSprite->Layer;
        ScopedItem.color = ScopedSprite->Color.rgba;
        ScopedItem.rotation = ScopedTransform->Rotation;
        ScopedItem.flags = ScopedSprite->Flags;
        *OutItem = ScopedItem;
        const std::uint32_t ScopedRequestedBucket = ScopedOrder != nullptr ? ScopedOrder->MaterialBucket : 0u;
        const std::uint8_t ScopedBucket =
            static_cast<std::uint8_t>(ScopedRequestedBucket < kMaxMaterialBuckets ? ScopedRequestedBucket : 0u);
        *OutKey = MakeMaterialRenderKey(ScopedBucket, ScopedLayer, ScopedDepth,
                                        static_cast<std::uint8_t>(EntityKey(InEntity) & 0xFFu));
    }

    void RebuildOutput() noexcept
    {
        MOutputCount = 0;
        for (std::uint32_t ScopedSlot = 0; ScopedSlot < Capacity; ++ScopedSlot)
        {
            if (MActive[ScopedSlot] != 0)
            {
                MOutputOrder[MOutputCount++] = ScopedSlot;
            }
        }
        std::sort(MOutputOrder.begin(), MOutputOrder.begin() + MOutputCount,
                  [this](std::uint32_t InLeft, std::uint32_t InRight) noexcept
                  {
                      if (MKeys[InLeft] != MKeys[InRight])
                      {
                          return MKeys[InLeft] < MKeys[InRight];
                      }
                      return EntityKey(MEntities[InLeft]) < EntityKey(MEntities[InRight]);
                  });
        MBatchCount = 0;
        std::uint32_t ScopedBatchBegin = 0;
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < MOutputCount; ++ScopedIndex)
        {
            const std::uint32_t ScopedSlot = MOutputOrder[ScopedIndex];
            MOutputItems[ScopedIndex] = MItems[ScopedSlot];
            MOutputKeys[ScopedIndex] = MKeys[ScopedSlot];
            MOutputIndex[ScopedSlot] = ScopedIndex;
            // The batch boundary is the material bucket and the sort layer: a
            // run inside one bucket shares one pipeline, so it can be one draw.
            if (ScopedIndex > 0 && (MOutputKeys[ScopedIndex] >> kMaterialKeyShift) !=
                                        (MOutputKeys[ScopedIndex - 1u] >> kMaterialKeyShift))
            {
                MBatches[MBatchCount++] =
                    DrawBatch{ScopedBatchBegin, ScopedIndex - ScopedBatchBegin,
                              PipelineForBucket(MaterialBucketOf(MOutputKeys[ScopedIndex - 1u])), 0};
                ScopedBatchBegin = ScopedIndex;
            }
        }
        if (MOutputCount > 0)
        {
            MBatches[MBatchCount++] =
                DrawBatch{ScopedBatchBegin, MOutputCount - ScopedBatchBegin,
                          PipelineForBucket(MaterialBucketOf(MOutputKeys[MOutputCount - 1u])), 0};
        }
        MOutputValid = true;
    }

    void BuildDirtyRanges() noexcept
    {
        MDirtyRangeCount = 0;
        if (MChangedCount == 0)
        {
            return;
        }
        std::sort(MChangedScratch.begin(), MChangedScratch.begin() + MChangedCount);
        std::uint32_t ScopedFirst = MChangedScratch[0];
        std::uint32_t ScopedLast = ScopedFirst;
        for (std::uint32_t ScopedIndex = 1; ScopedIndex < MChangedCount; ++ScopedIndex)
        {
            const std::uint32_t ScopedValue = MChangedScratch[ScopedIndex];
            if (ScopedValue == ScopedLast + 1u)
            {
                ScopedLast = ScopedValue;
                continue;
            }
            if (MDirtyRangeCount < kMaxDirtyRanges)
            {
                MDirtyRanges[MDirtyRangeCount++] = DirtyRange{ScopedFirst, ScopedLast - ScopedFirst + 1u};
            }
            ScopedFirst = ScopedValue;
            ScopedLast = ScopedValue;
        }
        if (MDirtyRangeCount < kMaxDirtyRanges)
        {
            MDirtyRanges[MDirtyRangeCount++] = DirtyRange{ScopedFirst, ScopedLast - ScopedFirst + 1u};
        }
    }

    static constexpr std::uint32_t kMaxDirtyRanges = Capacity;

    // Stable slots.
    std::array<Entity, Capacity> MEntities{};
    std::array<RenderItem, Capacity> MItems{};
    std::array<RenderKey, Capacity> MKeys{};
    std::array<std::uint32_t, Capacity> MOutputIndex{};
    std::array<std::uint8_t, Capacity> MActive{};
    std::array<std::uint32_t, Capacity> MFreeNext{};

    // Sorted output cache.
    std::array<RenderItem, Capacity> MOutputItems{};
    std::array<RenderKey, Capacity> MOutputKeys{};
    std::array<std::uint32_t, Capacity> MOutputOrder{};
    // Bucket -> material pipeline id. Bucket 0 is the engine quad pipeline, and
    // the table is owned by the caller and must outlive the extract.
    std::span<const std::uint32_t> MMaterialPipelines{};
    std::array<DrawBatch, Capacity> MBatches{};
    std::array<DirtyRange, Capacity> MDirtyRanges{};
    std::array<std::uint32_t, Capacity> MChangedScratch{};

    // Entity -> slot open-addressing table.
    std::array<LookupEntry, kLookupCapacity> MLookup{};

    std::uint32_t MFreeHead = 0;
    std::uint32_t MUsedSlots = 0;
    std::uint32_t MLookupCount = 0;
    std::uint32_t MFrame = 0;
    std::uint32_t MOutputCount = 0;
    std::uint32_t MBatchCount = 0;
    std::uint32_t MDirtyRangeCount = 0;
    std::uint32_t MChangedCount = 0;
    bool MOutputValid = false;
    ExtractStats MStats{};
};
}

// Local Variables:
