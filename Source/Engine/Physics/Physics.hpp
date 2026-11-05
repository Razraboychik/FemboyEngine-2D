// FemboyEngine2D - Box2D v3 ECS bridge.
#pragma once

#include "Engine/Core/Definitions.hpp"
#include "Engine/Core/Types.hpp"
#include "Engine/ECS/Components.hpp"
#include "Engine/ECS/Schedule.hpp"
#include "Engine/Physics/CollisionFilter.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <type_traits>
#include <vector>

namespace Fby
{

class DodPhysicsBackend;

// Body records are fixed-capacity diagnostics/ownership data, not a simulation
inline constexpr std::uint32_t kMaxTrackedPhysicsBodies = 65536;

// The project can leave the choice on Auto or force a backend. Auto keeps the
// general-purpose Box2D solver unless the caller asks for a large simple world.
enum class PhysicsBackendKind : std::uint8_t
{
    Auto = 0,
    Box2D = 1,
    Dod = 2,
};

struct PhysicsCapabilities
{
    bool Joints = false;
    bool Ccd = false;
    bool SoftBodies = false;
    bool Deterministic = false;
    bool LargeWorlds = false;
    std::uint32_t MaxRecommendedBodies = 0;
};

enum class BodyType : std::uint8_t
{
    Static = 0,
    Kinematic = 1,
    Dynamic = 2,
};

enum RigidBodyFlags : std::uint32_t
{
    RigidBodyAwake = 1u << 0,
    RigidBodyFixedRotation = 1u << 1,
    RigidBodyBullet = 1u << 2,
    RigidBodyEnabled = 1u << 3,
    // Set by gameplay to push Transform2D into Box2D before the next step
    RigidBodyTeleport = 1u << 4,
    // Runtime marker set by the time-scale pass while a body is frozen. The
    // authored RigidBodyEnabled bit stays untouched so a thaw restores it.
    RigidBodyFrozen = 1u << 5,
};

struct RigidBody2D
{
    // Packed b2BodyId, runtime only, never network data
    std::uint64_t Body = 0;
    Vector2D PreviousPosition{};
    Vector2D Position{};
    Vector2D LinearVelocity{};
    float PreviousRotation = 0.0f;
    float Rotation = 0.0f;
    float AngularVelocity = 0.0f;
    float GravityScale = 1.0f;
    float LinearDamping = 0.0f;
    float AngularDamping = 0.0f;
    BodyType Type = BodyType::Dynamic;
    std::uint32_t Flags = RigidBodyAwake | RigidBodyEnabled;
};

struct Collider2D
{
    Vector2D HalfExtents{16.0f, 16.0f};
    float Density = 1.0f;
    float Friction = 0.3f;
    float Restitution = 0.0f;
    std::uint32_t CategoryBits = 1u;
    std::uint32_t MaskBits = 0xFFFFFFFFu;
    // Packed b2ShapeId, runtime only
    std::uint64_t Shape = 0;
    bool Sensor = false;
    bool ContactEvents = true;
    std::uint16_t Reserved0 = 0;
    std::uint32_t Reserved1 = 0;
};

static_assert(std::is_trivially_copyable_v<RigidBody2D>);
static_assert(std::is_standard_layout_v<RigidBody2D>);
static_assert(std::is_trivially_copyable_v<Collider2D>);
static_assert(std::is_standard_layout_v<Collider2D>);

enum class PhysicsEventType : std::uint8_t
{
    BeginTouch = 0,
    EndTouch = 1,
    Hit = 2,
};

struct PhysicsContactEvent
{
    Entity a = entt::null;
    Entity b = entt::null;
    Vector2D point{};
    Vector2D normal{};
    float speed = 0.0f;
    PhysicsEventType type = PhysicsEventType::BeginTouch;
    std::uint32_t sequence = 0;
};

struct PhysicsRayHit
{
    Entity entity = entt::null;
    Vector2D point{};
    Vector2D normal{0.0f, -1.0f};
    float distance = 0.0f;
};

// Integer-valued modes keep the reflected component script and prefab friendly.
inline constexpr std::uint32_t kProjectileModeAuto = 0;
inline constexpr std::uint32_t kProjectileModeHitscan = 1;
inline constexpr std::uint32_t kProjectileModeSwept = 2;
inline constexpr std::uint32_t kProjectileModeDynamic = 3;

struct Projectile2D
{
    std::uint32_t Mode = kProjectileModeAuto;
    float Speed = 900.0f;
    float Radius = 2.0f;
    float MinWallThickness = 8.0f;
    Vector2D Direction{1.0f, 0.0f};
    std::uint32_t MaxSubsteps = 4;
    std::uint32_t CollisionMask = 0xFFFFFFFFu;
};

struct PhysicsDesc
{
    // Metres / second^2
    Vector2D gravity{0.0f, 9.8f};
    float pixelsPerMeter = 50.0f;
    std::uint32_t subSteps = 4;
    std::uint32_t maxContactEvents = 256;
    // Auto picks DOD only when the caller explicitly requests a large world.
    PhysicsBackendKind backend = PhysicsBackendKind::Auto;
    std::uint32_t bodyBudget = 0;
    bool requireJoints = false;
    bool deterministic = false;
};

class PhysicsSystem
{
public:
    PhysicsSystem();
    ~PhysicsSystem();

    PhysicsSystem(const PhysicsSystem&) = delete;
    PhysicsSystem& operator=(const PhysicsSystem&) = delete;

    [[nodiscard]] bool Initialize(const PhysicsDesc& InDesc = {});
    void Shutdown();

    // Selects a backend before Initialize. Calling it after a world is live is
    // ignored so gameplay code can never swap solver state mid-step.
    void SelectBackend(PhysicsBackendKind InKind) noexcept;
    void SetProjectPolicy(PhysicsBackendKind InKind, std::uint32_t InBodyBudget) noexcept;
    [[nodiscard]] PhysicsBackendKind Backend() const noexcept
    {
        return MBackendKind;
    }
    [[nodiscard]] PhysicsCapabilities Capabilities() const noexcept;

    // Creates missing bodies/shapes, steps the world once at fixedDt and copies
    void Step(Ecs::Registry& InRegistry, float InFixedDt);

    // Writes lerp(previous, current, alpha) into Transform2D. Call from
    void Interpolate(Ecs::Registry& InRegistry, float InAlpha) const;

    // Creates bodies/shapes for every RigidBody2D whose runtime handle is null.
    void EnsureBodies(Ecs::Registry& InRegistry);

    // Ownership tracking. CreateMissingBodies() calls TrackBody() itself; an
    void TrackBody(Entity InEntity, std::uint64_t InPackedBody) noexcept;
    void UntrackBody(std::uint64_t InPackedBody) noexcept;

    // Destroys every live Box2D body owned by this world before the registry
    std::uint32_t DestroyAllBodies(Ecs::Registry* InRegistry = nullptr) noexcept;

    // Installs a cleanup hook so a scene clear or snapshot restore releases the
    // native bodies of this world. Safe to call once per registry.
    void BindToRegistry(Ecs::Registry& InRegistry) noexcept;

    [[nodiscard]] std::uint32_t LiveBodyCount() const noexcept;

    [[nodiscard]] std::span<const PhysicsContactEvent> Contacts() const noexcept;

    [[nodiscard]] float PixelsPerMeter() const noexcept
    {
        return MDesc.pixelsPerMeter;
    }
    [[nodiscard]] const PhysicsDesc& Description() const noexcept
    {
        return MDesc;
    }
    [[nodiscard]] bool IsValid() const noexcept;

    // Packed b2WorldId. Exposed as an integer so extension modules (joint
    [[nodiscard]] std::uint32_t WorldId() const noexcept
    {
        return MWorld;
    }

    // Applies a linear impulse in pixel-space units (converted internally to
    void ApplyImpulse(Ecs::Registry& InRegistry, Entity InEntity, Vector2D InImpulse, bool InWake = true);

    // Sets the body velocity directly, in pixel-space units. Carrier groups use
    // this instead of writing RigidBody2D.LinearVelocity, because that field is
    // a readback of the solver state and is not pushed back into Box2D.
    void SetLinearVelocity(Ecs::Registry& InRegistry, Entity InEntity, Vector2D InVelocity, bool InWake = true);
    void SetAngularVelocity(Ecs::Registry& InRegistry, Entity InEntity, float InAngularVelocity, bool InWake = true);
    // Locks the body's angular degree of freedom. This is how a Fixed transport
    // root remains an elevator while still being allowed to translate.
    void SetFixedRotation(Ecs::Registry& InRegistry, Entity InEntity, bool InFixed) noexcept;
    // Enables continuous collision detection for fast carriers and cargo.
    void SetBullet(Ecs::Registry& InRegistry, Entity InEntity, bool InBullet) noexcept;

    // Applies an acceleration in pixel/s^2. The body mass is read from Box2D so
    // a local gravity field accelerates light and heavy bodies equally.
    void ApplyAcceleration(Ecs::Registry& InRegistry, Entity InEntity, Vector2D InAcceleration, bool InWake = true);

    // Segment query against the ECS colliders. Both backends use the same
    // sweep so projectiles behave identically before a body is created.
    [[nodiscard]] bool RayCast(Ecs::Registry& InRegistry, Vector2D InFrom, Vector2D InTo, Entity InIgnore,
                               std::uint32_t InMask, PhysicsRayHit& OutHit) const noexcept;

    // Replaces world gravity immediately. Level systems call this when the
    // authored mode or strength changes.
    void SetGravity(Vector2D InGravity) noexcept;
    [[nodiscard]] Vector2D Gravity() const noexcept
    {
        return MDesc.gravity;
    }

    // World clock multiplier for the fixed step. Zero stops the whole world
    // without dropping bodies; per-entity TimeScaleComponent can freeze a
    // single body while the rest of the world keeps running.
    void SetTimeScale(float InScale) noexcept
    {
        MTimeScale = InScale > 0.0f ? InScale : 0.0f;
    }
    [[nodiscard]] float TimeScale() const noexcept
    {
        return MTimeScale;
    }

    // Rewrites every live b2Shape filter from the project's layer matrix. The
    void ApplyCollisionFilterMatrix(Ecs::Registry& InRegistry, const CollisionFilterMatrix& InMatrix) noexcept;
    [[nodiscard]] const CollisionFilterMatrix& CollisionFilter() const noexcept
    {
        return MFilter;
    }

private:
    void ApplyShapeFilter(Collider2D& InCollider) noexcept;
    void CreateMissingBodies(Ecs::Registry& InRegistry);
    void SyncBodiesFromRegistry(Ecs::Registry& InRegistry);
    void ReadBodiesIntoRegistry(Ecs::Registry& InRegistry);
    void ReadContactEvents(Ecs::Registry& InRegistry);
    void PruneDeadBodies(Ecs::Registry& InRegistry) noexcept;
    // Applies CollisionPolicyComponent to the collider and the body, and syncs
    // a changed body type into Box2D before the next step.
    void ApplyPresentationPolicies(Ecs::Registry& InRegistry) noexcept;
    // Freezes and thaws bodies according to TimeScaleComponent.
    void ApplyEntityTimeScale(Ecs::Registry& InRegistry) noexcept;

    struct BodyRecord
    {
        Entity entity = entt::null;
        // Packed b2BodyId
        std::uint64_t body = 0;
    };

    PhysicsDesc MDesc{};
    PhysicsBackendKind MRequestedBackend = PhysicsBackendKind::Auto;
    PhysicsBackendKind MBackendKind = PhysicsBackendKind::Box2D;
    std::unique_ptr<DodPhysicsBackend> MDod{};
    // Packed b2WorldId
    std::uint32_t MWorld = 0;
    std::array<PhysicsContactEvent, 256> MEvents{};
    std::uint32_t MEventCount = 0;
    std::uint32_t MSequence = 0;
    CollisionFilterMatrix MFilter{};
    bool MFilterActive = false;
    // Global physics clock multiplier; 1 by default and clamped at zero.
    float MTimeScale = 1.0f;
    // Reserved in Initialize(); TrackBody() never allocates after that.
    std::vector<BodyRecord> MBodies{};
};
}

// Physics components are reflected for the editor inspector. Snapshot v4 uses
FBY_REFLECT_COMPONENT(Fby::RigidBody2D, "RigidBody2D", FBY_FIELD(Fby::RigidBody2D, Position, Vector2D),
                      FBY_FIELD(Fby::RigidBody2D, LinearVelocity, Vector2D),
                      FBY_FIELD(Fby::RigidBody2D, Rotation, Float), FBY_FIELD(Fby::RigidBody2D, AngularVelocity, Float),
                      FBY_FIELD(Fby::RigidBody2D, GravityScale, Float),
                      FBY_FIELD(Fby::RigidBody2D, LinearDamping, Float),
                      FBY_FIELD(Fby::RigidBody2D, AngularDamping, Float))

FBY_REFLECT_COMPONENT(Fby::Collider2D, "Collider2D", FBY_FIELD(Fby::Collider2D, HalfExtents, Vector2D),
                      FBY_FIELD(Fby::Collider2D, Density, Float), FBY_FIELD(Fby::Collider2D, Friction, Float),
                      FBY_FIELD(Fby::Collider2D, Restitution, Float), FBY_FIELD(Fby::Collider2D, CategoryBits, UInt32),
                      FBY_FIELD(Fby::Collider2D, MaskBits, UInt32), FBY_FIELD(Fby::Collider2D, Sensor, Bool),
                      FBY_FIELD(Fby::Collider2D, ContactEvents, Bool))

FBY_REFLECT_COMPONENT(Fby::Projectile2D, "Projectile2D", FBY_FIELD(Fby::Projectile2D, Mode, UInt32),
                      FBY_FIELD(Fby::Projectile2D, Speed, Float), FBY_FIELD(Fby::Projectile2D, Radius, Float),
                      FBY_FIELD(Fby::Projectile2D, MinWallThickness, Float),
                      FBY_FIELD(Fby::Projectile2D, Direction, Vector2D),
                      FBY_FIELD(Fby::Projectile2D, MaxSubsteps, UInt32),
                      FBY_FIELD(Fby::Projectile2D, CollisionMask, UInt32))
