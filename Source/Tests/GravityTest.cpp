// FemboyEngine2D - level gravity and local attractor/repulsor fields.
#include "Engine/Core/SimMath.hpp"
#include "Engine/ECS/Components.hpp"
#include "Engine/ECS/Tilemap.hpp"
#include "Engine/Physics/Gravity.hpp"
#include "Engine/Physics/Physics.hpp"

#include <cmath>
#include <cstdio>

namespace
{
std::uint32_t GFailures = 0;

#define FBY_CHECK(condition)                                                                                           \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);                                  \
            ++GFailures;                                                                                               \
        }                                                                                                              \
    } while (false)

[[nodiscard]] bool Near(float InA, float InB, float InTolerance = 1.0e-3f) noexcept
{
    return std::fabs(InA - InB) <= InTolerance;
}
}

int main()
{
    using namespace Fby;
    PhysicsDesc ScopedDesc;
    ScopedDesc.gravity = Vector2D{0.0f, 0.0f};
    PhysicsSystem ScopedPhysics;
    FBY_CHECK(ScopedPhysics.Initialize(ScopedDesc));

    Ecs::Registry ScopedRegistry;
    const Entity ScopedField = ScopedRegistry.create();
    ScopedRegistry.emplace<Transform2D>(ScopedField, Transform2D{Vector2D{0.0f, 0.0f}});
    auto& ScopedGravityBody = ScopedRegistry.emplace<GravityBodyComponent>(ScopedField);
    ScopedGravityBody.Shape = static_cast<std::uint32_t>(GravityBodyShape::Circle);
    ScopedGravityBody.Mode = static_cast<std::uint32_t>(GravityBodyMode::Attract);
    ScopedGravityBody.Falloff = static_cast<std::uint32_t>(GravityFalloff::None);
    ScopedGravityBody.Strength = 20.0f;
    ScopedGravityBody.Radius = 100.0f;

    const Entity ScopedTarget = ScopedRegistry.create();
    ScopedRegistry.emplace<Transform2D>(ScopedTarget, Transform2D{Vector2D{50.0f, 0.0f}});
    auto& ScopedTargetBody = ScopedRegistry.emplace<RigidBody2D>(ScopedTarget);
    ScopedTargetBody.Type = BodyType::Dynamic;
    ScopedRegistry.emplace<Collider2D>(ScopedTarget, Collider2D{Vector2D{4.0f, 4.0f}});
    ScopedPhysics.EnsureBodies(ScopedRegistry);

    GravitySystem ScopedGravity;
    ScopedGravity.Update(ScopedRegistry, ScopedPhysics, 1.0f / 60.0f);
    ScopedPhysics.Step(ScopedRegistry, 1.0f / 60.0f);
    FBY_CHECK(ScopedRegistry.get<RigidBody2D>(ScopedTarget).LinearVelocity.X < 0.0f);

    // Repel flips the direction.
    ScopedRegistry.get<GravityBodyComponent>(ScopedField).Mode = static_cast<std::uint32_t>(GravityBodyMode::Repel);
    auto& ScopedBody = ScopedRegistry.get<RigidBody2D>(ScopedTarget);
    ScopedBody.LinearVelocity = Vector2D{0.0f, 0.0f};
    ScopedBody.Position = Vector2D{50.0f, 0.0f};
    ScopedRegistry.get<Transform2D>(ScopedTarget).Position = Vector2D{50.0f, 0.0f};
    ScopedGravity.Update(ScopedRegistry, ScopedPhysics, 1.0f / 60.0f);
    ScopedPhysics.Step(ScopedRegistry, 1.0f / 60.0f);
    FBY_CHECK(ScopedRegistry.get<RigidBody2D>(ScopedTarget).LinearVelocity.X > 0.0f);

    // Directional pushes along its local axis instead of toward the centre.
    auto& ScopedDirectional = ScopedRegistry.get<GravityBodyComponent>(ScopedField);
    ScopedDirectional.Mode = static_cast<std::uint32_t>(GravityBodyMode::Directional);
    ScopedDirectional.Direction = Vector2D{0.0f, -1.0f};
    ScopedBody.LinearVelocity = Vector2D{0.0f, 0.0f};
    ScopedBody.Position = Vector2D{50.0f, 0.0f};
    ScopedRegistry.get<Transform2D>(ScopedTarget).Position = Vector2D{50.0f, 0.0f};
    ScopedGravity.Update(ScopedRegistry, ScopedPhysics, 1.0f / 60.0f);
    ScopedPhysics.Step(ScopedRegistry, 1.0f / 60.0f);
    FBY_CHECK(ScopedRegistry.get<RigidBody2D>(ScopedTarget).LinearVelocity.Y < 0.0f);

    // The same local direction rotates with the source transform.
    ScopedRegistry.get<Transform2D>(ScopedField).Rotation = Sim::kHalfPi;
    ScopedBody.LinearVelocity = Vector2D{0.0f, 0.0f};
    ScopedBody.Position = Vector2D{50.0f, 0.0f};
    ScopedRegistry.get<Transform2D>(ScopedTarget).Position = Vector2D{50.0f, 0.0f};
    ScopedGravity.Update(ScopedRegistry, ScopedPhysics, 1.0f / 60.0f);
    ScopedPhysics.Step(ScopedRegistry, 1.0f / 60.0f);
    FBY_CHECK(ScopedRegistry.get<RigidBody2D>(ScopedTarget).LinearVelocity.X > 0.0f);

    // Level modes override the world vector immediately.
    const Entity ScopedLevel = ScopedRegistry.create();
    auto& ScopedLevelGravity = ScopedRegistry.emplace<LevelGravityComponent>(ScopedLevel);
    ScopedLevelGravity.Mode = static_cast<std::uint32_t>(GravityDirection::Up);
    ScopedLevelGravity.Strength = 5.0f;
    ScopedGravity.Update(ScopedRegistry, ScopedPhysics, 1.0f / 60.0f);
    FBY_CHECK(Near(ScopedPhysics.Gravity().X, 0.0f));
    FBY_CHECK(Near(ScopedPhysics.Gravity().Y, -5.0f));

    ScopedLevelGravity.Mode = static_cast<std::uint32_t>(GravityDirection::None);
    ScopedGravity.Update(ScopedRegistry, ScopedPhysics, 1.0f / 60.0f);
    FBY_CHECK(Near(ScopedPhysics.Gravity().X, 0.0f) && Near(ScopedPhysics.Gravity().Y, 0.0f));

    // Terminal speed is applied along the authored direction.
    ScopedLevelGravity.Mode = static_cast<std::uint32_t>(GravityDirection::Down);
    ScopedLevelGravity.Strength = 9.8f;
    ScopedLevelGravity.MaxFallSpeed = 50.0f;
    ScopedRegistry.get<RigidBody2D>(ScopedTarget).LinearVelocity = Vector2D{0.0f, 100.0f};
    ScopedGravity.Update(ScopedRegistry, ScopedPhysics, 1.0f / 60.0f);
    FBY_CHECK(Near(ScopedRegistry.get<RigidBody2D>(ScopedTarget).LinearVelocity.Y, 50.0f));

    // Without a level component, projection decides the default: top-down and
    // iso are weightless, side falls down.
    ScopedRegistry.remove<LevelGravityComponent>(ScopedLevel);
    const Entity ScopedTilemap = ScopedRegistry.create();
    auto& ScopedMap = ScopedRegistry.emplace<TilemapComponent>(ScopedTilemap);
    ScopedMap.Grid.Mode = Grid::ProjectionMode::TopDown;
    ScopedGravity.Update(ScopedRegistry, ScopedPhysics, 1.0f / 60.0f);
    FBY_CHECK(Near(ScopedPhysics.Gravity().Y, 0.0f));
    ScopedMap.Grid.Mode = Grid::ProjectionMode::Side;
    ScopedGravity.Update(ScopedRegistry, ScopedPhysics, 1.0f / 60.0f);
    FBY_CHECK(Near(ScopedPhysics.Gravity().Y, 9.8f));

    if (GFailures == 0)
    {
        std::printf("gravity_test: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "gravity_test: %u failure(s)\n", GFailures);
    return 1;
}
