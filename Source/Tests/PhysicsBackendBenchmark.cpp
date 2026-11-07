// FemboyEngine2D - physics backend throughput baseline.
#include "Engine/ECS/Schedule.hpp"
#include "Engine/Physics/Physics.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string_view>

using namespace Fby;

namespace
{
void FillWorld(Ecs::Registry& InRegistry, std::uint32_t InBodies)
{
    const std::uint32_t ScopedColumns = 64;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < InBodies; ++ScopedIndex)
    {
        const Entity ScopedEntity = InRegistry.create();
        Transform2D ScopedTransform;
        ScopedTransform.Position = Vector2D{static_cast<float>(ScopedIndex % ScopedColumns) * 48.0f,
                                             static_cast<float>(ScopedIndex / ScopedColumns) * 24.0f};
        InRegistry.emplace<Transform2D>(ScopedEntity, ScopedTransform);
        RigidBody2D ScopedBody;
        ScopedBody.Type = ScopedIndex % 8 == 0 ? BodyType::Static : BodyType::Dynamic;
        InRegistry.emplace<RigidBody2D>(ScopedEntity, ScopedBody);
        Collider2D ScopedCollider;
        ScopedCollider.HalfExtents = Vector2D{8.0f, 8.0f};
        ScopedCollider.Density = 1.0f;
        InRegistry.emplace<Collider2D>(ScopedEntity, ScopedCollider);
    }
}

double RunBackend(PhysicsBackendKind InKind, std::uint32_t InBodies, int InFrames)
{
    Ecs::Registry ScopedRegistry;
    FillWorld(ScopedRegistry, InBodies);
    PhysicsDesc ScopedDesc;
    ScopedDesc.backend = InKind;
    ScopedDesc.bodyBudget = InBodies;
    ScopedDesc.gravity = Vector2D{0.0f, 9.8f};
    PhysicsSystem ScopedPhysics;
    if (!ScopedPhysics.Initialize(ScopedDesc))
    {
        return -1.0;
    }
    ScopedPhysics.Step(ScopedRegistry, 1.0f / 120.0f);
    const auto ScopedStart = std::chrono::steady_clock::now();
    for (int ScopedFrame = 0; ScopedFrame < InFrames; ++ScopedFrame)
    {
        ScopedPhysics.Step(ScopedRegistry, 1.0f / 120.0f);
    }
    const auto ScopedEnd = std::chrono::steady_clock::now();
    ScopedPhysics.Shutdown();
    return std::chrono::duration<double, std::milli>(ScopedEnd - ScopedStart).count() / static_cast<double>(InFrames);
}
}

int main(int InArgc, char** InArgv)
{
    std::uint32_t ScopedBodies = 2000;
    if (InArgc > 1)
    {
        ScopedBodies = static_cast<std::uint32_t>(std::clamp(std::atoi(InArgv[1]), 100, 50000));
    }
    const double ScopedBox = RunBackend(PhysicsBackendKind::Box2D, ScopedBodies, 60);
    const double ScopedDod = RunBackend(PhysicsBackendKind::Dod, ScopedBodies, 60);
    std::printf("physics_backend_benchmark: %u bodies\n", ScopedBodies);
    std::printf("  Box2D: %.3f ms/frame\n", ScopedBox);
    std::printf("  DOD:   %.3f ms/frame\n", ScopedDod);
    if (ScopedBox <= 0.0 || ScopedDod <= 0.0)
    {
        std::fprintf(stderr, "physics_backend_benchmark: backend initialization failed\n");
        return 1;
    }
    return 0;
}
