// FemboyEngine2D - projectile movement and tunneling guard.
#pragma once

#include "Engine/Physics/Physics.hpp"

#include <span>
#include <vector>

namespace Fby
{

struct ProjectileHitEvent
{
    Entity projectile = entt::null;
    Entity target = entt::null;
    Vector2D point{};
    Vector2D normal{};
    float speed = 0.0f;
};

// Projectiles can be hitscan, swept or ordinary rigid bodies. Auto promotes a
// projectile to a continuous query when its per-step displacement is larger
// than half of the authored wall thickness, so fast bullets cannot tunnel.
class ProjectileSystem
{
public:
    ProjectileSystem() = default;
    ~ProjectileSystem() = default;

    ProjectileSystem(const ProjectileSystem&) = delete;
    ProjectileSystem& operator=(const ProjectileSystem&) = delete;

    void Step(Ecs::Registry& InRegistry, PhysicsSystem& InPhysics, float InFixedDt);
    [[nodiscard]] std::span<const ProjectileHitEvent> Hits() const noexcept
    {
        return {MHits.data(), MHits.size()};
    }
    void ClearHits() noexcept
    {
        MHits.clear();
    }

private:
    [[nodiscard]] static std::uint32_t ResolveMode(const Projectile2D& InProjectile, float InDisplacement) noexcept;
    void RecordHit(Entity InProjectile, Entity InTarget, Vector2D InPoint, Vector2D InNormal, float InSpeed);

    std::vector<ProjectileHitEvent> MHits{};
};

}
