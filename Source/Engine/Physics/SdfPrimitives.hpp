// FemboyEngine2D - the palette of 2D signed distance primitives.
#pragma once

#include "Engine/Core/Mirror.hpp"
#include "Engine/Core/Types.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

namespace Fby
{

// Every shape family the palette can build, evaluate and describe.
enum class SdfShapeType : std::uint8_t
{
    Circle = 0,
    Box,
    RoundedBox,
    Capsule,
    Segment,
    Ellipse,
    Rhombus,
    EquilateralTriangle,
    IsoscelesTriangle,
    Trapezoid,
    Parallelogram,
    Hexagon,
    Octagon,
    Star,
    Pie,
    Ring,
    Arc,
    Cross,
    Moon,
    Heart,
    Count,
};

// Number of authored shapes. Count itself is only the sentinel.
inline constexpr std::uint32_t kSdfShapeTypeCount = static_cast<std::uint32_t>(SdfShapeType::Count);

// Central difference step used by SdfGradient, in world units.
inline constexpr float kSdfGradientEpsilon = 1.0e-3f;

static_assert(kSdfShapeTypeCount == 20, "the palette must describe 20 shape families");

// True when InType names a real shape family.
[[nodiscard]] constexpr bool IsSdfShapeTypeValid(std::uint32_t InType) noexcept
{
    return InType < kSdfShapeTypeCount;
}

// Authoring data of one primitive. The family picks the members it reads:
// Circle reads Radius.
// Box reads HalfExtents.
// RoundedBox reads HalfExtents and Radius as the corner radius.
// Capsule reads HalfExtents as the half chord and Radius as the thickness.
// Segment reads HalfExtents as the half chord.
// Ellipse reads HalfExtents as the two radii.
// Rhombus reads HalfExtents as the half diagonals.
// EquilateralTriangle reads HalfExtents.Y as the circumradius.
// IsoscelesTriangle reads HalfExtents.X as the half base and HalfExtents.Y as the half height.
// Trapezoid reads HalfExtents.X as the bottom half width, HalfExtents.Y as the half height and Width
//     as the top half width.
// Parallelogram reads HalfExtents as the half extents and Width as the horizontal shear.
// Hexagon and Octagon read HalfExtents.Y as the circumradius.
// Star reads HalfExtents.X as the inner radius, HalfExtents.Y as the outer radius and Sides as the
//     point count, clamped to 3..32.
// Pie reads Radius and Width as the half aperture, clamped to 0..pi/2.
// Ring reads Radius as the mid radius and HalfExtents.X as the half thickness.
// Arc reads Radius as the mid radius, HalfExtents.X as the half thickness and Width as the half
//     aperture, clamped to 0..pi/2. The stroke ends are round caps.
// Cross reads HalfExtents as the two arm half extents and Radius as the corner radius.
// Moon reads Radius as the outer radius, HalfExtents.X as the offset of the cutting circle and
//     HalfExtents.Y as the cutting radius.
// Heart reads Radius as the overall size. The shape is the exact union of two lobes and one
//     triangular body.
// Angle rotates every family around the origin, in radians. Flags is reserved for authoring and
//     render hints and never changes the distance.
struct SdfShape
{
    // SdfShapeType value, kept as UInt32 so Mirror and TOML need no enum codec
    std::uint32_t Type = 0;
    // Primary half size of the family
    Vector2D HalfExtents{};
    // Primary radius, offset or corner rounding of the family
    float Radius = 0.0f;
    // Rotation around the origin in radians
    float Angle = 0.0f;
    // Secondary scalar: aperture, top half width or shear
    float Width = 0.0f;
    // Star point count, ignored by the other families
    std::uint32_t Sides = 0;
    // Reserved authoring and render hint bits
    std::uint32_t Flags = 0;

    [[nodiscard]] constexpr SdfShapeType GetType() const noexcept
    {
        return static_cast<SdfShapeType>(Type);
    }
    constexpr void SetType(SdfShapeType InType) noexcept
    {
        Type = static_cast<std::uint32_t>(InType);
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return IsSdfShapeTypeValid(Type);
    }
};

// The members above need no padding: the real layout is 32 bytes, not the 40 bytes that were
// expected when the palette was specified. The asserted size is the one the compiler produces.
static_assert(sizeof(SdfShape) == 32, "SdfShape must stay 32 bytes");
static_assert(offsetof(SdfShape, HalfExtents) == 4, "SdfShape::HalfExtents must follow Type");
static_assert(offsetof(SdfShape, Radius) == 12, "SdfShape::Radius must follow HalfExtents");
static_assert(offsetof(SdfShape, Angle) == 16, "SdfShape::Angle must follow Radius");
static_assert(offsetof(SdfShape, Width) == 20, "SdfShape::Width must follow Angle");
static_assert(offsetof(SdfShape, Sides) == 24, "SdfShape::Sides must follow Width");
static_assert(offsetof(SdfShape, Flags) == 28, "SdfShape::Flags must follow Sides");
static_assert(std::is_trivially_copyable_v<SdfShape>, "SdfShape must stay trivially copyable");
static_assert(std::is_standard_layout_v<SdfShape>, "SdfShape must stay standard layout");

// ECS wrapper around one authored primitive. The single member keeps the component trivially
// copyable and standard layout so Mirror can describe the wrapped shape fields.
struct SdfShapeComponent
{
    SdfShape Shape;
};

static_assert(sizeof(SdfShapeComponent) == 32, "SdfShapeComponent must stay 32 bytes");
static_assert(std::is_trivially_copyable_v<SdfShapeComponent>, "SdfShapeComponent must stay trivial");
static_assert(std::is_standard_layout_v<SdfShapeComponent>, "SdfShapeComponent must stay standard layout");

// One palette row: the family tag, a stable English label and the shape it spawns.
struct SdfPrimitiveInfo
{
    SdfShapeType Type = SdfShapeType::Circle;
    const char* Label = nullptr;
    SdfShape Default{};
};

static_assert(std::is_trivially_copyable_v<SdfPrimitiveInfo>, "SdfPrimitiveInfo must stay trivial");
// One tag byte, pointer padding and the 32 byte shape: 48 bytes on 64 bit targets.
static_assert(sizeof(SdfPrimitiveInfo) == sizeof(SdfShape) + 2 * sizeof(void*),
              "SdfPrimitiveInfo is a tag, a label pointer and a default shape");

// Every palette row, in SdfShapeType order. The returned view points at static storage.
[[nodiscard]] std::span<const SdfPrimitiveInfo> SdfPrimitivePalette() noexcept;

// Palette row of InType. An out of range type describes a Circle.
[[nodiscard]] const SdfPrimitiveInfo& DescribeSdfPrimitive(SdfShapeType InType) noexcept;

// Default shape of InType, copied from the palette.
[[nodiscard]] SdfShape MakeSdfShape(SdfShapeType InType) noexcept;

// Exact signed distance from InPoint to the shape boundary, negative inside. InPoint is rotated by
// -Angle before the family formula runs. Every family is exact: the ellipse evaluates the closed
// form quartic root of Inigo Quilez instead of a polynomial iteration.
[[nodiscard]] float SdfDistance(const SdfShape& InShape, Vector2D InPoint) noexcept;

// Unit outward gradient of SdfDistance, away from the interior, from central differences. Returns
// zero where the finite difference vanishes.
[[nodiscard]] Vector2D SdfGradient(const SdfShape& InShape, Vector2D InPoint) noexcept;

// True when InPoint lies strictly inside the shape.
[[nodiscard]] bool SdfShapeCoversPoint(const SdfShape& InShape, Vector2D InPoint) noexcept;
}

// Reflected field of the wrapped shape: its offset is the wrapper offset plus the shape offset.
#define FBY_SDF_SHAPE_FIELD(member, kind)                                                                              \
    ::Fby::Reflect::FieldDesc                                                                                          \
    {                                                                                                                  \
        #member, ::Fby::Reflect::FieldKind::kind,                                                                      \
            static_cast<std::uint32_t>(offsetof(::Fby::SdfShapeComponent, Shape) + offsetof(::Fby::SdfShape, member)), \
            static_cast<std::uint32_t>(sizeof(::Fby::SdfShape::member))                                                \
    }

FBY_REFLECT_COMPONENT(Fby::SdfShapeComponent, "SdfShapeComponent", FBY_SDF_SHAPE_FIELD(Type, UInt32),
                      FBY_SDF_SHAPE_FIELD(HalfExtents, Vector2D), FBY_SDF_SHAPE_FIELD(Radius, Float),
                      FBY_SDF_SHAPE_FIELD(Angle, Float), FBY_SDF_SHAPE_FIELD(Width, Float),
                      FBY_SDF_SHAPE_FIELD(Sides, UInt32), FBY_SDF_SHAPE_FIELD(Flags, UInt32))
