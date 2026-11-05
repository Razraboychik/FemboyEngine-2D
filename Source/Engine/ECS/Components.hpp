// FemboyEngine2D - ECS components.
#pragma once

#include "Engine/Core/BigInt.hpp"
#include "Engine/Core/Decimal.hpp"
#include "Engine/Core/Definitions.hpp"
#include "Engine/Text/TextAsset.hpp"
#include "Engine/Core/Types.hpp"
#include "Engine/Core/Mirror.hpp"

#include <entt/entt.hpp>

#include <cstdint>
#include <type_traits>

namespace Fby
{

// The entity id is EnTT's, exposed under the engine's name so gameplay code
using Entity = entt::entity;

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
    Entity Parent = entt::null;
};

// Transport groups are a separate transform graph from Parent2D: the root is a
// kinematic carrier, and members may keep their own dynamic motion.
inline constexpr std::uint32_t kTransportGroupEnabled = 1u << 0;
inline constexpr std::uint32_t kTransportGroupAutoDetachOutside = 1u << 1;
inline constexpr std::uint32_t kTransportRotationFixed = 0;
inline constexpr std::uint32_t kTransportRotationAuthored = 1;
inline constexpr std::uint32_t kTransportRotationPhysics = 2;
inline constexpr std::uint32_t kCarrierModeRigid = 0;
inline constexpr std::uint32_t kCarrierModeCarriedDynamic = 1;
inline constexpr std::uint32_t kCarrierModeSupport = 2;
inline constexpr std::uint32_t kCarrierModeConveyor = 3;
inline constexpr std::uint32_t kCarrierFlagInheritRotation = 1u << 0;
inline constexpr std::uint32_t kCarrierFlagDetachRequested = 1u << 1;
// Runtime marker: the member had a supporting contact on the previous step.
inline constexpr std::uint32_t kCarrierFlagGrounded = 1u << 2;

// One local input user. Action values and contexts live in the fixed-capacity
// InputSystem; this component is the ECS binding to a user slot.
struct InputUserComponent
{
    std::uint32_t UserId = 0;
    // 0xFFFFFFFF means keyboard/mouse only.
    std::uint32_t GamepadSlot = 0xFFFFFFFFu;
    std::uint32_t Flags = 0;
};

// Vehicle drive/control primitives. The engine does not simulate engine RPM,
// clutch or tire slip; a game maps input to torque and per-wheel drive weights.
enum VehicleFlags : std::uint32_t
{
    VehicleFlagNone = 0,
    // Optional arcade yaw assist; off by default so physics stays predictable.
    VehicleFlagArcadeSteering = 1u << 0,
    VehicleFlagHandbrake = 1u << 1,
};

struct Vehicle2D
{
    // -1 reverse, 0 coast, 1 forward
    float Throttle = 0.0f;
    float Brake = 0.0f;
    // -1..1; applied only with VehicleFlagArcadeSteering
    float Steering = 0.0f;
    float MaxMotorTorque = 400.0f;
    float MaxSpeed = 800.0f;
    float SteeringTorque = 6000.0f;
    std::uint32_t Flags = VehicleFlagNone;
};

inline constexpr std::uint32_t kWheelFlagDrive = 1u << 0;
inline constexpr std::uint32_t kWheelFlagSteer = 1u << 1;

struct Wheel2D
{
    Entity Chassis = entt::null;
    // Chassis-local suspension anchor, pixels.
    Vector2D LocalAnchor{};
    // Chassis-local suspension axis.
    Vector2D SuspensionAxis{0.0f, 1.0f};
    float Radius = 12.0f;
    float SuspensionHertz = 4.0f;
    float SuspensionDamping = 0.7f;
    // Multiplier on Vehicle2D.MaxMotorTorque; set 0 for a non-driven wheel.
    float DriveWeight = 1.0f;
    // Multiplier on Vehicle2D brake; 1 for braked wheels.
    float BrakeWeight = 1.0f;
    // Runtime packed b2WheelJointId.
    std::uint64_t Joint = 0;
    std::uint32_t Flags = kWheelFlagDrive;
};

// Moving surface shared by Support and Conveyor members.
struct TransportSurface
{
    float Friction = 0.6f;
    float TangentSpeed = 0.0f;
    bool OneWay = false;
    bool AllowJump = true;
};

// Path authoring for kinematic carriers. Points are in world/root space; the
// system samples the polyline by arc length and keeps distance deterministic.
struct TransportPath
{
    std::array<Vector2D, 64> Points{};
    std::uint32_t Count = 0;
    bool Closed = false;
};

// Kinematic motion profile. A root with Transform2D only moves by pose; a root
// with a RigidBody2D receives a teleport so the solver sees the new pose.
struct TransportMotion
{
    float Speed = 80.0f;
    float Acceleration = 240.0f;
    float Deceleration = 320.0f;
    float DwellSeconds = 0.0f;
    float DwellRemaining = 0.0f;
    float Distance = 0.0f;
    float TargetDistance = 0.0f;
    float Direction = 1.0f;
    Vector2D Axis{1.0f, 0.0f};
    std::uint32_t Flags = 0;
};

struct TransportGroupComponent
{
    std::uint32_t GroupId = 0;
    std::uint32_t ParentGroupId = 0;
    std::uint32_t RotationPolicy = kTransportRotationFixed;
    std::uint32_t Flags = kTransportGroupEnabled;
    // Local-space bounds used by the optional auto-detach rule.
    Vector2D BoundsMin{-1.0e9f, -1.0e9f};
    Vector2D BoundsMax{1.0e9f, 1.0e9f};
    // Runtime Box2D weld joint to the parent group; zero means none.
    std::uint64_t ParentJoint = 0;
};

static_assert(std::is_trivially_copyable_v<TransportGroupComponent>);
static_assert(std::is_standard_layout_v<TransportGroupComponent>);

struct CarrierMemberComponent
{
    std::uint32_t GroupId = 0;
    std::uint32_t Mode = kCarrierModeCarriedDynamic;
    std::uint32_t Flags = kCarrierFlagInheritRotation;
    Vector2D LocalPosition{};
    float LocalRotation = 0.0f;
    // Velocity relative to the carrier; used by CarriedDynamic and Support.
    Vector2D RelativeVelocity{};
    float RelativeAngularVelocity = 0.0f;
    // Runtime Box2D weld joint for a Rigid member; zero means none.
    std::uint64_t Joint = 0;
};

static_assert(std::is_trivially_copyable_v<CarrierMemberComponent>);
static_assert(std::is_standard_layout_v<CarrierMemberComponent>);

// Linear motion, integrated by the movement system on the fixed step.
struct Velocity
{
    Vector2D Value{};
};

// Everything needed to emit one RenderItem. Kept separate from Transform2D so a
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

// Authored name for the narrative log and the editor inspector. A flat,
struct NarrativeLabelComponent
{
    char Name[32] = {};
};

static_assert(std::is_trivially_copyable_v<NarrativeLabelComponent>);
static_assert(std::is_standard_layout_v<NarrativeLabelComponent>);
static_assert(sizeof(NarrativeLabelComponent) == 32);

// A 128-bit signed gameplay counter. The value is inline and trivially
// copyable, so snapshots and the ECS store it without any allocation.
struct BigIntComponent
{
    BigNum::Int128 Value{};
};

static_assert(std::is_trivially_copyable_v<BigIntComponent>);
static_assert(std::is_standard_layout_v<BigIntComponent>);
static_assert(sizeof(BigIntComponent) == 16);

// A signed decimal gameplay value. The representation is sign, coefficient and
// base-10 exponent, so currency and large idle values keep their decimal scale.
struct DecimalComponent
{
    BigNum::Decimal128 Value{};
};

static_assert(std::is_trivially_copyable_v<DecimalComponent>);
static_assert(std::is_standard_layout_v<DecimalComponent>);
static_assert(sizeof(DecimalComponent) == 24);

// A reference to one block of a long text asset. The individual fields are
// reflected so scene TOML and snapshots need no custom text codec.
inline constexpr std::uint32_t kTextFlagShadow = 1u << 0;
inline constexpr std::uint32_t kTextFlagOutline = 1u << 1;
inline constexpr std::uint32_t kTextFlagCenterX = 1u << 2;
inline constexpr std::uint32_t kTextFlagRightX = 1u << 3;
inline constexpr std::uint32_t kTextFlagHidden = 1u << 4;
inline constexpr std::uint32_t kTextFlagRightToLeft = 1u << 5;
inline constexpr std::uint32_t kTextFlagVertical = 1u << 6;

struct TextComponent
{
    std::uint64_t Asset = 0;
    std::uint32_t Block = 0;
    std::uint32_t Generation = 0;
    std::uint32_t Flags = 0;
    std::uint32_t Reserved = 0;

    [[nodiscard]] Text::TextAssetRef Ref() const noexcept
    {
        return Text::TextAssetRef{AssetId{Asset}, Block, Generation};
    }

    void SetRef(Text::TextAssetRef InRef, std::uint32_t InFlags = 0) noexcept
    {
        Asset = InRef.Asset.value;
        Block = InRef.Block;
        Generation = InRef.Generation;
        Flags = InFlags;
    }
};

static_assert(std::is_trivially_copyable_v<TextComponent>);
static_assert(std::is_standard_layout_v<TextComponent>);
static_assert(sizeof(TextComponent) == 24);

// Optional companion for TextComponent: resolves LocalizationKey through the
// game localization table and renders the result in English or another locale.
struct LocalizedTextComponent
{
    std::uint32_t LocalizationKey = 0;
    float FontSize = 0.0f;
    Color32 Color = Color32::White();
    std::uint32_t Flags = 0;
    std::uint32_t PluralCount = 0;
    std::uint32_t Gender = 0;
};

static_assert(std::is_trivially_copyable_v<LocalizedTextComponent>);
static_assert(std::is_standard_layout_v<LocalizedTextComponent>);
static_assert(sizeof(LocalizedTextComponent) == 24);

// Handle into the DOD TextShapeCache; the component itself stays 8 bytes.
struct TextShapeComponent
{
    std::uint32_t ShapeIndex = 0xFFFFFFFFu;
    std::uint32_t ShapeGeneration = 0;
    std::uint64_t ShapeKeyHash = 0;
};

static_assert(std::is_trivially_copyable_v<TextShapeComponent>);
static_assert(std::is_standard_layout_v<TextShapeComponent>);
static_assert(sizeof(TextShapeComponent) == 16);

struct TextEffectComponent
{
    Color32 ShadowColor = Color32::FromBytes(0, 0, 0, 160);
    Color32 OutlineColor = Color32::FromBytes(0, 0, 0, 255);
    Vector2D ShadowOffset{1.0f, 1.0f};
    float OutlineWidth = 1.0f;
};

static_assert(std::is_trivially_copyable_v<TextEffectComponent>);
static_assert(std::is_standard_layout_v<TextEffectComponent>);
static_assert(sizeof(TextEffectComponent) == 20);

struct TextFormatComponent
{
    BigNum::Int128 Value0{};
    BigNum::Int128 Value1{};
    BigNum::Int128 Value2{};
    BigNum::Int128 Value3{};
    std::uint32_t Spec0 = 0;
    std::uint32_t Spec1 = 0;
    std::uint32_t Spec2 = 0;
    std::uint32_t Spec3 = 0;
    std::uint32_t ValueCount = 0;
    std::uint32_t Reserved = 0;
};

static_assert(std::is_trivially_copyable_v<TextFormatComponent>);
static_assert(std::is_standard_layout_v<TextFormatComponent>);
static_assert(sizeof(TextFormatComponent) == 88);

struct TextConditionComponent
{
    std::uint64_t Mask = 0;
    std::uint64_t Reserved = 0;
};

static_assert(std::is_trivially_copyable_v<TextConditionComponent>);
static_assert(std::is_standard_layout_v<TextConditionComponent>);
static_assert(sizeof(TextConditionComponent) == 16);

static_assert(std::is_trivially_copyable_v<Transform2D>);
static_assert(std::is_standard_layout_v<Transform2D>);
static_assert(sizeof(Transform2D) == 20, "Transform2D is Vec2 + Vec2 + float, tightly packed");

static_assert(std::is_trivially_copyable_v<Sprite2D>);
static_assert(std::is_standard_layout_v<Sprite2D>);
static_assert(std::is_trivially_copyable_v<Velocity>);
static_assert(std::is_trivially_copyable_v<Camera2D>);
}

// Mirror registration
FBY_REFLECT_COMPONENT(Fby::Transform2D, "Transform2D", FBY_FIELD(Fby::Transform2D, Position, Vector2D),
                      FBY_FIELD(Fby::Transform2D, Scale, Vector2D), FBY_FIELD(Fby::Transform2D, Rotation, Float))

FBY_REFLECT_COMPONENT(Fby::Velocity, "Velocity", FBY_FIELD(Fby::Velocity, Value, Vector2D))

FBY_REFLECT_COMPONENT(Fby::TransportGroupComponent, "TransportGroupComponent",
                      FBY_FIELD(Fby::TransportGroupComponent, GroupId, UInt32),
                      FBY_FIELD(Fby::TransportGroupComponent, ParentGroupId, UInt32),
                      FBY_FIELD(Fby::TransportGroupComponent, RotationPolicy, UInt32),
                      FBY_FIELD(Fby::TransportGroupComponent, Flags, UInt32),
                      FBY_FIELD(Fby::TransportGroupComponent, BoundsMin, Vector2D),
                      FBY_FIELD(Fby::TransportGroupComponent, BoundsMax, Vector2D))

FBY_REFLECT_COMPONENT(Fby::CarrierMemberComponent, "CarrierMemberComponent",
                      FBY_FIELD(Fby::CarrierMemberComponent, GroupId, UInt32),
                      FBY_FIELD(Fby::CarrierMemberComponent, Mode, UInt32),
                      FBY_FIELD(Fby::CarrierMemberComponent, Flags, UInt32),
                      FBY_FIELD(Fby::CarrierMemberComponent, LocalPosition, Vector2D),
                      FBY_FIELD(Fby::CarrierMemberComponent, LocalRotation, Float),
                      FBY_FIELD(Fby::CarrierMemberComponent, RelativeVelocity, Vector2D),
                      FBY_FIELD(Fby::CarrierMemberComponent, RelativeAngularVelocity, Float))

FBY_REFLECT_COMPONENT(Fby::TransportPath, "TransportPath",
                      FBY_FIELD(Fby::TransportPath, Count, UInt32),
                      FBY_FIELD(Fby::TransportPath, Closed, Bool))

FBY_REFLECT_COMPONENT(Fby::InputUserComponent, "InputUserComponent",
                      FBY_FIELD(Fby::InputUserComponent, UserId, UInt32),
                      FBY_FIELD(Fby::InputUserComponent, GamepadSlot, UInt32),
                      FBY_FIELD(Fby::InputUserComponent, Flags, UInt32))

FBY_REFLECT_COMPONENT(Fby::Vehicle2D, "Vehicle2D", FBY_FIELD(Fby::Vehicle2D, Throttle, Float),
                      FBY_FIELD(Fby::Vehicle2D, Brake, Float), FBY_FIELD(Fby::Vehicle2D, Steering, Float),
                      FBY_FIELD(Fby::Vehicle2D, MaxMotorTorque, Float), FBY_FIELD(Fby::Vehicle2D, MaxSpeed, Float),
                      FBY_FIELD(Fby::Vehicle2D, SteeringTorque, Float), FBY_FIELD(Fby::Vehicle2D, Flags, UInt32))

FBY_REFLECT_COMPONENT(Fby::Wheel2D, "Wheel2D", FBY_FIELD(Fby::Wheel2D, LocalAnchor, Vector2D),
                      FBY_FIELD(Fby::Wheel2D, SuspensionAxis, Vector2D), FBY_FIELD(Fby::Wheel2D, Radius, Float),
                      FBY_FIELD(Fby::Wheel2D, SuspensionHertz, Float),
                      FBY_FIELD(Fby::Wheel2D, SuspensionDamping, Float),
                      FBY_FIELD(Fby::Wheel2D, DriveWeight, Float), FBY_FIELD(Fby::Wheel2D, BrakeWeight, Float),
                      FBY_FIELD(Fby::Wheel2D, Flags, UInt32))

FBY_REFLECT_COMPONENT(Fby::TransportSurface, "TransportSurface", FBY_FIELD(Fby::TransportSurface, Friction, Float),
                      FBY_FIELD(Fby::TransportSurface, TangentSpeed, Float),
                      FBY_FIELD(Fby::TransportSurface, OneWay, Bool),
                      FBY_FIELD(Fby::TransportSurface, AllowJump, Bool))

FBY_REFLECT_COMPONENT(Fby::TransportMotion, "TransportMotion", FBY_FIELD(Fby::TransportMotion, Speed, Float),
                      FBY_FIELD(Fby::TransportMotion, Acceleration, Float),
                      FBY_FIELD(Fby::TransportMotion, Deceleration, Float),
                      FBY_FIELD(Fby::TransportMotion, DwellSeconds, Float),
                      FBY_FIELD(Fby::TransportMotion, Direction, Float),
                      FBY_FIELD(Fby::TransportMotion, Axis, Vector2D),
                      FBY_FIELD(Fby::TransportMotion, Flags, UInt32))

FBY_REFLECT_COMPONENT(Fby::Sprite2D, "Sprite2D", FBY_FIELD(Fby::Sprite2D, Size, Vector2D),
                      FBY_FIELD(Fby::Sprite2D, UVMin, Vector2D), FBY_FIELD(Fby::Sprite2D, UVMax, Vector2D),
                      FBY_FIELD(Fby::Sprite2D, Layer, UInt32), FBY_FIELD(Fby::Sprite2D, Color, Color32),
                      FBY_FIELD(Fby::Sprite2D, Flags, UInt32))

FBY_REFLECT_COMPONENT(Fby::DrawOrder, "DrawOrder", FBY_FIELD(Fby::DrawOrder, Layer, UInt32),
                      FBY_FIELD(Fby::DrawOrder, Depth, Float), FBY_FIELD(Fby::DrawOrder, MaterialBucket, UInt32))

FBY_REFLECT_COMPONENT(Fby::Camera2D, "Camera2D", FBY_FIELD(Fby::Camera2D, Center, Vector2D),
                      FBY_FIELD(Fby::Camera2D, ViewSize, Vector2D), FBY_FIELD(Fby::Camera2D, Zoom, Float),
                      FBY_FIELD(Fby::Camera2D, Rotation, Float))

FBY_REFLECT_COMPONENT(Fby::Lifetime, "Lifetime", FBY_FIELD(Fby::Lifetime, SecondsRemaining, Float))

FBY_REFLECT_COMPONENT(Fby::NarrativeLabelComponent, "NarrativeLabelComponent",
                      FBY_FIELD(Fby::NarrativeLabelComponent, Name, String))

FBY_REFLECT_COMPONENT(Fby::NameComponent, "NameComponent", FBY_FIELD(Fby::NameComponent, Value, String))

FBY_REFLECT_COMPONENT(Fby::BigIntComponent, "BigIntComponent", FBY_FIELD(Fby::BigIntComponent, Value, Int128))

FBY_REFLECT_COMPONENT(Fby::DecimalComponent, "DecimalComponent", FBY_FIELD(Fby::DecimalComponent, Value, Decimal))

FBY_REFLECT_COMPONENT(Fby::TextComponent, "TextComponent", FBY_FIELD(Fby::TextComponent, Asset, UInt64),
                      FBY_FIELD(Fby::TextComponent, Block, UInt32), FBY_FIELD(Fby::TextComponent, Generation, UInt32),
                      FBY_FIELD(Fby::TextComponent, Flags, UInt32), FBY_FIELD(Fby::TextComponent, Reserved, UInt32))
FBY_REFLECT_COMPONENT(Fby::TextShapeComponent, "TextShapeComponent",
                      FBY_FIELD(Fby::TextShapeComponent, ShapeIndex, UInt32),
                      FBY_FIELD(Fby::TextShapeComponent, ShapeGeneration, UInt32),
                      FBY_FIELD(Fby::TextShapeComponent, ShapeKeyHash, UInt64))
FBY_REFLECT_COMPONENT(Fby::LocalizedTextComponent, "LocalizedTextComponent",
                      FBY_FIELD(Fby::LocalizedTextComponent, LocalizationKey, UInt32),
                      FBY_FIELD(Fby::LocalizedTextComponent, FontSize, Float),
                      FBY_FIELD(Fby::LocalizedTextComponent, Color, Color32),
                      FBY_FIELD(Fby::LocalizedTextComponent, Flags, UInt32),
                      FBY_FIELD(Fby::LocalizedTextComponent, PluralCount, UInt32),
                      FBY_FIELD(Fby::LocalizedTextComponent, Gender, UInt32))

FBY_REFLECT_COMPONENT(Fby::TextEffectComponent, "TextEffectComponent",
                      FBY_FIELD(Fby::TextEffectComponent, ShadowColor, Color32),
                      FBY_FIELD(Fby::TextEffectComponent, OutlineColor, Color32),
                      FBY_FIELD(Fby::TextEffectComponent, ShadowOffset, Vector2D),
                      FBY_FIELD(Fby::TextEffectComponent, OutlineWidth, Float))

FBY_REFLECT_COMPONENT(
    Fby::TextFormatComponent, "TextFormatComponent", FBY_FIELD(Fby::TextFormatComponent, Value0, Int128),
    FBY_FIELD(Fby::TextFormatComponent, Value1, Int128), FBY_FIELD(Fby::TextFormatComponent, Value2, Int128),
    FBY_FIELD(Fby::TextFormatComponent, Value3, Int128), FBY_FIELD(Fby::TextFormatComponent, Spec0, UInt32),
    FBY_FIELD(Fby::TextFormatComponent, Spec1, UInt32), FBY_FIELD(Fby::TextFormatComponent, Spec2, UInt32),
    FBY_FIELD(Fby::TextFormatComponent, Spec3, UInt32), FBY_FIELD(Fby::TextFormatComponent, ValueCount, UInt32),
    FBY_FIELD(Fby::TextFormatComponent, Reserved, UInt32))

FBY_REFLECT_COMPONENT(Fby::TextConditionComponent, "TextConditionComponent",
                      FBY_FIELD(Fby::TextConditionComponent, Mask, UInt64),
                      FBY_FIELD(Fby::TextConditionComponent, Reserved, UInt64))

FBY_REFLECT_COMPONENT_EMPTY(Fby::Hidden, "Hidden")
FBY_REFLECT_COMPONENT_EMPTY(Fby::EditorSelected, "EditorSelected")
