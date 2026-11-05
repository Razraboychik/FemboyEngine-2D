// FemboyEngine2D - transport paths, conveyor support and backend capabilities.
#include "Engine/ECS/Components.hpp"
#include "Engine/ECS/Schedule.hpp"
#include "Engine/Physics/Physics.hpp"
#include "Engine/Physics/TransportGroups.hpp"

#include <cmath>
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

Entity MakeEntity(Ecs::Registry& InRegistry, Vector2D InPosition)
{
    const Entity ScopedEntity = InRegistry.create();
    Transform2D ScopedTransform;
    ScopedTransform.Position = InPosition;
    InRegistry.emplace<Transform2D>(ScopedEntity, ScopedTransform);
    return ScopedEntity;
}

void AddStaticPlatform(Ecs::Registry& InRegistry, Entity InEntity, Vector2D InHalf)
{
    RigidBody2D ScopedBody;
    ScopedBody.Type = BodyType::Static;
    InRegistry.emplace<RigidBody2D>(InEntity, ScopedBody);
    Collider2D ScopedCollider;
    ScopedCollider.HalfExtents = InHalf;
    InRegistry.emplace<Collider2D>(InEntity, ScopedCollider);
}

void AddDynamicBody(Ecs::Registry& InRegistry, Entity InEntity, Vector2D InHalf)
{
    RigidBody2D ScopedBody;
    ScopedBody.Type = BodyType::Dynamic;
    InRegistry.emplace<RigidBody2D>(InEntity, ScopedBody);
    Collider2D ScopedCollider;
    ScopedCollider.HalfExtents = InHalf;
    InRegistry.emplace<Collider2D>(InEntity, ScopedCollider);
}
}

int main()
{
    {
        Ecs::Registry ScopedRegistry;
        const Entity ScopedRoot = MakeEntity(ScopedRegistry, Vector2D{0.0f, 0.0f});
        TransportPath ScopedPath;
        ScopedPath.Count = 2;
        ScopedPath.Points[0] = Vector2D{0.0f, 0.0f};
        ScopedPath.Points[1] = Vector2D{100.0f, 0.0f};
        ScopedRegistry.emplace<TransportPath>(ScopedRoot, ScopedPath);
        TransportMotion ScopedMotion;
        ScopedMotion.Speed = 100.0f;
        ScopedMotion.Flags = kTransportMotionPath;
        ScopedRegistry.emplace<TransportMotion>(ScopedRoot, ScopedMotion);

        PhysicsSystem ScopedPhysics;
        PhysicsDesc ScopedDesc;
        ScopedDesc.gravity = Vector2D{0.0f, 0.0f};
        Check(ScopedPhysics.Initialize(ScopedDesc), "physics initializes for path");
        TransportGroupSystem ScopedGroups;
        Check(ScopedGroups.CreateGroup(ScopedRegistry, ScopedRoot, 1), "path group creates");
        ScopedGroups.PrePhysics(ScopedRegistry, ScopedPhysics, 0.5f);
        ScopedGroups.PostPhysics(ScopedRegistry, ScopedPhysics, 0.5f);
        Check(ScopedRegistry.get<Transform2D>(ScopedRoot).Position.X > 40.0f, "path samples by distance");
        ScopedPhysics.Shutdown();
    }

    {
        Ecs::Registry ScopedRegistry;
        const Entity ScopedRoot = MakeEntity(ScopedRegistry, Vector2D{0.0f, 0.0f});
        const Entity ScopedRider = MakeEntity(ScopedRegistry, Vector2D{0.0f, 8.0f});
        AddStaticPlatform(ScopedRegistry, ScopedRoot, Vector2D{512.0f, 4.0f});
        AddDynamicBody(ScopedRegistry, ScopedRider, Vector2D{4.0f, 4.0f});
        TransportSurface ScopedSurface;
        ScopedSurface.Friction = 1.0f;
        ScopedSurface.TangentSpeed = 90.0f;
        ScopedRegistry.emplace<TransportSurface>(ScopedRoot, ScopedSurface);

        PhysicsSystem ScopedPhysics;
        PhysicsDesc ScopedDesc;
        ScopedDesc.gravity = Vector2D{0.0f, 0.0f};
        Check(ScopedPhysics.Initialize(ScopedDesc), "physics initializes for conveyor");
        ScopedPhysics.EnsureBodies(ScopedRegistry);

        TransportGroupSystem ScopedGroups;
        Check(ScopedGroups.CreateGroup(ScopedRegistry, ScopedRoot, 2), "conveyor group creates");
        Check(ScopedGroups.Attach(ScopedRegistry, ScopedRider, 2, kCarrierModeConveyor, 0), "rider attaches");
        for (std::uint32_t ScopedStep = 0; ScopedStep < 30; ++ScopedStep)
        {
            ScopedGroups.PrePhysics(ScopedRegistry, ScopedPhysics, 1.0f / 60.0f);
            ScopedPhysics.Step(ScopedRegistry, 1.0f / 60.0f);
            ScopedGroups.PostPhysics(ScopedRegistry, ScopedPhysics, 1.0f / 60.0f);
        }
        ScopedPhysics.Interpolate(ScopedRegistry, 1.0f);
        const CarrierMemberComponent* ScopedMember = ScopedRegistry.try_get<CarrierMemberComponent>(ScopedRider);
        Check(ScopedMember != nullptr, "rider remains attached");
        Check((ScopedMember->Flags & kCarrierFlagGrounded) != 0, "contact support marks the rider grounded");
        Check(ScopedRegistry.get<Transform2D>(ScopedRider).Position.X > 1.0f, "conveyor moves the rider");
        ScopedPhysics.Shutdown();
    }

    {
        PhysicsDesc ScopedDesc;
        ScopedDesc.backend = PhysicsBackendKind::Dod;
        PhysicsSystem ScopedDod;
        Check(ScopedDod.Initialize(ScopedDesc), "DOD transport world initializes");
        const TransportCapabilities ScopedCapabilities = TransportGroupSystem::CapabilitiesFor(ScopedDod);
        Check(!ScopedCapabilities.Joints, "DOD transport has no joints");
        Check(!ScopedCapabilities.Weld, "DOD transport has no weld");
        Check(!ScopedCapabilities.Ccd, "DOD transport has no CCD");
        ScopedDod.Shutdown();
    }

    std::printf("transport_physics: ok\n");
    return 0;
}
