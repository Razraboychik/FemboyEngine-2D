// FemboyEngine2D - wheel suspension, drive and brake primitives.
#pragma once

#include "Engine/ECS/Schedule.hpp"
#include "Engine/Physics/Physics.hpp"

#include <cstdint>

namespace Fby
{

// Creates and drives Box2D wheel joints for entities carrying Wheel2D. The
// chassis owns Vehicle2D; each wheel body is a normal dynamic body with a
// collider. The system intentionally does not model RPM, gears, clutch or tire
// slip: a game maps throttle/brake to the exposed torque and per-wheel weights.
class WheelSystem
{
public:
    WheelSystem() = default;
    ~WheelSystem() = default;

    WheelSystem(const WheelSystem&) = delete;
    WheelSystem& operator=(const WheelSystem&) = delete;

    // Returns false on the DOD backend, which has no wheel joints.
    [[nodiscard]] bool Initialize(PhysicsSystem& InPhysics) noexcept;
    void Shutdown() noexcept;

    // Call before PhysicsSystem::Step. Creates missing joints lazily.
    void Update(Ecs::Registry& InRegistry, PhysicsSystem& InPhysics, float InFixedDt) noexcept;

    [[nodiscard]] std::uint32_t WheelCount() const noexcept
    {
        return MWheelCount;
    }
    [[nodiscard]] bool Available() const noexcept
    {
        return MAvailable;
    }

private:
    [[nodiscard]] bool EnsureJoint(Ecs::Registry& InRegistry, PhysicsSystem& InPhysics, Entity InWheel) noexcept;
    void ApplyDrive(Ecs::Registry& InRegistry, PhysicsSystem& InPhysics, Entity InWheel) noexcept;

    PhysicsSystem* MPhysics = nullptr;
    std::uint32_t MWheelCount = 0;
    bool MAvailable = false;
};

}
