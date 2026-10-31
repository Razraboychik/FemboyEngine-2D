// FemboyEngine2D - Box2D v3 rope/chain construction.
#include "Engine/Physics/PhysicsJoints.hpp"

#include "Engine/ECS/Components.hpp"
#include "Engine/ECS/Schedule.hpp"

#include <box2d/box2d.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace Fby
{
namespace
{

// Box2D works in metres; gameplay in pixels. PhysicsSystem is the single
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
[[nodiscard]] void* BodyUserData(Entity InEntity) noexcept
{
    return reinterpret_cast<void*>(static_cast<std::uintptr_t>(entt::to_integral(InEntity)) + 1u);
}

[[nodiscard]] bool BodyIsLive(std::uint64_t InPacked) noexcept
{
    return InPacked != 0 && b2Body_IsValid(b2LoadBodyId(InPacked));
}

// A capsule link is a line of two semicircles. `halfLength` is the distance
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
}

PhysicsJointSystem::~PhysicsJointSystem()
{
    // The world may already be gone, so the destructor only drops the binding.
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
    return true;
}

void PhysicsJointSystem::Shutdown(Ecs::Registry& InRegistry) noexcept
{
    for (ChainRuntime& ScopedChain : MChains)
    {
        if (ScopedChain.active)
        {
            DestroyRuntime(InRegistry, ScopedChain);
        }
    }
    MLiveChains = 0;
    MPhysics = nullptr;
}

bool PhysicsJointSystem::IsValid(JointChainHandle InHandle) const noexcept
{
    return Runtime(InHandle) != nullptr;
}

PhysicsJointSystem::ChainRuntime* PhysicsJointSystem::Runtime(JointChainHandle InHandle) noexcept
{
    if (InHandle.index >= kMaxChains)
    {
        return nullptr;
    }
    ChainRuntime& ScopedChain = MChains[InHandle.index];
    if (!ScopedChain.active || ScopedChain.generation != InHandle.generation)
    {
        return nullptr;
    }
    return &ScopedChain;
}

const PhysicsJointSystem::ChainRuntime* PhysicsJointSystem::Runtime(JointChainHandle InHandle) const noexcept
{
    if (InHandle.index >= kMaxChains)
    {
        return nullptr;
    }
    const ChainRuntime& ScopedChain = MChains[InHandle.index];
    if (!ScopedChain.active || ScopedChain.generation != InHandle.generation)
    {
        return nullptr;
    }
    return &ScopedChain;
}

void PhysicsJointSystem::ResetRuntime(ChainRuntime& InChain) noexcept
{
    const std::uint32_t ScopedGeneration = InChain.generation;
    InChain = ChainRuntime{};
    InChain.generation = ScopedGeneration;
}

void PhysicsJointSystem::DestroyRuntime(Ecs::Registry& InRegistry, ChainRuntime& InChain) noexcept
{
    // Joints first: destroying a body would invalidate its joint ids, and a
    for (std::uint32_t ScopedI = 0; ScopedI < InChain.jointCount; ++ScopedI)
    {
        const std::uint64_t ScopedPacked = InChain.joints[ScopedI];
        if (ScopedPacked == 0)
        {
            continue;
        }
        const b2JointId ScopedJoint = b2LoadJointId(ScopedPacked);
        if (b2Joint_IsValid(ScopedJoint))
        {
            b2DestroyJoint(ScopedJoint);
        }
        InChain.joints[ScopedI] = 0;
    }

    // Anchor bodies created by this system (not gameplay entities).
    if (InChain.ownsAnchorBodyA && BodyIsLive(InChain.anchorBodyA))
    {
        if (MPhysics != nullptr)
        {
            MPhysics->UntrackBody(InChain.anchorBodyA);
        }
        b2DestroyBody(b2LoadBodyId(InChain.anchorBodyA));
    }
    if (InChain.ownsAnchorBodyB && BodyIsLive(InChain.anchorBodyB))
    {
        if (MPhysics != nullptr)
        {
            MPhysics->UntrackBody(InChain.anchorBodyB);
        }
        b2DestroyBody(b2LoadBodyId(InChain.anchorBodyB));
    }

    // Link bodies. The entity may have been destroyed by gameplay before the
    for (std::uint32_t ScopedI = 0; ScopedI < InChain.linkCount; ++ScopedI)
    {
        const std::uint64_t ScopedPacked = InChain.linkBodies[ScopedI];
        if (ScopedPacked != 0 && b2Body_IsValid(b2LoadBodyId(ScopedPacked)))
        {
            if (MPhysics != nullptr)
            {
                MPhysics->UntrackBody(ScopedPacked);
            }
            b2DestroyBody(b2LoadBodyId(ScopedPacked));
        }
        const Entity ScopedEntity = InChain.links[ScopedI];
        if (ScopedEntity != entt::null && InRegistry.valid(ScopedEntity))
        {
            const auto* ScopedMarker = InRegistry.try_get<JointLinkComponent>(ScopedEntity);
            // Only destroy an entity that still belongs to this chain. EnTT
            if (ScopedMarker != nullptr && ScopedMarker->ChainSlot == InChain.index &&
                ScopedMarker->ChainGeneration == InChain.generation)
            {
                InRegistry.destroy(ScopedEntity);
            }
        }
        InChain.linkBodies[ScopedI] = 0;
    }

    const std::uint32_t ScopedNextGeneration = InChain.generation + 1u == 0 ? 1u : InChain.generation + 1u;
    if (InChain.stableId.IsValid())
    {
        MStableIds.Release(InChain.stableId);
    }
    InChain = ChainRuntime{};
    InChain.generation = ScopedNextGeneration;
}

JointChainHandle PhysicsJointSystem::CreateChain(Ecs::Registry& InRegistry, const JointChainDesc& InDesc,
                                                 ChainError* InError) noexcept
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
    if (MPhysics == nullptr || !MPhysics->IsValid())
    {
        return ScopedFail(ChainError::NotInitialized);
    }
    if (InDesc.kind != JointChainKind::Rope && InDesc.kind != JointChainKind::Chain)
    {
        return ScopedFail(ChainError::InvalidDesc);
    }
    if (InDesc.radius <= 0.0f || InDesc.density <= 0.0f || InDesc.maskBits == 0)
    {
        return ScopedFail(ChainError::InvalidDesc);
    }

    std::array<Vector2D, kMaxLinksPerChain + 1> ScopedPoints{};
    std::uint32_t ScopedPointCount = 0;
    if (!InDesc.path.empty())
    {
        if (InDesc.path.size() < 2 || InDesc.path.size() > ScopedPoints.size())
        {
            return ScopedFail(ChainError::InvalidDesc);
        }
        ScopedPointCount = static_cast<std::uint32_t>(InDesc.path.size());
        for (std::uint32_t ScopedI = 0; ScopedI < ScopedPointCount; ++ScopedI)
        {
            ScopedPoints[ScopedI] = InDesc.path[ScopedI];
        }
    }
    else
    {
        const std::uint32_t ScopedSegments = std::clamp(InDesc.segmentCount, 1u, kMaxLinksPerChain);
        if (ScopedSegments < 1)
        {
            return ScopedFail(ChainError::InvalidDesc);
        }
        const Vector2D ScopedDelta = InDesc.end - InDesc.start;
        if (ScopedDelta.LengthSquared() <= 1.0e-8f)
        {
            return ScopedFail(ChainError::InvalidDesc);
        }
        ScopedPointCount = ScopedSegments + 1;
        for (std::uint32_t ScopedI = 0; ScopedI < ScopedPointCount; ++ScopedI)
        {
            ScopedPoints[ScopedI] =
                InDesc.start + ScopedDelta * (static_cast<float>(ScopedI) / static_cast<float>(ScopedSegments));
        }
    }

    const std::uint32_t ScopedSegmentCount = ScopedPointCount - 1;
    if (ScopedSegmentCount == 0 || ScopedSegmentCount > kMaxLinksPerChain)
    {
        return ScopedFail(ChainError::OutOfLinks);
    }

    b2BodyId ScopedAnchorA = b2_nullBodyId;
    b2BodyId ScopedAnchorB = b2_nullBodyId;
    if (InDesc.anchorA != entt::null)
    {
        const auto* ScopedBody = InRegistry.try_get<RigidBody2D>(InDesc.anchorA);
        if (ScopedBody == nullptr || !BodyIsLive(ScopedBody->Body))
        {
            return ScopedFail(ChainError::AnchorBodyMissing);
        }
        ScopedAnchorA = b2LoadBodyId(ScopedBody->Body);
    }
    if (InDesc.anchorB != entt::null)
    {
        const auto* ScopedBody = InRegistry.try_get<RigidBody2D>(InDesc.anchorB);
        if (ScopedBody == nullptr || !BodyIsLive(ScopedBody->Body))
        {
            return ScopedFail(ChainError::AnchorBodyMissing);
        }
        ScopedAnchorB = b2LoadBodyId(ScopedBody->Body);
    }

    std::uint32_t ScopedSlot = kMaxChains;
    for (std::uint32_t ScopedI = 0; ScopedI < kMaxChains; ++ScopedI)
    {
        if (!MChains[ScopedI].active)
        {
            ScopedSlot = ScopedI;
            break;
        }
    }
    if (ScopedSlot == kMaxChains)
    {
        return ScopedFail(ChainError::OutOfChains);
    }

    ChainRuntime ScopedStaged{};
    ScopedStaged.index = ScopedSlot;
    ScopedStaged.generation = MChains[ScopedSlot].generation == 0 ? 1u : MChains[ScopedSlot].generation;
    ScopedStaged.stableId = MStableIds.Allocate();
    if (!ScopedStaged.stableId.IsValid())
    {
        return ScopedFail(ChainError::OutOfChains);
    }
    ScopedStaged.kind = InDesc.kind;
    ScopedStaged.flags = InDesc.flags;
    ScopedStaged.anchorA = InDesc.anchorA;
    ScopedStaged.anchorB = InDesc.anchorB;
    ScopedStaged.radius = InDesc.radius / MPhysics->PixelsPerMeter();
    ScopedStaged.linkCount = ScopedSegmentCount;

    const b2WorldId ScopedWorld = b2LoadWorldId(static_cast<std::uint32_t>(MPhysics->WorldId()));
    const float ScopedPixelsPerMeter = MPhysics->PixelsPerMeter();

    for (std::uint32_t ScopedI = 0; ScopedI < ScopedSegmentCount; ++ScopedI)
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
        float ScopedRadiusMetres = ScopedStaged.radius;
        // A capsule cannot be longer than the segment; clamp the radius so a
        ScopedRadiusMetres = std::min(ScopedRadiusMetres, ScopedLengthMetres * 0.49f);
        ScopedRadiusMetres = std::max(ScopedRadiusMetres, 0.001f);
        // Box2D rejects a capsule whose centres are closer than one linear slop.
        const float ScopedHalfLength = std::max(ScopedLengthMetres * 0.5f - ScopedRadiusMetres, 0.6f * B2_LINEAR_SLOP);
        const Vector2D ScopedCentre = (ScopedFrom + ScopedTo) * 0.5f;
        const float ScopedAngle = std::atan2(ScopedDelta.Y, ScopedDelta.X);

        const Entity ScopedEntity = InRegistry.create();
        ScopedStaged.links[ScopedI] = ScopedEntity;
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
            ScopedStaged.linkCount = ScopedI;
            DestroyRuntime(InRegistry, ScopedStaged);
            return ScopedFail(ChainError::Box2DRejected);
        }
        if (MPhysics != nullptr)
        {
            MPhysics->TrackBody(ScopedEntity, b2StoreBodyId(ScopedBody));
        }

        b2ShapeDef ScopedShapeDefinition = b2DefaultShapeDef();
        ScopedShapeDefinition.density = InDesc.density;
        ScopedShapeDefinition.material.friction = InDesc.friction;
        ScopedShapeDefinition.material.restitution = InDesc.restitution;
        ScopedShapeDefinition.filter.categoryBits = InDesc.categoryBits;
        // Connected links never collide with each other: a rope whose segments
        ScopedShapeDefinition.filter.maskBits = InDesc.maskBits & ~InDesc.categoryBits;
        ScopedShapeDefinition.enableContactEvents = (InDesc.flags & JointChainContactEvents) != 0;
        ScopedShapeDefinition.enableHitEvents = (InDesc.flags & JointChainContactEvents) != 0;
        const b2Capsule ScopedCapsule = MakeCapsule(ScopedHalfLength, ScopedRadiusMetres);
        const b2ShapeId ScopedShape = b2CreateCapsuleShape(ScopedBody, &ScopedShapeDefinition, &ScopedCapsule);
        if (!b2Shape_IsValid(ScopedShape))
        {
            b2DestroyBody(ScopedBody);
            InRegistry.destroy(ScopedEntity);
            ScopedStaged.linkCount = ScopedI;
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
        ScopedCollider.Density = InDesc.density;
        ScopedCollider.Friction = InDesc.friction;
        ScopedCollider.Restitution = InDesc.restitution;
        ScopedCollider.CategoryBits = static_cast<std::uint32_t>(InDesc.categoryBits);
        ScopedCollider.MaskBits = static_cast<std::uint32_t>(InDesc.maskBits);
        ScopedCollider.Shape = b2StoreShapeId(ScopedShape);
        ScopedCollider.ContactEvents = (InDesc.flags & JointChainContactEvents) != 0;

        InRegistry.emplace<RigidBody2D>(ScopedEntity, ScopedRigid);
        InRegistry.emplace<Collider2D>(ScopedEntity, ScopedCollider);
        InRegistry.emplace<JointLinkComponent>(ScopedEntity, JointLinkComponent{ScopedStaged.stableId,
                                                                                static_cast<std::uint16_t>(ScopedI), 0,
                                                                                ScopedHalfLength, ScopedRadiusMetres,
                                                                                ScopedSlot, ScopedStaged.generation});

        ScopedStaged.linkBodies[ScopedI] = ScopedRigid.Body;
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
            MPhysics->TrackBody(entt::null, b2StoreBodyId(ScopedAnchor));
        }
        return ScopedAnchor;
    };

    Vector2D ScopedStartWorld = ScopedPoints[0];
    Vector2D ScopedEndWorld = ScopedPoints[ScopedPointCount - 1];
    if (InDesc.anchorA != entt::null)
    {
        ScopedStartWorld = ToPixels(b2Body_GetPosition(ScopedAnchorA), ScopedPixelsPerMeter);
    }
    else if ((InDesc.flags & JointChainPinStart) != 0)
    {
        ScopedAnchorA = ScopedMakeStaticAnchor(ScopedStartWorld);
        if (!b2Body_IsValid(ScopedAnchorA))
        {
            DestroyRuntime(InRegistry, ScopedStaged);
            return ScopedFail(ChainError::Box2DRejected);
        }
        ScopedStaged.ownsAnchorBodyA = true;
    }
    if (InDesc.anchorB != entt::null)
    {
        ScopedEndWorld = ToPixels(b2Body_GetPosition(ScopedAnchorB), ScopedPixelsPerMeter);
    }
    else if ((InDesc.flags & JointChainPinEnd) != 0)
    {
        ScopedAnchorB = ScopedMakeStaticAnchor(ScopedEndWorld);
        if (!b2Body_IsValid(ScopedAnchorB))
        {
            DestroyRuntime(InRegistry, ScopedStaged);
            return ScopedFail(ChainError::Box2DRejected);
        }
        ScopedStaged.ownsAnchorBodyB = true;
    }
    ScopedStaged.anchorBodyA = b2StoreBodyId(ScopedAnchorA);
    ScopedStaged.anchorBodyB = b2StoreBodyId(ScopedAnchorB);

    const auto ScopedAddJoint = [&](std::uint64_t InPacked) -> bool
    {
        if (ScopedStaged.jointCount >= kMaxJointsPerChain)
        {
            return false;
        }
        ScopedStaged.joints[ScopedStaged.jointCount++] = InPacked;
        return true;
    };

    for (std::uint32_t ScopedI = 0; ScopedI + 1 < ScopedSegmentCount; ++ScopedI)
    {
        const b2BodyId ScopedBodyA = b2LoadBodyId(ScopedStaged.linkBodies[ScopedI]);
        const b2BodyId ScopedBodyB = b2LoadBodyId(ScopedStaged.linkBodies[ScopedI + 1]);
        const auto* ScopedMarkerA = InRegistry.try_get<JointLinkComponent>(ScopedStaged.links[ScopedI]);
        const auto* ScopedMarkerB = InRegistry.try_get<JointLinkComponent>(ScopedStaged.links[ScopedI + 1]);
        if (ScopedMarkerA == nullptr || ScopedMarkerB == nullptr)
        {
            DestroyRuntime(InRegistry, ScopedStaged);
            return ScopedFail(ChainError::InvalidDesc);
        }
        const b2Vec2 ScopedTipA = b2Vec2{ScopedMarkerA->HalfLength + ScopedMarkerA->Radius, 0.0f};
        const b2Vec2 ScopedTipB = b2Vec2{-(ScopedMarkerB->HalfLength + ScopedMarkerB->Radius), 0.0f};
        const bool ScopedCollide = (InDesc.flags & JointChainCollideConnected) != 0;

        if (InDesc.kind == JointChainKind::Rope)
        {
            b2DistanceJointDef ScopedDefinition = b2DefaultDistanceJointDef();
            ScopedDefinition.base.bodyIdA = ScopedBodyA;
            ScopedDefinition.base.bodyIdB = ScopedBodyB;
            ScopedDefinition.base.localFrameA = JointFrame(ScopedTipA);
            ScopedDefinition.base.localFrameB = JointFrame(ScopedTipB);
            ScopedDefinition.base.collideConnected = ScopedCollide;
            // The two caps touch by construction. Box2D rejects a rest length
            ScopedDefinition.length = B2_LINEAR_SLOP;
            if ((InDesc.flags & JointChainSpringRope) != 0)
            {
                ScopedDefinition.enableSpring = true;
                ScopedDefinition.hertz = InDesc.hertz;
                ScopedDefinition.dampingRatio = InDesc.dampingRatio;
            }
            const b2JointId ScopedJoint = b2CreateDistanceJoint(ScopedWorld, &ScopedDefinition);
            if (!b2Joint_IsValid(ScopedJoint) || !ScopedAddJoint(b2StoreJointId(ScopedJoint)))
            {
                DestroyRuntime(InRegistry, ScopedStaged);
                return ScopedFail(ChainError::Box2DRejected);
            }
        }
        else
        {
            b2RevoluteJointDef ScopedDefinition = b2DefaultRevoluteJointDef();
            ScopedDefinition.base.bodyIdA = ScopedBodyA;
            ScopedDefinition.base.bodyIdB = ScopedBodyB;
            ScopedDefinition.base.localFrameA = JointFrame(ScopedTipA);
            ScopedDefinition.base.localFrameB = JointFrame(ScopedTipB);
            ScopedDefinition.base.collideConnected = ScopedCollide;
            ScopedDefinition.enableLimit = InDesc.enableLimit;
            ScopedDefinition.lowerAngle = InDesc.lowerAngle;
            ScopedDefinition.upperAngle = InDesc.upperAngle;
            ScopedDefinition.enableMotor = InDesc.enableMotor;
            ScopedDefinition.motorSpeed = InDesc.motorSpeed;
            ScopedDefinition.maxMotorTorque = InDesc.maxMotorTorque;
            const b2JointId ScopedJoint = b2CreateRevoluteJoint(ScopedWorld, &ScopedDefinition);
            if (!b2Joint_IsValid(ScopedJoint) || !ScopedAddJoint(b2StoreJointId(ScopedJoint)))
            {
                DestroyRuntime(InRegistry, ScopedStaged);
                return ScopedFail(ChainError::Box2DRejected);
            }
        }
    }

    // Distance joints are used at the endpoints even for a chain: a revolute
    if (BodyIsLive(ScopedStaged.anchorBodyA))
    {
        const auto* ScopedMarker = InRegistry.try_get<JointLinkComponent>(ScopedStaged.links[0]);
        const b2BodyId ScopedLink = b2LoadBodyId(ScopedStaged.linkBodies[0]);
        const b2Vec2 ScopedTip = b2Vec2{-(ScopedMarker->HalfLength + ScopedMarker->Radius), 0.0f};
        const b2Vec2 ScopedLocalAnchor = b2Body_GetLocalPoint(ScopedAnchorA, b2Body_GetPosition(ScopedAnchorA));
        b2DistanceJointDef ScopedDefinition = b2DefaultDistanceJointDef();
        ScopedDefinition.base.bodyIdA = ScopedAnchorA;
        ScopedDefinition.base.bodyIdB = ScopedLink;
        ScopedDefinition.base.localFrameA = JointFrame(ScopedLocalAnchor);
        ScopedDefinition.base.localFrameB = JointFrame(ScopedTip);
        ScopedDefinition.base.collideConnected = false;
        ScopedDefinition.length = B2_LINEAR_SLOP;
        const b2JointId ScopedJoint = b2CreateDistanceJoint(ScopedWorld, &ScopedDefinition);
        if (!b2Joint_IsValid(ScopedJoint) || !ScopedAddJoint(b2StoreJointId(ScopedJoint)))
        {
            DestroyRuntime(InRegistry, ScopedStaged);
            return ScopedFail(ChainError::Box2DRejected);
        }
    }
    if (BodyIsLive(ScopedStaged.anchorBodyB))
    {
        const std::uint32_t ScopedLast = ScopedSegmentCount - 1;
        const auto* ScopedMarker = InRegistry.try_get<JointLinkComponent>(ScopedStaged.links[ScopedLast]);
        const b2BodyId ScopedLink = b2LoadBodyId(ScopedStaged.linkBodies[ScopedLast]);
        const b2Vec2 ScopedTip = b2Vec2{ScopedMarker->HalfLength + ScopedMarker->Radius, 0.0f};
        const b2Vec2 ScopedLocalAnchor = b2Body_GetLocalPoint(ScopedAnchorB, b2Body_GetPosition(ScopedAnchorB));
        b2DistanceJointDef ScopedDefinition = b2DefaultDistanceJointDef();
        ScopedDefinition.base.bodyIdA = ScopedLink;
        ScopedDefinition.base.bodyIdB = ScopedAnchorB;
        ScopedDefinition.base.localFrameA = JointFrame(ScopedTip);
        ScopedDefinition.base.localFrameB = JointFrame(ScopedLocalAnchor);
        ScopedDefinition.base.collideConnected = false;
        ScopedDefinition.length = B2_LINEAR_SLOP;
        const b2JointId ScopedJoint = b2CreateDistanceJoint(ScopedWorld, &ScopedDefinition);
        if (!b2Joint_IsValid(ScopedJoint) || !ScopedAddJoint(b2StoreJointId(ScopedJoint)))
        {
            DestroyRuntime(InRegistry, ScopedStaged);
            return ScopedFail(ChainError::Box2DRejected);
        }
    }

    ScopedStaged.active = true;
    MChains[ScopedSlot] = ScopedStaged;
    ++MLiveChains;
    if (InError != nullptr)
    {
        *InError = ChainError::None;
    }
    return JointChainHandle{ScopedSlot, ScopedStaged.generation, ScopedStaged.stableId};
}

JointChainHandle PhysicsJointSystem::CreateChain(Ecs::Registry& InRegistry, JointChainComponent& InComponent,
                                                 ChainError* InError) noexcept
{
    JointChainDesc ScopedDesc;
    ScopedDesc.kind = static_cast<JointChainKind>(InComponent.Kind);
    ScopedDesc.flags = InComponent.Flags;
    ScopedDesc.start = InComponent.Start;
    ScopedDesc.end = InComponent.End;
    ScopedDesc.segmentCount = InComponent.SegmentCount;
    ScopedDesc.anchorA = InComponent.AnchorA;
    ScopedDesc.anchorB = InComponent.AnchorB;
    ScopedDesc.radius = InComponent.Radius;
    ScopedDesc.density = InComponent.Density;
    ScopedDesc.friction = InComponent.Friction;
    ScopedDesc.restitution = InComponent.Restitution;
    ScopedDesc.categoryBits = InComponent.CategoryBits;
    ScopedDesc.maskBits = InComponent.MaskBits;
    ScopedDesc.hertz = InComponent.Hertz;
    ScopedDesc.dampingRatio = InComponent.DampingRatio;
    ScopedDesc.lowerAngle = InComponent.LowerAngle;
    ScopedDesc.upperAngle = InComponent.UpperAngle;
    ScopedDesc.enableLimit = InComponent.EnableLimit;
    ScopedDesc.enableMotor = InComponent.EnableMotor;
    ScopedDesc.motorSpeed = InComponent.MotorSpeed;
    ScopedDesc.maxMotorTorque = InComponent.MaxMotorTorque;
    ScopedDesc.linkLayer = InComponent.LinkLayer;
    const std::span<const Vector2D> ScopedPath(InComponent.Path.data(),
                                               std::min<std::uint32_t>(InComponent.PathCount, 16u));
    if (ScopedPath.size() >= 2)
    {
        ScopedDesc.path = ScopedPath;
    }

    const JointChainHandle ScopedHandle = CreateChain(InRegistry, ScopedDesc, InError);
    if (ScopedHandle.IsValid())
    {
        InComponent.ChainID = ScopedHandle.stableId;
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
    ScopedInfo.chainId = ScopedChain->stableId;
    ScopedInfo.kind = ScopedChain->kind;
    ScopedInfo.flags = ScopedChain->flags;
    ScopedInfo.generation = ScopedChain->generation;
    ScopedInfo.linkCount = ScopedChain->linkCount;
    ScopedInfo.jointCount = ScopedChain->jointCount;
    ScopedInfo.anchorA = ScopedChain->anchorA;
    ScopedInfo.anchorB = ScopedChain->anchorB;
    return ScopedInfo;
}

std::uint32_t PhysicsJointSystem::ChainCount() const noexcept
{
    return MLiveChains;
}

std::uint32_t PhysicsJointSystem::LinkCount() const noexcept
{
    std::uint32_t ScopedCount = 0;
    for (const ChainRuntime& ScopedChain : MChains)
    {
        if (ScopedChain.active)
        {
            ScopedCount += ScopedChain.linkCount;
        }
    }
    return ScopedCount;
}

std::uint32_t PhysicsJointSystem::JointCount() const noexcept
{
    std::uint32_t ScopedCount = 0;
    for (const ChainRuntime& ScopedChain : MChains)
    {
        if (ScopedChain.active)
        {
            ScopedCount += ScopedChain.jointCount;
        }
    }
    return ScopedCount;
}

bool PhysicsJointSystem::LinkTransform(Ecs::Registry& InRegistry, JointChainHandle InHandle, std::uint32_t InLink,
                                       Transform2D* Out) const noexcept
{
    const ChainRuntime* ScopedChain = Runtime(InHandle);
    if (ScopedChain == nullptr || InLink >= ScopedChain->linkCount || Out == nullptr || MPhysics == nullptr)
    {
        return false;
    }
    const Entity ScopedEntity = ScopedChain->links[InLink];
    if (ScopedEntity == entt::null || !InRegistry.valid(ScopedEntity))
    {
        return false;
    }
    if (const auto* ScopedTransform = InRegistry.try_get<Transform2D>(ScopedEntity); ScopedTransform != nullptr)
    {
        // PhysicsSystem::Interpolate writes the visual transform; use it so a
        *Out = *ScopedTransform;
        return true;
    }
    return false;
}
}
