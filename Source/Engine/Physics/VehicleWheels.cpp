// FemboyEngine2D - wheel suspension, drive and brake primitives.
#include "Engine/Physics/VehicleWheels.hpp"

#include "Engine/ECS/Components.hpp"

#include <box2d/box2d.h>

#include <algorithm>
#include <cmath>

namespace Fby
{
namespace
{
[[nodiscard]] b2Vec2 ToBox(Vector2D InPixels, float InScale) noexcept
{
    return b2Vec2{InPixels.X / std::max(InScale, 0.0001f), InPixels.Y / std::max(InScale, 0.0001f)};
}
}

bool WheelSystem::Initialize(PhysicsSystem& InPhysics) noexcept
{
    MPhysics = &InPhysics;
    MAvailable = InPhysics.WorldId() != 0;
    MWheelCount = 0;
    return MAvailable;
}

void WheelSystem::Shutdown() noexcept
{
    MPhysics = nullptr;
    MAvailable = false;
    MWheelCount = 0;
}

void WheelSystem::Update(Ecs::Registry& InRegistry, PhysicsSystem& InPhysics, float InFixedDt) noexcept
{
    (void)InFixedDt;
    if (!MAvailable || MPhysics != &InPhysics || InPhysics.WorldId() == 0)
    {
        return;
    }
    MWheelCount = 0;
    auto ScopedView = InRegistry.view<Wheel2D>();
    for (const Entity ScopedWheel : ScopedView)
    {
        if (EnsureJoint(InRegistry, InPhysics, ScopedWheel))
        {
            ApplyDrive(InRegistry, InPhysics, ScopedWheel);
            ++MWheelCount;
        }
    }
}

bool WheelSystem::EnsureJoint(Ecs::Registry& InRegistry, PhysicsSystem& InPhysics, Entity InWheel) noexcept
{
    Wheel2D* ScopedWheel = InRegistry.try_get<Wheel2D>(InWheel);
    if (ScopedWheel == nullptr || ScopedWheel->Chassis == entt::null || !InRegistry.valid(ScopedWheel->Chassis))
    {
        return false;
    }
    if (ScopedWheel->Joint != 0 && b2Joint_IsValid(b2LoadJointId(ScopedWheel->Joint)))
    {
        return true;
    }
    ScopedWheel->Joint = 0;
    RigidBody2D* ScopedChassis = InRegistry.try_get<RigidBody2D>(ScopedWheel->Chassis);
    RigidBody2D* ScopedBody = InRegistry.try_get<RigidBody2D>(InWheel);
    if (ScopedChassis == nullptr || ScopedBody == nullptr || ScopedChassis->Body == 0 || ScopedBody->Body == 0)
    {
        return false;
    }
    const b2BodyId ScopedChassisId = b2LoadBodyId(ScopedChassis->Body);
    const b2BodyId ScopedWheelId = b2LoadBodyId(ScopedBody->Body);
    if (!b2Body_IsValid(ScopedChassisId) || !b2Body_IsValid(ScopedWheelId))
    {
        return false;
    }

    b2WheelJointDef ScopedDefinition = b2DefaultWheelJointDef();
    ScopedDefinition.base.bodyIdA = ScopedChassisId;
    ScopedDefinition.base.bodyIdB = ScopedWheelId;
    ScopedDefinition.base.collideConnected = false;
    ScopedDefinition.base.localFrameA.p = ToBox(ScopedWheel->LocalAnchor, InPhysics.PixelsPerMeter());
    ScopedDefinition.base.localFrameB.p = b2Vec2{0.0f, 0.0f};
    ScopedDefinition.base.localFrameB.q = b2MakeRot(0.0f);
    Vector2D ScopedAxis = ScopedWheel->SuspensionAxis;
    const float ScopedAxisLength = std::sqrt(ScopedAxis.Dot(ScopedAxis));
    if (ScopedAxisLength <= 0.0001f)
    {
        ScopedAxis = Vector2D{0.0f, 1.0f};
    }
    else
    {
        ScopedAxis = ScopedAxis / ScopedAxisLength;
    }
    ScopedDefinition.base.localFrameA.q = b2MakeRot(std::atan2(ScopedAxis.Y, ScopedAxis.X));
    ScopedDefinition.enableSpring = true;
    ScopedDefinition.hertz = std::max(ScopedWheel->SuspensionHertz, 0.0f);
    ScopedDefinition.dampingRatio = std::clamp(ScopedWheel->SuspensionDamping, 0.0f, 1.0f);
    ScopedDefinition.enableMotor = true;
    ScopedDefinition.maxMotorTorque = 0.0f;
    ScopedDefinition.motorSpeed = 0.0f;
    const b2JointId ScopedJoint = b2CreateWheelJoint(b2LoadWorldId(InPhysics.WorldId()), &ScopedDefinition);
    if (!b2Joint_IsValid(ScopedJoint))
    {
        return false;
    }
    ScopedWheel->Joint = b2StoreJointId(ScopedJoint);
    return true;
}

void WheelSystem::ApplyDrive(Ecs::Registry& InRegistry, PhysicsSystem& InPhysics, Entity InWheel) noexcept
{
    Wheel2D* ScopedWheel = InRegistry.try_get<Wheel2D>(InWheel);
    if (ScopedWheel == nullptr || ScopedWheel->Joint == 0)
    {
        return;
    }
    const b2JointId ScopedJoint = b2LoadJointId(ScopedWheel->Joint);
    if (!b2Joint_IsValid(ScopedJoint))
    {
        ScopedWheel->Joint = 0;
        return;
    }
    Vehicle2D* ScopedVehicle = InRegistry.try_get<Vehicle2D>(ScopedWheel->Chassis);
    if (ScopedVehicle == nullptr)
    {
        b2WheelJoint_EnableMotor(ScopedJoint, false);
        return;
    }

    b2WheelJoint_SetSpringHertz(ScopedJoint, std::max(ScopedWheel->SuspensionHertz, 0.0f));
    b2WheelJoint_SetSpringDampingRatio(ScopedJoint, std::clamp(ScopedWheel->SuspensionDamping, 0.0f, 1.0f));
    const float ScopedThrottle = std::clamp(ScopedVehicle->Throttle, -1.0f, 1.0f);
    const float ScopedBrake = std::clamp(ScopedVehicle->Brake, 0.0f, 1.0f);
    const bool ScopedDriven = (ScopedWheel->Flags & kWheelFlagDrive) != 0;
    const bool ScopedHandbrake = (ScopedVehicle->Flags & VehicleFlagHandbrake) != 0;
    const float ScopedMaxSpeed = std::max(ScopedVehicle->MaxSpeed, 0.0f);
    const float ScopedRadius = std::max(ScopedWheel->Radius, 0.01f);
    const float ScopedMaxAngular = ScopedMaxSpeed / ScopedRadius;
    const float ScopedDriveTorque =
        std::abs(ScopedThrottle) * std::max(ScopedVehicle->MaxMotorTorque, 0.0f) *
        std::max(ScopedWheel->DriveWeight, 0.0f);
    const float ScopedBrakeTorque = ScopedBrake * std::max(ScopedVehicle->MaxMotorTorque, 0.0f) *
                                    std::max(ScopedWheel->BrakeWeight, 0.0f);

    b2WheelJoint_EnableMotor(ScopedJoint, true);
    if (ScopedBrake > 0.0f || ScopedHandbrake)
    {
        const float ScopedTorque = ScopedHandbrake
                                       ? std::max(ScopedVehicle->MaxMotorTorque, 0.0f) *
                                             std::max(ScopedWheel->BrakeWeight, 0.0f)
                                       : ScopedBrakeTorque;
        b2WheelJoint_EnableMotor(ScopedJoint, false);
        b2WheelJoint_SetMotorSpeed(ScopedJoint, 0.0f);
        b2WheelJoint_SetMaxMotorTorque(ScopedJoint, ScopedTorque);
        if (RigidBody2D* ScopedWheelBody = InRegistry.try_get<RigidBody2D>(InWheel);
            ScopedWheelBody != nullptr && ScopedWheelBody->Body != 0)
        {
            const b2BodyId ScopedWheelBodyId = b2LoadBodyId(ScopedWheelBody->Body);
            const float ScopedOmega = b2Body_GetAngularVelocity(ScopedWheelBodyId);
            const float ScopedDamping = std::clamp(ScopedBrake * 0.2f + (ScopedHandbrake ? 0.4f : 0.0f), 0.0f, 0.95f);
            b2Body_SetAngularVelocity(ScopedWheelBodyId, ScopedOmega * (1.0f - ScopedDamping));
        }
    }
    else if (ScopedDriven)
    {
        b2WheelJoint_SetMotorSpeed(ScopedJoint, ScopedThrottle * ScopedMaxAngular);
        b2WheelJoint_SetMaxMotorTorque(ScopedJoint, ScopedDriveTorque);
    }
    else
    {
        b2WheelJoint_SetMotorSpeed(ScopedJoint, 0.0f);
        b2WheelJoint_SetMaxMotorTorque(ScopedJoint, 0.0f);
    }

    if ((ScopedVehicle->Flags & VehicleFlagArcadeSteering) != 0 && (ScopedWheel->Flags & kWheelFlagSteer) != 0)
    {
        RigidBody2D* ScopedChassis = InRegistry.try_get<RigidBody2D>(ScopedWheel->Chassis);
        if (ScopedChassis != nullptr && ScopedChassis->Body != 0)
        {
            const b2BodyId ScopedChassisId = b2LoadBodyId(ScopedChassis->Body);
            const b2Vec2 ScopedVelocity = b2Body_GetLinearVelocity(ScopedChassisId);
            const float ScopedSpeedFactor =
                std::clamp(std::sqrt(ScopedVelocity.x * ScopedVelocity.x + ScopedVelocity.y * ScopedVelocity.y) /
                               std::max(ScopedMaxSpeed, 1.0f),
                           0.0f, 1.0f);
            b2Body_ApplyTorque(ScopedChassisId,
                               std::clamp(ScopedVehicle->Steering, -1.0f, 1.0f) * ScopedVehicle->SteeringTorque *
                                   ScopedSpeedFactor,
                               true);
        }
    }
}

}
