// FemboyEngine2D - FCS (Femboy Component System) chunked world.
//
// FCS is the internal archetype storage used by the 0.4 alpha line. Components
// of one archetype live in 16 KiB chunks; each component owns a contiguous
// column, so a system walks memory linearly instead of resolving a sparse index
// per entity. Structural changes move a row between archetypes and are expected
// to go through a command buffer at phase boundaries.
#pragma once

#include "Engine/ECS/FCS/AccessValidation.hpp"

#include <algorithm>
#include <array>
#include <bitset>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace Fby::Fcs
{

inline constexpr std::uint32_t kMaxComponents = 256;
inline constexpr std::uint32_t kChunkBytes = 16u * 1024u;
inline constexpr std::uint32_t kInvalidIndex = 0xFFFFFFFFu;
// Chunks are aligned so a hot column cannot share a cache line with the chunk
// header. Rows are capped independently of the byte budget: a one-byte tag
// would otherwise reserve 16384 entity handles per chunk. The cap only bites
// when a row is narrower than four bytes, so dense common archetypes keep
// their byte-sized capacity.
inline constexpr std::size_t kChunkAlignment = 64;
inline constexpr std::uint32_t kMaxRowsPerChunk = 4096;

// Rounds a column offset up to its component alignment. Alignments are powers
// of two, so the mask is the cheap form.
[[nodiscard]] constexpr std::uint32_t AlignUp(std::uint32_t InValue, std::uint32_t InAlignment) noexcept
{
    return (InValue + InAlignment - 1u) & ~(InAlignment - 1u);
}

// Stable entity handle. Generation invalidates handles to recycled indices.
struct Entity
{
    std::uint32_t Index = kInvalidIndex;
    std::uint32_t Generation = 0;

    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return Index != kInvalidIndex;
    }

    [[nodiscard]] friend constexpr bool operator==(Entity InLeft, Entity InRight) noexcept
    {
        return InLeft.Index == InRight.Index && InLeft.Generation == InRight.Generation;
    }

    [[nodiscard]] friend constexpr bool operator!=(Entity InLeft, Entity InRight) noexcept
    {
        return !(InLeft == InRight);
    }
};

inline constexpr Entity kNullEntity{};

// Wire handle used by the engine's entity fields: 20 index bits and 12
// generation bits, the same split the rest of the engine assumes. The index
// range covers 1M entities; the generation rejects a recycled slot for 4096
// cycles.
inline constexpr std::uint32_t kEntityIndexBits = 20;
inline constexpr std::uint32_t kEntityIndexMask = 0x000FFFFFu;
inline constexpr std::uint32_t kEntityGenerationShift = 20;
inline constexpr std::uint32_t kEntityGenerationMask = 0x00000FFFu;
inline constexpr std::uint32_t kPackedNullEntity = 0xFFFFFFFFu;

[[nodiscard]] constexpr std::uint32_t PackEntity(Entity InEntity) noexcept
{
    if (!InEntity.IsValid())
    {
        return kPackedNullEntity;
    }
    return ((InEntity.Generation & kEntityGenerationMask) << kEntityGenerationShift) |
           (InEntity.Index & kEntityIndexMask);
}

[[nodiscard]] constexpr Entity UnpackEntity(std::uint32_t InPacked) noexcept
{
    if (InPacked == kPackedNullEntity)
    {
        return kNullEntity;
    }
    return Entity{InPacked & kEntityIndexMask, (InPacked >> kEntityGenerationShift) & kEntityGenerationMask};
}

using ComponentId = std::uint16_t;
using Signature = std::bitset<kMaxComponents>;

namespace Detail
{

struct ComponentMeta
{
    std::uint32_t Size = 0;
    std::uint32_t Alignment = 1;
};

[[nodiscard]] inline std::array<ComponentMeta, kMaxComponents>& ComponentTable() noexcept
{
    static std::array<ComponentMeta, kMaxComponents> ScopedTable{};
    return ScopedTable;
}

[[nodiscard]] inline std::uint32_t& NextComponentId() noexcept
{
    static std::uint32_t ScopedNext = 0;
    return ScopedNext;
}

// One id per component type. The id is process-local; scene and snapshot code
// map it to the stable Mirror name separately.
template <typename Component> [[nodiscard]] ComponentId ComponentIdOf() noexcept
{
    static_assert(alignof(Component) <= kChunkAlignment,
                  "a component alignment above the chunk alignment cannot be honored");
    static const ComponentId ScopedId = []
    {
        const std::uint32_t ScopedNext = NextComponentId();
        // The id is also the bit index in Signature and in the scheduler access
        // masks. Running past the table is an out-of-bounds write, so this is a
        // hard failure in every build rather than a debug-only assert.
        if (ScopedNext >= kMaxComponents)
        {
            Fby::FailAssert("Fby::Fcs::ComponentIdOf", __FILE__, __LINE__,
                            "component type limit exceeded (kMaxComponents)");
        }
        ComponentMeta& ScopedMeta = ComponentTable()[ScopedNext];
        // A tag has no state; keeping it at zero bytes means a tag-only
        // archetype needs no component storage at all. The signature still
        // distinguishes the archetype. The returned reference is never read or
        // written, which keeps the zero-allocation contract of tag-only worlds.
        ScopedMeta.Size = std::is_empty_v<Component> ? 0u : static_cast<std::uint32_t>(sizeof(Component));
        ScopedMeta.Alignment = std::max<std::uint32_t>(1u, static_cast<std::uint32_t>(alignof(Component)));
        NextComponentId() = ScopedNext + 1u;
        return static_cast<ComponentId>(ScopedNext);
    }();
    return ScopedId;
}

}

// Owns the bytes of one chunk. The allocation is cache-line aligned, so the
// first column starts on a line boundary and no column shares a line with the
// chunk bookkeeping.
class ChunkStorage
{
public:
    ChunkStorage() = default;
    ~ChunkStorage()
    {
        Release();
    }

    ChunkStorage(const ChunkStorage&) = delete;
    ChunkStorage& operator=(const ChunkStorage&) = delete;

    ChunkStorage(ChunkStorage&& InOther) noexcept : MPointer(InOther.MPointer), MSize(InOther.MSize)
    {
        InOther.MPointer = nullptr;
        InOther.MSize = 0;
    }

    ChunkStorage& operator=(ChunkStorage&& InOther) noexcept
    {
        if (this != &InOther)
        {
            Release();
            MPointer = InOther.MPointer;
            MSize = InOther.MSize;
            InOther.MPointer = nullptr;
            InOther.MSize = 0;
        }
        return *this;
    }

    void Allocate(std::size_t InBytes)
    {
        Release();
        if (InBytes == 0)
        {
            return;
        }
        MPointer = static_cast<std::byte*>(::operator new(InBytes, std::align_val_t(kChunkAlignment)));
        MSize = InBytes;
    }

    // A tag-only archetype allocates no bytes, but every component accessor
    // still has to return a valid address. The shared aligned byte below is
    // that address; a component with a zero size never reads or writes it.
    [[nodiscard]] std::byte* Data() noexcept
    {
        return MPointer != nullptr ? MPointer : TagByte();
    }
    [[nodiscard]] const std::byte* Data() const noexcept
    {
        return MPointer != nullptr ? MPointer : TagByte();
    }
    [[nodiscard]] std::size_t Size() const noexcept
    {
        return MSize;
    }

private:
    [[nodiscard]] static std::byte* TagByte() noexcept
    {
        alignas(kChunkAlignment) static std::byte ScopedTagByte{};
        return &ScopedTagByte;
    }

    void Release() noexcept
    {
        if (MPointer != nullptr)
        {
            ::operator delete(MPointer, std::align_val_t(kChunkAlignment));
        }
        MPointer = nullptr;
        MSize = 0;
    }

    std::byte* MPointer = nullptr;
    std::size_t MSize = 0;
};

// Allocated bytes of one world, split by what a memory report needs to explain:
// fixed chunk capacity versus live entities. EntityBytes counts the handle
// array inside every chunk, which dominates for one-byte tag columns.
struct WorldMemoryStats
{
    std::size_t Archetypes = 0;
    std::size_t Chunks = 0;
    std::size_t LiveEntities = 0;
    std::size_t CapacityRows = 0;
    std::size_t ComponentBytes = 0;
    std::size_t EntityBytes = 0;

    [[nodiscard]] std::size_t TotalBytes() const noexcept
    {
        return ComponentBytes + EntityBytes;
    }
};

// One archetype owns a set of columns and a list of chunks. All chunks share
// the same capacity and column offsets, so a row index is enough to address
// every component of an entity.
namespace Detail
{
// The debug counterpart of a borrow rule: while a world iteration is active,
// structural changes invalidate the archetype walk. The guard keeps the depth
// in the world and lets the mutators assert against it.
class IterationGuard
{
public:
    explicit IterationGuard(std::uint32_t& InDepth) noexcept : MDepth(InDepth)
    {
        ++MDepth;
    }
    ~IterationGuard()
    {
        --MDepth;
    }

    IterationGuard(const IterationGuard&) = delete;
    IterationGuard& operator=(const IterationGuard&) = delete;

private:
    std::uint32_t& MDepth;
};
}

class World
{
public:
    struct Chunk
    {
        ChunkStorage Storage;
        std::vector<Entity> Entities;
        std::uint32_t Count = 0;
    };

    struct Archetype
    {
        Signature Type;
        // Sum of the component sizes in one row. It drives the row capacity,
        // while StorageBytes is the real aligned column footprint.
        std::uint32_t RowBytes = 0;
        std::uint32_t StorageBytes = 0;
        std::uint32_t Capacity = 0;
        std::array<std::uint32_t, kMaxComponents> Offsets{};
        std::array<std::uint32_t, kMaxComponents> Sizes{};
        std::vector<Chunk> Chunks;
    };

    World() = default;

    World(const World&) = delete;
    World& operator=(const World&) = delete;

    [[nodiscard]] Entity Create()
    {
        ++MRevision;
        assert(MIterationDepth == 0 && "FCS: structural change during iteration; queue it through CommandBuffer");
        std::uint32_t ScopedIndex = 0;
        if (!MFreeIndices.empty())
        {
            ScopedIndex = MFreeIndices.back();
            MFreeIndices.pop_back();
        }
        else
        {
            if (MRecords.size() >= (std::size_t{1} << kEntityIndexBits))
            {
                return kNullEntity;
            }
            ScopedIndex = static_cast<std::uint32_t>(MRecords.size());
            MRecords.push_back(EntityRecord{});
        }
        EntityRecord& ScopedRecord = MRecords[ScopedIndex];
        ScopedRecord.Generation += 1u;
        const std::uint32_t ScopedEmptyArchetype = GetOrCreateArchetype(Signature{});
        const auto [ScopedChunk, ScopedRow] = AllocateRow(ScopedEmptyArchetype);
        ScopedRecord.Archetype = ScopedEmptyArchetype;
        ScopedRecord.Chunk = ScopedChunk;
        ScopedRecord.Row = ScopedRow;
        MArchetypes[ScopedEmptyArchetype].Chunks[ScopedChunk].Entities[ScopedRow] =
            Entity{ScopedIndex, ScopedRecord.Generation};
        ++MLiveCount;
        return Entity{ScopedIndex, ScopedRecord.Generation};
    }

    // Recreates an exact handle. Snapshot restore and the command log use this
    // path to preserve the recorded entity identity.
    Entity CreateAt(Entity InEntity)
    {
        ++MRevision;
        assert(MIterationDepth == 0 && "FCS: structural change during iteration; queue it through CommandBuffer");
        if (!InEntity.IsValid() || InEntity.Index >= (std::uint32_t{1} << kEntityIndexBits))
        {
            return kNullEntity;
        }
        if (InEntity.Index >= MRecords.size())
        {
            MRecords.resize(InEntity.Index + 1u);
        }
        EntityRecord& ScopedRecord = MRecords[InEntity.Index];
        if (ScopedRecord.Archetype != kInvalidIndex)
        {
            // The slot is already live. Returning its current generation would
            // silently hand the caller a different entity, so a generation
            // mismatch is a failed restore rather than a replacement.
            return ScopedRecord.Generation == InEntity.Generation ? InEntity : kNullEntity;
        }
        const auto ScopedFree = std::find(MFreeIndices.begin(), MFreeIndices.end(), InEntity.Index);
        if (ScopedFree != MFreeIndices.end())
        {
            *ScopedFree = MFreeIndices.back();
            MFreeIndices.pop_back();
        }
        const std::uint32_t ScopedEmptyArchetype = GetOrCreateArchetype(Signature{});
        const auto [ScopedChunk, ScopedRow] = AllocateRow(ScopedEmptyArchetype);
        ScopedRecord.Generation = InEntity.Generation;
        ScopedRecord.Archetype = ScopedEmptyArchetype;
        ScopedRecord.Chunk = ScopedChunk;
        ScopedRecord.Row = ScopedRow;
        MArchetypes[ScopedEmptyArchetype].Chunks[ScopedChunk].Entities[ScopedRow] = InEntity;
        ++MLiveCount;
        return InEntity;
    }

    void Destroy(Entity InEntity)
    {
        ++MRevision;
        assert(MIterationDepth == 0 && "FCS: structural change during iteration; queue it through CommandBuffer");
        if (!IsAlive(InEntity))
        {
            return;
        }
        EntityRecord& ScopedRecord = MRecords[InEntity.Index];
        RemoveRow(ScopedRecord.Archetype, ScopedRecord.Chunk, ScopedRecord.Row);
        ScopedRecord.Archetype = kInvalidIndex;
        ScopedRecord.Chunk = 0;
        ScopedRecord.Row = 0;
        MFreeIndices.push_back(InEntity.Index);
        --MLiveCount;
    }

    [[nodiscard]] bool IsAlive(Entity InEntity) const noexcept
    {
        if (!InEntity.IsValid() || InEntity.Index >= MRecords.size())
        {
            return false;
        }
        const EntityRecord& ScopedRecord = MRecords[InEntity.Index];
        return ScopedRecord.Generation == InEntity.Generation && ScopedRecord.Archetype != kInvalidIndex;
    }

    template <typename Component, typename... Args> Component& Emplace(Entity InEntity, Args&&... InArgs)
    {
        ++MRevision;
        assert(MIterationDepth == 0 && "FCS: structural change during iteration; queue it through CommandBuffer");
        static_assert(std::is_trivially_copyable_v<Component>, "FCS components must be trivially copyable");
        static_assert(std::is_standard_layout_v<Component>, "FCS components must be standard layout");
        static_assert(alignof(Component) <= kChunkAlignment, "FCS component alignment exceeds the chunk alignment");
        assert(IsAlive(InEntity));

        const ComponentId ScopedId = Detail::ComponentIdOf<Component>();
        EntityRecord& ScopedRecord = MRecords[InEntity.Index];
        const Signature ScopedOldType = MArchetypes[ScopedRecord.Archetype].Type;
        if (ScopedOldType.test(ScopedId))
        {
            Component& ScopedExisting =
                *ComponentPtr<Component>(ScopedRecord.Archetype, ScopedRecord.Chunk, ScopedRecord.Row);
            ScopedExisting = Component(std::forward<Args>(InArgs)...);
            return ScopedExisting;
        }

        Signature ScopedNewType = ScopedOldType;
        ScopedNewType.set(ScopedId);
        const std::uint32_t ScopedNewArchetype = GetOrCreateArchetype(ScopedNewType);
        Component ScopedValue(std::forward<Args>(InArgs)...);
        MoveEntity(InEntity, ScopedNewArchetype, ScopedId, &ScopedValue);
        return *ComponentPtr<Component>(ScopedNewArchetype, MRecords[InEntity.Index].Chunk,
                                        MRecords[InEntity.Index].Row);
    }

    template <typename Component> [[nodiscard]] bool Remove(Entity InEntity)
    {
        ++MRevision;
        assert(MIterationDepth == 0 && "FCS: structural change during iteration; queue it through CommandBuffer");
        if (!IsAlive(InEntity))
        {
            return false;
        }
        const ComponentId ScopedId = Detail::ComponentIdOf<Component>();
        EntityRecord& ScopedRecord = MRecords[InEntity.Index];
        const Signature ScopedOldType = MArchetypes[ScopedRecord.Archetype].Type;
        if (!ScopedOldType.test(ScopedId))
        {
            return false;
        }
        Signature ScopedNewType = ScopedOldType;
        ScopedNewType.reset(ScopedId);
        const std::uint32_t ScopedNewArchetype = GetOrCreateArchetype(ScopedNewType);
        MoveEntity(InEntity, ScopedNewArchetype, kMaxComponents, nullptr);
        return true;
    }

    template <typename Component> [[nodiscard]] Component* TryGet(Entity InEntity) noexcept
    {
        if (!IsAlive(InEntity))
        {
            return nullptr;
        }
        const ComponentId ScopedId = Detail::ComponentIdOf<Component>();
        const EntityRecord& ScopedRecord = MRecords[InEntity.Index];
        if (!MArchetypes[ScopedRecord.Archetype].Type.test(ScopedId))
        {
            return nullptr;
        }
        return ComponentPtr<Component>(ScopedRecord.Archetype, ScopedRecord.Chunk, ScopedRecord.Row);
    }

    template <typename Component> [[nodiscard]] const Component* TryGet(Entity InEntity) const noexcept
    {
        return const_cast<World*>(this)->TryGet<Component>(InEntity);
    }

    template <typename Component> [[nodiscard]] bool Has(Entity InEntity) const noexcept
    {
        return TryGet<Component>(InEntity) != nullptr;
    }

    // Iterates every entity that owns every component in the pack. The callback
    // receives (Entity, Ts&...). Entities are visited in archetype order, then
    // chunk order, then row order: deterministic and cache-friendly.
    template <typename... Components, typename Function> void ForEach(Function&& InFunction)
    {
        const Detail::IterationGuard ScopedIteration(MIterationDepth);
        Signature ScopedMask;
        (ScopedMask.set(Detail::ComponentIdOf<Components>()), ...);
        (Detail::AccessValidation::Write(Detail::ComponentIdOf<Components>()), ...);
        ++MModificationRevision;
        for (Archetype& ScopedArchetype : MArchetypes)
        {
            if ((ScopedArchetype.Type & ScopedMask) != ScopedMask)
            {
                continue;
            }
            const std::array<std::uint32_t, sizeof...(Components)> ScopedStrides{
                ScopedArchetype.Sizes[Detail::ComponentIdOf<Components>()]...};
            for (Chunk& ScopedChunk : ScopedArchetype.Chunks)
            {
                const std::array<std::byte*, sizeof...(Components)> ScopedBases{
                    ScopedChunk.Storage.Data() + ScopedArchetype.Offsets[Detail::ComponentIdOf<Components>()]...};
                for (std::uint32_t ScopedRow = 0; ScopedRow < ScopedChunk.Count; ++ScopedRow)
                {
                    VisitRow<Components...>(InFunction, ScopedChunk.Entities[ScopedRow], ScopedBases, ScopedStrides,
                                            ScopedRow, std::index_sequence_for<Components...>{});
                }
            }
        }
    }

    // Read-only counterpart. The callback receives (Entity, const Ts&...).
    template <typename... Components, typename Function> void ForEach(Function&& InFunction) const
    {
        const Detail::IterationGuard ScopedIteration(const_cast<std::uint32_t&>(MIterationDepth));
        Signature ScopedMask;
        (ScopedMask.set(Detail::ComponentIdOf<Components>()), ...);
        (Detail::AccessValidation::Read(Detail::ComponentIdOf<Components>()), ...);
        for (const Archetype& ScopedArchetype : MArchetypes)
        {
            if ((ScopedArchetype.Type & ScopedMask) != ScopedMask)
            {
                continue;
            }
            const std::array<std::uint32_t, sizeof...(Components)> ScopedStrides{
                ScopedArchetype.Sizes[Detail::ComponentIdOf<Components>()]...};
            for (const Chunk& ScopedChunk : ScopedArchetype.Chunks)
            {
                const std::array<const std::byte*, sizeof...(Components)> ScopedBases{
                    ScopedChunk.Storage.Data() + ScopedArchetype.Offsets[Detail::ComponentIdOf<Components>()]...};
                for (std::uint32_t ScopedRow = 0; ScopedRow < ScopedChunk.Count; ++ScopedRow)
                {
                    VisitRowConst<Components...>(InFunction, ScopedChunk.Entities[ScopedRow], ScopedBases,
                                                 ScopedStrides, ScopedRow, std::index_sequence_for<Components...>{});
                }
            }
        }
    }

    [[nodiscard]] std::size_t EntityCount() const noexcept
    {
        return MLiveCount;
    }

    [[nodiscard]] std::uint32_t IterationDepth() const noexcept
    {
        return MIterationDepth;
    }

    // Structural revision used by render-side caches. Only create, destroy,
    // emplace, remove and clear move it; a paused world keeps it constant, so a
    // cache can reuse its previous packet even while systems iterate views.
    [[nodiscard]] std::uint64_t Revision() const noexcept
    {
        return MRevision;
    }

    // Mutable component access revision. Iteration and non-const get() bump it;
    // caches that mirror component values (not just the entity set) compare it.
    [[nodiscard]] std::uint64_t ModificationRevision() const noexcept
    {
        return MModificationRevision;
    }

    void MarkRevision() noexcept
    {
        ++MModificationRevision;
    }

    [[nodiscard]] std::uint32_t& IterationDepthRef() noexcept
    {
        return MIterationDepth;
    }

    [[nodiscard]] std::uint32_t& IterationDepthRef() const noexcept
    {
        return const_cast<World*>(this)->MIterationDepth;
    }

    [[nodiscard]] std::size_t RecordCount() const noexcept
    {
        return MRecords.size();
    }

    [[nodiscard]] Entity EntityAt(std::size_t InIndex) const noexcept
    {
        if (InIndex >= MRecords.size() || MRecords[InIndex].Archetype == kInvalidIndex)
        {
            return kNullEntity;
        }
        return Entity{static_cast<std::uint32_t>(InIndex), MRecords[InIndex].Generation};
    }

    [[nodiscard]] std::size_t ArchetypeCount() const noexcept
    {
        return MArchetypes.size();
    }

    [[nodiscard]] std::size_t ChunkCount() const noexcept
    {
        std::size_t ScopedCount = 0;
        for (const Archetype& ScopedArchetype : MArchetypes)
        {
            ScopedCount += ScopedArchetype.Chunks.size();
        }
        return ScopedCount;
    }

    [[nodiscard]] WorldMemoryStats Stats() const noexcept
    {
        WorldMemoryStats ScopedStats;
        ScopedStats.Archetypes = MArchetypes.size();
        ScopedStats.LiveEntities = MLiveCount;
        for (const Archetype& ScopedArchetype : MArchetypes)
        {
            ScopedStats.CapacityRows +=
                static_cast<std::size_t>(ScopedArchetype.Capacity) * ScopedArchetype.Chunks.size();
            for (const Chunk& ScopedChunk : ScopedArchetype.Chunks)
            {
                ++ScopedStats.Chunks;
                ScopedStats.ComponentBytes += ScopedChunk.Storage.Size();
                ScopedStats.EntityBytes += ScopedChunk.Entities.capacity() * sizeof(Entity);
            }
        }
        return ScopedStats;
    }

    void ReserveEntities(std::size_t InCount)
    {
        MRecords.reserve(InCount);
        MFreeIndices.reserve(InCount);
    }

    void ReserveArchetypes(std::size_t InCount)
    {
        MArchetypes.reserve(InCount);
    }

    void Clear()
    {
        // Clearing is a structural change: render-side caches compare the
        // revision and must not reuse a packet that still describes the removed
        // entities.
        ++MRevision;
        // Keep the archetype and chunk storage: rollback and PIE clear the world
        // every replay, and reallocating the chunks would violate the zero
        // allocation contract. Component destructors are not needed because FCS
        // components are trivially copyable.
        for (Archetype& ScopedArchetype : MArchetypes)
        {
            for (Chunk& ScopedChunk : ScopedArchetype.Chunks)
            {
                ScopedChunk.Count = 0;
                ScopedChunk.Entities.clear();
            }
        }
        MRecords.clear();
        MFreeIndices.clear();
        MLiveCount = 0;
    }

    [[nodiscard]] const Archetype& ArchetypeAt(std::size_t InIndex) const noexcept
    {
        return MArchetypes[InIndex];
    }

    template <typename Component>
    [[nodiscard]] Component* ComponentAt(std::size_t InArchetype, std::size_t InChunk, std::uint32_t InRow) noexcept
    {
        return ComponentPtr<Component>(MArchetypes[InArchetype], MArchetypes[InArchetype].Chunks[InChunk], InRow);
    }

    template <typename Component>
    [[nodiscard]] const Component* ComponentAt(std::size_t InArchetype, std::size_t InChunk,
                                               std::uint32_t InRow) const noexcept
    {
        const Archetype& ScopedArchetype = MArchetypes[InArchetype];
        const Chunk& ScopedChunk = ScopedArchetype.Chunks[InChunk];
        const ComponentId ScopedId = Detail::ComponentIdOf<Component>();
        const std::byte* ScopedBase = ScopedChunk.Storage.Data() + ScopedArchetype.Offsets[ScopedId] +
                                      static_cast<std::size_t>(InRow) * ScopedArchetype.Sizes[ScopedId];
        assert(reinterpret_cast<std::uintptr_t>(ScopedBase) % alignof(Component) == 0);
        return reinterpret_cast<const Component*>(ScopedBase);
    }

private:
    template <typename... Components, typename Function, std::size_t... Indices>
    static void VisitRow(Function& InFunction, Entity InEntity,
                         const std::array<std::byte*, sizeof...(Components)>& InBases,
                         const std::array<std::uint32_t, sizeof...(Components)>& InStrides, std::uint32_t InRow,
                         std::index_sequence<Indices...>)
    {
        InFunction(InEntity, *reinterpret_cast<Components*>(InBases[Indices] +
                                                            static_cast<std::size_t>(InRow) * InStrides[Indices])...);
    }

    template <typename... Components, typename Function, std::size_t... Indices>
    static void VisitRowConst(Function& InFunction, Entity InEntity,
                              const std::array<const std::byte*, sizeof...(Components)>& InBases,
                              const std::array<std::uint32_t, sizeof...(Components)>& InStrides, std::uint32_t InRow,
                              std::index_sequence<Indices...>)
    {
        InFunction(InEntity, *reinterpret_cast<const Components*>(InBases[Indices] + static_cast<std::size_t>(InRow) *
                                                                                         InStrides[Indices])...);
    }

    struct EntityRecord
    {
        std::uint32_t Archetype = kInvalidIndex;
        std::uint32_t Chunk = 0;
        std::uint32_t Row = 0;
        std::uint32_t Generation = 0;
    };

    [[nodiscard]] std::uint32_t GetOrCreateArchetype(const Signature& InType)
    {
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < MArchetypes.size(); ++ScopedIndex)
        {
            if (MArchetypes[ScopedIndex].Type == InType)
            {
                return ScopedIndex;
            }
        }
        Archetype ScopedArchetype;
        ScopedArchetype.Type = InType;
        std::uint32_t ScopedRowBytes = 0;
        for (std::uint32_t ScopedId = 0; ScopedId < kMaxComponents; ++ScopedId)
        {
            if (!InType.test(ScopedId))
            {
                continue;
            }
            const Detail::ComponentMeta& ScopedMeta = Detail::ComponentTable()[ScopedId];
            ScopedArchetype.Sizes[ScopedId] = ScopedMeta.Size;
            ScopedRowBytes += ScopedMeta.Size;
        }
        ScopedArchetype.RowBytes = ScopedRowBytes;
        const std::uint32_t ScopedByteCapacity =
            ScopedRowBytes == 0 ? kMaxRowsPerChunk : std::max<std::uint32_t>(1u, kChunkBytes / ScopedRowBytes);
        ScopedArchetype.Capacity = std::min(ScopedByteCapacity, kMaxRowsPerChunk);
        std::uint32_t ScopedOffset = 0;
        for (std::uint32_t ScopedId = 0; ScopedId < kMaxComponents; ++ScopedId)
        {
            if (!InType.test(ScopedId))
            {
                continue;
            }
            const std::uint32_t ScopedAlignment =
                std::max<std::uint32_t>(1u, Detail::ComponentTable()[ScopedId].Alignment);
            ScopedOffset = AlignUp(ScopedOffset, ScopedAlignment);
            ScopedArchetype.Offsets[ScopedId] = ScopedOffset;
            ScopedOffset += ScopedArchetype.Sizes[ScopedId] * ScopedArchetype.Capacity;
        }
        ScopedArchetype.StorageBytes = ScopedOffset;
        MArchetypes.push_back(std::move(ScopedArchetype));
        return static_cast<std::uint32_t>(MArchetypes.size() - 1u);
    }

    [[nodiscard]] std::pair<std::uint32_t, std::uint32_t> AllocateRow(std::uint32_t InArchetype)
    {
        Archetype& ScopedArchetype = MArchetypes[InArchetype];
        for (std::uint32_t ScopedChunk = 0; ScopedChunk < ScopedArchetype.Chunks.size(); ++ScopedChunk)
        {
            if (ScopedArchetype.Chunks[ScopedChunk].Count < ScopedArchetype.Capacity)
            {
                // Clear() releases the entity-id vector but keeps the chunk and
                // its capacity. Rebuild the row storage before a reused chunk is
                // written again.
                if (ScopedArchetype.Chunks[ScopedChunk].Entities.size() < ScopedArchetype.Capacity)
                {
                    ScopedArchetype.Chunks[ScopedChunk].Entities.resize(ScopedArchetype.Capacity);
                }
                return {ScopedChunk, ScopedArchetype.Chunks[ScopedChunk].Count++};
            }
        }
        Chunk ScopedChunk;
        ScopedChunk.Storage.Allocate(ScopedArchetype.StorageBytes);
        ScopedChunk.Entities.resize(ScopedArchetype.Capacity);
        ScopedArchetype.Chunks.push_back(std::move(ScopedChunk));
        const std::uint32_t ScopedIndex = static_cast<std::uint32_t>(ScopedArchetype.Chunks.size() - 1u);
        return {ScopedIndex, ScopedArchetype.Chunks[ScopedIndex].Count++};
    }

    void MoveEntity(Entity InEntity, std::uint32_t InNewArchetype, ComponentId InNewComponent, const void* InValue)
    {
        EntityRecord& ScopedRecord = MRecords[InEntity.Index];
        const std::uint32_t ScopedOldArchetype = ScopedRecord.Archetype;
        const std::uint32_t ScopedOldChunk = ScopedRecord.Chunk;
        const std::uint32_t ScopedOldRow = ScopedRecord.Row;
        Archetype& ScopedOld = MArchetypes[ScopedOldArchetype];
        Archetype& ScopedNew = MArchetypes[InNewArchetype];
        const auto [ScopedNewChunk, ScopedNewRow] = AllocateRow(InNewArchetype);
        Chunk& ScopedNewStorage = ScopedNew.Chunks[ScopedNewChunk];
        Chunk& ScopedOldStorage = ScopedOld.Chunks[ScopedOldChunk];
        for (std::uint32_t ScopedId = 0; ScopedId < kMaxComponents; ++ScopedId)
        {
            if (!ScopedNew.Type.test(ScopedId))
            {
                continue;
            }
            std::byte* ScopedDestination = ScopedNewStorage.Storage.Data() + ScopedNew.Offsets[ScopedId] +
                                           static_cast<std::size_t>(ScopedNewRow) * ScopedNew.Sizes[ScopedId];
            if (ScopedOld.Type.test(ScopedId))
            {
                const std::byte* ScopedSource = ScopedOldStorage.Storage.Data() + ScopedOld.Offsets[ScopedId] +
                                                static_cast<std::size_t>(ScopedOldRow) * ScopedOld.Sizes[ScopedId];
                std::memcpy(ScopedDestination, ScopedSource, ScopedNew.Sizes[ScopedId]);
            }
            else if (ScopedId == InNewComponent && InValue != nullptr)
            {
                std::memcpy(ScopedDestination, InValue, ScopedNew.Sizes[ScopedId]);
            }
            else
            {
                std::memset(ScopedDestination, 0, ScopedNew.Sizes[ScopedId]);
            }
        }
        ScopedNewStorage.Entities[ScopedNewRow] = InEntity;
        ScopedRecord.Archetype = InNewArchetype;
        ScopedRecord.Chunk = ScopedNewChunk;
        ScopedRecord.Row = ScopedNewRow;

        const std::uint32_t ScopedLastRow = ScopedOldStorage.Count - 1u;
        if (ScopedOldRow != ScopedLastRow)
        {
            for (std::uint32_t ScopedId = 0; ScopedId < kMaxComponents; ++ScopedId)
            {
                if (!ScopedOld.Type.test(ScopedId))
                {
                    continue;
                }
                std::byte* ScopedDestination = ScopedOldStorage.Storage.Data() + ScopedOld.Offsets[ScopedId] +
                                               static_cast<std::size_t>(ScopedOldRow) * ScopedOld.Sizes[ScopedId];
                const std::byte* ScopedSource = ScopedOldStorage.Storage.Data() + ScopedOld.Offsets[ScopedId] +
                                                static_cast<std::size_t>(ScopedLastRow) * ScopedOld.Sizes[ScopedId];
                std::memcpy(ScopedDestination, ScopedSource, ScopedOld.Sizes[ScopedId]);
            }
            const Entity ScopedMoved = ScopedOldStorage.Entities[ScopedLastRow];
            ScopedOldStorage.Entities[ScopedOldRow] = ScopedMoved;
            EntityRecord& ScopedMovedRecord = MRecords[ScopedMoved.Index];
            ScopedMovedRecord.Row = ScopedOldRow;
        }
        --ScopedOldStorage.Count;
    }

    void RemoveRow(std::uint32_t InArchetype, std::uint32_t InChunk, std::uint32_t InRow) noexcept
    {
        Archetype& ScopedArchetype = MArchetypes[InArchetype];
        Chunk& ScopedChunk = ScopedArchetype.Chunks[InChunk];
        const std::uint32_t ScopedLastRow = ScopedChunk.Count - 1u;
        if (InRow != ScopedLastRow)
        {
            for (std::uint32_t ScopedId = 0; ScopedId < kMaxComponents; ++ScopedId)
            {
                if (!ScopedArchetype.Type.test(ScopedId))
                {
                    continue;
                }
                std::byte* ScopedDestination = ScopedChunk.Storage.Data() + ScopedArchetype.Offsets[ScopedId] +
                                               static_cast<std::size_t>(InRow) * ScopedArchetype.Sizes[ScopedId];
                const std::byte* ScopedSource =
                    ScopedChunk.Storage.Data() + ScopedArchetype.Offsets[ScopedId] +
                    static_cast<std::size_t>(ScopedLastRow) * ScopedArchetype.Sizes[ScopedId];
                std::memcpy(ScopedDestination, ScopedSource, ScopedArchetype.Sizes[ScopedId]);
            }
            const Entity ScopedMoved = ScopedChunk.Entities[ScopedLastRow];
            ScopedChunk.Entities[InRow] = ScopedMoved;
            EntityRecord& ScopedMovedRecord = MRecords[ScopedMoved.Index];
            ScopedMovedRecord.Row = InRow;
        }
        --ScopedChunk.Count;
    }

    template <typename Component>
    [[nodiscard]] Component* ComponentPtr(Archetype& InArchetype, Chunk& InChunk, std::uint32_t InRow) noexcept
    {
        const ComponentId ScopedId = Detail::ComponentIdOf<Component>();
        std::byte* ScopedBase = InChunk.Storage.Data() + InArchetype.Offsets[ScopedId] +
                                static_cast<std::size_t>(InRow) * InArchetype.Sizes[ScopedId];
        assert(reinterpret_cast<std::uintptr_t>(ScopedBase) % alignof(Component) == 0);
        return reinterpret_cast<Component*>(ScopedBase);
    }

    template <typename Component>
    [[nodiscard]] Component* ComponentPtr(std::uint32_t InArchetype, std::uint32_t InChunk,
                                          std::uint32_t InRow) noexcept
    {
        Archetype& ScopedArchetype = MArchetypes[InArchetype];
        Chunk& ScopedChunk = ScopedArchetype.Chunks[InChunk];
        const ComponentId ScopedId = Detail::ComponentIdOf<Component>();
        std::byte* ScopedBase = ScopedChunk.Storage.Data() + ScopedArchetype.Offsets[ScopedId] +
                                static_cast<std::size_t>(InRow) * ScopedArchetype.Sizes[ScopedId];
        assert(reinterpret_cast<std::uintptr_t>(ScopedBase) % alignof(Component) == 0);
        return reinterpret_cast<Component*>(ScopedBase);
    }

    std::vector<EntityRecord> MRecords;
    std::vector<std::uint32_t> MFreeIndices;
    std::vector<Archetype> MArchetypes;
    std::size_t MLiveCount = 0;
    std::uint32_t MIterationDepth = 0;
    std::uint64_t MRevision = 0;
    std::uint64_t MModificationRevision = 0;
};

}
