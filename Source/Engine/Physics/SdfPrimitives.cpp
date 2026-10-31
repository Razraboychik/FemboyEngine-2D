// FemboyEngine2D - evaluation of the 2D signed distance primitive palette.
#include "Engine/Physics/SdfPrimitives.hpp"

#include <array>
#include <cmath>

namespace Fby
{
namespace
{

// Full turn and half turn in radians, used by the radial families.
constexpr float kTwoPi = 6.28318530717958647692f;
constexpr float kHalfPi = 1.57079632679489661923f;

// Square root of three, used by the equilateral triangle and the ellipse.
constexpr float kSqrt3 = 1.73205080756887729353f;

// Circumradius to apothem ratio of a regular hexagon, also the equilateral triangle scale.
constexpr float kSqrt3OverTwo = 0.86602540378443864676f;

// Cosine of one eighth of a turn, the octagon circumradius to apothem ratio.
constexpr float kCosPiOverEight = 0.92387953251128675612f;

// Below this length a vector is treated as degenerate.
constexpr float kTinyLength = 1.0e-6f;

[[nodiscard]] float Length(Vector2D InVector) noexcept
{
    return std::sqrt(InVector.LengthSquared());
}

[[nodiscard]] float ClampValue(float InValue, float InMin, float InMax) noexcept
{
    if (!(InValue > InMin))
    {
        return InMin;
    }
    if (!(InValue < InMax))
    {
        return InMax;
    }
    return InValue;
}

[[nodiscard]] float SdfSign(float InValue) noexcept
{
    if (InValue > 0.0f)
    {
        return 1.0f;
    }
    if (InValue < 0.0f)
    {
        return -1.0f;
    }
    return 0.0f;
}

// Distance from InPoint to the segment InA..InB, always non negative.
[[nodiscard]] float SegmentDistance(Vector2D InPoint, Vector2D InA, Vector2D InB) noexcept
{
    const Vector2D ScopedEdge = InB - InA;
    const float ScopedLengthSquared = ScopedEdge.LengthSquared();
    if (!(ScopedLengthSquared > kTinyLength))
    {
        return Length(InPoint - InA);
    }
    const float ScopedT = ClampValue((InPoint - InA).Dot(ScopedEdge) / ScopedLengthSquared, 0.0f, 1.0f);
    return Length(InPoint - (InA + ScopedEdge * ScopedT));
}

// Projection of InVector on InEdge, clamped to the segment parameter range.
[[nodiscard]] float SegmentParameter(Vector2D InVector, Vector2D InEdge) noexcept
{
    const float ScopedLengthSquared = InEdge.LengthSquared();
    if (!(ScopedLengthSquared > kTinyLength))
    {
        return 0.0f;
    }
    return ClampValue(InVector.Dot(InEdge) / ScopedLengthSquared, 0.0f, 1.0f);
}

// Exact distance to a disc of InRadius centred at the origin.
[[nodiscard]] float CircleDistance(Vector2D InPoint, float InRadius) noexcept
{
    return Length(InPoint) - InRadius;
}

// Exact distance to an axis aligned box with the given half extents.
[[nodiscard]] float BoxDistance(Vector2D InPoint, Vector2D InHalfExtents) noexcept
{
    const Vector2D ScopedQ{std::fabs(InPoint.X) - InHalfExtents.X, std::fabs(InPoint.Y) - InHalfExtents.Y};
    const Vector2D ScopedOutside{std::fmax(ScopedQ.X, 0.0f), std::fmax(ScopedQ.Y, 0.0f)};
    return Length(ScopedOutside) + std::fmin(std::fmax(ScopedQ.X, ScopedQ.Y), 0.0f);
}

// Exact distance to a box whose corners are rounded by InRadius.
[[nodiscard]] float RoundedBoxDistance(Vector2D InPoint, Vector2D InHalfExtents, float InRadius) noexcept
{
    const float ScopedRadius = std::fmax(InRadius, 0.0f);
    const Vector2D ScopedQ{std::fabs(InPoint.X) - InHalfExtents.X + ScopedRadius,
                           std::fabs(InPoint.Y) - InHalfExtents.Y + ScopedRadius};
    const Vector2D ScopedOutside{std::fmax(ScopedQ.X, 0.0f), std::fmax(ScopedQ.Y, 0.0f)};
    return Length(ScopedOutside) + std::fmin(std::fmax(ScopedQ.X, ScopedQ.Y), 0.0f) - ScopedRadius;
}

// Ellipse distance. The distance to an ellipse needs the root of a cubic, so the closed form
// quartic solution from Inigo Quilez is evaluated directly, with a circle fallback for a
// degenerate ellipse.
[[nodiscard]] float EllipseDistance(Vector2D InPoint, Vector2D InRadii) noexcept
{
    const float ScopedA = std::fmax(InRadii.X, kTinyLength);
    const float ScopedB = std::fmax(InRadii.Y, kTinyLength);
    const float ScopedFallback = Length(InPoint) - std::fmin(ScopedA, ScopedB);
    if (std::fabs(ScopedA - ScopedB) <= kTinyLength)
    {
        return Length(InPoint) - ScopedA;
    }
    Vector2D ScopedPoint{std::fabs(InPoint.X), std::fabs(InPoint.Y)};
    Vector2D ScopedRadii{ScopedA, ScopedB};
    if (ScopedPoint.X > ScopedPoint.Y)
    {
        ScopedPoint = Vector2D{ScopedPoint.Y, ScopedPoint.X};
        ScopedRadii = Vector2D{ScopedRadii.Y, ScopedRadii.X};
    }
    const float ScopedL = ScopedRadii.Y * ScopedRadii.Y - ScopedRadii.X * ScopedRadii.X;
    if (!(std::fabs(ScopedL) > kTinyLength))
    {
        return ScopedFallback;
    }
    const float ScopedM = ScopedRadii.X * ScopedPoint.X / ScopedL;
    const float ScopedM2 = ScopedM * ScopedM;
    const float ScopedN = ScopedRadii.Y * ScopedPoint.Y / ScopedL;
    const float ScopedN2 = ScopedN * ScopedN;
    const float ScopedC = (ScopedM2 + ScopedN2 - 1.0f) / 3.0f;
    const float ScopedC3 = ScopedC * ScopedC * ScopedC;
    const float ScopedQ = ScopedC3 + ScopedM2 * ScopedN2 * 2.0f;
    const float ScopedD = ScopedC3 + ScopedM2 * ScopedN2;
    const float ScopedG = ScopedM + ScopedM * ScopedN2;
    float ScopedCo = 0.0f;
    if (ScopedD < 0.0f)
    {
        if (!(std::fabs(ScopedC3) > kTinyLength))
        {
            return ScopedFallback;
        }
        const float ScopedH = std::acos(ClampValue(ScopedQ / ScopedC3, -1.0f, 1.0f)) / 3.0f;
        const float ScopedS = std::cos(ScopedH);
        const float ScopedT = std::sin(ScopedH) * kSqrt3;
        const float ScopedRx =
            std::sqrt(std::fmax(-ScopedC * (ScopedS + ScopedT + 2.0f) + ScopedM2, 0.0f));
        const float ScopedRy =
            std::sqrt(std::fmax(-ScopedC * (ScopedS - ScopedT + 2.0f) + ScopedM2, 0.0f));
        const float ScopedProduct = ScopedRx * ScopedRy;
        if (!(ScopedProduct > kTinyLength))
        {
            return ScopedFallback;
        }
        ScopedCo = (ScopedRy + SdfSign(ScopedL) * ScopedRx + std::fabs(ScopedG) / ScopedProduct - ScopedM) *
                   0.5f;
    }
    else
    {
        const float ScopedH = 2.0f * ScopedM * ScopedN * std::sqrt(std::fmax(ScopedD, 0.0f));
        const float ScopedS =
            SdfSign(ScopedQ + ScopedH) * std::pow(std::fabs(ScopedQ + ScopedH), 1.0f / 3.0f);
        const float ScopedU =
            SdfSign(ScopedQ - ScopedH) * std::pow(std::fabs(ScopedQ - ScopedH), 1.0f / 3.0f);
        const float ScopedRx = -ScopedS - ScopedU - ScopedC * 4.0f + 2.0f * ScopedM2;
        const float ScopedRy = (ScopedS - ScopedU) * kSqrt3;
        const float ScopedRm = std::sqrt(ScopedRx * ScopedRx + ScopedRy * ScopedRy);
        const float ScopedRoot = ScopedRm - ScopedRx;
        if (!(ScopedRoot > kTinyLength) || !(ScopedRm > kTinyLength))
        {
            return ScopedFallback;
        }
        ScopedCo = (ScopedRy / std::sqrt(ScopedRoot) + 2.0f * ScopedG / ScopedRm - ScopedM) * 0.5f;
    }
    const float ScopedClamped = ClampValue(ScopedCo, -1.0f, 1.0f);
    const float ScopedSine = std::sqrt(std::fmax(1.0f - ScopedClamped * ScopedClamped, 0.0f));
    const Vector2D ScopedClosest{ScopedRadii.X * ScopedClamped, ScopedRadii.Y * ScopedSine};
    return Length(ScopedClosest - ScopedPoint) * SdfSign(ScopedPoint.Y - ScopedClosest.Y);
}

// Exact distance to a rhombus with the given half diagonals.
[[nodiscard]] float RhombusDistance(Vector2D InPoint, Vector2D InHalfDiagonals) noexcept
{
    const Vector2D ScopedPoint{std::fabs(InPoint.X), std::fabs(InPoint.Y)};
    const float ScopedDenominator = InHalfDiagonals.LengthSquared();
    if (!(ScopedDenominator > kTinyLength))
    {
        return Length(ScopedPoint);
    }
    const Vector2D ScopedDifference = InHalfDiagonals - ScopedPoint * 2.0f;
    const float ScopedNDot =
        InHalfDiagonals.X * ScopedDifference.X - InHalfDiagonals.Y * ScopedDifference.Y;
    const float ScopedH = ClampValue(ScopedNDot / ScopedDenominator, -1.0f, 1.0f);
    const Vector2D ScopedHalf{InHalfDiagonals.X * 0.5f * (1.0f - ScopedH),
                              InHalfDiagonals.Y * 0.5f * (1.0f + ScopedH)};
    const float ScopedDistance = Length(ScopedPoint - ScopedHalf);
    const float ScopedSide = ScopedPoint.X * InHalfDiagonals.Y + ScopedPoint.Y * InHalfDiagonals.X -
                             InHalfDiagonals.X * InHalfDiagonals.Y;
    return ScopedDistance * SdfSign(ScopedSide);
}

// Exact distance to any triangle, from the three edge projections and the winding sign. The
// squared distances and the side values are minimised independently, as the reference formula does.
[[nodiscard]] float TriangleDistance(Vector2D InPoint, Vector2D InA, Vector2D InB, Vector2D InC) noexcept
{
    const Vector2D ScopedEdge0 = InB - InA;
    const Vector2D ScopedEdge1 = InC - InB;
    const Vector2D ScopedEdge2 = InA - InC;
    const Vector2D ScopedVertex0 = InPoint - InA;
    const Vector2D ScopedVertex1 = InPoint - InB;
    const Vector2D ScopedVertex2 = InPoint - InC;
    const Vector2D ScopedProjected0 = ScopedVertex0 - ScopedEdge0 * SegmentParameter(ScopedVertex0, ScopedEdge0);
    const Vector2D ScopedProjected1 = ScopedVertex1 - ScopedEdge1 * SegmentParameter(ScopedVertex1, ScopedEdge1);
    const Vector2D ScopedProjected2 = ScopedVertex2 - ScopedEdge2 * SegmentParameter(ScopedVertex2, ScopedEdge2);
    const float ScopedWinding = SdfSign(ScopedEdge0.Cross(ScopedEdge2));
    const float ScopedSquared0 = ScopedProjected0.LengthSquared();
    const float ScopedSquared1 = ScopedProjected1.LengthSquared();
    const float ScopedSquared2 = ScopedProjected2.LengthSquared();
    const float ScopedSide0 = ScopedWinding * ScopedVertex0.Cross(ScopedEdge0);
    const float ScopedSide1 = ScopedWinding * ScopedVertex1.Cross(ScopedEdge1);
    const float ScopedSide2 = ScopedWinding * ScopedVertex2.Cross(ScopedEdge2);
    const float ScopedBestSquared = std::fmin(std::fmin(ScopedSquared0, ScopedSquared1), ScopedSquared2);
    const float ScopedBestSide = std::fmin(std::fmin(ScopedSide0, ScopedSide1), ScopedSide2);
    return -std::sqrt(ScopedBestSquared) * SdfSign(ScopedBestSide);
}

// Exact distance to an equilateral triangle whose circumradius is InCircumradius.
[[nodiscard]] float EquilateralTriangleDistance(Vector2D InPoint, float InCircumradius) noexcept
{
    if (!(InCircumradius > kTinyLength))
    {
        return Length(InPoint);
    }
    const float ScopedRadius = InCircumradius * kSqrt3OverTwo;
    Vector2D ScopedPoint{std::fabs(InPoint.X) - ScopedRadius, InPoint.Y + ScopedRadius / kSqrt3};
    if (ScopedPoint.X + kSqrt3 * ScopedPoint.Y > 0.0f)
    {
        ScopedPoint = Vector2D{ScopedPoint.X - kSqrt3 * ScopedPoint.Y,
                               -kSqrt3 * ScopedPoint.X - ScopedPoint.Y} *
                      0.5f;
    }
    ScopedPoint.X -= ClampValue(ScopedPoint.X, -2.0f * ScopedRadius, 0.0f);
    return -Length(ScopedPoint) * SdfSign(ScopedPoint.Y);
}

// Exact distance to a trapezoid with the given bottom and top half widths and half height.
[[nodiscard]] float TrapezoidDistance(Vector2D InPoint, float InBottomHalfWidth, float InTopHalfWidth,
                                      float InHalfHeight) noexcept
{
    if (!(InHalfHeight > kTinyLength))
    {
        return Length(InPoint);
    }
    const Vector2D ScopedK1{InTopHalfWidth, InHalfHeight};
    const Vector2D ScopedK2{InTopHalfWidth - InBottomHalfWidth, 2.0f * InHalfHeight};
    const Vector2D ScopedPoint{std::fabs(InPoint.X), InPoint.Y};
    const float ScopedSideHalfWidth = InPoint.Y < 0.0f ? InBottomHalfWidth : InTopHalfWidth;
    const Vector2D ScopedCa{ScopedPoint.X - std::fmin(ScopedPoint.X, ScopedSideHalfWidth),
                            std::fabs(ScopedPoint.Y) - InHalfHeight};
    const Vector2D ScopedCb =
        ScopedPoint - ScopedK1 + ScopedK2 * SegmentParameter(ScopedK1 - ScopedPoint, ScopedK2);
    const float ScopedSign = (ScopedCb.X < 0.0f && ScopedCa.Y < 0.0f) ? -1.0f : 1.0f;
    return ScopedSign * std::sqrt(std::fmin(ScopedCa.LengthSquared(), ScopedCb.LengthSquared()));
}

// Exact distance to a flat topped hexagon whose circumradius is InCircumradius.
[[nodiscard]] float HexagonDistance(Vector2D InPoint, float InCircumradius) noexcept
{
    if (!(InCircumradius > kTinyLength))
    {
        return Length(InPoint);
    }
    const float ScopedRadius = InCircumradius * kSqrt3OverTwo;
    const Vector2D ScopedK{-0.86602540378443864676f, 0.5f};
    const float ScopedClampX = 0.57735026918962576451f * ScopedRadius;
    Vector2D ScopedPoint{std::fabs(InPoint.X), std::fabs(InPoint.Y)};
    const float ScopedDot = ScopedPoint.Dot(ScopedK);
    if (ScopedDot < 0.0f)
    {
        ScopedPoint = ScopedPoint - ScopedK * (2.0f * ScopedDot);
    }
    ScopedPoint =
        ScopedPoint - Vector2D{ClampValue(ScopedPoint.X, -ScopedClampX, ScopedClampX), ScopedRadius};
    return Length(ScopedPoint) * SdfSign(ScopedPoint.Y);
}

// Exact distance to a regular octagon whose circumradius is InCircumradius.
[[nodiscard]] float OctagonDistance(Vector2D InPoint, float InCircumradius) noexcept
{
    if (!(InCircumradius > kTinyLength))
    {
        return Length(InPoint);
    }
    const float ScopedRadius = InCircumradius * kCosPiOverEight;
    const Vector2D ScopedK{-0.92387953251128675612f, 0.38268343236508977173f};
    const float ScopedClampX = 0.41421356237309504880f * ScopedRadius;
    Vector2D ScopedPoint{std::fabs(InPoint.X), std::fabs(InPoint.Y)};
    const Vector2D ScopedFoldA{ScopedK.X, ScopedK.Y};
    const Vector2D ScopedFoldB{-ScopedK.X, ScopedK.Y};
    const float ScopedDotA = ScopedPoint.Dot(ScopedFoldA);
    if (ScopedDotA < 0.0f)
    {
        ScopedPoint = ScopedPoint - ScopedFoldA * (2.0f * ScopedDotA);
    }
    const float ScopedDotB = ScopedPoint.Dot(ScopedFoldB);
    if (ScopedDotB < 0.0f)
    {
        ScopedPoint = ScopedPoint - ScopedFoldB * (2.0f * ScopedDotB);
    }
    ScopedPoint =
        ScopedPoint - Vector2D{ClampValue(ScopedPoint.X, -ScopedClampX, ScopedClampX), ScopedRadius};
    return Length(ScopedPoint) * SdfSign(ScopedPoint.Y);
}

// Exact distance to an n pointed star. The point is folded into one angular sector and the exact
// distance to the two boundary edges of that sector is used. The sign compares the folded point
// against the sector boundary radius, which stays exact at the concave inner vertex.
[[nodiscard]] float StarDistance(Vector2D InPoint, Vector2D InHalfExtents, std::uint32_t InSides) noexcept
{
    const std::uint32_t ScopedSides = InSides < 3 ? 5 : (InSides > 32 ? 32 : InSides);
    const float ScopedOuter = InHalfExtents.Y > 0.0f ? InHalfExtents.Y : InHalfExtents.X;
    if (!(ScopedOuter > kTinyLength))
    {
        return Length(InPoint);
    }
    const float ScopedInner = ClampValue(InHalfExtents.X, 0.0f, ScopedOuter);
    const float ScopedTurn = kTwoPi / static_cast<float>(ScopedSides);
    const float ScopedHalf = ScopedTurn * 0.5f;
    const float ScopedLength = Length(InPoint);
    float ScopedFolded = std::fmod(std::atan2(InPoint.Y, InPoint.X), ScopedTurn);
    if (ScopedFolded < 0.0f)
    {
        ScopedFolded += ScopedTurn;
    }
    const Vector2D ScopedPoint{ScopedLength * std::cos(ScopedFolded), ScopedLength * std::sin(ScopedFolded)};
    const Vector2D ScopedOuter0{ScopedOuter, 0.0f};
    const Vector2D ScopedInnerPoint{ScopedInner * std::cos(ScopedHalf), ScopedInner * std::sin(ScopedHalf)};
    const Vector2D ScopedOuter1{ScopedOuter * std::cos(ScopedTurn), ScopedOuter * std::sin(ScopedTurn)};
    const float ScopedDistance =
        std::fmin(SegmentDistance(ScopedPoint, ScopedOuter0, ScopedInnerPoint),
                  SegmentDistance(ScopedPoint, ScopedInnerPoint, ScopedOuter1));
    const bool ScopedFirstHalf = ScopedFolded <= ScopedHalf;
    const Vector2D ScopedEdge = ScopedFirstHalf ? ScopedInnerPoint - ScopedOuter0
                                                : ScopedOuter1 - ScopedInnerPoint;
    const Vector2D ScopedStart = ScopedFirstHalf ? ScopedOuter0 : ScopedInnerPoint;
    const float ScopedLimit = ScopedStart.Cross(ScopedEdge);
    const float ScopedValue = ScopedPoint.Cross(ScopedEdge);
    const bool ScopedInside = ScopedValue >= 0.0f && ScopedValue < ScopedLimit;
    return ScopedInside ? -ScopedDistance : ScopedDistance;
}

// Exact distance to an isosceles circular sector whose axis points along +Y.
[[nodiscard]] float PieDistance(Vector2D InPoint, float InRadius, float InHalfAperture) noexcept
{
    const float ScopedHalf = ClampValue(InHalfAperture, 0.0f, kHalfPi);
    const Vector2D ScopedEdge{std::sin(ScopedHalf), std::cos(ScopedHalf)};
    const Vector2D ScopedPoint{std::fabs(InPoint.X), InPoint.Y};
    const float ScopedArc = Length(ScopedPoint) - InRadius;
    const float ScopedEdgeDistance =
        Length(ScopedPoint - ScopedEdge * ClampValue(ScopedPoint.Dot(ScopedEdge), 0.0f, InRadius));
    const float ScopedSide = SdfSign(ScopedEdge.Y * ScopedPoint.X - ScopedEdge.X * ScopedPoint.Y);
    return std::fmax(ScopedArc, ScopedEdgeDistance * ScopedSide);
}

// Exact distance to an annulus.
[[nodiscard]] float RingDistance(Vector2D InPoint, float InMidRadius, float InHalfThickness) noexcept
{
    return std::fabs(Length(InPoint) - InMidRadius) - InHalfThickness;
}

// Exact distance to an annular sector whose axis points along +Y.
[[nodiscard]] float ArcDistance(Vector2D InPoint, float InMidRadius, float InHalfThickness,
                                float InHalfAperture) noexcept
{
    const float ScopedHalf = ClampValue(InHalfAperture, 0.0f, kHalfPi);
    const Vector2D ScopedEdge{std::sin(ScopedHalf), std::cos(ScopedHalf)};
    const Vector2D ScopedPoint{std::fabs(InPoint.X), InPoint.Y};
    const bool ScopedOutsideAperture = ScopedEdge.Y * ScopedPoint.X > ScopedEdge.X * ScopedPoint.Y;
    if (ScopedOutsideAperture)
    {
        return Length(ScopedPoint - ScopedEdge * InMidRadius) - InHalfThickness;
    }
    return std::fabs(Length(ScopedPoint) - InMidRadius) - InHalfThickness;
}

// Exact distance to a cross, built as the union of two rounded boxes.
[[nodiscard]] float CrossDistance(Vector2D InPoint, Vector2D InHalfExtents, float InRadius) noexcept
{
    const Vector2D ScopedWide{InHalfExtents.X, InHalfExtents.Y};
    const Vector2D ScopedTall{InHalfExtents.Y, InHalfExtents.X};
    return std::fmin(RoundedBoxDistance(InPoint, ScopedWide, InRadius),
                     RoundedBoxDistance(InPoint, ScopedTall, InRadius));
}

// Exact distance to a crescent, built from one disc minus one offset disc.
[[nodiscard]] float MoonDistance(Vector2D InPoint, float InRadiusOuter, float InOffset, float InCutRadius) noexcept
{
    const float ScopedOffset = InOffset > kTinyLength ? InOffset : kTinyLength;
    const Vector2D ScopedPoint{InPoint.X, std::fabs(InPoint.Y)};
    const float ScopedA = (InRadiusOuter * InRadiusOuter - InCutRadius * InCutRadius +
                           ScopedOffset * ScopedOffset) /
                          (2.0f * ScopedOffset);
    const float ScopedB = std::sqrt(std::fmax(InRadiusOuter * InRadiusOuter - ScopedA * ScopedA, 0.0f));
    const float ScopedCut = ScopedOffset * (ScopedPoint.X * ScopedB - ScopedPoint.Y * ScopedA);
    const float ScopedLimit = ScopedOffset * ScopedOffset * std::fmax(ScopedB - ScopedPoint.Y, 0.0f);
    if (ScopedCut > ScopedLimit)
    {
        return Length(ScopedPoint - Vector2D{ScopedA, ScopedB});
    }
    const float ScopedOuter = Length(ScopedPoint) - InRadiusOuter;
    const float ScopedInner = -(Length(ScopedPoint - Vector2D{ScopedOffset, 0.0f}) - InCutRadius);
    return std::fmax(ScopedOuter, ScopedInner);
}

// Exact distance to a heart, built as the union of two lobes and one triangular body.
[[nodiscard]] float HeartDistance(Vector2D InPoint, float InSize) noexcept
{
    if (!(InSize > kTinyLength))
    {
        return Length(InPoint);
    }
    const float ScopedLobeRadius = 0.5f * InSize;
    const float ScopedLobeOffset = 0.35f * InSize;
    const float ScopedLobeHeight = 0.5f * InSize;
    const float ScopedBase = ScopedLobeOffset + ScopedLobeRadius;
    const float ScopedLeft = Length(InPoint - Vector2D{-ScopedLobeOffset, ScopedLobeHeight}) - ScopedLobeRadius;
    const float ScopedRight = Length(InPoint - Vector2D{ScopedLobeOffset, ScopedLobeHeight}) - ScopedLobeRadius;
    const float ScopedBody = TriangleDistance(InPoint, Vector2D{-ScopedBase, ScopedLobeHeight},
                                              Vector2D{ScopedBase, ScopedLobeHeight}, Vector2D{0.0f, -InSize});
    return std::fmin(std::fmin(ScopedLeft, ScopedRight), ScopedBody);
}

// Exact distance to a parallelogram with the given half extents and horizontal shear.
[[nodiscard]] float ParallelogramDistance(Vector2D InPoint, Vector2D InHalfExtents, float InShear) noexcept
{
    const Vector2D ScopedE{InShear, InHalfExtents.Y};
    const float ScopedWidth = InHalfExtents.X;
    const float ScopedHeight = InHalfExtents.Y;
    const Vector2D ScopedFlipped = InPoint.Y < 0.0f ? Vector2D{-InPoint.X, -InPoint.Y} : InPoint;
    Vector2D ScopedW = ScopedFlipped - ScopedE;
    ScopedW.X -= ClampValue(ScopedW.X, -ScopedWidth, ScopedWidth);
    Vector2D ScopedResult{ScopedW.LengthSquared(), -ScopedW.Y};
    const float ScopedSide = ScopedFlipped.X * ScopedE.Y - ScopedFlipped.Y * ScopedE.X;
    const Vector2D ScopedMirrored =
        ScopedSide < 0.0f ? Vector2D{-ScopedFlipped.X, -ScopedFlipped.Y} : ScopedFlipped;
    Vector2D ScopedV = ScopedMirrored - Vector2D{ScopedWidth, 0.0f};
    const float ScopedELengthSquared = ScopedE.LengthSquared();
    const float ScopedT =
        ScopedELengthSquared > kTinyLength
            ? ClampValue(ScopedV.Dot(ScopedE) / ScopedELengthSquared, -1.0f, 1.0f)
            : 0.0f;
    ScopedV = ScopedV - ScopedE * ScopedT;
    ScopedResult.X = std::fmin(ScopedResult.X, ScopedV.LengthSquared());
    ScopedResult.Y = std::fmin(ScopedResult.Y, ScopedWidth * ScopedHeight - std::fabs(ScopedSide));
    return std::sqrt(std::fmax(ScopedResult.X, 0.0f)) * SdfSign(-ScopedResult.Y);
}

// Circumradius of the regular polygons, taken from the Y half extent.
[[nodiscard]] float RegularRadius(Vector2D InHalfExtents) noexcept
{
    if (InHalfExtents.Y > 0.0f)
    {
        return InHalfExtents.Y;
    }
    return std::fmax(InHalfExtents.X, 0.0f);
}

// Builds one palette default shape with a zero rotation and zero flags.
[[nodiscard]] constexpr SdfShape MakeEntry(SdfShapeType InType, Vector2D InHalfExtents, float InRadius,
                                           float InWidth, std::uint32_t InSides) noexcept
{
    return SdfShape{static_cast<std::uint32_t>(InType), InHalfExtents, InRadius, 0.0f, InWidth, InSides, 0u};
}

constexpr std::array<SdfPrimitiveInfo, kSdfShapeTypeCount> kSdfPrimitiveTable = {
    SdfPrimitiveInfo{SdfShapeType::Circle, "Circle",
                     MakeEntry(SdfShapeType::Circle, Vector2D{16.0f, 16.0f}, 16.0f, 0.0f, 0u)},
    SdfPrimitiveInfo{SdfShapeType::Box, "Box",
                     MakeEntry(SdfShapeType::Box, Vector2D{16.0f, 16.0f}, 0.0f, 0.0f, 0u)},
    SdfPrimitiveInfo{SdfShapeType::RoundedBox, "RoundedBox",
                     MakeEntry(SdfShapeType::RoundedBox, Vector2D{16.0f, 16.0f}, 4.0f, 0.0f, 0u)},
    SdfPrimitiveInfo{SdfShapeType::Capsule, "Capsule",
                     MakeEntry(SdfShapeType::Capsule, Vector2D{16.0f, 0.0f}, 8.0f, 0.0f, 0u)},
    SdfPrimitiveInfo{SdfShapeType::Segment, "Segment",
                     MakeEntry(SdfShapeType::Segment, Vector2D{16.0f, 0.0f}, 0.0f, 0.0f, 0u)},
    SdfPrimitiveInfo{SdfShapeType::Ellipse, "Ellipse",
                     MakeEntry(SdfShapeType::Ellipse, Vector2D{20.0f, 12.0f}, 0.0f, 0.0f, 0u)},
    SdfPrimitiveInfo{SdfShapeType::Rhombus, "Rhombus",
                     MakeEntry(SdfShapeType::Rhombus, Vector2D{18.0f, 12.0f}, 0.0f, 0.0f, 0u)},
    SdfPrimitiveInfo{SdfShapeType::EquilateralTriangle, "EquilateralTriangle",
                     MakeEntry(SdfShapeType::EquilateralTriangle, Vector2D{16.0f, 16.0f}, 0.0f, 0.0f, 0u)},
    SdfPrimitiveInfo{SdfShapeType::IsoscelesTriangle, "IsoscelesTriangle",
                     MakeEntry(SdfShapeType::IsoscelesTriangle, Vector2D{16.0f, 16.0f}, 0.0f, 0.0f, 0u)},
    SdfPrimitiveInfo{SdfShapeType::Trapezoid, "Trapezoid",
                     MakeEntry(SdfShapeType::Trapezoid, Vector2D{16.0f, 12.0f}, 0.0f, 8.0f, 0u)},
    SdfPrimitiveInfo{SdfShapeType::Parallelogram, "Parallelogram",
                     MakeEntry(SdfShapeType::Parallelogram, Vector2D{16.0f, 12.0f}, 0.0f, 6.0f, 0u)},
    SdfPrimitiveInfo{SdfShapeType::Hexagon, "Hexagon",
                     MakeEntry(SdfShapeType::Hexagon, Vector2D{16.0f, 16.0f}, 0.0f, 0.0f, 0u)},
    SdfPrimitiveInfo{SdfShapeType::Octagon, "Octagon",
                     MakeEntry(SdfShapeType::Octagon, Vector2D{16.0f, 16.0f}, 0.0f, 0.0f, 0u)},
    SdfPrimitiveInfo{SdfShapeType::Star, "Star",
                     MakeEntry(SdfShapeType::Star, Vector2D{8.0f, 18.0f}, 0.0f, 0.0f, 5u)},
    SdfPrimitiveInfo{SdfShapeType::Pie, "Pie",
                     MakeEntry(SdfShapeType::Pie, Vector2D{0.0f, 0.0f}, 16.0f, 0.7853982f, 0u)},
    SdfPrimitiveInfo{SdfShapeType::Ring, "Ring",
                     MakeEntry(SdfShapeType::Ring, Vector2D{4.0f, 0.0f}, 16.0f, 0.0f, 0u)},
    SdfPrimitiveInfo{SdfShapeType::Arc, "Arc",
                     MakeEntry(SdfShapeType::Arc, Vector2D{3.0f, 0.0f}, 16.0f, 0.7853982f, 0u)},
    SdfPrimitiveInfo{SdfShapeType::Cross, "Cross",
                     MakeEntry(SdfShapeType::Cross, Vector2D{16.0f, 6.0f}, 0.0f, 0.0f, 0u)},
    SdfPrimitiveInfo{SdfShapeType::Moon, "Moon",
                     MakeEntry(SdfShapeType::Moon, Vector2D{8.0f, 14.0f}, 16.0f, 0.0f, 0u)},
    SdfPrimitiveInfo{SdfShapeType::Heart, "Heart",
                     MakeEntry(SdfShapeType::Heart, Vector2D{0.0f, 0.0f}, 16.0f, 0.0f, 0u)},
};

// The palette is indexed by type, so the table must follow the enumeration order.
[[nodiscard]] constexpr bool PaletteIsOrdered() noexcept
{
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < kSdfShapeTypeCount; ++ScopedIndex)
    {
        if (static_cast<std::uint32_t>(kSdfPrimitiveTable[ScopedIndex].Type) != ScopedIndex)
        {
            return false;
        }
    }
    return true;
}

static_assert(PaletteIsOrdered(), "kSdfPrimitiveTable must follow SdfShapeType order");
}

std::span<const SdfPrimitiveInfo> SdfPrimitivePalette() noexcept
{
    return std::span<const SdfPrimitiveInfo>{kSdfPrimitiveTable};
}

const SdfPrimitiveInfo& DescribeSdfPrimitive(SdfShapeType InType) noexcept
{
    const std::uint32_t ScopedIndex = static_cast<std::uint32_t>(InType);
    if (!IsSdfShapeTypeValid(ScopedIndex))
    {
        return kSdfPrimitiveTable[0];
    }
    return kSdfPrimitiveTable[ScopedIndex];
}

SdfShape MakeSdfShape(SdfShapeType InType) noexcept
{
    return DescribeSdfPrimitive(InType).Default;
}

float SdfDistance(const SdfShape& InShape, Vector2D InPoint) noexcept
{
    const float ScopedCos = std::cos(InShape.Angle);
    const float ScopedSin = std::sin(InShape.Angle);
    const Vector2D ScopedPoint{ScopedCos * InPoint.X + ScopedSin * InPoint.Y,
                               -ScopedSin * InPoint.X + ScopedCos * InPoint.Y};
    const Vector2D ScopedHalfExtents{std::fabs(InShape.HalfExtents.X), std::fabs(InShape.HalfExtents.Y)};
    const float ScopedRadius = std::fabs(InShape.Radius);
    const float ScopedWidth = std::fabs(InShape.Width);
    const Vector2D ScopedStart{-ScopedHalfExtents.X, -ScopedHalfExtents.Y};
    const Vector2D ScopedEnd{ScopedHalfExtents.X, ScopedHalfExtents.Y};
    switch (InShape.GetType())
    {
        case SdfShapeType::Circle:
        {
            return CircleDistance(ScopedPoint, ScopedRadius);
        }
        case SdfShapeType::Box:
        {
            return BoxDistance(ScopedPoint, ScopedHalfExtents);
        }
        case SdfShapeType::RoundedBox:
        {
            return RoundedBoxDistance(ScopedPoint, ScopedHalfExtents, ScopedRadius);
        }
        case SdfShapeType::Capsule:
        {
            return SegmentDistance(ScopedPoint, ScopedStart, ScopedEnd) - ScopedRadius;
        }
        case SdfShapeType::Segment:
        {
            return SegmentDistance(ScopedPoint, ScopedStart, ScopedEnd);
        }
        case SdfShapeType::Ellipse:
        {
            return EllipseDistance(ScopedPoint, ScopedHalfExtents);
        }
        case SdfShapeType::Rhombus:
        {
            return RhombusDistance(ScopedPoint, ScopedHalfExtents);
        }
        case SdfShapeType::EquilateralTriangle:
        {
            return EquilateralTriangleDistance(ScopedPoint, RegularRadius(ScopedHalfExtents));
        }
        case SdfShapeType::IsoscelesTriangle:
        {
            return TriangleDistance(ScopedPoint, Vector2D{0.0f, ScopedHalfExtents.Y},
                                    Vector2D{-ScopedHalfExtents.X, -ScopedHalfExtents.Y},
                                    Vector2D{ScopedHalfExtents.X, -ScopedHalfExtents.Y});
        }
        case SdfShapeType::Trapezoid:
        {
            return TrapezoidDistance(ScopedPoint, ScopedHalfExtents.X, ScopedWidth,
                                     ScopedHalfExtents.Y);
        }
        case SdfShapeType::Parallelogram:
        {
            return ParallelogramDistance(ScopedPoint, ScopedHalfExtents, ScopedWidth);
        }
        case SdfShapeType::Hexagon:
        {
            return HexagonDistance(ScopedPoint, RegularRadius(ScopedHalfExtents));
        }
        case SdfShapeType::Octagon:
        {
            return OctagonDistance(ScopedPoint, RegularRadius(ScopedHalfExtents));
        }
        case SdfShapeType::Star:
        {
            return StarDistance(ScopedPoint, ScopedHalfExtents, InShape.Sides);
        }
        case SdfShapeType::Pie:
        {
            return PieDistance(ScopedPoint, ScopedRadius, ScopedWidth);
        }
        case SdfShapeType::Ring:
        {
            return RingDistance(ScopedPoint, ScopedRadius, ScopedHalfExtents.X);
        }
        case SdfShapeType::Arc:
        {
            return ArcDistance(ScopedPoint, ScopedRadius, ScopedHalfExtents.X, ScopedWidth);
        }
        case SdfShapeType::Cross:
        {
            return CrossDistance(ScopedPoint, ScopedHalfExtents, ScopedRadius);
        }
        case SdfShapeType::Moon:
        {
            return MoonDistance(ScopedPoint, ScopedRadius, ScopedHalfExtents.X, ScopedHalfExtents.Y);
        }
        case SdfShapeType::Heart:
        {
            return HeartDistance(ScopedPoint, ScopedRadius);
        }
        case SdfShapeType::Count:
        default:
        {
            return CircleDistance(ScopedPoint, ScopedRadius);
        }
    }
}

Vector2D SdfGradient(const SdfShape& InShape, Vector2D InPoint) noexcept
{
    const Vector2D ScopedStepX{kSdfGradientEpsilon, 0.0f};
    const Vector2D ScopedStepY{0.0f, kSdfGradientEpsilon};
    const float ScopedLeft = SdfDistance(InShape, InPoint - ScopedStepX);
    const float ScopedRight = SdfDistance(InShape, InPoint + ScopedStepX);
    const float ScopedDown = SdfDistance(InShape, InPoint - ScopedStepY);
    const float ScopedUp = SdfDistance(InShape, InPoint + ScopedStepY);
    const Vector2D ScopedGradient{(ScopedRight - ScopedLeft) / (2.0f * kSdfGradientEpsilon),
                                  (ScopedUp - ScopedDown) / (2.0f * kSdfGradientEpsilon)};
    const float ScopedLength = Length(ScopedGradient);
    if (!(ScopedLength > kTinyLength))
    {
        return Vector2D{0.0f, 0.0f};
    }
    return ScopedGradient * (1.0f / ScopedLength);
}

bool SdfShapeCoversPoint(const SdfShape& InShape, Vector2D InPoint) noexcept
{
    return SdfDistance(InShape, InPoint) < 0.0f;
}
}
