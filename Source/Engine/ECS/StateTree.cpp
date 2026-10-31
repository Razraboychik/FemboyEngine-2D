// FemboyEngine2D - FemStateTree implementation.
#include "Engine/ECS/StateTree.hpp"

#include "Engine/ECS/CommandBuffer.hpp"
#include "Engine/ECS/SystemGraph.hpp"

#include <algorithm>
#include <cmath>

namespace Fby
{
namespace
{
[[nodiscard]] float SquaredDistance(Vector2D InA, Vector2D InB) noexcept
{
    const float ScopedX = InB.X - InA.X;
    const float ScopedY = InB.Y - InA.Y;
    return ScopedX * ScopedX + ScopedY * ScopedY;
}

// The target's live transform wins; TargetPosition is the fallback for a target
[[nodiscard]] Vector2D ResolveTargetPosition(const Ecs::Registry& InRegistry, const StateTreeComponent& InState) noexcept
{
    if (InState.Target != entt::null)
    {
        if (const auto* ScopedTransform = InRegistry.try_get<Transform2D>(InState.Target);
            ScopedTransform != nullptr)
        {
            return ScopedTransform->Position;
        }
    }
    return InState.TargetPosition;
}

[[nodiscard]] bool EvaluateCondition(const Ecs::Registry& InRegistry, Entity InEntity,
                                     const StateTreeComponent& InState,
                                     const StateTreeEvaluator& InEvaluator) noexcept
{
    switch (InEvaluator.Kind)
    {
        case StateTreeEvaluatorKind::AlwaysTrue:
            return true;
        case StateTreeEvaluatorKind::AlwaysFalse:
            return false;
        case StateTreeEvaluatorKind::TagsAny:
            return InEvaluator.TagMask != 0 && (InState.Tags & InEvaluator.TagMask) != 0;
        case StateTreeEvaluatorKind::TagsAll:
            return InEvaluator.TagMask != 0 && (InState.Tags & InEvaluator.TagMask) == InEvaluator.TagMask;
        case StateTreeEvaluatorKind::TagsNone:
            return InEvaluator.TagMask != 0 && (InState.Tags & InEvaluator.TagMask) == 0;
        case StateTreeEvaluatorKind::TargetWithinRadius:
        case StateTreeEvaluatorKind::TargetBeyondRadius:
        {
            const auto* ScopedSelf = InRegistry.try_get<Transform2D>(InEntity);
            if (ScopedSelf == nullptr)
            {
                return false;
            }
            const float ScopedRadius = std::max(InEvaluator.Threshold, 0.0f);
            const bool ScopedWithin =
                SquaredDistance(ScopedSelf->Position, ResolveTargetPosition(InRegistry, InState)) <=
                ScopedRadius * ScopedRadius;
            return InEvaluator.Kind == StateTreeEvaluatorKind::TargetWithinRadius ? ScopedWithin : !ScopedWithin;
        }
        case StateTreeEvaluatorKind::FloatBelow:
        case StateTreeEvaluatorKind::FloatAbove:
        {
            if (InEvaluator.FloatIndex >= InState.Floats.size())
            {
                return false;
            }
            const float ScopedValue = InState.Floats[InEvaluator.FloatIndex];
            if (!std::isfinite(ScopedValue))
            {
                return false;
            }
            return InEvaluator.Kind == StateTreeEvaluatorKind::FloatBelow ? ScopedValue < InEvaluator.Threshold
                                                                          : ScopedValue >= InEvaluator.Threshold;
        }
        case StateTreeEvaluatorKind::StateTimeAtLeast:
            return InState.StateTime >= InEvaluator.Threshold;
    }
    return false;
}

// Descends from InNode to an enabled leaf. A group selects the first child whose
[[nodiscard]] std::uint16_t SelectLeaf(const Ecs::Registry& InRegistry, Entity InEntity,
                                       const StateTreeComponent& InState, const StateTreeDefinition& InTree,
                                       std::uint16_t InNode, std::uint32_t InDepth) noexcept
{
    if (InNode >= InTree.NodeCount || InDepth > kMaxStateTreeDepth)
    {
        return kStateTreeNone;
    }
    const StateTreeNode& ScopedNode = InTree.Nodes[InNode];
    if (!ScopedNode.Enabled)
    {
        return kStateTreeNone;
    }
    if (ScopedNode.Kind == StateTreeNodeKind::Leaf || ScopedNode.FirstChild == kStateTreeNone)
    {
        return InNode;
    }

    std::uint16_t ScopedFallback = kStateTreeNone;
    std::uint16_t ScopedChild = ScopedNode.FirstChild;
    while (ScopedChild != kStateTreeNone && ScopedChild < InTree.NodeCount)
    {
        const StateTreeNode& ScopedCandidate = InTree.Nodes[ScopedChild];
        if (ScopedCandidate.Enabled)
        {
            if (ScopedFallback == kStateTreeNone)
            {
                ScopedFallback = ScopedChild;
            }
            if (EvaluateCondition(InRegistry, InEntity, InState, ScopedCandidate.Evaluator))
            {
                const std::uint16_t ScopedLeaf =
                    SelectLeaf(InRegistry, InEntity, InState, InTree, ScopedChild, InDepth + 1);
                if (ScopedLeaf != kStateTreeNone)
                {
                    return ScopedLeaf;
                }
            }
        }
        ScopedChild = ScopedCandidate.NextSibling;
    }
    if (ScopedFallback != kStateTreeNone)
    {
        return SelectLeaf(InRegistry, InEntity, InState, InTree, ScopedFallback, InDepth + 1);
    }
    return kStateTreeNone;
}

// Writes the root-first path into ActiveStates. The parent chain is authored and
void BuildPath(const StateTreeDefinition& InTree, std::uint16_t InLeaf, StateTreeComponent& InState) noexcept
{
    InState.ActiveStates.fill(kStateTreeNone);
    std::array<std::uint16_t, kMaxStateTreeDepth> ScopedReversed{};
    std::uint32_t ScopedCount = 0;
    std::uint16_t ScopedNode = InLeaf;
    while (ScopedNode != kStateTreeNone && ScopedCount < kMaxStateTreeDepth && ScopedNode < InTree.NodeCount)
    {
        ScopedReversed[ScopedCount++] = ScopedNode;
        if (ScopedNode == InTree.Root)
        {
            break;
        }
        ScopedNode = InTree.Nodes[ScopedNode].Parent;
    }
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedCount; ++ScopedIndex)
    {
        InState.ActiveStates[ScopedIndex] = ScopedReversed[ScopedCount - 1u - ScopedIndex];
    }
    InState.ActiveDepth = ScopedCount;
    InState.CurrentLeaf = InLeaf;
    InState.Ticks = 0;
    InState.StateTime = 0.0f;
}

[[nodiscard]] AICommandComponent MakeCommand(AICommandKind InKind, const StateTreeComponent& InState,
                                             const StateTreeTask& InTask, std::uint16_t InNodeIndex) noexcept
{
    AICommandComponent ScopedCommand;
    ScopedCommand.Kind = static_cast<std::uint32_t>(InKind);
    ScopedCommand.Target = InState.Target;
    ScopedCommand.Position = InState.TargetPosition;
    ScopedCommand.Speed = InTask.Speed;
    ScopedCommand.Range = InTask.Range;
    ScopedCommand.Flags = InTask.Flags;
    ScopedCommand.TaskIndex = InNodeIndex;
    ScopedCommand.Sequence = InState.Ticks;
    return ScopedCommand;
}

void EmitCommand(Ecs::Registry& InRegistry, Entity InEntity, Ecs::CommandBuffer* InCommands,
                 const AICommandComponent& InCommand) noexcept
{
    // The command buffer is the supported path: it defers the write to the safe
    if (InCommands != nullptr)
    {
        (void)InCommands->Emplace<AICommandComponent>(InEntity, InCommand);
        return;
    }
    // No buffer (a headless tool or a unit test): write in place only if the
    if (auto* ScopedExisting = InRegistry.try_get<AICommandComponent>(InEntity); ScopedExisting != nullptr)
    {
        *ScopedExisting = InCommand;
    }
}

void RunTask(Ecs::Registry& InRegistry, Entity InEntity, StateTreeComponent& InState,
             const StateTreeDefinition& InTree, std::uint16_t InNodeIndex, Ecs::CommandBuffer* InCommands) noexcept
{
    if (InNodeIndex >= InTree.NodeCount)
    {
        return;
    }
    const StateTreeTask& ScopedTask = InTree.Nodes[InNodeIndex].Task;
    switch (ScopedTask.Kind)
    {
        case StateTreeTaskKind::None:
            return;
        case StateTreeTaskKind::MoveToTarget:
        {
            AICommandComponent ScopedCommand = MakeCommand(AICommandKind::MoveTo, InState, ScopedTask, InNodeIndex);
            ScopedCommand.Position = ResolveTargetPosition(InRegistry, InState);
            EmitCommand(InRegistry, InEntity, InCommands, ScopedCommand);
            return;
        }
        case StateTreeTaskKind::Stop:
            EmitCommand(InRegistry, InEntity, InCommands,
                        MakeCommand(AICommandKind::Stop, InState, ScopedTask, InNodeIndex));
            return;
        case StateTreeTaskKind::AttackTarget:
        {
            AICommandComponent ScopedCommand = MakeCommand(AICommandKind::Attack, InState, ScopedTask, InNodeIndex);
            ScopedCommand.Position = ResolveTargetPosition(InRegistry, InState);
            EmitCommand(InRegistry, InEntity, InCommands, ScopedCommand);
            return;
        }
        case StateTreeTaskKind::SetTag:
        case StateTreeTaskKind::ClearTag:
        {
            // Tags are gameplay state, not movement intent: write the component
            if (auto* ScopedGameplay = InRegistry.try_get<GameplayState>(InEntity); ScopedGameplay != nullptr)
            {
                if (ScopedTask.Kind == StateTreeTaskKind::SetTag)
                {
                    ScopedGameplay->Bits |= ScopedTask.TagMask;
                }
                else
                {
                    ScopedGameplay->Bits &= ~ScopedTask.TagMask;
                }
                InState.Tags = ScopedGameplay->Bits;
            }
            return;
        }
    }
}
}

std::uint16_t StateTreeAddNode(StateTreeDefinition& InTree, StateTreeNodeKind InKind, std::uint16_t InParent) noexcept
{
    if (InTree.NodeCount >= kMaxStateTreeNodes)
    {
        return kStateTreeNone;
    }
    StateTreeNode ScopedNode;
    ScopedNode.Kind = InKind;
    ScopedNode.Parent = InParent;
    if (InParent == kStateTreeNone)
    {
        // Exactly one root, and it must be the first node.
        if (InTree.NodeCount != 0 || InKind != StateTreeNodeKind::Root)
        {
            return kStateTreeNone;
        }
        ScopedNode.Depth = 1;
        InTree.Root = 0;
    }
    else
    {
        if (InParent >= InTree.NodeCount || InKind == StateTreeNodeKind::Root)
        {
            return kStateTreeNone;
        }
        const StateTreeNode& ScopedParent = InTree.Nodes[InParent];
        if (ScopedParent.Depth + 1 > kMaxStateTreeDepth)
        {
            return kStateTreeNone;
        }
        ScopedNode.Depth = static_cast<std::uint8_t>(ScopedParent.Depth + 1);
        StateTreeNode& ScopedWritableParent = InTree.Nodes[InParent];
        if (ScopedWritableParent.FirstChild == kStateTreeNone)
        {
            ScopedWritableParent.FirstChild = static_cast<std::uint16_t>(InTree.NodeCount);
        }
        else
        {
            std::uint16_t ScopedSibling = ScopedWritableParent.FirstChild;
            std::uint32_t ScopedGuard = 0;
            while (InTree.Nodes[ScopedSibling].NextSibling != kStateTreeNone && ScopedGuard < kMaxStateTreeNodes)
            {
                ScopedSibling = InTree.Nodes[ScopedSibling].NextSibling;
                ++ScopedGuard;
            }
            InTree.Nodes[ScopedSibling].NextSibling = static_cast<std::uint16_t>(InTree.NodeCount);
        }
        ++ScopedWritableParent.ChildCount;
    }
    const auto ScopedIndex = static_cast<std::uint16_t>(InTree.NodeCount);
    InTree.Nodes[InTree.NodeCount++] = ScopedNode;
    return ScopedIndex;
}

void StateTreeSetLeaf(StateTreeDefinition& InTree, std::uint16_t InNode, const StateTreeEvaluator& InEvaluator,
                      const StateTreeTask& InTask) noexcept
{
    if (InNode >= InTree.NodeCount)
    {
        return;
    }
    InTree.Nodes[InNode].Evaluator = InEvaluator;
    InTree.Nodes[InNode].Task = InTask;
}

bool StateTreeAddTransition(StateTreeDefinition& InTree, const StateTreeTransition& InTransition) noexcept
{
    if (InTree.TransitionCount >= kMaxStateTreeTransitions || InTransition.ToNode >= InTree.NodeCount ||
        InTransition.TriggerMask == 0)
    {
        return false;
    }
    InTree.Transitions[InTree.TransitionCount++] = InTransition;
    return true;
}

bool StateTreeSystem::AddTree(const StateTreeDefinition& InTree) noexcept
{
    if (MTreeCount >= kMaxStateTrees || InTree.NodeCount == 0 || InTree.NodeCount > kMaxStateTreeNodes)
    {
        return false;
    }
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < MTreeCount; ++ScopedIndex)
    {
        if (MTrees[ScopedIndex].TreeId == InTree.TreeId)
        {
            return false;
        }
    }
    MTrees[MTreeCount++] = InTree;
    return true;
}

void StateTreeSystem::ClearTrees() noexcept
{
    for (StateTreeDefinition& ScopedTree : MTrees)
    {
        ScopedTree = StateTreeDefinition{};
    }
    MTreeCount = 0;
}

const StateTreeDefinition* StateTreeSystem::Tree(std::uint32_t InTreeId) const noexcept
{
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < MTreeCount; ++ScopedIndex)
    {
        if (MTrees[ScopedIndex].TreeId == InTreeId)
        {
            return &MTrees[ScopedIndex];
        }
    }
    return nullptr;
}

void StateTreeSystem::Run(Ecs::Registry& InRegistry, float InDt, Ecs::CommandBuffer* InCommands) const noexcept
{
    const float ScopedDt = std::isfinite(InDt) && InDt > 0.0f ? InDt : 0.0f;
    auto ScopedView = InRegistry.view<StateTreeComponent>();
    for (auto [ScopedEntity, ScopedState] : ScopedView.each())
    {
        if (!ScopedState.Active)
        {
            continue;
        }
        const StateTreeDefinition* ScopedTree = Tree(ScopedState.TreeId);
        if (ScopedTree == nullptr || ScopedTree->NodeCount == 0)
        {
            continue;
        }

        // One component read per entity per tick; every evaluator sees the same
        if (const auto* ScopedGameplay = InRegistry.try_get<GameplayState>(ScopedEntity); ScopedGameplay != nullptr)
        {
            ScopedState.Tags = ScopedGameplay->Bits;
        }
        else
        {
            ScopedState.Tags = 0;
        }
        ++ScopedState.Ticks;
        ScopedState.StateTime += ScopedDt;

        if (!ScopedState.Initialized)
        {
            const std::uint16_t ScopedLeaf =
                SelectLeaf(InRegistry, ScopedEntity, ScopedState, *ScopedTree, ScopedTree->Root, 1);
            if (ScopedLeaf == kStateTreeNone)
            {
                // A tree with no enabled leaf is broken data; stay inactive rather
                continue;
            }
            BuildPath(*ScopedTree, ScopedLeaf, ScopedState);
            ScopedState.Initialized = true;
        }
        else
        {
            // Only the active leaf is guarded each tick. A failing guard raises an
            if (ScopedState.CurrentLeaf >= ScopedTree->NodeCount ||
                !EvaluateCondition(InRegistry, ScopedEntity, ScopedState,
                                   ScopedTree->Nodes[ScopedState.CurrentLeaf].Evaluator))
            {
                ScopedState.Triggers |= kStateTreeTriggerLeafFailed;
            }
        }

        if (ScopedState.Triggers != 0)
        {
            // Change-driven: the loop only runs when at least one trigger bit is
            const std::uint16_t ScopedCount =
                std::min<std::uint16_t>(ScopedTree->TransitionCount, kMaxStateTreeTransitions);
            for (std::uint16_t ScopedIndex = 0; ScopedIndex < ScopedCount; ++ScopedIndex)
            {
                const StateTreeTransition& ScopedTransition = ScopedTree->Transitions[ScopedIndex];
                if (!ScopedTransition.Enabled)
                {
                    continue;
                }
                if (ScopedTransition.FromLeaf != kStateTreeNone &&
                    ScopedTransition.FromLeaf != ScopedState.CurrentLeaf)
                {
                    continue;
                }
                if ((ScopedState.Triggers & ScopedTransition.TriggerMask) == 0)
                {
                    continue;
                }
                if (ScopedTransition.RequireTagsAll != 0 &&
                    (ScopedState.Tags & ScopedTransition.RequireTagsAll) != ScopedTransition.RequireTagsAll)
                {
                    continue;
                }
                if (ScopedTransition.ForbidTagsAny != 0 &&
                    (ScopedState.Tags & ScopedTransition.ForbidTagsAny) != 0)
                {
                    continue;
                }
                const std::uint16_t ScopedLeaf =
                    SelectLeaf(InRegistry, ScopedEntity, ScopedState, *ScopedTree, ScopedTransition.ToNode, 1);
                if (ScopedLeaf == kStateTreeNone)
                {
                    continue;
                }
                if (ScopedTransition.ConsumeAll)
                {
                    ScopedState.Triggers = 0;
                }
                else
                {
                    ScopedState.Triggers &= ~ScopedTransition.ConsumeMask;
                }
                // The internal guard bit never leaks into the next state.
                ScopedState.Triggers &= ~kStateTreeTriggerLeafFailed;
                BuildPath(*ScopedTree, ScopedLeaf, ScopedState);
                ++ScopedState.TransitionsFired;
                break;
            }
        }

        // The leaf task runs once per fixed tick. It is a switch on a small enum,
        if (ScopedState.CurrentLeaf < ScopedTree->NodeCount)
        {
            RunTask(InRegistry, ScopedEntity, ScopedState, *ScopedTree,
                    static_cast<std::uint16_t>(ScopedState.CurrentLeaf), InCommands);
        }
    }
}

void StateTreeSystem::Run(Ecs::SystemContext& InContext) const noexcept
{
    Run(InContext.registry, InContext.dt, InContext.commands);
}

void RegisterStateTreeSystem(Ecs::Schedule& InSchedule, StateTreeSystem& InSystem)
{
    (void)InSchedule.Add(
        Ecs::Phase::FixedUpdate, "state_tree",
        +[](Ecs::SystemContext& InContext, void* InUser) { static_cast<StateTreeSystem*>(InUser)->Run(InContext); },
        &InSystem);
}

void RegisterStateTreeSystem(Ecs::SystemGraph& InGraph, StateTreeSystem& InSystem)
{
    (void)InGraph.Add(
        Ecs::Phase::FixedUpdate, "state_tree",
        +[](Ecs::SystemContext& InContext, void* InUser) { static_cast<StateTreeSystem*>(InUser)->Run(InContext); },
        &InSystem);
}
}
