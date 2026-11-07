// FemboyEngine2D - data-oriented fallback physics backend.
#pragma once

#include "Engine/Physics/CollisionFilter.hpp"
#include "Engine/Physics/Physics.hpp"

#include <span>
#include <unordered_map>
#include <vector>

namespace Fby
{

// Deterministic structure-of-arrays AABB solver used by the strategy-oriented
// backend. It deliberately omits joints and rotational collision response;
// the Box2D backend remains the general-purpose choice.
class DodPhysicsBackend
{
public:
    DodPhysicsBackend() = default;
    ~DodPhysicsBackend() = default;

    DodPhysicsBackend(const DodPhysicsBackend&) = delete;
    DodPhysicsBackend& operator=(const DodPhysicsBackend&) = delete;

    [[nodiscard]] bool Initialize(const PhysicsDesc& InDesc);
    void Shutdown();

    void EnsureBodies(Ecs::Registry& InRegistry);
    void Step(Ecs::Registry& InRegistry, float InFixedDt);
    void Interpolate(Ecs::Registry& InRegistry, float InAlpha) const;

    [[nodiscard]] std::uint32_t LiveBodyCount() const noexcept
    {
        return MAliveCount;
    }
    [[nodiscard]] std::span<const PhysicsContactEvent> Contacts() const noexcept
    {
        return {MEvents.data(), MEventCount};
    }
    [[nodiscard]] bool IsValid() const noexcept
    {
        return MInitialized;
    }

    void ApplyImpulse(Ecs::Registry& InRegistry, Entity InEntity, Vector2D InImpulse, bool InWake);
    void SetLinearVelocity(Ecs::Registry& InRegistry, Entity InEntity, Vector2D InVelocity, bool InWake);
    void SetAngularVelocity(Ecs::Registry& InRegistry, Entity InEntity, float InAngularVelocity, bool InWake);
    void SetFixedRotation(Ecs::Registry& InRegistry, Entity InEntity, bool InFixed);
    void SetBullet(Ecs::Registry& InRegistry, Entity InEntity, bool InBullet);
    void ApplyAcceleration(Ecs::Registry& InRegistry, Entity InEntity, Vector2D InAcceleration, bool InWake);
    void SetGravity(Vector2D InGravity) noexcept;
    void ApplyCollisionFilterMatrix(Ecs::Registry& InRegistry, const CollisionFilterMatrix& InMatrix) noexcept;
    std::uint32_t DestroyAllBodies(Ecs::Registry* InRegistry) noexcept;

private:
    // Structure of arrays. The integration and broadphase loops stream positions,
    // extents and velocities through separate vectors, so a step does not pull the
    // cold metadata of every body into the same cache lines.
    struct BodyColumns
    {
        std::vector<Entity> Entities;
        std::vector<Vector2D> Positions;
        std::vector<Vector2D> PreviousPositions;
        std::vector<Vector2D> Velocities;
        // Per-step acceleration in pixels per second squared. It is integrated
        // once and cleared so a caller that applies it every tick behaves the
        // same at any fixed step.
        std::vector<Vector2D> Accelerations;
        std::vector<float> Rotations;
        std::vector<float> PreviousRotations;
        std::vector<float> AngularVelocities;
        std::vector<float> InverseMasses;
        // World gravity multiplier from RigidBody2D::GravityScale.
        std::vector<float> GravityScales;
        std::vector<float> HalfWidths;
        std::vector<float> HalfHeights;
        std::vector<std::uint32_t> Categories;
        std::vector<std::uint32_t> Masks;
        std::vector<std::uint32_t> Flags;
        std::vector<BodyType> Types;
        std::vector<std::uint8_t> Sensors;
        std::vector<std::uint8_t> ContactEvents;
        std::vector<std::uint32_t> SleepFrames;
    };

    void PruneDeadBodies(Ecs::Registry& InRegistry);
    [[nodiscard]] std::uint32_t FindBody(Entity InEntity) const noexcept;
    // Returns the body index for `InEntity`, synchronizing bodies first only when
    // the entity is not registered yet. The setters call this instead of a full
    // EnsureBodies scan on every impulse.
    [[nodiscard]] std::uint32_t EnsureBodyIndex(Ecs::Registry& InRegistry, Entity InEntity);
    // Takes a free slot or appends a new one and resets every column of that slot.
    [[nodiscard]] std::uint32_t AcquireBodySlot() noexcept;
    void ResetBodySlot(std::uint32_t InIndex) noexcept;
    void ClearColumns() noexcept;
    [[nodiscard]] std::uint32_t BodyCount() const noexcept
    {
        return static_cast<std::uint32_t>(MColumns.Entities.size());
    }
    // Stable entity pair key used to turn raw overlaps into begin/end touch
    // events across steps; indices are reused, entity ids are not.
    [[nodiscard]] static std::uint64_t PairKey(Entity InA, Entity InB) noexcept;
    [[nodiscard]] static Entity EntityFromKey(std::uint32_t InRaw) noexcept;
    void EmitContactEvents() noexcept;
    [[nodiscard]] Vector2D MinCorner(std::uint32_t InIndex) const noexcept;
    [[nodiscard]] Vector2D MaxCorner(std::uint32_t InIndex) const noexcept;
    [[nodiscard]] bool Overlaps(std::uint32_t InA, std::uint32_t InB) const noexcept;
    void Resolve(std::uint32_t InA, std::uint32_t InB) noexcept;
    void EmitContact(Entity InA, Entity InB, Vector2D InPoint, Vector2D InNormal, float InSpeed,
                     PhysicsEventType InType) noexcept;

    PhysicsDesc MDesc{};
    BodyColumns MColumns{};
    // Per-body time scale for the current step, filled from TimeScaleComponent.
    std::vector<float> MBodyTimeScale{};
    std::vector<std::uint32_t> MFreeIndices{};
    struct GridEntry
    {
        std::uint64_t key = 0;
        std::uint32_t index = 0;
    };
    std::vector<GridEntry> MGrid{};
    std::vector<std::uint64_t> MPairKeys{};
    // Contacts that overlap during the current step. The previous step's keys are
    // kept to derive BeginTouch and EndTouch exactly once per pair.
    struct ContactRecord
    {
        std::uint64_t key = 0;
        Vector2D point{};
        Vector2D normal{};
        float speed = 0.0f;
    };
    std::vector<ContactRecord> MCurrentContacts{};
    std::vector<std::uint64_t> MPreviousPairKeys{};
    std::vector<PhysicsContactEvent> MEvents{};
    std::unordered_map<Entity, std::uint32_t> MEntityToIndex{};
    std::uint32_t MEventCount = 0;
    std::uint32_t MSequence = 0;
    // Live records only. The columns also hold freed slots waiting for reuse, so
    // their size is the capacity, not the body count.
    std::uint32_t MAliveCount = 0;
    bool MInitialized = false;
};

}
