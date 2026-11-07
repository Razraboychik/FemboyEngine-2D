// FemboyEngine2D - level gravity and local attractor/repulsor fields.
#pragma once

#include "Engine/Core/Grid.hpp"
#include "Engine/Core/Mirror.hpp"
#include "Engine/ECS/Schedule.hpp"
#include "Engine/Physics/Physics.hpp"

#include <cstdint>
#include <type_traits>

namespace Fby
{

enum class GravityDirection : std::uint8_t
{
    Down = 0,
    Up,
    Left,
    Right,
    Custom,
    None,
};

enum class GravityFalloff : std::uint8_t
{
    None = 0,
    Linear,
    Quadratic,
    Exponential,
};

enum class GravityBodyMode : std::uint8_t
{
    Attract = 0,
    Repel,
};

enum class GravityBodyShape : std::uint8_t
{
    Circle = 0,
    Box,
};

// Side-view projects fall down; top-down, iso-rect and iso-hex projects have no
// global gravity by default. Both remain fully authorable.
[[nodiscard]] Vector2D DefaultGravityVector(Grid::ProjectionMode InMode) noexcept;
[[nodiscard]] Vector2D GravityVector(GravityDirection InDirection, Vector2D InCustom, float InStrength) noexcept;
[[nodiscard]] const char* GravityDirectionName(GravityDirection InDirection) noexcept;

// One per level entity. The first component in the world wins; it is the
// authored level setting, not a per-body multiplier.
struct LevelGravityComponent
{
    std::uint32_t Mode = static_cast<std::uint32_t>(GravityDirection::Down);
    float Strength = 9.8f;
    Vector2D CustomDirection{0.0f, 1.0f};
    // 0 disables the terminal-speed clamp.
    float MaxFallSpeed = 0.0f;
    bool Enabled = true;
    // False keeps PhysicsDesc gravity untouched and only applies local fields.
    bool ApplyWorldGravity = true;

    [[nodiscard]] GravityDirection Direction() const noexcept
    {
        return static_cast<GravityDirection>(Mode > static_cast<std::uint32_t>(GravityDirection::None)
                                                 ? static_cast<std::uint32_t>(GravityDirection::None)
                                                 : Mode);
    }
};

static_assert(std::is_trivially_copyable_v<LevelGravityComponent>);
static_assert(std::is_standard_layout_v<LevelGravityComponent>);

// Local gravity field attached to any entity. Strength is an acceleration at
// the centre; FalloffDepth is the band from the zone edge to the centre.
struct GravityBodyComponent
{
    std::uint32_t Mode = static_cast<std::uint32_t>(GravityBodyMode::Attract);
    std::uint32_t Shape = static_cast<std::uint32_t>(GravityBodyShape::Circle);
    std::uint32_t Falloff = static_cast<std::uint32_t>(GravityFalloff::Linear);
    float Strength = 10.0f;
    float Radius = 128.0f;
    Vector2D HalfExtents{128.0f, 128.0f};
    Vector2D Offset{};
    // 0 means "edge to centre" over the whole radius/half-extent.
    float FalloffDepth = 0.0f;
    float FalloffPower = 3.0f;
    // 0 disables the clamp.
    float MaxAcceleration = 0.0f;
    // Only bodies whose collider category overlaps this mask are affected.
    std::uint32_t AffectsCategoryBits = 0xFFFFFFFFu;
    bool Enabled = true;

    [[nodiscard]] GravityBodyMode ModeValue() const noexcept
    {
        return static_cast<GravityBodyMode>(Mode > static_cast<std::uint32_t>(GravityBodyMode::Repel)
                                                ? static_cast<std::uint32_t>(GravityBodyMode::Repel)
                                                : Mode);
    }
    [[nodiscard]] GravityBodyShape ShapeValue() const noexcept
    {
        return static_cast<GravityBodyShape>(Shape > static_cast<std::uint32_t>(GravityBodyShape::Box)
                                                 ? static_cast<std::uint32_t>(GravityBodyShape::Box)
                                                 : Shape);
    }
    [[nodiscard]] GravityFalloff FalloffValue() const noexcept
    {
        return static_cast<GravityFalloff>(Falloff > static_cast<std::uint32_t>(GravityFalloff::Exponential)
                                               ? static_cast<std::uint32_t>(GravityFalloff::Exponential)
                                               : Falloff);
    }
};

static_assert(std::is_trivially_copyable_v<GravityBodyComponent>);
static_assert(std::is_standard_layout_v<GravityBodyComponent>);

// FixedUpdate, before PhysicsSystem::Step. Applies the level gravity vector and
// local fields through PhysicsSystem; it never writes Transform2D directly.
class GravitySystem
{
public:
    using Registry = Ecs::Registry;

    void Update(Registry& InRegistry, PhysicsSystem& InPhysics, float InDeltaSeconds) noexcept;
    void Update(Ecs::PhysicsSystemView& InView) noexcept;

    // When true and no LevelGravityComponent exists, a side-view tilemap gets
    // down gravity and top-down/iso tilemaps get zero. A level component always
    // takes precedence.
    bool InferFromProjection = true;

    [[nodiscard]] static bool Register(Ecs::SystemGraph& InGraph, GravitySystem& InSystem);

private:
    Grid::ProjectionMode MLastProjection = Grid::ProjectionMode::Side;
    bool MHasProjection = false;
    Vector2D MAppliedWorldGravity{0.0f, 0.0f};
    bool MWorldGravityApplied = false;
};
}

FBY_REFLECT_COMPONENT(Fby::LevelGravityComponent, "LevelGravityComponent",
                      FBY_FIELD(Fby::LevelGravityComponent, Mode, UInt32),
                      FBY_FIELD(Fby::LevelGravityComponent, Strength, Float),
                      FBY_FIELD(Fby::LevelGravityComponent, CustomDirection, Vector2D),
                      FBY_FIELD(Fby::LevelGravityComponent, MaxFallSpeed, Float),
                      FBY_FIELD(Fby::LevelGravityComponent, Enabled, Bool),
                      FBY_FIELD(Fby::LevelGravityComponent, ApplyWorldGravity, Bool))

FBY_REFLECT_COMPONENT(
    Fby::GravityBodyComponent, "GravityBodyComponent", FBY_FIELD(Fby::GravityBodyComponent, Mode, UInt32),
    FBY_FIELD(Fby::GravityBodyComponent, Shape, UInt32), FBY_FIELD(Fby::GravityBodyComponent, Falloff, UInt32),
    FBY_FIELD(Fby::GravityBodyComponent, Strength, Float), FBY_FIELD(Fby::GravityBodyComponent, Radius, Float),
    FBY_FIELD(Fby::GravityBodyComponent, HalfExtents, Vector2D), FBY_FIELD(Fby::GravityBodyComponent, Offset, Vector2D),
    FBY_FIELD(Fby::GravityBodyComponent, FalloffDepth, Float),
    FBY_FIELD(Fby::GravityBodyComponent, FalloffPower, Float),
    FBY_FIELD(Fby::GravityBodyComponent, MaxAcceleration, Float),
    FBY_FIELD(Fby::GravityBodyComponent, AffectsCategoryBits, UInt32),
    FBY_FIELD(Fby::GravityBodyComponent, Enabled, Bool))
