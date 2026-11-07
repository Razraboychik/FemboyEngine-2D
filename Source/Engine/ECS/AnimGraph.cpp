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

const AnimClip* AnimGraphFindClip(const AnimGraphDefinitionComponent& InDefinition, std::uint64_t InStateId) noexcept
{
    if (InStateId == 0)
    {
        // State 0 is the rest pose: it never owns a clip.
        return nullptr;
    }
    const std::uint32_t ScopedCount = std::min<std::uint32_t>(InDefinition.ClipCount, kMaxAnimClips);
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedCount; ++ScopedIndex)
    {
        if (InDefinition.Clips[ScopedIndex].State == InStateId)
        {
            return &InDefinition.Clips[ScopedIndex];
        }
    }
    return nullptr;
}

void AnimGraphCaptureRestPose(SkeletonComponent& InSkeleton) noexcept
{
    if (InSkeleton.RestPoseCaptured)
    {
        return;
    }
    const std::uint16_t ScopedCount = ClampBoneCount(InSkeleton);
    for (std::uint16_t ScopedBone = 0; ScopedBone < ScopedCount; ++ScopedBone)
    {
        InSkeleton.RestPositions[ScopedBone] = InSkeleton.LocalPositions[ScopedBone];
        InSkeleton.RestRotations[ScopedBone] = InSkeleton.LocalRotations[ScopedBone];
        InSkeleton.RestScales[ScopedBone] = InSkeleton.LocalScales[ScopedBone];
    }
    // Bones above the current count keep the identity rest pose the skeleton was
    InSkeleton.RestPoseCaptured = true;
}

bool AnimTransitionMatches(const AnimTransition& InTransition, const AnimStateComponent& InState,
                           const GameplayTagsComponent* InTags, const GameplayTagRegistry* InTagRegistry) noexcept
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
    const auto ScopedHasTag = [&](GameplayTag InTag) -> bool
    {
        return InTags != nullptr && InTagRegistry != nullptr &&
               GameplayTagContainerHas(InTags->Tags, *InTagRegistry, InTag);
    };
    if ((ScopedConditions & AnimConditionTagsAny) != 0)
    {
        bool ScopedMatched = false;
        for (std::uint8_t ScopedI = 0; ScopedI < InTransition.TagsAnyCount && ScopedI < InTransition.TagsAny.size();
             ++ScopedI)
        {
            ScopedMatched = ScopedMatched || ScopedHasTag(InTransition.TagsAny[ScopedI]);
        }
        if (!ScopedMatched)
        {
            return false;
        }
    }
    if ((ScopedConditions & AnimConditionTagsAll) != 0)
    {
        for (std::uint8_t ScopedI = 0; ScopedI < InTransition.TagsAllCount && ScopedI < InTransition.TagsAll.size();
             ++ScopedI)
        {
            if (!ScopedHasTag(InTransition.TagsAll[ScopedI]))
            {
                return false;
            }
        }
    }
    if ((ScopedConditions & AnimConditionTagsNone) != 0)
    {
        for (std::uint8_t ScopedI = 0; ScopedI < InTransition.TagsAnyCount && ScopedI < InTransition.TagsAny.size();
             ++ScopedI)
        {
            if (ScopedHasTag(InTransition.TagsAny[ScopedI]))
            {
                return false;
            }
        }
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

void AnimGraphApplySkeletal(AnimStateComponent& InState, const AnimGraphDefinitionComponent& InDefinition,
                            SkeletonComponent& InSkeleton) noexcept
{
    AnimGraphCaptureRestPose(InSkeleton);
    const std::uint16_t ScopedCount = ClampBoneCount(InSkeleton);
    if (ScopedCount == 0)
    {
        return;
    }
    const AnimClip* ScopedCurrent = AnimGraphFindClip(InDefinition, InState.CurrentState);
    const AnimClip* ScopedPrevious = AnimGraphFindClip(InDefinition, InState.PreviousState);
    const float ScopedWeight = SmoothStep(InState.BlendWeight);

    // The three destination arrays live in the skeleton; RestPose and the
    Vector2D* FBY_RESTRICT ScopedOutPositions = InSkeleton.LocalPositions.data();
    float* FBY_RESTRICT ScopedOutRotations = InSkeleton.LocalRotations.data();
    float* FBY_RESTRICT ScopedOutScales = InSkeleton.LocalScales.data();

    for (std::uint16_t ScopedBone = 0; ScopedBone < ScopedCount; ++ScopedBone)
    {
        const Vector2D ScopedRestPosition = InSkeleton.RestPositions[ScopedBone];
        const float ScopedRestRotation = InSkeleton.RestRotations[ScopedBone];
        const float ScopedRestScale = InSkeleton.RestScales[ScopedBone];
        const AnimBoneKey* ScopedCurrentKey = FindKey(ScopedCurrent, ScopedBone);
        const AnimBoneKey* ScopedPreviousKey = FindKey(ScopedPrevious, ScopedBone);

        const Vector2D ScopedCurrentPosition =
            ScopedCurrentKey != nullptr ? ScopedCurrentKey->Position : ScopedRestPosition;
        const float ScopedCurrentRotation =
            ScopedCurrentKey != nullptr ? ScopedCurrentKey->Rotation : ScopedRestRotation;
        const float ScopedCurrentScale = ScopedCurrentKey != nullptr ? ScopedCurrentKey->Scale : ScopedRestScale;

        const Vector2D ScopedPreviousPosition =
            ScopedPreviousKey != nullptr ? ScopedPreviousKey->Position : ScopedRestPosition;
        const float ScopedPreviousRotation =
            ScopedPreviousKey != nullptr ? ScopedPreviousKey->Rotation : ScopedRestRotation;
        const float ScopedPreviousScale = ScopedPreviousKey != nullptr ? ScopedPreviousKey->Scale : ScopedRestScale;

        // Local TRS blend, then one hierarchy pass. Composing two global matrices
        ScopedOutPositions[ScopedBone] = Lerp(ScopedPreviousPosition, ScopedCurrentPosition, ScopedWeight);
        ScopedOutRotations[ScopedBone] =
            ScopedPreviousRotation + WrapAngle(ScopedCurrentRotation - ScopedPreviousRotation) * ScopedWeight;
        ScopedOutScales[ScopedBone] = Lerp(ScopedPreviousScale, ScopedCurrentScale, ScopedWeight);
    }
    SkeletalAnimation::Evaluate(InSkeleton);
}

void AnimGraphApplySpriteClip(const AnimStateComponent& InState, const AnimGraphDefinitionComponent& InDefinition,
                              SpriteAnimationComponent& InSpriteAnimation, const SpriteSheetComponent* InGrid,
                              Sprite2D* InSprite) noexcept
{
    const AnimClip* ScopedClip = AnimGraphFindClip(InDefinition, InState.CurrentState);
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
    RunWithTags(InRegistry, InDt, MTags);
}

void AnimGraphSystem::RunWithTags(Ecs::Registry& InRegistry, float InDt,
                                  const GameplayTagRegistry* InTags) const noexcept
{
    const float ScopedDt = std::isfinite(InDt) && InDt > 0.0f ? InDt : 0.0f;
    auto ScopedView = InRegistry.view<AnimStateComponent, AnimGraphDefinitionComponent>();
    for (auto [ScopedEntity, ScopedState, ScopedDefinition] : ScopedView.each())
    {
        ScopedState.StateTime += ScopedDt;

        const auto* ScopedTags = InRegistry.try_get<GameplayTagsComponent>(ScopedEntity);

        // First transition in authored order wins. The loop is bounded by 8 and
        const AnimTransition* ScopedFired = nullptr;
        std::uint16_t ScopedFiredIndex = kNoAnimTransition;
        for (std::uint16_t ScopedIndex = 0; ScopedIndex < kMaxAnimTransitions; ++ScopedIndex)
        {
            const AnimTransition& ScopedTransition = ScopedDefinition.Transitions[ScopedIndex];
            if (ScopedTransition.FromState != kAnimAnyState && ScopedTransition.FromState != ScopedState.CurrentState)
            {
                continue;
            }
            if (AnimTransitionMatches(ScopedTransition, ScopedState, ScopedTags, InTags))
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
            AnimGraphApplySkeletal(ScopedState, ScopedDefinition, *ScopedSkeleton);
        }

        // Bridge 2: SpriteAnimation. Only on a state change; SpriteAnimationSystem owns the frame
        if (ScopedStateChanged)
        {
            if (auto* ScopedSpriteAnimation = InRegistry.try_get<SpriteAnimationComponent>(ScopedEntity); ScopedSpriteAnimation != nullptr)
            {
                const SpriteSheetComponent* ScopedGrid = InRegistry.try_get<SpriteSheetComponent>(ScopedEntity);
                AnimGraphApplySpriteClip(ScopedState, ScopedDefinition, *ScopedSpriteAnimation, ScopedGrid,
                                         InRegistry.try_get<Sprite2D>(ScopedEntity));
            }
        }
    }
}

void AnimGraphSystem::Run(const Ecs::GameplaySystemView& InView) const noexcept
{
    RunWithTags(InView.registry, InView.dt,
                InView.gameplay.gameplayTags != nullptr ? InView.gameplay.gameplayTags : MTags);
}

namespace
{
void AnimGraphSystemThunk(const Ecs::GameplaySystemView& InView, void*)
{
    AnimGraphSystem{}.Run(InView);
}
}

void RegisterAnimGraphSystem(Ecs::Schedule& InSchedule)
{
    // FixedUpdate keeps the cross-fade and the SpriteAnimation frame advance on the same
    (void)InSchedule.Add(
        Ecs::Phase::FixedUpdate, "anim_graph",
        +[](Ecs::SystemContext& InContext, void*)
        {
            Ecs::GameplaySystemView ScopedView = InContext.GameplayView();
            AnimGraphSystem{}.Run(ScopedView);
        });
}

void RegisterAnimGraphSystem(Ecs::SystemGraph& InGraph)
{
    (void)InGraph.Add(Ecs::Phase::FixedUpdate, "anim_graph", Ecs::BindGameplaySystem<&AnimGraphSystemThunk>());
}
}
