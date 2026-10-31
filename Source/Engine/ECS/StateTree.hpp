// FemboyEngine2D - FemStateTree: flat, change-driven AI state trees.
#pragma once

#include "Engine/Core/Mirror.hpp"
#include "Engine/Core/Types.hpp"
#include "Engine/ECS/Components.hpp"
#include "Engine/ECS/Schedule.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace Fby
{

inline constexpr std::uint32_t kMaxStateTreeDepth = 4;
inline constexpr std::uint32_t kMaxStateTreeNodes = 64;
inline constexpr std::uint32_t kMaxStateTreeTransitions = 32;
inline constexpr std::uint32_t kMaxStateTrees = 4;
inline constexpr std::uint16_t kStateTreeNone = 0xFFFFu;
// Internal trigger raised when the active leaf's own condition turns false. An
inline constexpr std::uint64_t kStateTreeTriggerLeafFailed = 1ull << 63;

// Evaluators: pure data + a flat switch, no per-node object
enum class StateTreeEvaluatorKind : std::uint8_t
{
    AlwaysTrue = 0,
    AlwaysFalse,
    // (Tags & TagMask) != 0
    TagsAny,
    // (Tags & TagMask) == TagMask (an empty mask is false)
    TagsAll,
    // (Tags & TagMask) == 0
    TagsNone,
    // Distance(entity, Target) <= Threshold
    TargetWithinRadius,
    // Distance(entity, Target) > Threshold
    TargetBeyondRadius,
    // Floats[FloatIndex] < Threshold
    FloatBelow,
    // Floats[FloatIndex] >= Threshold
    FloatAbove,
    // StateTime >= Threshold
    StateTimeAtLeast,
};

struct StateTreeEvaluator
{
    StateTreeEvaluatorKind Kind = StateTreeEvaluatorKind::AlwaysTrue;
    std::uint64_t TagMask = 0;
    float Threshold = 0.0f;
    // Index into StateTreeComponent::Floats (0..3)
    std::uint16_t FloatIndex = 0;
    std::uint16_t Reserved = 0;
};

static_assert(std::is_trivially_copyable_v<StateTreeEvaluator>);

// Tasks: the only place that produces gameplay commands
enum class StateTreeTaskKind : std::uint8_t
{
    None = 0,
    MoveToTarget,
    Stop,
    AttackTarget,
    SetTag,
    ClearTag,
};

struct StateTreeTask
{
    StateTreeTaskKind Kind = StateTreeTaskKind::None;
    std::uint64_t TagMask = 0;
    // Movement speed / attack range; interpretation belongs to the command
    float Speed = 128.0f;
    float Range = 32.0f;
    std::uint32_t Flags = 0;
};

static_assert(std::is_trivially_copyable_v<StateTreeTask>);

// Nodes and transitions
enum class StateTreeNodeKind : std::uint8_t
{
    // The only root; its children are the top-level states.
    Root = 0,
    // A group: descends into the first child whose condition passes.
    Selector,
    // A leaf: owns the evaluator that guards it and the task that runs.
    Leaf,
};

struct StateTreeNode
{
    StateTreeNodeKind Kind = StateTreeNodeKind::Leaf;
    std::uint16_t Parent = kStateTreeNone;
    // Intrusive child linked list: children do not have to be contiguous.
    std::uint16_t FirstChild = kStateTreeNone;
    std::uint16_t NextSibling = kStateTreeNone;
    std::uint16_t ChildCount = 0;
    // 1 = root depth; enforced <= kMaxStateTreeDepth by StateTreeAddNode().
    std::uint8_t Depth = 1;
    bool Enabled = true;
    StateTreeEvaluator Evaluator{};
    StateTreeTask Task{};
};

static_assert(std::is_trivially_copyable_v<StateTreeNode>);
static_assert(std::is_standard_layout_v<StateTreeNode>);

// A transition is fire-and-forget data. The system checks the active leaf's
struct StateTreeTransition
{
    // kStateTreeNone = any active leaf
    std::uint16_t FromLeaf = kStateTreeNone;
    std::uint16_t ToNode = 0;
    // Any of these pending trigger bits fires the transition
    std::uint64_t TriggerMask = 0;
    // Bits cleared when it fires (usually TriggerMask). LeafFailed is always
    std::uint64_t ConsumeMask = 0;
    std::uint64_t RequireTagsAll = 0;
    std::uint64_t ForbidTagsAny = 0;
    bool Enabled = true;
    bool ConsumeAll = false;
};

static_assert(std::is_trivially_copyable_v<StateTreeTransition>);

struct StateTreeDefinition
{
    std::uint32_t TreeId = 0;
    std::uint16_t Root = 0;
    std::uint16_t NodeCount = 0;
    std::uint16_t TransitionCount = 0;
    std::array<StateTreeNode, kMaxStateTreeNodes> Nodes{};
    std::array<StateTreeTransition, kMaxStateTreeTransitions> Transitions{};
};

// Gameplay command emitted by a task through CommandBuffer
enum class AICommandKind : std::uint8_t
{
    None = 0,
    MoveTo,
    Stop,
    Attack,
};

// Stored as uint32 so Mirror can describe it with FieldKind::UInt32 (the
struct AICommandComponent
{
    std::uint32_t Kind = static_cast<std::uint32_t>(AICommandKind::None);
    Entity Target = entt::null;
    Vector2D Position{};
    float Speed = 0.0f;
    float Range = 0.0f;
    std::uint32_t Flags = 0;
    std::uint32_t TaskIndex = 0;
    // Fixed-step tick that produced the command; consumers can drop stale ones.
    std::uint32_t Sequence = 0;
};

static_assert(std::is_trivially_copyable_v<AICommandComponent>);
static_assert(std::is_standard_layout_v<AICommandComponent>);

// Per-entity state
struct StateTreeComponent
{
    // The mandated path array: ActiveStates[0] is the root, the leaf is at
    std::array<std::uint16_t, kMaxStateTreeDepth> ActiveStates{kStateTreeNone, kStateTreeNone, kStateTreeNone,
                                                               kStateTreeNone};

    // Blackboard / trigger state
    std::uint64_t Triggers = 0;
    std::uint64_t Tags = 0;
    Entity Target = entt::null;
    Vector2D TargetPosition{};
    std::array<float, 4> Floats{};

    // Timing / diagnostics
    float StateTime = 0.0f;
    std::uint32_t Ticks = 0;
    std::uint32_t TransitionsFired = 0;
    std::uint32_t TreeId = 0;
    std::uint32_t CurrentLeaf = kStateTreeNone;
    std::uint32_t ActiveDepth = 0;
    bool Initialized = false;
    bool Active = true;
};

static_assert(std::is_trivially_copyable_v<StateTreeComponent>);
static_assert(std::is_standard_layout_v<StateTreeComponent>);
static_assert(sizeof(StateTreeComponent::ActiveStates) == kMaxStateTreeDepth * sizeof(std::uint16_t));

// Raises one or more external trigger bits (gameplay event, perception system,
constexpr void StateTreeRaiseTrigger(StateTreeComponent& InState, std::uint64_t InMask) noexcept
{
    InState.Triggers |= InMask;
}

// Authoring helpers (fixed capacity, no allocation)
[[nodiscard]] std::uint16_t StateTreeAddNode(StateTreeDefinition& InTree, StateTreeNodeKind InKind,
                                             std::uint16_t InParent = kStateTreeNone) noexcept;

// Attaches an evaluator and a task to a leaf. Safe to call on a group too, in
void StateTreeSetLeaf(StateTreeDefinition& InTree, std::uint16_t InNode, const StateTreeEvaluator& InEvaluator,
                      const StateTreeTask& InTask) noexcept;

[[nodiscard]] bool StateTreeAddTransition(StateTreeDefinition& InTree, const StateTreeTransition& InTransition) noexcept;

// System
class StateTreeSystem
{
public:
    // Copies the definition into the fixed slot table. Rejects a full table or a
    [[nodiscard]] bool AddTree(const StateTreeDefinition& InTree) noexcept;
    void ClearTrees() noexcept;
    [[nodiscard]] std::uint32_t TreeCount() const noexcept
    {
        return MTreeCount;
    }
    [[nodiscard]] const StateTreeDefinition* Tree(std::uint32_t InTreeId) const noexcept;

    // FixedUpdate pass. Allocation-free. Tasks write AICommandComponent through
    void Run(Ecs::SystemContext& InContext) const noexcept;
    void Run(Ecs::Registry& InRegistry, float InDt, Ecs::CommandBuffer* InCommands) const noexcept;

private:
    std::array<StateTreeDefinition, kMaxStateTrees> MTrees{};
    std::uint32_t MTreeCount = 0;
};

// The system outlives the registration: the graph stores a pointer to it.
void RegisterStateTreeSystem(Ecs::Schedule& InSchedule, StateTreeSystem& InSystem);
void RegisterStateTreeSystem(Ecs::SystemGraph& InGraph, StateTreeSystem& InSystem);
}

// Mirror registration
FBY_REFLECT_COMPONENT(Fby::StateTreeComponent, "StateTreeComponent",
                      FBY_FIELD(Fby::StateTreeComponent, TreeId, UInt32),
                      FBY_FIELD(Fby::StateTreeComponent, CurrentLeaf, UInt32),
                      FBY_FIELD(Fby::StateTreeComponent, ActiveDepth, UInt32),
                      FBY_FIELD(Fby::StateTreeComponent, StateTime, Float),
                      FBY_FIELD(Fby::StateTreeComponent, Ticks, UInt32),
                      FBY_FIELD(Fby::StateTreeComponent, TransitionsFired, UInt32),
                      FBY_FIELD(Fby::StateTreeComponent, Triggers, UInt64),
                      FBY_FIELD(Fby::StateTreeComponent, Tags, UInt64),
                      FBY_FIELD(Fby::StateTreeComponent, Target, Entity),
                      FBY_FIELD(Fby::StateTreeComponent, TargetPosition, Vector2D),
                      FBY_FIELD(Fby::StateTreeComponent, Initialized, Bool),
                      FBY_FIELD(Fby::StateTreeComponent, Active, Bool))

FBY_REFLECT_COMPONENT(Fby::AICommandComponent, "AICommandComponent",
                      FBY_FIELD(Fby::AICommandComponent, Kind, UInt32),
                      FBY_FIELD(Fby::AICommandComponent, Target, Entity),
                      FBY_FIELD(Fby::AICommandComponent, Position, Vector2D),
                      FBY_FIELD(Fby::AICommandComponent, Speed, Float),
                      FBY_FIELD(Fby::AICommandComponent, Range, Float),
                      FBY_FIELD(Fby::AICommandComponent, Flags, UInt32),
                      FBY_FIELD(Fby::AICommandComponent, TaskIndex, UInt32),
                      FBY_FIELD(Fby::AICommandComponent, Sequence, UInt32))
