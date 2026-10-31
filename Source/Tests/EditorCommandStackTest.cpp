// FemboyEngine2D - CommandStack (undo/redo) + Play-In-Editor tests.
#include "Editor/Infrastructure/CommandStack.hpp"
#include "Editor/Infrastructure/EditTransactions.hpp"
#include "Editor/Infrastructure/PlayInEditor.hpp"

#include "Engine/ECS/Components.hpp"
#include "Engine/ECS/Skeleton.hpp"
#include "Engine/ECS/SystemGraph.hpp"
#include "Engine/Core/JobSystem.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <thread>
#include <vector>

namespace
{
int GFailures = 0;
int GObserverCalls = 0;

void Check(bool InCondition, const char* InMessage)
{
    if (!InCondition)
    {
        std::printf("FAIL: %s\n", InMessage);
        ++GFailures;
    }
}

template <typename T>
[[nodiscard]] Fby::Editor::CommandStack::Result Capture(Fby::Editor::CommandStack& InStack, Fby::Entity InEntity,
                                                        std::uint16_t InCodec, T* Out)
{
    return InStack.CaptureComponent(InEntity, InCodec,
                                    std::span<std::byte>(reinterpret_cast<std::byte*>(Out), sizeof(T)));
}

void Observe(void*, Fby::Entity, std::uint16_t) noexcept
{
    ++GObserverCalls;
}

void IntegrateTransforms(Fby::Ecs::SystemContext& InContext)
{
    auto ScopedView = InContext.registry.view<Fby::Transform2D, Fby::Velocity>();
    for (const Fby::Entity ScopedEntity : ScopedView)
    {
        auto& ScopedTransform = ScopedView.get<Fby::Transform2D>(ScopedEntity);
        const auto& ScopedVelocity = ScopedView.get<Fby::Velocity>(ScopedEntity);
        ScopedTransform.Position.X += ScopedVelocity.Value.X * InContext.dt;
    }
}

void StepPhysics(Fby::Ecs::SystemContext& InContext)
{
    if (InContext.physics != nullptr)
    {
        InContext.physics->Step(InContext.registry, InContext.dt);
    }
}

// Probes. Every probe touches only atomics, so the test can prove overlap
std::atomic<int> GParallelCurrent{0};
std::atomic<int> GParallelMax{0};
std::atomic<bool> GDependencyAFired{false};
std::atomic<bool> GDependencyOrderOk{false};

void RecordOverlap()
{
    const int ScopedCurrent = GParallelCurrent.fetch_add(1, std::memory_order_acq_rel) + 1;
    int ScopedObserved = GParallelMax.load(std::memory_order_relaxed);
    while (ScopedCurrent > ScopedObserved &&
           !GParallelMax.compare_exchange_weak(ScopedObserved, ScopedCurrent, std::memory_order_relaxed))
    {
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(8));
    GParallelCurrent.fetch_sub(1, std::memory_order_acq_rel);
}

void ParallelProbeA(Fby::Ecs::SystemContext&)
{
    RecordOverlap();
}
void ParallelProbeB(Fby::Ecs::SystemContext&)
{
    RecordOverlap();
}
void ParallelProbeC(Fby::Ecs::SystemContext&)
{
    RecordOverlap();
}
void DependencyA(Fby::Ecs::SystemContext&)
{
    GDependencyAFired.store(true, std::memory_order_release);
}
void DependencyB(Fby::Ecs::SystemContext&)
{
    GDependencyOrderOk.store(GDependencyAFired.load(std::memory_order_acquire), std::memory_order_release);
}

struct ProbeState
{
    std::atomic<std::uint32_t> count{0};
};

void CountProbe(Fby::Ecs::SystemContext&, void* InUser)
{
    static_cast<ProbeState*>(InUser)->count.fetch_add(1, std::memory_order_relaxed);
}

void TestFieldUndoRedo()
{
    Fby::Editor::CommandStack ScopedStack;
    Check(ScopedStack.Initialize(), "command stack initializes");
    entt::registry ScopedRegistry;
    ScopedStack.Bind(&ScopedRegistry, nullptr);
    const std::uint16_t ScopedVelocityCodec = ScopedStack.RegisterComponent<Fby::Velocity>("Velocity");
    const std::uint16_t ScopedTransformCodec = ScopedStack.RegisterComponent<Fby::Transform2D>("Transform2D");
    Check(ScopedVelocityCodec != 0xFFFF && ScopedTransformCodec != 0xFFFF, "component codecs registered");
    ScopedStack.SetObserver(&Observe, nullptr);

    const Fby::Entity ScopedEntity = ScopedRegistry.create();
    ScopedRegistry.emplace<Fby::Velocity>(ScopedEntity, Fby::Velocity{Fby::Vector2D{1.0f, 2.0f}});
    ScopedRegistry.emplace<Fby::Transform2D>(
        ScopedEntity, Fby::Transform2D{Fby::Vector2D{10.0f, 20.0f}, Fby::Vector2D{1.0f, 1.0f}, 0.0f});

    Fby::Velocity ScopedOldVelocity{};
    Fby::Transform2D ScopedOldTransform{};
    Check(Capture(ScopedStack, ScopedEntity, ScopedVelocityCodec, &ScopedOldVelocity) ==
              Fby::Editor::CommandStack::Result::Ok,
          "capture old velocity");
    Check(Capture(ScopedStack, ScopedEntity, ScopedTransformCodec, &ScopedOldTransform) ==
              Fby::Editor::CommandStack::Result::Ok,
          "capture old transform");

    Check(ScopedStack.BeginTransaction("Inspector edit") == Fby::Editor::CommandStack::Result::Ok, "begin transaction");
    ScopedRegistry.get<Fby::Velocity>(ScopedEntity).Value = Fby::Vector2D{7.0f, 8.0f};
    ScopedRegistry.get<Fby::Transform2D>(ScopedEntity).Position = Fby::Vector2D{30.0f, 40.0f};
    Check(ScopedStack.RecordAfterMutation(
              ScopedEntity, ScopedVelocityCodec,
              std::span<const std::byte>(reinterpret_cast<const std::byte*>(&ScopedOldVelocity),
                                         sizeof(ScopedOldVelocity))) == Fby::Editor::CommandStack::Result::Ok,
          "record velocity delta");
    Check(ScopedStack.RecordAfterMutation(
              ScopedEntity, ScopedTransformCodec,
              std::span<const std::byte>(reinterpret_cast<const std::byte*>(&ScopedOldTransform),
                                         sizeof(ScopedOldTransform))) == Fby::Editor::CommandStack::Result::Ok,
          "record transform delta");
    Check(ScopedStack.CommitTransaction() == Fby::Editor::CommandStack::Result::Ok, "commit transaction");
    Check(ScopedStack.CanUndo(), "undo available");
    Check(ScopedStack.GetStats().bytesUsed < 160, "sparse delta is smaller than two full float payloads plus headers");
    Check(ScopedStack.UndoLabel() == "Inspector edit", "undo label preserved");

    Check(ScopedStack.Undo(), "undo");
    Check(ScopedRegistry.get<Fby::Velocity>(ScopedEntity).Value.X == 1.0f, "velocity restored by undo");
    Check(ScopedRegistry.get<Fby::Transform2D>(ScopedEntity).Position.X == 10.0f,
          "transform restored by the same undo step");
    Check(GObserverCalls == 2, "observer called for both components");
    Check(ScopedStack.Redo(), "redo");
    Check(ScopedRegistry.get<Fby::Velocity>(ScopedEntity).Value.X == 7.0f, "velocity restored by redo");
    Check(ScopedRegistry.get<Fby::Transform2D>(ScopedEntity).Position.Y == 40.0f, "transform restored by redo");

    Check(ScopedStack.CaptureComponent(ScopedEntity, 999, {}) == Fby::Editor::CommandStack::Result::UnknownCodec,
          "unknown codec rejected");
    Check(ScopedStack.Record(ScopedEntity, ScopedVelocityCodec, {}, {}) ==
              Fby::Editor::CommandStack::Result::NoOpenTransaction,
          "record outside a transaction rejected");
}

void TestLargeComponentDelta()
{
    Fby::Editor::CommandStack ScopedStack;
    Check(ScopedStack.Initialize(), "stack initializes for skeleton test");
    entt::registry ScopedRegistry;
    ScopedStack.Bind(&ScopedRegistry, nullptr);
    const std::uint16_t ScopedCodec = ScopedStack.RegisterComponent<Fby::SkeletonComponent>("SkeletonComponent");
    Check(ScopedCodec != 0xFFFF, "skeleton component codec registered");
    const Fby::Entity ScopedEntity = ScopedRegistry.create();
    Fby::SkeletonComponent& ScopedSkeleton = ScopedRegistry.emplace<Fby::SkeletonComponent>(ScopedEntity);
    Fby::SkeletonReset(ScopedSkeleton);
    (void)Fby::SkeletonAddBone(ScopedSkeleton, "root", -1, {0.0f, 0.0f}, 0.0f);
    (void)Fby::SkeletonAddBone(ScopedSkeleton, "arm", 0, {4.0f, 0.0f}, 0.2f);
    Fby::SkeletonBindPose(ScopedSkeleton);

    std::array<std::byte, Fby::Editor::kMaxTrackedComponentBytes> ScopedOldBytes{};
    Check(ScopedStack.CaptureComponent(ScopedEntity, ScopedCodec, ScopedOldBytes) ==
              Fby::Editor::CommandStack::Result::Ok,
          "capture skeleton");
    Check(ScopedStack.BeginTransaction("Rotate bone") == Fby::Editor::CommandStack::Result::Ok,
          "begin bone transaction");
    ScopedSkeleton.LocalRotations[1] = 1.25f;
    Check(ScopedStack.RecordAfterMutation(
              ScopedEntity, ScopedCodec,
              std::span<const std::byte>(ScopedOldBytes.data(), sizeof(Fby::SkeletonComponent))) ==
              Fby::Editor::CommandStack::Result::Ok,
          "record bone delta");
    Check(ScopedStack.CommitTransaction() == Fby::Editor::CommandStack::Result::Ok, "commit bone transaction");
    Check(ScopedStack.GetStats().bytesUsed < 512, "one bone edit compresses to a small delta");
    Check(ScopedStack.Undo(), "undo bone edit");
    Check(ScopedSkeleton.LocalRotations[1] == 0.2f, "bone rotation restored");
    Check(ScopedStack.Redo(), "redo bone edit");
    Check(ScopedSkeleton.LocalRotations[1] == 1.25f, "bone rotation reapplied");
}

void TestInteractionTransaction()
{
    using Fby::Editor::CommandStack;
    using Fby::Editor::InteractionTransaction;
    constexpr auto ScopedKGizmo = InteractionTransaction::Source::Gizmo;
    constexpr auto ScopedKInspector = InteractionTransaction::Source::InspectorField;

    CommandStack ScopedStack;
    Check(ScopedStack.Initialize(), "transaction stack initializes");
    entt::registry ScopedRegistry;
    ScopedStack.Bind(&ScopedRegistry, nullptr);
    const std::uint16_t ScopedTransformCodec = ScopedStack.RegisterComponent<Fby::Transform2D>("Transform2D");
    Check(ScopedTransformCodec != 0xFFFF, "transaction transform codec registered");
    Check(ScopedStack.CodecSize(ScopedTransformCodec) == sizeof(Fby::Transform2D), "codec size is exposed");

    const Fby::Entity ScopedEntity = ScopedRegistry.create();
    ScopedRegistry.emplace<Fby::Transform2D>(
        ScopedEntity, Fby::Transform2D{Fby::Vector2D{10.0f, 20.0f}, Fby::Vector2D{1.0f, 1.0f}, 0.0f});

    InteractionTransaction ScopedTransaction;
    Check(ScopedTransaction.Begin(ScopedStack, ScopedEntity, ScopedTransformCodec, "ImGuizmo transform",
                                  ScopedKGizmo) == CommandStack::Result::Ok,
          "gizmo transaction begins");
    Check(ScopedTransaction.Active() && !ScopedTransaction.Dirty(), "gizmo transaction starts clean");
    Check(ScopedTransaction.IsFor(ScopedEntity, ScopedTransformCodec, ScopedKGizmo),
          "transaction is scoped to the gizmo source");
    Check(!ScopedTransaction.IsFor(ScopedEntity, ScopedTransformCodec, ScopedKInspector),
          "an inspector source cannot adopt the gesture");

    // Simulate three mouse-move frames of an ImGuizmo drag.
    ScopedRegistry.get<Fby::Transform2D>(ScopedEntity).Position = Fby::Vector2D{11.0f, 21.0f};
    Check(ScopedTransaction.Record() == CommandStack::Result::Ok, "record first drag frame");
    ScopedRegistry.get<Fby::Transform2D>(ScopedEntity).Position = Fby::Vector2D{12.0f, 22.0f};
    Check(ScopedTransaction.Record() == CommandStack::Result::Ok, "record second drag frame");
    ScopedRegistry.get<Fby::Transform2D>(ScopedEntity).Position = Fby::Vector2D{13.0f, 23.0f};
    Check(ScopedTransaction.Record() == CommandStack::Result::Ok, "record release frame");
    Check(ScopedTransaction.Dirty(), "drag transaction is dirty");
    Check(ScopedTransaction.Commit() == CommandStack::Result::Ok, "mouse release commits the drag");
    Check(!ScopedTransaction.Active(), "transaction is closed after commit");
    Check(ScopedStack.CanUndo(), "a completed drag is undoable");
    Check(ScopedStack.GetStats().committedTransactions == 1, "one drag is one committed transaction");
    Check(ScopedStack.GetStats().commands == 1, "one drag is one undo command");
    Check(ScopedStack.UndoLabel() == "ImGuizmo transform", "drag label is preserved");
    Check(ScopedStack.GetStats().bytesUsed < 128, "one moving Vec2 is stored as a sparse delta");

    Check(ScopedStack.Undo(), "undo the drag");
    Check(ScopedRegistry.get<Fby::Transform2D>(ScopedEntity).Position.X == 10.0f,
          "undo restores the pre-drag position");
    Check(ScopedStack.Redo(), "redo the drag");
    Check(ScopedRegistry.get<Fby::Transform2D>(ScopedEntity).Position.X == 13.0f,
          "redo restores the final drag position");

    // Inspector path: the widget already wrote, the caller passes the image
    std::array<std::byte, Fby::Editor::kMaxTrackedComponentBytes> ScopedBefore{};
    Check(ScopedStack.CaptureComponent(ScopedEntity, ScopedTransformCodec, ScopedBefore) == CommandStack::Result::Ok,
          "capture inspector pre-image");
    ScopedRegistry.get<Fby::Transform2D>(ScopedEntity).Position = Fby::Vector2D{30.0f, 40.0f};
    Check(ScopedTransaction.BeginWithSnapshot(
              ScopedStack, ScopedEntity, ScopedTransformCodec, "Edit position", ScopedKInspector,
              std::span<const std::byte>(ScopedBefore.data(), sizeof(Fby::Transform2D))) == CommandStack::Result::Ok,
          "inspector transaction begins from a pre-image");
    Check(ScopedTransaction.IsFor(ScopedEntity, ScopedTransformCodec, ScopedKInspector), "inspector source is tracked");
    Check(ScopedTransaction.Record() == CommandStack::Result::Ok, "record inspector edit");
    Check(ScopedTransaction.Commit() == CommandStack::Result::Ok, "commit inspector edit");
    Check(ScopedStack.Undo(), "undo inspector edit");
    Check(ScopedRegistry.get<Fby::Transform2D>(ScopedEntity).Position.X == 13.0f,
          "inspector undo restores the pre-image, not the previous drag");

    // Abort restores the pre-gesture bytes and creates no undo step.
    Check(ScopedTransaction.Begin(ScopedStack, ScopedEntity, ScopedTransformCodec, "Abort me", ScopedKGizmo) ==
              CommandStack::Result::Ok,
          "abort transaction begins");
    const std::uint32_t ScopedCommandsBeforeAbort = ScopedStack.GetStats().commands;
    ScopedRegistry.get<Fby::Transform2D>(ScopedEntity).Position = Fby::Vector2D{-99.0f, -99.0f};
    Check(ScopedTransaction.Record() == CommandStack::Result::Ok, "abort transaction records");
    Check(ScopedTransaction.Abort() == CommandStack::Result::Ok, "abort restores the pre-image");
    Check(ScopedRegistry.get<Fby::Transform2D>(ScopedEntity).Position.X == 13.0f, "abort restored the component");
    Check(ScopedStack.GetStats().commands == ScopedCommandsBeforeAbort, "abort did not append an undo command");
    Check(!ScopedTransaction.Active(), "abort closed the transaction");

    // A click without a value change must not add a command.
    const std::uint32_t ScopedCommandsBeforeEmpty = ScopedStack.GetStats().commands;
    Check(ScopedTransaction.Begin(ScopedStack, ScopedEntity, ScopedTransformCodec, "No move", ScopedKGizmo) ==
              CommandStack::Result::Ok,
          "empty transaction begins");
    Check(ScopedTransaction.Commit() == CommandStack::Result::Ok, "empty transaction commits");
    Check(ScopedStack.GetStats().commands == ScopedCommandsBeforeEmpty, "empty transaction adds no undo command");

    // Validation: invalid entity, wrong source adoption and unknown codec.
    Check(ScopedTransaction.Begin(ScopedStack, static_cast<Fby::Entity>(0xFFFFFFFFu), ScopedTransformCodec, "bad",
                                  ScopedKGizmo) == CommandStack::Result::InvalidArgument,
          "invalid entity rejected");
    Check(ScopedTransaction.Begin(ScopedStack, ScopedEntity, 0xFFFFu, "bad codec", ScopedKGizmo) ==
              CommandStack::Result::UnknownCodec,
          "unknown codec rejected");
    Check(ScopedTransaction.Begin(ScopedStack, ScopedEntity, ScopedTransformCodec, "held", ScopedKGizmo) ==
              CommandStack::Result::Ok,
          "transaction begins for source conflict test");
    Check(ScopedTransaction.Commit() == CommandStack::Result::Ok, "close conflict transaction");
}

// A component with a deliberate alignment hole: the reflection-driven inspector
struct PaddedComponent
{
    std::uint8_t flag = 0;
    std::uint32_t value = 0;
};
static_assert(sizeof(PaddedComponent) == 8);
static_assert(offsetof(PaddedComponent, value) == 4);

void TestFieldWiseComparison()
{
    const Fby::Reflect::FieldDesc ScopedFlagField{"flag", Fby::Reflect::FieldKind::Bool,
                                                  offsetof(PaddedComponent, flag), sizeof(std::uint8_t)};
    const Fby::Reflect::FieldDesc ScopedValueField{"Value", Fby::Reflect::FieldKind::UInt32,
                                                   offsetof(PaddedComponent, value), sizeof(std::uint32_t)};
    const std::array<Fby::Reflect::FieldDesc, 2> ScopedFields{ScopedFlagField, ScopedValueField};

    std::array<std::byte, sizeof(PaddedComponent)> ScopedBefore{};
    std::array<std::byte, sizeof(PaddedComponent)> ScopedAfter{};
    const PaddedComponent ScopedSource{1, 0x11223344u};
    std::memcpy(ScopedBefore.data(), &ScopedSource, sizeof(ScopedSource));
    std::memcpy(ScopedAfter.data(), &ScopedSource, sizeof(ScopedSource));

    const auto FieldChanged = [&ScopedBefore, &ScopedAfter](const Fby::Reflect::FieldDesc& InField)
    {
        return !Fby::Reflect::FieldBytesEqual(InField, ScopedBefore.data() + InField.offset,
                                              ScopedAfter.data() + InField.offset);
    };

    // Only padding bytes differ: every field compares equal, while a raw memcmp
    ScopedAfter[1] = std::byte{0xAA};
    ScopedAfter[2] = std::byte{0xBB};
    ScopedAfter[3] = std::byte{0xCC};
    Check(!FieldChanged(ScopedFlagField), "padding does not change the bool field");
    Check(!FieldChanged(ScopedValueField), "padding does not change the uint32 field");
    Check(Fby::Reflect::ComponentFieldsEqual(ScopedFields, ScopedBefore, ScopedAfter),
          "padding-only images are field-equal");
    Check(std::memcmp(ScopedBefore.data(), ScopedAfter.data(), sizeof(PaddedComponent)) != 0,
          "a raw image comparison would have reported the padding as a change");

    // A real field change is still detected, including a one-byte bool.
    ScopedAfter[0] = std::byte{0};
    Check(FieldChanged(ScopedFlagField), "a bool change is detected");
    ScopedAfter[0] = std::byte{1};
    const std::uint32_t ScopedNewValue = 0x11223345u;
    std::memcpy(ScopedAfter.data() + offsetof(PaddedComponent, value), &ScopedNewValue, sizeof(ScopedNewValue));
    Check(FieldChanged(ScopedValueField), "a uint32 change is detected");
    Check(!Fby::Reflect::ComponentFieldsEqual(ScopedFields, ScopedBefore, ScopedAfter),
          "a real field change makes the images field-different");
}

void TestPlayInEditor()
{
    Fby::JobSystem ScopedJobs(1);
    Fby::Ecs::SnapshotSchema ScopedSchema = Fby::Ecs::SnapshotSchema::BuiltIns();
    (void)ScopedSchema.Add<Fby::Velocity>();
    (void)ScopedSchema.Add<Fby::Transform2D>();

    std::vector<std::byte> ScopedArenaStorage(1u << 20);
    Fby::Editor::PlayInEditor ScopedPie;
    Check(ScopedPie.Initialize(ScopedArenaStorage, ScopedSchema, ScopedArenaStorage.size()), "PIE initializes");

    entt::registry ScopedEditorWorld;
    const Fby::Entity ScopedEntity = ScopedEditorWorld.create();
    ScopedEditorWorld.emplace<Fby::Transform2D>(
        ScopedEntity, Fby::Transform2D{Fby::Vector2D{0.0f, 0.0f}, Fby::Vector2D{1.0f, 1.0f}, 0.0f});
    ScopedEditorWorld.emplace<Fby::Velocity>(ScopedEntity, Fby::Velocity{Fby::Vector2D{3.0f, 0.0f}});

    // The editor scene contains a body, but PIE must not insert clone
    Fby::PhysicsSystem ScopedEditorPhysics;
    Check(ScopedEditorPhysics.Initialize(), "editor physics world initializes");
    const Fby::Entity ScopedBodyEntity = ScopedEditorWorld.create();
    ScopedEditorWorld.emplace<Fby::Transform2D>(
        ScopedBodyEntity, Fby::Transform2D{Fby::Vector2D{5.0f, 5.0f}, Fby::Vector2D{1.0f, 1.0f}, 0.0f});
    Fby::RigidBody2D ScopedRigid;
    ScopedRigid.Type = Fby::BodyType::Dynamic;
    ScopedEditorWorld.emplace<Fby::RigidBody2D>(ScopedBodyEntity, ScopedRigid);
    Fby::Collider2D ScopedCollider;
    ScopedCollider.HalfExtents = Fby::Vector2D{4.0f, 4.0f};
    ScopedEditorWorld.emplace<Fby::Collider2D>(ScopedBodyEntity, ScopedCollider);

    Fby::Ecs::SystemContext ScopedServices{ScopedEditorWorld, ScopedJobs};
    ScopedServices.physics = &ScopedEditorPhysics;
    Check(ScopedPie.Start(ScopedEditorWorld, ScopedSchema, ScopedServices, 1, nullptr) ==
              Fby::Editor::PlayInEditor::Result::Ok,
          "PIE starts from the editor world");

    Fby::Ecs::SystemGraph ScopedGraph;
    Check(ScopedGraph.Add<&IntegrateTransforms>(Fby::Ecs::Phase::FixedUpdate, "integrate"), "graph system registered");
    Check(ScopedGraph.Add<&StepPhysics>(Fby::Ecs::Phase::FixedUpdate, "physics"), "physics system registered");
    Check(ScopedGraph.Build(), "graph builds");
    Check(ScopedPie.Step(ScopedGraph, 0.1f, 0.1f), "PIE step runs");
    Check(ScopedPie.World().get<Fby::Transform2D>(ScopedEntity).Position.X == 0.3f, "simulation moved the clone");
    Check(ScopedEditorWorld.get<Fby::Transform2D>(ScopedEntity).Position.X == 0.0f,
          "editor world untouched while playing");
    Check(ScopedEditorPhysics.LiveBodyCount() == 0, "editor physics world never received a PIE body");
    Check(ScopedPie.Physics().LiveBodyCount() == 1, "PIE owns exactly one native body for the clone");
    ScopedPie.Stop();
    Check(!ScopedPie.Active(), "PIE stopped");
    Check(ScopedPie.Physics().LiveBodyCount() == 0, "Stop destroyed the PIE physics world and its bodies");
    Check(ScopedEditorWorld.get<Fby::Transform2D>(ScopedEntity).Position.X == 0.0f, "editor world intact after stop");
    Check(ScopedEditorWorld.get<Fby::Transform2D>(ScopedBodyEntity).Position.X == 5.0f,
          "editor body transform intact after stop");

    // Stress the full PIE lifecycle: each Play/Step/Stop must create exactly one
    bool ScopedLifecycleStable = true;
    for (std::uint32_t ScopedIteration = 0; ScopedIteration < 16; ++ScopedIteration)
    {
        if (ScopedPie.Start(ScopedEditorWorld, ScopedSchema, ScopedServices, 1, nullptr) !=
                Fby::Editor::PlayInEditor::Result::Ok ||
            !ScopedPie.Step(ScopedGraph, 0.1f, 0.1f) || ScopedPie.Physics().LiveBodyCount() != 1)
        {
            ScopedLifecycleStable = false;
            break;
        }
        ScopedPie.Stop();
        if (ScopedPie.Active() || ScopedPie.Physics().LiveBodyCount() != 0)
        {
            ScopedLifecycleStable = false;
            break;
        }
    }
    Check(ScopedLifecycleStable, "16 PIE start/step/stop cycles leak no body or world");
    Check(ScopedEditorWorld.get<Fby::Transform2D>(ScopedEntity).Position.X == 0.0f,
          "editor world still untouched after PIE stress");
}

void TestSystemGraphParallel()
{
    Fby::JobSystem ScopedJobs(8);
    Fby::Ecs::Registry ScopedRegistry;
    Fby::Ecs::SystemContext ScopedContext{ScopedRegistry, ScopedJobs};

    GParallelCurrent.store(0);
    GParallelMax.store(0);
    GDependencyAFired.store(false);
    GDependencyOrderOk.store(false);

    Fby::Ecs::SystemGraph ScopedGraph;
    Check(ScopedGraph.Add<&DependencyA>(Fby::Ecs::Phase::PreUpdate, "dependency_a"), "independent root registered");
    // Bit 0 is dependency_a: this node must run in a later wave.
    Check(ScopedGraph.Add<&DependencyB>(Fby::Ecs::Phase::PreUpdate, "dependency_b", 1ull << 0),
          "dependent node registered");
    Check(ScopedGraph.Add<&ParallelProbeA>(Fby::Ecs::Phase::PreUpdate, "probe_a", 1ull << 0), "probe a registered");
    Check(ScopedGraph.Add<&ParallelProbeB>(Fby::Ecs::Phase::PreUpdate, "probe_b", 1ull << 0), "probe b registered");
    Check(ScopedGraph.Add<&ParallelProbeC>(Fby::Ecs::Phase::PreUpdate, "probe_c", 1ull << 0), "probe c registered");
    Check(ScopedGraph.Build(), "parallel graph builds");

    ScopedGraph.Run(Fby::Ecs::Phase::PreUpdate, ScopedContext);
    Check(GDependencyAFired.load(), "root dependency ran");
    Check(GDependencyOrderOk.load(), "after edge forced the dependent into a later wave");
    Check(GParallelMax.load() >= 2, "independent nodes overlapped on the job pool");
    ScopedJobs.WaitIdle();
}

void TestSystemGraphStress()
{
    Fby::JobSystem ScopedJobs(8);
    Fby::Ecs::Registry ScopedRegistry;
    Fby::Ecs::SystemContext ScopedContext{ScopedRegistry, ScopedJobs};

    constexpr std::uint32_t kProbeCount = 64;
    std::array<ProbeState, kProbeCount> ScopedStates{};
    Fby::Ecs::SystemGraph ScopedGraph;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < kProbeCount; ++ScopedIndex)
    {
        Check(ScopedGraph.Add(Fby::Ecs::Phase::PreUpdate, "probe", &CountProbe, &ScopedStates[ScopedIndex]),
              "stress graph registers a probe");
    }
    Check(ScopedGraph.Build(), "64-node graph builds");

    // 64 independent nodes force the widest wave the graph supports. Run the
    for (std::uint32_t ScopedFrame = 0; ScopedFrame < 64; ++ScopedFrame)
    {
        ScopedGraph.Run(Fby::Ecs::Phase::PreUpdate, ScopedContext);
    }
    ScopedJobs.WaitIdle();

    std::uint64_t ScopedTotal = 0;
    for (const ProbeState& ScopedState : ScopedStates)
    {
        ScopedTotal += ScopedState.count.load(std::memory_order_relaxed);
    }
    Check(ScopedTotal == static_cast<std::uint64_t>(kProbeCount) * 64u,
          "64-node wave ran exactly once per frame for 64 frames");
}
}

int main()
{
    TestFieldUndoRedo();
    TestLargeComponentDelta();
    TestInteractionTransaction();
    TestFieldWiseComparison();
    TestPlayInEditor();
    TestSystemGraphParallel();
    TestSystemGraphStress();
    if (GFailures != 0)
    {
        std::printf("%d editor command/PIE test(s) failed\n", GFailures);
        return 1;
    }
    std::printf("editor_command_stack_test: all tests passed\n");
    return 0;
}
