// FemboyEngine2D - data-oriented fallback physics backend.
#pragma once

#include "Engine/Physics/CollisionFilter.hpp"
#include "Engine/Physics/Physics.hpp"

#include <span>
#include <unordered_map>
#include <vector>

namespace Fby
{

// Deterministic fixed-capacity AABB solver used by the strategy-oriented
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
        return static_cast<std::uint32_t>(MBodies.size());
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
    struct Body
    {
        Entity entity = entt::null;
        Vector2D position{};
        Vector2D previousPosition{};
        Vector2D velocity{};
        float rotation = 0.0f;
        float previousRotation = 0.0f;
        float angularVelocity = 0.0f;
        float inverseMass = 0.0f;
        float halfWidth = 0.5f;
        float halfHeight = 0.5f;
        std::uint32_t category = 1u;
        std::uint32_t mask = 0xFFFFFFFFu;
        std::uint32_t flags = RigidBodyAwake | RigidBodyEnabled;
        BodyType type = BodyType::Dynamic;
        bool sensor = false;
        bool contactEvents = true;
        std::uint32_t sleepFrames = 0;
    };

    void PruneDeadBodies(Ecs::Registry& InRegistry);
    [[nodiscard]] std::uint32_t FindBody(Entity InEntity) const noexcept;
    [[nodiscard]] static Vector2D MinCorner(const Body& InBody) noexcept;
    [[nodiscard]] static Vector2D MaxCorner(const Body& InBody) noexcept;
    [[nodiscard]] static bool Overlaps(const Body& InA, const Body& InB) noexcept;
    void Resolve(Body& InA, Body& InB, std::uint32_t InAIndex, std::uint32_t InBIndex) noexcept;
    void EmitContact(Entity InA, Entity InB, Vector2D InPoint, Vector2D InNormal, float InSpeed,
                     PhysicsEventType InType) noexcept;

    PhysicsDesc MDesc{};
    std::vector<Body> MBodies{};
    std::vector<std::uint32_t> MFreeIndices{};
    struct GridEntry
    {
        std::uint64_t key = 0;
        std::uint32_t index = 0;
    };
    std::vector<GridEntry> MGrid{};
    std::vector<std::uint64_t> MPairKeys{};
    std::vector<PhysicsContactEvent> MEvents{};
    std::unordered_map<Entity, std::uint32_t> MEntityToIndex{};
    std::uint32_t MEventCount = 0;
    std::uint32_t MSequence = 0;
    bool MInitialized = false;
};

}
