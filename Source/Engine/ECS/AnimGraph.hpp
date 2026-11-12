// FemboyEngine2D - FemAnimGraph: the universal animation state machine.
#pragma once

#include "Engine/Core/Mirror.hpp"
#include "Engine/Core/Types.hpp"
#include "Engine/ECS/DirectionalBlend.hpp"
#include "Engine/ECS/Schedule.hpp"
#include "Engine/ECS/Skeleton.hpp"
#include "Engine/ECS/SpriteAnimation.hpp"
#include "Engine/Gameplay/GameplayTags.hpp"

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
// FromState sentinel: the transition may fire from any logical state.
inline constexpr std::uint64_t kAnimAnyState = 0xFFFFFFFFFFFFFFFFull;

// A transition fires when every flag selected in `Conditions` is true. A flag
enum AnimConditionFlags : std::uint32_t
{
    AnimConditionNone = 0,
    // StateTime >= StateTimeThreshold
    AnimConditionStateTimeAtLeast = 1u << 0,
    // StateTime <= StateTimeThreshold
    AnimConditionStateTimeAtMost = 1u << 1,
    // Any of TagsAny[0..TagsAnyCount) is present (hierarchical)
    AnimConditionTagsAny = 1u << 2,
    // All of TagsAll[0..TagsAllCount) are present (hierarchical)
    AnimConditionTagsAll = 1u << 3,
    // None of TagsAny[0..TagsAnyCount) is present
    AnimConditionTagsNone = 1u << 4,
    // Floats[FloatParam] >= FloatThreshold
    AnimConditionFloatAtLeast = 1u << 5,
    // Floats[FloatParam] <= FloatThreshold
    AnimConditionFloatAtMost = 1u << 6,
};

// One authored state change.
struct AnimTransition
{
    std::uint64_t FromState = kAnimAnyState;
    std::uint64_t ToState = 0;
    // Up to two required and two any-tags per transition; the runtime resolves
    // each id hierarchically through the project tag registry.
    std::array<GameplayTag, 2> TagsAny{};
    std::array<GameplayTag, 2> TagsAll{};
    // State-time threshold, seconds
    float StateTimeThreshold = 0.0f;
    // Float-parameter threshold
    float FloatThreshold = 0.0f;
    // Cross-fade from the previous pose; 0 = instant
    float BlendDuration = 0.12f;
    // Index into AnimStateComponent::Floats
    std::uint16_t FloatParam = 0;
    std::uint32_t Conditions = AnimConditionNone;
    std::uint8_t TagsAnyCount = 0;
    std::uint8_t TagsAllCount = 0;
    bool Enabled = true;
};

static_assert(std::is_trivially_copyable_v<AnimTransition>);
static_assert(std::is_standard_layout_v<AnimTransition>);

// One animated bone inside a clip.
struct AnimBoneKey
{
    std::uint16_t Bone = 0;
    float Rotation = 0.0f;
    Vector2D Position{};
    float Scale = 1.0f;
};

static_assert(sizeof(AnimBoneKey) == 20);
static_assert(std::is_trivially_copyable_v<AnimBoneKey>);

// A clip is shared by every entity in the same logical state.
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

// Cold authored data: the compiled graph. Many entities that play the same
// graph can share the same definition, while the hot component below stays
// small and cache-friendly.
struct AnimGraphDefinitionComponent
{
    // Authored transitions, evaluated in array order, first match wins
    std::array<AnimTransition, kMaxAnimTransitions> Transitions{};
    std::array<AnimClip, kMaxAnimClips> Clips{};
    // Directional state: while CurrentState equals DirectionalState, the baked
    // table replaces the two-pose crossfade. DirectionalStates maps an anchor's
    // authored ClipId to the clip state it selects.
    Animation::DirectionalBlendTable Directional{};
    std::array<std::uint64_t, kMaxAnimClips> DirectionalStates{};
    std::uint64_t DirectionalState = 0;
    std::uint32_t TransitionCount = 0;
    std::uint32_t ClipCount = 0;
    bool HasDirectional = false;
};

static_assert(std::is_trivially_copyable_v<AnimGraphDefinitionComponent>);
static_assert(std::is_standard_layout_v<AnimGraphDefinitionComponent>);

// Hot runtime state. The animation tick reads this component; the definition is
// touched only when a transition or clip is resolved.
struct AnimStateComponent
{
    // Logical state. 0 = rest pose, no clip.
    std::uint64_t CurrentState = 0;
    // State the cross-fade is coming from
    std::uint64_t PreviousState = 0;
    // Seconds spent in CurrentState; reset on every transition
    float StateTime = 0.0f;
    // Diagnostics: transitions fired so far
    std::uint32_t TransitionCount = 0;
    // Float parameters the conditions read (speed, health, aim pitch, ...)
    std::array<float, kMaxAnimParams> Floats{};

    // Cross-fade timing
    float BlendDuration = 0.0f;
    float BlendTime = 0.0f;
    // 0 = previous pose, 1 = current pose
    float BlendWeight = 1.0f;

    std::uint16_t LastTransition = kNoAnimTransition;
};

static_assert(std::is_trivially_copyable_v<AnimStateComponent>);
static_assert(std::is_standard_layout_v<AnimStateComponent>);
// The hot component must stay far below the old 4 KiB record; authored tables
// live in AnimGraphDefinitionComponent.
static_assert(sizeof(AnimStateComponent) <= 2048, "AnimStateComponent grew: move cold fields to the definition");
static_assert(offsetof(AnimStateComponent, CurrentState) == 0);
static_assert(offsetof(AnimStateComponent, PreviousState) == 8);
static_assert(offsetof(AnimStateComponent, StateTime) == 16);
static_assert(offsetof(AnimStateComponent, TransitionCount) == 20);

// Pure helpers (unit-testable without a registry)
[[nodiscard]] const AnimClip* AnimGraphFindClip(const AnimGraphDefinitionComponent& InDefinition,
                                                std::uint64_t InStateId) noexcept;

// Captures the skeleton's current local pose as its rest pose on the first tick.
void AnimGraphCaptureRestPose(SkeletonComponent& InSkeleton) noexcept;

// True when every condition in the transition holds. Tag conditions read the
// entity's GameplayTagsComponent and resolve hierarchy through the registry.
[[nodiscard]] bool AnimTransitionMatches(const AnimTransition& InTransition, const AnimStateComponent& InState,
                                         const GameplayTagsComponent* InTags,
                                         const GameplayTagRegistry* InTagRegistry) noexcept;

// Writes the blended local pose into the skeleton and runs one SkeletalAnimation evaluation,
void AnimGraphApplySkeletal(AnimStateComponent& InState, const AnimGraphDefinitionComponent& InDefinition,
                            SkeletonComponent& InSkeleton) noexcept;

// Switches the SpriteAnimation clip to the current state's frame range.
void AnimGraphApplySpriteClip(const AnimStateComponent& InState, const AnimGraphDefinitionComponent& InDefinition,
                              SpriteAnimationComponent& InSpriteAnimation, const SpriteSheetComponent* InGrid,
                              Sprite2D* InSprite) noexcept;

// Resolves the directional state into a pose state: the dominant anchor's clip
// state becomes CurrentState and the second anchor becomes PreviousState with
// its normalized weight. Returns false outside the directional state or when
// the winning anchor has no clip mapped.
[[nodiscard]] bool AnimGraphResolveDirectional(const AnimGraphDefinitionComponent& InDefinition,
                                               const AnimStateComponent& InState, Vector2D InDirection,
                                               AnimStateComponent* OutPoseState) noexcept;

class AnimGraphSystem
{
public:
    // Tag conditions require the project registry; the context overload takes
    // it from SystemContext::gameplayTags.
    void SetTagRegistry(const GameplayTagRegistry* InTags) noexcept
    {
        MTags = InTags;
    }
    [[nodiscard]] const GameplayTagRegistry* TagRegistry() const noexcept
    {
        return MTags;
    }

    // One pass per update. Allocation-free; reads/writes only components.
    void Run(const Ecs::GameplaySystemView& InView) const noexcept;
    void Run(Ecs::Registry& InRegistry, float InDt) const noexcept;

private:
    void RunWithTags(Ecs::Registry& InRegistry, float InDt, const GameplayTagRegistry* InTags) const noexcept;
    const GameplayTagRegistry* MTags = nullptr;
};

void RegisterAnimGraphSystem(Ecs::Schedule& InSchedule);
void RegisterAnimGraphSystem(Ecs::SystemGraph& InGraph);
}

// Mirror registration
FBY_REFLECT_COMPONENT(
    Fby::AnimStateComponent, "AnimStateComponent", FBY_FIELD(Fby::AnimStateComponent, CurrentState, UInt64),
    FBY_FIELD(Fby::AnimStateComponent, PreviousState, UInt64), FBY_FIELD(Fby::AnimStateComponent, StateTime, Float),
    FBY_FIELD(Fby::AnimStateComponent, BlendDuration, Float), FBY_FIELD(Fby::AnimStateComponent, BlendTime, Float),
    FBY_FIELD(Fby::AnimStateComponent, BlendWeight, Float), FBY_FIELD(Fby::AnimStateComponent, TransitionCount, UInt32))

// Authoring-side tables. These types are not components, but the animation
FBY_REFLECT_COMPONENT(Fby::AnimTransition, "AnimTransition", FBY_FIELD(Fby::AnimTransition, FromState, UInt64),
                      FBY_FIELD(Fby::AnimTransition, ToState, UInt64),
                      FBY_FIELD(Fby::AnimTransition, StateTimeThreshold, Float),
                      FBY_FIELD(Fby::AnimTransition, FloatThreshold, Float),
                      FBY_FIELD(Fby::AnimTransition, BlendDuration, Float),
                      FBY_FIELD(Fby::AnimTransition, FloatParam, UInt32),
                      FBY_FIELD(Fby::AnimTransition, Conditions, UInt32), FBY_FIELD(Fby::AnimTransition, Enabled, Bool))

FBY_REFLECT_COMPONENT(Fby::AnimClip, "AnimClip", FBY_FIELD(Fby::AnimClip, State, UInt64),
                      FBY_FIELD(Fby::AnimClip, SpriteStartFrame, UInt32),
                      FBY_FIELD(Fby::AnimClip, SpriteFrameCount, UInt32),
                      FBY_FIELD(Fby::AnimClip, SpriteFrameDuration, Float), FBY_FIELD(Fby::AnimClip, KeyCount, UInt32),
                      FBY_FIELD(Fby::AnimClip, SpriteLooping, Bool), FBY_FIELD(Fby::AnimClip, SpritePlaying, Bool))
