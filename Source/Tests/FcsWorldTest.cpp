// FemboyEngine2D - FCS world smoke test.
#include "Engine/ECS/FCS/CommandBuffer.hpp"
#include "Engine/ECS/FCS/Registry.hpp"
#include "Engine/ECS/FCS/World.hpp"

#include <cstdint>
#include <cstdio>
#include <type_traits>
#include <vector>

namespace
{

struct Position
{
    float X = 0.0f;
    float Y = 0.0f;
};

struct Velocity
{
    float X = 0.0f;
    float Y = 0.0f;
};

struct Health
{
    int Value = 0;
};

struct alignas(8) Align8
{
    std::uint64_t Value = 0;
};

struct alignas(32) Align32
{
    float Values[8] = {};
};

struct alignas(64) Align64
{
    float Values[16] = {};
};

struct Tag
{
};

int Failures = 0;

void Check(bool InCondition, const char* InMessage)
{
    if (!InCondition)
    {
        std::printf("fcs_world_test: FAIL: %s\n", InMessage);
        ++Failures;
    }
}

}

int main()
{
    Fby::Fcs::World ScopedWorld;
    std::vector<Fby::Fcs::Entity> ScopedEntities;
    ScopedEntities.reserve(1000);
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < 1000; ++ScopedIndex)
    {
        const Fby::Fcs::Entity ScopedEntity = ScopedWorld.Create();
        ScopedEntities.push_back(ScopedEntity);
        ScopedWorld.Emplace<Position>(ScopedEntity, Position{static_cast<float>(ScopedIndex), 1.0f});
        if ((ScopedIndex & 1u) == 0u)
        {
            ScopedWorld.Emplace<Velocity>(ScopedEntity, Velocity{1.0f, 2.0f});
        }
    }
    Check(ScopedWorld.EntityCount() == 1000, "all entities are alive");
    Check(ScopedWorld.IterationDepth() == 0, "iteration depth starts at zero");

    std::size_t ScopedPositionCount = 0;
    float ScopedPositionSum = 0.0f;
    bool ScopedDepthInside = false;
    ScopedWorld.ForEach<Position>(
        [&](Fby::Fcs::Entity, Position& InPosition)
        {
            ScopedDepthInside = ScopedDepthInside || ScopedWorld.IterationDepth() == 1;
            ++ScopedPositionCount;
            ScopedPositionSum += InPosition.X;
        });
    Check(ScopedPositionCount == 1000, "position iteration sees every entity");
    Check(ScopedDepthInside, "iteration depth is visible inside the callback");
    Check(ScopedWorld.IterationDepth() == 0, "iteration depth returns to zero");
    Check(ScopedPositionSum == 499500.0f, "position values survive archetype moves");

    std::size_t ScopedVelocityCount = 0;
    ScopedWorld.ForEach<Position, Velocity>(
        [&](Fby::Fcs::Entity, Position&, Velocity& InVelocity)
        {
            ++ScopedVelocityCount;
            InVelocity.X += 1.0f;
        });
    Check(ScopedVelocityCount == 500, "two-component iteration sees the moving half");

    const Fby::Fcs::Entity ScopedFirst = ScopedEntities.front();
    Check(ScopedWorld.TryGet<Health>(ScopedFirst) == nullptr, "health is absent before emplace");
    ScopedWorld.Emplace<Health>(ScopedFirst, Health{7});
    Check(ScopedWorld.TryGet<Health>(ScopedFirst) != nullptr, "health is present after emplace");
    Check(ScopedWorld.TryGet<Position>(ScopedFirst)->X == 0.0f, "position survives adding health");
    Check(ScopedWorld.Remove<Velocity>(ScopedFirst), "velocity removal succeeds");
    Check(!ScopedWorld.Has<Velocity>(ScopedFirst), "velocity is gone");
    Check(ScopedWorld.TryGet<Position>(ScopedFirst) != nullptr, "position survives removing velocity");

    const Fby::Fcs::Entity ScopedDestroyed = ScopedEntities[1];
    ScopedWorld.Destroy(ScopedDestroyed);
    Check(!ScopedWorld.IsAlive(ScopedDestroyed), "destroyed handle is dead");
    const Fby::Fcs::Entity ScopedReused = ScopedWorld.Create();
    Check(ScopedReused.Index == ScopedDestroyed.Index, "the index is recycled");
    Check(ScopedReused.Generation != ScopedDestroyed.Generation, "the generation rejects the old handle");
    Check(!ScopedWorld.IsAlive(ScopedDestroyed), "the old handle stays dead after reuse");
    Check(ScopedWorld.EntityCount() == 1000, "live count is stable after destroy and create");

    // Packed handles round-trip through the 24/8 wire layout used by the engine.
    {
        const Fby::Fcs::Entity ScopedHandle{123456u, 7u};
        const Fby::Fcs::Entity ScopedUnpacked = Fby::Fcs::UnpackEntity(Fby::Fcs::PackEntity(ScopedHandle));
        Check(ScopedUnpacked == ScopedHandle, "packed entity round-trips");
        Check(Fby::Fcs::PackEntity(Fby::Fcs::kNullEntity) == Fby::Fcs::kPackedNullEntity, "null entity packs");
    }

    // Commands stay deferred until Play and preserve the payload arena.
    {
        Fby::Fcs::World ScopedDeferred;
        Fby::Fcs::CommandBuffer ScopedCommands{ScopedDeferred};
        ScopedCommands.Reserve(4);
        const Fby::Fcs::Entity ScopedEntity = ScopedDeferred.Create();
        ScopedCommands.Emplace<Position>(ScopedEntity, Position{4.0f, 5.0f});
        ScopedCommands.Emplace<Health>(ScopedEntity, Health{9});
        Check(!ScopedDeferred.Has<Position>(ScopedEntity), "emplace is deferred");
        ScopedCommands.Play();
        Check(ScopedDeferred.TryGet<Position>(ScopedEntity)->X == 4.0f, "deferred emplace applies");
        Check(ScopedDeferred.TryGet<Health>(ScopedEntity)->Value == 9, "deferred payload survives");
        ScopedCommands.Remove<Health>(ScopedEntity);
        ScopedCommands.Destroy(ScopedEntity);
        ScopedCommands.Play();
        Check(!ScopedDeferred.IsAlive(ScopedEntity), "deferred destroy applies");
    }

    // A larger scene checks that the chunk path does not fall over at scale.
    Fby::Fcs::World ScopedLarge;
    ScopedLarge.ReserveEntities(100000);
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < 100000; ++ScopedIndex)
    {
        const Fby::Fcs::Entity ScopedEntity = ScopedLarge.Create();
        ScopedLarge.Emplace<Position>(ScopedEntity, Position{static_cast<float>(ScopedIndex), 0.0f});
    }
    std::size_t ScopedLargeCount = 0;
    ScopedLarge.ForEach<Position>(
        [&](Fby::Fcs::Entity, Position&)
        {
            ++ScopedLargeCount;
        });
    Check(ScopedLargeCount == 100000, "100k entities iterate");

    // Column offsets must honor alignof: an 8-byte column after a 4-byte one
    // used to land on a 4-byte boundary.
    {
        Fby::Fcs::World ScopedAligned;
        std::vector<Fby::Fcs::Entity> ScopedAlignedEntities;
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < 64; ++ScopedIndex)
        {
            const Fby::Fcs::Entity ScopedEntity = ScopedAligned.Create();
            ScopedAlignedEntities.push_back(ScopedEntity);
            ScopedAligned.Emplace<Position>(ScopedEntity, Position{1.0f, 2.0f});
            ScopedAligned.Emplace<Align8>(ScopedEntity, Align8{0x1122334455667788ull});
            ScopedAligned.Emplace<Align32>(ScopedEntity, Align32{});
            ScopedAligned.Emplace<Align64>(ScopedEntity, Align64{});
        }
        bool ScopedAlignedOk = true;
        for (const Fby::Fcs::Entity ScopedEntity : ScopedAlignedEntities)
        {
            const Align8* ScopedEight = ScopedAligned.TryGet<Align8>(ScopedEntity);
            const Align32* ScopedThirtyTwo = ScopedAligned.TryGet<Align32>(ScopedEntity);
            const Align64* ScopedSixtyFour = ScopedAligned.TryGet<Align64>(ScopedEntity);
            ScopedAlignedOk = ScopedAlignedOk && ScopedEight != nullptr && ScopedThirtyTwo != nullptr &&
                              ScopedSixtyFour != nullptr &&
                              reinterpret_cast<std::uintptr_t>(ScopedEight) % alignof(Align8) == 0 &&
                              reinterpret_cast<std::uintptr_t>(ScopedThirtyTwo) % alignof(Align32) == 0 &&
                              reinterpret_cast<std::uintptr_t>(ScopedSixtyFour) % alignof(Align64) == 0;
        }
        Check(ScopedAlignedOk, "aligned columns keep every component pointer aligned");
        Check(alignof(Align64) <= Fby::Fcs::kChunkAlignment, "test alignment fits the chunk alignment");

        const Fby::Fcs::WorldMemoryStats ScopedStats = ScopedAligned.Stats();
        Check(ScopedStats.Archetypes >= 1, "stats count archetypes");
        Check(ScopedStats.LiveEntities == 64, "stats count live entities");
        Check(ScopedStats.Chunks >= 1, "stats count chunks");
        Check(ScopedStats.ComponentBytes > 0, "stats count component bytes");
        Check(ScopedStats.EntityBytes > 0, "stats count chunk entity arrays");
        Check(ScopedStats.TotalBytes() == ScopedStats.ComponentBytes + ScopedStats.EntityBytes,
              "stats total is the sum of its parts");
        Check(ScopedStats.CapacityRows <= ScopedStats.Chunks * Fby::Fcs::kMaxRowsPerChunk,
              "chunk capacity respects the row cap");
    }

    // A tag-only archetype must not reserve 16384 entity handles per chunk.
    {
        Fby::Fcs::World ScopedTags;
        (void)ScopedTags.Create();
        const Fby::Fcs::WorldMemoryStats ScopedStats = ScopedTags.Stats();
        Check(ScopedStats.CapacityRows <= Fby::Fcs::kMaxRowsPerChunk, "empty archetype capacity is capped");
        Check(ScopedStats.EntityBytes <= std::size_t{Fby::Fcs::kMaxRowsPerChunk} * sizeof(Fby::Fcs::Entity),
              "empty archetype entity array is bounded");
    }

    // An empty tag takes no column bytes; the signature still tracks it and the
    // accessor returns a valid address.
    {
        Fby::Fcs::World ScopedTagWorld;
        std::vector<Fby::Fcs::Entity> ScopedTagEntities;
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < 128; ++ScopedIndex)
        {
            const Fby::Fcs::Entity ScopedEntity = ScopedTagWorld.Create();
            ScopedTagEntities.push_back(ScopedEntity);
            ScopedTagWorld.Emplace<Tag>(ScopedEntity);
        }
        bool ScopedTagsPresent = true;
        for (const Fby::Fcs::Entity ScopedEntity : ScopedTagEntities)
        {
            ScopedTagsPresent = ScopedTagsPresent && ScopedTagWorld.Has<Tag>(ScopedEntity) &&
                                ScopedTagWorld.TryGet<Tag>(ScopedEntity) != nullptr;
        }
        Check(ScopedTagsPresent, "empty tags are present without storage");
        std::size_t ScopedTagCount = 0;
        ScopedTagWorld.ForEach<Tag>(
            [&](Fby::Fcs::Entity, Tag&)
            {
                ++ScopedTagCount;
            });
        Check(ScopedTagCount == 128, "tag iteration sees every entity");
        const Fby::Fcs::WorldMemoryStats ScopedStats = ScopedTagWorld.Stats();
        Check(ScopedStats.ComponentBytes == 0, "a tag-only archetype allocates no component bytes");
        Check(ScopedStats.EntityBytes > 0, "the tag archetype still owns entity handles");
    }

    // Structural changes stay in the buffer until Play: a view iterated before
    // Play must still see the old archetypes and no half-moved rows.
    {
        Fby::Fcs::World ScopedDeferred;
        Fby::Fcs::CommandBuffer ScopedCommands{ScopedDeferred};
        const Fby::Fcs::Entity ScopedA = ScopedDeferred.Create();
        const Fby::Fcs::Entity ScopedB = ScopedDeferred.Create();
        const Fby::Fcs::Entity ScopedC = ScopedDeferred.Create();
        ScopedDeferred.Emplace<Position>(ScopedA, Position{1.0f, 0.0f});
        ScopedDeferred.Emplace<Position>(ScopedB, Position{2.0f, 0.0f});
        ScopedDeferred.Emplace<Position>(ScopedC, Position{3.0f, 0.0f});

        ScopedCommands.Emplace<Health>(ScopedA, Health{10});
        ScopedCommands.Emplace<Health>(ScopedB, Health{20});
        ScopedCommands.Destroy(ScopedC);
        std::size_t ScopedBefore = 0;
        ScopedDeferred.ForEach<Position>(
            [&](Fby::Fcs::Entity, Position&)
            {
                ++ScopedBefore;
            });
        Check(ScopedBefore == 3, "a view before Play sees the pre-command archetypes");
        Check(!ScopedDeferred.Has<Health>(ScopedA), "emplace stays deferred until Play");

        ScopedCommands.Play();
        std::size_t ScopedAfter = 0;
        ScopedDeferred.ForEach<Position>(
            [&](Fby::Fcs::Entity, Position&)
            {
                ++ScopedAfter;
            });
        Check(ScopedAfter == 2, "Play applies the structural changes together");
        Check(ScopedDeferred.Has<Health>(ScopedA) && ScopedDeferred.Has<Health>(ScopedB),
              "queued components exist after Play");
        Check(!ScopedDeferred.IsAlive(ScopedC), "queued destroy applies after Play");
    }

    // Commands queued from inside a view stay deferred and apply after the
    // iteration, which is the pattern the guard enforces.
    {
        Fby::Fcs::World ScopedInside;
        const Fby::Fcs::Entity ScopedEntity = ScopedInside.Create();
        ScopedInside.Emplace<Position>(ScopedEntity, Position{1.0f, 1.0f});
        Fby::Fcs::CommandBuffer ScopedCommands{ScopedInside};
        ScopedInside.ForEach<Position>(
            [&](Fby::Fcs::Entity InEntity, Position&)
            {
                ScopedCommands.Emplace<Health>(InEntity, Health{5});
            });
        Check(!ScopedInside.Has<Health>(ScopedEntity), "commands queued inside iteration stay deferred");
        ScopedCommands.Play();
        Check(ScopedInside.Has<Health>(ScopedEntity), "commands apply after the iteration");
    }

    // The public Registry view is the path real systems use; its guard must be
    // active for the whole range-for and commands must stay deferred.
    {
        Fby::Fcs::Registry ScopedRegistry;
        const Fby::Entity ScopedEntity = ScopedRegistry.create();
        ScopedRegistry.emplace<Position>(ScopedEntity, Position{2.0f, 3.0f});
        bool ScopedInsideView = false;
        for (auto [ScopedViewed, ScopedPosition] : ScopedRegistry.view<Position>().each())
        {
            ScopedInsideView = ScopedInsideView || ScopedRegistry.Data().IterationDepth() > 0;
            (void)ScopedViewed;
            (void)ScopedPosition;
        }
        Check(ScopedInsideView, "view iteration exposes its depth");
        Check(ScopedRegistry.Data().IterationDepth() == 0, "view iteration releases its depth");

        Fby::Fcs::CommandBuffer ScopedCommands{ScopedRegistry.Data()};
        for (auto [ScopedViewed, ScopedPosition] : ScopedRegistry.view<Position>().each())
        {
            (void)ScopedPosition;
            ScopedCommands.Emplace<Health>(Fby::Fcs::FromEngineEntity(ScopedViewed), Health{4});
        }
        Check(ScopedRegistry.try_get<Health>(ScopedEntity) == nullptr, "commands queued in a view stay deferred");
        ScopedCommands.Play();
        Check(ScopedRegistry.try_get<Health>(ScopedEntity) != nullptr, "commands apply after the view");
    }

    // A const registry yields const component references; writing through the
    // view is a compile error, not a runtime convention.
    {
        Fby::Fcs::Registry ScopedRegistry;
        const Fby::Entity ScopedEntity = ScopedRegistry.create();
        ScopedRegistry.emplace<Position>(ScopedEntity, Position{6.0f, 7.0f});
        const Fby::Fcs::Registry& ScopedConstRegistry = ScopedRegistry;
        bool ScopedSawConstView = false;
        for (const auto& [ScopedViewed, ScopedPosition] : ScopedConstRegistry.view<Position>().each())
        {
            static_assert(std::is_const_v<std::remove_reference_t<decltype(ScopedPosition)>>,
                          "a const registry view must yield const components");
            (void)ScopedViewed;
            (void)ScopedPosition;
            ScopedSawConstView = true;
        }
        Check(ScopedSawConstView, "const registry view iterates");
        const Position& ScopedConstPosition = ScopedConstRegistry.view<Position>().get<Position>(ScopedEntity);
        static_assert(std::is_const_v<std::remove_reference_t<decltype(ScopedConstPosition)>>,
                      "const registry view get returns a const component");
        Check(ScopedConstPosition.X == 6.0f, "const view reads the component");

        bool ScopedSawConstEach = false;
        const Fby::Fcs::World& ScopedConstWorld = ScopedRegistry.Data();
        ScopedConstWorld.ForEach<Position>(
            [&](Fby::Fcs::Entity, const Position& InPosition)
            {
                ScopedSawConstEach = InPosition.X == 6.0f;
            });
        Check(ScopedSawConstEach, "const World::ForEach yields const components");

        const std::uint64_t ScopedReadRevision = ScopedRegistry.Data().ModificationRevision();
        bool ScopedSawReadView = false;
        for (const auto& [ScopedViewed, ScopedPosition] : ScopedConstRegistry.view<Position>().each())
        {
            (void)ScopedViewed;
            (void)ScopedPosition;
            ScopedSawReadView = true;
        }
        Check(ScopedSawReadView, "const view iteration runs");
        Check(ScopedRegistry.Data().ModificationRevision() == ScopedReadRevision,
              "const iteration does not dirty the world");

        auto ScopedHintView = ScopedRegistry.view<Position>();
        Check(ScopedHintView.size_hint() == ScopedHintView.size(), "size_hint matches exact view size");
        const Fby::Fcs::Registry& ScopedConstHintRegistry = ScopedRegistry;
        auto ScopedConstHintView = ScopedConstHintRegistry.view<Position>();
        Check(ScopedConstHintView.size_hint() == ScopedConstHintView.size(), "const size_hint matches exact view size");

        for (auto [ScopedViewed, ScopedPosition] : ScopedRegistry.view<Position>().each())
        {
            (void)ScopedViewed;
            (void)ScopedPosition;
        }
        Check(ScopedRegistry.Data().ModificationRevision() > ScopedReadRevision,
              "mutable iteration bumps the modification revision");
    }

    // Clear keeps chunk storage, so the next Create must rebuild the entity-id
    // row before writing it. Regression for a PIE start/stop cycle.
    {
        Fby::Fcs::World ScopedCleared;
        const Fby::Fcs::Entity ScopedFirst = ScopedCleared.Create();
        ScopedCleared.Emplace<Position>(ScopedFirst, Position{1.0f, 1.0f});
        ScopedCleared.Clear();
        const Fby::Fcs::Entity ScopedSecond = ScopedCleared.Create();
        ScopedCleared.Emplace<Position>(ScopedSecond, Position{2.0f, 2.0f});
        Check(ScopedCleared.IsAlive(ScopedSecond), "an entity can be created after Clear");
    }

    if (Failures == 0)
    {
        std::printf("fcs_world_test: OK\n");
        return 0;
    }
    return 1;
}
