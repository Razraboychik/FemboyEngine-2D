// FemboyEngine2D - directional state resolution inside the animation graph.
#include "Engine/ECS/AnimGraph.hpp"
#include "Engine/ECS/Components.hpp"

#include <cmath>
#include <cstdio>

namespace
{

int GFailures = 0;

void Check(bool InCondition, const char* InMessage)
{
    if (!InCondition)
    {
        std::printf("FAIL: %s\n", InMessage);
        ++GFailures;
    }
}

void CheckClose(float InActual, float InExpected, float InTolerance, const char* InMessage)
{
    if (std::fabs(InActual - InExpected) > InTolerance)
    {
        std::printf("FAIL: %s (expected %f, got %f)\n", InMessage, static_cast<double>(InExpected),
                    static_cast<double>(InActual));
        ++GFailures;
    }
}

constexpr float kPi = 3.14159265358979323846f;

Fby::AnimGraphDefinitionComponent MakeDirectionalDefinition()
{
    using namespace Fby;
    AnimGraphDefinitionComponent ScopedDefinition;
    ScopedDefinition.ClipCount = 2;
    ScopedDefinition.Clips[0].State = 101;
    ScopedDefinition.Clips[0].KeyCount = 1;
    ScopedDefinition.Clips[0].Keys[0].Bone = 1;
    ScopedDefinition.Clips[0].Keys[0].Rotation = 0.0f;
    ScopedDefinition.Clips[0].SpriteStartFrame = 0;
    ScopedDefinition.Clips[0].SpriteFrameCount = 4;
    ScopedDefinition.Clips[1].State = 102;
    ScopedDefinition.Clips[1].KeyCount = 1;
    ScopedDefinition.Clips[1].Keys[0].Bone = 1;
    ScopedDefinition.Clips[1].Keys[0].Rotation = 0.5f * kPi;
    ScopedDefinition.Clips[1].SpriteStartFrame = 4;
    ScopedDefinition.Clips[1].SpriteFrameCount = 4;

    Animation::DirectionalBlendSpaceDocument ScopedDocument;
    ScopedDocument.AnchorCount = 2;
    ScopedDocument.Anchors[0].Position = {1.0f, 0.0f};
    ScopedDocument.Anchors[0].ClipId = 0;
    ScopedDocument.Anchors[1].Position = {0.0f, 1.0f};
    ScopedDocument.Anchors[1].ClipId = 1;
    (void)Animation::BakeDirectionalBlendSpace(ScopedDocument, &ScopedDefinition.Directional);
    ScopedDefinition.DirectionalStates[0] = 101;
    ScopedDefinition.DirectionalStates[1] = 102;
    ScopedDefinition.DirectionalState = 200;
    ScopedDefinition.HasDirectional = true;
    return ScopedDefinition;
}

void TestResolveDirectional()
{
    using namespace Fby;
    const AnimGraphDefinitionComponent ScopedDefinition = MakeDirectionalDefinition();
    AnimStateComponent ScopedState;
    ScopedState.CurrentState = ScopedDefinition.DirectionalState;
    AnimStateComponent ScopedPose;

    Check(AnimGraphResolveDirectional(ScopedDefinition, ScopedState, {1.0f, 0.0f}, &ScopedPose),
          "east heading resolves the directional state");
    Check(ScopedPose.CurrentState == 101, "east heading selects the first clip");
    Check(ScopedPose.PreviousState == 101 && ScopedPose.BlendWeight == 1.0f, "a single source has no second pose");

    Check(AnimGraphResolveDirectional(ScopedDefinition, ScopedState, {0.0f, 1.0f}, &ScopedPose),
          "north heading resolves the directional state");
    Check(ScopedPose.CurrentState == 102, "north heading selects the second clip");

    Check(AnimGraphResolveDirectional(ScopedDefinition, ScopedState, {1.0f, 1.0f}, &ScopedPose),
          "diagonal heading resolves the directional state");
    Check(ScopedPose.CurrentState == 101, "diagonal heading picks the dominant clip");
    Check(ScopedPose.PreviousState == 102, "diagonal heading keeps the second clip as the crossfade source");
    CheckClose(ScopedPose.BlendWeight, 0.5f, 1.0e-4f, "diagonal heading splits the weight");

    ScopedState.CurrentState = 101;
    Check(!AnimGraphResolveDirectional(ScopedDefinition, ScopedState, {1.0f, 0.0f}, &ScopedPose),
          "a non-directional state is not overridden");

    AnimGraphDefinitionComponent ScopedMissing = ScopedDefinition;
    ScopedMissing.DirectionalStates[1] = 0;
    ScopedState.CurrentState = 200;
    Check(!AnimGraphResolveDirectional(ScopedMissing, ScopedState, {0.0f, 1.0f}, &ScopedPose),
          "an anchor without a mapped clip is rejected");
}

void TestSystemBridge()
{
    using namespace Fby;
    Ecs::Registry ScopedRegistry;
    const Entity ScopedEntity = ScopedRegistry.create();
    SkeletonComponent& ScopedSkeleton = ScopedRegistry.emplace<SkeletonComponent>(ScopedEntity);
    SkeletonReset(ScopedSkeleton);
    (void)SkeletonAddBone(ScopedSkeleton, "root", -1, {0.0f, 0.0f}, 0.0f);
    (void)SkeletonAddBone(ScopedSkeleton, "arm", 0, {10.0f, 0.0f}, 0.0f);
    AnimGraphCaptureRestPose(ScopedSkeleton);

    AnimStateComponent ScopedState;
    ScopedState.CurrentState = 200;
    ScopedRegistry.emplace<AnimStateComponent>(ScopedEntity, ScopedState);
    ScopedRegistry.emplace<AnimGraphDefinitionComponent>(ScopedEntity, MakeDirectionalDefinition());

    Facing2D ScopedFacing;
    Facing2DFromVector(ScopedFacing, {1.0f, 1.0f});
    ScopedRegistry.emplace<Facing2D>(ScopedEntity, ScopedFacing);

    SpriteAnimationComponent ScopedSpriteAnimation;
    ScopedRegistry.emplace<SpriteAnimationComponent>(ScopedEntity, ScopedSpriteAnimation);
    ScopedRegistry.emplace<SpriteSheetComponent>(ScopedEntity, SpriteSheetComponent{});
    ScopedRegistry.emplace<Sprite2D>(ScopedEntity, Sprite2D{});

    AnimGraphSystem{}.Run(ScopedRegistry, 0.016f);
    CheckClose(ScopedRegistry.get<SkeletonComponent>(ScopedEntity).LocalRotations[1], 0.25f * kPi, 1.0e-3f,
               "the skeleton follows the directional pose");
    Check(ScopedRegistry.get<SpriteAnimationComponent>(ScopedEntity).StartFrame == 0,
          "the sprite follows the dominant direction");

    ScopedRegistry.get<Facing2D>(ScopedEntity).Direction = {0.0f, 1.0f};
    ScopedRegistry.get<Facing2D>(ScopedEntity).Angle = 0.5f * kPi;
    AnimGraphSystem{}.Run(ScopedRegistry, 0.016f);
    CheckClose(ScopedRegistry.get<SkeletonComponent>(ScopedEntity).LocalRotations[1], 0.5f * kPi, 1.0e-3f,
               "a new heading selects the other clip");
    Check(ScopedRegistry.get<SpriteAnimationComponent>(ScopedEntity).StartFrame == 4,
          "the sprite follows the new dominant direction");
}
}

int main()
{
    TestResolveDirectional();
    TestSystemBridge();
    if (GFailures != 0)
    {
        std::printf("%d animation graph directional test(s) failed\n", GFailures);
        return 1;
    }
    std::printf("animation_graph_directional_test: all tests passed\n");
    return 0;
}
