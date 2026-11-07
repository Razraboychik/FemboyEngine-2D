// FemboyEngine2D - Box2D v3 joint graph construction and break handling.
#include "Engine/Physics/PhysicsJoints.hpp"

#include "Engine/Core/SimMath.hpp"
#include "Engine/ECS/Components.hpp"
#include "Engine/ECS/Schedule.hpp"

#include <box2d/box2d.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdint>

namespace Fby
{
namespace
{

// Box2D works in metres; gameplay in pixels. PhysicsSystem is the single
// conversion point for both directions.
[[nodiscard]] Vector2D ToPixels(b2Pos InValue, float InPixelsPerMeter) noexcept
{
    return Vector2D{static_cast<float>(InValue.x) * InPixelsPerMeter, static_cast<float>(InValue.y) * InPixelsPerMeter};
}

[[nodiscard]] b2Pos ToBox(Vector2D InValue, float InPixelsPerMeter) noexcept
{
    b2Pos ScopedResult{};
    ScopedResult.x = static_cast<decltype(ScopedResult.x)>(InValue.X / InPixelsPerMeter);
    ScopedResult.y = static_cast<decltype(ScopedResult.y)>(InValue.Y / InPixelsPerMeter);
    return ScopedResult;
}

// Same encoding PhysicsSystem uses for b2Body_GetUserData: entity zero is
// stored as one so a null pointer never maps to a valid entity.
[[nodiscard]] void* BodyUserData(Entity InEntity) noexcept
{
    return reinterpret_cast<void*>(static_cast<std::uintptr_t>(entt::to_integral(InEntity)) + 1u);
}

[[nodiscard]] bool BodyIsLive(std::uint64_t InPacked) noexcept
{
    return InPacked != 0 && b2Body_IsValid(b2LoadBodyId(InPacked));
}

// A capsule link is a line of two semicircles. `halfLength` is the distance
// from the body centre to either circle centre.
[[nodiscard]] b2Capsule MakeCapsule(float InHalfLength, float InRadius) noexcept
{
    b2Capsule ScopedCapsule{};
    ScopedCapsule.center1 = b2Vec2{-InHalfLength, 0.0f};
    ScopedCapsule.center2 = b2Vec2{InHalfLength, 0.0f};
    ScopedCapsule.radius = InRadius;
    return ScopedCapsule;
}

[[nodiscard]] b2Transform JointFrame(b2Vec2 InLocalPoint) noexcept
{
    b2Transform ScopedFrame{};
    ScopedFrame.p = InLocalPoint;
    ScopedFrame.q = b2Rot_identity;
    return ScopedFrame;
}

// A connection between two adjacent links takes the softer material, so a rope
// tied to a chain link stays free to rotate instead of welding to the hinge.
[[nodiscard]] JointConnectionKind ConnectionKindFor(JointChainKind InLeftKind, std::uint32_t InLeftFlags,
                                                    JointChainKind InRightKind, std::uint32_t InRightFlags) noexcept
{
    const bool ScopedRope = InLeftKind == JointChainKind::Rope || InRightKind == JointChainKind::Rope;
    if (ScopedRope)
    {
        const bool ScopedSpring = (InLeftKind == JointChainKind::Rope && (InLeftFlags & JointChainSpringRope) != 0) ||
                                  (InRightKind == JointChainKind::Rope && (InRightFlags & JointChainSpringRope) != 0);
        return ScopedSpring ? JointConnectionKind::SpringRope : JointConnectionKind::Rope;
    }
    const bool ScopedSpring = (InLeftFlags & JointChainSpringHinge) != 0 || (InRightFlags & JointChainSpringHinge) != 0;
    return ScopedSpring ? JointConnectionKind::SpringHinge : JointConnectionKind::Hinge;
}

// The weakest material governs the connection it participates in. A zero keeps
// the connection unbreakable unless both sides or the chain default ask for a
// limit, so a rope spliced into a chain breaks before the chain does.
[[nodiscard]] float CombinedBreak(float InLeft, float InRight, float InDefault) noexcept
{
    if (InLeft > 0.0f && InRight > 0.0f)
    {
        return std::min(InLeft, InRight);
    }
    if (InLeft > 0.0f)
    {
        return InLeft;
    }
    if (InRight > 0.0f)
    {
        return InRight;
    }
    return InDefault;
}

[[nodiscard]] float SegmentDistanceSquared(Vector2D InPoint, Vector2D InFrom, Vector2D InTo) noexcept
{
    const Vector2D ScopedDelta = InTo - InFrom;
    const float ScopedLengthSquared = ScopedDelta.LengthSquared();
    if (ScopedLengthSquared <= 1.0e-8f)
    {
        return (InPoint - InFrom).LengthSquared();
    }
    float ScopedT =
        ((InPoint.X - InFrom.X) * ScopedDelta.X + (InPoint.Y - InFrom.Y) * ScopedDelta.Y) / ScopedLengthSquared;
    ScopedT = std::clamp(ScopedT, 0.0f, 1.0f);
    const Vector2D ScopedClosest = InFrom + ScopedDelta * ScopedT;
    return (InPoint - ScopedClosest).LengthSquared();
}

// Constraint reaction in newtons and newton metres. Box2D reports the last
// solved impulse times 1/h, so the value is valid after a step and before the
// joint is destroyed.
void QueryReaction(std::uint64_t InPackedJoint, float* OutForce, float* OutTorque) noexcept
{
    *OutForce = 0.0f;
    *OutTorque = 0.0f;
    if (InPackedJoint == 0)
    {
        return;
    }
    const b2JointId ScopedJoint = b2LoadJointId(InPackedJoint);
    if (!b2Joint_IsValid(ScopedJoint))
    {
        return;
    }
    const b2Vec2 ScopedForce = b2Joint_GetConstraintForce(ScopedJoint);
    *OutForce = std::sqrt(ScopedForce.x * ScopedForce.x + ScopedForce.y * ScopedForce.y);
    *OutTorque = std::fabs(b2Joint_GetConstraintTorque(ScopedJoint));
}
}

PhysicsJointSystem::~PhysicsJointSystem()
{
    // The world may already be gone, so the destructor only drops the binding.
    MStorage.reset();
    MPhysics = nullptr;
    MLiveChains = 0;
}

bool PhysicsJointSystem::Initialize(PhysicsSystem& InPhysics) noexcept
{
    if (!InPhysics.IsValid() || MLiveChains != 0)
    {
        return false;
    }
    MPhysics = &InPhysics;
    MPhysicsInstanceId = InPhysics.InstanceId();
    MPhysicsGeneration = InPhysics.Generation();
    if (MStorage == nullptr)
    {
        MStorage = std::make_unique<Storage>();
        // The free stack pops from the back, so index zero is handed out first.
        for (std::uint32_t ScopedI = kMaxConnections; ScopedI > 0; --ScopedI)
        {
            MStorage->FreeStack[MStorage->FreeCount++] = ScopedI - 1u;
        }
        MStorage->ActiveCount = 0;
    }
    return true;
}

void PhysicsJointSystem::Shutdown(Ecs::Registry& InRegistry) noexcept
{
    if (MStorage == nullptr)
    {
        MLiveChains = 0;
        MBreakCount = 0;
        MPhysics = nullptr;
        return;
    }
    for (ChainRuntime& ScopedChain : Chains())
    {
        if (ScopedChain.Active)
        {
            DestroyRuntime(InRegistry, ScopedChain);
        }
    }
    MLiveChains = 0;
    MBreakCount = 0;
    MStorage.reset();
    MPhysics = nullptr;
}

bool PhysicsJointSystem::IsValid(JointChainHandle InHandle) const noexcept
{
    return Runtime(InHandle) != nullptr;
}

PhysicsJointSystem::ChainRuntime* PhysicsJointSystem::Runtime(JointChainHandle InHandle) noexcept
{
    if (MStorage == nullptr || InHandle.Index >= kMaxChains)
    {
        return nullptr;
    }
    ChainRuntime& ScopedChain = Chains()[InHandle.Index];
    if (!ScopedChain.Active || ScopedChain.Generation != InHandle.Generation)
    {
        return nullptr;
    }
    return &ScopedChain;
}

const PhysicsJointSystem::ChainRuntime* PhysicsJointSystem::Runtime(JointChainHandle InHandle) const noexcept
{
    if (MStorage == nullptr || InHandle.Index >= kMaxChains)
    {
        return nullptr;
    }
    const ChainRuntime& ScopedChain = Chains()[InHandle.Index];
    if (!ScopedChain.Active || ScopedChain.Generation != InHandle.Generation)
    {
        return nullptr;
    }
    return &ScopedChain;
}

PhysicsJointSystem::ConnectionRuntime* PhysicsJointSystem::Connection(std::uint32_t InIndex) noexcept
{
    if (MStorage == nullptr || InIndex >= kMaxConnections)
    {
        return nullptr;
    }
    ConnectionRuntime& ScopedConnection = MStorage->Connections[InIndex];
    if (!ScopedConnection.Active)
    {
        return nullptr;
    }
    return &ScopedConnection;
}

const PhysicsJointSystem::ConnectionRuntime* PhysicsJointSystem::Connection(std::uint32_t InIndex) const noexcept
{
    if (MStorage == nullptr || InIndex >= kMaxConnections)
    {
        return nullptr;
    }
    const ConnectionRuntime& ScopedConnection = MStorage->Connections[InIndex];
    if (!ScopedConnection.Active)
    {
        return nullptr;
    }
    return &ScopedConnection;
}

PhysicsJointSystem::ConnectionRuntime* PhysicsJointSystem::Find(JointConnectionHandle InHandle) noexcept
{
    ConnectionRuntime* ScopedConnection = Connection(InHandle.Index);
    if (ScopedConnection == nullptr || ScopedConnection->Generation != InHandle.Generation)
    {
        return nullptr;
    }
    return ScopedConnection;
}

const PhysicsJointSystem::ConnectionRuntime* PhysicsJointSystem::Find(JointConnectionHandle InHandle) const noexcept
{
    const ConnectionRuntime* ScopedConnection = Connection(InHandle.Index);
    if (ScopedConnection == nullptr || ScopedConnection->Generation != InHandle.Generation)
    {
        return nullptr;
    }
    return ScopedConnection;
}

std::uint32_t PhysicsJointSystem::AllocateConnection() noexcept
{
    if (MStorage == nullptr || MStorage->FreeCount == 0)
    {
        return JointConnectionHandle::kInvalid;
    }
    const std::uint32_t ScopedIndex = MStorage->FreeStack[--MStorage->FreeCount];
    ConnectionRuntime& ScopedConnection = MStorage->Connections[ScopedIndex];
    const std::uint32_t ScopedGeneration = ScopedConnection.Generation == 0 ? 1u : ScopedConnection.Generation;
    ScopedConnection = ConnectionRuntime{};
    ScopedConnection.Active = true;
    ScopedConnection.Generation = ScopedGeneration;
    MStorage->ActiveSlots[MStorage->ActiveCount++] = ScopedIndex;
    return ScopedIndex;
}

void PhysicsJointSystem::FreeConnection(std::uint32_t InIndex) noexcept
{
    if (MStorage == nullptr || InIndex >= kMaxConnections)
    {
        return;
    }
    ConnectionRuntime& ScopedConnection = MStorage->Connections[InIndex];
    if (!ScopedConnection.Active)
    {
        return;
    }
    ScopedConnection.Joint = 0;
    ScopedConnection.BodyA = 0;
    ScopedConnection.BodyB = 0;
    ScopedConnection.Active = false;
    ScopedConnection.Generation = ScopedConnection.Generation + 1u == 0 ? 1u : ScopedConnection.Generation + 1u;
    // Remove from the compact active list (swap-pop; order has no meaning).
    for (std::uint32_t ScopedCursor = 0; ScopedCursor < MStorage->ActiveCount; ++ScopedCursor)
    {
        if (MStorage->ActiveSlots[ScopedCursor] == InIndex)
        {
            MStorage->ActiveSlots[ScopedCursor] = MStorage->ActiveSlots[--MStorage->ActiveCount];
            break;
        }
    }
    MStorage->FreeStack[MStorage->FreeCount++] = InIndex;
}

bool PhysicsJointSystem::CreateJoint(ConnectionRuntime& InConnection, std::uint32_t InIndex) noexcept
{
    if (MPhysics == nullptr || !MPhysics->IsValid() || InConnection.Joint != 0)
    {
        return false;
    }
    const b2BodyId ScopedBodyA = b2LoadBodyId(InConnection.BodyA);
    const b2BodyId ScopedBodyB = b2LoadBodyId(InConnection.BodyB);
    if (!b2Body_IsValid(ScopedBodyA) || !b2Body_IsValid(ScopedBodyB))
    {
        return false;
    }
    const b2WorldId ScopedWorld = b2LoadWorldId(MPhysics->WorldId());
    const auto ScopedApplyBase = [&](b2JointDef& InBase)
    {
        InBase.bodyIdA = ScopedBodyA;
        InBase.bodyIdB = ScopedBodyB;
        InBase.localFrameA = JointFrame(b2Vec2{InConnection.Params.LocalA.X, InConnection.Params.LocalA.Y});
        InBase.localFrameB = JointFrame(b2Vec2{InConnection.Params.LocalB.X, InConnection.Params.LocalB.Y});
        InBase.collideConnected = InConnection.Params.CollideConnected;
        // The event user data maps a Box2D joint back to its pool slot.
        InBase.userData = reinterpret_cast<void*>(static_cast<std::uintptr_t>(InIndex) + 1u);
        // A finite threshold is what makes Box2D report the joint at all. The
        // authored strength switch can suppress both break thresholds while
        // leaving the tension telemetry alive.
        const float ScopedForceThreshold =
            InConnection.Params.StrengthEnabled
                ? (InConnection.Params.MaxTension > 0.0f ? InConnection.Params.MaxTension : InConnection.BreakForce)
                : 0.0f;
        InBase.forceThreshold = ScopedForceThreshold > 0.0f ? ScopedForceThreshold : FLT_MAX;
        InBase.torqueThreshold = InConnection.Params.StrengthEnabled && InConnection.BreakTorque > 0.0f
                                     ? InConnection.BreakTorque
                                     : FLT_MAX;
    };

    b2JointId ScopedJoint = b2_nullJointId;
    switch (InConnection.Kind)
    {
        case JointConnectionKind::Rope:
        case JointConnectionKind::SpringRope:
        {
            b2DistanceJointDef ScopedDefinition = b2DefaultDistanceJointDef();
            ScopedApplyBase(ScopedDefinition.base);
            ScopedDefinition.length = std::max(InConnection.Params.Length, B2_LINEAR_SLOP);
            if (InConnection.Kind == JointConnectionKind::SpringRope)
            {
                ScopedDefinition.enableSpring = true;
                ScopedDefinition.hertz = InConnection.Params.Hertz;
                ScopedDefinition.dampingRatio = InConnection.Params.DampingRatio;
                if (InConnection.Params.LowerSpringForce != 0.0f)
                {
                    ScopedDefinition.lowerSpringForce = InConnection.Params.LowerSpringForce;
                }
                if (InConnection.Params.UpperSpringForce != 0.0f)
                {
                    ScopedDefinition.upperSpringForce = InConnection.Params.UpperSpringForce;
                }
            }
            ScopedJoint = b2CreateDistanceJoint(ScopedWorld, &ScopedDefinition);
            break;
        }
        case JointConnectionKind::Hinge:
        case JointConnectionKind::SpringHinge:
        {
            b2RevoluteJointDef ScopedDefinition = b2DefaultRevoluteJointDef();
            ScopedApplyBase(ScopedDefinition.base);
            ScopedDefinition.enableLimit = InConnection.Params.EnableLimit;
            ScopedDefinition.lowerAngle = InConnection.Params.LowerAngle;
            ScopedDefinition.upperAngle = InConnection.Params.UpperAngle;
            ScopedDefinition.enableMotor = InConnection.Params.EnableMotor;
            ScopedDefinition.motorSpeed = InConnection.Params.MotorSpeed;
            ScopedDefinition.maxMotorTorque = InConnection.Params.MaxMotorTorque;
            if (InConnection.Kind == JointConnectionKind::SpringHinge)
            {
                ScopedDefinition.enableSpring = true;
                ScopedDefinition.targetAngle = 0.0f;
                ScopedDefinition.hertz = InConnection.Params.Hertz;
                ScopedDefinition.dampingRatio = InConnection.Params.DampingRatio;
            }
            ScopedJoint = b2CreateRevoluteJoint(ScopedWorld, &ScopedDefinition);
            break;
        }
    }
    if (!b2Joint_IsValid(ScopedJoint))
    {
        return false;
    }
    InConnection.Joint = b2StoreJointId(ScopedJoint);
    return true;
}

void PhysicsJointSystem::FillConnectionInfo(std::uint32_t InIndex, JointConnectionInfo* Out) const noexcept
{
    if (Out == nullptr)
    {
        return;
    }
    const ConnectionRuntime* ScopedConnection = Connection(InIndex);
    if (ScopedConnection == nullptr)
    {
        *Out = JointConnectionInfo{};
        return;
    }
    JointConnectionInfo ScopedInfo{};
    ScopedInfo.Handle = JointConnectionHandle{InIndex, ScopedConnection->Generation};
    ScopedInfo.Kind = ScopedConnection->Kind;
    ScopedInfo.Flags = ScopedConnection->Flags;
    ScopedInfo.ChainA = ScopedConnection->ChainA;
    ScopedInfo.ChainB = ScopedConnection->ChainB;
    ScopedInfo.LinkA = ScopedConnection->LinkA;
    ScopedInfo.LinkB = ScopedConnection->LinkB;
    ScopedInfo.BreakForce = ScopedConnection->BreakForce;
    ScopedInfo.BreakTorque = ScopedConnection->BreakTorque;
    ScopedInfo.CurrentTension = ScopedConnection->CurrentTension;
    ScopedInfo.MaxTension = ScopedConnection->Params.MaxTension;
    ScopedInfo.Stress = ScopedConnection->Stress;
    ScopedInfo.Damage = ScopedConnection->Damage;
    ScopedInfo.StrengthEnabled = ScopedConnection->Params.StrengthEnabled;
    ScopedInfo.TensionLocked = ScopedConnection->TensionLocked;
    ScopedInfo.Broken = ScopedConnection->Broken;
    *Out = ScopedInfo;
}

bool PhysicsJointSystem::ConnectionInfo(JointConnectionHandle InHandle, JointConnectionInfo* Out) const noexcept
{
    if (Out == nullptr)
    {
        return false;
    }
    if (Find(InHandle) == nullptr)
    {
        *Out = JointConnectionInfo{};
        return false;
    }
    FillConnectionInfo(InHandle.Index, Out);
    return true;
}

std::uint32_t PhysicsJointSystem::ChainConnectionCount(JointChainHandle InHandle) const noexcept
{
    const ChainRuntime* ScopedChain = Runtime(InHandle);
    return ScopedChain == nullptr ? 0 : ScopedChain->ConnectionCount;
}

bool PhysicsJointSystem::ChainConnection(JointChainHandle InHandle, std::uint32_t InConnection,
                                         JointConnectionInfo* Out) const noexcept
{
    const ChainRuntime* ScopedChain = Runtime(InHandle);
    if (ScopedChain == nullptr || InConnection >= ScopedChain->ConnectionCount)
    {
        if (Out != nullptr)
        {
            *Out = JointConnectionInfo{};
        }
        return false;
    }
    FillConnectionInfo(ScopedChain->ConnectionSlots[InConnection], Out);
    return true;
}

void PhysicsJointSystem::RefreshLinkFlags(Ecs::Registry& InRegistry, std::uint32_t InChainSlot) noexcept
{
    if (MStorage == nullptr || InChainSlot >= kMaxChains || !Chains()[InChainSlot].Active)
    {
        return;
    }
    ChainRuntime& ScopedChain = Chains()[InChainSlot];
    const auto ScopedFindMarker = [&](std::uint16_t InLink) -> JointLinkComponent*
    {
        if (InLink >= ScopedChain.LinkCount)
        {
            return nullptr;
        }
        const Entity ScopedEntity = ScopedChain.Links[InLink];
        if (ScopedEntity == entt::null || !InRegistry.valid(ScopedEntity))
        {
            return nullptr;
        }
        auto* ScopedMarker = InRegistry.try_get<JointLinkComponent>(ScopedEntity);
        if (ScopedMarker == nullptr || ScopedMarker->ChainSlot != InChainSlot ||
            ScopedMarker->ChainGeneration != ScopedChain.Generation)
        {
            return nullptr;
        }
        return ScopedMarker;
    };

    // Start from both ends free, then every live connection claims its side.
    for (std::uint32_t ScopedI = 0; ScopedI < ScopedChain.LinkCount; ++ScopedI)
    {
        JointLinkComponent* ScopedMarker = ScopedFindMarker(static_cast<std::uint16_t>(ScopedI));
        if (ScopedMarker != nullptr)
        {
            ScopedMarker->Flags = JointLinkFreeStart | JointLinkFreeEnd;
        }
    }
    for (std::uint32_t ScopedI = 0; ScopedI < ScopedChain.ConnectionCount; ++ScopedI)
    {
        const ConnectionRuntime* ScopedConnection = Connection(ScopedChain.ConnectionSlots[ScopedI]);
        if (ScopedConnection == nullptr || ScopedConnection->Broken)
        {
            continue;
        }
        if (ScopedConnection->LinkA != kNoLink)
        {
            JointLinkComponent* ScopedMarker = ScopedFindMarker(ScopedConnection->LinkA);
            if (ScopedMarker != nullptr)
            {
                ScopedMarker->Flags &= static_cast<std::uint16_t>(~JointLinkFreeEnd);
            }
        }
        if (ScopedConnection->LinkB != kNoLink)
        {
            JointLinkComponent* ScopedMarker = ScopedFindMarker(ScopedConnection->LinkB);
            if (ScopedMarker != nullptr)
            {
                ScopedMarker->Flags &= static_cast<std::uint16_t>(~JointLinkFreeStart);
            }
        }
    }
}

void PhysicsJointSystem::DestroyConnectionsOf(ChainRuntime& InChain) noexcept
{
    if (MStorage == nullptr)
    {
        InChain.ConnectionCount = 0;
        return;
    }
    const bool ScopedWorldLive = MPhysics != nullptr && MPhysics->IsValid();
    for (std::uint32_t ScopedI = 0; ScopedI < kMaxConnections; ++ScopedI)
    {
        ConnectionRuntime& ScopedConnection = MStorage->Connections[ScopedI];
        if (!ScopedConnection.Active)
        {
            continue;
        }
        if (ScopedConnection.ChainA != InChain.Index && ScopedConnection.ChainB != InChain.Index)
        {
            continue;
        }
        if (ScopedWorldLive && ScopedConnection.Joint != 0)
        {
            const b2JointId ScopedJoint = b2LoadJointId(ScopedConnection.Joint);
            if (b2Joint_IsValid(ScopedJoint))
            {
                b2DestroyJoint(ScopedJoint);
            }
        }
        ScopedConnection.Joint = 0;
        FreeConnection(ScopedI);
    }
    InChain.ConnectionCount = 0;
}

void PhysicsJointSystem::DestroyRuntime(Ecs::Registry& InRegistry, ChainRuntime& InChain, bool InKeepIdentity) noexcept
{
    // Joints first: destroying a body would invalidate its joint ids, and a
    // destroyed joint id must never reach b2Joint_IsValid.
    DestroyConnectionsOf(InChain);

    const bool ScopedWorldLive = MPhysics != nullptr && MPhysics->IsValid();

    // Anchor bodies created by this system (not gameplay entities).
    if (InChain.OwnsAnchorBodyA && ScopedWorldLive && BodyIsLive(InChain.AnchorBodyA))
    {
        if (MPhysics != nullptr)
        {
            MPhysics->UntrackBody(InChain.AnchorBodyA);
        }
        b2DestroyBody(b2LoadBodyId(InChain.AnchorBodyA));
    }
    if (InChain.OwnsAnchorBodyB && ScopedWorldLive && BodyIsLive(InChain.AnchorBodyB))
    {
        if (MPhysics != nullptr)
        {
            MPhysics->UntrackBody(InChain.AnchorBodyB);
        }
        b2DestroyBody(b2LoadBodyId(InChain.AnchorBodyB));
    }

    // Link bodies. The entity may have been destroyed by gameplay before the
    // chain itself, so both the body and the entity are validated separately.
    for (std::uint32_t ScopedI = 0; ScopedI < InChain.LinkCount; ++ScopedI)
    {
        const std::uint64_t ScopedPacked = InChain.LinkBodies[ScopedI];
        if (ScopedWorldLive && ScopedPacked != 0 && b2Body_IsValid(b2LoadBodyId(ScopedPacked)))
        {
            if (MPhysics != nullptr)
            {
                MPhysics->UntrackBody(ScopedPacked);
            }
            b2DestroyBody(b2LoadBodyId(ScopedPacked));
        }
        const Entity ScopedEntity = InChain.Links[ScopedI];
        if (ScopedEntity != entt::null && InRegistry.valid(ScopedEntity))
        {
            const auto* ScopedMarker = InRegistry.try_get<JointLinkComponent>(ScopedEntity);
            // Only destroy an entity that still belongs to this chain. EnTT
            if (ScopedMarker != nullptr && ScopedMarker->ChainSlot == InChain.Index &&
                ScopedMarker->ChainGeneration == InChain.Generation)
            {
                InRegistry.destroy(ScopedEntity);
            }
        }
        InChain.LinkBodies[ScopedI] = 0;
    }

    if (InKeepIdentity)
    {
        // A rebuild recreates the chain under the same slot and stable id, so
        // the id stays reserved and the generation is not advanced.
        const std::uint32_t ScopedIndex = InChain.Index;
        const std::uint32_t ScopedGeneration = InChain.Generation;
        const ChainId ScopedStableId = InChain.StableId;
        InChain = ChainRuntime{};
        InChain.Index = ScopedIndex;
        InChain.Generation = ScopedGeneration;
        InChain.StableId = ScopedStableId;
        return;
    }
    const std::uint32_t ScopedNextGeneration = InChain.Generation + 1u == 0 ? 1u : InChain.Generation + 1u;
    if (InChain.StableId.IsValid())
    {
        MStableIds.Release(InChain.StableId);
    }
    InChain = ChainRuntime{};
    InChain.Generation = ScopedNextGeneration;
}

bool PhysicsJointSystem::BreakConnection(Ecs::Registry& InRegistry, std::uint32_t InIndex,
                                         JointBreakReason InReason) noexcept
{
    ConnectionRuntime* ScopedConnection = Connection(InIndex);
    if (ScopedConnection == nullptr || ScopedConnection->Broken || ScopedConnection->Joint == 0)
    {
        return false;
    }
    const bool ScopedWorldLive = MPhysics != nullptr && MPhysics->IsValid();
    float ScopedForce = 0.0f;
    float ScopedTorque = 0.0f;
    Vector2D ScopedPoint{};
    if (ScopedWorldLive)
    {
        QueryReaction(ScopedConnection->Joint, &ScopedForce, &ScopedTorque);
        const b2JointId ScopedJoint = b2LoadJointId(ScopedConnection->Joint);
        if (b2Joint_IsValid(ScopedJoint))
        {
            const b2Transform ScopedFrameA = b2Joint_GetLocalFrameA(ScopedJoint);
            const b2Transform ScopedFrameB = b2Joint_GetLocalFrameB(ScopedJoint);
            const b2BodyId ScopedBodyA = b2LoadBodyId(ScopedConnection->BodyA);
            const b2BodyId ScopedBodyB = b2LoadBodyId(ScopedConnection->BodyB);
            if (b2Body_IsValid(ScopedBodyA) && b2Body_IsValid(ScopedBodyB))
            {
                const float ScopedPixelsPerMeter = MPhysics->PixelsPerMeter();
                const Vector2D ScopedWorldA =
                    ToPixels(b2Body_GetWorldPoint(ScopedBodyA, ScopedFrameA.p), ScopedPixelsPerMeter);
                const Vector2D ScopedWorldB =
                    ToPixels(b2Body_GetWorldPoint(ScopedBodyB, ScopedFrameB.p), ScopedPixelsPerMeter);
                ScopedPoint = (ScopedWorldA + ScopedWorldB) * 0.5f;
                b2Body_SetAwake(ScopedBodyA, true);
                b2Body_SetAwake(ScopedBodyB, true);
            }
            b2DestroyJoint(ScopedJoint);
        }
    }
    ScopedConnection->Joint = 0;
    ScopedConnection->Broken = true;
    ScopedConnection->Stress = 0.0f;
    ScopedConnection->Damage = 0.0f;

    if (MBreakCount < kMaxBreakEvents)
    {
        ChainBreakEvent ScopedEvent{};
        ScopedEvent.Connection = JointConnectionHandle{InIndex, ScopedConnection->Generation};
        ScopedEvent.Point = ScopedPoint;
        ScopedEvent.Force = ScopedForce;
        ScopedEvent.Torque = ScopedTorque;
        ScopedEvent.LinkA = ScopedConnection->LinkA;
        ScopedEvent.LinkB = ScopedConnection->LinkB;
        ScopedEvent.Sequence = MBreakSequence++;
        ScopedEvent.Reason = InReason;
        if (ScopedConnection->ChainA < kMaxChains && Chains()[ScopedConnection->ChainA].Active)
        {
            ScopedEvent.ChainA =
                JointChainHandle{ScopedConnection->ChainA, Chains()[ScopedConnection->ChainA].Generation,
                                 Chains()[ScopedConnection->ChainA].StableId};
        }
        if (ScopedConnection->ChainB < kMaxChains && Chains()[ScopedConnection->ChainB].Active)
        {
            ScopedEvent.ChainB =
                JointChainHandle{ScopedConnection->ChainB, Chains()[ScopedConnection->ChainB].Generation,
                                 Chains()[ScopedConnection->ChainB].StableId};
        }
        MStorage->Breaks[MBreakCount++] = ScopedEvent;
    }

    if (ScopedConnection->ChainA != kNoChain)
    {
        RefreshLinkFlags(InRegistry, ScopedConnection->ChainA);
    }
    if (ScopedConnection->ChainB != kNoChain && ScopedConnection->ChainB != ScopedConnection->ChainA)
    {
        RefreshLinkFlags(InRegistry, ScopedConnection->ChainB);
    }
    return true;
}

void PhysicsJointSystem::Update(Ecs::Registry& InRegistry, float InFixedDt) noexcept
{
    if (MStorage == nullptr || MPhysics == nullptr || !PhysicsSystem::IsLive(MPhysics, MPhysicsInstanceId) ||
        MPhysics->Generation() != MPhysicsGeneration || !MPhysics->IsValid())
    {
        return;
    }
    // A body tie outlives neither endpoint. When gameplay destroys one of the
    // bodies, Box2D silently drops the joint, so the pool slot must follow.
    std::uint32_t ScopedCursor = 0;
    while (ScopedCursor < MStorage->ActiveCount)
    {
        const std::uint32_t ScopedI = MStorage->ActiveSlots[ScopedCursor];
        ConnectionRuntime* ScopedConnection = Connection(ScopedI);
        if (ScopedConnection == nullptr || (ScopedConnection->Flags & JointConnectionTie) == 0)
        {
            ++ScopedCursor;
            continue;
        }
        if (ScopedConnection->ChainA != kNoChain || ScopedConnection->ChainB != kNoChain)
        {
            ++ScopedCursor;
            continue;
        }
        if (!BodyIsLive(ScopedConnection->BodyA) || !BodyIsLive(ScopedConnection->BodyB))
        {
            ScopedConnection->Joint = 0;
            // FreeConnection swap-pops the active list, so retry this cursor.
            FreeConnection(ScopedI);
            continue;
        }
        ++ScopedCursor;
    }
    const b2JointEvents ScopedEvents = b2World_GetJointEvents(b2LoadWorldId(MPhysics->WorldId()));
    for (int ScopedI = 0; ScopedI < ScopedEvents.count; ++ScopedI)
    {
        const b2JointEvent& ScopedEvent = ScopedEvents.jointEvents[ScopedI];
        const std::uintptr_t ScopedEncoded = reinterpret_cast<std::uintptr_t>(ScopedEvent.userData);
        if (ScopedEncoded == 0)
        {
            continue;
        }
        const std::uint32_t ScopedIndex = static_cast<std::uint32_t>(ScopedEncoded - 1u);
        ConnectionRuntime* ScopedConnection = Connection(ScopedIndex);
        if (ScopedConnection == nullptr || ScopedConnection->Broken)
        {
            continue;
        }
        if (ScopedConnection->Joint != b2StoreJointId(ScopedEvent.jointId))
        {
            continue;
        }
        float ScopedForce = 0.0f;
        float ScopedTorque = 0.0f;
        QueryReaction(ScopedConnection->Joint, &ScopedForce, &ScopedTorque);
        JointBreakReason ScopedReason = JointBreakReason::Force;
        if (ScopedConnection->BreakTorque > 0.0f && ScopedTorque >= ScopedConnection->BreakTorque)
        {
            ScopedReason = JointBreakReason::Torque;
        }
        BreakConnection(InRegistry, ScopedIndex, ScopedReason);
    }

    // A destroyed link body makes Box2D drop its joint silently. Mark the slot
    // so the tension pass below never queries a dead body.
    for (std::uint32_t ScopedCursor = 0; ScopedCursor < MStorage->ActiveCount; ++ScopedCursor)
    {
        const std::uint32_t ScopedI = MStorage->ActiveSlots[ScopedCursor];
        ConnectionRuntime* ScopedConnection = Connection(ScopedI);
        if (ScopedConnection == nullptr || ScopedConnection->Joint == 0 ||
            (ScopedConnection->Flags & JointConnectionTie) != 0)
        {
            continue;
        }
        if (!BodyIsLive(ScopedConnection->BodyA) || !BodyIsLive(ScopedConnection->BodyB))
        {
            ScopedConnection->Joint = 0;
            ScopedConnection->Broken = true;
        }
    }

    // Stress is opt-in telemetry; it never feeds back into the simulation.
    for (std::uint32_t ScopedCursor = 0; ScopedCursor < MStorage->ActiveCount; ++ScopedCursor)
    {
        const std::uint32_t ScopedI = MStorage->ActiveSlots[ScopedCursor];
        ConnectionRuntime* ScopedConnection = Connection(ScopedI);
        if (ScopedConnection == nullptr || ScopedConnection->Broken || ScopedConnection->Joint == 0)
        {
            continue;
        }
        if ((ScopedConnection->Flags & JointConnectionTrackStress) == 0)
        {
            continue;
        }
        float ScopedForce = 0.0f;
        float ScopedTorque = 0.0f;
        QueryReaction(ScopedConnection->Joint, &ScopedForce, &ScopedTorque);
        float ScopedStress = 0.0f;
        if (ScopedConnection->BreakForce > 0.0f)
        {
            ScopedStress = std::max(ScopedStress, ScopedForce / ScopedConnection->BreakForce);
        }
        if (ScopedConnection->BreakTorque > 0.0f)
        {
            ScopedStress = std::max(ScopedStress, ScopedTorque / ScopedConnection->BreakTorque);
        }
        ScopedConnection->Stress = ScopedStress;
    }

    // Rope tension model. It is telemetry plus an optional break cap; Box2D
    // still owns the constraint, so a locked tension never changes the solver.
    const float ScopedPixelsPerMeter = MPhysics->PixelsPerMeter();
    for (std::uint32_t ScopedCursor = 0; ScopedCursor < MStorage->ActiveCount; ++ScopedCursor)
    {
        const std::uint32_t ScopedI = MStorage->ActiveSlots[ScopedCursor];
        ConnectionRuntime* ScopedConnection = Connection(ScopedI);
        if (ScopedConnection == nullptr || ScopedConnection->Broken || ScopedConnection->Joint == 0)
        {
            continue;
        }
        // No authored tension model means CurrentTension is always zero; skip
        // the two body queries for the common case.
        if (ScopedConnection->Params.BaseTension <= 0.0f && ScopedConnection->Params.TensionPerDistance <= 0.0f)
        {
            ScopedConnection->CurrentTension = 0.0f;
            ScopedConnection->TensionLocked = false;
            continue;
        }
        const b2Vec2 ScopedPositionA = b2Body_GetPosition(b2LoadBodyId(ScopedConnection->BodyA));
        const b2Vec2 ScopedPositionB = b2Body_GetPosition(b2LoadBodyId(ScopedConnection->BodyB));
        const float ScopedDx = ScopedPositionB.x - ScopedPositionA.x;
        const float ScopedDy = ScopedPositionB.y - ScopedPositionA.y;
        const float ScopedDistance = std::sqrt(ScopedDx * ScopedDx + ScopedDy * ScopedDy);
        const float ScopedStretchPixels = std::max(0.0f, (ScopedDistance - ScopedConnection->Params.Length) *
                                                              ScopedPixelsPerMeter);
        const float ScopedDistanceLimit = ScopedConnection->Params.TensionDistance;
        const float ScopedEffectiveStretch = ScopedDistanceLimit > 0.0f
                                                 ? std::min(ScopedStretchPixels, ScopedDistanceLimit)
                                                 : ScopedStretchPixels;
        ScopedConnection->CurrentTension = ScopedConnection->Params.BaseTension +
                                           ScopedConnection->Params.TensionPerDistance * ScopedEffectiveStretch;
        ScopedConnection->TensionLocked =
            ScopedDistanceLimit > 0.0f && ScopedStretchPixels >= ScopedDistanceLimit;
    }

    // Damage is a slow path: a connection under sustained overstress accumulates
    // damage and finally snaps, which keeps a mechanism from behaving as a hard
    // on/off switch. A zero rate leaves the instant break untouched.
    if (InFixedDt <= 0.0f)
    {
        return;
    }
    for (std::uint32_t ScopedCursor = 0; ScopedCursor < MStorage->ActiveCount; ++ScopedCursor)
    {
        const std::uint32_t ScopedI = MStorage->ActiveSlots[ScopedCursor];
        ConnectionRuntime* ScopedConnection = Connection(ScopedI);
        if (ScopedConnection == nullptr || ScopedConnection->Broken || ScopedConnection->Joint == 0 ||
            ScopedConnection->Params.DamageRate <= 0.0f)
        {
            continue;
        }
        if (ScopedConnection->BreakForce <= 0.0f && ScopedConnection->BreakTorque <= 0.0f)
        {
            continue;
        }
        float ScopedForce = 0.0f;
        float ScopedTorque = 0.0f;
        QueryReaction(ScopedConnection->Joint, &ScopedForce, &ScopedTorque);
        float ScopedStress = 0.0f;
        if (ScopedConnection->BreakForce > 0.0f)
        {
            ScopedStress = std::max(ScopedStress, ScopedForce / ScopedConnection->BreakForce);
        }
        if (ScopedConnection->BreakTorque > 0.0f)
        {
            ScopedStress = std::max(ScopedStress, ScopedTorque / ScopedConnection->BreakTorque);
        }
        const float ScopedThreshold = std::clamp(ScopedConnection->Params.DamageThreshold, 0.05f, 1.0f);
        if (ScopedStress <= ScopedThreshold)
        {
            continue;
        }
        ScopedConnection->Damage += (ScopedStress - ScopedThreshold) * ScopedConnection->Params.DamageRate * InFixedDt;
        if (ScopedConnection->Damage >= 1.0f)
        {
            BreakConnection(InRegistry, ScopedI, JointBreakReason::Damage);
        }
    }
}

bool PhysicsJointSystem::CutConnection(Ecs::Registry& InRegistry, JointConnectionHandle InHandle) noexcept
{
    ConnectionRuntime* ScopedConnection = Find(InHandle);
    if (ScopedConnection == nullptr)
    {
        return false;
    }
    return BreakConnection(InRegistry, InHandle.Index, JointBreakReason::Cut);
}

bool PhysicsJointSystem::CutAtPoint(Ecs::Registry& InRegistry, JointChainHandle InHandle, Vector2D InWorldPoint,
                                    float InTolerancePixels) noexcept
{
    const ChainRuntime* ScopedChain = Runtime(InHandle);
    if (ScopedChain == nullptr || MPhysics == nullptr || !MPhysics->IsValid())
    {
        return false;
    }
    const float ScopedPixelsPerMeter = MPhysics->PixelsPerMeter();
    float ScopedBestDistance = InTolerancePixels * InTolerancePixels;
    std::uint32_t ScopedBestIndex = JointConnectionHandle::kInvalid;
    for (std::uint32_t ScopedI = 0; ScopedI < ScopedChain->ConnectionCount; ++ScopedI)
    {
        const std::uint32_t ScopedSlot = ScopedChain->ConnectionSlots[ScopedI];
        const ConnectionRuntime* ScopedConnection = Connection(ScopedSlot);
        if (ScopedConnection == nullptr || ScopedConnection->Broken)
        {
            continue;
        }
        const b2BodyId ScopedBodyA = b2LoadBodyId(ScopedConnection->BodyA);
        const b2BodyId ScopedBodyB = b2LoadBodyId(ScopedConnection->BodyB);
        if (!b2Body_IsValid(ScopedBodyA) || !b2Body_IsValid(ScopedBodyB))
        {
            continue;
        }
        const Vector2D ScopedFrom = ToPixels(b2Body_GetPosition(ScopedBodyA), ScopedPixelsPerMeter);
        const Vector2D ScopedTo = ToPixels(b2Body_GetPosition(ScopedBodyB), ScopedPixelsPerMeter);
        const float ScopedDistance = SegmentDistanceSquared(InWorldPoint, ScopedFrom, ScopedTo);
        if (ScopedDistance < ScopedBestDistance)
        {
            ScopedBestDistance = ScopedDistance;
            ScopedBestIndex = ScopedSlot;
        }
    }
    if (ScopedBestIndex == JointConnectionHandle::kInvalid)
    {
        return false;
    }
    return BreakConnection(InRegistry, ScopedBestIndex, JointBreakReason::Cut);
}

bool PhysicsJointSystem::RepairConnection(Ecs::Registry& InRegistry, JointConnectionHandle InHandle,
                                          ChainError* InError) noexcept
{
    if (InError != nullptr)
    {
        *InError = ChainError::None;
    }
    ConnectionRuntime* ScopedConnection = Find(InHandle);
    if (ScopedConnection == nullptr)
    {
        if (InError != nullptr)
        {
            *InError = ChainError::InvalidConnection;
        }
        return false;
    }
    if (!ScopedConnection->Broken)
    {
        if (InError != nullptr)
        {
            *InError = ChainError::InvalidConnection;
        }
        return false;
    }
    if (!CreateJoint(*ScopedConnection, InHandle.Index))
    {
        if (InError != nullptr)
        {
            *InError = ChainError::Box2DRejected;
        }
        return false;
    }
    ScopedConnection->Broken = false;
    ScopedConnection->Stress = 0.0f;
    ScopedConnection->Damage = 0.0f;
    if (ScopedConnection->ChainA != kNoChain)
    {
        RefreshLinkFlags(InRegistry, ScopedConnection->ChainA);
    }
    if (ScopedConnection->ChainB != kNoChain && ScopedConnection->ChainB != ScopedConnection->ChainA)
    {
        RefreshLinkFlags(InRegistry, ScopedConnection->ChainB);
    }
    return true;
}

bool PhysicsJointSystem::RebuildChain(Ecs::Registry& InRegistry, JointChainHandle InHandle,
                                      const JointChainDesc& InDesc, ChainError* InError) noexcept
{
    if (InError != nullptr)
    {
        *InError = ChainError::None;
    }
    ChainRuntime* ScopedChain = Runtime(InHandle);
    if (ScopedChain == nullptr)
    {
        if (InError != nullptr)
        {
            *InError = ChainError::InvalidConnection;
        }
        return false;
    }
    ChainRuntime ScopedIdentity{};
    ScopedIdentity.Index = ScopedChain->Index;
    ScopedIdentity.Generation = ScopedChain->Generation;
    ScopedIdentity.StableId = ScopedChain->StableId;
    if (MLiveChains > 0)
    {
        --MLiveChains;
    }
    DestroyRuntime(InRegistry, *ScopedChain, true);
    const JointChainHandle ScopedHandle = CreateChainInternal(InRegistry, InDesc, &ScopedIdentity, InError);
    if (!ScopedHandle.IsValid())
    {
        // A validation failure happens before the staging cleanup, so the id
        // reserved for the rebuild still needs releasing.
        MStableIds.Release(ScopedIdentity.StableId);
        return false;
    }
    return true;
}

JointChainHandle PhysicsJointSystem::FindChain(ChainId InId) const noexcept
{
    if (MStorage == nullptr || !InId.IsValid())
    {
        return {};
    }
    for (std::uint32_t ScopedI = 0; ScopedI < kMaxChains; ++ScopedI)
    {
        const ChainRuntime& ScopedChain = Chains()[ScopedI];
        if (ScopedChain.Active && ScopedChain.StableId == InId)
        {
            return JointChainHandle{ScopedI, ScopedChain.Generation, ScopedChain.StableId};
        }
    }
    return {};
}

void PhysicsJointSystem::CaptureChainState(JointChainHandle InHandle, JointChainComponent* InChainState) const noexcept
{
    if (InChainState == nullptr)
    {
        return;
    }
    const ChainRuntime* ScopedChain = Runtime(InHandle);
    if (ScopedChain == nullptr)
    {
        return;
    }
    InChainState->ChainID = ScopedChain->StableId;
    InChainState->BrokenMaskLow = 0;
    InChainState->BrokenMaskHigh = 0;
    for (std::uint32_t ScopedI = 0; ScopedI < ScopedChain->ConnectionCount; ++ScopedI)
    {
        const ConnectionRuntime* ScopedConnection = Connection(ScopedChain->ConnectionSlots[ScopedI]);
        if (ScopedConnection == nullptr || !ScopedConnection->Broken)
        {
            continue;
        }
        if (ScopedI < 64)
        {
            InChainState->BrokenMaskLow |= std::uint64_t{1} << ScopedI;
        }
        else
        {
            InChainState->BrokenMaskHigh |= std::uint64_t{1} << (ScopedI - 64);
        }
    }
}

bool PhysicsJointSystem::ApplyChainState(Ecs::Registry& InRegistry, JointChainHandle InHandle,
                                         const JointChainComponent& InComponent, ChainError* InError) noexcept
{
    if (InError != nullptr)
    {
        *InError = ChainError::None;
    }
    ChainRuntime* ScopedChain = Runtime(InHandle);
    if (ScopedChain == nullptr)
    {
        if (InError != nullptr)
        {
            *InError = ChainError::InvalidConnection;
        }
        return false;
    }
    if (InComponent.ChainID.IsValid() && InComponent.ChainID != ScopedChain->StableId)
    {
        if (InError != nullptr)
        {
            *InError = ChainError::InvalidConnection;
        }
        return false;
    }
    for (std::uint32_t ScopedI = 0; ScopedI < ScopedChain->ConnectionCount; ++ScopedI)
    {
        const bool ScopedBroken = ScopedI < 64 ? ((InComponent.BrokenMaskLow >> ScopedI) & 1u) != 0
                                               : ((InComponent.BrokenMaskHigh >> (ScopedI - 64)) & 1u) != 0;
        ConnectionRuntime* ScopedConnection = Connection(ScopedChain->ConnectionSlots[ScopedI]);
        if (ScopedConnection == nullptr)
        {
            continue;
        }
        const JointConnectionHandle ScopedHandle{ScopedChain->ConnectionSlots[ScopedI], ScopedConnection->Generation};
        if (ScopedBroken && !ScopedConnection->Broken)
        {
            (void)CutConnection(InRegistry, ScopedHandle);
        }
        else if (!ScopedBroken && ScopedConnection->Broken)
        {
            if (!RepairConnection(InRegistry, ScopedHandle, InError))
            {
                return false;
            }
        }
    }
    return true;
}

std::uint32_t PhysicsJointSystem::CaptureTies(std::span<JointTieSave> Out) const noexcept
{
    if (MStorage == nullptr)
    {
        return 0;
    }
    const float ScopedPixelsPerMeter = MPhysics != nullptr ? MPhysics->PixelsPerMeter() : 50.0f;
    std::uint32_t ScopedCount = 0;
    for (const ConnectionRuntime& ScopedConnection : MStorage->Connections)
    {
        if (!ScopedConnection.Active || (ScopedConnection.Flags & JointConnectionTie) == 0)
        {
            continue;
        }
        if (ScopedCount >= Out.size())
        {
            break;
        }
        if (ScopedConnection.ChainA >= kMaxChains || ScopedConnection.ChainB >= kMaxChains)
        {
            continue;
        }
        const ChainRuntime& ScopedChainA = Chains()[ScopedConnection.ChainA];
        const ChainRuntime& ScopedChainB = Chains()[ScopedConnection.ChainB];
        if (!ScopedChainA.Active || !ScopedChainB.Active)
        {
            continue;
        }
        JointTieSave& ScopedSave = Out[ScopedCount++];
        ScopedSave = JointTieSave{};
        ScopedSave.ChainA = ScopedChainA.StableId;
        ScopedSave.ChainB = ScopedChainB.StableId;
        ScopedSave.LinkA = ScopedConnection.LinkA;
        ScopedSave.LinkB = ScopedConnection.LinkB;
        ScopedSave.Kind = ScopedConnection.Kind;
        ScopedSave.Length = ScopedConnection.Params.Length * ScopedPixelsPerMeter;
        ScopedSave.Hertz = ScopedConnection.Params.Hertz;
        ScopedSave.DampingRatio = ScopedConnection.Params.DampingRatio;
        ScopedSave.BreakForce = ScopedConnection.BreakForce;
        ScopedSave.BreakTorque = ScopedConnection.BreakTorque;
        ScopedSave.LowerSpringForce = ScopedConnection.Params.LowerSpringForce;
        ScopedSave.UpperSpringForce = ScopedConnection.Params.UpperSpringForce;
        ScopedSave.DamageRate = ScopedConnection.Params.DamageRate;
        ScopedSave.DamageThreshold = ScopedConnection.Params.DamageThreshold;
        ScopedSave.CollideConnected = ScopedConnection.Params.CollideConnected;
        ScopedSave.TrackStress = (ScopedConnection.Flags & JointConnectionTrackStress) != 0;
        ScopedSave.Broken = ScopedConnection.Broken;
    }
    return ScopedCount;
}

bool PhysicsJointSystem::RestoreTies(Ecs::Registry& InRegistry, std::span<const JointTieSave> InTies,
                                     ChainError* InError) noexcept
{
    if (InError != nullptr)
    {
        *InError = ChainError::None;
    }
    for (const JointTieSave& ScopedSave : InTies)
    {
        const JointChainHandle ScopedChainA = FindChain(ScopedSave.ChainA);
        const JointChainHandle ScopedChainB = FindChain(ScopedSave.ChainB);
        if (!ScopedChainA.IsValid() || !ScopedChainB.IsValid())
        {
            if (InError != nullptr)
            {
                *InError = ChainError::InvalidConnection;
            }
            return false;
        }
        JointTieDesc ScopedDesc;
        ScopedDesc.Kind = ScopedSave.Kind;
        ScopedDesc.Length = ScopedSave.Length;
        ScopedDesc.Hertz = ScopedSave.Hertz;
        ScopedDesc.DampingRatio = ScopedSave.DampingRatio;
        ScopedDesc.BreakForce = ScopedSave.BreakForce;
        ScopedDesc.BreakTorque = ScopedSave.BreakTorque;
        ScopedDesc.LowerSpringForce = ScopedSave.LowerSpringForce;
        ScopedDesc.UpperSpringForce = ScopedSave.UpperSpringForce;
        ScopedDesc.DamageRate = ScopedSave.DamageRate;
        ScopedDesc.DamageThreshold = ScopedSave.DamageThreshold;
        ScopedDesc.CollideConnected = ScopedSave.CollideConnected;
        ScopedDesc.TrackStress = ScopedSave.TrackStress;
        const JointConnectionHandle ScopedTie =
            TieChains(InRegistry, ScopedChainA, ScopedSave.LinkA, ScopedChainB, ScopedSave.LinkB, ScopedDesc, InError);
        if (!ScopedTie.IsValid())
        {
            return false;
        }
        if (ScopedSave.Broken)
        {
            (void)CutConnection(InRegistry, ScopedTie);
        }
    }
    return true;
}

JointConnectionHandle PhysicsJointSystem::TieChains(Ecs::Registry& InRegistry, JointChainHandle InChainA,
                                                    std::uint32_t InLinkA, JointChainHandle InChainB,
                                                    std::uint32_t InLinkB, const JointTieDesc& InDesc,
                                                    ChainError* InError) noexcept
{
    const auto ScopedFail = [InError](ChainError InReason)
    {
        if (InError != nullptr)
        {
            *InError = InReason;
        }
        return JointConnectionHandle{};
    };
    if (InError != nullptr)
    {
        *InError = ChainError::None;
    }
    ChainRuntime* ScopedChainA = Runtime(InChainA);
    ChainRuntime* ScopedChainB = Runtime(InChainB);
    if (ScopedChainA == nullptr || ScopedChainB == nullptr)
    {
        return ScopedFail(ChainError::InvalidDesc);
    }
    if (InLinkA >= ScopedChainA->LinkCount || InLinkB >= ScopedChainB->LinkCount)
    {
        return ScopedFail(ChainError::InvalidDesc);
    }
    const Entity ScopedEntityA = ScopedChainA->Links[InLinkA];
    const Entity ScopedEntityB = ScopedChainB->Links[InLinkB];
    if (ScopedEntityA == entt::null || ScopedEntityB == entt::null || !InRegistry.valid(ScopedEntityA) ||
        !InRegistry.valid(ScopedEntityB))
    {
        return ScopedFail(ChainError::AnchorBodyMissing);
    }
    if (InDesc.Length < 0.0f || InDesc.BreakForce < 0.0f || InDesc.BreakTorque < 0.0f || InDesc.DamageRate < 0.0f ||
        InDesc.DamageThreshold <= 0.0f)
    {
        return ScopedFail(ChainError::InvalidDesc);
    }
    if (InDesc.Kind != JointConnectionKind::Hinge && InDesc.Kind != JointConnectionKind::Rope &&
        InDesc.Kind != JointConnectionKind::SpringRope && InDesc.Kind != JointConnectionKind::SpringHinge)
    {
        return ScopedFail(ChainError::InvalidDesc);
    }
    const std::uint64_t ScopedBodyA = ScopedChainA->LinkBodies[InLinkA];
    const std::uint64_t ScopedBodyB = ScopedChainB->LinkBodies[InLinkB];
    if (!BodyIsLive(ScopedBodyA) || !BodyIsLive(ScopedBodyB))
    {
        return ScopedFail(ChainError::AnchorBodyMissing);
    }
    // Box2D rejects a joint whose two endpoints are the same body.
    if (ScopedBodyA == ScopedBodyB)
    {
        return ScopedFail(ChainError::InvalidDesc);
    }
    return CreateTie(ScopedBodyA, ScopedBodyB, ScopedChainA->Index, ScopedChainB->Index,
                     static_cast<std::uint16_t>(InLinkA), static_cast<std::uint16_t>(InLinkB), InDesc, InError);
}

JointConnectionHandle PhysicsJointSystem::TieBodies(Ecs::Registry& InRegistry, Entity InEntityA, Entity InEntityB,
                                                    const JointTieDesc& InDesc, ChainError* InError) noexcept
{
    const auto ScopedFail = [InError](ChainError InReason)
    {
        if (InError != nullptr)
        {
            *InError = InReason;
        }
        return JointConnectionHandle{};
    };
    if (InError != nullptr)
    {
        *InError = ChainError::None;
    }
    if (InEntityA == entt::null || InEntityB == entt::null || InEntityA == InEntityB)
    {
        return ScopedFail(ChainError::InvalidDesc);
    }
    const auto* ScopedBodyA = InRegistry.try_get<RigidBody2D>(InEntityA);
    const auto* ScopedBodyB = InRegistry.try_get<RigidBody2D>(InEntityB);
    if (ScopedBodyA == nullptr || ScopedBodyB == nullptr || !BodyIsLive(ScopedBodyA->Body) ||
        !BodyIsLive(ScopedBodyB->Body))
    {
        return ScopedFail(ChainError::AnchorBodyMissing);
    }
    return CreateTie(ScopedBodyA->Body, ScopedBodyB->Body, kNoChain, kNoChain, kNoLink, kNoLink, InDesc, InError);
}

JointConnectionHandle PhysicsJointSystem::CreateTie(std::uint64_t InBodyA, std::uint64_t InBodyB,
                                                    std::uint32_t InChainA, std::uint32_t InChainB,
                                                    std::uint16_t InLinkA, std::uint16_t InLinkB,
                                                    const JointTieDesc& InDesc, ChainError* InError) noexcept
{
    const auto ScopedFail = [InError](ChainError InReason)
    {
        if (InError != nullptr)
        {
            *InError = InReason;
        }
        return JointConnectionHandle{};
    };
    if (InError != nullptr)
    {
        *InError = ChainError::None;
    }
    if (MPhysics == nullptr || !MPhysics->IsValid() || MStorage == nullptr)
    {
        return ScopedFail(ChainError::NotInitialized);
    }
    if (!BodyIsLive(InBodyA) || !BodyIsLive(InBodyB) || InBodyA == InBodyB)
    {
        return ScopedFail(ChainError::AnchorBodyMissing);
    }
    if (InDesc.Length < 0.0f || InDesc.BreakForce < 0.0f || InDesc.BreakTorque < 0.0f || InDesc.DamageRate < 0.0f ||
        InDesc.DamageThreshold <= 0.0f)
    {
        return ScopedFail(ChainError::InvalidDesc);
    }
    if (InDesc.Kind != JointConnectionKind::Hinge && InDesc.Kind != JointConnectionKind::Rope &&
        InDesc.Kind != JointConnectionKind::SpringRope && InDesc.Kind != JointConnectionKind::SpringHinge)
    {
        return ScopedFail(ChainError::InvalidDesc);
    }
    const std::uint32_t ScopedIndex = AllocateConnection();
    if (ScopedIndex == JointConnectionHandle::kInvalid)
    {
        return ScopedFail(ChainError::OutOfConnections);
    }
    ConnectionRuntime& ScopedConnection = MStorage->Connections[ScopedIndex];
    ScopedConnection.Kind = InDesc.Kind;
    ScopedConnection.Flags = JointConnectionTie;
    if (InDesc.TrackStress)
    {
        ScopedConnection.Flags |= JointConnectionTrackStress;
    }
    ScopedConnection.ChainA = InChainA;
    ScopedConnection.ChainB = InChainB;
    ScopedConnection.LinkA = InLinkA;
    ScopedConnection.LinkB = InLinkB;
    ScopedConnection.BodyA = InBodyA;
    ScopedConnection.BodyB = InBodyB;
    ScopedConnection.BreakForce = InDesc.BreakForce;
    ScopedConnection.BreakTorque = InDesc.BreakTorque;
    ScopedConnection.Params.Hertz = InDesc.Hertz;
    ScopedConnection.Params.DampingRatio = InDesc.DampingRatio;
    ScopedConnection.Params.LowerSpringForce = InDesc.LowerSpringForce;
    ScopedConnection.Params.UpperSpringForce = InDesc.UpperSpringForce;
    ScopedConnection.Params.DamageRate = InDesc.DamageRate;
    ScopedConnection.Params.DamageThreshold = InDesc.DamageThreshold;
    ScopedConnection.Params.CollideConnected = InDesc.CollideConnected;

    const b2BodyId ScopedBoxBodyA = b2LoadBodyId(InBodyA);
    const b2BodyId ScopedBoxBodyB = b2LoadBodyId(InBodyB);
    if (InDesc.Kind == JointConnectionKind::Rope || InDesc.Kind == JointConnectionKind::SpringRope)
    {
        ScopedConnection.Params.LocalA = Vector2D{};
        ScopedConnection.Params.LocalB = Vector2D{};
        const b2Pos ScopedPositionA = b2Body_GetPosition(ScopedBoxBodyA);
        const b2Pos ScopedPositionB = b2Body_GetPosition(ScopedBoxBodyB);
        const float ScopedDx = static_cast<float>(ScopedPositionB.x - ScopedPositionA.x);
        const float ScopedDy = static_cast<float>(ScopedPositionB.y - ScopedPositionA.y);
        const float ScopedDistance = std::sqrt(ScopedDx * ScopedDx + ScopedDy * ScopedDy);
        ScopedConnection.Params.Length = InDesc.Length > 0.0f ? InDesc.Length / MPhysics->PixelsPerMeter()
                                                              : std::max(ScopedDistance, B2_LINEAR_SLOP);
    }
    else
    {
        // A hinge tie meets at the midpoint of the two body centres.
        const b2Pos ScopedPositionA = b2Body_GetPosition(ScopedBoxBodyA);
        const b2Pos ScopedPositionB = b2Body_GetPosition(ScopedBoxBodyB);
        b2Pos ScopedMid{};
        ScopedMid.x = (ScopedPositionA.x + ScopedPositionB.x) * 0.5;
        ScopedMid.y = (ScopedPositionA.y + ScopedPositionB.y) * 0.5;
        const b2Vec2 ScopedLocalA = b2Body_GetLocalPoint(ScopedBoxBodyA, ScopedMid);
        const b2Vec2 ScopedLocalB = b2Body_GetLocalPoint(ScopedBoxBodyB, ScopedMid);
        ScopedConnection.Params.LocalA = Vector2D{ScopedLocalA.x, ScopedLocalA.y};
        ScopedConnection.Params.LocalB = Vector2D{ScopedLocalB.x, ScopedLocalB.y};
        ScopedConnection.Params.Length = B2_LINEAR_SLOP;
    }
    if (!CreateJoint(ScopedConnection, ScopedIndex))
    {
        FreeConnection(ScopedIndex);
        return ScopedFail(ChainError::Box2DRejected);
    }
    return JointConnectionHandle{ScopedIndex, ScopedConnection.Generation};
}

bool PhysicsJointSystem::Untie(JointConnectionHandle InHandle) noexcept
{
    ConnectionRuntime* ScopedConnection = Find(InHandle);
    if (ScopedConnection == nullptr || (ScopedConnection->Flags & JointConnectionTie) == 0)
    {
        return false;
    }
    const bool ScopedWorldLive = MPhysics != nullptr && MPhysics->IsValid();
    if (ScopedWorldLive && ScopedConnection->Joint != 0)
    {
        const b2JointId ScopedJoint = b2LoadJointId(ScopedConnection->Joint);
        if (b2Joint_IsValid(ScopedJoint))
        {
            b2DestroyJoint(ScopedJoint);
        }
    }
    ScopedConnection->Joint = 0;
    FreeConnection(InHandle.Index);
    return true;
}

std::span<const ChainBreakEvent> PhysicsJointSystem::Breaks() const noexcept
{
    if (MStorage == nullptr)
    {
        return {};
    }
    return {MStorage->Breaks.data(), MBreakCount};
}

std::uint32_t PhysicsJointSystem::TieCount() const noexcept
{
    if (MStorage == nullptr)
    {
        return 0;
    }
    std::uint32_t ScopedCount = 0;
    for (const ConnectionRuntime& ScopedConnection : MStorage->Connections)
    {
        if (ScopedConnection.Active && (ScopedConnection.Flags & JointConnectionTie) != 0)
        {
            ++ScopedCount;
        }
    }
    return ScopedCount;
}

JointChainHandle PhysicsJointSystem::CreateChain(Ecs::Registry& InRegistry, const JointChainDesc& InDesc,
                                                 ChainError* InError) noexcept
{
    return CreateChainInternal(InRegistry, InDesc, nullptr, InError);
}

JointChainHandle PhysicsJointSystem::CreateChainInternal(Ecs::Registry& InRegistry, const JointChainDesc& InDesc,
                                                         const ChainRuntime* InRebuild, ChainError* InError) noexcept
{
    const auto ScopedFail = [InError](ChainError InReason)
    {
        if (InError != nullptr)
        {
            *InError = InReason;
        }
        return JointChainHandle{};
    };

    if (InError != nullptr)
    {
        *InError = ChainError::None;
    }
    if (MPhysics == nullptr || !MPhysics->IsValid() || MStorage == nullptr)
    {
        return ScopedFail(ChainError::NotInitialized);
    }
    if (InDesc.Kind != JointChainKind::Rope && InDesc.Kind != JointChainKind::Chain)
    {
        return ScopedFail(ChainError::InvalidDesc);
    }
    if (InDesc.Radius <= 0.0f || InDesc.Density <= 0.0f || InDesc.MaskBits == 0 || InDesc.BreakForce < 0.0f ||
        InDesc.BreakTorque < 0.0f || InDesc.DamageRate < 0.0f || InDesc.DamageThreshold <= 0.0f)
    {
        return ScopedFail(ChainError::InvalidDesc);
    }

    std::array<JointChainSection, kMaxChainSections> ScopedSections{};
    std::uint32_t ScopedSectionCount = 0;
    std::uint32_t ScopedLinkCount = 0;
    if (!InDesc.Sections.empty())
    {
        if (InDesc.Sections.size() > kMaxChainSections)
        {
            return ScopedFail(ChainError::InvalidDesc);
        }
        for (const JointChainSection& ScopedSection : InDesc.Sections)
        {
            if ((ScopedSection.Kind != JointChainKind::Rope && ScopedSection.Kind != JointChainKind::Chain) ||
                ScopedSection.LinkCount == 0 || ScopedSection.Radius < 0.0f || ScopedSection.LinkLengthScale <= 0.0f ||
                ScopedSection.BreakForce < 0.0f || ScopedSection.BreakTorque < 0.0f ||
                ScopedSection.DamageRate < 0.0f || ScopedSection.DamageThreshold <= 0.0f)
            {
                return ScopedFail(ChainError::InvalidDesc);
            }
            ScopedSections[ScopedSectionCount++] = ScopedSection;
            ScopedLinkCount += ScopedSection.LinkCount;
        }
        if (ScopedLinkCount == 0 || ScopedLinkCount > kMaxLinksPerChain)
        {
            return ScopedFail(ChainError::OutOfLinks);
        }
    }

    // The chain is a polyline of links; a path point count of N+1 means N links.
    std::array<Vector2D, kMaxLinksPerChain + 1> ScopedPoints{};
    std::uint32_t ScopedPointCount = 0;
    if (!InDesc.Path.empty())
    {
        if (InDesc.Path.size() < 2 || InDesc.Path.size() > ScopedPoints.size())
        {
            return ScopedFail(ChainError::InvalidDesc);
        }
        ScopedPointCount = static_cast<std::uint32_t>(InDesc.Path.size());
        for (std::uint32_t ScopedI = 0; ScopedI < ScopedPointCount; ++ScopedI)
        {
            ScopedPoints[ScopedI] = InDesc.Path[ScopedI];
        }
        if (ScopedSectionCount == 0)
        {
            JointChainSection ScopedUniform;
            ScopedUniform.Kind = InDesc.Kind;
            ScopedUniform.Flags = InDesc.Flags & (JointChainSpringRope | JointChainSpringHinge);
            ScopedUniform.LinkCount = ScopedPointCount - 1;
            ScopedUniform.Radius = InDesc.Radius;
            ScopedUniform.LinkLengthScale = 1.0f;
            ScopedSections[0] = ScopedUniform;
            ScopedSectionCount = 1;
            ScopedLinkCount = ScopedPointCount - 1;
        }
        else if (ScopedLinkCount != ScopedPointCount - 1)
        {
            return ScopedFail(ChainError::InvalidDesc);
        }
    }
    else
    {
        if (ScopedSectionCount == 0)
        {
            JointChainSection ScopedUniform;
            ScopedUniform.Kind = InDesc.Kind;
            ScopedUniform.Flags = InDesc.Flags & (JointChainSpringRope | JointChainSpringHinge);
            ScopedUniform.LinkCount = std::clamp(InDesc.SegmentCount, 1u, kMaxLinksPerChain);
            ScopedUniform.Radius = InDesc.Radius;
            ScopedUniform.LinkLengthScale = 1.0f;
            ScopedSections[0] = ScopedUniform;
            ScopedSectionCount = 1;
            ScopedLinkCount = ScopedUniform.LinkCount;
        }
        const Vector2D ScopedDelta = InDesc.End - InDesc.Start;
        if (ScopedDelta.LengthSquared() <= 1.0e-8f)
        {
            return ScopedFail(ChainError::InvalidDesc);
        }
        ScopedPointCount = ScopedLinkCount + 1;
        std::array<float, kMaxLinksPerChain> ScopedWeights{};
        float ScopedTotal = 0.0f;
        std::uint32_t ScopedSectionCursor = 0;
        std::uint32_t ScopedSectionEnd = ScopedSections[0].LinkCount;
        for (std::uint32_t ScopedI = 0; ScopedI < ScopedLinkCount; ++ScopedI)
        {
            while (ScopedI >= ScopedSectionEnd && ScopedSectionCursor + 1 < ScopedSectionCount)
            {
                ++ScopedSectionCursor;
                ScopedSectionEnd += ScopedSections[ScopedSectionCursor].LinkCount;
            }
            ScopedWeights[ScopedI] = std::max(ScopedSections[ScopedSectionCursor].LinkLengthScale, 0.01f);
            ScopedTotal += ScopedWeights[ScopedI];
        }
        float ScopedAccumulated = 0.0f;
        for (std::uint32_t ScopedI = 0; ScopedI < ScopedPointCount; ++ScopedI)
        {
            if (ScopedI == 0)
            {
                ScopedPoints[ScopedI] = InDesc.Start;
            }
            else if (ScopedI == ScopedPointCount - 1)
            {
                ScopedPoints[ScopedI] = InDesc.End;
            }
            else
            {
                ScopedAccumulated += ScopedWeights[ScopedI - 1];
                ScopedPoints[ScopedI] = InDesc.Start + ScopedDelta * (ScopedAccumulated / ScopedTotal);
            }
        }
    }
    if (ScopedLinkCount == 0 || ScopedLinkCount > kMaxLinksPerChain)
    {
        return ScopedFail(ChainError::OutOfLinks);
    }
    ScopedPointCount = ScopedLinkCount + 1;

    const auto ScopedSectionForLink = [&](std::uint32_t InLink) -> const JointChainSection&
    {
        std::uint32_t ScopedAccumulated = 0;
        for (std::uint32_t ScopedI = 0; ScopedI < ScopedSectionCount; ++ScopedI)
        {
            ScopedAccumulated += ScopedSections[ScopedI].LinkCount;
            if (InLink < ScopedAccumulated)
            {
                return ScopedSections[ScopedI];
            }
        }
        return ScopedSections[ScopedSectionCount - 1];
    };

    b2BodyId ScopedAnchorA = b2_nullBodyId;
    b2BodyId ScopedAnchorB = b2_nullBodyId;
    if (InDesc.AnchorA != entt::null)
    {
        const auto* ScopedBody = InRegistry.try_get<RigidBody2D>(InDesc.AnchorA);
        if (ScopedBody == nullptr || !BodyIsLive(ScopedBody->Body))
        {
            return ScopedFail(ChainError::AnchorBodyMissing);
        }
        ScopedAnchorA = b2LoadBodyId(ScopedBody->Body);
    }
    if (InDesc.AnchorB != entt::null)
    {
        const auto* ScopedBody = InRegistry.try_get<RigidBody2D>(InDesc.AnchorB);
        if (ScopedBody == nullptr || !BodyIsLive(ScopedBody->Body))
        {
            return ScopedFail(ChainError::AnchorBodyMissing);
        }
        ScopedAnchorB = b2LoadBodyId(ScopedBody->Body);
    }

    std::uint32_t ScopedSlot = kMaxChains;
    std::uint32_t ScopedGeneration = 0;
    ChainId ScopedStableId{};
    if (InRebuild != nullptr)
    {
        ScopedSlot = InRebuild->Index;
        ScopedGeneration = InRebuild->Generation;
        ScopedStableId = InRebuild->StableId;
        if (ScopedSlot >= kMaxChains || Chains()[ScopedSlot].Active || !ScopedStableId.IsValid())
        {
            return ScopedFail(ChainError::OutOfChains);
        }
    }
    else
    {
        for (std::uint32_t ScopedI = 0; ScopedI < kMaxChains; ++ScopedI)
        {
            if (!Chains()[ScopedI].Active)
            {
                ScopedSlot = ScopedI;
                break;
            }
        }
        if (ScopedSlot == kMaxChains)
        {
            return ScopedFail(ChainError::OutOfChains);
        }
        ScopedGeneration = Chains()[ScopedSlot].Generation == 0 ? 1u : Chains()[ScopedSlot].Generation;
        ScopedStableId = MStableIds.Allocate();
        if (!ScopedStableId.IsValid())
        {
            return ScopedFail(ChainError::OutOfChains);
        }
    }

    ChainRuntime ScopedStaged{};
    ScopedStaged.Index = ScopedSlot;
    ScopedStaged.Generation = ScopedGeneration;
    ScopedStaged.StableId = ScopedStableId;
    ScopedStaged.Kind = InDesc.Kind;
    ScopedStaged.Flags = InDesc.Flags;
    ScopedStaged.AnchorA = InDesc.AnchorA;
    ScopedStaged.AnchorB = InDesc.AnchorB;
    ScopedStaged.Radius = InDesc.Radius / MPhysics->PixelsPerMeter();
    ScopedStaged.LinkCount = ScopedLinkCount;

    const b2WorldId ScopedWorld = b2LoadWorldId(MPhysics->WorldId());
    const float ScopedPixelsPerMeter = MPhysics->PixelsPerMeter();
    // Links stop short of the shared path point so adjacent tips keep a real
    // gap. Setting the joint rest length to that gap removes the pre-compression
    // that a taut, both-ends-pinned cable would otherwise carry.
    const float ScopedJointGapMetres = 2.0f * B2_LINEAR_SLOP;

    for (std::uint32_t ScopedI = 0; ScopedI < ScopedLinkCount; ++ScopedI)
    {
        const Vector2D ScopedFrom = ScopedPoints[ScopedI];
        const Vector2D ScopedTo = ScopedPoints[ScopedI + 1];
        const Vector2D ScopedDelta = ScopedTo - ScopedFrom;
        const float ScopedLengthPixels = std::sqrt(ScopedDelta.LengthSquared());
        if (ScopedLengthPixels <= 1.0e-4f)
        {
            DestroyRuntime(InRegistry, ScopedStaged);
            return ScopedFail(ChainError::InvalidDesc);
        }

        const float ScopedLengthMetres = ScopedLengthPixels / ScopedPixelsPerMeter;
        if (ScopedLengthMetres <= 2.0f * B2_LINEAR_SLOP)
        {
            DestroyRuntime(InRegistry, ScopedStaged);
            return ScopedFail(ChainError::InvalidDesc);
        }
        const JointChainSection& ScopedSection = ScopedSectionForLink(ScopedI);
        const float ScopedRadiusPixels = ScopedSection.Radius > 0.0f ? ScopedSection.Radius : InDesc.Radius;
        // A capsule cannot be longer than the segment; clamp the radius so the
        // capsule stays inside it.
        float ScopedRadiusMetres = std::min(ScopedRadiusPixels / ScopedPixelsPerMeter, ScopedLengthMetres * 0.49f);
        ScopedRadiusMetres = std::max(ScopedRadiusMetres, 0.001f);
        // Box2D rejects a capsule whose centres are closer than one linear slop.
        const float ScopedHalfLength = std::max(
            ScopedLengthMetres * 0.5f - ScopedRadiusMetres - 0.5f * ScopedJointGapMetres, 0.6f * B2_LINEAR_SLOP);
        const Vector2D ScopedCentre = (ScopedFrom + ScopedTo) * 0.5f;
        const float ScopedAngle = Sim::Atan2(ScopedDelta.Y, ScopedDelta.X);

        const Entity ScopedEntity = InRegistry.create();
        ScopedStaged.Links[ScopedI] = ScopedEntity;
        InRegistry.emplace<Transform2D>(ScopedEntity, Transform2D{ScopedCentre, Vector2D{1.0f, 1.0f}, ScopedAngle});

        b2BodyDef ScopedBodyDefinition = b2DefaultBodyDef();
        ScopedBodyDefinition.type = b2_dynamicBody;
        ScopedBodyDefinition.position = ToBox(ScopedCentre, ScopedPixelsPerMeter);
        ScopedBodyDefinition.rotation = b2MakeRot(ScopedAngle);
        ScopedBodyDefinition.userData = BodyUserData(ScopedEntity);
        ScopedBodyDefinition.linearDamping = 0.08f;
        ScopedBodyDefinition.angularDamping = 0.15f;
        ScopedBodyDefinition.isAwake = true;
        b2BodyId ScopedBody = b2CreateBody(ScopedWorld, &ScopedBodyDefinition);
        if (!b2Body_IsValid(ScopedBody))
        {
            InRegistry.destroy(ScopedEntity);
            ScopedStaged.LinkCount = ScopedI;
            DestroyRuntime(InRegistry, ScopedStaged);
            return ScopedFail(ChainError::Box2DRejected);
        }
        if (MPhysics != nullptr)
        {
            MPhysics->TrackBody(ScopedEntity, b2StoreBodyId(ScopedBody));
        }

        b2ShapeDef ScopedShapeDefinition = b2DefaultShapeDef();
        ScopedShapeDefinition.density = InDesc.Density;
        ScopedShapeDefinition.material.friction = InDesc.Friction;
        ScopedShapeDefinition.material.restitution = InDesc.Restitution;
        ScopedShapeDefinition.filter.categoryBits = InDesc.CategoryBits;
        // Connected links never collide with each other: a rope whose segments
        // collided would push itself apart.
        ScopedShapeDefinition.filter.maskBits = InDesc.MaskBits & ~InDesc.CategoryBits;
        ScopedShapeDefinition.enableContactEvents = (InDesc.Flags & JointChainContactEvents) != 0;
        ScopedShapeDefinition.enableHitEvents = (InDesc.Flags & JointChainContactEvents) != 0;
        const b2Capsule ScopedCapsule = MakeCapsule(ScopedHalfLength, ScopedRadiusMetres);
        const b2ShapeId ScopedShape = b2CreateCapsuleShape(ScopedBody, &ScopedShapeDefinition, &ScopedCapsule);
        if (!b2Shape_IsValid(ScopedShape))
        {
            b2DestroyBody(ScopedBody);
            InRegistry.destroy(ScopedEntity);
            ScopedStaged.LinkCount = ScopedI;
            DestroyRuntime(InRegistry, ScopedStaged);
            return ScopedFail(ChainError::Box2DRejected);
        }

        RigidBody2D ScopedRigid{};
        ScopedRigid.Body = b2StoreBodyId(ScopedBody);
        ScopedRigid.PreviousPosition = ScopedCentre;
        ScopedRigid.Position = ScopedCentre;
        ScopedRigid.PreviousRotation = ScopedAngle;
        ScopedRigid.Rotation = ScopedAngle;
        ScopedRigid.Type = BodyType::Dynamic;
        ScopedRigid.Flags = RigidBodyAwake | RigidBodyEnabled;

        Collider2D ScopedCollider{};
        ScopedCollider.HalfExtents = Vector2D{ScopedHalfLength + ScopedRadiusMetres, ScopedRadiusMetres};
        ScopedCollider.Density = InDesc.Density;
        ScopedCollider.Friction = InDesc.Friction;
        ScopedCollider.Restitution = InDesc.Restitution;
        ScopedCollider.CategoryBits = static_cast<std::uint32_t>(InDesc.CategoryBits);
        ScopedCollider.MaskBits = static_cast<std::uint32_t>(InDesc.MaskBits);
        ScopedCollider.Shape = b2StoreShapeId(ScopedShape);
        ScopedCollider.ContactEvents = (InDesc.Flags & JointChainContactEvents) != 0;

        InRegistry.emplace<RigidBody2D>(ScopedEntity, ScopedRigid);
        InRegistry.emplace<Collider2D>(ScopedEntity, ScopedCollider);
        InRegistry.emplace<JointLinkComponent>(ScopedEntity, JointLinkComponent{ScopedStaged.StableId,
                                                                                static_cast<std::uint16_t>(ScopedI), 0,
                                                                                ScopedHalfLength, ScopedRadiusMetres,
                                                                                ScopedSlot, ScopedStaged.Generation});

        ScopedStaged.LinkBodies[ScopedI] = ScopedRigid.Body;
    }

    const auto ScopedMakeStaticAnchor = [&](Vector2D InWorldPoint) -> b2BodyId
    {
        b2BodyDef ScopedDefinition = b2DefaultBodyDef();
        ScopedDefinition.type = b2_staticBody;
        ScopedDefinition.position = ToBox(InWorldPoint, ScopedPixelsPerMeter);
        ScopedDefinition.rotation = b2Rot_identity;
        const b2BodyId ScopedAnchor = b2CreateBody(ScopedWorld, &ScopedDefinition);
        if (MPhysics != nullptr && b2Body_IsValid(ScopedAnchor))
        {
            // An anchor has no ECS entity; ownership tracking is the only way a
            // shutdown or a scene clear can find and destroy it.
            MPhysics->TrackBody(entt::null, b2StoreBodyId(ScopedAnchor));
        }
        return ScopedAnchor;
    };

    Vector2D ScopedStartWorld = ScopedPoints[0];
    Vector2D ScopedEndWorld = ScopedPoints[ScopedPointCount - 1];
    if (InDesc.AnchorA != entt::null)
    {
        ScopedStartWorld = ToPixels(b2Body_GetPosition(ScopedAnchorA), ScopedPixelsPerMeter);
    }
    else if ((InDesc.Flags & JointChainPinStart) != 0)
    {
        ScopedAnchorA = ScopedMakeStaticAnchor(ScopedStartWorld);
        if (!b2Body_IsValid(ScopedAnchorA))
        {
            DestroyRuntime(InRegistry, ScopedStaged);
            return ScopedFail(ChainError::Box2DRejected);
        }
        ScopedStaged.OwnsAnchorBodyA = true;
    }
    if (InDesc.AnchorB != entt::null)
    {
        ScopedEndWorld = ToPixels(b2Body_GetPosition(ScopedAnchorB), ScopedPixelsPerMeter);
    }
    else if ((InDesc.Flags & JointChainPinEnd) != 0)
    {
        ScopedAnchorB = ScopedMakeStaticAnchor(ScopedEndWorld);
        if (!b2Body_IsValid(ScopedAnchorB))
        {
            DestroyRuntime(InRegistry, ScopedStaged);
            return ScopedFail(ChainError::Box2DRejected);
        }
        ScopedStaged.OwnsAnchorBodyB = true;
    }
    ScopedStaged.AnchorBodyA = b2StoreBodyId(ScopedAnchorA);
    ScopedStaged.AnchorBodyB = b2StoreBodyId(ScopedAnchorB);

    const auto ScopedNewConnection = [&]() -> ConnectionRuntime*
    {
        if (ScopedStaged.ConnectionCount >= kMaxJointsPerChain)
        {
            return nullptr;
        }
        const std::uint32_t ScopedIndex = AllocateConnection();
        if (ScopedIndex == JointConnectionHandle::kInvalid)
        {
            return nullptr;
        }
        ScopedStaged.ConnectionSlots[ScopedStaged.ConnectionCount++] = ScopedIndex;
        return &MStorage->Connections[ScopedIndex];
    };

    const auto ScopedLinkLengthMetres = [&](std::uint32_t InLink) -> float
    {
        const Vector2D ScopedLinkDelta = ScopedPoints[InLink + 1] - ScopedPoints[InLink];
        return std::sqrt(ScopedLinkDelta.LengthSquared()) / ScopedPixelsPerMeter;
    };

    for (std::uint32_t ScopedI = 0; ScopedI + 1 < ScopedLinkCount; ++ScopedI)
    {
        const JointChainSection& ScopedLeft = ScopedSectionForLink(ScopedI);
        const JointChainSection& ScopedRight = ScopedSectionForLink(ScopedI + 1);
        const auto* ScopedMarkerA = InRegistry.try_get<JointLinkComponent>(ScopedStaged.Links[ScopedI]);
        const auto* ScopedMarkerB = InRegistry.try_get<JointLinkComponent>(ScopedStaged.Links[ScopedI + 1]);
        if (ScopedMarkerA == nullptr || ScopedMarkerB == nullptr)
        {
            DestroyRuntime(InRegistry, ScopedStaged);
            return ScopedFail(ChainError::InvalidDesc);
        }
        ConnectionRuntime* ScopedConnection = ScopedNewConnection();
        if (ScopedConnection == nullptr)
        {
            DestroyRuntime(InRegistry, ScopedStaged);
            return ScopedFail(ChainError::OutOfConnections);
        }
        ScopedConnection->Kind =
            ConnectionKindFor(ScopedLeft.Kind, ScopedLeft.Flags, ScopedRight.Kind, ScopedRight.Flags);
        ScopedConnection->ChainA = ScopedSlot;
        ScopedConnection->ChainB = ScopedSlot;
        ScopedConnection->LinkA = static_cast<std::uint16_t>(ScopedI);
        ScopedConnection->LinkB = static_cast<std::uint16_t>(ScopedI + 1);
        ScopedConnection->BodyA = ScopedStaged.LinkBodies[ScopedI];
        ScopedConnection->BodyB = ScopedStaged.LinkBodies[ScopedI + 1];
        ScopedConnection->BreakForce = CombinedBreak(ScopedLeft.BreakForce, ScopedRight.BreakForce, InDesc.BreakForce);
        ScopedConnection->BreakTorque =
            CombinedBreak(ScopedLeft.BreakTorque, ScopedRight.BreakTorque, InDesc.BreakTorque);
        if ((InDesc.Flags & JointChainTrackStress) != 0)
        {
            ScopedConnection->Flags |= JointConnectionTrackStress;
        }
        const float ScopedSizeA = ScopedMarkerA->HalfLength + ScopedMarkerA->Radius;
        const float ScopedSizeB = ScopedMarkerB->HalfLength + ScopedMarkerB->Radius;
        const b2Vec2 ScopedTipA = b2Vec2{ScopedSizeA, 0.0f};
        const b2Vec2 ScopedTipB = b2Vec2{-ScopedSizeB, 0.0f};
        ScopedConnection->Params.LocalA = Vector2D{ScopedTipA.x, ScopedTipA.y};
        ScopedConnection->Params.LocalB = Vector2D{ScopedTipB.x, ScopedTipB.y};
        // The rest length equals the geometric gap between the two tips. A
        // shorter value would pre-compress the chain against its anchors.
        ScopedConnection->Params.Length = std::max(ScopedLinkLengthMetres(ScopedI) * 0.5f - ScopedSizeA +
                                                       ScopedLinkLengthMetres(ScopedI + 1) * 0.5f - ScopedSizeB,
                                                   B2_LINEAR_SLOP);
        ScopedConnection->Params.Hertz = InDesc.Hertz;
        ScopedConnection->Params.DampingRatio = InDesc.DampingRatio;
        ScopedConnection->Params.LowerAngle = InDesc.LowerAngle;
        ScopedConnection->Params.UpperAngle = InDesc.UpperAngle;
        ScopedConnection->Params.EnableLimit = InDesc.EnableLimit;
        ScopedConnection->Params.EnableMotor = InDesc.EnableMotor;
        ScopedConnection->Params.MotorSpeed = InDesc.MotorSpeed;
        ScopedConnection->Params.MaxMotorTorque = InDesc.MaxMotorTorque;
        // A connection inside one section inherits that section's spring range
        // and damage curve; a boundary falls back to the chain defaults.
        const bool ScopedSameSection = &ScopedLeft == &ScopedRight;
        ScopedConnection->Params.LowerSpringForce =
            ScopedSameSection ? ScopedLeft.LowerSpringForce : InDesc.LowerSpringForce;
        ScopedConnection->Params.UpperSpringForce =
            ScopedSameSection ? ScopedLeft.UpperSpringForce : InDesc.UpperSpringForce;
        ScopedConnection->Params.DamageRate = ScopedSameSection ? ScopedLeft.DamageRate : InDesc.DamageRate;
        ScopedConnection->Params.DamageThreshold =
            ScopedSameSection ? ScopedLeft.DamageThreshold : InDesc.DamageThreshold;
        ScopedConnection->Params.BaseTension = InDesc.BaseTension;
        ScopedConnection->Params.MaxTension = InDesc.MaxTension;
        ScopedConnection->Params.TensionPerDistance = InDesc.TensionPerDistance;
        ScopedConnection->Params.TensionDistance = InDesc.TensionDistance;
        ScopedConnection->Params.StrengthEnabled = InDesc.StrengthEnabled;
        if (InDesc.StrengthEnabled && InDesc.MaxTension > 0.0f)
        {
            ScopedConnection->BreakForce = std::max(ScopedConnection->BreakForce, InDesc.MaxTension);
        }
        ScopedConnection->Params.CollideConnected = (InDesc.Flags & JointChainCollideConnected) != 0;
        if (!CreateJoint(*ScopedConnection, ScopedStaged.ConnectionSlots[ScopedStaged.ConnectionCount - 1]))
        {
            DestroyRuntime(InRegistry, ScopedStaged);
            return ScopedFail(ChainError::Box2DRejected);
        }
    }

    // Distance joints are used at the endpoints; a pin is a rope tie to a static
    // anchor, so it can be cut or broken without destroying the anchor body.
    const JointConnectionKind ScopedPinKind =
        (InDesc.Flags & JointChainSpringRope) != 0 ? JointConnectionKind::SpringRope : JointConnectionKind::Rope;
    const auto ScopedAddPin = [&](bool InStart) -> bool
    {
        const b2BodyId ScopedAnchor = InStart ? ScopedAnchorA : ScopedAnchorB;
        if (!b2Body_IsValid(ScopedAnchor))
        {
            return true;
        }
        const std::uint32_t ScopedLink = InStart ? 0u : ScopedLinkCount - 1u;
        const auto* ScopedMarker = InRegistry.try_get<JointLinkComponent>(ScopedStaged.Links[ScopedLink]);
        if (ScopedMarker == nullptr)
        {
            return false;
        }
        ConnectionRuntime* ScopedConnection = ScopedNewConnection();
        if (ScopedConnection == nullptr)
        {
            return false;
        }
        const float ScopedTipSize = ScopedMarker->HalfLength + ScopedMarker->Radius;
        ScopedConnection->Kind = ScopedPinKind;
        ScopedConnection->Flags |= JointConnectionPinned;
        if ((InDesc.Flags & JointChainTrackStress) != 0)
        {
            ScopedConnection->Flags |= JointConnectionTrackStress;
        }
        ScopedConnection->ChainA = ScopedSlot;
        ScopedConnection->ChainB = ScopedSlot;
        ScopedConnection->BodyA = b2StoreBodyId(ScopedAnchor);
        ScopedConnection->BodyB = ScopedStaged.LinkBodies[ScopedLink];
        ScopedConnection->BreakForce = InDesc.BreakForce;
        ScopedConnection->BreakTorque = InDesc.BreakTorque;
        // The anchor sits on the path endpoint; the link tip stops half a gap
        // short of it, so the pin rests without pre-tension.
        ScopedConnection->Params.Length =
            std::max(ScopedLinkLengthMetres(ScopedLink) * 0.5f - ScopedTipSize, B2_LINEAR_SLOP);
        ScopedConnection->Params.Hertz = InDesc.Hertz;
        ScopedConnection->Params.DampingRatio = InDesc.DampingRatio;
        ScopedConnection->Params.LowerSpringForce = InDesc.LowerSpringForce;
        ScopedConnection->Params.UpperSpringForce = InDesc.UpperSpringForce;
        ScopedConnection->Params.DamageRate = InDesc.DamageRate;
        ScopedConnection->Params.DamageThreshold = InDesc.DamageThreshold;
        ScopedConnection->Params.BaseTension = InDesc.BaseTension;
        ScopedConnection->Params.MaxTension = InDesc.MaxTension;
        ScopedConnection->Params.TensionPerDistance = InDesc.TensionPerDistance;
        ScopedConnection->Params.TensionDistance = InDesc.TensionDistance;
        ScopedConnection->Params.StrengthEnabled = InDesc.StrengthEnabled;
        if (InDesc.StrengthEnabled && InDesc.MaxTension > 0.0f)
        {
            ScopedConnection->BreakForce = std::max(ScopedConnection->BreakForce, InDesc.MaxTension);
        }
        if (InStart)
        {
            ScopedConnection->LinkA = kNoLink;
            ScopedConnection->LinkB = 0;
            ScopedConnection->Params.LocalA = Vector2D{};
            ScopedConnection->Params.LocalB = Vector2D{-ScopedTipSize, 0.0f};
        }
        else
        {
            ScopedConnection->LinkA = static_cast<std::uint16_t>(ScopedLink);
            ScopedConnection->LinkB = kNoLink;
            // Body A is the anchor and body B is the link, so the tip offset
            // belongs to LocalB on both pins.
            ScopedConnection->Params.LocalA = Vector2D{};
            ScopedConnection->Params.LocalB = Vector2D{ScopedTipSize, 0.0f};
        }
        return CreateJoint(*ScopedConnection, ScopedStaged.ConnectionSlots[ScopedStaged.ConnectionCount - 1]);
    };
    if (!ScopedAddPin(true) || !ScopedAddPin(false))
    {
        DestroyRuntime(InRegistry, ScopedStaged);
        return ScopedFail(ChainError::Box2DRejected);
    }

    ScopedStaged.Active = true;
    Chains()[ScopedSlot] = ScopedStaged;
    ++MLiveChains;
    RefreshLinkFlags(InRegistry, ScopedSlot);
    if (InError != nullptr)
    {
        *InError = ChainError::None;
    }
    return JointChainHandle{ScopedSlot, ScopedStaged.Generation, ScopedStaged.StableId};
}

JointChainHandle PhysicsJointSystem::CreateChain(Ecs::Registry& InRegistry, JointChainComponent& InComponent,
                                                 ChainError* InError) noexcept
{
    JointChainDesc ScopedDesc;
    ScopedDesc.Kind = static_cast<JointChainKind>(InComponent.Kind);
    ScopedDesc.Flags = InComponent.Flags;
    ScopedDesc.Start = InComponent.Start;
    ScopedDesc.End = InComponent.End;
    ScopedDesc.SegmentCount = InComponent.SegmentCount;
    ScopedDesc.AnchorA = InComponent.AnchorA;
    ScopedDesc.AnchorB = InComponent.AnchorB;
    ScopedDesc.Radius = InComponent.Radius;
    ScopedDesc.Density = InComponent.Density;
    ScopedDesc.Friction = InComponent.Friction;
    ScopedDesc.Restitution = InComponent.Restitution;
    ScopedDesc.CategoryBits = InComponent.CategoryBits;
    ScopedDesc.MaskBits = InComponent.MaskBits;
    ScopedDesc.Hertz = InComponent.Hertz;
    ScopedDesc.DampingRatio = InComponent.DampingRatio;
    ScopedDesc.LowerAngle = InComponent.LowerAngle;
    ScopedDesc.UpperAngle = InComponent.UpperAngle;
    ScopedDesc.EnableLimit = InComponent.EnableLimit;
    ScopedDesc.EnableMotor = InComponent.EnableMotor;
    ScopedDesc.MotorSpeed = InComponent.MotorSpeed;
    ScopedDesc.MaxMotorTorque = InComponent.MaxMotorTorque;
    ScopedDesc.BreakForce = InComponent.DefaultBreakForce;
    ScopedDesc.BreakTorque = InComponent.DefaultBreakTorque;
    ScopedDesc.BaseTension = InComponent.BaseTension;
    ScopedDesc.MaxTension = InComponent.MaxTension;
    ScopedDesc.TensionPerDistance = InComponent.TensionPerDistance;
    ScopedDesc.TensionDistance = InComponent.TensionDistance;
    ScopedDesc.StrengthEnabled = InComponent.StrengthEnabled;
    ScopedDesc.LowerSpringForce = InComponent.DefaultLowerSpringForce;
    ScopedDesc.UpperSpringForce = InComponent.DefaultUpperSpringForce;
    ScopedDesc.DamageRate = InComponent.DefaultDamageRate;
    ScopedDesc.DamageThreshold = InComponent.DefaultDamageThreshold;
    ScopedDesc.LinkLayer = InComponent.LinkLayer;
    const std::span<const Vector2D> ScopedPath(InComponent.Path.data(),
                                               std::min<std::uint32_t>(InComponent.PathCount, 16u));
    if (ScopedPath.size() >= 2)
    {
        ScopedDesc.Path = ScopedPath;
    }
    if (InComponent.SectionCount > 0)
    {
        ScopedDesc.Sections = std::span<const JointChainSection>(InComponent.Sections.data(),
                                                                 std::min(InComponent.SectionCount, kMaxChainSections));
    }

    const JointChainHandle ScopedHandle = CreateChain(InRegistry, ScopedDesc, InError);
    if (!ScopedHandle.IsValid())
    {
        return ScopedHandle;
    }
    InComponent.ChainID = ScopedHandle.StableId;
    if (InComponent.BrokenMaskLow != 0 || InComponent.BrokenMaskHigh != 0)
    {
        if (!ApplyChainState(InRegistry, ScopedHandle, InComponent, InError))
        {
            DestroyChain(InRegistry, ScopedHandle);
            return JointChainHandle{};
        }
    }
    return ScopedHandle;
}

bool PhysicsJointSystem::DestroyChain(Ecs::Registry& InRegistry, JointChainHandle InHandle) noexcept
{
    ChainRuntime* ScopedChain = Runtime(InHandle);
    if (ScopedChain == nullptr)
    {
        return false;
    }
    DestroyRuntime(InRegistry, *ScopedChain);
    if (MLiveChains > 0)
    {
        --MLiveChains;
    }
    return true;
}

JointChainInfo PhysicsJointSystem::Info(JointChainHandle InHandle) const noexcept
{
    JointChainInfo ScopedInfo{};
    const ChainRuntime* ScopedChain = Runtime(InHandle);
    if (ScopedChain == nullptr)
    {
        return ScopedInfo;
    }
    ScopedInfo.ChainID = ScopedChain->StableId;
    ScopedInfo.Kind = ScopedChain->Kind;
    ScopedInfo.Flags = ScopedChain->Flags;
    ScopedInfo.Generation = ScopedChain->Generation;
    ScopedInfo.LinkCount = ScopedChain->LinkCount;
    ScopedInfo.AnchorA = ScopedChain->AnchorA;
    ScopedInfo.AnchorB = ScopedChain->AnchorB;
    for (std::uint32_t ScopedI = 0; ScopedI < ScopedChain->ConnectionCount; ++ScopedI)
    {
        const ConnectionRuntime* ScopedConnection = Connection(ScopedChain->ConnectionSlots[ScopedI]);
        if (ScopedConnection == nullptr)
        {
            continue;
        }
        if (ScopedConnection->Broken)
        {
            ++ScopedInfo.BrokenCount;
        }
        else if (ScopedConnection->Joint != 0)
        {
            ++ScopedInfo.JointCount;
        }
    }
    return ScopedInfo;
}

std::uint32_t PhysicsJointSystem::ChainCount() const noexcept
{
    return MLiveChains;
}

std::uint32_t PhysicsJointSystem::LinkCount() const noexcept
{
    if (MStorage == nullptr)
    {
        return 0;
    }
    std::uint32_t ScopedCount = 0;
    for (const ChainRuntime& ScopedChain : Chains())
    {
        if (ScopedChain.Active)
        {
            ScopedCount += ScopedChain.LinkCount;
        }
    }
    return ScopedCount;
}

std::uint32_t PhysicsJointSystem::JointCount() const noexcept
{
    if (MStorage == nullptr)
    {
        return 0;
    }
    std::uint32_t ScopedCount = 0;
    for (const ChainRuntime& ScopedChain : Chains())
    {
        if (!ScopedChain.Active)
        {
            continue;
        }
        for (std::uint32_t ScopedI = 0; ScopedI < ScopedChain.ConnectionCount; ++ScopedI)
        {
            const ConnectionRuntime* ScopedConnection = Connection(ScopedChain.ConnectionSlots[ScopedI]);
            if (ScopedConnection != nullptr && !ScopedConnection->Broken && ScopedConnection->Joint != 0)
            {
                ++ScopedCount;
            }
        }
    }
    return ScopedCount;
}

bool PhysicsJointSystem::LinkTransform(Ecs::Registry& InRegistry, JointChainHandle InHandle, std::uint32_t InLink,
                                       Transform2D* Out) const noexcept
{
    const ChainRuntime* ScopedChain = Runtime(InHandle);
    if (ScopedChain == nullptr || InLink >= ScopedChain->LinkCount || Out == nullptr || MPhysics == nullptr)
    {
        return false;
    }
    const Entity ScopedEntity = ScopedChain->Links[InLink];
    if (ScopedEntity == entt::null || !InRegistry.valid(ScopedEntity))
    {
        return false;
    }
    if (const auto* ScopedTransform = InRegistry.try_get<Transform2D>(ScopedEntity); ScopedTransform != nullptr)
    {
        // PhysicsSystem::Interpolate writes the visual transform; use it so the
        // debug draw matches what the renderer shows.
        *Out = *ScopedTransform;
        return true;
    }
    return false;
}
}
