// FemboyEngine2D - physics -> animation event routing.
#pragma once

#include "Engine/ECS/SpriteAnimation.hpp"
#include "Engine/Physics/Physics.hpp"

#include <span>

namespace Fby::Gameplay
{

inline void RouteContactEvents(std::span<const PhysicsContactEvent> InContacts, Ecs::Registry& InRegistry,
                               SpriteAnimationEventQueue& InEvents) noexcept
{
    for (const PhysicsContactEvent& ScopedContact : InContacts)
    {
        if (ScopedContact.type == PhysicsEventType::EndTouch)
        {
            continue;
        }
        const Entity ScopedCandidates[2] = {ScopedContact.a, ScopedContact.b};
        for (const Entity ScopedEntity : ScopedCandidates)
        {
            if (ScopedEntity == entt::null)
            {
                continue;
            }
            if (const auto* ScopedSpriteAnimation = InRegistry.try_get<SpriteAnimationComponent>(ScopedEntity))
            {
                (void)InEvents.Push(ScopedEntity, ScopedSpriteAnimation->CurrentFrame, kSpriteAnimationEventContact);
            }
        }
    }
}
}
