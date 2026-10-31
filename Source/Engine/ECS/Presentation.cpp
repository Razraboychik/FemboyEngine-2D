// FemboyEngine2D - collision policy, time scale and sleeping-static helpers.
#include "Engine/ECS/Presentation.hpp"

#include "Engine/ECS/LightSource.hpp"
#include "Engine/ECS/Lighting.hpp"

#include <box2d/box2d.h>

namespace Fby
{
namespace
{
// Clears the static-light classification of a woken light. Both the unified
// component and the legacy radial component are handled so a scene migrated in
// part still wakes consistently.
void SwitchLightToDynamic(Ecs::Registry& InRegistry, Entity InEntity, bool InFromContact) noexcept
{
    auto* ScopedUnified = InRegistry.try_get<LightSourceComponent>(InEntity);
    if (ScopedUnified != nullptr)
    {
        (void)WakeLightSource(*ScopedUnified, InFromContact);
    }
    auto* ScopedLegacy = InRegistry.try_get<Light2DComponent>(InEntity);
    if (ScopedLegacy != nullptr)
    {
        // The legacy component has no static cache, so waking only has to keep
        // the enabled flag intact; nothing else changes.
        ScopedLegacy->Flags |= kLight2DFlagEnabled;
    }
}
}

void ResetSleepingStatic(Ecs::Registry& InRegistry, Entity InEntity) noexcept
{
    auto* ScopedStatic = InRegistry.try_get<SleepingStaticComponent>(InEntity);
    if (ScopedStatic == nullptr)
    {
        return;
    }
    ScopedStatic->Flags |= kSleepingStaticFlagActive | kSleepingStaticFlagSleeping;
    ScopedStatic->LastWakeReason = kStaticWakeNone;
}

void WakeSleepingStatic(Ecs::Registry& InRegistry, Entity InEntity, std::uint32_t InReason) noexcept
{
    if (!InRegistry.valid(InEntity))
    {
        return;
    }
    auto* ScopedStatic = InRegistry.try_get<SleepingStaticComponent>(InEntity);
    if (ScopedStatic == nullptr || !ScopedStatic->IsSleeping())
    {
        return;
    }
    ScopedStatic->Flags &= ~kSleepingStaticFlagSleeping;
    ScopedStatic->LastWakeReason = InReason;
    ++ScopedStatic->WakeCount;

    auto* ScopedBody = InRegistry.try_get<RigidBody2D>(InEntity);
    if (ScopedBody != nullptr)
    {
        ScopedBody->Type = BodyType::Dynamic;
        ScopedBody->Flags |= RigidBodyAwake | RigidBodyEnabled;
        if (ScopedBody->Body != 0)
        {
            const b2BodyId ScopedBodyId = b2LoadBodyId(ScopedBody->Body);
            if (b2Body_IsValid(ScopedBodyId))
            {
                b2Body_SetType(ScopedBodyId, b2_dynamicBody);
                b2Body_SetAwake(ScopedBodyId, true);
            }
        }
    }
    SwitchLightToDynamic(InRegistry, InEntity, InReason == kStaticWakeContact);
}

std::uint32_t WakeSleepingStaticFromContacts(Ecs::Registry& InRegistry,
                                             std::span<const PhysicsContactEvent> InContacts) noexcept
{
    std::uint32_t ScopedWoken = 0;
    for (const PhysicsContactEvent& ScopedEvent : InContacts)
    {
        if (ScopedEvent.type != PhysicsEventType::BeginTouch && ScopedEvent.type != PhysicsEventType::Hit)
        {
            continue;
        }
        for (const Entity ScopedEntity : {ScopedEvent.a, ScopedEvent.b})
        {
            if (ScopedEntity == entt::null || !InRegistry.valid(ScopedEntity))
            {
                continue;
            }
            const auto* ScopedStatic = InRegistry.try_get<SleepingStaticComponent>(ScopedEntity);
            if (ScopedStatic == nullptr || !ScopedStatic->IsSleeping() || !ScopedStatic->WakesOnContact())
            {
                continue;
            }
            WakeSleepingStatic(InRegistry, ScopedEntity, kStaticWakeContact);
            ++ScopedWoken;
        }
    }
    return ScopedWoken;
}

void ApplyCollisionPolicyToCollider(Ecs::Registry& InRegistry, Entity InEntity,
                                    CollisionPolicyComponent& InPolicy) noexcept
{
    const CollisionMode ScopedMode = InPolicy.GetMode();
    auto* ScopedCollider = InRegistry.try_get<Collider2D>(InEntity);
    if (ScopedCollider != nullptr)
    {
        ScopedCollider->Sensor = CollisionModeIsSensor(ScopedMode);
        ScopedCollider->CategoryBits = InPolicy.Category;
        ScopedCollider->MaskBits = ScopedMode == CollisionMode::NoCollision ? 0u : InPolicy.Mask;
        ScopedCollider->ContactEvents = CollisionModeEmitsEvents(ScopedMode);
    }
    auto* ScopedBody = InRegistry.try_get<RigidBody2D>(InEntity);
    if (ScopedBody != nullptr)
    {
        // A policy-managed body is always simulated; NoCollision only removes
        // the shape from the broad phase through a zero mask.
        ScopedBody->Flags |= RigidBodyEnabled;
        if (CollisionModeResolvesContacts(ScopedMode))
        {
            ScopedBody->Type = CollisionModeIsStatic(ScopedMode) ? BodyType::Static : BodyType::Dynamic;
        }
    }
}
}
