#include "Engine/Physics/PhysicsProfile.hpp"

namespace Fby
{
PhysicsProfileDesc PhysicsProfileFor(ViewProfile InProfile) noexcept
{
    PhysicsProfileDesc ScopedProfile;
    switch (InProfile)
    {
        case ViewProfile::SideView:
            ScopedProfile.Backend = PhysicsBackendKind::Box2D;
            ScopedProfile.Gravity = {0.0f, 9.8f};
            ScopedProfile.Rotation = true;
            break;
        case ViewProfile::TopDown:
            ScopedProfile.Backend = PhysicsBackendKind::Auto;
            ScopedProfile.Gravity = {};
            ScopedProfile.Rotation = true;
            break;
        case ViewProfile::Isometric:
            ScopedProfile.Backend = PhysicsBackendKind::Auto;
            ScopedProfile.Gravity = {};
            ScopedProfile.Rotation = false;
            break;
        case ViewProfile::Hex:
            ScopedProfile.Backend = PhysicsBackendKind::Dod;
            ScopedProfile.Gravity = {};
            ScopedProfile.Rotation = false;
            ScopedProfile.GridMovement = true;
            break;
        default:
            break;
    }
    return ScopedProfile;
}

PhysicsDesc ApplyPhysicsProfile(PhysicsDesc InDesc, ViewProfile InProfile) noexcept
{
    const PhysicsProfileDesc ScopedProfile = PhysicsProfileFor(InProfile);
    InDesc.backend = ScopedProfile.Backend;
    InDesc.gravity = ScopedProfile.Gravity;
    return InDesc;
}
}
