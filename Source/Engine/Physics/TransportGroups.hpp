// FemboyEngine2D - kinematic transport groups for elevators, trains and platforms.
#pragma once

#include "Engine/ECS/Components.hpp"
#include "Engine/ECS/Schedule.hpp"

#include <array>
#include <cstdint>

namespace Fby
{

class PhysicsSystem;

inline constexpr std::uint32_t kTransportMaxGroups = 128;
inline constexpr std::uint32_t kTransportMaxMembers = 256;
inline constexpr std::uint32_t kTransportMaxDepth = 16;
inline constexpr std::uint32_t kTransportInvalidGroup = 0xFFFFFFFFu;
// Above this speed the carrier enables Box2D continuous collision detection.
inline constexpr float kTransportHighSpeedPixelsPerSecond = 1200.0f;

// TransportMotion flags.
inline constexpr std::uint32_t kTransportMotionPath = 1u << 0;
inline constexpr std::uint32_t kTransportMotionPingPong = 1u << 1;
inline constexpr std::uint32_t kTransportMotionStopAtEnds = 1u << 2;
inline constexpr std::uint32_t kTransportMotionFaceDirection = 1u << 3;

// Capabilities the active physics backend can provide for transport joints and
// continuous support. DOD has no joints or CCD, so rigid/weld transport must
// either fall back to kinematic carry or force the Box2D backend.
struct TransportCapabilities
{
    bool Joints = false;
    bool Ccd = false;
    bool Weld = false;
    bool ContactSupport = true;
};

// Runtime view of one carrier. The root is a normal entity with Transform2D and
// optionally a kinematic RigidBody2D; the group is not a second physics world.
struct TransportGroupState
{
    bool Active = false;
    bool Processed = false;
    Entity Root = entt::null;
    std::uint32_t GroupId = 0;
    std::uint32_t ParentGroupId = 0;
    std::uint32_t RotationPolicy = kTransportRotationFixed;
    std::uint32_t Flags = 0;
    Vector2D BoundsMin{-1.0e9f, -1.0e9f};
    Vector2D BoundsMax{1.0e9f, 1.0e9f};
    Vector2D PreviousPosition{};
    float PreviousRotation = 0.0f;
    Vector2D Velocity{};
    float AngularVelocity = 0.0f;
    Vector2D DeltaPosition{};
    float DeltaRotation = 0.0f;
};

// Carrier system. It is not a SystemGraph node because its two halves must run
// on opposite sides of PhysicsSystem::Step. A game calls PrePhysics before the
// physics step and PostPhysics after it.
class TransportGroupSystem
{
public:
    TransportGroupSystem() = default;

    void Clear() noexcept;

    [[nodiscard]] bool CreateGroup(Ecs::Registry& InRegistry, Entity InRoot, std::uint32_t InGroupId,
                                   std::uint32_t InParentGroupId = 0,
                                   std::uint32_t InRotationPolicy = kTransportRotationFixed,
                                   std::uint32_t InFlags = kTransportGroupEnabled) noexcept;
    [[nodiscard]] bool DestroyGroup(Ecs::Registry& InRegistry, std::uint32_t InGroupId) noexcept;
    [[nodiscard]] bool SetParentGroup(Ecs::Registry& InRegistry, std::uint32_t InGroupId,
                                      std::uint32_t InParentGroupId) noexcept;

    [[nodiscard]] bool Attach(Ecs::Registry& InRegistry, Entity InMember, std::uint32_t InGroupId,
                              std::uint32_t InMode = kCarrierModeCarriedDynamic,
                              std::uint32_t InFlags = kCarrierFlagInheritRotation) noexcept;
    [[nodiscard]] bool Detach(Ecs::Registry& InRegistry, Entity InMember, bool InPreserveVelocity = true) noexcept;

    // Computes group deltas, updates nested roots and gives carried bodies their
    // carrier velocity. Call before PhysicsSystem::Step.
    void PrePhysics(Ecs::Registry& InRegistry, PhysicsSystem& InPhysics, float InFixedDt) noexcept;
    // Recomputes local poses, relative velocities and applies detach conditions.
    // Call after PhysicsSystem::Step.
    void PostPhysics(Ecs::Registry& InRegistry, PhysicsSystem& InPhysics, float InFixedDt) noexcept;

    [[nodiscard]] std::uint32_t GroupCount() const noexcept
    {
        return MGroupCount;
    }
    [[nodiscard]] const TransportGroupState* Group(std::uint32_t InGroupId) const noexcept;
    [[nodiscard]] static TransportCapabilities CapabilitiesFor(const PhysicsSystem& InPhysics) noexcept;

protected:
    [[nodiscard]] TransportGroupState* FindGroup(std::uint32_t InGroupId) noexcept;
    [[nodiscard]] const TransportGroupState* FindGroup(std::uint32_t InGroupId) const noexcept;
    void RefreshGroups(Ecs::Registry& InRegistry) noexcept;
    [[nodiscard]] bool ProcessGroup(Ecs::Registry& InRegistry, PhysicsSystem& InPhysics, std::uint32_t InIndex,
                                    std::uint32_t InDepth, float InFixedDt) noexcept;
    [[nodiscard]] bool OutsideBounds(const TransportGroupState& InGroup, Vector2D InLocalPoint) const noexcept;
    [[nodiscard]] Entity FindGroupRoot(Ecs::Registry& InRegistry, std::uint32_t InGroupId) noexcept;
    [[nodiscard]] bool ParentChainContains(Ecs::Registry& InRegistry, std::uint32_t InStartGroupId,
                                           std::uint32_t InWantedGroupId) const noexcept;

    void AdvanceMotion(Ecs::Registry& InRegistry, Entity InRoot, float InFixedDt) noexcept;
    [[nodiscard]] bool IsMemberSupported(Ecs::Registry& InRegistry, Entity InMember, Entity InRoot,
                                         Vector2D InMemberPosition) const noexcept;
    void EnsureRigidJoint(Ecs::Registry& InRegistry, Entity InMember, CarrierMemberComponent& InComponent,
                          const TransportGroupState& InGroup) noexcept;
    void EnsureParentJoint(Ecs::Registry& InRegistry, TransportGroupState& InChild,
                           const TransportGroupState& InParent) noexcept;
    void DestroyJoint(std::uint64_t InJoint) noexcept;
    void ApplyRootPolicy(Ecs::Registry& InRegistry, PhysicsSystem& InPhysics,
                         const TransportGroupState& InGroup) noexcept;
    void EnableHighSpeed(PhysicsSystem& InPhysics, Ecs::Registry& InRegistry, Entity InEntity,
                         Vector2D InVelocity) noexcept;

    std::array<TransportGroupState, kTransportMaxGroups> MGroups{};
    std::uint32_t MGroupCount = 0;
    PhysicsSystem* MPhysics = nullptr;
};
}
