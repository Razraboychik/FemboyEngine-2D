// FemboyEngine2D - bone attachment system implementation.
#include "Engine/ECS/BoneAttachment.hpp"

#include "Engine/ECS/CoreComponents.hpp"
#include "Engine/ECS/Skeleton.hpp"
#include "Engine/ECS/SystemGraph.hpp"

#include <cmath>

namespace Fby
{
namespace
{
void BoneAttachmentThunk(const Ecs::CoreSystemView& InView, void*)
{
    BoneAttachmentSystem{}.Run(InView);
}
}

void BoneAttachmentSystem::Run(Ecs::Registry& InRegistry) const
{
    auto ScopedView = InRegistry.view<BoneAttachment2D, Transform2D>();
    for (auto [ScopedEntity, ScopedAttachment, ScopedTransform] : ScopedView.each())
    {
        const auto* ScopedSkeleton = InRegistry.try_get<SkeletonComponent>(ScopedAttachment.Skeleton);
        if (ScopedSkeleton == nullptr || ScopedAttachment.Bone >= ScopedSkeleton->BoneCount ||
            ScopedAttachment.Bone >= kMaxBones)
        {
            continue;
        }
        const Mat3& ScopedMatrix = ScopedSkeleton->GlobalMatrices[ScopedAttachment.Bone];
        ScopedTransform.Position = Mat3TransformPoint(ScopedMatrix, ScopedAttachment.Offset);
        ScopedTransform.Rotation = Sim::Atan2(ScopedMatrix.m[1], ScopedMatrix.m[0]) + ScopedAttachment.RotationOffset;
        const float ScopedBoneScale =
            std::sqrt(ScopedMatrix.m[0] * ScopedMatrix.m[0] + ScopedMatrix.m[1] * ScopedMatrix.m[1]);
        ScopedTransform.Scale =
            Vector2D{ScopedAttachment.Scale.X * ScopedBoneScale, ScopedAttachment.Scale.Y * ScopedBoneScale};
    }
}

void BoneAttachmentSystem::Run(const Ecs::CoreSystemView& InView) const
{
    Run(InView.registry);
}

void RegisterBoneAttachmentSystem(Ecs::Schedule& InSchedule)
{
    (void)InSchedule.Add(
        Ecs::Phase::FixedUpdate, "boneAttachment",
        +[](Ecs::SystemContext& InContext, void*)
        {
            Ecs::CoreSystemView ScopedView = InContext.CoreView();
            BoneAttachmentSystem{}.Run(ScopedView);
        });
}

void RegisterBoneAttachmentSystem(Ecs::SystemGraph& InGraph)
{
    Ecs::SystemAccess ScopedAccess;
    ScopedAccess.ComponentReads = Ecs::MakeComponentAccessMask<SkeletonComponent, BoneAttachment2D>();
    ScopedAccess.ComponentWrites = Ecs::MakeComponentAccessMask<Transform2D>();
    (void)InGraph.Add(Ecs::Phase::FixedUpdate, "boneAttachment", Ecs::BindCoreSystem<&BoneAttachmentThunk>(), nullptr,
                      0, 0, ScopedAccess);
}
}
