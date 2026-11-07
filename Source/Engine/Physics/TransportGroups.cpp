// FemboyEngine2D - transport group implementation.
#include "Engine/Physics/TransportGroups.hpp"

#include "Engine/Core/SimMath.hpp"
#include "Engine/Physics/Physics.hpp"

#include <box2d/box2d.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace Fby
{
namespace
{
constexpr float kTransportEpsilon = 1.0e-6f;
constexpr float kTransportPi = 3.14159265358979323846f;

[[nodiscard]] Vector2D Rotate(Vector2D InValue, float InRadians) noexcept
{
    if (InRadians == 0.0f)
    {
        return InValue;
    }
    const float ScopedCos = Sim::Cos(InRadians);
    const float ScopedSin = Sim::Sin(InRadians);
    return Vector2D{InValue.X * ScopedCos - InValue.Y * ScopedSin, InValue.X * ScopedSin + InValue.Y * ScopedCos};
}

[[nodiscard]] float NormalizeAngle(float InAngle) noexcept
{
    while (InAngle > kTransportPi)
    {
        InAngle -= 2.0f * kTransportPi;
    }
    while (InAngle < -kTransportPi)
    {
        InAngle += 2.0f * kTransportPi;
    }
    return InAngle;
}

[[nodiscard]] Vector2D CrossAngular(float InAngularVelocity, Vector2D InRadius) noexcept
{
    return Vector2D{-InAngularVelocity * InRadius.Y, InAngularVelocity * InRadius.X};
}

[[nodiscard]] Vector2D TransformPoint(const Transform2D& InTransform, Vector2D InLocal) noexcept
{
    const Vector2D ScopedScaled{InLocal.X * InTransform.Scale.X, InLocal.Y * InTransform.Scale.Y};
    return InTransform.Position + Rotate(ScopedScaled, InTransform.Rotation);
}

[[nodiscard]] Vector2D InverseTransformPoint(const Transform2D& InTransform, Vector2D InWorld) noexcept
{
    const Vector2D ScopedDelta = Rotate(InWorld - InTransform.Position, -InTransform.Rotation);
    return Vector2D{ScopedDelta.X / std::max(InTransform.Scale.X, kTransportEpsilon),
                    ScopedDelta.Y / std::max(InTransform.Scale.Y, kTransportEpsilon)};
}

[[nodiscard]] Transform2D Compose(const Transform2D& InParent, Vector2D InLocalPosition, float InLocalRotation) noexcept
{
    Transform2D ScopedResult{};
    ScopedResult.Position = TransformPoint(InParent, InLocalPosition);
    ScopedResult.Scale = InParent.Scale;
    ScopedResult.Rotation = InParent.Rotation + InLocalRotation;
    return ScopedResult;
}

// Moves a child by the parent's world delta. Scale is treated as one for the
// group root; a carrier root is a kinematic frame, not a scaling hierarchy.
[[nodiscard]] Transform2D ApplyParentDelta(const Transform2D& InOldParent, const Transform2D& InNewParent,
                                           const Transform2D& InChild) noexcept
{
    const Vector2D ScopedLocalPosition = InverseTransformPoint(InOldParent, InChild.Position);
    const float ScopedLocalRotation = InChild.Rotation - InOldParent.Rotation;
    Transform2D ScopedResult = Compose(InNewParent, ScopedLocalPosition, ScopedLocalRotation);
    ScopedResult.Scale = InChild.Scale;
    return ScopedResult;
}

[[nodiscard]] const TransportGroupComponent* FindGroupComponent(Ecs::Registry& InRegistry,
                                                                std::uint32_t InGroupId) noexcept
{
    auto ScopedView = InRegistry.view<TransportGroupComponent>();
    for (const Entity ScopedEntity : ScopedView)
    {
        const TransportGroupComponent& ScopedComponent = ScopedView.get<TransportGroupComponent>(ScopedEntity);
        if (ScopedComponent.GroupId == InGroupId)
        {
            return &ScopedComponent;
        }
    }
    return nullptr;
}

[[nodiscard]] Vector2D MemberCarryVelocity(const TransportGroupState& InGroup, const Transform2D& InRoot,
                                           Vector2D InMemberPosition) noexcept
{
    return InGroup.Velocity + CrossAngular(InGroup.AngularVelocity, InMemberPosition - InRoot.Position);
}

// Reads the pose a carrier should use. PhysicsSystem writes the current pose into
// RigidBody2D and only Interpolate writes Transform2D, so using Transform2D here
// would lag a fixed step behind.
[[nodiscard]] bool ReadEntityPose(Ecs::Registry& InRegistry, Entity InEntity, Transform2D* OutPose) noexcept
{
    const Transform2D* ScopedTransform = InRegistry.try_get<Transform2D>(InEntity);
    const RigidBody2D* ScopedBody = InRegistry.try_get<RigidBody2D>(InEntity);
    if (ScopedTransform == nullptr && ScopedBody == nullptr)
    {
        return false;
    }
    if (ScopedTransform != nullptr)
    {
        *OutPose = *ScopedTransform;
    }
    if (ScopedBody != nullptr && ScopedBody->Body != 0 && ScopedBody->Type != BodyType::Static &&
        (ScopedBody->Flags & RigidBodyTeleport) == 0)
    {
        OutPose->Position = ScopedBody->Position;
        OutPose->Rotation = ScopedBody->Rotation;
    }
    return true;
}

void WriteEntityPose(Ecs::Registry& InRegistry, Entity InEntity, const Transform2D& InPose, bool InTeleport) noexcept
{
    if (Transform2D* ScopedTransform = InRegistry.try_get<Transform2D>(InEntity); ScopedTransform != nullptr)
    {
        *ScopedTransform = InPose;
    }
    if (RigidBody2D* ScopedBody = InRegistry.try_get<RigidBody2D>(InEntity); ScopedBody != nullptr)
    {
        ScopedBody->Position = InPose.Position;
        ScopedBody->Rotation = InPose.Rotation;
        if (InTeleport)
        {
            ScopedBody->Flags |= RigidBodyTeleport;
        }
    }
}
}

[[nodiscard]] float PathLength(const TransportPath& InPath) noexcept
{
    if (InPath.Count < 2)
    {
        return 0.0f;
    }
    float ScopedLength = 0.0f;
    const std::uint32_t ScopedSegments = InPath.Closed ? InPath.Count : InPath.Count - 1;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedSegments; ++ScopedIndex)
    {
        const Vector2D ScopedDelta = InPath.Points[(ScopedIndex + 1u) % InPath.Count] - InPath.Points[ScopedIndex];
        ScopedLength += std::sqrt(ScopedDelta.Dot(ScopedDelta));
    }
    return ScopedLength;
}

[[nodiscard]] Vector2D SamplePath(const TransportPath& InPath, float InDistance, Vector2D* OutDirection) noexcept
{
    if (InPath.Count == 0)
    {
        return Vector2D{};
    }
    if (InPath.Count == 1)
    {
        *OutDirection = Vector2D{1.0f, 0.0f};
        return InPath.Points[0];
    }
    const std::uint32_t ScopedSegments = InPath.Closed ? InPath.Count : InPath.Count - 1;
    float ScopedRemaining = InDistance;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedSegments; ++ScopedIndex)
    {
        const Vector2D ScopedA = InPath.Points[ScopedIndex];
        const Vector2D ScopedB = InPath.Points[(ScopedIndex + 1u) % InPath.Count];
        const Vector2D ScopedDelta = ScopedB - ScopedA;
        const float ScopedSegment = std::sqrt(ScopedDelta.Dot(ScopedDelta));
        if (ScopedRemaining <= ScopedSegment || ScopedIndex + 1u == ScopedSegments)
        {
            const float ScopedT = ScopedSegment > kTransportEpsilon ? std::clamp(ScopedRemaining / ScopedSegment, 0.0f, 1.0f) : 0.0f;
            *OutDirection = ScopedSegment > kTransportEpsilon ? ScopedDelta / ScopedSegment : Vector2D{1.0f, 0.0f};
            return ScopedA + ScopedDelta * ScopedT;
        }
        ScopedRemaining -= ScopedSegment;
    }
    return InPath.Points[InPath.Count - 1];
}

[[nodiscard]] const TransportSurface& SurfaceFor(Ecs::Registry& InRegistry, Entity InRoot) noexcept
{
    static const TransportSurface ScopedDefault{};
    const TransportSurface* ScopedSurface = InRegistry.try_get<TransportSurface>(InRoot);
    return ScopedSurface != nullptr ? *ScopedSurface : ScopedDefault;
}

void TransportGroupSystem::AdvanceMotion(Ecs::Registry& InRegistry, Entity InRoot, float InFixedDt) noexcept
{
    TransportMotion* ScopedMotion = InRegistry.try_get<TransportMotion>(InRoot);
    if (ScopedMotion == nullptr || InFixedDt <= 0.0f)
    {
        return;
    }
    Transform2D ScopedPose{};
    if (!ReadEntityPose(InRegistry, InRoot, &ScopedPose))
    {
        return;
    }
    if (ScopedMotion->DwellRemaining > 0.0f)
    {
        ScopedMotion->DwellRemaining = std::max(0.0f, ScopedMotion->DwellRemaining - InFixedDt);
        return;
    }

    const float ScopedDirection = ScopedMotion->Direction < 0.0f ? -1.0f : 1.0f;
    const float ScopedSpeed = std::max(ScopedMotion->Speed, 0.0f) * ScopedDirection;
    const TransportPath* ScopedPath = InRegistry.try_get<TransportPath>(InRoot);
    if ((ScopedMotion->Flags & kTransportMotionPath) != 0 && ScopedPath != nullptr && ScopedPath->Count >= 2)
    {
        const float ScopedLength = PathLength(*ScopedPath);
        if (ScopedLength <= kTransportEpsilon)
        {
            return;
        }
        ScopedMotion->Distance += ScopedSpeed * InFixedDt;
        if (ScopedPath->Closed)
        {
            ScopedMotion->Distance = std::fmod(ScopedMotion->Distance, ScopedLength);
            if (ScopedMotion->Distance < 0.0f)
            {
                ScopedMotion->Distance += ScopedLength;
            }
        }
        else if (ScopedMotion->Distance >= ScopedLength)
        {
            ScopedMotion->Distance = ScopedLength;
            if ((ScopedMotion->Flags & kTransportMotionStopAtEnds) != 0)
            {
                ScopedMotion->Speed = 0.0f;
                ScopedMotion->DwellRemaining = std::max(ScopedMotion->DwellSeconds, 0.0f);
            }
            else if ((ScopedMotion->Flags & kTransportMotionPingPong) != 0)
            {
                ScopedMotion->Direction = -1.0f;
                ScopedMotion->Distance = std::max(ScopedLength - 0.001f, 0.0f);
            }
        }
        else if (ScopedMotion->Distance < 0.0f)
        {
            ScopedMotion->Distance = 0.0f;
            if ((ScopedMotion->Flags & kTransportMotionPingPong) != 0)
            {
                ScopedMotion->Direction = 1.0f;
            }
            else
            {
                ScopedMotion->Speed = 0.0f;
            }
        }
        Vector2D ScopedDirectionVector{1.0f, 0.0f};
        ScopedPose.Position = SamplePath(*ScopedPath, ScopedMotion->Distance, &ScopedDirectionVector);
        if ((ScopedMotion->Flags & kTransportMotionFaceDirection) != 0)
        {
            ScopedPose.Rotation = Sim::Atan2(ScopedDirectionVector.Y, ScopedDirectionVector.X);
        }
    }
    else
    {
        ScopedPose.Position += ScopedMotion->Axis * (ScopedSpeed * InFixedDt);
    }
    WriteEntityPose(InRegistry, InRoot, ScopedPose,
                    InRegistry.try_get<RigidBody2D>(InRoot) != nullptr);
}

bool TransportGroupSystem::IsMemberSupported(Ecs::Registry& InRegistry, Entity InMember, Entity InRoot,
                                             Vector2D InMemberPosition) const noexcept
{
    const Collider2D* ScopedMemberCollider = InRegistry.try_get<Collider2D>(InMember);
    const Collider2D* ScopedRootCollider = InRegistry.try_get<Collider2D>(InRoot);
    if (ScopedMemberCollider == nullptr || ScopedRootCollider == nullptr)
    {
        return false;
    }
    Transform2D ScopedRootPose{};
    if (!ReadEntityPose(InRegistry, InRoot, &ScopedRootPose))
    {
        return false;
    }
    const TransportSurface& ScopedSurface = SurfaceFor(InRegistry, InRoot);
    if (ScopedSurface.OneWay && InMemberPosition.Y < ScopedRootPose.Position.Y)
    {
        return false;
    }
    const float ScopedMemberBottom = InMemberPosition.Y - ScopedMemberCollider->HalfExtents.Y;
    const float ScopedRootTop = ScopedRootPose.Position.Y + ScopedRootCollider->HalfExtents.Y;
    const float ScopedTolerance = std::max(ScopedMemberCollider->HalfExtents.Y * 0.35f, 2.0f);
    const float ScopedHorizontal = std::abs(InMemberPosition.X - ScopedRootPose.Position.X);
    return ScopedHorizontal <= ScopedMemberCollider->HalfExtents.X + ScopedRootCollider->HalfExtents.X &&
           std::abs(ScopedMemberBottom - ScopedRootTop) <= ScopedTolerance;
}

void TransportGroupSystem::DestroyJoint(std::uint64_t InJoint) noexcept
{
    if (InJoint == 0)
    {
        return;
    }
    const b2JointId ScopedJoint = b2LoadJointId(InJoint);
    if (b2Joint_IsValid(ScopedJoint))
    {
        b2DestroyJoint(ScopedJoint);
    }
}

TransportCapabilities TransportGroupSystem::CapabilitiesFor(const PhysicsSystem& InPhysics) noexcept
{
    const PhysicsCapabilities ScopedCapabilities = InPhysics.Capabilities();
    TransportCapabilities ScopedResult;
    ScopedResult.Joints = ScopedCapabilities.Joints;
    ScopedResult.Ccd = ScopedCapabilities.Ccd;
    ScopedResult.Weld = ScopedCapabilities.Joints;
    ScopedResult.ContactSupport = true;
    return ScopedResult;
}

void TransportGroupSystem::EnsureRigidJoint(Ecs::Registry& InRegistry, Entity InMember,
                                            CarrierMemberComponent& InComponent,
                                            const TransportGroupState& InGroup) noexcept
{
    if (MPhysics != nullptr && !CapabilitiesFor(*MPhysics).Weld)
    {
        return;
    }
    if (InComponent.Joint != 0 && !b2Joint_IsValid(b2LoadJointId(InComponent.Joint)))
    {
        InComponent.Joint = 0;
    }
    if (InComponent.Mode != kCarrierModeRigid || (InComponent.Flags & kCarrierFlagInheritRotation) == 0)
    {
        DestroyJoint(InComponent.Joint);
        InComponent.Joint = 0;
        return;
    }
    if (MPhysics == nullptr || InComponent.Joint != 0)
    {
        return;
    }
    const RigidBody2D* ScopedRootBody = InRegistry.try_get<RigidBody2D>(InGroup.Root);
    const RigidBody2D* ScopedMemberBody = InRegistry.try_get<RigidBody2D>(InMember);
    if (ScopedRootBody == nullptr || ScopedMemberBody == nullptr || ScopedRootBody->Body == 0 ||
        ScopedMemberBody->Body == 0)
    {
        return;
    }
    const b2BodyId ScopedRootId = b2LoadBodyId(ScopedRootBody->Body);
    const b2BodyId ScopedMemberId = b2LoadBodyId(ScopedMemberBody->Body);
    if (!b2Body_IsValid(ScopedRootId) || !b2Body_IsValid(ScopedMemberId))
    {
        return;
    }
    const float ScopedPixelsPerMeter = std::max(MPhysics->PixelsPerMeter(), 1.0e-3f);
    b2WeldJointDef ScopedDefinition = b2DefaultWeldJointDef();
    ScopedDefinition.base.bodyIdA = ScopedRootId;
    ScopedDefinition.base.bodyIdB = ScopedMemberId;
    ScopedDefinition.base.localFrameA.p =
        b2Vec2{InComponent.LocalPosition.X / ScopedPixelsPerMeter, InComponent.LocalPosition.Y / ScopedPixelsPerMeter};
    ScopedDefinition.base.localFrameA.q = b2MakeRot(InComponent.LocalRotation);
    ScopedDefinition.base.localFrameB.p = b2Vec2{0.0f, 0.0f};
    ScopedDefinition.base.localFrameB.q = b2MakeRot(0.0f);
    ScopedDefinition.base.collideConnected = false;
    ScopedDefinition.linearHertz = 0.0f;
    ScopedDefinition.angularHertz = 0.0f;
    const b2JointId ScopedJoint = b2CreateWeldJoint(b2LoadWorldId(MPhysics->WorldId()), &ScopedDefinition);
    if (b2Joint_IsValid(ScopedJoint))
    {
        InComponent.Joint = b2StoreJointId(ScopedJoint);
    }
}

void TransportGroupSystem::EnsureParentJoint(Ecs::Registry& InRegistry, TransportGroupState& InChild,
                                             const TransportGroupState& InParent) noexcept
{
    if (MPhysics == nullptr)
    {
        return;
    }
    TransportGroupComponent* ScopedComponent = InRegistry.try_get<TransportGroupComponent>(InChild.Root);
    if (ScopedComponent == nullptr)
    {
        return;
    }
    if (ScopedComponent->ParentJoint != 0 && !b2Joint_IsValid(b2LoadJointId(ScopedComponent->ParentJoint)))
    {
        ScopedComponent->ParentJoint = 0;
    }
    if (ScopedComponent->ParentJoint != 0)
    {
        return;
    }
    const RigidBody2D* ScopedChildBody = InRegistry.try_get<RigidBody2D>(InChild.Root);
    const RigidBody2D* ScopedParentBody = InRegistry.try_get<RigidBody2D>(InParent.Root);
    if (ScopedChildBody == nullptr || ScopedParentBody == nullptr || ScopedChildBody->Body == 0 ||
        ScopedParentBody->Body == 0)
    {
        return;
    }
    Transform2D ScopedChildPose{};
    Transform2D ScopedParentPose{};
    if (!ReadEntityPose(InRegistry, InChild.Root, &ScopedChildPose) ||
        !ReadEntityPose(InRegistry, InParent.Root, &ScopedParentPose))
    {
        return;
    }
    const b2BodyId ScopedChildId = b2LoadBodyId(ScopedChildBody->Body);
    const b2BodyId ScopedParentId = b2LoadBodyId(ScopedParentBody->Body);
    if (!b2Body_IsValid(ScopedChildId) || !b2Body_IsValid(ScopedParentId))
    {
        return;
    }
    const float ScopedPixelsPerMeter = std::max(MPhysics->PixelsPerMeter(), 1.0e-3f);
    const Vector2D ScopedLocalPosition = InverseTransformPoint(ScopedParentPose, ScopedChildPose.Position);
    const float ScopedLocalRotation = NormalizeAngle(ScopedChildPose.Rotation - ScopedParentPose.Rotation);
    b2WeldJointDef ScopedDefinition = b2DefaultWeldJointDef();
    ScopedDefinition.base.bodyIdA = ScopedParentId;
    ScopedDefinition.base.bodyIdB = ScopedChildId;
    ScopedDefinition.base.localFrameA.p =
        b2Vec2{ScopedLocalPosition.X / ScopedPixelsPerMeter, ScopedLocalPosition.Y / ScopedPixelsPerMeter};
    ScopedDefinition.base.localFrameA.q = b2MakeRot(ScopedLocalRotation);
    ScopedDefinition.base.localFrameB.p = b2Vec2{0.0f, 0.0f};
    ScopedDefinition.base.localFrameB.q = b2MakeRot(0.0f);
    ScopedDefinition.base.collideConnected = false;
    ScopedDefinition.linearHertz = 0.0f;
    ScopedDefinition.angularHertz = 0.0f;
    const b2JointId ScopedJoint = b2CreateWeldJoint(b2LoadWorldId(MPhysics->WorldId()), &ScopedDefinition);
    if (b2Joint_IsValid(ScopedJoint))
    {
        ScopedComponent->ParentJoint = b2StoreJointId(ScopedJoint);
    }
}

void TransportGroupSystem::ApplyRootPolicy(Ecs::Registry& InRegistry, PhysicsSystem& InPhysics,
                                           const TransportGroupState& InGroup) noexcept
{
    RigidBody2D* ScopedBody = InRegistry.try_get<RigidBody2D>(InGroup.Root);
    if (ScopedBody == nullptr)
    {
        return;
    }
    InPhysics.SetFixedRotation(InRegistry, InGroup.Root, InGroup.RotationPolicy == kTransportRotationFixed);
    EnableHighSpeed(InPhysics, InRegistry, InGroup.Root, InGroup.Velocity);
}

void TransportGroupSystem::EnableHighSpeed(PhysicsSystem& InPhysics, Ecs::Registry& InRegistry, Entity InEntity,
                                           Vector2D InVelocity) noexcept
{
    // CCD is switched with the current speed: leaving the bullet flag latched on
    // after one fast frame changed contact behaviour for the rest of the body's
    // life.
    const bool ScopedHighSpeed =
        InVelocity.LengthSquared() >= kTransportHighSpeedPixelsPerSecond * kTransportHighSpeedPixelsPerSecond;
    InPhysics.SetBullet(InRegistry, InEntity, ScopedHighSpeed);
}

void TransportGroupSystem::Clear() noexcept
{
    MGroups.fill(TransportGroupState{});
    MGroupCount = 0;
}

bool TransportGroupSystem::CreateGroup(Ecs::Registry& InRegistry, Entity InRoot, std::uint32_t InGroupId,
                                       std::uint32_t InParentGroupId, std::uint32_t InRotationPolicy,
                                       std::uint32_t InFlags) noexcept
{
    if (InGroupId == 0 || !InRegistry.valid(InRoot) || InRegistry.try_get<Transform2D>(InRoot) == nullptr)
    {
        return false;
    }
    auto ScopedExistingView = InRegistry.view<TransportGroupComponent>();
    for (const Entity ScopedExistingEntity : ScopedExistingView)
    {
        if (ScopedExistingView.get<TransportGroupComponent>(ScopedExistingEntity).GroupId == InGroupId &&
            ScopedExistingEntity != InRoot)
        {
            return false;
        }
    }
    TransportGroupComponent* ScopedComponent = InRegistry.try_get<TransportGroupComponent>(InRoot);
    if (ScopedComponent == nullptr)
    {
        ScopedComponent = &InRegistry.emplace<TransportGroupComponent>(InRoot);
    }
    ScopedComponent->GroupId = InGroupId;
    ScopedComponent->ParentGroupId = InParentGroupId;
    ScopedComponent->RotationPolicy = InRotationPolicy;
    ScopedComponent->Flags = InFlags;
    return true;
}

bool TransportGroupSystem::DestroyGroup(Ecs::Registry& InRegistry, std::uint32_t InGroupId) noexcept
{
    const Entity ScopedRoot = FindGroupRoot(InRegistry, InGroupId);
    if (ScopedRoot == entt::null)
    {
        return false;
    }
    std::array<Entity, kTransportMaxMembers> ScopedMembers{};
    std::uint32_t ScopedCount = 0;
    auto ScopedView = InRegistry.view<CarrierMemberComponent>();
    for (const Entity ScopedEntity : ScopedView)
    {
        if (ScopedCount < ScopedMembers.size() &&
            ScopedView.get<CarrierMemberComponent>(ScopedEntity).GroupId == InGroupId)
        {
            ScopedMembers[ScopedCount++] = ScopedEntity;
        }
    }
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedCount; ++ScopedIndex)
    {
        (void)Detach(InRegistry, ScopedMembers[ScopedIndex], false);
    }
    if (TransportGroupComponent* ScopedGroupComponent = InRegistry.try_get<TransportGroupComponent>(ScopedRoot);
        ScopedGroupComponent != nullptr)
    {
        DestroyJoint(ScopedGroupComponent->ParentJoint);
        ScopedGroupComponent->ParentJoint = 0;
    }
    // A destroyed parent must not leave a child group welded to a dead body.
    auto ScopedChildView = InRegistry.view<TransportGroupComponent>();
    for (const Entity ScopedChild : ScopedChildView)
    {
        TransportGroupComponent& ScopedChildComponent = ScopedChildView.get<TransportGroupComponent>(ScopedChild);
        if (ScopedChildComponent.ParentGroupId == InGroupId)
        {
            DestroyJoint(ScopedChildComponent.ParentJoint);
            ScopedChildComponent.ParentJoint = 0;
            ScopedChildComponent.ParentGroupId = 0;
        }
    }
    InRegistry.remove<TransportGroupComponent>(ScopedRoot);
    return true;
}

bool TransportGroupSystem::SetParentGroup(Ecs::Registry& InRegistry, std::uint32_t InGroupId,
                                          std::uint32_t InParentGroupId) noexcept
{
    if (InGroupId == 0 || InGroupId == InParentGroupId)
    {
        return false;
    }
    const Entity ScopedRoot = FindGroupRoot(InRegistry, InGroupId);
    if (ScopedRoot == entt::null)
    {
        return false;
    }
    if (InParentGroupId != 0)
    {
        if (FindGroupRoot(InRegistry, InParentGroupId) == entt::null)
        {
            return false;
        }
        if (ParentChainContains(InRegistry, InParentGroupId, InGroupId))
        {
            return false;
        }
    }
    TransportGroupComponent* ScopedComponent = InRegistry.try_get<TransportGroupComponent>(ScopedRoot);
    if (ScopedComponent == nullptr)
    {
        return false;
    }
    DestroyJoint(ScopedComponent->ParentJoint);
    ScopedComponent->ParentJoint = 0;
    ScopedComponent->ParentGroupId = InParentGroupId;
    return true;
}

bool TransportGroupSystem::Attach(Ecs::Registry& InRegistry, Entity InMember, std::uint32_t InGroupId,
                                  std::uint32_t InMode, std::uint32_t InFlags) noexcept
{
    if (InGroupId == 0 || !InRegistry.valid(InMember))
    {
        return false;
    }
    const Entity ScopedRoot = FindGroupRoot(InRegistry, InGroupId);
    if (ScopedRoot == entt::null)
    {
        return false;
    }
    Transform2D ScopedRootTransform{};
    Transform2D ScopedMemberTransform{};
    if (!ReadEntityPose(InRegistry, ScopedRoot, &ScopedRootTransform) ||
        !ReadEntityPose(InRegistry, InMember, &ScopedMemberTransform))
    {
        return false;
    }
    CarrierMemberComponent* ScopedComponent = InRegistry.try_get<CarrierMemberComponent>(InMember);
    if (ScopedComponent == nullptr)
    {
        ScopedComponent = &InRegistry.emplace<CarrierMemberComponent>(InMember);
    }
    ScopedComponent->GroupId = InGroupId;
    ScopedComponent->Mode = InMode;
    ScopedComponent->Flags = InFlags;
    ScopedComponent->LocalPosition = InverseTransformPoint(ScopedRootTransform, ScopedMemberTransform.Position);
    ScopedComponent->LocalRotation = ScopedMemberTransform.Rotation - ScopedRootTransform.Rotation;
    ScopedComponent->RelativeVelocity = Vector2D{};
    ScopedComponent->RelativeAngularVelocity = 0.0f;
    if (const RigidBody2D* ScopedBody = InRegistry.try_get<RigidBody2D>(InMember); ScopedBody != nullptr)
    {
        if (const TransportGroupState* ScopedGroup = FindGroup(InGroupId); ScopedGroup != nullptr)
        {
            const Vector2D ScopedCarry =
                MemberCarryVelocity(*ScopedGroup, ScopedRootTransform, ScopedMemberTransform.Position);
            ScopedComponent->RelativeVelocity = ScopedBody->LinearVelocity - ScopedCarry;
            ScopedComponent->RelativeAngularVelocity =
                ScopedBody->AngularVelocity -
                (InFlags & kCarrierFlagInheritRotation ? ScopedGroup->AngularVelocity : 0.0f);
        }
    }
    return true;
}

bool TransportGroupSystem::Detach(Ecs::Registry& InRegistry, Entity InMember, bool InPreserveVelocity) noexcept
{
    CarrierMemberComponent* ScopedComponent = InRegistry.try_get<CarrierMemberComponent>(InMember);
    if (ScopedComponent == nullptr)
    {
        return false;
    }
    DestroyJoint(ScopedComponent->Joint);
    ScopedComponent->Joint = 0;
    const TransportGroupState* ScopedGroup = FindGroup(ScopedComponent->GroupId);
    RigidBody2D* ScopedBody = InRegistry.try_get<RigidBody2D>(InMember);
    Transform2D ScopedMemberTransform{};
    const bool ScopedHasMemberPose = ReadEntityPose(InRegistry, InMember, &ScopedMemberTransform);
    if (ScopedBody != nullptr && ScopedHasMemberPose)
    {
        if (!InPreserveVelocity)
        {
            ScopedBody->LinearVelocity = Vector2D{};
            ScopedBody->AngularVelocity = 0.0f;
            ScopedBody->Flags |= RigidBodyAwake;
        }
        else if (ScopedComponent->Mode == kCarrierModeRigid)
        {
            Vector2D ScopedVelocity{};
            float ScopedAngular = 0.0f;
            if (ScopedGroup != nullptr)
            {
                Transform2D ScopedRoot{};
                if (ReadEntityPose(InRegistry, ScopedGroup->Root, &ScopedRoot))
                {
                    ScopedVelocity = MemberCarryVelocity(*ScopedGroup, ScopedRoot, ScopedMemberTransform.Position);
                    ScopedAngular = ScopedGroup->AngularVelocity;
                }
            }
            ScopedBody->LinearVelocity = ScopedVelocity;
            ScopedBody->AngularVelocity = ScopedAngular;
            ScopedBody->Flags |= RigidBodyAwake;
        }
    }
    InRegistry.remove<CarrierMemberComponent>(InMember);
    return true;
}

void TransportGroupSystem::PrePhysics(Ecs::Registry& InRegistry, PhysicsSystem& InPhysics, float InFixedDt) noexcept
{
    RefreshGroups(InRegistry);
    MPhysics = &InPhysics;
    auto ScopedMotionView = InRegistry.view<TransportMotion>();
    for (const Entity ScopedMotionEntity : ScopedMotionView)
    {
        AdvanceMotion(InRegistry, ScopedMotionEntity, InFixedDt);
    }
    if (InFixedDt <= 0.0f)
    {
        return;
    }
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < MGroupCount; ++ScopedIndex)
    {
        if (ProcessGroup(InRegistry, InPhysics, ScopedIndex, 0, InFixedDt))
        {
            ApplyRootPolicy(InRegistry, InPhysics, MGroups[ScopedIndex]);
        }
    }

    auto ScopedView = InRegistry.view<CarrierMemberComponent>();
    for (const Entity ScopedEntity : ScopedView)
    {
        CarrierMemberComponent& ScopedMember = ScopedView.get<CarrierMemberComponent>(ScopedEntity);
        TransportGroupState* ScopedGroup = FindGroup(ScopedMember.GroupId);
        if (ScopedGroup == nullptr || !ScopedGroup->Active)
        {
            continue;
        }
        Transform2D ScopedRootPose{};
        Transform2D ScopedMemberPose{};
        if (!ReadEntityPose(InRegistry, ScopedGroup->Root, &ScopedRootPose) ||
            !ReadEntityPose(InRegistry, ScopedEntity, &ScopedMemberPose))
        {
            continue;
        }
        const Vector2D ScopedCarry = MemberCarryVelocity(*ScopedGroup, ScopedRootPose, ScopedMemberPose.Position);
        RigidBody2D* ScopedBody = InRegistry.try_get<RigidBody2D>(ScopedEntity);
        if (ScopedMember.Mode == kCarrierModeRigid)
        {
            EnsureRigidJoint(InRegistry, ScopedEntity, ScopedMember, *ScopedGroup);
            if (ScopedMember.Joint != 0)
            {
                EnableHighSpeed(InPhysics, InRegistry, ScopedEntity, ScopedCarry);
                continue;
            }
            Transform2D ScopedDesired = Compose(ScopedRootPose, ScopedMember.LocalPosition, ScopedMember.LocalRotation);
            if ((ScopedMember.Flags & kCarrierFlagInheritRotation) == 0)
            {
                ScopedDesired.Rotation = ScopedMemberPose.Rotation;
            }
            WriteEntityPose(InRegistry, ScopedEntity, ScopedDesired, ScopedBody != nullptr);
            if (ScopedBody != nullptr)
            {
                ScopedBody->Flags |= RigidBodyAwake;
                InPhysics.SetLinearVelocity(InRegistry, ScopedEntity, ScopedCarry, true);
                InPhysics.SetAngularVelocity(
                    InRegistry, ScopedEntity,
                    (ScopedMember.Flags & kCarrierFlagInheritRotation) != 0 ? ScopedGroup->AngularVelocity : 0.0f,
                    true);
            }
            continue;
        }
        if (ScopedBody != nullptr)
        {
            if (ScopedMember.Mode == kCarrierModeSupport || ScopedMember.Mode == kCarrierModeConveyor)
            {
                const bool ScopedSupported =
                    IsMemberSupported(InRegistry, ScopedEntity, ScopedGroup->Root, ScopedMemberPose.Position);
                if (ScopedSupported)
                {
                    ScopedMember.Flags |= kCarrierFlagGrounded;
                    const TransportSurface& ScopedSurface = SurfaceFor(InRegistry, ScopedGroup->Root);
                    Vector2D ScopedTarget = ScopedCarry;
                    if (ScopedMember.Mode == kCarrierModeSupport)
                    {
                        ScopedTarget += ScopedMember.RelativeVelocity;
                    }
                    else
                    {
                        ScopedTarget.X += ScopedSurface.TangentSpeed;
                    }
                    // Exponential approach with the authored friction per 60 Hz
                    // reference step: dt-correct and stable at any fixed rate.
                    const float ScopedBlend =
                        std::clamp(1.0f - std::exp(-ScopedSurface.Friction * 60.0f * InFixedDt), 0.0f, 1.0f);
                    const float ScopedTargetX =
                        ScopedBody->LinearVelocity.X +
                        (ScopedTarget.X - ScopedBody->LinearVelocity.X) * ScopedBlend;
                    const float ScopedTargetY =
                        (!ScopedSurface.AllowJump && ScopedBody->LinearVelocity.Y > ScopedCarry.Y)
                            ? ScopedCarry.Y
                            : (ScopedCarry.Y > ScopedBody->LinearVelocity.Y ? ScopedCarry.Y
                                                                             : ScopedBody->LinearVelocity.Y);
                    EnableHighSpeed(InPhysics, InRegistry, ScopedEntity, ScopedTarget);
                    InPhysics.SetLinearVelocity(InRegistry, ScopedEntity,
                                                Vector2D{ScopedTargetX, ScopedTargetY}, true);
                    const float ScopedAngular = (ScopedMember.Flags & kCarrierFlagInheritRotation) != 0
                                                    ? ScopedGroup->AngularVelocity
                                                    : ScopedMember.RelativeAngularVelocity;
                    InPhysics.SetAngularVelocity(InRegistry, ScopedEntity, ScopedAngular, true);
                    continue;
                }
                ScopedMember.Flags &= ~kCarrierFlagGrounded;
            }
            const Vector2D ScopedVelocity = ScopedCarry + ScopedMember.RelativeVelocity;
            EnableHighSpeed(InPhysics, InRegistry, ScopedEntity, ScopedVelocity);
            InPhysics.SetLinearVelocity(InRegistry, ScopedEntity, ScopedVelocity, true);
            const float ScopedAngular = (ScopedMember.Flags & kCarrierFlagInheritRotation) != 0
                                            ? ScopedGroup->AngularVelocity + ScopedMember.RelativeAngularVelocity
                                            : ScopedMember.RelativeAngularVelocity;
            InPhysics.SetAngularVelocity(InRegistry, ScopedEntity, ScopedAngular, true);
        }
        else
        {
            ScopedMemberPose.Position += ScopedGroup->DeltaPosition;
            if ((ScopedMember.Flags & kCarrierFlagInheritRotation) != 0)
            {
                ScopedMemberPose.Rotation = NormalizeAngle(ScopedMemberPose.Rotation + ScopedGroup->DeltaRotation);
            }
            WriteEntityPose(InRegistry, ScopedEntity, ScopedMemberPose, false);
        }
    }
}

void TransportGroupSystem::PostPhysics(Ecs::Registry& InRegistry, PhysicsSystem& InPhysics, float InFixedDt) noexcept
{
    if (InFixedDt <= 0.0f)
    {
        return;
    }
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < MGroupCount; ++ScopedIndex)
    {
        TransportGroupState& ScopedGroup = MGroups[ScopedIndex];
        if (!ScopedGroup.Active)
        {
            continue;
        }
        Transform2D ScopedRootPose{};
        if (!ReadEntityPose(InRegistry, ScopedGroup.Root, &ScopedRootPose))
        {
            continue;
        }
        const RigidBody2D* ScopedBody = InRegistry.try_get<RigidBody2D>(ScopedGroup.Root);
        const Vector2D ScopedDeltaVelocity = (ScopedRootPose.Position - ScopedGroup.PreviousPosition) / InFixedDt;
        const float ScopedDeltaAngular =
            NormalizeAngle(ScopedRootPose.Rotation - ScopedGroup.PreviousRotation) / InFixedDt;
        const bool ScopedHasBody =
            ScopedBody != nullptr && ScopedBody->Body != 0 && ScopedBody->Type != BodyType::Static;
        if (ScopedHasBody)
        {
            ScopedGroup.Velocity = ScopedBody->LinearVelocity;
            ScopedGroup.AngularVelocity = ScopedBody->AngularVelocity;
        }
        else
        {
            ScopedGroup.Velocity = ScopedDeltaVelocity;
            ScopedGroup.AngularVelocity = ScopedDeltaAngular;
        }
        const TransportGroupComponent* ScopedComponent = InRegistry.try_get<TransportGroupComponent>(ScopedGroup.Root);
        const bool ScopedParentJoint = ScopedComponent != nullptr && ScopedComponent->ParentJoint != 0;
        const TransportGroupState* ScopedParent = FindGroup(ScopedGroup.ParentGroupId);
        if (ScopedHasBody && !ScopedParentJoint && ScopedParent != nullptr && ScopedParent->Active)
        {
            ScopedGroup.Velocity += ScopedParent->Velocity;
            ScopedGroup.AngularVelocity += ScopedParent->AngularVelocity;
        }
    }

    std::array<Entity, kTransportMaxMembers> ScopedToDetach{};
    std::uint32_t ScopedDetachCount = 0;
    auto ScopedView = InRegistry.view<CarrierMemberComponent>();
    for (const Entity ScopedEntity : ScopedView)
    {
        CarrierMemberComponent& ScopedMember = ScopedView.get<CarrierMemberComponent>(ScopedEntity);
        TransportGroupState* ScopedGroup = FindGroup(ScopedMember.GroupId);
        if (ScopedGroup == nullptr)
        {
            continue;
        }
        Transform2D ScopedRootPose{};
        Transform2D ScopedMemberPose{};
        if (!ReadEntityPose(InRegistry, ScopedGroup->Root, &ScopedRootPose) ||
            !ReadEntityPose(InRegistry, ScopedEntity, &ScopedMemberPose))
        {
            continue;
        }
        ScopedMember.LocalPosition = InverseTransformPoint(ScopedRootPose, ScopedMemberPose.Position);
        ScopedMember.LocalRotation = NormalizeAngle(ScopedMemberPose.Rotation - ScopedRootPose.Rotation);
        const RigidBody2D* ScopedBody = InRegistry.try_get<RigidBody2D>(ScopedEntity);
        if (ScopedBody != nullptr)
        {
            const Vector2D ScopedCarry = MemberCarryVelocity(*ScopedGroup, ScopedRootPose, ScopedMemberPose.Position);
            ScopedMember.RelativeVelocity = ScopedBody->LinearVelocity - ScopedCarry;
            ScopedMember.RelativeAngularVelocity =
                ScopedBody->AngularVelocity -
                ((ScopedMember.Flags & kCarrierFlagInheritRotation) != 0 ? ScopedGroup->AngularVelocity : 0.0f);
        }
        else
        {
            ScopedMember.RelativeVelocity = Vector2D{};
            ScopedMember.RelativeAngularVelocity = 0.0f;
        }
        const bool ScopedRequested = (ScopedMember.Flags & kCarrierFlagDetachRequested) != 0;
        const bool ScopedOutside = (ScopedGroup->Flags & kTransportGroupAutoDetachOutside) != 0 &&
                                   OutsideBounds(*ScopedGroup, ScopedMember.LocalPosition);
        if (ScopedRequested || ScopedOutside)
        {
            if (ScopedDetachCount < ScopedToDetach.size())
            {
                ScopedToDetach[ScopedDetachCount++] = ScopedEntity;
            }
            else
            {
                ++MDroppedDetaches;
            }
        }
    }
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedDetachCount; ++ScopedIndex)
    {
        (void)Detach(InRegistry, ScopedToDetach[ScopedIndex], true);
    }

    for (TransportGroupState& ScopedGroup : MGroups)
    {
        if (!ScopedGroup.Active)
        {
            continue;
        }
        Transform2D ScopedRootPose{};
        if (ReadEntityPose(InRegistry, ScopedGroup.Root, &ScopedRootPose))
        {
            ScopedGroup.PreviousPosition = ScopedRootPose.Position;
            ScopedGroup.PreviousRotation = ScopedRootPose.Rotation;
        }
    }
    (void)InPhysics;
}

const TransportGroupState* TransportGroupSystem::Group(std::uint32_t InGroupId) const noexcept
{
    return FindGroup(InGroupId);
}

TransportGroupState* TransportGroupSystem::FindGroup(std::uint32_t InGroupId) noexcept
{
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < MGroupCount; ++ScopedIndex)
    {
        if (MGroups[ScopedIndex].Active && MGroups[ScopedIndex].GroupId == InGroupId)
        {
            return &MGroups[ScopedIndex];
        }
    }
    return nullptr;
}

const TransportGroupState* TransportGroupSystem::FindGroup(std::uint32_t InGroupId) const noexcept
{
    if (InGroupId == 0)
    {
        return nullptr;
    }
    std::uint32_t ScopedCursor = (InGroupId * 2654435761u) & (kGroupIndexSize - 1u);
    for (std::uint32_t ScopedProbe = 0; ScopedProbe < kGroupIndexSize; ++ScopedProbe)
    {
        const GroupIndexEntry& ScopedEntry = MGroupIndex[ScopedCursor];
        if (ScopedEntry.GroupId == 0)
        {
            return nullptr;
        }
        if (ScopedEntry.GroupId == InGroupId && ScopedEntry.Slot < MGroupCount &&
            MGroups[ScopedEntry.Slot].Active && MGroups[ScopedEntry.Slot].GroupId == InGroupId)
        {
            return &MGroups[ScopedEntry.Slot];
        }
        ScopedCursor = (ScopedCursor + 1u) & (kGroupIndexSize - 1u);
    }
    return nullptr;
}

void TransportGroupSystem::RefreshGroups(Ecs::Registry& InRegistry) noexcept
{
    std::array<TransportGroupState, kTransportMaxGroups> ScopedNew{};
    std::uint32_t ScopedCount = 0;
    auto ScopedView = InRegistry.view<TransportGroupComponent>();
    for (const Entity ScopedEntity : ScopedView)
    {
        const TransportGroupComponent& ScopedComponent = ScopedView.get<TransportGroupComponent>(ScopedEntity);
        Transform2D ScopedTransform{};
        if (!ReadEntityPose(InRegistry, ScopedEntity, &ScopedTransform))
        {
            continue;
        }
        if (ScopedComponent.GroupId == 0 || ScopedCount >= ScopedNew.size())
        {
            continue;
        }
        bool ScopedDuplicate = false;
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedCount; ++ScopedIndex)
        {
            if (ScopedNew[ScopedIndex].GroupId == ScopedComponent.GroupId)
            {
                ScopedDuplicate = true;
                break;
            }
        }
        if (ScopedDuplicate)
        {
            continue;
        }
        TransportGroupState ScopedState{};
        ScopedState.Active = true;
        ScopedState.Root = ScopedEntity;
        ScopedState.GroupId = ScopedComponent.GroupId;
        ScopedState.ParentGroupId = ScopedComponent.ParentGroupId;
        ScopedState.RotationPolicy = ScopedComponent.RotationPolicy;
        ScopedState.Flags = ScopedComponent.Flags;
        ScopedState.BoundsMin = ScopedComponent.BoundsMin;
        ScopedState.BoundsMax = ScopedComponent.BoundsMax;
        ScopedState.PreviousPosition = ScopedTransform.Position;
        ScopedState.PreviousRotation = ScopedTransform.Rotation;
        for (const TransportGroupState& ScopedOld : MGroups)
        {
            if (ScopedOld.Active && ScopedOld.GroupId == ScopedComponent.GroupId && ScopedOld.Root == ScopedEntity)
            {
                ScopedState.PreviousPosition = ScopedOld.PreviousPosition;
                ScopedState.PreviousRotation = ScopedOld.PreviousRotation;
                ScopedState.Velocity = ScopedOld.Velocity;
                ScopedState.AngularVelocity = ScopedOld.AngularVelocity;
                break;
            }
        }
        ScopedNew[ScopedCount++] = ScopedState;
    }
    MGroups = ScopedNew;
    MGroupCount = ScopedCount;
    // Rebuild the open-addressing index in the same pass as the group array.
    MGroupIndex.fill(GroupIndexEntry{});
    for (std::uint32_t ScopedI = 0; ScopedI < MGroupCount; ++ScopedI)
    {
        if (!MGroups[ScopedI].Active)
        {
            continue;
        }
        std::uint32_t ScopedCursor = (MGroups[ScopedI].GroupId * 2654435761u) & (kGroupIndexSize - 1u);
        while (MGroupIndex[ScopedCursor].GroupId != 0)
        {
            ScopedCursor = (ScopedCursor + 1u) & (kGroupIndexSize - 1u);
        }
        MGroupIndex[ScopedCursor] = GroupIndexEntry{MGroups[ScopedI].GroupId, static_cast<std::uint16_t>(ScopedI)};
    }
}

bool TransportGroupSystem::ProcessGroup(Ecs::Registry& InRegistry, PhysicsSystem& InPhysics, std::uint32_t InIndex,
                                        std::uint32_t InDepth, float InFixedDt) noexcept
{
    if (InIndex >= MGroupCount || InDepth > kTransportMaxDepth)
    {
        return false;
    }
    TransportGroupState& ScopedGroup = MGroups[InIndex];
    if (!ScopedGroup.Active)
    {
        return false;
    }
    if (ScopedGroup.Processed)
    {
        return true;
    }
    Transform2D ScopedRootPose{};
    if (!ReadEntityPose(InRegistry, ScopedGroup.Root, &ScopedRootPose))
    {
        ScopedGroup.Active = false;
        return false;
    }

    if (ScopedGroup.ParentGroupId != 0)
    {
        TransportGroupState* ScopedParent = FindGroup(ScopedGroup.ParentGroupId);
        if (ScopedParent != nullptr && ScopedParent->Active)
        {
            const std::uint32_t ScopedParentIndex = static_cast<std::uint32_t>(ScopedParent - MGroups.data());
            if (ProcessGroup(InRegistry, InPhysics, ScopedParentIndex, InDepth + 1, InFixedDt))
            {
                EnsureParentJoint(InRegistry, ScopedGroup, *ScopedParent);
                const TransportGroupComponent* ScopedComponent =
                    InRegistry.try_get<TransportGroupComponent>(ScopedGroup.Root);
                const bool ScopedParentJoint = ScopedComponent != nullptr && ScopedComponent->ParentJoint != 0;
                if (!ScopedParentJoint)
                {
                    Transform2D ScopedParentPose{};
                    if (ReadEntityPose(InRegistry, ScopedParent->Root, &ScopedParentPose))
                    {
                        Transform2D ScopedOldParent{};
                        ScopedOldParent.Position = ScopedParent->PreviousPosition;
                        ScopedOldParent.Rotation = ScopedParent->PreviousRotation;
                        ScopedRootPose = ApplyParentDelta(ScopedOldParent, ScopedParentPose, ScopedRootPose);
                        WriteEntityPose(InRegistry, ScopedGroup.Root, ScopedRootPose, true);
                    }
                }
            }
        }
    }

    ScopedGroup.DeltaPosition = ScopedRootPose.Position - ScopedGroup.PreviousPosition;
    ScopedGroup.DeltaRotation = NormalizeAngle(ScopedRootPose.Rotation - ScopedGroup.PreviousRotation);
    const RigidBody2D* ScopedBody = InRegistry.try_get<RigidBody2D>(ScopedGroup.Root);
    const bool ScopedHasBody = ScopedBody != nullptr && ScopedBody->Body != 0 && ScopedBody->Type != BodyType::Static;
    if (ScopedHasBody)
    {
        ScopedGroup.Velocity = ScopedBody->LinearVelocity;
        ScopedGroup.AngularVelocity = ScopedBody->AngularVelocity;
    }
    else
    {
        ScopedGroup.Velocity = ScopedGroup.DeltaPosition / InFixedDt;
        ScopedGroup.AngularVelocity = ScopedGroup.DeltaRotation / InFixedDt;
    }
    const TransportGroupComponent* ScopedComponent = InRegistry.try_get<TransportGroupComponent>(ScopedGroup.Root);
    const bool ScopedParentJoint = ScopedComponent != nullptr && ScopedComponent->ParentJoint != 0;
    const TransportGroupState* ScopedParent = FindGroup(ScopedGroup.ParentGroupId);
    if (ScopedHasBody && !ScopedParentJoint && ScopedParent != nullptr && ScopedParent->Active)
    {
        ScopedGroup.Velocity += ScopedParent->Velocity;
        ScopedGroup.AngularVelocity += ScopedParent->AngularVelocity;
    }
    ScopedGroup.Processed = true;
    return true;
}

bool TransportGroupSystem::OutsideBounds(const TransportGroupState& InGroup, Vector2D InLocalPoint) const noexcept
{
    return InLocalPoint.X < InGroup.BoundsMin.X || InLocalPoint.X > InGroup.BoundsMax.X ||
           InLocalPoint.Y < InGroup.BoundsMin.Y || InLocalPoint.Y > InGroup.BoundsMax.Y;
}

Entity TransportGroupSystem::FindGroupRoot(Ecs::Registry& InRegistry, std::uint32_t InGroupId) noexcept
{
    auto ScopedView = InRegistry.view<TransportGroupComponent>();
    for (const Entity ScopedEntity : ScopedView)
    {
        if (ScopedView.get<TransportGroupComponent>(ScopedEntity).GroupId == InGroupId)
        {
            return ScopedEntity;
        }
    }
    return entt::null;
}

bool TransportGroupSystem::ParentChainContains(Ecs::Registry& InRegistry, std::uint32_t InStartGroupId,
                                               std::uint32_t InWantedGroupId) const noexcept
{
    std::uint32_t ScopedCurrent = InStartGroupId;
    for (std::uint32_t ScopedDepth = 0; ScopedDepth < kTransportMaxDepth; ++ScopedDepth)
    {
        if (ScopedCurrent == InWantedGroupId)
        {
            return true;
        }
        if (ScopedCurrent == 0)
        {
            return false;
        }
        const TransportGroupComponent* ScopedComponent = FindGroupComponent(InRegistry, ScopedCurrent);
        if (ScopedComponent == nullptr)
        {
            return false;
        }
        ScopedCurrent = ScopedComponent->ParentGroupId;
    }
    return false;
}
}
