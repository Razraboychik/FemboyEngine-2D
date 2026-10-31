// FemboyEngine2D - opt-in transform hierarchy.
#include "Engine/ECS/Hierarchy.hpp"

#include <cmath>
#include <cstdio>

namespace
{
int GFailures = 0;

void Check(bool InCondition, const char* InMessage)
{
    if (!InCondition)
    {
        std::fprintf(stderr, "FAIL: %s\n", InMessage);
        ++GFailures;
    }
}

bool Near(float InA, float InB)
{
    return std::fabs(InA - InB) < 0.001f;
}
}

int main()
{
    using namespace Fby;
    using namespace Fby::Ecs;

    Registry ScopedRegistry;
    const Entity ScopedRoot = ScopedRegistry.create();
    ScopedRegistry.emplace<LocalTransform2D>(
        ScopedRoot, LocalTransform2D{Vector2D{10.0f, 20.0f}, Vector2D{2.0f, 2.0f}, 1.57079633f});
    const Entity ScopedChild = ScopedRegistry.create();
    ScopedRegistry.emplace<LocalTransform2D>(
        ScopedChild, LocalTransform2D{Vector2D{5.0f, 0.0f}, Vector2D{0.5f, 0.5f}, 0.0f});
    ScopedRegistry.emplace<Parent2D>(ScopedChild, Parent2D{ScopedRoot});
    const Entity ScopedGrandchild = ScopedRegistry.create();
    ScopedRegistry.emplace<LocalTransform2D>(ScopedGrandchild, LocalTransform2D{Vector2D{0.0f, 1.0f}});
    ScopedRegistry.emplace<Parent2D>(ScopedGrandchild, Parent2D{ScopedChild});

    Check(UpdateHierarchy(ScopedRegistry), "a valid hierarchy updates");
    const Transform2D& ScopedRootWorld = ScopedRegistry.get<Transform2D>(ScopedRoot);
    Check(Near(ScopedRootWorld.Position.X, 10.0f) && Near(ScopedRootWorld.Position.Y, 20.0f),
          "a root local transform becomes world");
    const Transform2D& ScopedChildWorld = ScopedRegistry.get<Transform2D>(ScopedChild);
    Check(Near(ScopedChildWorld.Position.X, 10.0f) && Near(ScopedChildWorld.Position.Y, 30.0f),
          "the child rotates and scales through the parent");
    Check(Near(ScopedChildWorld.Scale.X, 1.0f) && Near(ScopedChildWorld.Rotation, 1.57079633f),
          "scale and rotation multiply through the parent");
    const Transform2D& ScopedGrandchildWorld = ScopedRegistry.get<Transform2D>(ScopedGrandchild);
    Check(Near(ScopedGrandchildWorld.Position.X, 9.0f) && Near(ScopedGrandchildWorld.Position.Y, 30.0f),
          "the grandchild composes the full chain");

    // A plain Transform2D entity is untouched.
    const Entity ScopedPlain = ScopedRegistry.create();
    ScopedRegistry.emplace<Transform2D>(ScopedPlain, Transform2D{Vector2D{7.0f, 8.0f}});
    Check(UpdateHierarchy(ScopedRegistry), "a scene without locals still updates");
    Check(Near(ScopedRegistry.get<Transform2D>(ScopedPlain).Position.X, 7.0f), "plain transforms are untouched");

    // A cycle is reported and leaves the members at their previous world state.
    const Entity ScopedCycleA = ScopedRegistry.create();
    const Entity ScopedCycleB = ScopedRegistry.create();
    ScopedRegistry.emplace<LocalTransform2D>(ScopedCycleA, LocalTransform2D{Vector2D{1.0f, 1.0f}});
    ScopedRegistry.emplace<LocalTransform2D>(ScopedCycleB, LocalTransform2D{Vector2D{2.0f, 2.0f}});
    ScopedRegistry.emplace<Parent2D>(ScopedCycleA, Parent2D{ScopedCycleB});
    ScopedRegistry.emplace<Parent2D>(ScopedCycleB, Parent2D{ScopedCycleA});
    Check(!UpdateHierarchy(ScopedRegistry), "a cycle is reported");
    Check(!ScopedRegistry.all_of<Transform2D>(ScopedCycleA), "a cycle member keeps no new world transform");

    if (GFailures == 0)
    {
        std::printf("hierarchy: OK\n");
        return 0;
    }
    std::fprintf(stderr, "hierarchy: %d failure(s)\n", GFailures);
    return 1;
}
