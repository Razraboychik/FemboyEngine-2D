// FemboyEngine2D - FemAnimGraph: the universal animation state machine.
#pragma once

#include "Engine/Core/Mirror.hpp"
#include "Engine/Core/Types.hpp"
#include "Engine/ECS/Schedule.hpp"
#include "Engine/ECS/Skeleton.hpp"
#include "Engine/ECS/SpriteAnimation.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace Fby
{

inline constexpr std::uint32_t kMaxAnimTransitions = 8;
inline constexpr std::uint32_t kMaxAnimClips = 8;
inline constexpr std::uint32_t kMaxAnimPoseKeys = 8;
inline constexpr std::uint32_t kMaxAnimParams = 8;
inline constexpr std::uint16_t kNoAnimTransition = 0xFFFFu;
// FromState sentinel: the transition may fire from any logical state. State 0 is
inline constexpr std::uint64_t kAnimAnyState = 0xFFFFFFFFFFFFFFFFull;

// A transition fires when every flag selected in `Conditions` is true. A flag
enum AnimConditionFlags : std::uint32_t
{
    AnimConditionNone = 0,
    // StateTime >= StateTimeThreshold
    AnimConditionStateTimeAtLeast = 1u << 0,
    // StateTime <= StateTimeThreshold
    AnimConditionStateTimeAtMost = 1u << 1,
    // (GameplayState bits & TagsAny) != 0
    AnimConditionTagsAny = 1u << 2,
    // (GameplayState bits & TagsAll) == TagsAll
    AnimConditionTagsAll = 1u << 3,
    // (GameplayState bits & TagsAny) == 0; reuses TagsAny
    AnimConditionTagsNone = 1u << 4,
    // Floats[FloatParam] >= FloatThreshold
    AnimConditionFloatAtLeast = 1u << 5,
    // Floats[FloatParam] <= FloatThreshold
    AnimConditionFloatAtMost = 1u << 6,
};

// One authored state change. `FromState == kAnimAnyState` means "from any
struct AnimTransition
{
    std::uint64_t FromState = kAnimAnyState;
    std::uint64_t ToState = 0;
    // Tag masks read from the entity's GameplayState (uint64 keeps the engine's
    std::uint64_t TagsAny = 0;
    std::uint64_t TagsAll = 0;
    // State-time threshold, seconds
    float StateTimeThreshold = 0.0f;
    // Float-parameter threshold
    float FloatThreshold = 0.0f;
    // Cross-fade from the previous pose; 0 = instant
    float BlendDuration = 0.12f;
    // Index into AnimStateComponent::Floats
    std::uint16_t FloatParam = 0;
    std::uint32_t Conditions = AnimConditionNone;
    bool Enabled = true;
};

static_assert(std::is_trivially_copyable_v<AnimTransition>);
static_assert(std::is_standard_layout_v<AnimTransition>);

// One animated bone inside a clip. Absolute local pose, not a delta: a clip is
struct AnimBoneKey
{
    std::uint16_t Bone = 0;
    float Rotation = 0.0f;
    Vector2D Position{};
    float Scale = 1.0f;
};

static_assert(sizeof(AnimBoneKey) == 20);
static_assert(std::is_trivially_copyable_v<AnimBoneKey>);

// A clip is shared by every entity in the same logical state. It carries both
struct AnimClip
{
    std::uint64_t State = 0;
    std::array<AnimBoneKey, kMaxAnimPoseKeys> Keys{};
    // SpriteAnimation bridge: absolute first cell, cell count, seconds per cell
    std::uint32_t SpriteStartFrame = 0;
    std::uint32_t SpriteFrameCount = 0;
    float SpriteFrameDuration = 0.1f;
    std::uint16_t KeyCount = 0;
    bool SpriteLooping = true;
    bool SpritePlaying = true;
};

static_assert(std::is_trivially_copyable_v<AnimClip>);
static_assert(std::is_standard_layout_v<AnimClip>);
static_assert(offsetof(AnimClip, State) == 0);

// Captured once per entity: the clip pose is blended against this so a clip may
struct AnimRestBone
{
    Vector2D Position{};
    float Rotation = 0.0f;
    float Scale = 1.0f;
};

static_assert(sizeof(AnimRestBone) == 16);

// The component the specification calls for, plus the data the two bridges need.
struct AnimStateComponent
{
    // Logical state. 0 = rest pose, no clip.
    std::uint64_t CurrentState = 0;
    // State the cross-fade is coming from
    std::uint64_t PreviousState = 0;
    // Seconds spent in CurrentState; reset on every transition
    float StateTime = 0.0f;
    // Diagnostics, never gameplay input
    std::uint32_t TransitionCount = 0;
    // Authored transitions, evaluated in array order, first match wins
    std::array<AnimTransition, kMaxAnimTransitions> Transitions{};

    std::array<AnimClip, kMaxAnimClips> Clips{};
    // Float parameters the conditions read (speed, health, aim pitch, ...)
    std::array<float, kMaxAnimParams> Floats{};
    // Rest pose captured from the skeleton on the first driven tick
    std::array<AnimRestBone, kMaxBones> RestPose{};

    // Cross-fade timing
    float BlendDuration = 0.0f;
    float BlendTime = 0.0f;
    // 0 = previous pose, 1 = current pose
    float BlendWeight = 1.0f;

    // Diagnostics, never gameplay input
    std::uint32_t ClipCount = 0;
    std::uint16_t LastTransition = kNoAnimTransition;
    bool RestPoseCaptured = false;

    // Cache-line tail padding. The component is stored once per animated entity
    std::array<std::byte, 48> CacheLinePadding{};
};

static_assert(std::is_trivially_copyable_v<AnimStateComponent>);
static_assert(std::is_standard_layout_v<AnimStateComponent>);
static_assert(offsetof(AnimStateComponent, CurrentState) == 0);
static_assert(offsetof(AnimStateComponent, PreviousState) == 8);
static_assert(offsetof(AnimStateComponent, StateTime) == 16);
static_assert(offsetof(AnimStateComponent, TransitionCount) == 20);
static_assert(offsetof(AnimStateComponent, Transitions) == 24);
static_assert(sizeof(AnimStateComponent) % 64 == 0,
              "AnimStateComponent must end on a cache-line boundary");
static_assert(sizeof(AnimStateComponent) == 4096,
              "AnimStateComponent layout changed: update CacheLinePadding and the audit note");

// Pure helpers (unit-testable without a registry)
[[nodiscard]] const AnimClip* AnimGraphFindClip(const AnimStateComponent& InState, std::uint64_t InStateId) noexcept;

// Captures the skeleton's current local pose as the rest pose if it has not been
void AnimGraphCaptureRestPose(AnimStateComponent& InState, const SkeletonComponent& InSkeleton) noexcept;

// True when every condition in the transition holds for the given state, tags
[[nodiscard]] bool AnimTransitionMatches(const AnimTransition& InTransition, const AnimStateComponent& InState,
                                         std::uint64_t InTags) noexcept;

// Writes the blended local pose into the skeleton and runs one SkeletalAnimation evaluation,
void AnimGraphApplySkeletal(AnimStateComponent& InState, SkeletonComponent& InSkeleton) noexcept;

// Switches the SpriteAnimation clip to the current state's frame range. The UV rect is
void AnimGraphApplySpriteClip(const AnimStateComponent& InState, SpriteAnimationComponent& InSpriteAnimation,
                         const SpriteSheetComponent* InGrid, Sprite2D* InSprite) noexcept;

// System
class AnimGraphSystem
{
public:
    // One pass per update. Allocation-free; reads/writes only components.
    void Run(Ecs::SystemContext& InContext) const noexcept;
    void Run(Ecs::Registry& InRegistry, float InDt) const noexcept;
};

void RegisterAnimGraphSystem(Ecs::Schedule& InSchedule);
void RegisterAnimGraphSystem(Ecs::SystemGraph& InGraph);
}

// Mirror registration
FBY_REFLECT_COMPONENT(Fby::AnimStateComponent, "AnimStateComponent",
                      FBY_FIELD(Fby::AnimStateComponent, CurrentState, UInt64),
                      FBY_FIELD(Fby::AnimStateComponent, PreviousState, UInt64),
                      FBY_FIELD(Fby::AnimStateComponent, StateTime, Float),
                      FBY_FIELD(Fby::AnimStateComponent, BlendDuration, Float),
                      FBY_FIELD(Fby::AnimStateComponent, BlendTime, Float),
                      FBY_FIELD(Fby::AnimStateComponent, BlendWeight, Float),
                      FBY_FIELD(Fby::AnimStateComponent, TransitionCount, UInt32),
                      FBY_FIELD(Fby::AnimStateComponent, ClipCount, UInt32))

// Authoring-side tables. These types are not components, but the animation
FBY_REFLECT_COMPONENT(Fby::AnimTransition, "AnimTransition",
                      FBY_FIELD(Fby::AnimTransition, FromState, UInt64),
                      FBY_FIELD(Fby::AnimTransition, ToState, UInt64),
                      FBY_FIELD(Fby::AnimTransition, TagsAny, UInt64),
                      FBY_FIELD(Fby::AnimTransition, TagsAll, UInt64),
                      FBY_FIELD(Fby::AnimTransition, StateTimeThreshold, Float),
                      FBY_FIELD(Fby::AnimTransition, FloatThreshold, Float),
                      FBY_FIELD(Fby::AnimTransition, BlendDuration, Float),
                      FBY_FIELD(Fby::AnimTransition, FloatParam, UInt32),
                      FBY_FIELD(Fby::AnimTransition, Conditions, UInt32),
                      FBY_FIELD(Fby::AnimTransition, Enabled, Bool))

FBY_REFLECT_COMPONENT(Fby::AnimClip, "AnimClip",
                      FBY_FIELD(Fby::AnimClip, State, UInt64),
                      FBY_FIELD(Fby::AnimClip, SpriteStartFrame, UInt32),
                      FBY_FIELD(Fby::AnimClip, SpriteFrameCount, UInt32),
                      FBY_FIELD(Fby::AnimClip, SpriteFrameDuration, Float),
                      FBY_FIELD(Fby::AnimClip, KeyCount, UInt32),
                      FBY_FIELD(Fby::AnimClip, SpriteLooping, Bool),
                      FBY_FIELD(Fby::AnimClip, SpritePlaying, Bool))
