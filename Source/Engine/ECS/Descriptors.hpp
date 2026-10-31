// FemboyEngine2D - data-driven prefab and gameplay descriptors.
#pragma once

#include "Engine/Core/Definitions.hpp"
#include "Engine/Core/Types.hpp"
#include "Engine/ECS/Components.hpp"

#include <array>
#include <cstdint>
#include <type_traits>

namespace Fby
{

enum class PhysicsShapeKind : std::uint32_t
{
    Box = 0,
    Circle = 1,
    Capsule = 2,
};

struct PhysicsBodyDesc
{
    // BodyType: 0 static, 1 kinematic, 2 dynamic
    std::uint32_t Type = 2;
    // ShapeKind: 0 box, 1 circle, 2 capsule
    std::uint32_t Shape = 0;
    Vector2D HalfExtents{16.0f, 16.0f};
    float Radius = 8.0f;
    // Box2D mass is derived from density and area; this field is the authored
    float Mass = 1.0f;
    float Density = 1.0f;
    float Friction = 0.3f;
    float Restitution = 0.0f;
    float LinearDamping = 0.0f;
    float AngularDamping = 0.0f;
    float GravityScale = 1.0f;
    // Box2D b2Filter fields. categoryBits selects the collision-layer row.
    std::uint32_t CategoryBits = 1u;
    std::uint32_t MaskBits = 0xFFFFFFFFu;
    std::int32_t GroupIndex = 0;
    bool Sensor = false;
    bool ContactEvents = true;
    bool FixedRotation = false;
    bool Bullet = false;
};

static_assert(std::is_trivially_copyable_v<PhysicsBodyDesc>);
static_assert(std::is_standard_layout_v<PhysicsBodyDesc>);

struct CharacterDesc
{
    // Rest/starting profile. A spawner copies these values into the runtime
    float MaxHealth = 100.0f;
    float MaxStamina = 100.0f;
    float MoveSpeed = 220.0f;
    float Acceleration = 1800.0f;
    float JumpVelocity = 520.0f;
    float Damage = 10.0f;
    float FireInterval = 0.12f;
    float ProjectileSpeed = 900.0f;
    float InvulnerabilitySeconds = 0.25f;
    float Armor = 0.0f;
    std::uint32_t Team = 0;
    std::uint32_t WeaponArchetype = 0;
};

static_assert(std::is_trivially_copyable_v<CharacterDesc>);
static_assert(std::is_standard_layout_v<CharacterDesc>);

inline constexpr std::uint32_t kPrefabMaxEntities = 32;
inline constexpr std::uint32_t kPrefabNameBytes = 32;
inline constexpr std::uint32_t kPrefabAssetNameBytes = 64;

// A fixed-size prefab record. `flags` bit 0 = physics, bit 1 = character,
struct PrefabEntityDesc
{
    char name[kPrefabNameBytes] = {};
    std::uint32_t flags = 0;
    Transform2D transform{};
    PhysicsBodyDesc physics{};
    CharacterDesc character{};
    Sprite2D sprite{};
};

static_assert(std::is_trivially_copyable_v<PrefabEntityDesc>);

struct PrefabAsset
{
    char name[kPrefabAssetNameBytes] = {};
    std::uint32_t version = 1;
    std::uint32_t count = 0;
    std::array<PrefabEntityDesc, kPrefabMaxEntities> entities{};
};

static_assert(std::is_trivially_copyable_v<PrefabAsset>);

enum PrefabEntityFlags : std::uint32_t
{
    PrefabHasPhysics = 1u << 0,
    PrefabHasCharacter = 1u << 1,
    PrefabHasSprite = 1u << 2,
};
}

// Descriptors are reflected exactly like components so the inspector, the TOML
FBY_REFLECT_COMPONENT(Fby::PhysicsBodyDesc, "PhysicsBodyDesc", FBY_FIELD(Fby::PhysicsBodyDesc, Type, UInt32),
                      FBY_FIELD(Fby::PhysicsBodyDesc, Shape, UInt32),
                      FBY_FIELD(Fby::PhysicsBodyDesc, HalfExtents, Vector2D),
                      FBY_FIELD(Fby::PhysicsBodyDesc, Radius, Float), FBY_FIELD(Fby::PhysicsBodyDesc, Mass, Float),
                      FBY_FIELD(Fby::PhysicsBodyDesc, Density, Float),
                      FBY_FIELD(Fby::PhysicsBodyDesc, Friction, Float),
                      FBY_FIELD(Fby::PhysicsBodyDesc, Restitution, Float),
                      FBY_FIELD(Fby::PhysicsBodyDesc, LinearDamping, Float),
                      FBY_FIELD(Fby::PhysicsBodyDesc, AngularDamping, Float),
                      FBY_FIELD(Fby::PhysicsBodyDesc, GravityScale, Float),
                      FBY_FIELD(Fby::PhysicsBodyDesc, CategoryBits, UInt32),
                      FBY_FIELD(Fby::PhysicsBodyDesc, MaskBits, UInt32),
                      FBY_FIELD(Fby::PhysicsBodyDesc, GroupIndex, Int32),
                      FBY_FIELD(Fby::PhysicsBodyDesc, Sensor, Bool),
                      FBY_FIELD(Fby::PhysicsBodyDesc, ContactEvents, Bool),
                      FBY_FIELD(Fby::PhysicsBodyDesc, FixedRotation, Bool),
                      FBY_FIELD(Fby::PhysicsBodyDesc, Bullet, Bool))

FBY_REFLECT_COMPONENT(Fby::CharacterDesc, "CharacterDesc", FBY_FIELD(Fby::CharacterDesc, MaxHealth, Float),
                      FBY_FIELD(Fby::CharacterDesc, MaxStamina, Float),
                      FBY_FIELD(Fby::CharacterDesc, MoveSpeed, Float),
                      FBY_FIELD(Fby::CharacterDesc, Acceleration, Float),
                      FBY_FIELD(Fby::CharacterDesc, JumpVelocity, Float),
                      FBY_FIELD(Fby::CharacterDesc, Damage, Float),
                      FBY_FIELD(Fby::CharacterDesc, FireInterval, Float),
                      FBY_FIELD(Fby::CharacterDesc, ProjectileSpeed, Float),
                      FBY_FIELD(Fby::CharacterDesc, InvulnerabilitySeconds, Float),
                      FBY_FIELD(Fby::CharacterDesc, Armor, Float),
                      FBY_FIELD(Fby::CharacterDesc, Team, UInt32),
                      FBY_FIELD(Fby::CharacterDesc, WeaponArchetype, UInt32))
