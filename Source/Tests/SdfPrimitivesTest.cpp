// FemboyEngine2D - the 2D SDF primitive palette: distances, signs and reflection.
#include "Engine/Physics/SdfPrimitives.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <string_view>
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

// Quarter turn in radians, the palette half aperture of the pie and the arc.
constexpr float kQuarterTurn = 0.7853982f;

// Apothem of a regular polygon whose circumradius is ten.
constexpr float kTenApothem = 10.0f * 0.86602540378f;

// Cosine of one eighth of a turn, used for the octagon apothem.
constexpr float kTenOctagonApothem = 10.0f * 0.92387953251f;

// Scale of the heart test points relative to the palette default size.
constexpr float kHeartSize = 16.0f;

static_assert(sizeof(Fby::SdfShape) == 32, "SdfShape must be 32 bytes");
static_assert(sizeof(Fby::SdfShapeComponent) == 32, "SdfShapeComponent must wrap the 32 byte shape");
static_assert(sizeof(Fby::SdfPrimitiveInfo) == sizeof(Fby::SdfShape) + 2 * sizeof(const char*),
              "SdfPrimitiveInfo is the tag, the label pointer and the shape");
static_assert(offsetof(Fby::SdfShape, Flags) == 28, "SdfShape flags must be the last member");
static_assert(std::is_trivially_copyable_v<Fby::SdfShape>, "SdfShape must be trivially copyable");
static_assert(std::is_standard_layout_v<Fby::SdfShape>, "SdfShape must be standard layout");
static_assert(std::is_trivially_copyable_v<Fby::SdfShapeComponent>, "SdfShapeComponent must be trivial");
static_assert(std::is_standard_layout_v<Fby::SdfShapeComponent>, "SdfShapeComponent must be standard layout");

[[nodiscard]] bool Near(float InValue, float InExpected, float InTolerance)
{
    return std::fabs(InValue - InExpected) <= InTolerance;
}

[[nodiscard]] Fby::SdfShape ShapeOf(Fby::SdfShapeType InType, Fby::Vector2D InHalfExtents, float InRadius,
                                    float InWidth, std::uint32_t InSides)
{
    Fby::SdfShape ScopedShape{};
    ScopedShape.SetType(InType);
    ScopedShape.HalfExtents = InHalfExtents;
    ScopedShape.Radius = InRadius;
    ScopedShape.Width = InWidth;
    ScopedShape.Sides = InSides;
    return ScopedShape;
}
}

int main()
{
    using namespace Fby;

    // Layout of the authored shape.
    FBY_CHECK(kSdfShapeTypeCount == 20);
    SdfShape ScopedEmpty{};
    FBY_CHECK(ScopedEmpty.GetType() == SdfShapeType::Circle);
    FBY_CHECK(ScopedEmpty.IsValid());
    ScopedEmpty.SetType(SdfShapeType::Heart);
    FBY_CHECK(ScopedEmpty.GetType() == SdfShapeType::Heart);
    FBY_CHECK(ScopedEmpty.Type == static_cast<std::uint32_t>(SdfShapeType::Heart));
    SdfShape ScopedInvalid{};
    ScopedInvalid.Type = 200;
    FBY_CHECK(!ScopedInvalid.IsValid());

    // Circle: exact centre, boundary and outside distance, plus the cover predicate.
    const SdfShape ScopedCircle = ShapeOf(SdfShapeType::Circle, Vector2D{16.0f, 16.0f}, 16.0f, 0.0f, 0u);
    FBY_CHECK(Near(SdfDistance(ScopedCircle, Vector2D{0.0f, 0.0f}), -16.0f, 1.0e-4f));
    FBY_CHECK(Near(SdfDistance(ScopedCircle, Vector2D{16.0f, 0.0f}), 0.0f, 1.0e-3f));
    FBY_CHECK(Near(SdfDistance(ScopedCircle, Vector2D{24.0f, 0.0f}), 8.0f, 1.0e-3f));
    FBY_CHECK(Near(SdfDistance(ScopedCircle, Vector2D{0.0f, -24.0f}), 8.0f, 1.0e-3f));
    FBY_CHECK(SdfShapeCoversPoint(ScopedCircle, Vector2D{4.0f, 4.0f}));
    FBY_CHECK(!SdfShapeCoversPoint(ScopedCircle, Vector2D{40.0f, 0.0f}));

    // Circle gradient: unit length and pointing away from the centre.
    const Vector2D ScopedGradientEast = SdfGradient(ScopedCircle, Vector2D{5.0f, 0.0f});
    FBY_CHECK(ScopedGradientEast.X > 0.9f);
    FBY_CHECK(std::fabs(ScopedGradientEast.Y) < 0.1f);
    FBY_CHECK(Near(std::sqrt(ScopedGradientEast.LengthSquared()), 1.0f, 1.0e-3f));
    const Vector2D ScopedGradientSouth = SdfGradient(ScopedCircle, Vector2D{0.0f, -5.0f});
    FBY_CHECK(ScopedGradientSouth.Y < -0.9f);
    FBY_CHECK(std::fabs(ScopedGradientSouth.X) < 0.1f);

    // Box: negative on the short axis, positive along both axes and in the corner.
    const SdfShape ScopedBox = ShapeOf(SdfShapeType::Box, Vector2D{10.0f, 5.0f}, 0.0f, 0.0f, 0u);
    FBY_CHECK(Near(SdfDistance(ScopedBox, Vector2D{0.0f, 0.0f}), -5.0f, 1.0e-4f));
    FBY_CHECK(Near(SdfDistance(ScopedBox, Vector2D{15.0f, 0.0f}), 5.0f, 1.0e-3f));
    FBY_CHECK(Near(SdfDistance(ScopedBox, Vector2D{0.0f, -9.0f}), 4.0f, 1.0e-3f));
    FBY_CHECK(Near(SdfDistance(ScopedBox, Vector2D{-15.0f, 0.0f}), 5.0f, 1.0e-3f));
    FBY_CHECK(Near(SdfDistance(ScopedBox, Vector2D{13.0f, 8.0f}), std::sqrt(18.0f), 1.0e-3f));
    FBY_CHECK(Near(SdfDistance(ScopedBox, Vector2D{10.0f, 5.0f}), 0.0f, 1.0e-3f));

    // Rounded box: flat side unchanged, corner inset by the corner radius.
    const SdfShape ScopedRounded = ShapeOf(SdfShapeType::RoundedBox, Vector2D{10.0f, 5.0f}, 2.0f, 0.0f, 0u);
    FBY_CHECK(Near(SdfDistance(ScopedRounded, Vector2D{15.0f, 0.0f}), 5.0f, 1.0e-3f));
    FBY_CHECK(Near(SdfDistance(ScopedRounded, Vector2D{10.0f, 0.0f}), 0.0f, 1.0e-3f));
    FBY_CHECK(Near(SdfDistance(ScopedRounded, Vector2D{0.0f, 0.0f}), -5.0f, 1.0e-3f));
    const float ScopedRoundedCorner = SdfDistance(ScopedRounded, Vector2D{10.0f, 5.0f});
    FBY_CHECK(ScopedRoundedCorner > 0.5f);
    FBY_CHECK(ScopedRoundedCorner < 1.0f);

    // Capsule and segment: the radius is a constant offset of the segment distance.
    const SdfShape ScopedCapsule = ShapeOf(SdfShapeType::Capsule, Vector2D{10.0f, 0.0f}, 2.0f, 0.0f, 0u);
    FBY_CHECK(Near(SdfDistance(ScopedCapsule, Vector2D{0.0f, 0.0f}), -2.0f, 1.0e-3f));
    FBY_CHECK(Near(SdfDistance(ScopedCapsule, Vector2D{10.0f, 0.0f}), -2.0f, 1.0e-3f));
    FBY_CHECK(Near(SdfDistance(ScopedCapsule, Vector2D{12.0f, 0.0f}), 0.0f, 1.0e-3f));
    FBY_CHECK(Near(SdfDistance(ScopedCapsule, Vector2D{0.0f, 4.0f}), 2.0f, 1.0e-3f));
    FBY_CHECK(Near(SdfDistance(ScopedCapsule, Vector2D{10.0f, 4.0f}), 2.0f, 1.0e-3f));
    const SdfShape ScopedSegment = ShapeOf(SdfShapeType::Segment, Vector2D{10.0f, 0.0f}, 0.0f, 0.0f, 0u);
    FBY_CHECK(Near(SdfDistance(ScopedSegment, Vector2D{0.0f, 0.0f}), 0.0f, 1.0e-4f));
    FBY_CHECK(Near(SdfDistance(ScopedSegment, Vector2D{10.0f, 0.0f}), 0.0f, 1.0e-4f));
    FBY_CHECK(Near(SdfDistance(ScopedSegment, Vector2D{14.0f, 0.0f}), 4.0f, 1.0e-3f));
    FBY_CHECK(Near(SdfDistance(ScopedSegment, Vector2D{0.0f, 3.0f}), 3.0f, 1.0e-3f));
    FBY_CHECK(Near(SdfDistance(ScopedSegment, Vector2D{-14.0f, 0.0f}), 4.0f, 1.0e-3f));
    const SdfShape ScopedVerticalSegment =
        ShapeOf(SdfShapeType::Segment, Vector2D{0.0f, 10.0f}, 0.0f, 0.0f, 0u);
    FBY_CHECK(Near(SdfDistance(ScopedVerticalSegment, Vector2D{0.0f, 12.0f}), 2.0f, 1.0e-3f));
    FBY_CHECK(Near(SdfDistance(ScopedVerticalSegment, Vector2D{3.0f, 0.0f}), 3.0f, 1.0e-3f));

    // Ellipse: zero on both axes, correct sign and distance on the axes.
    const SdfShape ScopedEllipse = ShapeOf(SdfShapeType::Ellipse, Vector2D{20.0f, 10.0f}, 0.0f, 0.0f, 0u);
    FBY_CHECK(Near(SdfDistance(ScopedEllipse, Vector2D{20.0f, 0.0f}), 0.0f, 1.0e-3f));
    FBY_CHECK(Near(SdfDistance(ScopedEllipse, Vector2D{0.0f, 10.0f}), 0.0f, 1.0e-3f));
    FBY_CHECK(Near(SdfDistance(ScopedEllipse, Vector2D{30.0f, 0.0f}), 10.0f, 1.0e-3f));
    FBY_CHECK(Near(SdfDistance(ScopedEllipse, Vector2D{0.0f, 20.0f}), 10.0f, 1.0e-3f));
    FBY_CHECK(Near(SdfDistance(ScopedEllipse, Vector2D{0.0f, 0.0f}), -10.0f, 1.0e-3f));
    FBY_CHECK(SdfDistance(ScopedEllipse, Vector2D{10.0f, 0.0f}) < 0.0f);
    FBY_CHECK(SdfDistance(ScopedEllipse, Vector2D{40.0f, 0.0f}) > 0.0f);

    // Rhombus: the vertices and the centre of the two half diagonals.
    const SdfShape ScopedRhombus = ShapeOf(SdfShapeType::Rhombus, Vector2D{20.0f, 10.0f}, 0.0f, 0.0f, 0u);
    FBY_CHECK(Near(SdfDistance(ScopedRhombus, Vector2D{20.0f, 0.0f}), 0.0f, 1.0e-3f));
    FBY_CHECK(Near(SdfDistance(ScopedRhombus, Vector2D{0.0f, 10.0f}), 0.0f, 1.0e-3f));
    FBY_CHECK(Near(SdfDistance(ScopedRhombus, Vector2D{0.0f, 0.0f}), -std::sqrt(80.0f), 1.0e-3f));
    FBY_CHECK(Near(SdfDistance(ScopedRhombus, Vector2D{0.0f, 12.0f}), 2.0f, 1.0e-3f));
    FBY_CHECK(SdfDistance(ScopedRhombus, Vector2D{25.0f, 0.0f}) > 0.0f);

    // Equilateral triangle: apex and base vertices of the circumradius.
    const SdfShape ScopedEquilateral =
        ShapeOf(SdfShapeType::EquilateralTriangle, Vector2D{16.0f, 16.0f}, 0.0f, 0.0f, 0u);
    FBY_CHECK(Near(SdfDistance(ScopedEquilateral, Vector2D{0.0f, 16.0f}), 0.0f, 1.0e-3f));
    FBY_CHECK(Near(SdfDistance(ScopedEquilateral, Vector2D{13.8564065f, -8.0f}), 0.0f, 1.0e-3f));
    FBY_CHECK(SdfDistance(ScopedEquilateral, Vector2D{0.0f, 0.0f}) < 0.0f);
    FBY_CHECK(SdfDistance(ScopedEquilateral, Vector2D{0.0f, 20.0f}) > 0.0f);

    // Isosceles triangle: apex and both base corners.
    const SdfShape ScopedIsosceles =
        ShapeOf(SdfShapeType::IsoscelesTriangle, Vector2D{16.0f, 12.0f}, 0.0f, 0.0f, 0u);
    FBY_CHECK(Near(SdfDistance(ScopedIsosceles, Vector2D{0.0f, 12.0f}), 0.0f, 1.0e-3f));
    FBY_CHECK(Near(SdfDistance(ScopedIsosceles, Vector2D{16.0f, -12.0f}), 0.0f, 1.0e-3f));
    FBY_CHECK(Near(SdfDistance(ScopedIsosceles, Vector2D{-16.0f, -12.0f}), 0.0f, 1.0e-3f));
    FBY_CHECK(SdfDistance(ScopedIsosceles, Vector2D{0.0f, 0.0f}) < 0.0f);
    FBY_CHECK(SdfDistance(ScopedIsosceles, Vector2D{0.0f, 20.0f}) > 0.0f);

    // Trapezoid: bottom corners, top corners and the interior.
    const SdfShape ScopedTrapezoid =
        ShapeOf(SdfShapeType::Trapezoid, Vector2D{16.0f, 12.0f}, 0.0f, 8.0f, 0u);
    FBY_CHECK(Near(SdfDistance(ScopedTrapezoid, Vector2D{16.0f, -12.0f}), 0.0f, 1.0e-3f));
    FBY_CHECK(Near(SdfDistance(ScopedTrapezoid, Vector2D{8.0f, 12.0f}), 0.0f, 1.0e-3f));
    FBY_CHECK(Near(SdfDistance(ScopedTrapezoid, Vector2D{-8.0f, 12.0f}), 0.0f, 1.0e-3f));
    FBY_CHECK(Near(SdfDistance(ScopedTrapezoid, Vector2D{0.0f, 0.0f}), -288.0f / std::sqrt(640.0f), 1.0e-3f));
    FBY_CHECK(SdfDistance(ScopedTrapezoid, Vector2D{0.0f, 20.0f}) > 0.0f);

    // Parallelogram: the sheared corners and the flat centre distance.
    const SdfShape ScopedParallelogram =
        ShapeOf(SdfShapeType::Parallelogram, Vector2D{16.0f, 12.0f}, 0.0f, 6.0f, 0u);
    FBY_CHECK(Near(SdfDistance(ScopedParallelogram, Vector2D{22.0f, 12.0f}), 0.0f, 1.0e-3f));
    FBY_CHECK(Near(SdfDistance(ScopedParallelogram, Vector2D{-22.0f, -12.0f}), 0.0f, 1.0e-3f));
    FBY_CHECK(Near(SdfDistance(ScopedParallelogram, Vector2D{0.0f, 0.0f}), -12.0f, 1.0e-3f));
    FBY_CHECK(SdfDistance(ScopedParallelogram, Vector2D{0.0f, 30.0f}) > 0.0f);

    // Hexagon: one vertex on the X axis and the flat side above the centre.
    const SdfShape ScopedHexagon = ShapeOf(SdfShapeType::Hexagon, Vector2D{10.0f, 10.0f}, 0.0f, 0.0f, 0u);
    FBY_CHECK(Near(SdfDistance(ScopedHexagon, Vector2D{10.0f, 0.0f}), 0.0f, 1.0e-3f));
    FBY_CHECK(Near(SdfDistance(ScopedHexagon, Vector2D{0.0f, kTenApothem}), 0.0f, 1.0e-3f));
    FBY_CHECK(Near(SdfDistance(ScopedHexagon, Vector2D{0.0f, 0.0f}), -kTenApothem, 1.0e-3f));
    FBY_CHECK(SdfDistance(ScopedHexagon, Vector2D{20.0f, 20.0f}) > 0.0f);

    // Octagon: the flat right side sits at the apothem of the circumradius.
    const SdfShape ScopedOctagon = ShapeOf(SdfShapeType::Octagon, Vector2D{10.0f, 10.0f}, 0.0f, 0.0f, 0u);
    FBY_CHECK(Near(SdfDistance(ScopedOctagon, Vector2D{kTenOctagonApothem, 0.0f}), 0.0f, 1.0e-3f));
    FBY_CHECK(Near(SdfDistance(ScopedOctagon, Vector2D{0.0f, kTenOctagonApothem}), 0.0f, 1.0e-3f));
    FBY_CHECK(Near(SdfDistance(ScopedOctagon, Vector2D{0.0f, 0.0f}), -kTenOctagonApothem, 1.0e-3f));
    FBY_CHECK(SdfDistance(ScopedOctagon, Vector2D{10.0f, 0.0f}) > 0.5f);
    FBY_CHECK(SdfDistance(ScopedOctagon, Vector2D{20.0f, 20.0f}) > 0.0f);

    // Star: outer tips on the X axis, inner vertices on the turned sector seams.
    const SdfShape ScopedStar = ShapeOf(SdfShapeType::Star, Vector2D{8.0f, 18.0f}, 0.0f, 0.0f, 5u);
    const float ScopedStarTurn = 6.2831853f / 5.0f;
    const Vector2D ScopedStarSecondTip{18.0f * std::cos(ScopedStarTurn), 18.0f * std::sin(ScopedStarTurn)};
    FBY_CHECK(Near(SdfDistance(ScopedStar, Vector2D{18.0f, 0.0f}), 0.0f, 1.0e-3f));
    FBY_CHECK(Near(SdfDistance(ScopedStar, ScopedStarSecondTip), 0.0f, 1.0e-3f));
    FBY_CHECK(Near(SdfDistance(ScopedStar, Vector2D{-8.0f, 0.0f}), 0.0f, 1.0e-3f));
    FBY_CHECK(Near(SdfDistance(ScopedStar, Vector2D{0.0f, 0.0f}), -8.0f, 1.0e-3f));
    FBY_CHECK(Near(SdfDistance(ScopedStar, Vector2D{23.0f, 0.0f}), 5.0f, 1.0e-3f));
    FBY_CHECK(SdfDistance(ScopedStar, Vector2D{0.0f, 8.0f}) < 0.0f);
    FBY_CHECK(SdfDistance(ScopedStar, Vector2D{0.0f, 20.0f}) > 0.0f);
    FBY_CHECK(SdfDistance(ScopedStar, Vector2D{-18.0f, 0.0f}) > 0.0f);
    const Vector2D ScopedStarRotated{-8.0f * std::sin(ScopedStarTurn), 8.0f * std::cos(ScopedStarTurn)};
    FBY_CHECK(Near(SdfDistance(ScopedStar, Vector2D{0.0f, 8.0f}),
                   SdfDistance(ScopedStar, ScopedStarRotated), 1.0e-3f));

    // Pie: the apex is on the boundary, the axis inside and the back outside.
    const SdfShape ScopedPie = ShapeOf(SdfShapeType::Pie, Vector2D{0.0f, 0.0f}, 16.0f, kQuarterTurn, 0u);
    FBY_CHECK(Near(SdfDistance(ScopedPie, Vector2D{0.0f, 0.0f}), 0.0f, 1.0e-3f));
    FBY_CHECK(SdfDistance(ScopedPie, Vector2D{0.0f, 8.0f}) < 0.0f);
    FBY_CHECK(Near(SdfDistance(ScopedPie, Vector2D{8.0f, 8.0f}), 0.0f, 1.0e-3f));
    FBY_CHECK(Near(SdfDistance(ScopedPie, Vector2D{0.0f, 20.0f}), 4.0f, 1.0e-3f));
    FBY_CHECK(SdfDistance(ScopedPie, Vector2D{0.0f, -8.0f}) > 0.0f);
    FBY_CHECK(SdfDistance(ScopedPie, Vector2D{14.0f, 4.0f}) > 0.0f);

    // Ring: the centre is a hole, the mid radius is inside and both rims are on the boundary.
    const SdfShape ScopedRing = ShapeOf(SdfShapeType::Ring, Vector2D{4.0f, 0.0f}, 16.0f, 0.0f, 0u);
    FBY_CHECK(Near(SdfDistance(ScopedRing, Vector2D{0.0f, 0.0f}), 12.0f, 1.0e-3f));
    FBY_CHECK(Near(SdfDistance(ScopedRing, Vector2D{16.0f, 0.0f}), -4.0f, 1.0e-3f));
    FBY_CHECK(Near(SdfDistance(ScopedRing, Vector2D{12.0f, 0.0f}), 0.0f, 1.0e-3f));
    FBY_CHECK(Near(SdfDistance(ScopedRing, Vector2D{20.0f, 0.0f}), 0.0f, 1.0e-3f));
    FBY_CHECK(Near(SdfDistance(ScopedRing, Vector2D{24.0f, 0.0f}), 4.0f, 1.0e-3f));

    // Arc: the band is solid, the aperture ends and the centre are outside.
    const SdfShape ScopedArc =
        ShapeOf(SdfShapeType::Arc, Vector2D{3.0f, 0.0f}, 16.0f, kQuarterTurn, 0u);
    FBY_CHECK(Near(SdfDistance(ScopedArc, Vector2D{0.0f, 16.0f}), -3.0f, 1.0e-3f));
    FBY_CHECK(Near(SdfDistance(ScopedArc, Vector2D{0.0f, 13.0f}), 0.0f, 1.0e-3f));
    FBY_CHECK(Near(SdfDistance(ScopedArc, Vector2D{0.0f, 19.0f}), 0.0f, 1.0e-3f));
    FBY_CHECK(Near(SdfDistance(ScopedArc, Vector2D{0.0f, 0.0f}), 13.0f, 1.0e-3f));
    FBY_CHECK(SdfDistance(ScopedArc, Vector2D{16.0f, 0.0f}) > 0.0f);

    // Cross: the centre, arm centres, arm ends and the outer corner.
    const SdfShape ScopedCross = ShapeOf(SdfShapeType::Cross, Vector2D{16.0f, 6.0f}, 0.0f, 0.0f, 0u);
    FBY_CHECK(Near(SdfDistance(ScopedCross, Vector2D{0.0f, 0.0f}), -6.0f, 1.0e-3f));
    FBY_CHECK(Near(SdfDistance(ScopedCross, Vector2D{10.0f, 0.0f}), -6.0f, 1.0e-3f));
    FBY_CHECK(Near(SdfDistance(ScopedCross, Vector2D{0.0f, 16.0f}), 0.0f, 1.0e-3f));
    FBY_CHECK(Near(SdfDistance(ScopedCross, Vector2D{0.0f, 20.0f}), 4.0f, 1.0e-3f));
    FBY_CHECK(Near(SdfDistance(ScopedCross, Vector2D{20.0f, 0.0f}), 4.0f, 1.0e-3f));
    FBY_CHECK(Near(SdfDistance(ScopedCross, Vector2D{10.0f, 6.0f}), 0.0f, 1.0e-3f));
    FBY_CHECK(Near(SdfDistance(ScopedCross, Vector2D{10.0f, 10.0f}), 4.0f, 1.0e-3f));

    // Moon: the crescent is solid on the left, the bite is a hole and the rims are exact.
    const SdfShape ScopedMoon = ShapeOf(SdfShapeType::Moon, Vector2D{8.0f, 14.0f}, 16.0f, 0.0f, 0u);
    FBY_CHECK(Near(SdfDistance(ScopedMoon, Vector2D{0.0f, 16.0f}), 0.0f, 1.0e-3f));
    FBY_CHECK(Near(SdfDistance(ScopedMoon, Vector2D{-16.0f, 0.0f}), 0.0f, 1.0e-3f));
    FBY_CHECK(SdfDistance(ScopedMoon, Vector2D{-12.0f, 0.0f}) < 0.0f);
    FBY_CHECK(SdfDistance(ScopedMoon, Vector2D{0.0f, 12.0f}) < 0.0f);
    FBY_CHECK(SdfDistance(ScopedMoon, Vector2D{0.0f, 0.0f}) > 0.0f);
    FBY_CHECK(Near(SdfDistance(ScopedMoon, Vector2D{0.0f, 20.0f}), 4.0f, 1.0e-3f));

    // Heart: the tip, the lobes, the body and the outside.
    const SdfShape ScopedHeart = ShapeOf(SdfShapeType::Heart, Vector2D{0.0f, 0.0f}, kHeartSize, 0.0f, 0u);
    FBY_CHECK(Near(SdfDistance(ScopedHeart, Vector2D{0.0f, -kHeartSize}), 0.0f, 1.0e-3f));
    FBY_CHECK(SdfDistance(ScopedHeart, Vector2D{0.0f, 0.0f}) < 0.0f);
    FBY_CHECK(SdfDistance(ScopedHeart, Vector2D{5.6f, 8.0f}) < 0.0f);
    FBY_CHECK(SdfDistance(ScopedHeart, Vector2D{-5.6f, 8.0f}) < 0.0f);
    FBY_CHECK(SdfDistance(ScopedHeart, Vector2D{0.0f, 16.0f}) > 0.0f);
    FBY_CHECK(SdfDistance(ScopedHeart, Vector2D{0.0f, -24.0f}) > 0.0f);

    // Angle rotates the primitive around its origin.
    SdfShape ScopedRotatedBox = ShapeOf(SdfShapeType::Box, Vector2D{10.0f, 2.0f}, 0.0f, 0.0f, 0u);
    ScopedRotatedBox.Angle = 1.57079632679f;
    FBY_CHECK(Near(SdfDistance(ScopedRotatedBox, Vector2D{0.0f, 5.0f}), -2.0f, 1.0e-3f));
    FBY_CHECK(Near(SdfDistance(ScopedRotatedBox, Vector2D{5.0f, 0.0f}), 3.0f, 1.0e-3f));
    SdfShape ScopedRotatedCircle = ScopedCircle;
    ScopedRotatedCircle.Angle = 0.75f;
    FBY_CHECK(Near(SdfDistance(ScopedRotatedCircle, Vector2D{10.0f, 0.0f}), -6.0f, 1.0e-3f));

    // Palette: one row per shape, ordered, uniquely labelled and non null.
    const std::span<const SdfPrimitiveInfo> ScopedPalette = SdfPrimitivePalette();
    FBY_CHECK(ScopedPalette.size() == kSdfShapeTypeCount);
    FBY_CHECK(ScopedPalette.size() == 20);
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < kSdfShapeTypeCount; ++ScopedIndex)
    {
        const SdfPrimitiveInfo& ScopedInfo = ScopedPalette[ScopedIndex];
        FBY_CHECK(static_cast<std::uint32_t>(ScopedInfo.Type) == ScopedIndex);
        FBY_CHECK(ScopedInfo.Label != nullptr);
        FBY_CHECK(ScopedInfo.Label != nullptr && ScopedInfo.Label[0] != '\0');
        FBY_CHECK(ScopedInfo.Default.IsValid());
        FBY_CHECK(ScopedInfo.Default.GetType() == ScopedInfo.Type);
        for (std::uint32_t ScopedOther = ScopedIndex + 1; ScopedOther < kSdfShapeTypeCount; ++ScopedOther)
        {
            FBY_CHECK(ScopedInfo.Label != nullptr && ScopedPalette[ScopedOther].Label != nullptr);
            FBY_CHECK(std::strcmp(ScopedInfo.Label, ScopedPalette[ScopedOther].Label) != 0);
        }
    }
    FBY_CHECK(DescribeSdfPrimitive(SdfShapeType::Star).Type == SdfShapeType::Star);
    FBY_CHECK(DescribeSdfPrimitive(static_cast<SdfShapeType>(200)).Type == SdfShapeType::Circle);
    FBY_CHECK(MakeSdfShape(SdfShapeType::Heart).GetType() == SdfShapeType::Heart);
    FBY_CHECK(Near(SdfDistance(MakeSdfShape(SdfShapeType::Circle), Vector2D{0.0f, 0.0f}), -16.0f, 1.0e-3f));

    // Every palette default, distance and gradient, is finite at representative probes.
    const std::array<Vector2D, 4> ScopedProbes{Vector2D{0.0f, 0.0f}, Vector2D{3.5f, -2.5f},
                                               Vector2D{1000.0f, 1000.0f}, Vector2D{-1000.0f, 250.0f}};
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < kSdfShapeTypeCount; ++ScopedIndex)
    {
        const SdfShape ScopedShape = DescribeSdfPrimitive(static_cast<SdfShapeType>(ScopedIndex)).Default;
        for (const Vector2D& ScopedProbe : ScopedProbes)
        {
            const float ScopedDistance = SdfDistance(ScopedShape, ScopedProbe);
            const Vector2D ScopedGradient = SdfGradient(ScopedShape, ScopedProbe);
            FBY_CHECK(std::isfinite(ScopedDistance));
            FBY_CHECK(std::isfinite(ScopedGradient.X));
            FBY_CHECK(std::isfinite(ScopedGradient.Y));
            FBY_CHECK(std::fabs(std::sqrt(ScopedGradient.LengthSquared()) - 1.0f) < 1.0e-3f ||
                      ScopedGradient.LengthSquared() == 0.0f);
        }
    }

    // Mirror reflection of the component, including the wrapped shape offsets.
    FBY_CHECK(Reflect::IsRegistered<SdfShapeComponent>());
    const std::span<const Reflect::FieldDesc> ScopedFields = Reflect::Fields<SdfShapeComponent>();
    FBY_CHECK(ScopedFields.size() == 7);
    const std::array<std::string_view, 7> ScopedNames{"Type", "HalfExtents", "Radius", "Angle",
                                                      "Width", "Sides", "Flags"};
    const std::array<std::uint32_t, 7> ScopedOffsets{0u, 4u, 12u, 16u, 20u, 24u, 28u};
    const std::array<Reflect::FieldKind, 7> ScopedKinds{Reflect::FieldKind::UInt32, Reflect::FieldKind::Vector2D,
                                                        Reflect::FieldKind::Float, Reflect::FieldKind::Float,
                                                        Reflect::FieldKind::Float, Reflect::FieldKind::UInt32,
                                                        Reflect::FieldKind::UInt32};
    const std::array<std::uint32_t, 7> ScopedSizes{4u, 8u, 4u, 4u, 4u, 4u, 4u};
    for (std::size_t ScopedIndex = 0; ScopedIndex < ScopedNames.size(); ++ScopedIndex)
    {
        FBY_CHECK(ScopedFields[ScopedIndex].name == ScopedNames[ScopedIndex]);
        FBY_CHECK(ScopedFields[ScopedIndex].offset == ScopedOffsets[ScopedIndex]);
        FBY_CHECK(ScopedFields[ScopedIndex].kind == ScopedKinds[ScopedIndex]);
        FBY_CHECK(ScopedFields[ScopedIndex].size == ScopedSizes[ScopedIndex]);
    }
    constexpr auto ScopedDescription = Reflect::Describe<SdfShapeComponent>();
    FBY_CHECK(ScopedDescription.size == sizeof(SdfShapeComponent));
    FBY_CHECK(ScopedDescription.name == std::string_view{"SdfShapeComponent"});
    FBY_CHECK(ScopedDescription.fields.size() == 7);
    SdfShapeComponent ScopedComponent{};
    ScopedComponent.Shape = ScopedCircle;
    FBY_CHECK(Near(SdfDistance(ScopedComponent.Shape, Vector2D{24.0f, 0.0f}), 8.0f, 1.0e-3f));

    if (GFailures == 0)
    {
        std::printf("sdf_primitives_test: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "sdf_primitives_test: %u failure(s)\n", GFailures);
    return 1;
}
