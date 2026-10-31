#include "Engine/Physics/SnapshotRemap.hpp"

namespace Fby
{

bool RebuildPhysicsBodies(Ecs::Registry& InRegistry, PhysicsSystem& InPhysics, PhysicsBodyRemapTable& InRemap) noexcept
{
    if (!InPhysics.IsValid())
    {
        return false;
    }
    auto ScopedView = InRegistry.view<RigidBody2D>();
    std::uint32_t ScopedCount = 0;
    for (auto ScopedEntry : ScopedView.each())
    {
        (void)ScopedEntry;
        ++ScopedCount;
    }
    if (ScopedCount > kMaxPhysicsRemapEntries)
    {
        return false;
    }
    InRemap.Clear();

    // Capture the old ids for diagnostics and for a sidecar that still refers to
    for (auto [ScopedEntity, ScopedBody] : ScopedView.each())
    {
        const std::uint64_t ScopedOldBody = ScopedBody.Body;
        ScopedBody.Body = 0;
        if (auto* ScopedCollider = InRegistry.try_get<Collider2D>(ScopedEntity); ScopedCollider != nullptr)
        {
            ScopedCollider->Shape = 0;
        }
        if (!InRemap.Add(ScopedEntity, ScopedOldBody, 0))
        {
            return false;
        }
    }

    InPhysics.EnsureBodies(InRegistry);

    for (const PhysicsBodyRemapEntry& ScopedEntry : InRemap.Entries())
    {
        const auto* ScopedBody = InRegistry.try_get<RigidBody2D>(ScopedEntry.entity);
        if (ScopedBody == nullptr || ScopedBody->Body == 0)
        {
            continue;
        }
        (void)InRemap.SetLive(ScopedEntry.entity, ScopedBody->Body);
    }
    return true;
}
}
