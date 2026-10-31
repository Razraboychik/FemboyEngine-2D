// FemboyEngine2D - mdspan SDF sampling and Box2D v3 push-out.
#include "Engine/Core/JobSystem.hpp"
#include "Engine/Physics/Physics.hpp"
#include "Engine/Physics/SdfCollision.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
std::uint32_t GFailures = 0;

#define FBY_CHECK(condition)                                                                                           \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);                                  \
            ++GFailures;                                                                                              \
        }                                                                                                              \
    } while (false)
}

int main()
{
    using namespace Fby;
    constexpr int kWidth = 128;
    constexpr int kHeight = 128;
    std::vector<float> ScopedSamples(kWidth * kHeight);
    for (int ScopedY = 0; ScopedY < kHeight; ++ScopedY)
    {
        for (int ScopedX = 0; ScopedX < kWidth; ++ScopedX)
        {
            const float ScopedPx = static_cast<float>(ScopedX - kWidth / 2) * 2.0f;
            const float ScopedPy = static_cast<float>(ScopedY - kHeight / 2) * 2.0f;
            ScopedSamples[ScopedY * kWidth + ScopedX] = std::sqrt(ScopedPx * ScopedPx + ScopedPy * ScopedPy) - 50.0f;
        }
    }
    SdfField ScopedField{};
    ScopedField.samples = ScopedSamples.data();
    ScopedField.width = kWidth;
    ScopedField.height = kHeight;
    ScopedField.origin = Vector2D{-128.0f, -128.0f};
    ScopedField.cellSize = Vector2D{2.0f, 2.0f};
    FBY_CHECK(ScopedField.Valid());
    FBY_CHECK(ScopedField.Sample(Vector2D{0.0f, 0.0f}) < -49.0f);
    FBY_CHECK(ScopedField.Sample(Vector2D{0.0f, 50.0f}) > -1.0f);
    const Vector2D ScopedGradient = ScopedField.Gradient(Vector2D{30.0f, 0.0f});
    FBY_CHECK(ScopedGradient.X > 0.9f);
    FBY_CHECK(std::fabs(ScopedGradient.Y) < 0.2f);
    FBY_CHECK(ScopedField.Sample(Vector2D{10000.0f, 10000.0f}) > 1000.0f);

    PhysicsSystem ScopedPhysics;
    PhysicsDesc ScopedPhysicsDesc{};
    ScopedPhysicsDesc.gravity = Vector2D{0.0f, 0.0f};
    FBY_CHECK(ScopedPhysics.Initialize(ScopedPhysicsDesc));
    Ecs::Registry ScopedRegistry;
    const Entity ScopedBody = ScopedRegistry.create();
    Transform2D ScopedTransform{};
    ScopedTransform.Position = Vector2D{30.0f, 0.0f};
    ScopedRegistry.emplace<Transform2D>(ScopedBody, ScopedTransform);
    RigidBody2D ScopedRigid{};
    ScopedRigid.Type = BodyType::Dynamic;
    ScopedRegistry.emplace<RigidBody2D>(ScopedBody, ScopedRigid);
    Collider2D ScopedCollider{};
    ScopedCollider.HalfExtents = Vector2D{8.0f, 8.0f};
    ScopedRegistry.emplace<Collider2D>(ScopedBody, ScopedCollider);
    ScopedPhysics.EnsureBodies(ScopedRegistry);

    SdfCollisionSystem ScopedSystem;
    ScopedSystem.Initialize({});
    ScopedSystem.SetField(ScopedField);
    const std::uint32_t ScopedCorrected = ScopedSystem.Step(ScopedRegistry, ScopedPhysics, 1.0f / 120.0f);
    FBY_CHECK(ScopedCorrected == 1);
    FBY_CHECK(ScopedSystem.Contacts().size() == 1);
    const RigidBody2D& ScopedAfter = ScopedRegistry.get<RigidBody2D>(ScopedBody);
    FBY_CHECK(ScopedAfter.Position.X > 30.0f);
    FBY_CHECK(ScopedSystem.Contacts()[0].normal.X > 0.9f);
    FBY_CHECK(ScopedSystem.Contacts()[0].penetration > 0.0f);

    // A body outside the solid must not be modified.
    const Entity ScopedFree = ScopedRegistry.create();
    Transform2D ScopedFreeTransform{};
    ScopedFreeTransform.Position = Vector2D{0.0f, 200.0f};
    ScopedRegistry.emplace<Transform2D>(ScopedFree, ScopedFreeTransform);
    RigidBody2D ScopedFreeRigid{};
    ScopedFreeRigid.Type = BodyType::Dynamic;
    ScopedRegistry.emplace<RigidBody2D>(ScopedFree, ScopedFreeRigid);
    Collider2D ScopedFreeCollider{};
    ScopedFreeCollider.HalfExtents = Vector2D{4.0f, 4.0f};
    ScopedRegistry.emplace<Collider2D>(ScopedFree, ScopedFreeCollider);
    ScopedPhysics.EnsureBodies(ScopedRegistry);
    const Vector2D ScopedBefore{ScopedRegistry.get<RigidBody2D>(ScopedFree).Position};
    ScopedSystem.Step(ScopedRegistry, ScopedPhysics, 1.0f / 120.0f);
    FBY_CHECK(ScopedRegistry.get<RigidBody2D>(ScopedFree).Position == ScopedBefore);

    // A body destroyed between steps must not leave its dead shape id in the
    // contact-end events: destroying a touching body makes Box2D push end-touch
    // events, and reading them after the destroy used to assert in b2GetShape
    // (this is the "editor closes when opening a project" crash).
    {
        const std::uint32_t ScopedBodiesBefore = ScopedPhysics.LiveBodyCount();
        const Entity ScopedGround = ScopedRegistry.create();
        Transform2D ScopedGroundTransform;
        ScopedGroundTransform.Position = Vector2D{0.0f, 0.0f};
        ScopedRegistry.emplace<Transform2D>(ScopedGround, ScopedGroundTransform);
        RigidBody2D ScopedGroundRigid;
        ScopedGroundRigid.Type = BodyType::Static;
        ScopedRegistry.emplace<RigidBody2D>(ScopedGround, ScopedGroundRigid);
        Collider2D ScopedGroundCollider;
        ScopedGroundCollider.HalfExtents = Vector2D{64.0f, 8.0f};
        ScopedGroundCollider.ContactEvents = true;
        ScopedRegistry.emplace<Collider2D>(ScopedGround, ScopedGroundCollider);

        const Entity ScopedBox = ScopedRegistry.create();
        Transform2D ScopedBoxTransform;
        ScopedBoxTransform.Position = Vector2D{0.0f, 10.0f};
        ScopedRegistry.emplace<Transform2D>(ScopedBox, ScopedBoxTransform);
        RigidBody2D ScopedBoxRigid;
        ScopedBoxRigid.Type = BodyType::Dynamic;
        ScopedRegistry.emplace<RigidBody2D>(ScopedBox, ScopedBoxRigid);
        Collider2D ScopedBoxCollider;
        ScopedBoxCollider.HalfExtents = Vector2D{8.0f, 8.0f};
        ScopedBoxCollider.ContactEvents = true;
        ScopedRegistry.emplace<Collider2D>(ScopedBox, ScopedBoxCollider);

        ScopedPhysics.Step(ScopedRegistry, 1.0f / 120.0f);
        FBY_CHECK(ScopedPhysics.LiveBodyCount() == ScopedBodiesBefore + 2);
        ScopedRegistry.destroy(ScopedBox);
        ScopedRegistry.destroy(ScopedGround);
        // Pruning the touching bodies pushes end-touch events for dead shapes;
        // the guarded event lookup keeps this step assert-free.
        ScopedPhysics.Step(ScopedRegistry, 1.0f / 120.0f);
        FBY_CHECK(ScopedPhysics.LiveBodyCount() == ScopedBodiesBefore);
    }

    ScopedPhysics.Shutdown();
    if (GFailures == 0)
    {
        std::printf("sdf_physics_test: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "sdf_physics_test: %u failure(s)\n", GFailures);
    return 1;
}
