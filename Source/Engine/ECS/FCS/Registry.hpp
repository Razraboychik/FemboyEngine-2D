// FemboyEngine2D - FCS registry adapter for the engine entity type.
//
// The adapter keeps the engine's 32-bit wire handle while the storage is FCS.
// It exists only for the migration window; the final 0.4 tree uses the FCS
// handle directly and drops the legacy include.
#pragma once

#include "Engine/ECS/Entity.hpp"
#include "Engine/ECS/FCS/AccessValidation.hpp"
#include "Engine/ECS/FCS/World.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <map>
#include <new>
#include <tuple>
#include <type_traits>
#include <utility>

namespace Fby::Fcs
{

[[nodiscard]] inline Fby::Entity ToEngineEntity(Entity InEntity) noexcept
{
    return PackEntity(InEntity);
}

[[nodiscard]] inline Entity FromEngineEntity(Fby::Entity InEntity) noexcept
{
    return UnpackEntity(static_cast<std::uint32_t>(Fby::ToIntegral(InEntity)));
}

// A view over one or more component columns. Iteration follows archetype, chunk
// and row order, so the result is deterministic and cache-friendly.
namespace Detail
{
// A live view is a live iteration capability: while it exists, structural
// changes would invalidate its cursors. The guard is copyable because views are
// passed by value; every live copy contributes one depth level.
class ViewIterationGuard
{
public:
    explicit ViewIterationGuard(std::uint32_t& InDepth) noexcept : MDepth(&InDepth)
    {
        ++*MDepth;
    }

    ViewIterationGuard(const ViewIterationGuard& InOther) noexcept : MDepth(InOther.MDepth)
    {
        if (MDepth != nullptr)
        {
            ++*MDepth;
        }
    }

    ViewIterationGuard& operator=(const ViewIterationGuard& InOther) noexcept
    {
        if (this != &InOther)
        {
            if (MDepth != nullptr)
            {
                --*MDepth;
            }
            MDepth = InOther.MDepth;
            if (MDepth != nullptr)
            {
                ++*MDepth;
            }
        }
        return *this;
    }

    ViewIterationGuard(ViewIterationGuard&& InOther) noexcept : MDepth(InOther.MDepth)
    {
        InOther.MDepth = nullptr;
    }

    ViewIterationGuard& operator=(ViewIterationGuard&& InOther) noexcept
    {
        if (this != &InOther)
        {
            if (MDepth != nullptr)
            {
                --*MDepth;
            }
            MDepth = InOther.MDepth;
            InOther.MDepth = nullptr;
        }
        return *this;
    }

    ~ViewIterationGuard()
    {
        if (MDepth != nullptr)
        {
            --*MDepth;
        }
    }

private:
    std::uint32_t* MDepth = nullptr;
};
}

template <typename... Components> class View
{
public:
    explicit View(World& InWorld) noexcept : MWorld(&InWorld)
    {
        (Detail::AccessValidation::Read(Detail::ComponentIdOf<Components>()), ...);
    }

    class Cursor
    {
    public:
        Cursor(World* InWorld, std::size_t InArchetype, std::size_t InChunk, std::uint32_t InRow) noexcept
            : WorldPtr(InWorld), Archetype(InArchetype), Chunk(InChunk), Row(InRow)
        {
            SkipToValid();
        }

        [[nodiscard]] bool operator==(const Cursor& InOther) const noexcept
        {
            return WorldPtr == InOther.WorldPtr && Archetype == InOther.Archetype && Chunk == InOther.Chunk &&
                   Row == InOther.Row;
        }

        void Next() noexcept
        {
            ++Row;
            SkipToValid();
        }

        [[nodiscard]] Fby::Entity Entity() const noexcept
        {
            return ToEngineEntity(WorldPtr->ArchetypeAt(Archetype).Chunks[Chunk].Entities[Row]);
        }

        World* WorldPtr = nullptr;
        std::size_t Archetype = 0;
        std::size_t Chunk = 0;
        std::uint32_t Row = 0;

    private:
        void SkipToValid() noexcept
        {
            if (WorldPtr == nullptr)
            {
                return;
            }
            const Signature ScopedMask = MakeMask();
            while (Archetype < WorldPtr->ArchetypeCount())
            {
                const World::Archetype& ScopedArchetype = WorldPtr->ArchetypeAt(Archetype);
                if ((ScopedArchetype.Type & ScopedMask) != ScopedMask)
                {
                    ++Archetype;
                    Chunk = 0;
                    Row = 0;
                    continue;
                }
                if (Chunk >= ScopedArchetype.Chunks.size())
                {
                    ++Archetype;
                    Chunk = 0;
                    Row = 0;
                    continue;
                }
                if (Row >= ScopedArchetype.Chunks[Chunk].Count)
                {
                    ++Chunk;
                    Row = 0;
                    continue;
                }
                return;
            }
            Chunk = 0;
            Row = 0;
        }

        [[nodiscard]] static Signature MakeMask() noexcept
        {
            Signature ScopedMask;
            (ScopedMask.set(Detail::ComponentIdOf<Components>()), ...);
            return ScopedMask;
        }
    };

    class Iterator
    {
    public:
        explicit Iterator(const Cursor& InCursor) noexcept
            : MCursor(InCursor), MGuard(InCursor.WorldPtr->IterationDepthRef())
        {
        }

        [[nodiscard]] bool operator==(const Iterator& InOther) const noexcept
        {
            return MCursor == InOther.MCursor;
        }

        [[nodiscard]] bool operator!=(const Iterator& InOther) const noexcept
        {
            return !(*this == InOther);
        }

        [[nodiscard]] Fby::Entity operator*() const noexcept
        {
            return MCursor.Entity();
        }

        Iterator& operator++() noexcept
        {
            MCursor.Next();
            return *this;
        }

    private:
        Cursor MCursor;
        Detail::ViewIterationGuard MGuard;
    };

    class EachIterator
    {
    public:
        explicit EachIterator(const Cursor& InCursor) noexcept
            : MCursor(InCursor), MGuard(InCursor.WorldPtr->IterationDepthRef())
        {
        }

        [[nodiscard]] bool operator==(const EachIterator& InOther) const noexcept
        {
            return MCursor == InOther.MCursor;
        }

        [[nodiscard]] bool operator!=(const EachIterator& InOther) const noexcept
        {
            return !(*this == InOther);
        }

        [[nodiscard]] auto operator*() const noexcept
        {
            (Detail::AccessValidation::Write(Detail::ComponentIdOf<Components>()), ...);
            return std::tuple<Fby::Entity, Components&...>(
                MCursor.Entity(),
                *MCursor.WorldPtr->template ComponentAt<Components>(MCursor.Archetype, MCursor.Chunk, MCursor.Row)...);
        }

        EachIterator& operator++() noexcept
        {
            MCursor.Next();
            return *this;
        }

    private:
        Cursor MCursor;
        Detail::ViewIterationGuard MGuard;
    };

    class EachRange
    {
    public:
        explicit EachRange(World& InWorld) noexcept : MWorld(&InWorld)
        {
        }

        [[nodiscard]] EachIterator begin() const noexcept
        {
            return EachIterator(Cursor(MWorld, 0, 0, 0));
        }

        [[nodiscard]] EachIterator end() const noexcept
        {
            return EachIterator(Cursor(MWorld, MWorld->ArchetypeCount(), 0, 0));
        }

    private:
        World* MWorld = nullptr;
    };

    [[nodiscard]] Iterator begin() noexcept
    {
        return Iterator(Cursor(MWorld, 0, 0, 0));
    }

    [[nodiscard]] Iterator end() noexcept
    {
        return Iterator(Cursor(MWorld, MWorld->ArchetypeCount(), 0, 0));
    }

    [[nodiscard]] Iterator begin() const noexcept
    {
        return Iterator(Cursor(MWorld, 0, 0, 0));
    }

    [[nodiscard]] Iterator end() const noexcept
    {
        return Iterator(Cursor(MWorld, MWorld->ArchetypeCount(), 0, 0));
    }

    [[nodiscard]] EachRange each() const noexcept
    {
        MWorld->MarkRevision();
        return EachRange(*MWorld);
    }

    [[nodiscard]] std::size_t size() const noexcept
    {
        std::size_t ScopedCount = 0;
        for (Iterator ScopedIt = begin(); ScopedIt != end(); ++ScopedIt)
        {
            ++ScopedCount;
        }
        return ScopedCount;
    }

    [[nodiscard]] std::size_t size_hint() const noexcept
    {
        Signature ScopedMask;
        (ScopedMask.set(Detail::ComponentIdOf<Components>()), ...);
        std::size_t ScopedCount = 0;
        for (std::size_t ScopedArchetype = 0; ScopedArchetype < MWorld->ArchetypeCount(); ++ScopedArchetype)
        {
            const World::Archetype& ScopedType = MWorld->ArchetypeAt(ScopedArchetype);
            if ((ScopedType.Type & ScopedMask) != ScopedMask)
            {
                continue;
            }
            for (const World::Chunk& ScopedChunk : ScopedType.Chunks)
            {
                ScopedCount += ScopedChunk.Count;
            }
        }
        return ScopedCount;
    }

    [[nodiscard]] bool empty() const noexcept
    {
        return begin() == end();
    }

    template <typename Component> [[nodiscard]] Component& get(Fby::Entity InEntity) noexcept
    {
        Detail::AccessValidation::Write(Detail::ComponentIdOf<Component>());
        MWorld->MarkRevision();
        Component* ScopedComponent = MWorld->TryGet<Component>(FromEngineEntity(InEntity));
        assert(ScopedComponent != nullptr);
        return *ScopedComponent;
    }

    template <typename Component> [[nodiscard]] const Component& get(Fby::Entity InEntity) const noexcept
    {
        Detail::AccessValidation::Read(Detail::ComponentIdOf<Component>());
        const Component* ScopedComponent = MWorld->TryGet<Component>(FromEngineEntity(InEntity));
        assert(ScopedComponent != nullptr);
        return *ScopedComponent;
    }

private:
    World* MWorld = nullptr;
};

// A read-only view. It yields `const T&`, so a system that receives a
// `const Registry&` cannot mutate a component through iteration.
template <typename... Components> class ConstView
{
public:
    explicit ConstView(const World& InWorld) noexcept : MWorld(&InWorld)
    {
        (Detail::AccessValidation::Read(Detail::ComponentIdOf<Components>()), ...);
    }

    class Cursor
    {
    public:
        Cursor(const World* InWorld, std::size_t InArchetype, std::size_t InChunk, std::uint32_t InRow) noexcept
            : WorldPtr(InWorld), Archetype(InArchetype), Chunk(InChunk), Row(InRow)
        {
            SkipToValid();
        }

        [[nodiscard]] bool operator==(const Cursor& InOther) const noexcept
        {
            return WorldPtr == InOther.WorldPtr && Archetype == InOther.Archetype && Chunk == InOther.Chunk &&
                   Row == InOther.Row;
        }

        void Next() noexcept
        {
            ++Row;
            SkipToValid();
        }

        [[nodiscard]] Fby::Entity Entity() const noexcept
        {
            return ToEngineEntity(WorldPtr->ArchetypeAt(Archetype).Chunks[Chunk].Entities[Row]);
        }

        const World* WorldPtr = nullptr;
        std::size_t Archetype = 0;
        std::size_t Chunk = 0;
        std::uint32_t Row = 0;

    private:
        void SkipToValid() noexcept
        {
            if (WorldPtr == nullptr)
            {
                return;
            }
            const Signature ScopedMask = MakeMask();
            while (Archetype < WorldPtr->ArchetypeCount())
            {
                const World::Archetype& ScopedArchetype = WorldPtr->ArchetypeAt(Archetype);
                if ((ScopedArchetype.Type & ScopedMask) != ScopedMask)
                {
                    ++Archetype;
                    Chunk = 0;
                    Row = 0;
                    continue;
                }
                if (Chunk >= ScopedArchetype.Chunks.size())
                {
                    ++Archetype;
                    Chunk = 0;
                    Row = 0;
                    continue;
                }
                if (Row >= ScopedArchetype.Chunks[Chunk].Count)
                {
                    ++Chunk;
                    Row = 0;
                    continue;
                }
                return;
            }
            Chunk = 0;
            Row = 0;
        }

        [[nodiscard]] static Signature MakeMask() noexcept
        {
            Signature ScopedMask;
            (ScopedMask.set(Detail::ComponentIdOf<Components>()), ...);
            return ScopedMask;
        }
    };

    class Iterator
    {
    public:
        explicit Iterator(const Cursor& InCursor) noexcept
            : MCursor(InCursor), MGuard(InCursor.WorldPtr->IterationDepthRef())
        {
        }

        [[nodiscard]] bool operator==(const Iterator& InOther) const noexcept
        {
            return MCursor == InOther.MCursor;
        }

        [[nodiscard]] bool operator!=(const Iterator& InOther) const noexcept
        {
            return !(*this == InOther);
        }

        [[nodiscard]] Fby::Entity operator*() const noexcept
        {
            return MCursor.Entity();
        }

        Iterator& operator++() noexcept
        {
            MCursor.Next();
            return *this;
        }

    private:
        Cursor MCursor;
        Detail::ViewIterationGuard MGuard;
    };

    class EachIterator
    {
    public:
        explicit EachIterator(const Cursor& InCursor) noexcept
            : MCursor(InCursor), MGuard(InCursor.WorldPtr->IterationDepthRef())
        {
        }

        [[nodiscard]] bool operator==(const EachIterator& InOther) const noexcept
        {
            return MCursor == InOther.MCursor;
        }

        [[nodiscard]] bool operator!=(const EachIterator& InOther) const noexcept
        {
            return !(*this == InOther);
        }

        [[nodiscard]] auto operator*() const noexcept
        {
            (Detail::AccessValidation::Read(Detail::ComponentIdOf<Components>()), ...);
            return std::tuple<Fby::Entity, const Components&...>(
                MCursor.Entity(),
                *MCursor.WorldPtr->template ComponentAt<Components>(MCursor.Archetype, MCursor.Chunk, MCursor.Row)...);
        }

        EachIterator& operator++() noexcept
        {
            MCursor.Next();
            return *this;
        }

    private:
        Cursor MCursor;
        Detail::ViewIterationGuard MGuard;
    };

    class EachRange
    {
    public:
        explicit EachRange(const World& InWorld) noexcept : MWorld(&InWorld)
        {
        }

        [[nodiscard]] EachIterator begin() const noexcept
        {
            return EachIterator(Cursor(MWorld, 0, 0, 0));
        }

        [[nodiscard]] EachIterator end() const noexcept
        {
            return EachIterator(Cursor(MWorld, MWorld->ArchetypeCount(), 0, 0));
        }

    private:
        const World* MWorld = nullptr;
    };

    [[nodiscard]] Iterator begin() const noexcept
    {
        return Iterator(Cursor(MWorld, 0, 0, 0));
    }

    [[nodiscard]] Iterator end() const noexcept
    {
        return Iterator(Cursor(MWorld, MWorld->ArchetypeCount(), 0, 0));
    }

    [[nodiscard]] EachRange each() const noexcept
    {
        return EachRange(*MWorld);
    }

    [[nodiscard]] std::size_t size() const noexcept
    {
        std::size_t ScopedCount = 0;
        for (Iterator ScopedIt = begin(); ScopedIt != end(); ++ScopedIt)
        {
            ++ScopedCount;
        }
        return ScopedCount;
    }

    [[nodiscard]] std::size_t size_hint() const noexcept
    {
        Signature ScopedMask;
        (ScopedMask.set(Detail::ComponentIdOf<Components>()), ...);
        std::size_t ScopedCount = 0;
        for (std::size_t ScopedArchetype = 0; ScopedArchetype < MWorld->ArchetypeCount(); ++ScopedArchetype)
        {
            const World::Archetype& ScopedType = MWorld->ArchetypeAt(ScopedArchetype);
            if ((ScopedType.Type & ScopedMask) != ScopedMask)
            {
                continue;
            }
            for (const World::Chunk& ScopedChunk : ScopedType.Chunks)
            {
                ScopedCount += ScopedChunk.Count;
            }
        }
        return ScopedCount;
    }

    [[nodiscard]] bool empty() const noexcept
    {
        return begin() == end();
    }

    template <typename Component> [[nodiscard]] const Component& get(Fby::Entity InEntity) const noexcept
    {
        Detail::AccessValidation::Read(Detail::ComponentIdOf<Component>());
        const Component* ScopedComponent = MWorld->TryGet<Component>(FromEngineEntity(InEntity));
        assert(ScopedComponent != nullptr);
        return *ScopedComponent;
    }

private:
    const World* MWorld = nullptr;
};

// Type-erased registry context for the two engine services that used legacy registry
// ctx(): the native cleanup hook and the hierarchy scratch. The key is the
// address of a per-type static, so no RTTI or type_index is needed.
class Context
{
public:
    Context() = default;
    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;

    ~Context()
    {
        Clear();
    }

    template <typename T, typename... Args> T& emplace(Args&&... InArgs)
    {
        const void* ScopedKey = Key<T>();
        if (const auto ScopedFound = MEntries.find(ScopedKey); ScopedFound != MEntries.end())
        {
            return *static_cast<T*>(ScopedFound->second.Object);
        }
        T* ScopedObject = new T(std::forward<Args>(InArgs)...);
        MEntries.emplace(ScopedKey, Entry{ScopedObject, &DestroyEntry<T>});
        return *ScopedObject;
    }

    template <typename T> [[nodiscard]] T* find() noexcept
    {
        const auto ScopedFound = MEntries.find(Key<T>());
        return ScopedFound != MEntries.end() ? static_cast<T*>(ScopedFound->second.Object) : nullptr;
    }

    template <typename T> [[nodiscard]] const T* find() const noexcept
    {
        const auto ScopedFound = MEntries.find(Key<T>());
        return ScopedFound != MEntries.end() ? static_cast<const T*>(ScopedFound->second.Object) : nullptr;
    }

    template <typename T> void insert_or_assign(T InValue)
    {
        const void* ScopedKey = Key<T>();
        if (const auto ScopedFound = MEntries.find(ScopedKey); ScopedFound != MEntries.end())
        {
            *static_cast<T*>(ScopedFound->second.Object) = std::move(InValue);
            return;
        }
        T* ScopedObject = new T(std::move(InValue));
        MEntries.emplace(ScopedKey, Entry{ScopedObject, &DestroyEntry<T>});
    }

    void Clear() noexcept
    {
        for (auto& ScopedEntry : MEntries)
        {
            if (ScopedEntry.second.Destroy != nullptr)
            {
                ScopedEntry.second.Destroy(ScopedEntry.second.Object);
            }
        }
        MEntries.clear();
    }

private:
    struct Entry
    {
        void* Object = nullptr;
        void (*Destroy)(void*) = nullptr;
    };

    template <typename T> [[nodiscard]] static const void* Key() noexcept
    {
        static const int ScopedKey = 0;
        return &ScopedKey;
    }

    template <typename T> static void DestroyEntry(void* InObject) noexcept
    {
        delete static_cast<T*>(InObject);
    }

    std::map<const void*, Entry> MEntries;
};

// Range over every live entity, used by scene capture and the registry cleanup
// path. It yields Fby::Entity values so the existing serialization code keeps
// working during the migration.
class EntityStorage
{
public:
    explicit EntityStorage(World& InWorld) noexcept : MWorld(&InWorld)
    {
    }

    class Iterator
    {
    public:
        Iterator(World* InWorld, std::size_t InIndex) noexcept : MWorld(InWorld), MIndex(InIndex)
        {
            SkipDead();
        }

        [[nodiscard]] Fby::Entity operator*() const noexcept
        {
            return ToEngineEntity(MWorld->EntityAt(MIndex));
        }

        Iterator& operator++() noexcept
        {
            ++MIndex;
            SkipDead();
            return *this;
        }

        [[nodiscard]] bool operator==(const Iterator& InOther) const noexcept
        {
            return MWorld == InOther.MWorld && MIndex == InOther.MIndex;
        }

        [[nodiscard]] bool operator!=(const Iterator& InOther) const noexcept
        {
            return !(*this == InOther);
        }

    private:
        void SkipDead() noexcept
        {
            while (MWorld != nullptr && MIndex < MWorld->RecordCount() && !MWorld->EntityAt(MIndex).IsValid())
            {
                ++MIndex;
            }
        }

        World* MWorld = nullptr;
        std::size_t MIndex = 0;
    };

    [[nodiscard]] Iterator begin() const noexcept
    {
        return Iterator(MWorld, 0);
    }

    [[nodiscard]] Iterator end() const noexcept
    {
        return Iterator(MWorld, MWorld->RecordCount());
    }

    [[nodiscard]] std::size_t size() const noexcept
    {
        std::size_t ScopedCount = 0;
        for (Iterator ScopedIt = begin(); ScopedIt != end(); ++ScopedIt)
        {
            ++ScopedCount;
        }
        return ScopedCount;
    }

    void clear() noexcept
    {
    }

private:
    World* MWorld = nullptr;
};

// Minimal component-pool view for the few call sites that ask only for the
// number of components of one type. Iteration uses view<T>() instead.
template <typename Component> class ComponentStorage
{
public:
    explicit ComponentStorage(World& InWorld) noexcept : MWorld(&InWorld)
    {
    }

    [[nodiscard]] std::size_t size() const noexcept
    {
        std::size_t ScopedCount = 0;
        MWorld->ForEach<Component>(
            [&](Entity, Component&)
            {
                ++ScopedCount;
            });
        return ScopedCount;
    }

    [[nodiscard]] bool empty() const noexcept
    {
        return size() == 0;
    }

private:
    World* MWorld = nullptr;
};

// The engine-facing registry. It intentionally keeps the small engine-facing
// surface the engine uses today; the FCS storage underneath is chunked SoA.
class Registry
{
public:
    using entity_type = Fby::Entity;
    using EntityType = Fby::Entity;

    [[nodiscard]] entity_type create()
    {
        return ToEngineEntity(MWorld.Create());
    }

    [[nodiscard]] entity_type create(entity_type InEntity)
    {
        return ToEngineEntity(MWorld.CreateAt(FromEngineEntity(InEntity)));
    }

    void destroy(entity_type InEntity) noexcept
    {
        MWorld.Destroy(FromEngineEntity(InEntity));
    }

    [[nodiscard]] bool valid(entity_type InEntity) const noexcept
    {
        return MWorld.IsAlive(FromEngineEntity(InEntity));
    }

    void clear() noexcept
    {
        MWorld.Clear();
    }

    // The engine spells "remove this component from every entity" as clear<T>().
    template <typename Component> void clear()
    {
        for (std::size_t ScopedIndex = 0; ScopedIndex < MWorld.RecordCount(); ++ScopedIndex)
        {
            const Entity ScopedEntity = MWorld.EntityAt(ScopedIndex);
            if (ScopedEntity.IsValid())
            {
                (void)MWorld.Remove<Component>(ScopedEntity);
            }
        }
    }

    [[nodiscard]] std::size_t size() const noexcept
    {
        return MWorld.EntityCount();
    }

    [[nodiscard]] bool empty() const noexcept
    {
        return MWorld.EntityCount() == 0;
    }

    template <typename Component, typename... Args> Component& emplace(entity_type InEntity, Args&&... InArgs)
    {
        return MWorld.Emplace<Component>(FromEngineEntity(InEntity), std::forward<Args>(InArgs)...);
    }

    template <typename Component, typename... Args>
    Component& emplace_or_replace(entity_type InEntity, Args&&... InArgs)
    {
        return MWorld.Emplace<Component>(FromEngineEntity(InEntity), std::forward<Args>(InArgs)...);
    }

    template <typename Component, typename... Args> Component& replace(entity_type InEntity, Args&&... InArgs)
    {
        const Entity ScopedEntity = FromEngineEntity(InEntity);
        (void)MWorld.Remove<Component>(ScopedEntity);
        return MWorld.Emplace<Component>(ScopedEntity, std::forward<Args>(InArgs)...);
    }

    template <typename Component, typename Function> Component* patch(entity_type InEntity, Function&& InFunction)
    {
        Component* ScopedComponent = try_get<Component>(InEntity);
        if (ScopedComponent != nullptr)
        {
            InFunction(*ScopedComponent);
        }
        return ScopedComponent;
    }

    template <typename Component> bool remove(entity_type InEntity) noexcept
    {
        return MWorld.Remove<Component>(FromEngineEntity(InEntity));
    }

    template <typename Component> [[nodiscard]] Component* try_get(entity_type InEntity) noexcept
    {
        MWorld.MarkRevision();
        return MWorld.TryGet<Component>(FromEngineEntity(InEntity));
    }

    template <typename Component> [[nodiscard]] const Component* try_get(entity_type InEntity) const noexcept
    {
        return MWorld.TryGet<Component>(FromEngineEntity(InEntity));
    }

    template <typename Component> [[nodiscard]] Component& get(entity_type InEntity)
    {
        Detail::AccessValidation::Write(Detail::ComponentIdOf<Component>());
        MWorld.MarkRevision();
        Component* ScopedComponent = try_get<Component>(InEntity);
        assert(ScopedComponent != nullptr);
        return *ScopedComponent;
    }

    template <typename Component> [[nodiscard]] const Component& get(entity_type InEntity) const
    {
        Detail::AccessValidation::Read(Detail::ComponentIdOf<Component>());
        const Component* ScopedComponent = try_get<Component>(InEntity);
        assert(ScopedComponent != nullptr);
        return *ScopedComponent;
    }

    template <typename... Components> [[nodiscard]] bool all_of(entity_type InEntity) const noexcept
    {
        return ((try_get<Components>(InEntity) != nullptr) && ...);
    }

    template <typename Component, typename... Args> Component& get_or_emplace(entity_type InEntity, Args&&... InArgs)
    {
        if (Component* ScopedExisting = try_get<Component>(InEntity); ScopedExisting != nullptr)
        {
            return *ScopedExisting;
        }
        return emplace<Component>(InEntity, std::forward<Args>(InArgs)...);
    }

    template <typename... Components> [[nodiscard]] View<Components...> view() noexcept
    {
        return View<Components...>(MWorld);
    }

    template <typename... Components> [[nodiscard]] ConstView<Components...> view() const noexcept
    {
        return ConstView<Components...>(MWorld);
    }

    [[nodiscard]] World& Data() noexcept
    {
        return MWorld;
    }

    [[nodiscard]] const World& Data() const noexcept
    {
        return MWorld;
    }

    [[nodiscard]] Context& ctx() noexcept
    {
        return MContext;
    }

    [[nodiscard]] const Context& ctx() const noexcept
    {
        return MContext;
    }

    template <typename T> [[nodiscard]] decltype(auto) storage() noexcept
    {
        if constexpr (std::is_same_v<T, Fby::Entity>)
        {
            return (MEntityStorage);
        }
        else
        {
            return ComponentStorage<T>(MWorld);
        }
    }

    template <typename T> [[nodiscard]] decltype(auto) storage() const noexcept
    {
        if constexpr (std::is_same_v<T, Fby::Entity>)
        {
            return (MEntityStorage);
        }
        else
        {
            return ComponentStorage<T>(const_cast<World&>(MWorld));
        }
    }

private:
    World MWorld;
    EntityStorage MEntityStorage{MWorld};
    Context MContext;
};

}
