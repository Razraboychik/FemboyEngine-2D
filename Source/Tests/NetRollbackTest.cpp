// FemboyEngine2D - deterministic network rollback acceptance test.
#include "Engine/Network/NetRollback.hpp"

#include "Engine/ECS/CommandLog.hpp"
#include "Engine/ECS/Components.hpp"
#include "Engine/ECS/Snapshot.hpp"
#include "Engine/Physics/Physics.hpp"
#include "Engine/Physics/Particles.hpp"

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <span>
#include <type_traits>
#include <vector>

namespace
{
int GFailures = 0;
std::atomic<std::size_t> GAllocations{0};

void Check(bool InCondition, const char* InMessage)
{
    if (!InCondition)
    {
        std::fprintf(stderr, "FAIL: %s\n", InMessage);
        ++GFailures;
    }
}

using Fby::Entity;
using Fby::GameplayState;
using Fby::GameplayStateBit;
using Fby::GameplayStateFlag;
using Fby::Pcg32;
using Fby::Transform2D;
using Fby::Vector2D;
using Fby::Velocity;
using Fby::Ecs::CommandLog;
using Fby::Ecs::Registry;
using Fby::Ecs::SnapshotSchema;
using Fby::Net::InputRingBuffer;
using Fby::Net::kInputRingCapacity;
using Fby::Net::NetEntityId;
using Fby::Net::NetEntityMap;
using Fby::Net::NetInputFrame;
using Fby::Net::NetPlayerId;
using Fby::Net::NetRollbackSession;
using Fby::Net::NetTickInputs;
using Fby::Net::RollbackReport;

constexpr float kStep = 1.0f / 60.0f;

struct TestWorld
{
    std::array<Entity, 2> players{entt::null, entt::null};
};

Entity CreatePlayer(Registry& InRegistry, Vector2D InPosition)
{
    const Entity ScopedEntity = InRegistry.create();
    Transform2D ScopedTransform;
    ScopedTransform.Position = InPosition;
    InRegistry.emplace<Transform2D>(ScopedEntity, ScopedTransform);
    InRegistry.emplace<Velocity>(ScopedEntity, Velocity{Vector2D{0.0f, 0.0f}});
    InRegistry.emplace<GameplayState>(ScopedEntity, GameplayState{});
    return ScopedEntity;
}

TestWorld CreateWorld(Registry& InRegistry)
{
    TestWorld ScopedWorld;
    ScopedWorld.players[0] = CreatePlayer(InRegistry, Vector2D{0.0f, 0.0f});
    ScopedWorld.players[1] = CreatePlayer(InRegistry, Vector2D{100.0f, 0.0f});
    return ScopedWorld;
}

void Simulate(void* InUser, Registry& InRegistry, std::uint64_t InTick, const NetTickInputs& InInputs,
              Pcg32& InRng) noexcept
{
    auto* ScopedWorld = static_cast<TestWorld*>(InUser);
    for (std::uint32_t ScopedPlayer = 0; ScopedPlayer < 2; ++ScopedPlayer)
    {
        const Entity ScopedEntity = ScopedWorld->players[ScopedPlayer];
        if (!InRegistry.valid(ScopedEntity))
        {
            continue;
        }
        const NetInputFrame& ScopedInput = InInputs.Player(ScopedPlayer);
        auto& ScopedVelocity = InRegistry.get<Velocity>(ScopedEntity);
        auto& ScopedState = InRegistry.get<GameplayState>(ScopedEntity);
        ScopedVelocity.Value = Vector2D{ScopedInput.AxisX() * 180.0f, ScopedInput.AxisY() * 180.0f};

        ScopedState.Bits = (InTick & 1u) != 0 ? GameplayStateBit(GameplayStateFlag::Grounded)
                                              : GameplayStateBit(GameplayStateFlag::Airborne);
        if (ScopedInput.Button(1u))
        {
            ScopedState.Set(GameplayStateFlag::Jumping);
        }
        if (ScopedInput.moveX < 0)
        {
            ScopedState.Set(GameplayStateFlag::Dashing);
        }
        if ((InTick + ScopedPlayer) % 5u == 0)
        {
            ScopedState.Set(GameplayStateFlag::Invulnerable);
        }
        InRegistry.get<Transform2D>(ScopedEntity).Rotation += InRng.RangeFloat(-0.003f, 0.003f);
    }

    // The CommandLog test spawns an extra body mid-simulation. It has no player
    auto ScopedBodies = InRegistry.view<Transform2D, Velocity>();
    for (const Entity ScopedEntity : ScopedBodies)
    {
        auto& ScopedTransform = ScopedBodies.get<Transform2D>(ScopedEntity);
        const auto& ScopedVelocity = ScopedBodies.get<Velocity>(ScopedEntity);
        ScopedTransform.Position += ScopedVelocity.Value * kStep;
    }
}

NetInputFrame MakeLocal(std::uint64_t InTick)
{
    NetInputFrame ScopedFrame;
    ScopedFrame.tick = InTick;
    ScopedFrame.moveX = static_cast<std::int16_t>(static_cast<int>(InTick) * 137 - 900);
    ScopedFrame.moveY = static_cast<std::int16_t>(static_cast<int>(InTick) * 41 - 300);
    ScopedFrame.buttons = (InTick % 3u) == 0 ? 1u : 0u;
    ScopedFrame.sequence = static_cast<std::uint16_t>(InTick);
    return ScopedFrame;
}

NetInputFrame MakeRemote(std::uint64_t InTick, bool InCorrected)
{
    NetInputFrame ScopedFrame;
    ScopedFrame.tick = InTick;
    ScopedFrame.moveX = static_cast<std::int16_t>((InCorrected ? -1 : 1) * static_cast<int>(InTick) * 1000);
    ScopedFrame.moveY = static_cast<std::int16_t>((InCorrected ? -1 : 1) * static_cast<int>(InTick) * 500);
    ScopedFrame.buttons = InCorrected ? 2u : 4u;
    ScopedFrame.sequence = static_cast<std::uint16_t>(InTick);
    return ScopedFrame;
}

constexpr std::uint64_t kTicks = 12;
constexpr std::uint32_t kRollbackWindow = 8;
constexpr std::uint32_t kSnapshotBytes = 8192;
using Arena = std::array<std::byte, kSnapshotBytes * kRollbackWindow>;

// std::array<...> as a named type keeps Run addressable but not copyable; the
struct SandboxArena
{
    Arena bytes{};
};

struct Run
{
    Registry registry;
    TestWorld world;
    SandboxArena arena;
    NetRollbackSession session;
    std::array<std::uint64_t, kTicks + 1> snapshots{};
    std::array<std::uint64_t, kTicks + 1> states{};
};

[[nodiscard]] bool InitializeRun(Run& InRun, CommandLog* InLog = nullptr,
                                 std::uint64_t InSeed = 0xFEEDFACEull)
{
    InRun.world = CreateWorld(InRun.registry);
    NetRollbackSession::Config ScopedConfig;
    ScopedConfig.playerCount = 2;
    ScopedConfig.localPlayer = 0;
    ScopedConfig.rollbackTicks = kRollbackWindow;
    ScopedConfig.snapshotBytes = kSnapshotBytes;
    ScopedConfig.startTick = 0;

    if (InRun.session.Initialize(InRun.registry, SnapshotSchema::BuiltIns(), ScopedConfig,
                                 std::span<std::byte>(InRun.arena.bytes.data(), InRun.arena.bytes.size())) !=
        NetRollbackSession::Result::Ok)
    {
        return false;
    }
    InRun.session.SetSimulation(&Simulate, &InRun.world);
    InRun.session.SetCommandLog(InLog);
    if (InRun.session.RegisterEntity(InRun.world.players[0], NetEntityId{1}) != NetEntityId{1})
    {
        return false;
    }
    if (InRun.session.RegisterEntity(InRun.world.players[1], NetEntityId{2}) != NetEntityId{2})
    {
        return false;
    }
    if (InRun.session.Start(Pcg32::FromSeed(InSeed, 77u)) != NetRollbackSession::Result::Ok)
    {
        return false;
    }
    InRun.snapshots[0] = InRun.session.SnapshotChecksumAt(0);
    InRun.states[0] = InRun.session.StateChecksum();
    return true;
}

void RunLive(std::uint64_t InTick, Run& InLive, bool InSubmitRemote, bool InCorrected)
{
    if (InSubmitRemote)
    {
        Check(InLive.session.SubmitRemoteInput(NetPlayerId{1}, MakeRemote(InTick, InCorrected)) ==
                  NetRollbackSession::Result::Ok,
              "live remote input submitted");
    }
    Check(InLive.session.Step(MakeLocal(InTick)) == NetRollbackSession::Result::Ok, "live step succeeds");
    InLive.snapshots[InTick] = InLive.session.SnapshotChecksumAt(InTick);
    InLive.states[InTick] = InLive.session.StateChecksum();
}

void RunReference(Run& InReference, std::uint64_t InFromTick)
{
    for (std::uint64_t ScopedTick = 1; ScopedTick <= kTicks; ++ScopedTick)
    {
        const bool ScopedCorrected = ScopedTick >= InFromTick;
        Check(InReference.session.SubmitRemoteInput(NetPlayerId{1}, MakeRemote(ScopedTick, ScopedCorrected)) ==
                  NetRollbackSession::Result::Ok,
              "reference remote input submitted");
        Check(InReference.session.Step(MakeLocal(ScopedTick)) == NetRollbackSession::Result::Ok,
              "reference step succeeds");
        InReference.snapshots[ScopedTick] = InReference.session.SnapshotChecksumAt(ScopedTick);
        InReference.states[ScopedTick] = InReference.session.StateChecksum();
    }
}

void TestInputRing()
{
    InputRingBuffer ScopedRing;
    NetInputFrame ScopedFrame;
    ScopedFrame.tick = 10;
    ScopedFrame.moveX = 100;
    Check(ScopedRing.Push(ScopedFrame), "push first input");
    ScopedFrame.tick = 11;
    ScopedFrame.moveX = 200;
    Check(ScopedRing.Push(ScopedFrame), "push second input");
    Check(ScopedRing.Find(10) != nullptr && ScopedRing.Find(10)->moveX == 100, "find exact tick");
    Check(ScopedRing.LatestAtOrBefore(11) != nullptr && ScopedRing.LatestAtOrBefore(11)->moveX == 200,
          "latest at or before returns the newest sample");
    Check(ScopedRing.LatestAtOrBefore(9) == nullptr, "prediction has no sample before the first pushed tick");
    Check(ScopedRing.NewestTick() == 11 && ScopedRing.OldestTick() == 10, "ring bounds are tracked");

    // A late correction for an already-known tick overwrites in place.
    ScopedFrame.tick = 10;
    ScopedFrame.moveX = -100;
    Check(ScopedRing.Push(ScopedFrame), "late correction overwrites the tick");
    Check(ScopedRing.Find(10) != nullptr && ScopedRing.Find(10)->moveX == -100, "late correction is visible");

    ScopedFrame.tick = 11 + kInputRingCapacity + 1;
    Check(!ScopedRing.Push(ScopedFrame), "far-future tick is rejected");

    // Ticks whose window arithmetic could wrap are rejected outright.
    ScopedFrame.tick = Fby::Net::kInvalidNetTick - 1;
    Check(!ScopedRing.Push(ScopedFrame), "tick near the sentinel is rejected");

    // A fresh ring establishes a window at tick 300; tick 1 is now more than a
    ScopedRing.Reset();
    ScopedFrame.tick = 300;
    Check(ScopedRing.Push(ScopedFrame), "window anchor accepted");
    ScopedFrame.tick = 1;
    Check(!ScopedRing.Push(ScopedFrame), "too-old tick is rejected");

    ScopedRing.Reset();
    Check(ScopedRing.Count() == 0 && ScopedRing.Find(10) == nullptr, "reset clears the ring");
}

void TestFrameDigest()
{
    // The same world captured with a different RNG state must produce a
    // different frame digest; the payload-only checksum would not see it.
    Run ScopedFirst;
    Run ScopedSecond;
    Check(InitializeRun(ScopedFirst), "digest run A initializes");
    Check(InitializeRun(ScopedSecond, nullptr, 0x12345678ull), "digest run B initializes");
    Check(ScopedFirst.snapshots[0] != ScopedSecond.snapshots[0],
          "frame digest covers the RNG state and tick");
}

void TestFailedReplayClearsPending()
{
    Run ScopedRun;
    Check(InitializeRun(ScopedRun), "pending-reset run initializes");
    for (std::uint64_t ScopedTick = 1; ScopedTick <= 5; ++ScopedTick)
    {
        Check(ScopedRun.session.Step(MakeLocal(ScopedTick)) == NetRollbackSession::Result::Ok,
              "pending-reset step succeeds");
    }
    Check(ScopedRun.session.SubmitRemoteInput(NetPlayerId{1}, MakeRemote(3, true)) ==
              NetRollbackSession::Result::Ok,
          "pending-reset remote sample accepted");

    // A rejected snapshot must not destroy the frame's native bodies.
    Fby::PhysicsSystem ScopedPhysics;    Fby::PhysicsDesc ScopedPhysicsDesc;
    ScopedPhysicsDesc.gravity = Vector2D{0.0f, 0.0f};
    Check(ScopedPhysics.Initialize(ScopedPhysicsDesc), "pending-reset physics initializes");
    ScopedRun.session.SetPhysics(&ScopedPhysics);
    const Entity ScopedBodyEntity = ScopedRun.registry.create();
    ScopedRun.registry.emplace<Transform2D>(ScopedBodyEntity,
                                            Transform2D{Vector2D{0.0f, 0.0f}, Vector2D{1.0f, 1.0f}, 0.0f});
    ScopedRun.registry.emplace<Fby::RigidBody2D>(ScopedBodyEntity, Fby::RigidBody2D{});
    ScopedRun.registry.emplace<Fby::Collider2D>(ScopedBodyEntity, Fby::Collider2D{});
    ScopedPhysics.EnsureBodies(ScopedRun.registry);
    Check(ScopedRun.registry.get<Fby::RigidBody2D>(ScopedBodyEntity).Body != 0, "pending-reset body exists");

    // Corrupt the base snapshot (tick 2) so the replay fails in RestoreSnapshot.
    ScopedRun.arena.bytes[2u * kSnapshotBytes] ^= std::byte{0xFF};
    Check(ScopedRun.session.ReplayPending() == NetRollbackSession::Result::RestoreFailed,
          "corrupt base snapshot fails the replay");
    Check(ScopedRun.registry.get<Fby::RigidBody2D>(ScopedBodyEntity).Body != 0,
          "rejected restore keeps the native body");
    Check(ScopedRun.session.ReplayPending() == NetRollbackSession::Result::NoRollbackPending,
          "failed replay clears the pending tick");
    ScopedPhysics.Shutdown();
}

void TestFarFutureFirstInput()
{
    Run ScopedRun;
    Check(InitializeRun(ScopedRun), "far-future run initializes");
    Check(ScopedRun.session.SubmitRemoteInput(NetPlayerId{1}, MakeRemote(1000, false)) ==
              NetRollbackSession::Result::InvalidTick,
          "a far-future first remote sample is rejected");
}

void TestLocalOnlyStateSkipped()
{
    Run ScopedRun;
    Check(InitializeRun(ScopedRun), "local-state run initializes");
    const std::uint64_t ScopedBefore = ScopedRun.session.StateChecksum();
    const Entity ScopedLocal = ScopedRun.registry.create();
    ScopedRun.registry.emplace<GameplayState>(ScopedLocal, GameplayState{});
    ScopedRun.registry.get<GameplayState>(ScopedLocal).Bits = 0x1234u;
    Check(ScopedRun.session.StateChecksum() == ScopedBefore,
          "local-only gameplay state stays out of the network checksum");
}

void TestEntityMap()
{
    NetEntityMap ScopedMap;
    const Entity ScopedFirst = static_cast<Entity>(17u);
    const Entity ScopedSecond = static_cast<Entity>(18u);
    Check(ScopedMap.Register(NetEntityId{7}, ScopedFirst), "register first mapping");
    Check(ScopedMap.Register(NetEntityId{8}, ScopedSecond), "register second mapping");
    Check(ScopedMap.Resolve(NetEntityId{7}) == ScopedFirst, "resolve net id");
    Check(ScopedMap.IdOf(ScopedSecond) == NetEntityId{8}, "reverse lookup");
    Check(!ScopedMap.Register(NetEntityId{8}, ScopedFirst), "conflicting net id rejected");
    Check(!ScopedMap.Register(NetEntityId{9}, ScopedFirst), "conflicting local entity rejected");
    ScopedMap.UnregisterNet(NetEntityId{7});
    Check(ScopedMap.Resolve(NetEntityId{7}) == entt::null, "unregister removes net mapping");
    Check(!ScopedMap.IdOf(ScopedFirst).IsValid(), "unregister removes local mapping");
    Check(ScopedMap.Register(NetEntityId{7}, ScopedFirst), "a freed net id can be reused");
}

void TestGameplayStateComponent()
{
    static_assert(sizeof(GameplayState) == 8);
    static_assert(std::is_trivially_copyable_v<GameplayState>);
    const SnapshotSchema ScopedSchema = SnapshotSchema::BuiltIns();
    Check(ScopedSchema.Find("GameplayState") != nullptr, "GameplayState is in the snapshot schema");
    GameplayState ScopedState;
    ScopedState.Set(GameplayStateFlag::Grounded);
    ScopedState.Set(GameplayStateFlag::Attacking);
    Check(ScopedState.Has(GameplayStateFlag::Grounded), "state bit set");
    Check(ScopedState.HasAll(GameplayStateBit(GameplayStateFlag::Grounded) |
                             GameplayStateBit(GameplayStateFlag::Attacking)),
          "HasAll checks a mask");
    Check(!ScopedState.Has(GameplayStateFlag::Dead), "unset state bit is clear");
    ScopedState.Clear(GameplayStateFlag::Attacking);
    Check(!ScopedState.Has(GameplayStateFlag::Attacking), "state bit cleared");
    ScopedState.Toggle(GameplayStateFlag::Dead);
    Check(ScopedState.Has(GameplayStateFlag::Dead), "state bit toggled");
}

void TestPredictionAndRollback()
{
    // Use one shared frame buffer type but never copy a Run: the session holds a
    auto ScopedLive = std::make_unique<Run>();
    auto ScopedReference = std::make_unique<Run>();
    Check(InitializeRun(*ScopedLive), "live session initializes");
    Check(InitializeRun(*ScopedReference), "reference session initializes");

    // Wrong remote input for ticks 1..8, nothing for 9..12 (the session predicts).
    for (std::uint64_t ScopedTick = 1; ScopedTick <= kTicks; ++ScopedTick)
    {
        RunLive(ScopedTick, *ScopedLive, ScopedTick <= 8, false);
    }
    Check(!ScopedLive->session.Inputs(NetPlayerId{1}).Has(9), "tick 9 remote input is still missing");

    RunReference(*ScopedReference, 6);
    Check(ScopedLive->snapshots[kTicks] != ScopedReference->snapshots[kTicks],
          "the wrongly predicted live frame differs from the reference");

    const std::array<std::uint64_t, 6> ScopedUntouched{ScopedLive->snapshots[0], ScopedLive->snapshots[1],
                                                       ScopedLive->snapshots[2], ScopedLive->snapshots[3],
                                                       ScopedLive->snapshots[4], ScopedLive->snapshots[5]};

    // Late packet for tick 6. Everything from 6 to 12 must be re-simulated.
    Check(ScopedLive->session.SubmitRemoteInput(NetPlayerId{1}, MakeRemote(6, true)) == NetRollbackSession::Result::Ok,
          "late input accepted");
    Check(ScopedLive->session.RollbackPending(), "late input requests a rollback");
    Check(ScopedLive->session.PendingRollbackTick() == 6, "rollback starts at the corrected tick");
    RollbackReport ScopedReport;
    Check(ScopedLive->session.ReplayPending(&ScopedReport) == NetRollbackSession::Result::Ok,
          "rollback replay succeeds");
    Check(ScopedReport.fromTick == 6 && ScopedReport.toTick == kTicks, "report spans the affected window");
    Check(ScopedReport.replayedTicks == kTicks - 6 + 1, "every affected tick was replayed");
    Check(ScopedReport.changedTicks > 0, "the correction changed at least one frame");
    Check(ScopedReport.firstChangedTick >= 6, "no frame before the correction changed");
    Check(!ScopedLive->session.RollbackPending(), "pending rollback is consumed");

    for (std::uint32_t ScopedTick = 0; ScopedTick < ScopedUntouched.size(); ++ScopedTick)
    {
        Check(ScopedLive->snapshots[ScopedTick] == ScopedUntouched[ScopedTick],
              "frames before the correction are byte-identical");
    }

    // Duplicate retransmit of the same packet must be a no-op.
    Check(ScopedLive->session.SubmitRemoteInput(NetPlayerId{1}, MakeRemote(6, true)) == NetRollbackSession::Result::Ok,
          "duplicate retransmit accepted");
    Check(!ScopedLive->session.RollbackPending(), "duplicate retransmit does not roll back");

    // Correct the remaining remote ticks. Ticks 9..12 were predictions, so the
    for (std::uint64_t ScopedTick = 7; ScopedTick <= kTicks; ++ScopedTick)
    {
        Check(ScopedLive->session.SubmitRemoteInput(NetPlayerId{1}, MakeRemote(ScopedTick, true)) ==
                  NetRollbackSession::Result::Ok,
              "remaining late input accepted");
    }
    Check(ScopedLive->session.PendingRollbackTick() == 7, "rollback starts at the earliest pending tick");
    ScopedReport = {};
    Check(ScopedLive->session.ReplayPending(&ScopedReport) == NetRollbackSession::Result::Ok, "second replay succeeds");
    Check(ScopedReport.replayedTicks == kTicks - 7 + 1, "second replay covers 7..12");

    // The live session is now identical to a reference that never predicted.
    Fby::Net::FrameInfo ScopedLiveInfo;
    Fby::Net::FrameInfo ScopedReferenceInfo;
    Check(ScopedLive->session.FrameInfoAt(kTicks, ScopedLiveInfo), "live final frame exists");
    Check(ScopedReference->session.FrameInfoAt(kTicks, ScopedReferenceInfo), "reference final frame exists");
    Check(ScopedLiveInfo.snapshotChecksum == ScopedReferenceInfo.snapshotChecksum,
          "final snapshot checksum matches reference");
    Check(ScopedLiveInfo.stateChecksum == ScopedReferenceInfo.stateChecksum,
          "final gameplay-state checksum matches reference");
    Check(ScopedLive->session.CompareChecksums(kTicks, ScopedReferenceInfo.snapshotChecksum,
                                               ScopedReferenceInfo.stateChecksum),
          "CompareChecksums accepts the reference frame");
    Check(!ScopedLive->session.CompareChecksums(kTicks, ScopedReferenceInfo.snapshotChecksum ^ 1u,
                                                ScopedReferenceInfo.stateChecksum),
          "CompareChecksums rejects a tampered snapshot checksum");

    Fby::Net::NetRollbackSession::ChecksumComparison ScopedComparison;
    Check(!ScopedLive->session.CompareChecksums(kTicks, ScopedReferenceInfo.snapshotChecksum,
                                                ScopedReferenceInfo.stateChecksum ^ 0x8000000000000000ull,
                                                &ScopedComparison),
          "CompareChecksums rejects a tampered state checksum");
    Check(!ScopedComparison.stateMatches && ScopedComparison.remoteState != ScopedComparison.localState,
          "comparison exposes both state checksums");
    Check(ScopedComparison.snapshotMatches, "snapshot checksum still matches in a state-only mismatch");

    // Entity ids survived the rollback: local handles and network ids still meet.
    Check(ScopedLive->session.ResolveEntity(NetEntityId{1}) == ScopedLive->world.players[0],
          "net id still resolves after rollback");
    Check(ScopedLive->session.NetIdOf(ScopedLive->world.players[1]) == NetEntityId{2},
          "local id still maps after rollback");
    Check(ScopedLive->registry.valid(ScopedLive->world.players[0]), "player 0 survived restore");

    // An input older than the snapshot window cannot be replayed.
    Check(ScopedLive->session.SubmitRemoteInput(NetPlayerId{1}, MakeRemote(1, true)) ==
              NetRollbackSession::Result::RollbackTooOld,
          "too-old late input is refused");
}

void TestCommandLogRollback()
{
    auto ScopedLive = std::make_unique<Run>();
    auto ScopedReference = std::make_unique<Run>();

    CommandLog ScopedLog;
    ScopedLog.Clear(0);
    const Entity ScopedSpawned = static_cast<Entity>(500u);
    Check(ScopedLog.Spawn(ScopedSpawned, 6), "command log spawn recorded");
    Transform2D ScopedSpawnedTransform;
    ScopedSpawnedTransform.Position = Vector2D{250.0f, -40.0f};
    Check(ScopedLog.AddComponent<Transform2D>(ScopedSpawned, ScopedSpawnedTransform, 6),
          "command log transform recorded");
    Velocity ScopedSpawnedVelocity;
    ScopedSpawnedVelocity.Value = Vector2D{12.0f, -3.0f};
    Check(ScopedLog.AddComponent<Velocity>(ScopedSpawned, ScopedSpawnedVelocity, 6), "command log velocity recorded");
    GameplayState ScopedSpawnedState;
    ScopedSpawnedState.Set(GameplayStateFlag::Frozen);
    Check(ScopedLog.AddComponent<GameplayState>(ScopedSpawned, ScopedSpawnedState, 6), "command log state recorded");

    Check(InitializeRun(*ScopedLive, &ScopedLog), "live command-log session initializes");
    Check(InitializeRun(*ScopedReference, &ScopedLog), "reference command-log session initializes");

    for (std::uint64_t ScopedTick = 1; ScopedTick <= kTicks; ++ScopedTick)
    {
        Check(ScopedLive->session.SubmitRemoteInput(NetPlayerId{1}, MakeRemote(ScopedTick, false)) ==
                  NetRollbackSession::Result::Ok,
              "command-log live input accepted");
        Check(ScopedLive->session.Step(MakeLocal(ScopedTick)) == NetRollbackSession::Result::Ok,
              "command-log step succeeds");
    }
    Check(ScopedLive->registry.valid(ScopedSpawned), "CommandLog spawned the entity at tick 6");
    Check(ScopedLive->registry.all_of<Transform2D, Velocity, GameplayState>(ScopedSpawned),
          "spawned entity has its components");

    // Correct tick 6: the snapshot at tick 5 predates the spawn, so replay must
    Check(ScopedLive->session.SubmitRemoteInput(NetPlayerId{1}, MakeRemote(6, true)) == NetRollbackSession::Result::Ok,
          "command-log correction accepted");
    Check(ScopedLive->session.ReplayPending() == NetRollbackSession::Result::Ok, "command-log replay succeeds");
    Check(ScopedLive->registry.valid(ScopedSpawned), "CommandLog re-spawned the entity during replay");
    Check(ScopedLive->registry.get<GameplayState>(ScopedSpawned).Has(GameplayStateFlag::Frozen),
          "replayed component payload is intact");

    // Correct the remaining remote ticks as well, then replay once more with the
    for (std::uint64_t ScopedTick = 7; ScopedTick <= kTicks; ++ScopedTick)
    {
        Check(ScopedLive->session.SubmitRemoteInput(NetPlayerId{1}, MakeRemote(ScopedTick, true)) ==
                  NetRollbackSession::Result::Ok,
              "command-log remaining input accepted");
    }
    Check(ScopedLive->session.ReplayPending() == NetRollbackSession::Result::Ok, "command-log second replay succeeds");

    // Reference sees the spawn too and gets the corrected input from tick 6.
    for (std::uint64_t ScopedTick = 1; ScopedTick <= kTicks; ++ScopedTick)
    {
        Check(ScopedReference->session.SubmitRemoteInput(NetPlayerId{1}, MakeRemote(ScopedTick, ScopedTick >= 6)) ==
                  NetRollbackSession::Result::Ok,
              "command-log reference input accepted");
        Check(ScopedReference->session.Step(MakeLocal(ScopedTick)) == NetRollbackSession::Result::Ok,
              "command-log reference step succeeds");
    }
    Fby::Net::FrameInfo ScopedLiveInfo;
    Fby::Net::FrameInfo ScopedReferenceInfo;
    Check(ScopedLive->session.FrameInfoAt(kTicks, ScopedLiveInfo), "command-log live frame exists");
    Check(ScopedReference->session.FrameInfoAt(kTicks, ScopedReferenceInfo), "command-log reference frame exists");
    Check(ScopedLiveInfo.snapshotChecksum == ScopedReferenceInfo.snapshotChecksum,
          "CommandLog + snapshot rollback reproduces the reference bytes");
    Check(ScopedLiveInfo.stateChecksum == ScopedReferenceInfo.stateChecksum,
          "CommandLog + snapshot rollback reproduces the state-tag checksum");
}

void TestNoAllocationAfterStart()
{
    auto ScopedRun = std::make_unique<Run>();
    Check(InitializeRun(*ScopedRun), "allocation test session initializes");
    for (std::uint64_t ScopedTick = 1; ScopedTick <= 6; ++ScopedTick)
    {
        Check(ScopedRun->session.SubmitRemoteInput(NetPlayerId{1}, MakeRemote(ScopedTick, false)) ==
                  NetRollbackSession::Result::Ok,
              "allocation test input accepted");
        Check(ScopedRun->session.Step(MakeLocal(ScopedTick)) == NetRollbackSession::Result::Ok,
              "allocation test warmup step");
    }

    // Warm every storage the replay will touch, then count allocations around
    Check(ScopedRun->session.SubmitRemoteInput(NetPlayerId{1}, MakeRemote(5, true)) == NetRollbackSession::Result::Ok,
          "allocation test correction accepted");
    Check(ScopedRun->session.ReplayPending() == NetRollbackSession::Result::Ok, "allocation test warmup replay");
    Check(ScopedRun->session.SubmitRemoteInput(NetPlayerId{1}, MakeRemote(5, false)) == NetRollbackSession::Result::Ok,
          "allocation test restore input accepted");
    Check(ScopedRun->session.ReplayPending() == NetRollbackSession::Result::Ok, "allocation test restore replay");

    const std::size_t ScopedBefore = GAllocations.load(std::memory_order_relaxed);
    Check(ScopedRun->session.SubmitRemoteInput(NetPlayerId{1}, MakeRemote(5, true)) == NetRollbackSession::Result::Ok,
          "allocation test live correction accepted");
    Check(ScopedRun->session.ReplayPending() == NetRollbackSession::Result::Ok, "allocation test replay");
    Check(ScopedRun->session.SubmitRemoteInput(NetPlayerId{1}, MakeRemote(7, false)) == NetRollbackSession::Result::Ok,
          "allocation test future input accepted");
    Check(ScopedRun->session.Step(MakeLocal(7)) == NetRollbackSession::Result::Ok, "allocation test step");
    const std::size_t ScopedAfter = GAllocations.load(std::memory_order_relaxed);
    Check(ScopedAfter == ScopedBefore, "Step/Replay/SubmitRemoteInput perform zero heap allocations");
}

// A restore must destroy the native Box2D bodies of the old frame before
void TestPhysicsBodySweep()
{
    Registry ScopedRegistry;
    const SnapshotSchema ScopedSchema = SnapshotSchema::BuiltIns();

    const Entity ScopedEntity = ScopedRegistry.create();
    ScopedRegistry.emplace<Transform2D>(ScopedEntity, Transform2D{Vector2D{10.0f, 20.0f}, Vector2D{1.0f, 1.0f}, 0.0f});
    Fby::RigidBody2D ScopedBody;
    ScopedBody.Type = Fby::BodyType::Dynamic;
    ScopedRegistry.emplace<Fby::RigidBody2D>(ScopedEntity, ScopedBody);
    Fby::Collider2D ScopedCollider;
    ScopedCollider.HalfExtents = Vector2D{8.0f, 8.0f};
    ScopedRegistry.emplace<Fby::Collider2D>(ScopedEntity, ScopedCollider);

    Fby::PhysicsSystem ScopedPhysics;
    Check(ScopedPhysics.Initialize(), "physics world initializes");
    ScopedPhysics.Step(ScopedRegistry, kStep);
    Check(ScopedPhysics.LiveBodyCount() == 1, "physics created exactly one body");

    std::vector<std::byte> ScopedSnapshot(Fby::Ecs::SnapshotSize(ScopedRegistry, ScopedSchema) + 4096);
    std::size_t ScopedSnapshotBytes = 0;
    Fby::DeterministicRng ScopedRng = Fby::DeterministicRng::FromSeed(7u, 3u);
    Check(Fby::Ecs::CaptureSnapshot(ScopedRegistry, ScopedSchema, ScopedSnapshot, 1, ScopedRng, &ScopedSnapshotBytes),
          "physics snapshot captured");
    ScopedSnapshot.resize(ScopedSnapshotBytes);

    NetRollbackSession ScopedSession;
    NetRollbackSession::Config ScopedConfig;
    ScopedConfig.playerCount = 2;
    ScopedConfig.rollbackTicks = 8;
    std::vector<std::byte> ScopedArena(4u * 1024u * 1024u);
    Check(ScopedSession.Initialize(ScopedRegistry, ScopedSchema, ScopedConfig, ScopedArena) ==
              NetRollbackSession::Result::Ok,
          "rollback session initializes");
    ScopedSession.SetPhysics(&ScopedPhysics);

    Check(ScopedSession.RestoreSnapshot(ScopedSnapshot) == NetRollbackSession::Result::Ok,
          "restore succeeds with a physics world attached");
    Check(ScopedPhysics.LiveBodyCount() == 1, "restore recreates one body, not a duplicate");

    // Stress the destroy/recreate cycle: every iteration must replace the body,
    bool ScopedStable = true;
    for (std::uint32_t ScopedIteration = 0; ScopedIteration < 128; ++ScopedIteration)
    {
        if (ScopedSession.RestoreSnapshot(ScopedSnapshot) != NetRollbackSession::Result::Ok ||
            ScopedPhysics.LiveBodyCount() != 1)
        {
            ScopedStable = false;
            break;
        }
    }
    Check(ScopedStable, "128 repeated restores keep exactly one native body");

    const auto* ScopedRestoredBody = ScopedRegistry.try_get<Fby::RigidBody2D>(ScopedEntity);
    Check(ScopedRestoredBody != nullptr && ScopedRestoredBody->Body != 0,
          "restored RigidBody2D carries a live native handle");

    // Count native destroys directly: a leaked old body would make this 2.
    Check(ScopedPhysics.DestroyAllBodies(&ScopedRegistry) == 1, "exactly one native body existed after restore");
}
}

void* operator new(std::size_t InSize)
{
    GAllocations.fetch_add(1, std::memory_order_relaxed);
    void* ScopedMemory = std::malloc(InSize == 0 ? 1 : InSize);
    if (ScopedMemory == nullptr)
    {
        throw std::bad_alloc{};
    }
    return ScopedMemory;
}

void* operator new[](std::size_t InSize)
{
    GAllocations.fetch_add(1, std::memory_order_relaxed);
    void* ScopedMemory = std::malloc(InSize == 0 ? 1 : InSize);
    if (ScopedMemory == nullptr)
    {
        throw std::bad_alloc{};
    }
    return ScopedMemory;
}

void operator delete(void* InMemory) noexcept
{
    std::free(InMemory);
}

void operator delete[](void* InMemory) noexcept
{
    std::free(InMemory);
}

void operator delete(void* InMemory, std::size_t) noexcept
{
    std::free(InMemory);
}

void operator delete[](void* InMemory, std::size_t) noexcept
{
    std::free(InMemory);
}

struct PhysicsSimWorld
{
    Fby::PhysicsSystem* physics = nullptr;
};

void SimulatePhysics(void* InUser, Registry& InRegistry, std::uint64_t InTick, const NetTickInputs& InInputs,
                     Pcg32& InRng) noexcept
{
    (void)InTick;
    (void)InInputs;
    (void)InRng;
    auto* ScopedWorld = static_cast<PhysicsSimWorld*>(InUser);
    ScopedWorld->physics->Step(InRegistry, kStep);
    auto ScopedEmitters = InRegistry.view<Fby::ParticleEmitterComponent>();
    for (const Entity ScopedEntity : ScopedEmitters)
    {
        auto& ScopedEmitter = ScopedEmitters.get<Fby::ParticleEmitterComponent>(ScopedEntity);
        ScopedEmitter.AnimationTime += kStep;
        ScopedEmitter.ActiveCount = (ScopedEmitter.ActiveCount + 1u) & 3u;
    }
}

struct PhysicsRun
{
    Registry registry;
    Fby::PhysicsSystem physics;
    PhysicsSimWorld sim;
    SandboxArena arena;
    NetRollbackSession session;
};

bool InitializePhysicsRun(PhysicsRun& InRun)
{
    const Entity ScopedEntity = InRun.registry.create();
    InRun.registry.emplace<Transform2D>(ScopedEntity,
                                        Transform2D{Vector2D{0.0f, 0.0f}, Vector2D{1.0f, 1.0f}, 0.0f});
    Fby::RigidBody2D ScopedBody;
    ScopedBody.Type = Fby::BodyType::Dynamic;
    ScopedBody.LinearDamping = 0.3f;
    ScopedBody.AngularDamping = 0.2f;
    InRun.registry.emplace<Fby::RigidBody2D>(ScopedEntity, ScopedBody);
    Fby::Collider2D ScopedCollider;
    ScopedCollider.HalfExtents = Vector2D{4.0f, 4.0f};
    InRun.registry.emplace<Fby::Collider2D>(ScopedEntity, ScopedCollider);
    Fby::ParticleEmitterComponent ScopedEmitter;
    ScopedEmitter.BurstInterval = 0.25f;
    ScopedEmitter.RotationSpeed = 2.0f;
    ScopedEmitter.StartRotation = 0.1f;
    ScopedEmitter.MaterialPipeline = 7u;
    InRun.registry.emplace<Fby::ParticleEmitterComponent>(ScopedEntity, ScopedEmitter);

    if (!InRun.physics.Initialize())
    {
        return false;
    }
    InRun.sim.physics = &InRun.physics;

    NetRollbackSession::Config ScopedConfig;
    ScopedConfig.playerCount = 2;
    ScopedConfig.localPlayer = 0;
    ScopedConfig.rollbackTicks = kRollbackWindow;
    ScopedConfig.snapshotBytes = kSnapshotBytes;
    if (InRun.session.Initialize(InRun.registry, SnapshotSchema::BuiltIns(), ScopedConfig,
                                 std::span<std::byte>(InRun.arena.bytes.data(), InRun.arena.bytes.size())) !=
        NetRollbackSession::Result::Ok)
    {
        return false;
    }
    InRun.session.SetSimulation(&SimulatePhysics, &InRun.sim);
    InRun.session.SetPhysics(&InRun.physics);
    if (InRun.session.RegisterEntity(ScopedEntity, NetEntityId{1}) != NetEntityId{1})
    {
        return false;
    }
    return InRun.session.Start(Pcg32::FromSeed(0xFEEDFACEull, 77u)) == NetRollbackSession::Result::Ok;
}

void TestPhysicsDeterminism()
{
    PhysicsRun ScopedFirst;
    PhysicsRun ScopedSecond;
    Check(InitializePhysicsRun(ScopedFirst), "physics determinism run A initializes");
    Check(InitializePhysicsRun(ScopedSecond), "physics determinism run B initializes");

    for (std::uint64_t ScopedTick = 1; ScopedTick <= 20; ++ScopedTick)
    {
        Check(ScopedFirst.session.SubmitRemoteInput(NetPlayerId{1}, MakeRemote(ScopedTick, false)) ==
                  NetRollbackSession::Result::Ok,
              "physics determinism remote input A");
        Check(ScopedSecond.session.SubmitRemoteInput(NetPlayerId{1}, MakeRemote(ScopedTick, false)) ==
                  NetRollbackSession::Result::Ok,
              "physics determinism remote input B");
        Check(ScopedFirst.session.Step(MakeLocal(ScopedTick)) == NetRollbackSession::Result::Ok,
              "physics determinism step A");
        Check(ScopedSecond.session.Step(MakeLocal(ScopedTick)) == NetRollbackSession::Result::Ok,
              "physics determinism step B");
        Check(ScopedFirst.session.SnapshotChecksumAt(ScopedTick) ==
                  ScopedSecond.session.SnapshotChecksumAt(ScopedTick),
              "physics frame digests match across runs");
    }

    // A late correction replays the same ticks in both runs; the digests must
    // still agree after the restore destroyed and recreated the bodies.
    Check(ScopedFirst.session.SubmitRemoteInput(NetPlayerId{1}, MakeRemote(18, true)) ==
              NetRollbackSession::Result::Ok,
          "physics determinism correction A");
    Check(ScopedSecond.session.SubmitRemoteInput(NetPlayerId{1}, MakeRemote(18, true)) ==
              NetRollbackSession::Result::Ok,
          "physics determinism correction B");
    Check(ScopedFirst.session.ReplayPending() == NetRollbackSession::Result::Ok, "physics replay A");
    Check(ScopedSecond.session.ReplayPending() == NetRollbackSession::Result::Ok, "physics replay B");
    Check(ScopedFirst.session.SnapshotChecksumAt(20) == ScopedSecond.session.SnapshotChecksumAt(20),
          "post-replay physics digests match");
}

struct ExtensionState
{
    std::uint32_t value = 0;
};

bool CaptureExtension(void* InUser, std::span<std::byte> OutBytes, std::size_t* OutSize) noexcept
{
    if (OutBytes.size() < sizeof(std::uint32_t))
    {
        return false;
    }
    std::memcpy(OutBytes.data(), InUser, sizeof(std::uint32_t));
    *OutSize = sizeof(std::uint32_t);
    return true;
}

void RestoreExtension(void* InUser, std::span<const std::byte> InBytes) noexcept
{
    if (InBytes.size() >= sizeof(std::uint32_t))
    {
        std::memcpy(InUser, InBytes.data(), sizeof(std::uint32_t));
    }
}

// The world extension travels in every rollback slot and is handed back on
// restore.
void TestExtensionSlots()
{
    constexpr std::uint32_t ScopedSlotBytes = 8192;
    constexpr std::uint32_t ScopedTicks = 8;
    constexpr std::uint32_t ScopedExtensionBytes = 64;
    Registry ScopedRegistry;
    TestWorld ScopedWorld = CreateWorld(ScopedRegistry);
    ExtensionState ScopedState;
    ScopedState.value = 42;
    std::array<std::byte, (ScopedSlotBytes + ScopedExtensionBytes) * ScopedTicks + ScopedExtensionBytes> ScopedArena{};

    NetRollbackSession::Config ScopedConfig;
    ScopedConfig.playerCount = 2;
    ScopedConfig.localPlayer = 0;
    ScopedConfig.rollbackTicks = ScopedTicks;
    ScopedConfig.snapshotBytes = ScopedSlotBytes;
    ScopedConfig.extensionBytes = ScopedExtensionBytes;
    ScopedConfig.extensionCapture = &CaptureExtension;
    ScopedConfig.extensionRestore = &RestoreExtension;
    ScopedConfig.extensionUser = &ScopedState;

    NetRollbackSession ScopedSession;
    const NetRollbackSession::Result ScopedInit =
        ScopedSession.Initialize(ScopedRegistry, SnapshotSchema::BuiltIns(), ScopedConfig,
                                 std::span<std::byte>(ScopedArena.data(), ScopedArena.size()));
    if (ScopedInit != NetRollbackSession::Result::Ok)
    {
        std::fprintf(stderr, "extension init failed: %s\n", NetRollbackSession::ResultName(ScopedInit));
    }
    Check(ScopedInit == NetRollbackSession::Result::Ok, "extension session initializes");
    Check(ScopedSession.CaptureFrame(0) == NetRollbackSession::Result::Ok, "frame with extension captures");
    Check(ScopedState.value == 42, "capture leaves the world extension untouched");

    ScopedState.value = 7;
    Check(ScopedSession.RestoreFrame(0) == NetRollbackSession::Result::Ok, "frame with extension restores");
    Check(ScopedState.value == 42, "the world extension is restored with the frame");

    // A configuration that promises an extension but cannot encode it is invalid.
    NetRollbackSession::Config ScopedBad = ScopedConfig;
    ScopedBad.extensionCapture = nullptr;
    NetRollbackSession ScopedBadSession;
    Check(ScopedBadSession.Initialize(ScopedRegistry, SnapshotSchema::BuiltIns(), ScopedBad,
                                      std::span<std::byte>(ScopedArena.data(), ScopedArena.size())) ==
              NetRollbackSession::Result::InvalidArgument,
          "an extension without a capture hook is rejected");
}

int main()
{
    TestInputRing();
    TestFrameDigest();
    TestFailedReplayClearsPending();
    TestFarFutureFirstInput();
    TestLocalOnlyStateSkipped();
    TestEntityMap();
    TestGameplayStateComponent();
    TestPredictionAndRollback();
    TestCommandLogRollback();
    TestNoAllocationAfterStart();
    TestPhysicsBodySweep();
    TestPhysicsDeterminism();
    TestExtensionSlots();

    if (GFailures != 0)
    {
        std::fprintf(stderr, "net rollback: %d failure(s)\n", GFailures);
        return 1;
    }
    std::printf("net rollback: OK (ids, input ring, prediction, checksum, CommandLog replay)\n");
    return 0;
}
