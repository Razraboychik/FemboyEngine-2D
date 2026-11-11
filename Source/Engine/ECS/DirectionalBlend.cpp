// FemboyEngine2D - directional blend space implementation.
#include "Engine/ECS/DirectionalBlend.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

namespace Fby
{

void Facing2DFromVector(Facing2D& InFacing, Vector2D InVector) noexcept
{
    const float ScopedLengthSquared = InVector.LengthSquared();
    if (!(ScopedLengthSquared > 1.0e-12f))
    {
        return;
    }
    const float ScopedInverseLength = 1.0f / std::sqrt(ScopedLengthSquared);
    InFacing.Direction = InVector * ScopedInverseLength;
    InFacing.Angle = Animation::DirectionalBlendAngleOf(InFacing.Direction);
}
}

namespace Fby::Animation
{
namespace
{

// A plane sample that lands exactly on a shared hull edge belongs to both
// triangles; the small tolerance lets either one accept it and keeps the
// weights continuous across the seam.
constexpr float kPlaneEdgeTolerance = 1.0e-5f;

[[nodiscard]] float Cross(Vector2D InA, Vector2D InB, Vector2D InC) noexcept
{
    return (InB - InA).Cross(InC - InA);
}

// Writes the authored sources after clamping, optional normalization and zero
// removal. A muted sample keeps its first clip id with weight one, so the
// caller always resolves a pose source.
void FinalizeSample(DirectionalBlendSample* OutSample, bool InNormalize) noexcept
{
    std::uint8_t ScopedCount = OutSample->Count;
    if (ScopedCount > kMaxDirectionalWeights)
    {
        ScopedCount = static_cast<std::uint8_t>(kMaxDirectionalWeights);
    }
    float ScopedSum = 0.0f;
    for (std::uint8_t ScopedIndex = 0; ScopedIndex < ScopedCount; ++ScopedIndex)
    {
        const float ScopedWeight = std::max(OutSample->Weights[ScopedIndex].Weight, 0.0f);
        OutSample->Weights[ScopedIndex].Weight = ScopedWeight;
        ScopedSum += ScopedWeight;
    }
    if (InNormalize && ScopedSum > kDirectionalBlendEpsilon)
    {
        const float ScopedInverseSum = 1.0f / ScopedSum;
        for (std::uint8_t ScopedIndex = 0; ScopedIndex < ScopedCount; ++ScopedIndex)
        {
            OutSample->Weights[ScopedIndex].Weight *= ScopedInverseSum;
        }
    }
    std::uint8_t ScopedOut = 0;
    for (std::uint8_t ScopedIndex = 0; ScopedIndex < ScopedCount; ++ScopedIndex)
    {
        if (OutSample->Weights[ScopedIndex].Weight > kDirectionalBlendEpsilon)
        {
            OutSample->Weights[ScopedOut] = OutSample->Weights[ScopedIndex];
            ++ScopedOut;
        }
    }
    if (ScopedOut == 0 && ScopedCount > 0)
    {
        OutSample->Weights[0].Weight = 1.0f;
        ScopedOut = 1;
    }
    OutSample->Count = ScopedOut;
}

// An exact heading hit: the interpolation weight is one for a single source.
void EmitSingle(DirectionalBlendSample* OutSample, const DirectionalBlendAnchorEntry& InAnchor,
                bool InNormalize) noexcept
{
    const float ScopedWeight = InNormalize ? 1.0f : std::max(InAnchor.WeightScale, 0.0f);
    OutSample->Weights[0] = DirectionalBlendWeight{InAnchor.ClipId, ScopedWeight};
    OutSample->Count = 1;
    FinalizeSample(OutSample, InNormalize);
}

void EmitSegment(DirectionalBlendSample* OutSample, const DirectionalBlendAnchorEntry& InA,
                 const DirectionalBlendAnchorEntry& InB, float InT, bool InNormalize) noexcept
{
    const float ScopedT = std::clamp(InT, 0.0f, 1.0f);
    OutSample->Weights[0] = DirectionalBlendWeight{InA.ClipId, (1.0f - ScopedT) * std::max(InA.WeightScale, 0.0f)};
    OutSample->Weights[1] = DirectionalBlendWeight{InB.ClipId, ScopedT * std::max(InB.WeightScale, 0.0f)};
    OutSample->Count = 2;
    FinalizeSample(OutSample, InNormalize);
}

// Projects the sample onto a segment. A degenerate segment pins the sample to
// its first endpoint instead of producing a division by zero.
[[nodiscard]] float SegmentParameter(Vector2D InPoint, Vector2D InA, Vector2D InB) noexcept
{
    const Vector2D ScopedEdge = InB - InA;
    const float ScopedLengthSquared = ScopedEdge.LengthSquared();
    if (!(ScopedLengthSquared > 1.0e-12f))
    {
        return 0.0f;
    }
    return std::clamp((InPoint - InA).Dot(ScopedEdge) / ScopedLengthSquared, 0.0f, 1.0f);
}

// Barycentric coordinates of a point inside a triangle. The edge tolerance
// accepts a point that sits on a shared hull edge. Returns false when the
// triangle is degenerate.
[[nodiscard]] bool Barycentric(Vector2D InPoint, Vector2D InA, Vector2D InB, Vector2D InC, float& OutA, float& OutB,
                               float& OutC) noexcept
{
    const float ScopedDenominator = (InB - InA).Cross(InC - InA);
    if (std::fabs(ScopedDenominator) <= 1.0e-12f)
    {
        return false;
    }
    const float ScopedInverse = 1.0f / ScopedDenominator;
    const float ScopedB = (InPoint - InA).Cross(InC - InA) * ScopedInverse;
    const float ScopedC = (InB - InA).Cross(InPoint - InA) * ScopedInverse;
    const float ScopedA = 1.0f - ScopedB - ScopedC;
    OutA = ScopedA;
    OutB = ScopedB;
    OutC = ScopedC;
    return ScopedA >= -kPlaneEdgeTolerance && ScopedB >= -kPlaneEdgeTolerance && ScopedC >= -kPlaneEdgeTolerance;
}

void EvaluateRing(const DirectionalBlendTable& InTable, Vector2D InDirection,
                  DirectionalBlendSample* OutSample) noexcept
{
    const std::uint16_t ScopedCount = InTable.AnchorCount;
    if (ScopedCount == 1)
    {
        EmitSingle(OutSample, InTable.Anchors[0], InTable.NormalizeWeights);
        return;
    }
    const float ScopedAngle = DirectionalBlendAngleOf(InDirection);

    // First anchor whose angle is strictly past the sample: the bracket is
    // Anchors[Middle - 1] and Anchors[Middle], with the array treated as a ring.
    std::uint16_t ScopedLow = 0;
    std::uint16_t ScopedHigh = ScopedCount;
    while (ScopedLow < ScopedHigh)
    {
        const std::uint16_t ScopedMiddle = static_cast<std::uint16_t>((ScopedLow + ScopedHigh) / 2);
        if (InTable.Anchors[ScopedMiddle].Angle <= ScopedAngle)
        {
            ScopedLow = static_cast<std::uint16_t>(ScopedMiddle + 1);
        }
        else
        {
            ScopedHigh = ScopedMiddle;
        }
    }

    const std::uint16_t ScopedNext = ScopedLow;
    const std::uint16_t ScopedPrev =
        ScopedNext == 0 ? static_cast<std::uint16_t>(ScopedCount - 1) : static_cast<std::uint16_t>(ScopedNext - 1);
    const std::uint16_t ScopedNextIndex = ScopedNext == ScopedCount ? 0 : ScopedNext;
    float ScopedStart = InTable.Anchors[ScopedPrev].Angle;
    float ScopedEnd = ScopedStart;
    float ScopedTheta = ScopedAngle;
    if (ScopedNext == ScopedCount)
    {
        // The sample is past the last anchor, so the span wraps to the first.
        ScopedEnd = InTable.Anchors[0].Angle + Sim::kTwoPi;
    }
    else if (ScopedNext == 0)
    {
        // The sample is before the first anchor, so the span starts at the last.
        ScopedEnd = InTable.Anchors[0].Angle + Sim::kTwoPi;
        ScopedTheta = ScopedAngle + Sim::kTwoPi;
    }
    else
    {
        ScopedEnd = InTable.Anchors[ScopedNext].Angle;
    }

    const float ScopedSpan = ScopedEnd - ScopedStart;
    if (!(ScopedSpan > kDirectionalBlendEpsilon))
    {
        EmitSingle(OutSample, InTable.Anchors[ScopedPrev], InTable.NormalizeWeights);
        return;
    }
    const float ScopedT = std::clamp((ScopedTheta - ScopedStart) / ScopedSpan, 0.0f, 1.0f);
    if (ScopedT <= kDirectionalBlendEpsilon)
    {
        EmitSingle(OutSample, InTable.Anchors[ScopedPrev], InTable.NormalizeWeights);
        return;
    }
    if (ScopedT >= 1.0f - kDirectionalBlendEpsilon)
    {
        EmitSingle(OutSample, InTable.Anchors[ScopedNextIndex], InTable.NormalizeWeights);
        return;
    }
    EmitSegment(OutSample, InTable.Anchors[ScopedPrev], InTable.Anchors[ScopedNextIndex], ScopedT,
                InTable.NormalizeWeights);
}

void EvaluatePlane(const DirectionalBlendTable& InTable, Vector2D InDirection,
                   DirectionalBlendSample* OutSample) noexcept
{
    const std::uint16_t ScopedCount = InTable.AnchorCount;
    if (ScopedCount == 1)
    {
        EmitSingle(OutSample, InTable.Anchors[0], InTable.NormalizeWeights);
        return;
    }
    if (InTable.HullCount < 3)
    {
        // Collinear or two-point authoring reduces to a segment blend.
        const std::uint16_t ScopedA = InTable.Hull[0];
        const std::uint16_t ScopedB = InTable.Hull[InTable.HullCount - 1];
        if (ScopedA == ScopedB)
        {
            EmitSingle(OutSample, InTable.Anchors[ScopedA], InTable.NormalizeWeights);
            return;
        }
        const float ScopedT =
            SegmentParameter(InDirection, InTable.Anchors[ScopedA].Position, InTable.Anchors[ScopedB].Position);
        EmitSegment(OutSample, InTable.Anchors[ScopedA], InTable.Anchors[ScopedB], ScopedT, InTable.NormalizeWeights);
        return;
    }

    for (std::uint16_t ScopedTriangle = 0; ScopedTriangle < InTable.TriangleCount; ++ScopedTriangle)
    {
        const DirectionalBlendTriangle& ScopedShape = InTable.Triangles[ScopedTriangle];
        const DirectionalBlendAnchorEntry& ScopedA = InTable.Anchors[ScopedShape.AnchorA];
        const DirectionalBlendAnchorEntry& ScopedB = InTable.Anchors[ScopedShape.AnchorB];
        const DirectionalBlendAnchorEntry& ScopedC = InTable.Anchors[ScopedShape.AnchorC];
        float ScopedWeightA = 0.0f;
        float ScopedWeightB = 0.0f;
        float ScopedWeightC = 0.0f;
        if (!Barycentric(InDirection, ScopedA.Position, ScopedB.Position, ScopedC.Position, ScopedWeightA,
                         ScopedWeightB, ScopedWeightC))
        {
            continue;
        }
        OutSample->Weights[0] =
            DirectionalBlendWeight{ScopedA.ClipId, std::max(ScopedWeightA, 0.0f) * ScopedA.WeightScale};
        OutSample->Weights[1] =
            DirectionalBlendWeight{ScopedB.ClipId, std::max(ScopedWeightB, 0.0f) * ScopedB.WeightScale};
        OutSample->Weights[2] =
            DirectionalBlendWeight{ScopedC.ClipId, std::max(ScopedWeightC, 0.0f) * ScopedC.WeightScale};
        OutSample->Count = 3;
        FinalizeSample(OutSample, InTable.NormalizeWeights);
        return;
    }

    // Outside the hull the closest boundary point is the continuous extension:
    // the sample never jumps to an unrelated anchor.
    float ScopedBestDistance = std::numeric_limits<float>::max();
    std::uint16_t ScopedBestA = InTable.Hull[0];
    std::uint16_t ScopedBestB = InTable.Hull[1];
    float ScopedBestT = 0.0f;
    for (std::uint16_t ScopedEdge = 0; ScopedEdge < InTable.HullCount; ++ScopedEdge)
    {
        const std::uint16_t ScopedEdgeA = InTable.Hull[ScopedEdge];
        const std::uint16_t ScopedEdgeB = InTable.Hull[(ScopedEdge + 1) % InTable.HullCount];
        const Vector2D ScopedPointA = InTable.Anchors[ScopedEdgeA].Position;
        const Vector2D ScopedPointB = InTable.Anchors[ScopedEdgeB].Position;
        const float ScopedT = SegmentParameter(InDirection, ScopedPointA, ScopedPointB);
        const Vector2D ScopedClosest = ScopedPointA + (ScopedPointB - ScopedPointA) * ScopedT;
        const float ScopedDistance = (InDirection - ScopedClosest).LengthSquared();
        if (ScopedDistance < ScopedBestDistance)
        {
            ScopedBestDistance = ScopedDistance;
            ScopedBestA = ScopedEdgeA;
            ScopedBestB = ScopedEdgeB;
            ScopedBestT = ScopedT;
        }
    }
    EmitSegment(OutSample, InTable.Anchors[ScopedBestA], InTable.Anchors[ScopedBestB], ScopedBestT,
                InTable.NormalizeWeights);
}

// Monotone-chain convex hull over the authored anchor positions. Sorting by
// position with the array index as the final key keeps the hull deterministic.
void BuildHull(const std::array<DirectionalBlendAnchorEntry, kMaxDirectionalAnchors>& InAnchors, std::uint16_t InCount,
               std::array<std::uint16_t, kMaxDirectionalAnchors>& OutHull, std::uint16_t& OutHullCount) noexcept
{
    std::array<std::uint16_t, kMaxDirectionalAnchors> ScopedOrder{};
    for (std::uint16_t ScopedIndex = 0; ScopedIndex < InCount; ++ScopedIndex)
    {
        ScopedOrder[ScopedIndex] = ScopedIndex;
    }
    std::sort(ScopedOrder.begin(), ScopedOrder.begin() + static_cast<std::ptrdiff_t>(InCount),
              [&InAnchors](std::uint16_t InLeft, std::uint16_t InRight)
              {
                  const Vector2D ScopedLeft = InAnchors[InLeft].Position;
                  const Vector2D ScopedRight = InAnchors[InRight].Position;
                  if (ScopedLeft.X != ScopedRight.X)
                  {
                      return ScopedLeft.X < ScopedRight.X;
                  }
                  if (ScopedLeft.Y != ScopedRight.Y)
                  {
                      return ScopedLeft.Y < ScopedRight.Y;
                  }
                  return InLeft < InRight;
              });

    // Duplicate positions would add zero-area edges, so they collapse first.
    std::uint16_t ScopedUnique = 0;
    for (std::uint16_t ScopedIndex = 0; ScopedIndex < InCount; ++ScopedIndex)
    {
        const std::uint16_t ScopedCurrent = ScopedOrder[ScopedIndex];
        if (ScopedUnique > 0 && InAnchors[ScopedCurrent].Position == InAnchors[ScopedOrder[ScopedUnique - 1]].Position)
        {
            continue;
        }
        ScopedOrder[ScopedUnique] = ScopedCurrent;
        ++ScopedUnique;
    }

    std::array<std::uint16_t, kMaxDirectionalAnchors * 2> ScopedChain{};
    std::uint16_t ScopedChainCount = 0;
    for (std::uint16_t ScopedIndex = 0; ScopedIndex < ScopedUnique; ++ScopedIndex)
    {
        while (ScopedChainCount >= 2 && Cross(InAnchors[ScopedChain[ScopedChainCount - 2]].Position,
                                              InAnchors[ScopedChain[ScopedChainCount - 1]].Position,
                                              InAnchors[ScopedOrder[ScopedIndex]].Position) <= 0.0f)
        {
            --ScopedChainCount;
        }
        ScopedChain[ScopedChainCount] = ScopedOrder[ScopedIndex];
        ++ScopedChainCount;
    }
    const std::uint16_t ScopedLowerCount = static_cast<std::uint16_t>(ScopedChainCount + 1);
    for (std::int32_t ScopedIndex = static_cast<std::int32_t>(ScopedUnique) - 1; ScopedIndex >= 0; --ScopedIndex)
    {
        const std::uint16_t ScopedPoint = ScopedOrder[static_cast<std::uint16_t>(ScopedIndex)];
        while (ScopedChainCount >= ScopedLowerCount &&
               Cross(InAnchors[ScopedChain[ScopedChainCount - 2]].Position,
                     InAnchors[ScopedChain[ScopedChainCount - 1]].Position, InAnchors[ScopedPoint].Position) <= 0.0f)
        {
            --ScopedChainCount;
        }
        ScopedChain[ScopedChainCount] = ScopedPoint;
        ++ScopedChainCount;
    }
    // The final point repeats the first one.
    OutHullCount = static_cast<std::uint16_t>(ScopedChainCount - 1);
    for (std::uint16_t ScopedIndex = 0; ScopedIndex < OutHullCount; ++ScopedIndex)
    {
        OutHull[ScopedIndex] = ScopedChain[ScopedIndex];
    }
}
}

float DirectionalBlendNormalizeAngle(float InRadians) noexcept
{
    if (!std::isfinite(InRadians))
    {
        return 0.0f;
    }
    float ScopedAngle = std::fmod(InRadians, Sim::kTwoPi);
    if (ScopedAngle < 0.0f)
    {
        ScopedAngle += Sim::kTwoPi;
    }
    return ScopedAngle;
}

float DirectionalBlendAngleOf(Vector2D InDirection) noexcept
{
    if (!(InDirection.LengthSquared() > 1.0e-12f))
    {
        return 0.0f;
    }
    return DirectionalBlendNormalizeAngle(Sim::Atan2(InDirection.Y, InDirection.X));
}

bool ValidateDirectionalBlendSpace(const DirectionalBlendSpaceDocument& InDocument) noexcept
{
    if (InDocument.AnchorCount > kMaxDirectionalAnchors)
    {
        return false;
    }
    std::uint16_t ScopedEnabled = 0;
    for (std::uint16_t ScopedIndex = 0; ScopedIndex < InDocument.AnchorCount; ++ScopedIndex)
    {
        const DirectionalBlendAnchor& ScopedAnchor = InDocument.Anchors[ScopedIndex];
        if (!ScopedAnchor.Enabled)
        {
            continue;
        }
        ++ScopedEnabled;
        if (!std::isfinite(ScopedAnchor.Position.X) || !std::isfinite(ScopedAnchor.Position.Y) ||
            !std::isfinite(ScopedAnchor.WeightScale) || ScopedAnchor.WeightScale < 0.0f)
        {
            return false;
        }
        if (InDocument.Topology == DirectionalBlendTopology::Ring &&
            !(ScopedAnchor.Position.LengthSquared() > 1.0e-12f))
        {
            return false;
        }
    }
    return ScopedEnabled > 0;
}

bool BakeDirectionalBlendSpace(const DirectionalBlendSpaceDocument& InDocument,
                               DirectionalBlendTable* OutTable) noexcept
{
    if (OutTable == nullptr)
    {
        return false;
    }
    *OutTable = DirectionalBlendTable{};
    if (!ValidateDirectionalBlendSpace(InDocument))
    {
        return false;
    }
    std::uint16_t ScopedCount = 0;
    for (std::uint16_t ScopedIndex = 0; ScopedIndex < InDocument.AnchorCount; ++ScopedIndex)
    {
        const DirectionalBlendAnchor& ScopedAnchor = InDocument.Anchors[ScopedIndex];
        if (!ScopedAnchor.Enabled)
        {
            continue;
        }
        DirectionalBlendAnchorEntry& ScopedEntry = OutTable->Anchors[ScopedCount];
        ScopedEntry.Position = ScopedAnchor.Position;
        ScopedEntry.WeightScale = ScopedAnchor.WeightScale;
        ScopedEntry.ClipId = ScopedAnchor.ClipId;
        ScopedEntry.Angle = InDocument.Topology == DirectionalBlendTopology::Ring
                                ? DirectionalBlendAngleOf(ScopedAnchor.Position)
                                : 0.0f;
        ++ScopedCount;
    }
    OutTable->AnchorCount = ScopedCount;
    OutTable->Topology = InDocument.Topology;
    OutTable->NormalizeWeights = InDocument.NormalizeWeights;

    if (InDocument.Topology == DirectionalBlendTopology::Ring)
    {
        // Stable insertion sort: equal angles keep the authored order, so the
        // bracket lookup stays deterministic.
        for (std::uint16_t ScopedIndex = 1; ScopedIndex < ScopedCount; ++ScopedIndex)
        {
            const DirectionalBlendAnchorEntry ScopedEntry = OutTable->Anchors[ScopedIndex];
            std::uint16_t ScopedPosition = ScopedIndex;
            while (ScopedPosition > 0 && OutTable->Anchors[ScopedPosition - 1].Angle > ScopedEntry.Angle)
            {
                OutTable->Anchors[ScopedPosition] = OutTable->Anchors[ScopedPosition - 1];
                --ScopedPosition;
            }
            OutTable->Anchors[ScopedPosition] = ScopedEntry;
        }
        return true;
    }

    BuildHull(OutTable->Anchors, OutTable->AnchorCount, OutTable->Hull, OutTable->HullCount);
    for (std::uint16_t ScopedVertex = 1; ScopedVertex + 1 < OutTable->HullCount; ++ScopedVertex)
    {
        OutTable->Triangles[OutTable->TriangleCount] =
            DirectionalBlendTriangle{OutTable->Hull[0], OutTable->Hull[ScopedVertex],
                                     OutTable->Hull[static_cast<std::uint16_t>(ScopedVertex + 1)]};
        ++OutTable->TriangleCount;
    }
    return true;
}

void EvaluateDirectionalBlend(const DirectionalBlendTable& InTable, Vector2D InDirection,
                              DirectionalBlendSample* OutSample) noexcept
{
    if (OutSample == nullptr)
    {
        return;
    }
    OutSample->Count = 0;
    if (InTable.AnchorCount == 0 || InTable.AnchorCount > kMaxDirectionalAnchors)
    {
        return;
    }
    if (InTable.Topology == DirectionalBlendTopology::Ring)
    {
        EvaluateRing(InTable, InDirection, OutSample);
        return;
    }
    EvaluatePlane(InTable, InDirection, OutSample);
}
}
