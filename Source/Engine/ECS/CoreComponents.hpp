// FemboyEngine2D - core ECS components shared by every engine module.
#pragma once

#include "Engine/Core/Definitions.hpp"
#include "Engine/Core/Mirror.hpp"
#include "Engine/Core/Types.hpp"
#include "Engine/ECS/Entity.hpp"

#include <cstdint>
#include <type_traits>

namespace Fby
{

// The transform the renderer and the game both use. Rotation is radians,
struct Transform2D
{
    // World units (pixels at zoom 1)
    Vector2D Position{};
    Vector2D Scale{1.0f, 1.0f};
    float Rotation = 0.0f;
};

// Opt-in hierarchy: an entity with LocalTransform2D writes its world transform
// into Transform2D each time UpdateHierarchy runs. Without Parent2D the local
// transform is the world transform.
struct LocalTransform2D
{
    // Relative to the parent's world transform
    Vector2D Position{};
    Vector2D Scale{1.0f, 1.0f};
    float Rotation = 0.0f;
};

struct Parent2D
{
    Entity Parent = Fby::kNullEntity;
};

// Linear motion, integrated by the movement system on the fixed step.
struct Velocity
{
    Vector2D Value{};
};

// Everything needed to emit one RenderItem.
struct Sprite2D
{
    // World-space extent before scale
    Vector2D Size{32.0f, 32.0f};
    // Normalized source rect in the layer
    Vector2D UVMin{0.0f, 0.0f};
    Vector2D UVMax{1.0f, 1.0f};
    // Texture-array slice
    std::uint32_t Layer = 0;
    Color32 Color = Color32::White();
    // Rhi::RenderFlags
    std::uint32_t Flags = 0;
};

// A second sprite layer over the base Sprite2D. A directional flipbook
// crossfade puts the outgoing frame in Sprite2D and the incoming frame here
// with Weight as its alpha, so both cells are drawn from one entity.
struct SpriteOverlay2D
{
    Vector2D UVMin{0.0f, 0.0f};
    Vector2D UVMax{1.0f, 1.0f};
    std::uint32_t Layer = 0;
    Color32 Color = Color32::White();
    // Rhi::RenderFlags
    std::uint32_t Flags = 0;
    // Multiplies the authored alpha; zero skips the layer
    float Weight = 1.0f;
    // Keeps the overlay behind the base sprite at equal depth keys
    float DepthBias = 0.0001f;
};

// Sorting bucket. A scene sets this once; the extract copies it into the key.
struct DrawOrder
{
    // Coarse bucket, see Rhi::MakeRenderKey
    std::uint16_t Layer = 0;
    // Fractional depth inside the bucket
    float Depth = 0.0f;
    // Engine material bucket, see Rhi::MakeMaterialRenderKey. 0 keeps the quad
    // pipeline; any other value indexes the extract's material pipeline table.
    std::uint32_t MaterialBucket = 0;
};

struct Camera2D
{
    Vector2D Center{};
    Vector2D ViewSize{1920.0f, 1080.0f};
    float Zoom = 1.0f;
    float Rotation = 0.0f;
};

struct Lifetime
{
    float SecondsRemaining = 0.0f;
};

// Marker: this entity is picked by the editor selection system.
struct EditorSelected
{
};

// Marker: keep the entity out of the render extract (still simulated).
struct Hidden
{
};

// Authoring name shown in the hierarchy and the inspector. Fixed-size and flat
// like every other component, so snapshots and scene serialization stay
// allocation-free.
struct NameComponent
{
    char Value[64] = {};
};

static_assert(std::is_trivially_copyable_v<NameComponent>);
static_assert(std::is_standard_layout_v<NameComponent>);
static_assert(sizeof(NameComponent) == 64);

static_assert(std::is_trivially_copyable_v<Transform2D>);
static_assert(std::is_standard_layout_v<Transform2D>);
static_assert(sizeof(Transform2D) == 20, "Transform2D is Vec2 + Vec2 + float, tightly packed");

static_assert(std::is_trivially_copyable_v<Sprite2D>);
static_assert(std::is_standard_layout_v<Sprite2D>);
static_assert(std::is_trivially_copyable_v<SpriteOverlay2D>);
static_assert(std::is_standard_layout_v<SpriteOverlay2D>);
static_assert(std::is_trivially_copyable_v<Velocity>);
static_assert(std::is_trivially_copyable_v<Camera2D>);
}

// Mirror registration
FBY_REFLECT_COMPONENT(Fby::Transform2D, "Transform2D", FBY_FIELD(Fby::Transform2D, Position, Vector2D),
                      FBY_FIELD(Fby::Transform2D, Scale, Vector2D), FBY_FIELD(Fby::Transform2D, Rotation, Float))

FBY_REFLECT_COMPONENT(Fby::LocalTransform2D, "LocalTransform2D",
                      FBY_FIELD(Fby::LocalTransform2D, Position, Vector2D),
                      FBY_FIELD(Fby::LocalTransform2D, Scale, Vector2D),
                      FBY_FIELD(Fby::LocalTransform2D, Rotation, Float))

FBY_REFLECT_COMPONENT(Fby::Parent2D, "Parent2D", FBY_FIELD(Fby::Parent2D, Parent, Entity))

FBY_REFLECT_COMPONENT(Fby::Velocity, "Velocity", FBY_FIELD(Fby::Velocity, Value, Vector2D))

FBY_REFLECT_COMPONENT(Fby::Sprite2D, "Sprite2D", FBY_FIELD(Fby::Sprite2D, Size, Vector2D),
                      FBY_FIELD(Fby::Sprite2D, UVMin, Vector2D), FBY_FIELD(Fby::Sprite2D, UVMax, Vector2D),
                      FBY_FIELD(Fby::Sprite2D, Layer, UInt32), FBY_FIELD(Fby::Sprite2D, Color, Color32),
                      FBY_FIELD(Fby::Sprite2D, Flags, UInt32))

FBY_REFLECT_COMPONENT(Fby::SpriteOverlay2D, "SpriteOverlay2D", FBY_FIELD(Fby::SpriteOverlay2D, UVMin, Vector2D),
                      FBY_FIELD(Fby::SpriteOverlay2D, UVMax, Vector2D), FBY_FIELD(Fby::SpriteOverlay2D, Layer, UInt32),
                      FBY_FIELD(Fby::SpriteOverlay2D, Color, Color32), FBY_FIELD(Fby::SpriteOverlay2D, Flags, UInt32),
                      FBY_FIELD(Fby::SpriteOverlay2D, Weight, Float), FBY_FIELD(Fby::SpriteOverlay2D, DepthBias, Float))

FBY_REFLECT_COMPONENT(Fby::DrawOrder, "DrawOrder", FBY_FIELD(Fby::DrawOrder, Layer, UInt32),
                      FBY_FIELD(Fby::DrawOrder, Depth, Float), FBY_FIELD(Fby::DrawOrder, MaterialBucket, UInt32))

FBY_REFLECT_COMPONENT(Fby::Camera2D, "Camera2D", FBY_FIELD(Fby::Camera2D, Center, Vector2D),
                      FBY_FIELD(Fby::Camera2D, ViewSize, Vector2D), FBY_FIELD(Fby::Camera2D, Zoom, Float),
                      FBY_FIELD(Fby::Camera2D, Rotation, Float))

FBY_REFLECT_COMPONENT(Fby::Lifetime, "Lifetime", FBY_FIELD(Fby::Lifetime, SecondsRemaining, Float))

FBY_REFLECT_COMPONENT(Fby::NameComponent, "NameComponent", FBY_FIELD(Fby::NameComponent, Value, String))

FBY_REFLECT_COMPONENT_EMPTY(Fby::Hidden, "Hidden")
FBY_REFLECT_COMPONENT_EMPTY(Fby::EditorSelected, "EditorSelected")
