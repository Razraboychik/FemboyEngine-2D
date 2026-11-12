// FemboyEngine2D - opt-in transform hierarchy.
#include "Engine/ECS/Hierarchy.hpp"
#include "Engine/ECS/Scene.hpp"

#include <cmath>
#include <cstdio>
#include <string>

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

    // A deep chain resolves through the memoized pass without recomputing the
    // whole ancestor path for every descendant.
    {
        Registry ScopedDeep;
        Entity ScopedParent = ScopedDeep.create();
        ScopedDeep.emplace<LocalTransform2D>(ScopedParent, LocalTransform2D{Vector2D{1.0f, 0.0f}});
        for (int ScopedDepth = 0; ScopedDepth < 32; ++ScopedDepth)
        {
            const Entity ScopedChainChild = ScopedDeep.create();
            ScopedDeep.emplace<LocalTransform2D>(ScopedChainChild, LocalTransform2D{Vector2D{1.0f, 0.0f}});
            ScopedDeep.emplace<Parent2D>(ScopedChainChild, Parent2D{ScopedParent});
            ScopedParent = ScopedChainChild;
        }
        Check(UpdateHierarchy(ScopedDeep), "a deep chain resolves");
        Check(Near(ScopedDeep.get<Transform2D>(ScopedParent).Position.X, 33.0f),
              "the deepest child accumulates every offset");
    }

    // Parenting preserves the child's world transform and rejects cycles.
    {
        Registry ScopedParenting;
        const Entity ScopedParentEntity = ScopedParenting.create();
        ScopedParenting.emplace<LocalTransform2D>(
            ScopedParentEntity, LocalTransform2D{Vector2D{10.0f, 0.0f}, Vector2D{2.0f, 2.0f}, 1.57079633f});
        (void)UpdateHierarchy(ScopedParenting);
        const Entity ScopedChildEntity = ScopedParenting.create();
        ScopedParenting.emplace<Transform2D>(ScopedChildEntity, Transform2D{Vector2D{0.0f, 5.0f}});

        Check(SetParent(ScopedParenting, ScopedChildEntity, ScopedParentEntity), "a parent is assigned");
        const Transform2D& ScopedParentedWorld = ScopedParenting.get<Transform2D>(ScopedChildEntity);
        Check(Near(ScopedParentedWorld.Position.X, 0.0f) && Near(ScopedParentedWorld.Position.Y, 5.0f),
              "parenting keeps the child's world position");
        Check(Near(ScopedParentedWorld.Scale.X, 1.0f) && Near(ScopedParentedWorld.Rotation, 0.0f),
              "parenting keeps the child's world scale and rotation");
        const LocalTransform2D& ScopedChildLocal = ScopedParenting.get<LocalTransform2D>(ScopedChildEntity);
        Check(Near(ScopedChildLocal.Position.X, 2.5f) && Near(ScopedChildLocal.Position.Y, 5.0f),
              "the local position is expressed in the parent space");
        Check(Near(ScopedChildLocal.Scale.X, 0.5f) && Near(ScopedChildLocal.Rotation, -1.57079633f),
              "the local scale and rotation invert the parent transform");

        Check(!SetParent(ScopedParenting, ScopedChildEntity, ScopedChildEntity), "self-parenting is rejected");
        Check(SetParent(ScopedParenting, ScopedChildEntity, Fby::kNullEntity), "the parent is cleared");
        Check(!ScopedParenting.all_of<Parent2D>(ScopedChildEntity), "clearing removes Parent2D");
        Check(Near(ScopedParenting.get<Transform2D>(ScopedChildEntity).Position.Y, 5.0f),
              "clearing the parent keeps the world position");

        Check(SetParent(ScopedParenting, ScopedParentEntity, ScopedChildEntity), "a former child can be a parent");
        Check(!SetParent(ScopedParenting, ScopedChildEntity, ScopedParentEntity), "a parent chain cycle is rejected");
    }

    // The hierarchy survives a TOML scene round trip: both components are
    // registered, and Parent2D's entity reference is remapped on load.
    {
        Registry ScopedSerialized;
        const Entity ScopedSerializedRoot = ScopedSerialized.create();
        ScopedSerialized.emplace<LocalTransform2D>(ScopedSerializedRoot, LocalTransform2D{Vector2D{3.0f, 4.0f}});
        const Entity ScopedSerializedChild = ScopedSerialized.create();
        ScopedSerialized.emplace<LocalTransform2D>(ScopedSerializedChild, LocalTransform2D{Vector2D{1.0f, 0.0f}});
        ScopedSerialized.emplace<Parent2D>(ScopedSerializedChild, Parent2D{ScopedSerializedRoot});

        Fby::Scene::ComponentRegistry ScopedComponents;
        Fby::Scene::RegisterBuiltInComponents(ScopedComponents);
        const std::string ScopedToml = Fby::Scene::SaveSceneToml(ScopedSerialized, ScopedComponents);
        Registry ScopedRoundTrip;
        const auto ScopedLoaded = Fby::Scene::LoadSceneToml(ScopedRoundTrip, ScopedComponents, ScopedToml);
        Check(ScopedLoaded.has_value(), "the hierarchy scene loads");
        Check(UpdateHierarchy(ScopedRoundTrip), "the loaded hierarchy composes");
        bool ScopedSawParent = false;
        for (const Entity ScopedEntity : ScopedRoundTrip.view<Parent2D>())
        {
            const Parent2D* ScopedParent = ScopedRoundTrip.try_get<Parent2D>(ScopedEntity);
            ScopedSawParent =
                ScopedSawParent || (ScopedParent != nullptr && ScopedParent->Parent != kNullEntity &&
                                    ScopedRoundTrip.try_get<LocalTransform2D>(ScopedParent->Parent) != nullptr);
        }
        Check(ScopedSawParent, "the parent reference is remapped into the loaded registry");
    }

    if (GFailures == 0)
    {
        std::printf("hierarchy: OK\n");
        return 0;
    }
    std::fprintf(stderr, "hierarchy: %d failure(s)\n", GFailures);
    return 1;
}
