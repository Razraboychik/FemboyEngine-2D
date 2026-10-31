#include "Engine/ECS/Skeleton.hpp"

#include "Engine/Core/Definitions.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Fby
{
namespace
{
[[nodiscard]] std::uint16_t ClampBoneCount(std::uint16_t InCount) noexcept
{
    return std::min(InCount, kMaxBones);
}

// Embed the 2D affine transform in a column-major Mat4 so the GPU palette is
[[nodiscard]] Mat4 ToMat4(const Mat3& InMatrix) noexcept
{
    Mat4 ScopedResult;
    ScopedResult.m[0] = InMatrix.m[0];
    ScopedResult.m[1] = InMatrix.m[1];
    ScopedResult.m[4] = InMatrix.m[3];
    ScopedResult.m[5] = InMatrix.m[4];
    ScopedResult.m[12] = InMatrix.m[6];
    ScopedResult.m[13] = InMatrix.m[7];
    return ScopedResult;
}

void RebuildSkinningMatrices(SkeletonComponent& InSkeleton) noexcept
{
    const std::uint16_t ScopedCount = ClampBoneCount(InSkeleton.BoneCount);
    // The palette and the two source arrays are disjoint members. Spelling
    Mat4* FBY_RESTRICT ScopedOut = InSkeleton.SkinningMatrices.data();
    const Mat3* FBY_RESTRICT ScopedGlobals = InSkeleton.GlobalMatrices.data();
    const Mat3* FBY_RESTRICT ScopedInverseBind = InSkeleton.InverseBindMatrices.data();
    for (std::uint16_t ScopedI = 0; ScopedI < ScopedCount; ++ScopedI)
    {
        ScopedOut[ScopedI] = ToMat4(Mat3Multiply(ScopedGlobals[ScopedI], ScopedInverseBind[ScopedI]));
    }
}
}

void SkeletonReset(SkeletonComponent& InSkeleton) noexcept
{
    InSkeleton = SkeletonComponent{};
    InSkeleton.LocalScales.fill(1.0f);
    InSkeleton.ParentIndices.fill(-1);
}

std::int16_t SkeletonAddBone(SkeletonComponent& InSkeleton, std::string_view InName, std::int16_t InParent,
                             Vector2D InLocalPosition, float InLocalRotation, float InLocalScale) noexcept
{
    if (InSkeleton.BoneCount >= kMaxBones)
    {
        return -1;
    }
    if (InParent >= static_cast<std::int16_t>(InSkeleton.BoneCount))
    {
        // Parent must already exist: keeps the array in topological order
        return -1;
    }
    const auto ScopedIndex = static_cast<std::int16_t>(InSkeleton.BoneCount);
    InSkeleton.LocalPositions[ScopedIndex] = InLocalPosition;
    InSkeleton.LocalRotations[ScopedIndex] = InLocalRotation;
    InSkeleton.LocalScales[ScopedIndex] = InLocalScale;
    InSkeleton.ParentIndices[ScopedIndex] = InParent;
    InSkeleton.BoneNameHashes[ScopedIndex] = BoneNameHash(InName);
    InSkeleton.InverseBindMatrices[ScopedIndex] = Mat3Identity();
    InSkeleton.GlobalMatrices[ScopedIndex] = Mat3Identity();
    ++InSkeleton.BoneCount;
    InSkeleton.BindPoseValid = false;
    return ScopedIndex;
}

std::int16_t SkeletonFindBone(const SkeletonComponent& InSkeleton, std::uint64_t InNameHash) noexcept
{
    const std::uint16_t ScopedCount = ClampBoneCount(InSkeleton.BoneCount);
    for (std::uint16_t ScopedI = 0; ScopedI < ScopedCount; ++ScopedI)
    {
        if (InSkeleton.BoneNameHashes[ScopedI] == InNameHash)
        {
            return static_cast<std::int16_t>(ScopedI);
        }
    }
    return -1;
}

void SkeletonBindPose(SkeletonComponent& InSkeleton) noexcept
{
    SkeletalAnimation::Evaluate(InSkeleton);
    const std::uint16_t ScopedCount = ClampBoneCount(InSkeleton.BoneCount);
    for (std::uint16_t ScopedI = 0; ScopedI < ScopedCount; ++ScopedI)
    {
        InSkeleton.InverseBindMatrices[ScopedI] = Mat3InverseAffine(InSkeleton.GlobalMatrices[ScopedI]);
    }
    InSkeleton.BindPoseValid = true;
    RebuildSkinningMatrices(InSkeleton);
}

namespace SkeletalAnimation
{
void Evaluate(SkeletonComponent& InSkeleton) noexcept
{
    const std::uint16_t ScopedCount = ClampBoneCount(InSkeleton.BoneCount);
    if (ScopedCount == 0)
    {
        return;
    }

    // Five disjoint SoA arrays; the restrict qualifiers are what allow the
    Mat3* FBY_RESTRICT ScopedGlobals = InSkeleton.GlobalMatrices.data();
    const Vector2D* FBY_RESTRICT ScopedPositions = InSkeleton.LocalPositions.data();
    const float* FBY_RESTRICT ScopedRotations = InSkeleton.LocalRotations.data();
    const float* FBY_RESTRICT ScopedScales = InSkeleton.LocalScales.data();
    const std::int16_t* FBY_RESTRICT ScopedParents = InSkeleton.ParentIndices.data();

    for (std::uint16_t ScopedI = 0; ScopedI < ScopedCount; ++ScopedI)
    {
        const Mat3 ScopedLocal =
            Mat3Multiply(Mat3Translation(ScopedPositions[ScopedI]),
                         Mat3Multiply(Mat3Rotation(ScopedRotations[ScopedI]), Mat3Scale(ScopedScales[ScopedI])));

        const std::int16_t ScopedParent = ScopedParents[ScopedI];
        if (ScopedParent >= 0 && ScopedParent < static_cast<std::int16_t>(ScopedI))
        {
            ScopedGlobals[ScopedI] = Mat3Multiply(ScopedGlobals[ScopedParent], ScopedLocal);
        }
        else
        {
            ScopedGlobals[ScopedI] = ScopedLocal;
        }
    }
    // One extra pass over bones, not vertices: the GPU consumes this palette
    RebuildSkinningMatrices(InSkeleton);
}

void Deform(const SkeletonComponent& InSkeleton, SkinComponent& InSkin) noexcept
{
    const std::uint16_t ScopedBoneCount = ClampBoneCount(InSkeleton.BoneCount);
    if (ScopedBoneCount == 0 || InSkin.VertexCount == 0)
    {
        return;
    }

    const std::uint32_t ScopedVertexCount = std::min(InSkin.VertexCount, kMaxSkinVertices);

    // The skinner reads five input arrays and writes one output array. None
    const Mat3* FBY_RESTRICT ScopedGlobals = InSkeleton.GlobalMatrices.data();
    const Mat3* FBY_RESTRICT ScopedInverseBind = InSkeleton.InverseBindMatrices.data();
    const Vector2D* FBY_RESTRICT ScopedBind = InSkin.BindPositions.data();
    const std::uint16_t* FBY_RESTRICT ScopedBoneIndices = InSkin.BoneIndices.data();
    const float* FBY_RESTRICT ScopedWeights = InSkin.BoneWeights.data();
    Vector2D* FBY_RESTRICT ScopedSkinned = InSkin.SkinnedPositions.data();

    for (std::uint32_t ScopedVertex = 0; ScopedVertex < ScopedVertexCount; ++ScopedVertex)
    {
        const Vector2D ScopedBindPosition = ScopedBind[ScopedVertex];
        Vector2D ScopedAccumulated{};
        float ScopedTotalWeight = 0.0f;

        for (std::uint32_t ScopedInfluence = 0; ScopedInfluence < kMaxBoneInfluences; ++ScopedInfluence)
        {
            const std::uint32_t ScopedSlot = ScopedVertex * kMaxBoneInfluences + ScopedInfluence;
            const float ScopedWeight = ScopedWeights[ScopedSlot];
            if (ScopedWeight <= 0.0f)
            {
                continue;
            }
            const std::uint16_t ScopedBone = ScopedBoneIndices[ScopedSlot];
            if (ScopedBone >= ScopedBoneCount)
            {
                continue;
            }
            const Mat3 ScopedSkinMatrix = Mat3Multiply(ScopedGlobals[ScopedBone], ScopedInverseBind[ScopedBone]);
            const Vector2D ScopedTransformed = Mat3TransformPoint(ScopedSkinMatrix, ScopedBindPosition);
            ScopedAccumulated += ScopedTransformed * ScopedWeight;
            ScopedTotalWeight += ScopedWeight;
        }

        ScopedSkinned[ScopedVertex] =
            ScopedTotalWeight > 0.0f ? ScopedAccumulated / ScopedTotalWeight : ScopedBindPosition;
    }
}
}

void SkeletonSystem::Run(entt::registry& InRegistry) const
{
    auto ScopedView = InRegistry.view<SkeletonComponent>();
    for (auto [ScopedEntity, ScopedSkeleton] : ScopedView.each())
    {
        SkeletalAnimation::Evaluate(ScopedSkeleton);
        if (deformOnCpu)
        {
            if (auto* ScopedSkin = InRegistry.try_get<SkinComponent>(ScopedEntity))
            {
                SkeletalAnimation::Deform(ScopedSkeleton, *ScopedSkin);
            }
        }
    }
}

void SkeletonSystem::Run(Ecs::SystemContext& InContext) const
{
    Run(InContext.registry);
}
}
