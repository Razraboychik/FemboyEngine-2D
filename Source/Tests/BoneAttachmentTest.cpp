// FemboyEngine2D - bone attachment system tests.
#include "Engine/ECS/BoneAttachment.hpp"
#include "Engine/ECS/Components.hpp"
#include "Engine/ECS/Skeleton.hpp"

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
}

int main()
{
    using namespace Fby;

    Ecs::Registry ScopedRegistry;
    const Entity ScopedSkeletonEntity = ScopedRegistry.create();
    SkeletonComponent& ScopedSkeleton = ScopedRegistry.emplace<SkeletonComponent>(ScopedSkeletonEntity);
    SkeletonReset(ScopedSkeleton);
    (void)SkeletonAddBone(ScopedSkeleton, "root", -1, {10.0f, 0.0f}, 0.0f);
    (void)SkeletonAddBone(ScopedSkeleton, "hand", 0, {5.0f, 0.0f}, 0.0f);
    SkeletalAnimation::Evaluate(ScopedSkeleton);

    const Entity ScopedSprite = ScopedRegistry.create();
    Transform2D ScopedTransform;
    ScopedTransform.Position = {0.0f, 0.0f};
    ScopedRegistry.emplace<Transform2D>(ScopedSprite, ScopedTransform);
    BoneAttachment2D ScopedAttachment;
    ScopedAttachment.Skeleton = ScopedSkeletonEntity;
    ScopedAttachment.Bone = 1;
    ScopedAttachment.Offset = {2.0f, 0.0f};
    ScopedRegistry.emplace<BoneAttachment2D>(ScopedSprite, ScopedAttachment);

    BoneAttachmentSystem{}.Run(ScopedRegistry);
    CheckClose(ScopedRegistry.get<Transform2D>(ScopedSprite).Position.X, 17.0f, 1.0e-4f,
               "attachment follows the bone position");
    CheckClose(ScopedRegistry.get<Transform2D>(ScopedSprite).Position.Y, 0.0f, 1.0e-4f,
               "attachment keeps the bone plane");
    CheckClose(ScopedRegistry.get<Transform2D>(ScopedSprite).Rotation, 0.0f, 1.0e-4f,
               "attachment follows the bone rotation");

    ScopedSkeleton.LocalRotations[1] = 0.5f * kPi;
    SkeletalAnimation::Evaluate(ScopedSkeleton);
    BoneAttachmentSystem{}.Run(ScopedRegistry);
    CheckClose(ScopedRegistry.get<Transform2D>(ScopedSprite).Position.X, 15.0f, 1.0e-4f,
               "rotated bone keeps the pivot");
    CheckClose(ScopedRegistry.get<Transform2D>(ScopedSprite).Position.Y, 2.0f, 1.0e-4f,
               "rotated offset lands on the bone axis");
    CheckClose(ScopedRegistry.get<Transform2D>(ScopedSprite).Rotation, 0.5f * kPi, 1.0e-4f,
               "attachment rotation includes the bone angle");

    ScopedSkeleton.LocalRotations[1] = 0.0f;
    ScopedSkeleton.LocalScales[1] = 2.0f;
    SkeletalAnimation::Evaluate(ScopedSkeleton);
    BoneAttachmentSystem{}.Run(ScopedRegistry);
    CheckClose(ScopedRegistry.get<Transform2D>(ScopedSprite).Position.X, 19.0f, 1.0e-4f,
               "bone scale scales the offset");
    CheckClose(ScopedRegistry.get<Transform2D>(ScopedSprite).Scale.X, 2.0f, 1.0e-4f,
               "bone scale propagates to the attachment");

    ScopedSkeleton.LocalScales[1] = 1.0f;
    SkeletalAnimation::Evaluate(ScopedSkeleton);
    ScopedAttachment.RotationOffset = 0.25f;
    ScopedAttachment.Skeleton = kNullEntity;
    ScopedRegistry.get<BoneAttachment2D>(ScopedSprite) = ScopedAttachment;
    const Vector2D ScopedBefore = ScopedRegistry.get<Transform2D>(ScopedSprite).Position;
    BoneAttachmentSystem{}.Run(ScopedRegistry);
    Check(ScopedRegistry.get<Transform2D>(ScopedSprite).Position == ScopedBefore,
          "a missing skeleton leaves the transform untouched");

    ScopedAttachment.Skeleton = ScopedSkeletonEntity;
    ScopedAttachment.Bone = 7;
    ScopedRegistry.get<BoneAttachment2D>(ScopedSprite) = ScopedAttachment;
    BoneAttachmentSystem{}.Run(ScopedRegistry);
    Check(ScopedRegistry.get<Transform2D>(ScopedSprite).Position == ScopedBefore,
          "an out-of-range bone leaves the transform untouched");

    ScopedAttachment.Bone = 1;
    ScopedRegistry.get<BoneAttachment2D>(ScopedSprite) = ScopedAttachment;
    BoneAttachmentSystem{}.Run(ScopedRegistry);
    CheckClose(ScopedRegistry.get<Transform2D>(ScopedSprite).Rotation, 0.25f, 1.0e-4f,
               "rotation offset is applied on top of the bone");

    if (GFailures != 0)
    {
        std::printf("%d bone attachment test(s) failed\n", GFailures);
        return 1;
    }
    std::printf("bone_attachment_test: all tests passed\n");
    return 0;
}
