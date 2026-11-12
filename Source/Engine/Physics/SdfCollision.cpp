// FemboyEngine2D - SDF terrain collision for Box2D v3.
#include "Engine/Physics/SdfCollision.hpp"

#include "Engine/Core/SimMath.hpp"
#include "Engine/Core/ThreadAffinity.hpp"
#include "Engine/ECS/SystemGraph.hpp"
#include "Engine/Physics/SdfBrickMap.hpp"

#include <box2d/box2d.h>

#include <algorithm>
#include <cmath>

namespace Fby
{
namespace
{

[[nodiscard]] b2Vec2 ToBoxVelocity(Vector2D InValue, float InPixelsPerMeter) noexcept
{
    return b2Vec2{InValue.X / InPixelsPerMeter, InValue.Y / InPixelsPerMeter};
}

[[nodiscard]] Vector2D ToPixelsVelocity(b2Vec2 InValue, float InPixelsPerMeter) noexcept
{
    return Vector2D{InValue.x * InPixelsPerMeter, InValue.y * InPixelsPerMeter};
}

}

float SdfField::At(std::uint32_t InX, std::uint32_t InY) const noexcept
{
    if (InX >= width || InY >= height || samples == nullptr)
    {
        return outsideDistance;
    }
    return View2D()[std::array<std::size_t, 2>{static_cast<std::size_t>(InY), static_cast<std::size_t>(InX)}];
}

float SdfField::Sample(Vector2D InWorld) const noexcept
{
    if (!Valid())
    {
        return outsideDistance;
    }
    const float ScopedX = (InWorld.X - origin.X) / cellSize.X;
    const float ScopedY = (InWorld.Y - origin.Y) / cellSize.Y;
    // Written as a positive range check so a NaN world position also leaves the
    // grid instead of being cast to an integer.
    if (!(ScopedX >= 0.0f && ScopedY >= 0.0f && ScopedX <= static_cast<float>(width - 1) &&
          ScopedY <= static_cast<float>(height - 1)))
    {
        return outsideDistance;
    }
    const auto ScopedX0 = static_cast<std::uint32_t>(ScopedX);
    const auto ScopedY0 = static_cast<std::uint32_t>(ScopedY);
    const std::uint32_t ScopedX1 = std::min(ScopedX0 + 1, width - 1);
    const std::uint32_t ScopedY1 = std::min(ScopedY0 + 1, height - 1);
    const float ScopedFx = ScopedX - static_cast<float>(ScopedX0);
    const float ScopedFy = ScopedY - static_cast<float>(ScopedY0);
    const float Scoped00 = At(ScopedX0, ScopedY0);
    const float Scoped10 = At(ScopedX1, ScopedY0);
    const float Scoped01 = At(ScopedX0, ScopedY1);
    const float Scoped11 = At(ScopedX1, ScopedY1);
    const float ScopedTop = Scoped00 + (Scoped10 - Scoped00) * ScopedFx;
    const float ScopedBottom = Scoped01 + (Scoped11 - Scoped01) * ScopedFx;
    return ScopedTop + (ScopedBottom - ScopedTop) * ScopedFy;
}

Vector2D SdfField::Gradient(Vector2D InWorld) const noexcept
{
    const float ScopedStepX = std::max(cellSize.X, 0.001f);
    const float ScopedStepY = std::max(cellSize.Y, 0.001f);
    const float ScopedDx =
        (Sample(Vector2D{InWorld.X + ScopedStepX, InWorld.Y}) - Sample(Vector2D{InWorld.X - ScopedStepX, InWorld.Y})) /
        (2.0f * ScopedStepX);
    const float ScopedDy =
        (Sample(Vector2D{InWorld.X, InWorld.Y + ScopedStepY}) - Sample(Vector2D{InWorld.X, InWorld.Y - ScopedStepY})) /
        (2.0f * ScopedStepY);
    Vector2D ScopedNormal{ScopedDx, ScopedDy};
    const float ScopedLength = std::sqrt(ScopedNormal.LengthSquared());
    if (ScopedLength < 1.0e-6f)
    {
        return Vector2D{0.0f, -1.0f};
    }
    return ScopedNormal / ScopedLength;
}

void SdfCollisionSystem::Initialize(const SdfCollisionDesc& InDesc) noexcept
{
    MDesc = InDesc;
    MContacts.fill(SdfContact{});
    MContactCount = 0;
    MManifolds.fill(SdfContactManifold{});
    MManifoldCount = 0;
    MSequence = 0;
    MLastCorrected = 0;
    MDroppedContacts = 0;
}

void SdfCollisionSystem::SetField(const SdfField& InField) noexcept
{
    MField = InField;
}

std::uint32_t SdfCollisionSystem::Step(Ecs::Registry& InRegistry, PhysicsSystem& InPhysics, float InFixedDt) noexcept
{
    if (!RequireMainThread("SdfCollisionSystem::Step"))
    {
        return 0;
    }
    MContactCount = 0;
    MManifoldCount = 0;
    MBroadphaseSkipped = 0;
    MLastCorrected = 0;
    if (!MField.Valid() || InFixedDt <= 0.0f)
    {
        return 0;
    }
    // A body spawned after the physics prepare pass still needs its native body
    // before the field can sample a pose for it.
    InPhysics.EnsureBodies(InRegistry);
    const float ScopedPixelsPerMeter = std::max(InPhysics.PixelsPerMeter(), 0.001f);
    auto ScopedView = InRegistry.view<RigidBody2D, Collider2D, Transform2D>();
    for (const Entity ScopedEntity : ScopedView)
    {
        RigidBody2D& ScopedBody = ScopedView.get<RigidBody2D>(ScopedEntity);
        const Collider2D& ScopedCollider = ScopedView.get<Collider2D>(ScopedEntity);
        Transform2D& ScopedTransform = ScopedView.get<Transform2D>(ScopedEntity);
        if (ScopedBody.Body == 0 || ScopedCollider.Sensor || (ScopedBody.Flags & RigidBodyEnabled) == 0)
        {
            continue;
        }
        if (MDesc.dynamicOnly && ScopedBody.Type != BodyType::Dynamic)
        {
            continue;
        }
        const b2BodyId ScopedBodyId = b2LoadBodyId(ScopedBody.Body);
        if (!b2Body_IsValid(ScopedBodyId))
        {
            continue;
        }
        const b2WorldTransform ScopedWorld = b2Body_GetTransform(ScopedBodyId);
        const Vector2D ScopedCenter{static_cast<float>(ScopedWorld.p.x) * ScopedPixelsPerMeter,
                                    static_cast<float>(ScopedWorld.p.y) * ScopedPixelsPerMeter};
        const float ScopedRotation = b2Rot_GetAngle(ScopedWorld.q);
        const float ScopedCos = Sim::Cos(ScopedRotation);
        const float ScopedSin = Sim::Sin(ScopedRotation);
        // Brick broadphase: a uniform outside brick that is farther than the
        // collider reach cannot produce a contact, so the sample grid is skipped.
        if (MBricks != nullptr)
        {
            const SdfBrickHeader* ScopedHeader = MBricks->HeaderAt(ScopedCenter);
            if (ScopedHeader != nullptr && ScopedHeader->State == SdfBrickState::Empty)
            {
                const float ScopedReach = std::sqrt(ScopedCollider.HalfExtents.X * ScopedCollider.HalfExtents.X +
                                                    ScopedCollider.HalfExtents.Y * ScopedCollider.HalfExtents.Y) +
                                          std::max(MField.cellSize.X, MField.cellSize.Y);
                if (ScopedHeader->MinDistance > ScopedReach)
                {
                    ++MBroadphaseSkipped;
                    continue;
                }
            }
        }
        const std::uint32_t ScopedSamples = std::max(MDesc.subSamples, 1u) + 1u;

        float ScopedMinDistance = MField.outsideDistance;
        Vector2D ScopedDeepest = ScopedCenter;
        bool ScopedTouchedInside = false;
        // Up to four deepest inside samples become the contact manifold points.
        std::array<Vector2D, 4> ScopedManifoldPoints{};
        std::array<float, 4> ScopedManifoldDepths{};
        std::uint32_t ScopedManifoldPointsCount = 0;
        for (std::uint32_t ScopedIy = 0; ScopedIy < ScopedSamples; ++ScopedIy)
        {
            const float ScopedY = -ScopedCollider.HalfExtents.Y + (2.0f * ScopedCollider.HalfExtents.Y) *
                                                                      static_cast<float>(ScopedIy) /
                                                                      static_cast<float>(ScopedSamples - 1);
            for (std::uint32_t ScopedIx = 0; ScopedIx < ScopedSamples; ++ScopedIx)
            {
                const float ScopedX = -ScopedCollider.HalfExtents.X + (2.0f * ScopedCollider.HalfExtents.X) *
                                                                          static_cast<float>(ScopedIx) /
                                                                          static_cast<float>(ScopedSamples - 1);
                const Vector2D ScopedLocal{ScopedX, ScopedY};
                const Vector2D ScopedSample{ScopedCenter.X + ScopedLocal.X * ScopedCos - ScopedLocal.Y * ScopedSin,
                                            ScopedCenter.Y + ScopedLocal.X * ScopedSin + ScopedLocal.Y * ScopedCos};
                const float ScopedDistance = MField.Sample(ScopedSample);
                if (ScopedDistance < MField.outsideDistance)
                {
                    ScopedTouchedInside = true;
                }
                if (ScopedDistance < 0.0f)
                {
                    const float ScopedDepth = -ScopedDistance;
                    if (ScopedManifoldPointsCount < ScopedManifoldPoints.size())
                    {
                        ScopedManifoldPoints[ScopedManifoldPointsCount] = ScopedSample;
                        ScopedManifoldDepths[ScopedManifoldPointsCount] = ScopedDepth;
                        ++ScopedManifoldPointsCount;
                    }
                    else
                    {
                        // Keep the four deepest samples; a shallow one yields to a
                        // deeper sample so the manifold spans the real contact.
                        std::size_t ScopedShallowest = 0;
                        for (std::size_t ScopedPointIndex = 1; ScopedPointIndex < ScopedManifoldPoints.size();
                             ++ScopedPointIndex)
                        {
                            if (ScopedManifoldDepths[ScopedPointIndex] < ScopedManifoldDepths[ScopedShallowest])
                            {
                                ScopedShallowest = ScopedPointIndex;
                            }
                        }
                        if (ScopedDepth > ScopedManifoldDepths[ScopedShallowest])
                        {
                            ScopedManifoldPoints[ScopedShallowest] = ScopedSample;
                            ScopedManifoldDepths[ScopedShallowest] = ScopedDepth;
                        }
                    }
                }
                if (ScopedDistance < ScopedMinDistance)
                {
                    ScopedMinDistance = ScopedDistance;
                    ScopedDeepest = ScopedSample;
                }
            }
        }
        if (!ScopedTouchedInside || ScopedMinDistance >= 0.0f)
        {
            continue;
        }

        const float ScopedPenetration = -ScopedMinDistance;
        Vector2D ScopedNormal = MField.Gradient(ScopedDeepest);
        if (ScopedNormal.LengthSquared() < 1.0e-8f)
        {
            ScopedNormal = Vector2D{0.0f, -1.0f};
        }
        const float ScopedMass = std::max(b2Body_GetMass(ScopedBodyId), 0.001f);
        const Vector2D ScopedVelocity = ToPixelsVelocity(b2Body_GetLinearVelocity(ScopedBodyId), ScopedPixelsPerMeter);
        const float ScopedInward = ScopedVelocity.Dot(ScopedNormal);
        Vector2D ScopedReflected = ScopedVelocity;
        if (ScopedInward < 0.0f)
        {
            ScopedReflected =
                ScopedVelocity - ScopedNormal * (ScopedInward * (1.0f + std::clamp(MDesc.restitution, 0.0f, 1.0f)));
        }
        // Momentum that removes the penetration in one step. Dividing by the
        // actual fixed dt keeps the authored tuning stable at other rates.
        const float ScopedImpulseMagnitude =
            std::min(ScopedPenetration * ScopedMass / InFixedDt, std::max(MDesc.maxImpulse, 0.0f));
        // Set the reflected velocity first, then let the impulse add the push
        b2Body_SetLinearVelocity(ScopedBodyId, ToBoxVelocity(ScopedReflected, ScopedPixelsPerMeter));
        if (ScopedImpulseMagnitude > 0.0f)
        {
            b2Body_ApplyLinearImpulseToCenter(
                ScopedBodyId, ToBoxVelocity(ScopedNormal * ScopedImpulseMagnitude, ScopedPixelsPerMeter), true);
        }
        const float ScopedPushSpeed = std::min(ScopedPenetration * std::clamp(MDesc.pushOut, 0.0f, 1.0f) / InFixedDt,
                                               std::max(MDesc.maxPushSpeed, 0.0f));
        // Depenetration is a velocity, not a teleport: the body keeps its contact
        // continuity (and CCD) and the solver integrates the correction over the
        // next few steps instead of snapping.
        const Vector2D ScopedNewVelocity =
            ScopedReflected + ScopedNormal * (ScopedPushSpeed + ScopedImpulseMagnitude / ScopedMass);
        b2Body_SetLinearVelocity(ScopedBodyId, ToBoxVelocity(ScopedNewVelocity, ScopedPixelsPerMeter));

        // The pose and the previous pose are owned by the physics readback;
        // writing Transform2D here would be overwritten by the next step and
        // would break interpolation for the corrected body.
        ScopedBody.LinearVelocity = ScopedNewVelocity;
        (void)ScopedTransform;
        ++MLastCorrected;

        if (MContactCount < kMaxContacts)
        {
            SdfContact& ScopedContact = MContacts[MContactCount++];
            ScopedContact.EntityHandle = ScopedEntity;
            ScopedContact.Point = ScopedDeepest;
            ScopedContact.Normal = ScopedNormal;
            ScopedContact.Penetration = ScopedPenetration;
            ScopedContact.Impulse = ScopedImpulseMagnitude;
            ScopedContact.RelativeSpeed = std::max(-ScopedInward, 0.0f);
            ScopedContact.KineticEnergy = 0.5f * ScopedMass * ScopedContact.RelativeSpeed * ScopedContact.RelativeSpeed;
            ScopedContact.Mass = ScopedMass;
            ScopedContact.Sequence = ++MSequence;
        }
        else
        {
            ++MDroppedContacts;
        }

        if (MManifoldCount < kMaxManifolds && ScopedManifoldPointsCount > 0)
        {
            SdfContactManifold& ScopedManifold = MManifolds[MManifoldCount++];
            ScopedManifold.EntityHandle = ScopedEntity;
            ScopedManifold.Count = ScopedManifoldPointsCount;
            const float ScopedPointImpulse = ScopedImpulseMagnitude / static_cast<float>(ScopedManifoldPointsCount);
            for (std::uint32_t ScopedPointIndex = 0; ScopedPointIndex < ScopedManifoldPointsCount; ++ScopedPointIndex)
            {
                SdfContactPoint& ScopedPoint = ScopedManifold.Points[ScopedPointIndex];
                ScopedPoint.Point = ScopedManifoldPoints[ScopedPointIndex];
                // One gradient per body is enough for the manifold; re-sampling
                // every point cost four extra bilinear reads each.
                ScopedPoint.Normal = ScopedNormal;
                ScopedPoint.Penetration = ScopedManifoldDepths[ScopedPointIndex];
                ScopedPoint.Impulse = ScopedPointImpulse;
            }
        }
    }
    return MLastCorrected;
}

std::uint32_t SdfCollisionSystem::Update(Ecs::PhysicsSystemView& InView) noexcept
{
    if (InView.services.physics == nullptr || InView.dt <= 0.0f)
    {
        return 0;
    }
    return Step(InView.registry, *InView.services.physics, InView.dt);
}

bool SdfCollisionSystem::Register(Ecs::SystemGraph& InGraph)
{
    Ecs::SystemAccess ScopedAccess;
    ScopedAccess.ComponentReads = Ecs::MakeComponentAccessMask<RigidBody2D, Collider2D, Transform2D>();
    ScopedAccess.ComponentWrites = Ecs::MakeComponentAccessMask<RigidBody2D, Transform2D>();
    ScopedAccess.Writes = Ecs::kServicePhysics;
    return InGraph.Add(Ecs::Phase::FixedUpdate, "SdfCollisionSystem",
                       Ecs::BindPhysicsSystem<&SdfCollisionSystem::Tick>(), this, 0, 0, ScopedAccess);
}

void SdfCollisionSystem::Tick(Ecs::PhysicsSystemView& InView, void* InUser)
{
    static_cast<SdfCollisionSystem*>(InUser)->Update(InView);
}
}
