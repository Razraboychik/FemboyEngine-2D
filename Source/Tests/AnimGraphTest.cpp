// FemboyEngine2D - FemAnimGraph tests.
#include "Engine/ECS/AnimGraph.hpp"

#include "Engine/Core/JobSystem.hpp"
#include "Engine/ECS/Schedule.hpp"

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

[[nodiscard]] Fby::SkeletonComponent MakeSkeleton()
{
    Fby::SkeletonComponent ScopedSkeleton;
    Fby::SkeletonReset(ScopedSkeleton);
    (void)Fby::SkeletonAddBone(ScopedSkeleton, "root", -1, Fby::Vector2D{0.0f, 0.0f}, 0.0f);
    (void)Fby::SkeletonAddBone(ScopedSkeleton, "arm", 0, Fby::Vector2D{10.0f, 0.0f}, 0.0f);
    Fby::SkeletonBindPose(ScopedSkeleton);
    return ScopedSkeleton;
}

[[nodiscard]] Fby::AnimClip MakeClip(std::uint64_t InState, float InArmRotation, std::uint32_t InStartFrame,
                                     std::uint32_t InFrameCount)
{
    Fby::AnimClip ScopedClip;
    ScopedClip.State = InState;
    ScopedClip.KeyCount = 1;
    ScopedClip.Keys[0].Bone = 1;
    ScopedClip.Keys[0].Rotation = InArmRotation;
    ScopedClip.Keys[0].Scale = 1.0f;
    ScopedClip.SpriteStartFrame = InStartFrame;
    ScopedClip.SpriteFrameCount = InFrameCount;
    ScopedClip.SpriteFrameDuration = 0.1f;
    return ScopedClip;
}

void TestConditionEvaluators()
{
    Fby::AnimStateComponent ScopedState;
    ScopedState.StateTime = 0.5f;
    ScopedState.Floats[2] = 0.75f;

    Fby::AnimTransition ScopedTime;
    ScopedTime.Conditions = Fby::AnimConditionStateTimeAtLeast;
    ScopedTime.StateTimeThreshold = 0.4f;
    FBY_CHECK(Fby::AnimTransitionMatches(ScopedTime, ScopedState, 0));
    ScopedTime.StateTimeThreshold = 0.6f;
    FBY_CHECK(!Fby::AnimTransitionMatches(ScopedTime, ScopedState, 0));

    Fby::AnimTransition ScopedTags;
    ScopedTags.Conditions = Fby::AnimConditionTagsAll;
    ScopedTags.TagsAll = 0b101u;
    FBY_CHECK(Fby::AnimTransitionMatches(ScopedTags, ScopedState, 0b111u));
    FBY_CHECK(!Fby::AnimTransitionMatches(ScopedTags, ScopedState, 0b001u));

    Fby::AnimTransition ScopedAny;
    ScopedAny.Conditions = Fby::AnimConditionTagsAny;
    ScopedAny.TagsAny = 0b010u;
    FBY_CHECK(Fby::AnimTransitionMatches(ScopedAny, ScopedState, 0b011u));
    FBY_CHECK(!Fby::AnimTransitionMatches(ScopedAny, ScopedState, 0b100u));

    Fby::AnimTransition ScopedNone;
    ScopedNone.Conditions = Fby::AnimConditionTagsNone;
    ScopedNone.TagsAny = 0b100u;
    FBY_CHECK(Fby::AnimTransitionMatches(ScopedNone, ScopedState, 0b011u));
    FBY_CHECK(!Fby::AnimTransitionMatches(ScopedNone, ScopedState, 0b100u));

    Fby::AnimTransition ScopedFloat;
    ScopedFloat.Conditions = Fby::AnimConditionFloatAtLeast;
    ScopedFloat.FloatParam = 2;
    ScopedFloat.FloatThreshold = 0.5f;
    FBY_CHECK(Fby::AnimTransitionMatches(ScopedFloat, ScopedState, 0));
    ScopedFloat.FloatThreshold = 1.0f;
    FBY_CHECK(!Fby::AnimTransitionMatches(ScopedFloat, ScopedState, 0));

    // An empty condition set is inert: an unconditional transition would fire on
    Fby::AnimTransition ScopedUnconditional;
    FBY_CHECK(!Fby::AnimTransitionMatches(ScopedUnconditional, ScopedState, 0));

    // A guard with invalid parameter/index data fails closed.
    Fby::AnimTransition ScopedInvalidParam = ScopedFloat;
    ScopedInvalidParam.FloatParam = 99;
    FBY_CHECK(!Fby::AnimTransitionMatches(ScopedInvalidParam, ScopedState, 0));
}

void TestStateMachine()
{
    Fby::Ecs::Registry ScopedRegistry;
    Fby::JobSystem ScopedJobs(1);
    Fby::Ecs::SystemContext ScopedContext{ScopedRegistry, ScopedJobs};

    const Fby::Entity ScopedEntity = ScopedRegistry.create();
    auto& ScopedSkeleton = ScopedRegistry.emplace<Fby::SkeletonComponent>(ScopedEntity, MakeSkeleton());
    auto& ScopedGameplay = ScopedRegistry.emplace<Fby::GameplayState>(ScopedEntity);
    auto& ScopedState = ScopedRegistry.emplace<Fby::AnimStateComponent>(ScopedEntity);
    auto& ScopedSpriteAnimation = ScopedRegistry.emplace<Fby::SpriteAnimationComponent>(ScopedEntity);
    auto& ScopedGrid = ScopedRegistry.emplace<Fby::SpriteSheetComponent>(ScopedEntity);
    auto& ScopedSprite = ScopedRegistry.emplace<Fby::Sprite2D>(ScopedEntity);
    ScopedGrid.Columns = 4;
    ScopedGrid.Rows = 1;
    ScopedGrid.Layer = 3;

    ScopedState.Clips[0] = MakeClip(1, 0.5f, 4, 4);
    ScopedState.Clips[1] = MakeClip(2, 1.25f, 8, 2);
    ScopedState.ClipCount = 2;

    // From rest to idle when the entity is grounded. FromState == 0 is
    ScopedState.Transitions[0].FromState = 0;
    ScopedState.Transitions[0].ToState = 1;
    ScopedState.Transitions[0].Conditions = Fby::AnimConditionTagsAny;
    ScopedState.Transitions[0].TagsAny = Fby::GameplayStateBit(Fby::GameplayStateFlag::Grounded);
    ScopedState.Transitions[0].BlendDuration = 0.2f;

    // Idle to attack after 0.1 s, once the speed parameter reaches 1.0.
    ScopedState.Transitions[1].FromState = 1;
    ScopedState.Transitions[1].ToState = 2;
    ScopedState.Transitions[1].Conditions =
        Fby::AnimConditionStateTimeAtLeast | Fby::AnimConditionFloatAtLeast;
    ScopedState.Transitions[1].StateTimeThreshold = 0.1f;
    ScopedState.Transitions[1].FloatParam = 0;
    ScopedState.Transitions[1].FloatThreshold = 1.0f;
    ScopedState.Transitions[1].BlendDuration = 0.0f;

    Fby::AnimGraphSystem ScopedGraph;

    // No tag yet: no transition, no clip, arm keeps the rest rotation.
    ScopedGraph.Run(ScopedContext);
    FBY_CHECK(ScopedState.CurrentState == 0);
    FBY_CHECK(ScopedState.TransitionCount == 0);
    FBY_CHECK(std::fabs(ScopedSkeleton.LocalRotations[1]) < 1.0e-6f);
    FBY_CHECK(ScopedState.RestPoseCaptured);

    // Raise the tag: the first transition fires on the next tick.
    ScopedGameplay.Bits |= Fby::GameplayStateBit(Fby::GameplayStateFlag::Grounded);
    ScopedContext.dt = 0.1f;
    ScopedGraph.Run(ScopedContext);
    FBY_CHECK(ScopedState.CurrentState == 1);
    FBY_CHECK(ScopedState.PreviousState == 0);
    FBY_CHECK(ScopedState.TransitionCount == 1);
    FBY_CHECK(ScopedState.BlendWeight == 0.0f);
    FBY_CHECK(ScopedState.BlendDuration == 0.2f);
    FBY_CHECK(ScopedState.LastTransition == 0);
    // SpriteAnimation switched atomically to the idle range in the same tick.
    FBY_CHECK(ScopedSpriteAnimation.StartFrame == 4);
    FBY_CHECK(ScopedSpriteAnimation.FrameCount == 4);
    FBY_CHECK(ScopedSpriteAnimation.CurrentFrame == 0);
    FBY_CHECK(ScopedSpriteAnimation.ElapsedTime == 0.0f);
    const Fby::SpriteFrameRect ScopedRect = Fby::SpriteFrameUv(ScopedSpriteAnimation, &ScopedGrid);
    FBY_CHECK(ScopedSprite.UVMin == ScopedRect.uvMin);
    FBY_CHECK(ScopedSprite.UVMax == ScopedRect.uvMax);
    FBY_CHECK(ScopedSprite.Layer == 3);

    // At weight 0 the pose is still the rest pose; one more tick completes the
    FBY_CHECK(std::fabs(ScopedSkeleton.LocalRotations[1]) < 1.0e-6f);
    ScopedGraph.Run(ScopedContext);
    FBY_CHECK(ScopedState.BlendWeight > 0.0f && ScopedState.BlendWeight < 1.0f);
    ScopedContext.dt = 0.1f;
    ScopedGraph.Run(ScopedContext);
    FBY_CHECK(ScopedState.BlendWeight == 1.0f);
    FBY_CHECK(std::fabs(ScopedSkeleton.LocalRotations[1] - 0.5f) < 1.0e-4f);
    // The palette the GPU consumes was rebuilt from the blended pose.
    FBY_CHECK(std::isfinite(ScopedSkeleton.SkinningMatrices[1].m[0]));

    // Float parameter below the threshold: no attack transition yet.
    ScopedState.StateTime = 1.0f;
    ScopedState.Floats[0] = 0.5f;
    ScopedGraph.Run(ScopedContext);
    FBY_CHECK(ScopedState.CurrentState == 1);

    // Both conditions now hold: transition to the attack clip, no blend.
    ScopedState.Floats[0] = 1.0f;
    ScopedGraph.Run(ScopedContext);
    FBY_CHECK(ScopedState.CurrentState == 2);
    FBY_CHECK(ScopedState.PreviousState == 1);
    FBY_CHECK(ScopedState.TransitionCount == 2);
    FBY_CHECK(ScopedState.BlendWeight == 1.0f);
    FBY_CHECK(std::fabs(ScopedSkeleton.LocalRotations[1] - 1.25f) < 1.0e-4f);
    FBY_CHECK(ScopedSpriteAnimation.StartFrame == 8);
    FBY_CHECK(ScopedSpriteAnimation.FrameCount == 2);

    // FromState mismatch: the idle transition cannot fire from the attack state.
    ScopedState.StateTime = 100.0f;
    ScopedGraph.Run(ScopedContext);
    FBY_CHECK(ScopedState.TransitionCount == 2);
}

void TestRegistration()
{
    Fby::Ecs::Registry ScopedRegistry;
    Fby::JobSystem ScopedJobs(1);
    Fby::Ecs::SystemContext ScopedContext{ScopedRegistry, ScopedJobs};
    Fby::Ecs::Schedule ScopedSchedule;
    Fby::RegisterAnimGraphSystem(ScopedSchedule);

    const Fby::Entity ScopedEntity = ScopedRegistry.create();
    ScopedRegistry.emplace<Fby::AnimStateComponent>(ScopedEntity);
    ScopedContext.dt = 0.016f;
    ScopedSchedule.Run(Fby::Ecs::Phase::FixedUpdate, ScopedContext);
    FBY_CHECK(ScopedRegistry.get<Fby::AnimStateComponent>(ScopedEntity).StateTime > 0.0f);
}
}

int main()
{
    static_assert(std::is_trivially_copyable_v<Fby::AnimStateComponent>);
    static_assert(std::is_standard_layout_v<Fby::AnimStateComponent>);
    static_assert(sizeof(Fby::AnimBoneKey) == 20);

    TestConditionEvaluators();
    TestStateMachine();
    TestRegistration();

    if (GFailures != 0)
    {
        std::fprintf(stderr, "anim_graph_test: %u failure(s)\n", GFailures);
        return 1;
    }
    std::printf("anim_graph_test: all checks passed\n");
    return 0;
}
