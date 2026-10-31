// FemboyEngine2D - deterministic rollback replay test.

#include "Engine/Core/DeterministicRng.hpp"
#include "Engine/ECS/CommandLog.hpp"
#include "Engine/ECS/Components.hpp"
#include "Engine/ECS/Snapshot.hpp"
#include "Engine/ECS/SpriteAnimation.hpp"
#include "Engine/Physics/Physics.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <span>
#include <vector>

namespace
{
using Fby::Entity;

int GFailures = 0;

#define FBY_CHECK(condition)                                                                                           \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);                                  \
            ++GFailures;                                                                                              \
        }                                                                                                              \
    } while (false)

constexpr float kFixedStep = 1.0f / 120.0f;
constexpr std::uint64_t kFinalTick = 240;

[[nodiscard]] Entity MakeEntity(std::uint32_t InId) noexcept
{
    return static_cast<Entity>(InId);
}

void SeedWorld(Fby::Ecs::Registry& InRegistry)
{
    {
        const Entity ScopedEntity = MakeEntity(1);
        (void)InRegistry.create(ScopedEntity);
        InRegistry.emplace<Fby::Transform2D>(ScopedEntity, Fby::Vector2D{10.0f, 20.0f}, Fby::Vector2D{1.0f, 1.0f},
                                             0.1f);
        InRegistry.emplace<Fby::Velocity>(ScopedEntity, Fby::Vector2D{3.0f, -1.0f});
        InRegistry.emplace<Fby::Sprite2D>(ScopedEntity);
    }
    {
        const Entity ScopedEntity = MakeEntity(2);
        (void)InRegistry.create(ScopedEntity);
        InRegistry.emplace<Fby::Transform2D>(ScopedEntity, Fby::Vector2D{-5.0f, 7.0f}, Fby::Vector2D{1.0f, 1.0f}, 0.0f);
        InRegistry.emplace<Fby::Velocity>(ScopedEntity, Fby::Vector2D{-2.0f, 4.0f});
        Fby::SpriteAnimationComponent ScopedSpriteAnimation;
        ScopedSpriteAnimation.StartFrame = 2;
        ScopedSpriteAnimation.FrameCount = 4;
        ScopedSpriteAnimation.FrameDuration = 0.1f;
        InRegistry.emplace<Fby::SpriteAnimationComponent>(ScopedEntity, ScopedSpriteAnimation);
    }
    {
        const Entity ScopedEntity = MakeEntity(3);
        (void)InRegistry.create(ScopedEntity);
        InRegistry.emplace<Fby::Transform2D>(ScopedEntity, Fby::Vector2D{100.0f, 100.0f}, Fby::Vector2D{1.0f, 1.0f},
                                             0.0f);
        Fby::RigidBody2D ScopedBody;
        ScopedBody.Position = {100.0f, 100.0f};
        ScopedBody.LinearVelocity = {2.0f, 0.0f};
        ScopedBody.AngularVelocity = 0.5f;
        ScopedBody.Type = Fby::BodyType::Dynamic;
        InRegistry.emplace<Fby::RigidBody2D>(ScopedEntity, ScopedBody);
        Fby::Collider2D ScopedCollider;
        ScopedCollider.HalfExtents = {8.0f, 8.0f};
        InRegistry.emplace<Fby::Collider2D>(ScopedEntity, ScopedCollider);
    }
    {
        // An entity with no snapshotted simulation component: the snapshot must
        const Entity ScopedEntity = MakeEntity(4);
        (void)InRegistry.create(ScopedEntity);
    }
}

void Simulate(Fby::Ecs::Registry& InRegistry, Fby::DeterministicRng& InRng)
{
    auto ScopedMovement = InRegistry.view<Fby::Transform2D, Fby::Velocity>();
    for (auto [ScopedEntity, ScopedTransform, ScopedVelocity] : ScopedMovement.each())
    {
        (void)ScopedEntity;
        ScopedTransform.Position += ScopedVelocity.Value * kFixedStep;
        ScopedVelocity.Value.X += InRng.RangeFloat(-0.05f, 0.05f);
        ScopedVelocity.Value.Y += InRng.RangeFloat(-0.05f, 0.05f) - 0.002f;
    }

    auto ScopedSpriteAnimation = InRegistry.view<Fby::SpriteAnimationComponent>();
    for (auto [ScopedEntity, ScopedComponent] : ScopedSpriteAnimation.each())
    {
        (void)ScopedEntity;
        (void)Fby::AdvanceSpriteAnimation(ScopedComponent, kFixedStep);
    }

    auto ScopedBodies = InRegistry.view<Fby::RigidBody2D>();
    for (auto [ScopedEntity, ScopedBody] : ScopedBodies.each())
    {
        (void)ScopedEntity;
        ScopedBody.Position += ScopedBody.LinearVelocity * kFixedStep;
        ScopedBody.LinearVelocity.Y += 0.5f * kFixedStep;
        ScopedBody.Rotation += ScopedBody.AngularVelocity * kFixedStep;
        ScopedBody.PreviousPosition = ScopedBody.Position;
        ScopedBody.PreviousRotation = ScopedBody.Rotation;
    }

    // Keep one component value moving for the delta test.
    const Entity ScopedEntity = MakeEntity(1);
    if (auto* ScopedTransform = InRegistry.try_get<Fby::Transform2D>(ScopedEntity))
    {
        ScopedTransform->Rotation += InRng.RangeFloat(-0.01f, 0.01f);
    }
}

void ScriptedCommands(std::uint64_t InTick, Fby::Ecs::Registry& InRegistry, Fby::Ecs::CommandLog* InLog,
                      const Fby::Ecs::SnapshotSchema& InSchema)
{
    (void)InSchema;
    if (InTick == 10)
    {
        const Entity ScopedEntity = MakeEntity(100);
        if (!InRegistry.valid(ScopedEntity))
        {
            (void)InRegistry.create(ScopedEntity);
        }
        if (InLog != nullptr)
        {
            FBY_CHECK(InLog->Spawn(ScopedEntity, InTick));
        }
        Fby::Transform2D ScopedTransform;
        ScopedTransform.Position = {42.0f, -17.0f};
        InRegistry.emplace_or_replace<Fby::Transform2D>(ScopedEntity, ScopedTransform);
        if (InLog != nullptr)
        {
            FBY_CHECK(InLog->AddComponent(ScopedEntity, ScopedTransform, InTick));
        }
        Fby::Velocity ScopedVelocity;
        ScopedVelocity.Value = {1.5f, 0.5f};
        InRegistry.emplace_or_replace<Fby::Velocity>(ScopedEntity, ScopedVelocity);
        if (InLog != nullptr)
        {
            FBY_CHECK(InLog->AddComponent(ScopedEntity, ScopedVelocity, InTick));
        }
        Fby::Sprite2D ScopedSprite;
        ScopedSprite.Size = {24.0f, 24.0f};
        ScopedSprite.Layer = 2;
        InRegistry.emplace_or_replace<Fby::Sprite2D>(ScopedEntity, ScopedSprite);
        if (InLog != nullptr)
        {
            FBY_CHECK(InLog->AddComponent(ScopedEntity, ScopedSprite, InTick));
        }
        Fby::DrawOrder ScopedOrder;
        ScopedOrder.Layer = 3;
        ScopedOrder.Depth = 0.5f;
        InRegistry.emplace_or_replace<Fby::DrawOrder>(ScopedEntity, ScopedOrder);
        if (InLog != nullptr)
        {
            FBY_CHECK(InLog->AddComponent(ScopedEntity, ScopedOrder, InTick));
        }
    }
    else if (InTick == 20)
    {
        const Entity ScopedEntity = MakeEntity(1);
        Fby::DrawOrder ScopedOrder;
        ScopedOrder.Layer = 9;
        ScopedOrder.Depth = 0.25f;
        InRegistry.emplace_or_replace<Fby::DrawOrder>(ScopedEntity, ScopedOrder);
        if (InLog != nullptr)
        {
            FBY_CHECK(InLog->AddComponent(ScopedEntity, ScopedOrder, InTick));
        }
    }
    else if (InTick == 30)
    {
        const Entity ScopedEntity = MakeEntity(2);
        InRegistry.remove<Fby::Velocity>(ScopedEntity);
        if (InLog != nullptr)
        {
            FBY_CHECK(InLog->RemoveComponent<Fby::Velocity>(ScopedEntity, InTick));
        }
    }
    else if (InTick == 40)
    {
        const Entity ScopedEntity = MakeEntity(3);
        if (InRegistry.valid(ScopedEntity))
        {
            InRegistry.destroy(ScopedEntity);
        }
        if (InLog != nullptr)
        {
            FBY_CHECK(InLog->Despawn(ScopedEntity, InTick));
        }
    }
    else if (InTick == 50)
    {
        const Entity ScopedEntity = MakeEntity(4);
        Fby::Lifetime ScopedLifetime;
        ScopedLifetime.SecondsRemaining = 3.5f;
        InRegistry.emplace_or_replace<Fby::Lifetime>(ScopedEntity, ScopedLifetime);
        if (InLog != nullptr)
        {
            FBY_CHECK(InLog->AddComponent(ScopedEntity, ScopedLifetime, InTick));
        }
    }
    else if (InTick == 60)
    {
        const Entity ScopedEntity = MakeEntity(4);
        InRegistry.remove<Fby::Lifetime>(ScopedEntity);
        if (InLog != nullptr)
        {
            FBY_CHECK(InLog->RemoveComponent<Fby::Lifetime>(ScopedEntity, InTick));
        }
    }
}

[[nodiscard]] std::vector<std::byte> Capture(Fby::Ecs::Registry& InRegistry, const Fby::Ecs::SnapshotSchema& InSchema,
                                             std::uint64_t InTick, const Fby::DeterministicRng& InRng)
{
    std::vector<std::byte> ScopedBuffer(Fby::Ecs::SnapshotSize(InRegistry, InSchema) + 4096);
    std::size_t ScopedWritten = 0;
    FBY_CHECK(Fby::Ecs::CaptureSnapshot(InRegistry, InSchema, ScopedBuffer, InTick, InRng, &ScopedWritten));
    ScopedBuffer.resize(ScopedWritten);
    return ScopedBuffer;
}

[[nodiscard]] bool SameBytes(std::span<const std::byte> InLeft, std::span<const std::byte> InRight)
{
    return InLeft.size() == InRight.size() && std::memcmp(InLeft.data(), InRight.data(), InLeft.size()) == 0;
}

void RunDeltaAndReplay(const Fby::Ecs::SnapshotSchema& InSchema)
{
    Fby::Ecs::Registry ScopedRegistry;
    SeedWorld(ScopedRegistry);
    Fby::DeterministicRng ScopedRng = Fby::DeterministicRng::FromSeed(0xC0FFEEu, 7u);

    std::vector<std::byte> ScopedBase(Fby::Ecs::SnapshotSize(ScopedRegistry, InSchema) + 4096);
    std::size_t ScopedBaseBytes = 0;
    FBY_CHECK(Fby::Ecs::CaptureSnapshot(ScopedRegistry, InSchema, ScopedBase, 0, ScopedRng, &ScopedBaseBytes));
    ScopedBase.resize(ScopedBaseBytes);

    Fby::Ecs::CommandLog ScopedLog;
    ScopedLog.Clear(0);
    for (std::uint64_t ScopedTick = 1; ScopedTick <= kFinalTick; ++ScopedTick)
    {
        ScriptedCommands(ScopedTick, ScopedRegistry, &ScopedLog, InSchema);
        Simulate(ScopedRegistry, ScopedRng);
    }

    // Spawn+4 adds, add, remove, despawn, add, remove = 10
    FBY_CHECK(ScopedLog.Count() == 10);
    const std::vector<std::byte> ScopedFull = Capture(ScopedRegistry, InSchema, kFinalTick, ScopedRng);
    FBY_CHECK(!ScopedFull.empty());

    // 1. Snapshot round-trip.
    {
        Fby::Ecs::Registry ScopedRestored;
        Fby::DeterministicRng ScopedRestoredRng;
        FBY_CHECK(Fby::Ecs::RestoreSnapshot(ScopedRestored, InSchema, ScopedFull, &ScopedRestoredRng));
        const std::vector<std::byte> ScopedAgain = Capture(ScopedRestored, InSchema, kFinalTick, ScopedRestoredRng);
        FBY_CHECK(SameBytes(ScopedFull, ScopedAgain));
        FBY_CHECK(ScopedRestoredRng.Save() == ScopedRng.Save());
        FBY_CHECK(ScopedRestored.valid(MakeEntity(4)));
        FBY_CHECK(!ScopedRestored.valid(MakeEntity(3)));
        FBY_CHECK(ScopedRestored.all_of<Fby::Transform2D>(MakeEntity(100)));
    }

    // 2. Base + command log replay.
    {
        Fby::Ecs::Registry ScopedReplayed;
        Fby::DeterministicRng ScopedReplayRng;
        FBY_CHECK(Fby::Ecs::RestoreSnapshot(ScopedReplayed, InSchema, ScopedBase, &ScopedReplayRng));
        Fby::Ecs::CommandLog::Cursor ScopedCursor;
        for (std::uint64_t ScopedTick = 1; ScopedTick <= kFinalTick; ++ScopedTick)
        {
            FBY_CHECK(ScopedLog.ApplyUntil(ScopedReplayed, InSchema, ScopedTick, ScopedCursor));
            Simulate(ScopedReplayed, ScopedReplayRng);
        }
        const std::vector<std::byte> ScopedReplayedBytes =
            Capture(ScopedReplayed, InSchema, kFinalTick, ScopedReplayRng);
        FBY_CHECK(SameBytes(ScopedFull, ScopedReplayedBytes));
        FBY_CHECK(ScopedCursor.commandIndex == ScopedLog.Count());
        FBY_CHECK(ScopedReplayRng.Save() == ScopedRng.Save());
    }

    // 3. Delta compression: base + delta must reconstruct the exact frame.
    {
        std::vector<std::byte> ScopedDelta(Fby::Ecs::SnapshotSize(ScopedRegistry, InSchema) +
                                           sizeof(Fby::Ecs::SnapshotHeader) + 4096);
        std::size_t ScopedDeltaBytes = 0;
        FBY_CHECK(Fby::Ecs::CaptureDelta(ScopedRegistry, InSchema, ScopedBase, ScopedDelta, kFinalTick, ScopedRng,
                                         &ScopedDeltaBytes));
        ScopedDelta.resize(ScopedDeltaBytes);
        // The test frame really is compressed
        FBY_CHECK(ScopedDelta.size() < ScopedFull.size());

        Fby::Ecs::Registry ScopedRestored;
        Fby::DeterministicRng ScopedRestoredRng;
        FBY_CHECK(Fby::Ecs::RestoreDelta(ScopedRestored, InSchema, ScopedBase, ScopedDelta, &ScopedRestoredRng));
        const std::vector<std::byte> ScopedAgain = Capture(ScopedRestored, InSchema, kFinalTick, ScopedRestoredRng);
        FBY_CHECK(SameBytes(ScopedFull, ScopedAgain));
        FBY_CHECK(ScopedRestoredRng.Save() == ScopedRng.Save());
    }

    // 4. A snapshot from a different component schema must be rejected before
    {
        Fby::Ecs::SnapshotSchema ScopedSubset;
        FBY_CHECK(ScopedSubset.Add<Fby::Transform2D>());
        Fby::Ecs::Registry ScopedOther;
        FBY_CHECK(!Fby::Ecs::RestoreSnapshot(ScopedOther, ScopedSubset, ScopedFull, nullptr));
    }

    // 5. A delta must be rejected when applied to the wrong base.
    {
        Fby::Ecs::Registry ScopedOther;
        SeedWorld(ScopedOther);
        Fby::DeterministicRng ScopedOtherRng = Fby::DeterministicRng::FromSeed(1u, 1u);
        for (std::uint64_t ScopedTick = 1; ScopedTick <= 20; ++ScopedTick)
        {
            Simulate(ScopedOther, ScopedOtherRng);
        }
        const std::vector<std::byte> ScopedWrongBase = Capture(ScopedOther, InSchema, 20, ScopedOtherRng);
        std::vector<std::byte> ScopedDelta(Fby::Ecs::SnapshotSize(ScopedRegistry, InSchema) +
                                           sizeof(Fby::Ecs::SnapshotHeader) + 4096);
        std::size_t ScopedDeltaBytes = 0;
        FBY_CHECK(Fby::Ecs::CaptureDelta(ScopedRegistry, InSchema, ScopedBase, ScopedDelta, kFinalTick, ScopedRng,
                                         &ScopedDeltaBytes));
        ScopedDelta.resize(ScopedDeltaBytes);
        Fby::Ecs::Registry ScopedRejected;
        FBY_CHECK(!Fby::Ecs::RestoreDelta(ScopedRejected, InSchema, ScopedWrongBase, ScopedDelta, nullptr));
    }
}

void RunLogStability(const Fby::Ecs::SnapshotSchema& InSchema)
{
    auto ScopedBuild = [&InSchema](std::vector<std::byte>& Out)
    {
        Fby::Ecs::Registry ScopedRegistry;
        SeedWorld(ScopedRegistry);
        Fby::Ecs::CommandLog ScopedLog;
        ScopedLog.Clear(0);
        Fby::DeterministicRng ScopedRng = Fby::DeterministicRng::FromSeed(1u, 2u);
        for (std::uint64_t ScopedTick = 1; ScopedTick <= kFinalTick; ++ScopedTick)
        {
            ScriptedCommands(ScopedTick, ScopedRegistry, &ScopedLog, InSchema);
            Simulate(ScopedRegistry, ScopedRng);
        }
        const std::span<const std::byte> ScopedBytes = ScopedLog.Bytes();
        Out.assign(ScopedBytes.begin(), ScopedBytes.end());
    };

    std::vector<std::byte> ScopedFirst;
    std::vector<std::byte> ScopedSecond;
    ScopedBuild(ScopedFirst);
    ScopedBuild(ScopedSecond);
    FBY_CHECK(SameBytes(ScopedFirst, ScopedSecond));

    Fby::Ecs::CommandLog::View ScopedView = Fby::Ecs::CommandLog::View::Open(ScopedFirst, nullptr);
    FBY_CHECK(ScopedView.header.version == Fby::Ecs::kCommandLogVersion);
    FBY_CHECK(ScopedView.header.commandCount == 10);
}
}

int main()
{
    const Fby::Ecs::SnapshotSchema ScopedSchema = Fby::Ecs::SnapshotSchema::BuiltIns();
    FBY_CHECK(ScopedSchema.Size() >= 12);
    FBY_CHECK(ScopedSchema.Find("Transform2D") != nullptr);
    FBY_CHECK(ScopedSchema.Find("RigidBody2D") != nullptr);

    RunLogStability(ScopedSchema);
    RunDeltaAndReplay(ScopedSchema);

    if (GFailures != 0)
    {
        std::fprintf(stderr, "rollback replay: %d failure(s)\n", GFailures);
        return 1;
    }
    std::printf("rollback replay: OK (snapshot bytes, command log, delta, RNG)\n");
    return 0;
}
