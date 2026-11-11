// FemboyEngine2D - backend selection, segment queries and projectile modes.
#include "Engine/ECS/Schedule.hpp"
#include "Engine/ECS/Presentation.hpp"
#include "Engine/Physics/Physics.hpp"
#include "Engine/Physics/ProjectileSystem.hpp"

#include <cstdio>
#include <cstdlib>

using namespace Fby;

namespace
{
void Check(bool InCondition, const char* InMessage)
{
    if (!InCondition)
    {
        std::fprintf(stderr, "FAIL: %s\n", InMessage);
        std::exit(1);
    }
}

Entity MakeBody(Ecs::Registry& InRegistry, Vector2D InPosition, Vector2D InHalfExtents, BodyType InType)
{
    const Entity ScopedEntity = InRegistry.create();
    Transform2D ScopedTransform;
    ScopedTransform.Position = InPosition;
    InRegistry.emplace<Transform2D>(ScopedEntity, ScopedTransform);
    RigidBody2D ScopedBody;
    ScopedBody.Type = InType;
    InRegistry.emplace<RigidBody2D>(ScopedEntity, ScopedBody);
    Collider2D ScopedCollider;
    ScopedCollider.HalfExtents = InHalfExtents;
    InRegistry.emplace<Collider2D>(ScopedEntity, ScopedCollider);
    return ScopedEntity;
}
}

int main()
{
    Ecs::Registry ScopedRegistry;
    PhysicsDesc ScopedDesc;
    ScopedDesc.gravity = Vector2D{0.0f, 0.0f};
    ScopedDesc.backend = PhysicsBackendKind::Auto;
    ScopedDesc.bodyBudget = 10000;

    PhysicsSystem ScopedPhysics;
    Check(ScopedPhysics.Initialize(ScopedDesc), "initialize auto backend");
    Check(ScopedPhysics.Backend() == PhysicsBackendKind::Dod, "auto selects DOD for a large body budget");
    Check(ScopedPhysics.Capabilities().Deterministic, "DOD is deterministic");
    Check(!ScopedPhysics.Capabilities().Joints, "DOD has no joints");

    const Entity ScopedMover = MakeBody(ScopedRegistry, Vector2D{0.0f, 0.0f}, Vector2D{4.0f, 4.0f}, BodyType::Dynamic);
    RigidBody2D* ScopedMoverBody = ScopedRegistry.try_get<RigidBody2D>(ScopedMover);
    ScopedMoverBody->LinearVelocity = Vector2D{100.0f, 0.0f};
    ScopedPhysics.Step(ScopedRegistry, 0.1f);
    Transform2D* ScopedMoverTransform = ScopedRegistry.try_get<Transform2D>(ScopedMover);
    Check(ScopedMoverTransform->Position.X > 9.0f, "DOD integrates velocity");
    ScopedMoverTransform->Position = Vector2D{0.0f, 100.0f};
    ScopedMoverBody->Position = ScopedMoverTransform->Position;

    // A destroyed body must not leave a ghost in the broadphase. The freed slot
    // used to keep Body{} defaults (enabled dynamic body at the origin) and
    // collided with real bodies until it was reused.
    const Entity ScopedGhost = MakeBody(ScopedRegistry, Vector2D{0.0f, 0.0f}, Vector2D{4.0f, 4.0f}, BodyType::Dynamic);
    ScopedPhysics.EnsureBodies(ScopedRegistry);
    Check(ScopedPhysics.LiveBodyCount() == 2, "two live DOD bodies before the destroy");
    ScopedRegistry.destroy(ScopedGhost);
    ScopedPhysics.Step(ScopedRegistry, 0.1f);
    Check(ScopedPhysics.LiveBodyCount() == 1, "a destroyed body releases its live slot");
    const Entity ScopedProbe = MakeBody(ScopedRegistry, Vector2D{0.0f, 0.0f}, Vector2D{1.0f, 1.0f}, BodyType::Dynamic);
    ScopedPhysics.Step(ScopedRegistry, 0.1f);
    const Transform2D* ScopedProbeTransform = ScopedRegistry.try_get<Transform2D>(ScopedProbe);
    Check(ScopedProbeTransform->Position.X == 0.0f && ScopedProbeTransform->Position.Y == 0.0f,
          "a freed slot does not collide with a body at the origin");
    Check(ScopedPhysics.LiveBodyCount() == 2, "the reused slot is live again");

    // The ray and projectile checks need a clear x axis: both helper bodies
    // still overlap it, and DOD keeps its own position columns.
    ScopedRegistry.destroy(ScopedMover);
    ScopedRegistry.destroy(ScopedProbe);

    // TimeScaleComponent freezes a DOD body: it does not integrate and drops the
    // velocity the game assigned before the step.
    const Entity ScopedFrozen =
        MakeBody(ScopedRegistry, Vector2D{-200.0f, 0.0f}, Vector2D{1.0f, 1.0f}, BodyType::Dynamic);
    ScopedRegistry.try_get<RigidBody2D>(ScopedFrozen)->LinearVelocity = Vector2D{100.0f, 0.0f};
    ScopedRegistry.emplace<TimeScaleComponent>(ScopedFrozen, TimeScaleComponent{0.0f});
    ScopedPhysics.Step(ScopedRegistry, 0.1f);
    const Transform2D* ScopedFrozenTransform = ScopedRegistry.try_get<Transform2D>(ScopedFrozen);
    Check(ScopedFrozenTransform != nullptr && ScopedFrozenTransform->Position.X == -200.0f,
          "a frozen DOD body does not integrate");

    const Entity ScopedWall = MakeBody(ScopedRegistry, Vector2D{50.0f, 0.0f}, Vector2D{5.0f, 20.0f}, BodyType::Static);
    PhysicsRayHit ScopedHit;
    Check(ScopedPhysics.RayCast(ScopedRegistry, Vector2D{0.0f, 0.0f}, Vector2D{100.0f, 0.0f}, Fby::kNullEntity,
                                0xFFFFFFFFu, ScopedHit),
          "segment query finds a wall");
    Check(ScopedHit.EntityHandle == ScopedWall, "segment query returns the wall");
    Check(ScopedHit.Point.X < 46.0f, "segment query stops at the near face");

    const Entity ScopedProjectile =
        MakeBody(ScopedRegistry, Vector2D{0.0f, 0.0f}, Vector2D{1.0f, 1.0f}, BodyType::Kinematic);
    Projectile2D ScopedProjectileData;
    ScopedProjectileData.Mode = kProjectileModeAuto;
    ScopedProjectileData.Speed = 10000.0f;
    ScopedProjectileData.MinWallThickness = 8.0f;
    ScopedProjectileData.Direction = Vector2D{1.0f, 0.0f};
    ScopedRegistry.emplace<Projectile2D>(ScopedProjectile, ScopedProjectileData);
    ProjectileSystem ScopedProjectiles;
    ScopedProjectiles.Step(ScopedRegistry, ScopedPhysics, 1.0f / 60.0f);
    Check(ScopedProjectiles.Hits().size() == 1, "auto projectile promotes to hitscan");
    Check(ScopedProjectiles.Hits()[0].target == ScopedWall, "projectile reports the wall hit");
    Transform2D* ScopedProjectileTransform = ScopedRegistry.try_get<Transform2D>(ScopedProjectile);
    Check(ScopedProjectileTransform->Position.X > 40.0f && ScopedProjectileTransform->Position.X < 46.0f,
          "projectile stops at the wall");

    PhysicsDesc ScopedJointDesc;
    ScopedJointDesc.requireJoints = true;
    PhysicsSystem ScopedJointPhysics;
    Check(ScopedJointPhysics.Initialize(ScopedJointDesc), "initialize joint backend");
    Check(ScopedJointPhysics.Backend() == PhysicsBackendKind::Box2D, "joints force Box2D");
    Check(ScopedJointPhysics.Capabilities().Joints, "Box2D exposes joints");
    ScopedJointPhysics.Shutdown();
    ScopedPhysics.Shutdown();

    std::printf("physics_backend: ok\n");
    return 0;
}
