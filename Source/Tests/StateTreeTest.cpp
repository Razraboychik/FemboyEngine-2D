// FemboyEngine2D - FemStateTree tests.
#include "Engine/ECS/StateTree.hpp"

#include "Engine/Core/FrameArena.hpp"
#include "Engine/Core/JobSystem.hpp"
#include "Engine/ECS/CommandBuffer.hpp"
#include "Engine/ECS/Schedule.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <type_traits>

namespace
{
std::uint32_t GFailures = 0;

#define FBY_CHECK(condition)                                                                                           \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);                                  \
            ++GFailures;                                                                                              \
        }                                                                                                              \
    } while (false)

struct TestTree
{
    Fby::StateTreeDefinition Definition;
    std::uint16_t Root = Fby::kStateTreeNone;
    std::uint16_t Group = Fby::kStateTreeNone;
    std::uint16_t Attack = Fby::kStateTreeNone;
    std::uint16_t Move = Fby::kStateTreeNone;
};

[[nodiscard]] TestTree MakeTree()
{
    TestTree ScopedTree;
    ScopedTree.Definition.TreeId = 7;
    ScopedTree.Root = Fby::StateTreeAddNode(ScopedTree.Definition, Fby::StateTreeNodeKind::Root);
    ScopedTree.Group = Fby::StateTreeAddNode(ScopedTree.Definition, Fby::StateTreeNodeKind::Selector,
                                             ScopedTree.Root);
    ScopedTree.Attack = Fby::StateTreeAddNode(ScopedTree.Definition, Fby::StateTreeNodeKind::Leaf,
                                              ScopedTree.Group);
    ScopedTree.Move = Fby::StateTreeAddNode(ScopedTree.Definition, Fby::StateTreeNodeKind::Leaf,
                                            ScopedTree.Group);

    Fby::StateTreeEvaluator ScopedAttackGuard;
    ScopedAttackGuard.Kind = Fby::StateTreeEvaluatorKind::TargetWithinRadius;
    ScopedAttackGuard.Threshold = 64.0f;
    Fby::StateTreeTask ScopedAttackTask;
    ScopedAttackTask.Kind = Fby::StateTreeTaskKind::AttackTarget;
    ScopedAttackTask.Range = 48.0f;
    Fby::StateTreeSetLeaf(ScopedTree.Definition, ScopedTree.Attack, ScopedAttackGuard, ScopedAttackTask);

    Fby::StateTreeEvaluator ScopedMoveGuard;
    ScopedMoveGuard.Kind = Fby::StateTreeEvaluatorKind::AlwaysTrue;
    Fby::StateTreeTask ScopedMoveTask;
    ScopedMoveTask.Kind = Fby::StateTreeTaskKind::MoveToTarget;
    ScopedMoveTask.Speed = 120.0f;
    Fby::StateTreeSetLeaf(ScopedTree.Definition, ScopedTree.Move, ScopedMoveGuard, ScopedMoveTask);

    // "Player spotted" trigger: move -> attack.
    Fby::StateTreeTransition ScopedSpotted;
    ScopedSpotted.FromLeaf = ScopedTree.Move;
    ScopedSpotted.ToNode = ScopedTree.Attack;
    ScopedSpotted.TriggerMask = 1ull << 0;
    ScopedSpotted.ConsumeMask = 1ull << 0;
    FBY_CHECK(Fby::StateTreeAddTransition(ScopedTree.Definition, ScopedSpotted));

    // Attack guard failed: back to move.
    Fby::StateTreeTransition ScopedLostTarget;
    ScopedLostTarget.FromLeaf = ScopedTree.Attack;
    ScopedLostTarget.ToNode = ScopedTree.Move;
    ScopedLostTarget.TriggerMask = Fby::kStateTreeTriggerLeafFailed;
    ScopedLostTarget.ConsumeMask = Fby::kStateTreeTriggerLeafFailed;
    FBY_CHECK(Fby::StateTreeAddTransition(ScopedTree.Definition, ScopedLostTarget));

    // Tag-gated transition: only while the entity is attacking (external bit).
    Fby::StateTreeTransition ScopedTagged;
    ScopedTagged.FromLeaf = ScopedTree.Move;
    ScopedTagged.ToNode = ScopedTree.Attack;
    ScopedTagged.TriggerMask = 1ull << 1;
    ScopedTagged.ConsumeMask = 1ull << 1;
    ScopedTagged.RequireTagsAll = Fby::GameplayStateBit(Fby::GameplayStateFlag::Attacking);
    FBY_CHECK(Fby::StateTreeAddTransition(ScopedTree.Definition, ScopedTagged));
    return ScopedTree;
}

void TestAuthoringLimits()
{
    Fby::StateTreeDefinition ScopedDeep;
    ScopedDeep.TreeId = 1;
    const std::uint16_t ScopedRoot = Fby::StateTreeAddNode(ScopedDeep, Fby::StateTreeNodeKind::Root);
    const std::uint16_t ScopedDepth2 =
        Fby::StateTreeAddNode(ScopedDeep, Fby::StateTreeNodeKind::Selector, ScopedRoot);
    const std::uint16_t ScopedDepth3 =
        Fby::StateTreeAddNode(ScopedDeep, Fby::StateTreeNodeKind::Selector, ScopedDepth2);
    const std::uint16_t ScopedDepth4 =
        Fby::StateTreeAddNode(ScopedDeep, Fby::StateTreeNodeKind::Leaf, ScopedDepth3);
    FBY_CHECK(ScopedRoot != Fby::kStateTreeNone && ScopedDepth2 != Fby::kStateTreeNone);
    FBY_CHECK(ScopedDepth3 != Fby::kStateTreeNone && ScopedDepth4 != Fby::kStateTreeNone);
    // A fifth level would exceed ActiveStates[4].
    FBY_CHECK(Fby::StateTreeAddNode(ScopedDeep, Fby::StateTreeNodeKind::Leaf, ScopedDepth4) == Fby::kStateTreeNone);
    // A second root, or a root that is not the first node, is rejected.
    FBY_CHECK(Fby::StateTreeAddNode(ScopedDeep, Fby::StateTreeNodeKind::Root) == Fby::kStateTreeNone);
    // A transition without a trigger bit can never fire.
    Fby::StateTreeTransition ScopedEmpty;
    ScopedEmpty.ToNode = ScopedRoot;
    FBY_CHECK(!Fby::StateTreeAddTransition(ScopedDeep, ScopedEmpty));
}

void TestSystem()
{
    const TestTree ScopedTree = MakeTree();
    FBY_CHECK(ScopedTree.Definition.NodeCount == 4);
    FBY_CHECK(ScopedTree.Definition.TransitionCount == 3);

    Fby::StateTreeSystem ScopedSystem;
    FBY_CHECK(ScopedSystem.AddTree(ScopedTree.Definition));
    // A duplicate TreeId is rejected instead of silently shadowing the first.
    FBY_CHECK(!ScopedSystem.AddTree(ScopedTree.Definition));
    FBY_CHECK(ScopedSystem.TreeCount() == 1);
    FBY_CHECK(ScopedSystem.Tree(7) != nullptr);
    FBY_CHECK(ScopedSystem.Tree(99) == nullptr);

    Fby::Ecs::Registry ScopedRegistry;
    Fby::JobSystem ScopedJobs(1);
    std::array<std::byte, 64u << 10> ScopedArenaStorage{};
    Fby::FrameArena ScopedArena(ScopedArenaStorage);
    Fby::Ecs::CommandBuffer ScopedCommands;
    ScopedCommands.Initialize(&ScopedArena);

    Fby::Ecs::SystemContext ScopedContext{ScopedRegistry, ScopedJobs};
    ScopedContext.commands = &ScopedCommands;

    const Fby::Entity ScopedTarget = ScopedRegistry.create();
    auto& ScopedTargetTransform = ScopedRegistry.emplace<Fby::Transform2D>(ScopedTarget);
    ScopedTargetTransform.Position = Fby::Vector2D{1000.0f, 0.0f};

    const Fby::Entity ScopedEntity = ScopedRegistry.create();
    auto& ScopedSelfTransform = ScopedRegistry.emplace<Fby::Transform2D>(ScopedEntity);
    ScopedSelfTransform.Position = Fby::Vector2D{0.0f, 0.0f};
    auto& ScopedGameplay = ScopedRegistry.emplace<Fby::GameplayState>(ScopedEntity);
    auto& ScopedState = ScopedRegistry.emplace<Fby::StateTreeComponent>(ScopedEntity);
    ScopedState.TreeId = 7;
    ScopedState.Target = ScopedTarget;

    ScopedContext.dt = 0.1f;
    ScopedSystem.Run(ScopedContext);

    // Initial descent selected the move leaf: root -> group -> move.
    FBY_CHECK(ScopedState.Initialized);
    FBY_CHECK(ScopedState.ActiveDepth == 3);
    FBY_CHECK(ScopedState.ActiveStates[0] == ScopedTree.Root);
    FBY_CHECK(ScopedState.ActiveStates[1] == ScopedTree.Group);
    FBY_CHECK(ScopedState.ActiveStates[2] == ScopedTree.Move);
    FBY_CHECK(ScopedState.ActiveStates[3] == Fby::kStateTreeNone);
    FBY_CHECK(ScopedState.CurrentLeaf == ScopedTree.Move);
    FBY_CHECK(ScopedState.TransitionsFired == 0);

    // The task emitted one deferred move command.
    FBY_CHECK(ScopedCommands.Count() == 1);
    ScopedCommands.Play(ScopedRegistry);
    const auto* ScopedMoveCommand = ScopedRegistry.try_get<Fby::AICommandComponent>(ScopedEntity);
    FBY_CHECK(ScopedMoveCommand != nullptr);
    FBY_CHECK(ScopedMoveCommand->Kind == static_cast<std::uint32_t>(Fby::AICommandKind::MoveTo));
    FBY_CHECK(ScopedMoveCommand->Target == ScopedTarget);
    FBY_CHECK(ScopedMoveCommand->Position == Fby::Vector2D(1000.0f, 0.0f));
    FBY_CHECK(ScopedMoveCommand->Speed == 120.0f);
    ScopedCommands.Reset();
    ScopedArena.Reset();

    // Steady state without triggers: no transition, same path.
    ScopedSystem.Run(ScopedContext);
    FBY_CHECK(ScopedState.TransitionsFired == 0);
    FBY_CHECK(ScopedState.ActiveStates[2] == ScopedTree.Move);
    ScopedCommands.Play(ScopedRegistry);
    ScopedCommands.Reset();
    ScopedArena.Reset();

    // Player spotted: the external trigger fires exactly one transition.
    ScopedTargetTransform.Position = Fby::Vector2D{10.0f, 0.0f};
    Fby::StateTreeRaiseTrigger(ScopedState, 1ull << 0);
    ScopedSystem.Run(ScopedContext);
    FBY_CHECK(ScopedState.TransitionsFired == 1);
    FBY_CHECK(ScopedState.CurrentLeaf == ScopedTree.Attack);
    FBY_CHECK(ScopedState.ActiveDepth == 3);
    FBY_CHECK(ScopedState.ActiveStates[2] == ScopedTree.Attack);
    FBY_CHECK(ScopedState.Triggers == 0);
    FBY_CHECK(ScopedState.StateTime == 0.0f);
    FBY_CHECK(ScopedCommands.Count() == 1);
    ScopedCommands.Play(ScopedRegistry);
    const auto* ScopedAttackCommand = ScopedRegistry.try_get<Fby::AICommandComponent>(ScopedEntity);
    FBY_CHECK(ScopedAttackCommand != nullptr);
    FBY_CHECK(ScopedAttackCommand->Kind == static_cast<std::uint32_t>(Fby::AICommandKind::Attack));
    FBY_CHECK(ScopedAttackCommand->Range == 48.0f);
    FBY_CHECK(ScopedAttackCommand->Position == Fby::Vector2D(10.0f, 0.0f));
    ScopedCommands.Reset();
    ScopedArena.Reset();

    // The guard still holds: attack stays active, no scan-driven transition.
    ScopedSystem.Run(ScopedContext);
    FBY_CHECK(ScopedState.TransitionsFired == 1);
    FBY_CHECK(ScopedState.CurrentLeaf == ScopedTree.Attack);
    ScopedCommands.Play(ScopedRegistry);
    ScopedCommands.Reset();
    ScopedArena.Reset();

    // Target left the radius: the leaf guard fails, the internal trigger fires
    ScopedTargetTransform.Position = Fby::Vector2D{1000.0f, 0.0f};
    ScopedSystem.Run(ScopedContext);
    FBY_CHECK(ScopedState.TransitionsFired == 2);
    FBY_CHECK(ScopedState.CurrentLeaf == ScopedTree.Move);
    FBY_CHECK((ScopedState.Triggers & Fby::kStateTreeTriggerLeafFailed) == 0);
    ScopedCommands.Play(ScopedRegistry);
    FBY_CHECK(ScopedRegistry.try_get<Fby::AICommandComponent>(ScopedEntity)->Kind ==
              static_cast<std::uint32_t>(Fby::AICommandKind::MoveTo));
    ScopedCommands.Reset();
    ScopedArena.Reset();

    // Tag-gated transition: the trigger alone is not enough while the required
    Fby::StateTreeRaiseTrigger(ScopedState, 1ull << 1);
    ScopedSystem.Run(ScopedContext);
    FBY_CHECK(ScopedState.TransitionsFired == 2);
    FBY_CHECK(ScopedState.CurrentLeaf == ScopedTree.Move);
    FBY_CHECK((ScopedState.Triggers & (1ull << 1)) != 0);
    ScopedCommands.Play(ScopedRegistry);
    ScopedCommands.Reset();
    ScopedArena.Reset();

    // With the tag the same trigger now fires.
    ScopedGameplay.Bits |= Fby::GameplayStateBit(Fby::GameplayStateFlag::Attacking);
    ScopedSystem.Run(ScopedContext);
    FBY_CHECK(ScopedState.TransitionsFired == 3);
    FBY_CHECK(ScopedState.CurrentLeaf == ScopedTree.Attack);
    FBY_CHECK((ScopedState.Triggers & (1ull << 1)) == 0);

    // A task cannot use a missing tree; inactive entities are skipped.
    ScopedState.Active = false;
    const std::uint32_t ScopedFired = ScopedState.TransitionsFired;
    ScopedSystem.Run(ScopedContext);
    FBY_CHECK(ScopedState.TransitionsFired == ScopedFired);
}

void TestRegistration()
{
    const TestTree ScopedTree = MakeTree();
    Fby::StateTreeSystem ScopedSystem;
    FBY_CHECK(ScopedSystem.AddTree(ScopedTree.Definition));

    Fby::Ecs::Registry ScopedRegistry;
    Fby::JobSystem ScopedJobs(1);
    std::array<std::byte, 32u << 10> ScopedArenaStorage{};
    Fby::FrameArena ScopedArena(ScopedArenaStorage);
    Fby::Ecs::CommandBuffer ScopedCommands;
    ScopedCommands.Initialize(&ScopedArena);

    Fby::Ecs::SystemContext ScopedContext{ScopedRegistry, ScopedJobs};
    ScopedContext.commands = &ScopedCommands;
    ScopedContext.dt = 1.0f / 60.0f;

    Fby::Ecs::Schedule ScopedSchedule;
    Fby::RegisterStateTreeSystem(ScopedSchedule, ScopedSystem);

    const Fby::Entity ScopedEntity = ScopedRegistry.create();
    ScopedRegistry.emplace<Fby::Transform2D>(ScopedEntity);
    auto& ScopedState = ScopedRegistry.emplace<Fby::StateTreeComponent>(ScopedEntity);
    ScopedState.TreeId = 7;

    ScopedSchedule.Run(Fby::Ecs::Phase::FixedUpdate, ScopedContext);
    FBY_CHECK(ScopedState.Initialized);
    FBY_CHECK(ScopedCommands.Count() == 1);

    ScopedSchedule.Clear();
    FBY_CHECK(ScopedSchedule.Count(Fby::Ecs::Phase::FixedUpdate) == 0);
}

void TestWithoutCommandBuffer()
{
    const TestTree ScopedTree = MakeTree();
    Fby::StateTreeSystem ScopedSystem;
    FBY_CHECK(ScopedSystem.AddTree(ScopedTree.Definition));

    Fby::Ecs::Registry ScopedRegistry;
    Fby::JobSystem ScopedJobs(1);
    Fby::Ecs::SystemContext ScopedContext{ScopedRegistry, ScopedJobs};
    ScopedContext.dt = 0.05f;

    const Fby::Entity ScopedEntity = ScopedRegistry.create();
    ScopedRegistry.emplace<Fby::Transform2D>(ScopedEntity);
    auto& ScopedState = ScopedRegistry.emplace<Fby::StateTreeComponent>(ScopedEntity);
    ScopedState.TreeId = 7;

    // No CommandBuffer and no pre-existing AICommandComponent: the system must
    ScopedSystem.Run(ScopedContext);
    FBY_CHECK(ScopedState.Initialized);
    FBY_CHECK(ScopedRegistry.try_get<Fby::AICommandComponent>(ScopedEntity) == nullptr);
}
}

int main()
{
    static_assert(std::is_trivially_copyable_v<Fby::StateTreeComponent>);
    static_assert(std::is_standard_layout_v<Fby::StateTreeComponent>);
    static_assert(sizeof(Fby::StateTreeComponent::ActiveStates) == 4 * sizeof(std::uint16_t));
    static_assert(std::is_trivially_copyable_v<Fby::AICommandComponent>);

    TestAuthoringLimits();
    TestSystem();
    TestRegistration();
    TestWithoutCommandBuffer();

    if (GFailures != 0)
    {
        std::fprintf(stderr, "state_tree_test: %u failure(s)\n", GFailures);
        return 1;
    }
    std::printf("state_tree_test: all checks passed\n");
    return 0;
}
