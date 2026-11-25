// FemboyEngine2D - per-view physics policy.
#pragma once

#include "Engine/Core/ViewProfile.hpp"
#include "Engine/Physics/Physics.hpp"

namespace Fby
{
struct PhysicsProfileDesc
{
    PhysicsBackendKind Backend = PhysicsBackendKind::Auto;
    Vector2D Gravity{0.0f, 9.8f};
    bool Rotation = true;
    bool GridMovement = false;
};

[[nodiscard]] PhysicsProfileDesc PhysicsProfileFor(ViewProfile InProfile) noexcept;
[[nodiscard]] PhysicsDesc ApplyPhysicsProfile(PhysicsDesc InDesc, ViewProfile InProfile) noexcept;
}
