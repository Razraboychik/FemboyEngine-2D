#include "Engine/RHI/SkinningBridge.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Fby::Render
{

void BuildMeshVertices(const SkinComponent& InSkin, std::span<const Vector2D> InUvs, Rhi::MeshVertex* Out,
                       std::uint32_t InColor) noexcept
{
    if (Out == nullptr)
    {
        return;
    }
    const std::uint32_t ScopedCount = std::min(InSkin.VertexCount, kMaxSkinVertices);

    Vector2D ScopedMinimum{std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
    Vector2D ScopedMaximum{std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()};
    for (std::uint32_t ScopedVertex = 0; ScopedVertex < ScopedCount; ++ScopedVertex)
    {
        const Vector2D ScopedPoint = InSkin.BindPositions[ScopedVertex];
        ScopedMinimum.X = std::min(ScopedMinimum.X, ScopedPoint.X);
        ScopedMinimum.Y = std::min(ScopedMinimum.Y, ScopedPoint.Y);
        ScopedMaximum.X = std::max(ScopedMaximum.X, ScopedPoint.X);
        ScopedMaximum.Y = std::max(ScopedMaximum.Y, ScopedPoint.Y);
    }
    const Vector2D ScopedExtent = ScopedMaximum - ScopedMinimum;
    const Vector2D ScopedInverseExtent{ScopedExtent.X != 0.0f ? 1.0f / ScopedExtent.X : 0.0f,
                                       ScopedExtent.Y != 0.0f ? 1.0f / ScopedExtent.Y : 0.0f};

    for (std::uint32_t ScopedVertex = 0; ScopedVertex < ScopedCount; ++ScopedVertex)
    {
        Rhi::MeshVertex& ScopedTarget = Out[ScopedVertex];
        ScopedTarget.position = InSkin.BindPositions[ScopedVertex];
        ScopedTarget.uv =
            ScopedVertex < InUvs.size()
                ? InUvs[ScopedVertex]
                : Vector2D{(InSkin.BindPositions[ScopedVertex].X - ScopedMinimum.X) * ScopedInverseExtent.X,
                           (InSkin.BindPositions[ScopedVertex].Y - ScopedMinimum.Y) * ScopedInverseExtent.Y};
        ScopedTarget.color = InColor;
        ScopedTarget.padding = 0;
        for (std::uint32_t ScopedInfluence = 0; ScopedInfluence < kMaxBoneInfluences; ++ScopedInfluence)
        {
            const std::uint32_t ScopedSlot = ScopedVertex * kMaxBoneInfluences + ScopedInfluence;
            ScopedTarget.boneIndices[ScopedInfluence] = InSkin.BoneIndices[ScopedSlot];
            ScopedTarget.boneWeights[ScopedInfluence] = InSkin.BoneWeights[ScopedSlot];
        }
    }
}

std::span<const Mat4> SkeletonPalette(const SkeletonComponent& InSkeleton) noexcept
{
    return {InSkeleton.SkinningMatrices.data(), std::min<std::uint32_t>(InSkeleton.BoneCount, kMaxBones)};
}

Mat4 MakeModelMatrix(const Transform2D& InTransform) noexcept
{
    const Mat4 ScopedTranslation = Mat4::Translation(InTransform.Position);
    const Mat4 ScopedRotation = Mat4::Rotation(InTransform.Rotation);
    const Mat4 ScopedScale = Mat4::Scale(InTransform.Scale);
    return ScopedTranslation * (ScopedRotation * ScopedScale);
}
}
