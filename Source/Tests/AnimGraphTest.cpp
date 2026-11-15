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

    Fby::GameplayTagRegistry ScopedTags;
    const Fby::GameplayTag ScopedRoot = ScopedTags.Register("Character");
    const Fby::GameplayTag ScopedGrounded = ScopedTags.Register("Character.State.Grounded");
    const Fby::GameplayTag ScopedAirborne = ScopedTags.Register("Character.State.Airborne");
    Fby::GameplayTagsComponent ScopedEntityTags;
    ScopedEntityTags.Tags.Add(ScopedGrounded);

    const auto ScopedMatches = [&ScopedState, &ScopedEntityTags, &ScopedTags](const Fby::AnimTransition& InTransition)
    { return Fby::AnimTransitionMatches(InTransition, ScopedState, &ScopedEntityTags, &ScopedTags); };

    Fby::AnimTransition ScopedTime;
    ScopedTime.Conditions = Fby::AnimConditionStateTimeAtLeast;
    ScopedTime.StateTimeThreshold = 0.4f;
    FBY_CHECK(ScopedMatches(ScopedTime));
    ScopedTime.StateTimeThreshold = 0.6f;
    FBY_CHECK(!ScopedMatches(ScopedTime));

    Fby::AnimTransition ScopedAll;
    ScopedAll.Conditions = Fby::AnimConditionTagsAll;
    ScopedAll.TagsAll[0] = ScopedRoot;
    ScopedAll.TagsAll[1] = ScopedGrounded;
    ScopedAll.TagsAllCount = 2;
    FBY_CHECK(ScopedMatches(ScopedAll));
    ScopedAll.TagsAll[1] = ScopedAirborne;
    FBY_CHECK(!ScopedMatches(ScopedAll));

    Fby::AnimTransition ScopedAny;
    ScopedAny.Conditions = Fby::AnimConditionTagsAny;
    ScopedAny.TagsAny[0] = ScopedAirborne;
    ScopedAny.TagsAny[1] = ScopedGrounded;
    ScopedAny.TagsAnyCount = 2;
    FBY_CHECK(ScopedMatches(ScopedAny));

    Fby::AnimTransition ScopedNone;
    ScopedNone.Conditions = Fby::AnimConditionTagsNone;
    ScopedNone.TagsAny[0] = ScopedAirborne;
    ScopedNone.TagsAnyCount = 1;
    FBY_CHECK(ScopedMatches(ScopedNone));

    Fby::AnimTransition ScopedFloat;
    ScopedFloat.Conditions = Fby::AnimConditionFloatAtLeast;
    ScopedFloat.FloatParam = 2;
    ScopedFloat.FloatThreshold = 0.5f;
    FBY_CHECK(ScopedMatches(ScopedFloat));
    ScopedFloat.FloatThreshold = 1.0f;
    FBY_CHECK(!ScopedMatches(ScopedFloat));

    // An empty condition set is inert: an unconditional transition would fire.
    Fby::AnimTransition ScopedUnconditional;
    FBY_CHECK(!ScopedMatches(ScopedUnconditional));

    // A guard with invalid parameter/index data fails closed.
    Fby::AnimTransition ScopedInvalidParam = ScopedFloat;
    ScopedInvalidParam.FloatParam = 99;
    FBY_CHECK(!ScopedMatches(ScopedInvalidParam));
}

void TestStateMachine()
{
    Fby::Ecs::Registry ScopedRegistry;
    Fby::JobSystem ScopedJobs(1);
    Fby::Ecs::SystemContext ScopedContext{ScopedRegistry, ScopedJobs};
    Fby::GameplayTagRegistry ScopedTags;
    const Fby::GameplayTag ScopedGrounded = ScopedTags.Register("Character.State.Grounded");
    ScopedContext.gameplayTags = &ScopedTags;

    const Fby::Entity ScopedEntity = ScopedRegistry.create();
    // Add every component before taking references: each emplace moves the
    // entity between archetypes and invalidates earlier references.
    (void)ScopedRegistry.emplace<Fby::SkeletonComponent>(ScopedEntity, MakeSkeleton());
    (void)ScopedRegistry.emplace<Fby::GameplayTagsComponent>(ScopedEntity);
    (void)ScopedRegistry.emplace<Fby::AnimStateComponent>(ScopedEntity);
    (void)ScopedRegistry.emplace<Fby::AnimGraphDefinitionComponent>(ScopedEntity);
    (void)ScopedRegistry.emplace<Fby::SpriteAnimationComponent>(ScopedEntity);
    (void)ScopedRegistry.emplace<Fby::SpriteSheetComponent>(ScopedEntity);
    (void)ScopedRegistry.emplace<Fby::Sprite2D>(ScopedEntity);
    auto& ScopedSkeleton = ScopedRegistry.get<Fby::SkeletonComponent>(ScopedEntity);
    auto& ScopedEntityTags = ScopedRegistry.get<Fby::GameplayTagsComponent>(ScopedEntity);
    auto& ScopedState = ScopedRegistry.get<Fby::AnimStateComponent>(ScopedEntity);
    auto& ScopedDefinition = ScopedRegistry.get<Fby::AnimGraphDefinitionComponent>(ScopedEntity);
    auto& ScopedSpriteAnimation = ScopedRegistry.get<Fby::SpriteAnimationComponent>(ScopedEntity);
    auto& ScopedGrid = ScopedRegistry.get<Fby::SpriteSheetComponent>(ScopedEntity);
    auto& ScopedSprite = ScopedRegistry.get<Fby::Sprite2D>(ScopedEntity);
    ScopedGrid.Columns = 4;
    ScopedGrid.Rows = 1;
    ScopedGrid.Layer = 3;

    ScopedDefinition.Clips[0] = MakeClip(1, 0.5f, 4, 4);
    ScopedDefinition.Clips[1] = MakeClip(2, 1.25f, 8, 2);
    ScopedDefinition.ClipCount = 2;

    // From rest to idle when the entity is grounded.
    ScopedDefinition.Transitions[0].FromState = 0;
    ScopedDefinition.Transitions[0].ToState = 1;
    ScopedDefinition.Transitions[0].Conditions = Fby::AnimConditionTagsAny;
    ScopedDefinition.Transitions[0].TagsAny[0] = ScopedGrounded;
    ScopedDefinition.Transitions[0].TagsAnyCount = 1;
    ScopedDefinition.Transitions[0].BlendDuration = 0.2f;

    // Idle to attack after 0.1 s, once the speed parameter reaches 1.0.
    ScopedDefinition.Transitions[1].FromState = 1;
    ScopedDefinition.Transitions[1].ToState = 2;
    ScopedDefinition.Transitions[1].Conditions =
        Fby::AnimConditionStateTimeAtLeast | Fby::AnimConditionFloatAtLeast;
    ScopedDefinition.Transitions[1].StateTimeThreshold = 0.1f;
    ScopedDefinition.Transitions[1].FloatParam = 0;
    ScopedDefinition.Transitions[1].FloatThreshold = 1.0f;
    ScopedDefinition.Transitions[1].BlendDuration = 0.0f;

    Fby::AnimGraphSystem ScopedGraph;

    // No tag yet: no transition, no clip, arm keeps the rest rotation.
    ScopedGraph.Run(ScopedContext.GameplayView());
    FBY_CHECK(ScopedState.CurrentState == 0);
    FBY_CHECK(ScopedState.TransitionCount == 0);
    FBY_CHECK(std::fabs(ScopedSkeleton.LocalRotations[1]) < 1.0e-6f);
    FBY_CHECK(ScopedSkeleton.RestPoseCaptured);

    // Raise the tag: the first transition fires on the next tick.
    ScopedEntityTags.BaseTags.Add(ScopedGrounded);
    ScopedEntityTags.Tags.Add(ScopedGrounded);
    ScopedContext.dt = 0.1f;
    ScopedGraph.Run(ScopedContext.GameplayView());
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

    // At weight 0 the pose is still the rest pose.
    FBY_CHECK(std::fabs(ScopedSkeleton.LocalRotations[1]) < 1.0e-6f);
    ScopedGraph.Run(ScopedContext.GameplayView());
    FBY_CHECK(ScopedState.BlendWeight > 0.0f && ScopedState.BlendWeight < 1.0f);
    ScopedContext.dt = 0.1f;
    ScopedGraph.Run(ScopedContext.GameplayView());
    FBY_CHECK(ScopedState.BlendWeight == 1.0f);
    FBY_CHECK(std::fabs(ScopedSkeleton.LocalRotations[1] - 0.5f) < 1.0e-4f);
    // The palette the GPU consumes was rebuilt from the blended pose.
    FBY_CHECK(std::isfinite(ScopedSkeleton.SkinningMatrices[1].m[0]));

    // Float parameter below the threshold: no attack transition yet.
    ScopedState.StateTime = 1.0f;
    ScopedState.Floats[0] = 0.5f;
    ScopedGraph.Run(ScopedContext.GameplayView());
    FBY_CHECK(ScopedState.CurrentState == 1);

    // Both conditions now hold: transition to the attack clip, no blend.
    ScopedState.Floats[0] = 1.0f;
    ScopedGraph.Run(ScopedContext.GameplayView());
    FBY_CHECK(ScopedState.CurrentState == 2);
    FBY_CHECK(ScopedState.PreviousState == 1);
    FBY_CHECK(ScopedState.TransitionCount == 2);
    FBY_CHECK(ScopedState.BlendWeight == 1.0f);
    FBY_CHECK(std::fabs(ScopedSkeleton.LocalRotations[1] - 1.25f) < 1.0e-4f);
    FBY_CHECK(ScopedSpriteAnimation.StartFrame == 8);
    FBY_CHECK(ScopedSpriteAnimation.FrameCount == 2);

    // FromState mismatch: the idle transition cannot fire from the attack state.
    ScopedState.StateTime = 100.0f;
    ScopedGraph.Run(ScopedContext.GameplayView());
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
    ScopedRegistry.emplace<Fby::AnimGraphDefinitionComponent>(ScopedEntity);
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
