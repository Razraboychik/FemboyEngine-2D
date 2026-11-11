// FemboyEngine2D - bone-bound SDF character evaluation and field baking.
#include "Engine/Physics/SdfCharacter.hpp"

#include <algorithm>
#include <cmath>

namespace Fby
{

float SdfSmoothMin(float InA, float InB, float InBlend) noexcept
{
    if (!(InBlend > 0.0f))
    {
        return std::min(InA, InB);
    }
    const float ScopedDifference = std::abs(InA - InB);
    const float ScopedOverlap = std::max(InBlend - ScopedDifference, 0.0f) / InBlend;
    return std::min(InA, InB) - ScopedOverlap * ScopedOverlap * InBlend * 0.25f;
}

namespace
{

[[nodiscard]] float PartDistance(const SkeletonComponent& InSkeleton, const SdfBonePart& InPart,
                                 Vector2D InWorldPoint) noexcept
{
    if (InPart.BoneIndex >= InSkeleton.BoneCount)
    {
        return 1.0e6f;
    }
    const Mat3 ScopedInverse = Mat3InverseAffine(InSkeleton.GlobalMatrices[InPart.BoneIndex]);
    const Vector2D ScopedLocal = Mat3TransformPoint(ScopedInverse, InWorldPoint);
    SdfShape ScopedShape = InPart.Shape;
    ScopedShape.Angle += InPart.LocalRotation;
    return SdfDistance(ScopedShape, ScopedLocal - InPart.LocalOffset);
}

}

float SdfCharacterDistance(const SkeletonComponent& InSkeleton, const SdfCharacterComponent& InCharacter,
                           Vector2D InWorldPoint) noexcept
{
    const std::uint32_t ScopedCount = std::min<std::uint32_t>(InCharacter.PartCount, kMaxSdfCharacterParts);
    if (ScopedCount == 0)
    {
        return 1.0e6f;
    }
    float ScopedDistance = PartDistance(InSkeleton, InCharacter.Parts[0], InWorldPoint);
    for (std::uint32_t ScopedIndex = 1; ScopedIndex < ScopedCount; ++ScopedIndex)
    {
        const SdfBonePart& ScopedPart = InCharacter.Parts[ScopedIndex];
        const float ScopedPartDistance = PartDistance(InSkeleton, ScopedPart, InWorldPoint);
        ScopedDistance = SdfSmoothMin(ScopedDistance, ScopedPartDistance, ScopedPart.BlendRadius);
    }
    return ScopedDistance;
}

Vector2D SdfCharacterGradient(const SkeletonComponent& InSkeleton, const SdfCharacterComponent& InCharacter,
                              Vector2D InWorldPoint) noexcept
{
    constexpr float ScopedEpsilon = 1.0e-3f;
    const float ScopedX0 = SdfCharacterDistance(InSkeleton, InCharacter, InWorldPoint - Vector2D{ScopedEpsilon, 0.0f});
    const float ScopedX1 = SdfCharacterDistance(InSkeleton, InCharacter, InWorldPoint + Vector2D{ScopedEpsilon, 0.0f});
    const float ScopedY0 = SdfCharacterDistance(InSkeleton, InCharacter, InWorldPoint - Vector2D{0.0f, ScopedEpsilon});
    const float ScopedY1 = SdfCharacterDistance(InSkeleton, InCharacter, InWorldPoint + Vector2D{0.0f, ScopedEpsilon});
    const Vector2D ScopedGradient{(ScopedX1 - ScopedX0) * 0.5f, (ScopedY1 - ScopedY0) * 0.5f};
    const float ScopedLength = std::sqrt(ScopedGradient.X * ScopedGradient.X + ScopedGradient.Y * ScopedGradient.Y);
    if (ScopedLength < 1.0e-8f)
    {
        return Vector2D{};
    }
    return Vector2D{ScopedGradient.X / ScopedLength, ScopedGradient.Y / ScopedLength};
}

bool SdfCharacterBounds(const SkeletonComponent& InSkeleton, const SdfCharacterComponent& InCharacter,
                        Vector2D* OutMin, Vector2D* OutMax) noexcept
{
    if (OutMin == nullptr || OutMax == nullptr)
    {
        return false;
    }
    const std::uint32_t ScopedCount = std::min<std::uint32_t>(InCharacter.PartCount, kMaxSdfCharacterParts);
    bool ScopedAny = false;
    Vector2D ScopedMin{};
    Vector2D ScopedMax{};
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedCount; ++ScopedIndex)
    {
        const SdfBonePart& ScopedPart = InCharacter.Parts[ScopedIndex];
        if (ScopedPart.BoneIndex >= InSkeleton.BoneCount)
        {
            continue;
        }
        const Vector2D ScopedCenter =
            Mat3TransformPoint(InSkeleton.GlobalMatrices[ScopedPart.BoneIndex], ScopedPart.LocalOffset);
        const float ScopedExtent = std::max({std::abs(ScopedPart.Shape.HalfExtents.X),
                                             std::abs(ScopedPart.Shape.HalfExtents.Y),
                                             std::abs(ScopedPart.Shape.Radius)}) +
                                   std::abs(ScopedPart.BlendRadius) + 1.0f;
        if (!ScopedAny)
        {
            ScopedMin = ScopedCenter - Vector2D{ScopedExtent, ScopedExtent};
            ScopedMax = ScopedCenter + Vector2D{ScopedExtent, ScopedExtent};
            ScopedAny = true;
        }
        else
        {
            ScopedMin.X = std::min(ScopedMin.X, ScopedCenter.X - ScopedExtent);
            ScopedMin.Y = std::min(ScopedMin.Y, ScopedCenter.Y - ScopedExtent);
            ScopedMax.X = std::max(ScopedMax.X, ScopedCenter.X + ScopedExtent);
            ScopedMax.Y = std::max(ScopedMax.Y, ScopedCenter.Y + ScopedExtent);
        }
    }
    if (!ScopedAny)
    {
        return false;
    }
    *OutMin = ScopedMin;
    *OutMax = ScopedMax;
    return true;
}

bool SdfCharacterCoversPoint(const SkeletonComponent& InSkeleton, const SdfCharacterComponent& InCharacter,
                             Vector2D InWorldPoint) noexcept
{
    return SdfCharacterDistance(InSkeleton, InCharacter, InWorldPoint) < 0.0f;
}

float SdfCharacterField::At(std::uint32_t InX, std::uint32_t InY) const noexcept
{
    if (!Valid() || InX >= Width || InY >= Height)
    {
        return 1.0e6f;
    }
    return Samples[static_cast<std::size_t>(InY) * Width + InX];
}

float SdfCharacterField::Sample(Vector2D InWorld) const noexcept
{
    if (!Valid())
    {
        return 1.0e6f;
    }
    const float ScopedX = (InWorld.X - Origin.X) / CellSize.X;
    const float ScopedY = (InWorld.Y - Origin.Y) / CellSize.Y;
    if (ScopedX < 0.0f || ScopedY < 0.0f || ScopedX > static_cast<float>(Width - 1) ||
        ScopedY > static_cast<float>(Height - 1))
    {
        return 1.0e6f;
    }
    const std::uint32_t ScopedX0 = static_cast<std::uint32_t>(ScopedX);
    const std::uint32_t ScopedY0 = static_cast<std::uint32_t>(ScopedY);
    const std::uint32_t ScopedX1 = std::min(ScopedX0 + 1, Width - 1);
    const std::uint32_t ScopedY1 = std::min(ScopedY0 + 1, Height - 1);
    const float ScopedFx = ScopedX - static_cast<float>(ScopedX0);
    const float ScopedFy = ScopedY - static_cast<float>(ScopedY0);
    const float ScopedTop = At(ScopedX0, ScopedY0) * (1.0f - ScopedFx) + At(ScopedX1, ScopedY0) * ScopedFx;
    const float ScopedBottom = At(ScopedX0, ScopedY1) * (1.0f - ScopedFx) + At(ScopedX1, ScopedY1) * ScopedFx;
    return ScopedTop * (1.0f - ScopedFy) + ScopedBottom * ScopedFy;
}

bool BakeSdfCharacterField(const SkeletonComponent& InSkeleton, const SdfCharacterComponent& InCharacter,
                           Vector2D InMin, Vector2D InMax, std::uint32_t InWidth, std::uint32_t InHeight,
                           SdfCharacterField* OutField) noexcept
{
    if (OutField == nullptr || InWidth < 2 || InHeight < 2 || InMax.X <= InMin.X || InMax.Y <= InMin.Y)
    {
        return false;
    }
    const std::uint64_t ScopedSamples = static_cast<std::uint64_t>(InWidth) * InHeight;
    if (ScopedSamples > kMaxSdfCharacterFieldSamples)
    {
        return false;
    }
    OutField->Width = InWidth;
    OutField->Height = InHeight;
    OutField->Origin = InMin;
    OutField->CellSize = Vector2D{(InMax.X - InMin.X) / static_cast<float>(InWidth - 1),
                                  (InMax.Y - InMin.Y) / static_cast<float>(InHeight - 1)};
    OutField->Samples.resize(static_cast<std::size_t>(ScopedSamples));
    for (std::uint32_t ScopedY = 0; ScopedY < InHeight; ++ScopedY)
    {
        for (std::uint32_t ScopedX = 0; ScopedX < InWidth; ++ScopedX)
        {
            const Vector2D ScopedPoint{InMin.X + OutField->CellSize.X * static_cast<float>(ScopedX),
                                       InMin.Y + OutField->CellSize.Y * static_cast<float>(ScopedY)};
            OutField->Samples[static_cast<std::size_t>(ScopedY) * InWidth + ScopedX] =
                SdfCharacterDistance(InSkeleton, InCharacter, ScopedPoint);
        }
    }
    ++OutField->Version;
    return true;
}

bool BuildSdfCharacterMesh(const SdfCharacterField& InField, float InIso, SdfCharacterMeshData* OutMesh) noexcept
{
    if (OutMesh == nullptr || !InField.Valid())
    {
        return false;
    }
    OutMesh->Positions.clear();
    OutMesh->Indices.clear();
    constexpr std::uint32_t ScopedMaxVertices = 65000u;
    for (std::uint32_t ScopedY = 0; ScopedY + 1 < InField.Height; ++ScopedY)
    {
        for (std::uint32_t ScopedX = 0; ScopedX + 1 < InField.Width; ++ScopedX)
        {
            const float ScopedCenter =
                (InField.At(ScopedX, ScopedY) + InField.At(ScopedX + 1, ScopedY) +
                 InField.At(ScopedX, ScopedY + 1) + InField.At(ScopedX + 1, ScopedY + 1)) * 0.25f;
            if (ScopedCenter >= InIso)
            {
                continue;
            }
            if (OutMesh->Positions.size() + 4 > ScopedMaxVertices)
            {
                return false;
            }
            const std::uint16_t ScopedBase = static_cast<std::uint16_t>(OutMesh->Positions.size());
            const float ScopedX0 = InField.Origin.X + InField.CellSize.X * static_cast<float>(ScopedX);
            const float ScopedY0 = InField.Origin.Y + InField.CellSize.Y * static_cast<float>(ScopedY);
            const float ScopedX1 = ScopedX0 + InField.CellSize.X;
            const float ScopedY1 = ScopedY0 + InField.CellSize.Y;
            OutMesh->Positions.push_back(Vector2D{ScopedX0, ScopedY0});
            OutMesh->Positions.push_back(Vector2D{ScopedX1, ScopedY0});
            OutMesh->Positions.push_back(Vector2D{ScopedX1, ScopedY1});
            OutMesh->Positions.push_back(Vector2D{ScopedX0, ScopedY1});
            OutMesh->Indices.push_back(ScopedBase);
            OutMesh->Indices.push_back(static_cast<std::uint16_t>(ScopedBase + 1));
            OutMesh->Indices.push_back(static_cast<std::uint16_t>(ScopedBase + 2));
            OutMesh->Indices.push_back(ScopedBase);
            OutMesh->Indices.push_back(static_cast<std::uint16_t>(ScopedBase + 2));
            OutMesh->Indices.push_back(static_cast<std::uint16_t>(ScopedBase + 3));
        }
    }
    return !OutMesh->Positions.empty();
}

bool BuildSdfCharacterBrickMap(const SdfCharacterField& InField, float InNarrowBand, SdfBrickMap* OutMap) noexcept
{
    if (OutMap == nullptr || !InField.Valid())
    {
        return false;
    }
    SdfField ScopedView;
    ScopedView.samples = InField.Samples.data();
    ScopedView.width = InField.Width;
    ScopedView.height = InField.Height;
    ScopedView.origin = InField.Origin;
    ScopedView.cellSize = InField.CellSize;
    ScopedView.version = InField.Version;
    return OutMap->Build(ScopedView, InNarrowBand);
}

}
