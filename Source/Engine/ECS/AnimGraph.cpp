// FemboyEngine2D - FemAnimGraph implementation.
#include "Engine/ECS/AnimGraph.hpp"

#include "Engine/Core/Definitions.hpp"
#include "Engine/ECS/SystemGraph.hpp"

#include <algorithm>
#include <cmath>

namespace Fby
{
namespace
{
constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 2.0f * kPi;
constexpr float kBlendEpsilon = 1.0e-6f;

[[nodiscard]] float Lerp(float InA, float InB, float InWeight) noexcept
{
    return InA + (InB - InA) * InWeight;
}

[[nodiscard]] Vector2D Lerp(Vector2D InA, Vector2D InB, float InWeight) noexcept
{
    return Vector2D{InA.X + (InB.X - InA.X) * InWeight, InA.Y + (InB.Y - InA.Y) * InWeight};
}

// Shortest-arc wrap into [-pi, pi]. A 350-degree turn must interpolate through
[[nodiscard]] float WrapAngle(float InRadians) noexcept
{
    if (!std::isfinite(InRadians))
    {
        return 0.0f;
    }
    float ScopedWrapped = std::fmod(InRadians + kPi, kTwoPi);
    if (ScopedWrapped < 0.0f)
    {
        ScopedWrapped += kTwoPi;
    }
    return ScopedWrapped - kPi;
}

// Smoothstep only at the cross-fade level: linear per-bone interpolation with an
[[nodiscard]] float SmoothStep(float InWeight) noexcept
{
    const float ScopedClamped = std::clamp(InWeight, 0.0f, 1.0f);
    return ScopedClamped * ScopedClamped * (3.0f - 2.0f * ScopedClamped);
}

[[nodiscard]] const AnimBoneKey* FindKey(const AnimClip* InClip, std::uint16_t InBone) noexcept
{
    if (InClip == nullptr)
    {
        return nullptr;
    }
    const std::uint32_t ScopedCount = std::min<std::uint32_t>(InClip->KeyCount, kMaxAnimPoseKeys);
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedCount; ++ScopedIndex)
    {
        if (InClip->Keys[ScopedIndex].Bone == InBone)
        {
            return &InClip->Keys[ScopedIndex];
        }
    }
    return nullptr;
}

[[nodiscard]] std::uint16_t ClampBoneCount(const SkeletonComponent& InSkeleton) noexcept
{
    return static_cast<std::uint16_t>(std::min<std::uint32_t>(InSkeleton.BoneCount, kMaxBones));
}
}

const AnimClip* AnimGraphFindClip(const AnimStateComponent& InState, std::uint64_t InStateId) noexcept
{
    if (InStateId == 0)
    {
        // State 0 is the rest pose: it never owns a clip.
        return nullptr;
    }
    const std::uint32_t ScopedCount = std::min<std::uint32_t>(InState.ClipCount, kMaxAnimClips);
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedCount; ++ScopedIndex)
    {
        if (InState.Clips[ScopedIndex].State == InStateId)
        {
            return &InState.Clips[ScopedIndex];
        }
    }
    return nullptr;
}

void AnimGraphCaptureRestPose(AnimStateComponent& InState, const SkeletonComponent& InSkeleton) noexcept
{
    if (InState.RestPoseCaptured)
    {
        return;
    }
    const std::uint16_t ScopedCount = ClampBoneCount(InSkeleton);
    AnimRestBone* FBY_RESTRICT ScopedRestOut = InState.RestPose.data();
    const Vector2D* FBY_RESTRICT ScopedPositions = InSkeleton.LocalPositions.data();
    const float* FBY_RESTRICT ScopedRotations = InSkeleton.LocalRotations.data();
    const float* FBY_RESTRICT ScopedScales = InSkeleton.LocalScales.data();
    for (std::uint16_t ScopedBone = 0; ScopedBone < ScopedCount; ++ScopedBone)
    {
        ScopedRestOut[ScopedBone].Position = ScopedPositions[ScopedBone];
        ScopedRestOut[ScopedBone].Rotation = ScopedRotations[ScopedBone];
        ScopedRestOut[ScopedBone].Scale = ScopedScales[ScopedBone];
    }
    // Bones above the current count keep the identity rest pose the component was
    InState.RestPoseCaptured = true;
}

bool AnimTransitionMatches(const AnimTransition& InTransition, const AnimStateComponent& InState,
                           std::uint64_t InTags) noexcept
{
    if (!InTransition.Enabled)
    {
        return false;
    }
    const std::uint32_t ScopedConditions = InTransition.Conditions;
    if (ScopedConditions == AnimConditionNone)
    {
        // Inert by construction: an unconditional transition would fire every
        return false;
    }
    if ((ScopedConditions & AnimConditionStateTimeAtLeast) != 0 &&
        !(InState.StateTime >= InTransition.StateTimeThreshold))
    {
        return false;
    }
    if ((ScopedConditions & AnimConditionStateTimeAtMost) != 0 &&
        !(InState.StateTime <= InTransition.StateTimeThreshold))
    {
        return false;
    }
    if ((ScopedConditions & AnimConditionTagsAny) != 0 &&
        (InTransition.TagsAny == 0 || (InTags & InTransition.TagsAny) == 0))
    {
        return false;
    }
    if ((ScopedConditions & AnimConditionTagsAll) != 0 &&
        (InTransition.TagsAll == 0 || (InTags & InTransition.TagsAll) != InTransition.TagsAll))
    {
        return false;
    }
    if ((ScopedConditions & AnimConditionTagsNone) != 0 &&
        (InTransition.TagsAny == 0 || (InTags & InTransition.TagsAny) != 0))
    {
        return false;
    }
    if ((ScopedConditions & (AnimConditionFloatAtLeast | AnimConditionFloatAtMost)) != 0)
    {
        if (InTransition.FloatParam >= kMaxAnimParams)
        {
            return false;
        }
        const float ScopedValue = InState.Floats[InTransition.FloatParam];
        if (!std::isfinite(ScopedValue))
        {
            return false;
        }
        if ((ScopedConditions & AnimConditionFloatAtLeast) != 0 && !(ScopedValue >= InTransition.FloatThreshold))
        {
            return false;
        }
        if ((ScopedConditions & AnimConditionFloatAtMost) != 0 && !(ScopedValue <= InTransition.FloatThreshold))
        {
            return false;
        }
    }
    return true;
}

void AnimGraphApplySkeletal(AnimStateComponent& InState, SkeletonComponent& InSkeleton) noexcept
{
    AnimGraphCaptureRestPose(InState, InSkeleton);
    const std::uint16_t ScopedCount = ClampBoneCount(InSkeleton);
    if (ScopedCount == 0)
    {
        return;
    }
    const AnimClip* ScopedCurrent = AnimGraphFindClip(InState, InState.CurrentState);
    const AnimClip* ScopedPrevious = AnimGraphFindClip(InState, InState.PreviousState);
    const float ScopedWeight = SmoothStep(InState.BlendWeight);

    // The three destination arrays live in the skeleton; RestPose and the
    Vector2D* FBY_RESTRICT ScopedOutPositions = InSkeleton.LocalPositions.data();
    float* FBY_RESTRICT ScopedOutRotations = InSkeleton.LocalRotations.data();
    float* FBY_RESTRICT ScopedOutScales = InSkeleton.LocalScales.data();

    for (std::uint16_t ScopedBone = 0; ScopedBone < ScopedCount; ++ScopedBone)
    {
        const AnimRestBone& ScopedRest = InState.RestPose[ScopedBone];
        const AnimBoneKey* ScopedCurrentKey = FindKey(ScopedCurrent, ScopedBone);
        const AnimBoneKey* ScopedPreviousKey = FindKey(ScopedPrevious, ScopedBone);

        const Vector2D ScopedCurrentPosition =
            ScopedCurrentKey != nullptr ? ScopedCurrentKey->Position : ScopedRest.Position;
        const float ScopedCurrentRotation =
            ScopedCurrentKey != nullptr ? ScopedCurrentKey->Rotation : ScopedRest.Rotation;
        const float ScopedCurrentScale = ScopedCurrentKey != nullptr ? ScopedCurrentKey->Scale : ScopedRest.Scale;

        const Vector2D ScopedPreviousPosition =
            ScopedPreviousKey != nullptr ? ScopedPreviousKey->Position : ScopedRest.Position;
        const float ScopedPreviousRotation =
            ScopedPreviousKey != nullptr ? ScopedPreviousKey->Rotation : ScopedRest.Rotation;
        const float ScopedPreviousScale = ScopedPreviousKey != nullptr ? ScopedPreviousKey->Scale : ScopedRest.Scale;

        // Local TRS blend, then one hierarchy pass. Composing two global matrices
        ScopedOutPositions[ScopedBone] = Lerp(ScopedPreviousPosition, ScopedCurrentPosition, ScopedWeight);
        ScopedOutRotations[ScopedBone] =
            ScopedPreviousRotation + WrapAngle(ScopedCurrentRotation - ScopedPreviousRotation) * ScopedWeight;
        ScopedOutScales[ScopedBone] = Lerp(ScopedPreviousScale, ScopedCurrentScale, ScopedWeight);
    }
    SkeletalAnimation::Evaluate(InSkeleton);
}

void AnimGraphApplySpriteClip(const AnimStateComponent& InState, SpriteAnimationComponent& InSpriteAnimation, const SpriteSheetComponent* InGrid,
                         Sprite2D* InSprite) noexcept
{
    const AnimClip* ScopedClip = AnimGraphFindClip(InState, InState.CurrentState);
    if (ScopedClip == nullptr || ScopedClip->SpriteFrameCount == 0)
    {
        return;
    }
    // Frame ranges switch atomically: reset the accumulator and the local frame
    InSpriteAnimation.StartFrame = ScopedClip->SpriteStartFrame;
    InSpriteAnimation.FrameCount = ScopedClip->SpriteFrameCount;
    InSpriteAnimation.FrameDuration = ScopedClip->SpriteFrameDuration > 0.0f ? ScopedClip->SpriteFrameDuration : 0.1f;
    InSpriteAnimation.CurrentFrame = 0;
    InSpriteAnimation.ElapsedTime = 0.0f;
    InSpriteAnimation.IsLooping = ScopedClip->SpriteLooping;
    InSpriteAnimation.IsPlaying = ScopedClip->SpritePlaying;
    if (InSprite != nullptr)
    {
        ApplySpriteFrame(InSpriteAnimation, InGrid, *InSprite);
    }
}

void AnimGraphSystem::Run(Ecs::Registry& InRegistry, float InDt) const noexcept
{
    const float ScopedDt = std::isfinite(InDt) && InDt > 0.0f ? InDt : 0.0f;
    auto ScopedView = InRegistry.view<AnimStateComponent>();
    for (auto [ScopedEntity, ScopedState] : ScopedView.each())
    {
        ScopedState.StateTime += ScopedDt;

        std::uint64_t ScopedTags = 0;
        if (const auto* ScopedGameplay = InRegistry.try_get<GameplayState>(ScopedEntity); ScopedGameplay != nullptr)
        {
            ScopedTags = ScopedGameplay->Bits;
        }

        // First transition in authored order wins. The loop is bounded by 8 and
        const AnimTransition* ScopedFired = nullptr;
        std::uint16_t ScopedFiredIndex = kNoAnimTransition;
        for (std::uint16_t ScopedIndex = 0; ScopedIndex < kMaxAnimTransitions; ++ScopedIndex)
        {
            const AnimTransition& ScopedTransition = ScopedState.Transitions[ScopedIndex];
            if (ScopedTransition.FromState != kAnimAnyState && ScopedTransition.FromState != ScopedState.CurrentState)
            {
                continue;
            }
            if (AnimTransitionMatches(ScopedTransition, ScopedState, ScopedTags))
            {
                ScopedFired = &ScopedTransition;
                ScopedFiredIndex = ScopedIndex;
                break;
            }
        }

        bool ScopedStateChanged = false;
        if (ScopedFired != nullptr)
        {
            ScopedState.PreviousState = ScopedState.CurrentState;
            ScopedState.CurrentState = ScopedFired->ToState;
            ScopedState.StateTime = 0.0f;
            ScopedState.BlendTime = 0.0f;
            ScopedState.BlendDuration = std::max(ScopedFired->BlendDuration, 0.0f);
            ScopedState.BlendWeight = ScopedState.BlendDuration > kBlendEpsilon ? 0.0f : 1.0f;
            ScopedState.LastTransition = ScopedFiredIndex;
            ++ScopedState.TransitionCount;
            ScopedStateChanged = true;
        }
        else if (ScopedState.BlendWeight < 1.0f)
        {
            ScopedState.BlendTime += ScopedDt;
            ScopedState.BlendWeight = ScopedState.BlendDuration > kBlendEpsilon
                                          ? std::clamp(ScopedState.BlendTime / ScopedState.BlendDuration, 0.0f, 1.0f)
                                          : 1.0f;
        }

        // Bridge 1: SkeletalAnimation. Always apply, not only on a transition, because the
        if (auto* ScopedSkeleton = InRegistry.try_get<SkeletonComponent>(ScopedEntity); ScopedSkeleton != nullptr)
        {
            AnimGraphApplySkeletal(ScopedState, *ScopedSkeleton);
        }

        // Bridge 2: SpriteAnimation. Only on a state change; SpriteAnimationSystem owns the frame
        if (ScopedStateChanged)
        {
            if (auto* ScopedSpriteAnimation = InRegistry.try_get<SpriteAnimationComponent>(ScopedEntity); ScopedSpriteAnimation != nullptr)
            {
                const SpriteSheetComponent* ScopedGrid = InRegistry.try_get<SpriteSheetComponent>(ScopedEntity);
                AnimGraphApplySpriteClip(ScopedState, *ScopedSpriteAnimation, ScopedGrid, InRegistry.try_get<Sprite2D>(ScopedEntity));
            }
        }
    }
}

void AnimGraphSystem::Run(Ecs::SystemContext& InContext) const noexcept
{
    Run(InContext.registry, InContext.dt);
}

void RegisterAnimGraphSystem(Ecs::Schedule& InSchedule)
{
    // FixedUpdate keeps the cross-fade and the SpriteAnimation frame advance on the same
    (void)InSchedule.Add(
        Ecs::Phase::FixedUpdate, "anim_graph",
        +[](Ecs::SystemContext& InContext, void*) { AnimGraphSystem{}.Run(InContext); });
}

void RegisterAnimGraphSystem(Ecs::SystemGraph& InGraph)
{
    (void)InGraph.Add(
        Ecs::Phase::FixedUpdate, "anim_graph",
        +[](Ecs::SystemContext& InContext, void*) { AnimGraphSystem{}.Run(InContext); });
}
}
