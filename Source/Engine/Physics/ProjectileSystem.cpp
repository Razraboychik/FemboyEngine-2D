// FemboyEngine2D - projectile movement and tunneling guard.
#include "Engine/Physics/ProjectileSystem.hpp"

#include <algorithm>
#include <cmath>

namespace Fby
{
std::uint32_t ProjectileSystem::ResolveMode(const Projectile2D& InProjectile, float InDisplacement) noexcept
{
    if (InProjectile.Mode != kProjectileModeAuto)
    {
        return InProjectile.Mode;
    }
    const float ScopedThreshold = std::max(InProjectile.MinWallThickness * 0.5f, 0.001f);
    if (InDisplacement <= ScopedThreshold)
    {
        return kProjectileModeDynamic;
    }
    return InProjectile.Radius <= 1.0f ? kProjectileModeHitscan : kProjectileModeSwept;
}

void ProjectileSystem::Step(Ecs::Registry& InRegistry, PhysicsSystem& InPhysics, float InFixedDt)
{
    MHits.clear();
    if (!(InFixedDt > 0.0f))
    {
        return;
    }

    auto ScopedView = InRegistry.view<Projectile2D, Transform2D>();
    for (auto [ScopedEntity, ScopedProjectile, ScopedTransform] : ScopedView.each())
    {
        RigidBody2D* ScopedBody = InRegistry.try_get<RigidBody2D>(ScopedEntity);
        if (ScopedBody == nullptr)
        {
            continue;
        }

        const Vector2D ScopedDirection = ScopedProjectile.Direction;
        const float ScopedDirectionLength = std::sqrt(ScopedDirection.Dot(ScopedDirection));
        if (!(ScopedDirectionLength > 0.0001f))
        {
            continue;
        }
        const Vector2D ScopedUnit = ScopedDirection / ScopedDirectionLength;
        const float ScopedSpeed = ScopedProjectile.Speed > 0.0f
                                      ? ScopedProjectile.Speed
                                      : std::sqrt(ScopedBody->LinearVelocity.Dot(ScopedBody->LinearVelocity));
        if (!(ScopedSpeed > 0.0f))
        {
            continue;
        }

        const float ScopedDisplacement = ScopedSpeed * InFixedDt;
        const std::uint32_t ScopedMode = ResolveMode(ScopedProjectile, ScopedDisplacement);
        if (ScopedMode == kProjectileModeDynamic)
        {
            // Route velocity through the physics bridge: the DOD backend keeps
            // its own velocity columns and ignores component writes.
            InPhysics.SetLinearVelocity(InRegistry, ScopedEntity, ScopedUnit * ScopedSpeed, true);
            InPhysics.SetBullet(InRegistry, ScopedEntity, true);
            continue;
        }

        const Vector2D ScopedFrom = ScopedTransform.Position;
        const Vector2D ScopedTo = ScopedFrom + ScopedUnit * ScopedDisplacement;
        PhysicsRayHit ScopedHit;
        if (InPhysics.RayCast(InRegistry, ScopedFrom, ScopedTo, ScopedEntity, ScopedProjectile.CollisionMask, ScopedHit))
        {
            ScopedTransform.Position = ScopedHit.Point;
            ScopedBody->Position = ScopedHit.Point;
            // The teleport flag is the bridge's transform channel; writing the
            // body directly would be ignored by the DOD backend.
            ScopedBody->Flags |= RigidBodyTeleport;
            ScopedBody->LinearVelocity = Vector2D{};
            InPhysics.SetLinearVelocity(InRegistry, ScopedEntity, Vector2D{}, false);
            RecordHit(ScopedEntity, ScopedHit.Entity, ScopedHit.Point, ScopedHit.Normal, ScopedSpeed);
            continue;
        }

        ScopedTransform.Position = ScopedTo;
        ScopedBody->Position = ScopedTo;
        ScopedBody->Flags |= RigidBodyTeleport;
        ScopedBody->LinearVelocity = ScopedUnit * ScopedSpeed;
        InPhysics.SetLinearVelocity(InRegistry, ScopedEntity, ScopedBody->LinearVelocity, true);
    }
}

void ProjectileSystem::RecordHit(Entity InProjectile, Entity InTarget, Vector2D InPoint, Vector2D InNormal, float InSpeed)
{
    if (MHits.size() >= 256)
    {
        return;
    }
    ProjectileHitEvent ScopedHit;
    ScopedHit.projectile = InProjectile;
    ScopedHit.target = InTarget;
    ScopedHit.point = InPoint;
    ScopedHit.normal = InNormal;
    ScopedHit.speed = InSpeed;
    MHits.push_back(ScopedHit);
}

}
