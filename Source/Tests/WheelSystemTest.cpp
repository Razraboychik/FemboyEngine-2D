// FemboyEngine2D - wheel joint suspension, drive, brake and DOD fallback.
#include "Engine/ECS/Components.hpp"
#include "Engine/Physics/Physics.hpp"
#include "Engine/Physics/VehicleWheels.hpp"

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

Entity MakeBody(Ecs::Registry& InRegistry, Vector2D InPosition, Vector2D InHalf, BodyType InType)
{
    const Entity ScopedEntity = InRegistry.create();
    Transform2D ScopedTransform;
    ScopedTransform.Position = InPosition;
    InRegistry.emplace<Transform2D>(ScopedEntity, ScopedTransform);
    RigidBody2D ScopedBody;
    ScopedBody.Type = InType;
    InRegistry.emplace<RigidBody2D>(ScopedEntity, ScopedBody);
    Collider2D ScopedCollider;
    ScopedCollider.HalfExtents = InHalf;
    InRegistry.emplace<Collider2D>(ScopedEntity, ScopedCollider);
    return ScopedEntity;
}
}

int main()
{
    {
        Ecs::Registry ScopedRegistry;
        PhysicsSystem ScopedPhysics;
        PhysicsDesc ScopedDesc;
        ScopedDesc.gravity = Vector2D{0.0f, 0.0f};
        Check(ScopedPhysics.Initialize(ScopedDesc), "Box2D world initializes");
        const Entity ScopedChassis = MakeBody(ScopedRegistry, Vector2D{0.0f, 0.0f}, Vector2D{24.0f, 8.0f}, BodyType::Dynamic);
        const Entity ScopedLeft = MakeBody(ScopedRegistry, Vector2D{-16.0f, 0.0f}, Vector2D{5.0f, 5.0f}, BodyType::Dynamic);
        const Entity ScopedRight = MakeBody(ScopedRegistry, Vector2D{16.0f, 0.0f}, Vector2D{5.0f, 5.0f}, BodyType::Dynamic);
        Vehicle2D ScopedVehicle;
        ScopedVehicle.MaxMotorTorque = 600.0f;
        ScopedVehicle.MaxSpeed = 600.0f;
        ScopedRegistry.emplace<Vehicle2D>(ScopedChassis, ScopedVehicle);
        for (const Entity ScopedWheel : {ScopedLeft, ScopedRight})
        {
            Wheel2D ScopedWheelComponent;
            ScopedWheelComponent.Chassis = ScopedChassis;
            ScopedWheelComponent.LocalAnchor =
                ScopedWheel == ScopedLeft ? Vector2D{-16.0f, 0.0f} : Vector2D{16.0f, 0.0f};
            ScopedWheelComponent.Radius = 5.0f;
            ScopedRegistry.emplace<Wheel2D>(ScopedWheel, ScopedWheelComponent);
        }
        ScopedPhysics.EnsureBodies(ScopedRegistry);

        WheelSystem ScopedWheels;
        Check(ScopedWheels.Initialize(ScopedPhysics), "wheel system initializes on Box2D");
        Check(ScopedWheels.Available(), "wheel system reports availability");
        ScopedWheels.Update(ScopedRegistry, ScopedPhysics, 1.0f / 60.0f);
        Check(ScopedWheels.WheelCount() == 2, "two wheel joints are created");
        Check(ScopedRegistry.get<Wheel2D>(ScopedLeft).Joint != 0, "left wheel joint is stored");
        Check(ScopedRegistry.get<Wheel2D>(ScopedRight).Joint != 0, "right wheel joint is stored");

        ScopedRegistry.get<Vehicle2D>(ScopedChassis).Throttle = 1.0f;
        for (std::uint32_t ScopedStep = 0; ScopedStep < 20; ++ScopedStep)
        {
            ScopedWheels.Update(ScopedRegistry, ScopedPhysics, 1.0f / 60.0f);
            ScopedPhysics.Step(ScopedRegistry, 1.0f / 60.0f);
        }
        const RigidBody2D& ScopedWheelBody = ScopedRegistry.get<RigidBody2D>(ScopedLeft);
        Check(std::fabs(ScopedWheelBody.AngularVelocity) > 0.1f, "drive motor spins the wheel");

        ScopedRegistry.get<Vehicle2D>(ScopedChassis).Throttle = 0.0f;
        ScopedRegistry.get<Vehicle2D>(ScopedChassis).Brake = 1.0f;
        for (std::uint32_t ScopedStep = 0; ScopedStep < 60; ++ScopedStep)
        {
            ScopedWheels.Update(ScopedRegistry, ScopedPhysics, 1.0f / 60.0f);
            ScopedPhysics.Step(ScopedRegistry, 1.0f / 60.0f);
        }
        const RigidBody2D& ScopedBrakeBody = ScopedRegistry.get<RigidBody2D>(ScopedLeft);
        Check(std::fabs(ScopedBrakeBody.AngularVelocity) < 1.0f, "brake motor stops the wheel");
        ScopedWheels.Shutdown();
        ScopedPhysics.Shutdown();
    }

    {
        PhysicsDesc ScopedDesc;
        ScopedDesc.backend = PhysicsBackendKind::Dod;
        PhysicsSystem ScopedPhysics;
        Check(ScopedPhysics.Initialize(ScopedDesc), "DOD world initializes");
        WheelSystem ScopedWheels;
        Check(!ScopedWheels.Initialize(ScopedPhysics), "DOD has no wheel-joint support");
        Check(!ScopedWheels.Available(), "DOD wheel support is unavailable");
        ScopedPhysics.Shutdown();
    }

    std::printf("wheel_system: ok\n");
    return 0;
}
