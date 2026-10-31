// FemboyEngine2D - Narrative Tree (BlindMode semantic scene description) tests.
#include "Engine/UI/NarrativeTree.hpp"

#include "Engine/ECS/Skeleton.hpp"
#include "Engine/Physics/Cloth.hpp"
#include "Engine/Physics/Physics.hpp"
#include "Engine/Physics/PhysicsJoints.hpp"

#include <cstdio>
#include <cstring>
#include <string_view>

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

    Ecs::Registry ScopedRegistry;
    const Entity ScopedActor = ScopedRegistry.create();
    ScopedRegistry.emplace<Transform2D>(ScopedActor, Transform2D{Vector2D{120.0f, 40.0f}, Vector2D{1.0f, 1.0f}, 0.0f});
    ScopedRegistry.emplace<Velocity>(ScopedActor, Velocity{Vector2D{30.0f, 40.0f}});
    GameplayState ScopedState;
    ScopedState.Bits = 1ull << 0;
    ScopedRegistry.emplace<GameplayState>(ScopedActor, ScopedState);

    const Entity ScopedBody = ScopedRegistry.create();
    ScopedRegistry.emplace<Transform2D>(ScopedBody, Transform2D{Vector2D{300.0f, 200.0f}, Vector2D{1.0f, 1.0f}, 0.0f});
    ScopedRegistry.emplace<RigidBody2D>(ScopedBody, RigidBody2D{});
    ScopedRegistry.emplace<Collider2D>(ScopedBody, Collider2D{});

    const Entity ScopedCloth = ScopedRegistry.create();
    ScopedRegistry.emplace<Transform2D>(ScopedCloth, Transform2D{});
    ScopedRegistry.emplace<ClothComponent>(ScopedCloth, ClothComponent{});

    const Entity ScopedRope = ScopedRegistry.create();
    ScopedRegistry.emplace<Transform2D>(ScopedRope, Transform2D{});
    ScopedRegistry.emplace<JointLinkComponent>(ScopedRope, JointLinkComponent{});

    const Entity ScopedVisual = ScopedRegistry.create();
    ScopedRegistry.emplace<Transform2D>(ScopedVisual, Transform2D{});
    ScopedRegistry.emplace<Sprite2D>(ScopedVisual, Sprite2D{});

    const Entity ScopedCamera = ScopedRegistry.create();
    ScopedRegistry.emplace<Transform2D>(ScopedCamera, Transform2D{});
    ScopedRegistry.emplace<Camera2D>(ScopedCamera, Camera2D{});

    const Entity ScopedNamed = ScopedRegistry.create();
    ScopedRegistry.emplace<Transform2D>(ScopedNamed, Transform2D{Vector2D{10.0f, 20.0f}, Vector2D{1.0f, 1.0f}, 0.0f});
    ScopedRegistry.emplace<NarrativeLabelComponent>(ScopedNamed, NarrativeLabelComponent{"Boss"});

    Ui::NarrativeTree ScopedTree;
    ScopedTree.SetLocale(Ui::NarrativeLocale::English);
    ScopedTree.Build(ScopedRegistry);
    FBY_CHECK(ScopedTree.NodeCount() == 7);
    FBY_CHECK(ScopedTree.ArchetypeOf(ScopedActor) == Ui::NarrativeArchetype::Actor);
    FBY_CHECK(ScopedTree.ArchetypeOf(ScopedBody) == Ui::NarrativeArchetype::PhysicsBody);
    FBY_CHECK(ScopedTree.ArchetypeOf(ScopedCloth) == Ui::NarrativeArchetype::Cloth);
    FBY_CHECK(ScopedTree.ArchetypeOf(ScopedRope) == Ui::NarrativeArchetype::Rope);
    FBY_CHECK(ScopedTree.ArchetypeOf(ScopedVisual) == Ui::NarrativeArchetype::Visual);
    FBY_CHECK(ScopedTree.ArchetypeOf(ScopedCamera) == Ui::NarrativeArchetype::Camera);

    char ScopedBuffer[160]{};
    ScopedTree.DescribeEntity(ScopedRegistry, ScopedActor, ScopedBuffer, sizeof(ScopedBuffer));
    FBY_CHECK(std::strstr(ScopedBuffer, "actor #") != nullptr);
    FBY_CHECK(std::strstr(ScopedBuffer, "speed 50") != nullptr);
    FBY_CHECK(std::strstr(ScopedBuffer, "grounded") != nullptr);

    ScopedTree.DescribeEntity(ScopedRegistry, ScopedBody, ScopedBuffer, sizeof(ScopedBuffer));
    FBY_CHECK(std::strstr(ScopedBuffer, "physics body") != nullptr);
    FBY_CHECK(std::strstr(ScopedBuffer, "asleep") == nullptr);

    ScopedTree.DescribeEntity(ScopedRegistry, ScopedCloth, ScopedBuffer, sizeof(ScopedBuffer));
    FBY_CHECK(std::strstr(ScopedBuffer, "cloth") != nullptr);

    ScopedTree.DescribeStateChange(ScopedRegistry, ScopedActor, 0, true, ScopedBuffer, sizeof(ScopedBuffer));
    FBY_CHECK(std::strstr(ScopedBuffer, "on grounded") != nullptr);

    ScopedTree.DescribeContact(ScopedRegistry, ScopedActor, ScopedBody, 2, 123.0f, ScopedBuffer, sizeof(ScopedBuffer));
    FBY_CHECK(std::strstr(ScopedBuffer, "Contact:") != nullptr);
    FBY_CHECK(std::strstr(ScopedBuffer, "speed 123") != nullptr);

    ScopedTree.DescribeScene(ScopedRegistry, ScopedBuffer, sizeof(ScopedBuffer));
    FBY_CHECK(std::strstr(ScopedBuffer, "Scene: 7 entities") != nullptr);
    FBY_CHECK(std::strstr(ScopedBuffer, "1 actors") != nullptr);
    FBY_CHECK(std::strstr(ScopedBuffer, "3 physical") != nullptr);

    ScopedTree.DescribeHierarchy(ScopedRegistry, ScopedBody, ScopedBuffer, sizeof(ScopedBuffer));
    FBY_CHECK(std::strstr(ScopedBuffer, "Scene > physics > physics body #") != nullptr);

    // Authored NarrativeLabelComponent replaces the archetype and the raw id.
    ScopedTree.DescribeEntity(ScopedRegistry, ScopedNamed, ScopedBuffer, sizeof(ScopedBuffer));
    FBY_CHECK(std::strstr(ScopedBuffer, "Boss, position 10, 20") != nullptr);
    FBY_CHECK(std::strstr(ScopedBuffer, "Boss #") == nullptr);
    ScopedTree.DescribeStateChange(ScopedRegistry, ScopedNamed, 0, true, ScopedBuffer, sizeof(ScopedBuffer));
    FBY_CHECK(std::strstr(ScopedBuffer, "Boss:") != nullptr);
    ScopedTree.DescribeHierarchy(ScopedRegistry, ScopedNamed, ScopedBuffer, sizeof(ScopedBuffer));
    FBY_CHECK(std::strstr(ScopedBuffer, "Boss") != nullptr);

    // Russian templates: UTF-8 bytes for the actor noun and the "on" state.
    ScopedTree.SetLocaleFromCode("ru-RU");
    FBY_CHECK(ScopedTree.Locale() == Ui::NarrativeLocale::Russian);
    ScopedTree.DescribeEntity(ScopedRegistry, ScopedActor, ScopedBuffer, sizeof(ScopedBuffer));
    FBY_CHECK(std::strstr(ScopedBuffer, "\xD0\x90\xD0\xBA\xD1\x82\xD1\x91\xD1\x80") != nullptr);
    FBY_CHECK(std::strstr(ScopedBuffer, "\xD1\x81\xD0\xBA\xD0\xBE\xD1\x80\xD0\xBE\xD1\x81\xD1\x82\xD1\x8C") != nullptr);
    ScopedTree.DescribeStateChange(ScopedRegistry, ScopedActor, 0, true, ScopedBuffer, sizeof(ScopedBuffer));
    FBY_CHECK(std::strstr(ScopedBuffer, "\xD0\xB2\xD0\xBA\xD0\xBB\xD1\x8E\xD1\x87\xD0\xB5\xD0\xBD\xD0\xBE") != nullptr);

    // Truncation must stay NUL-terminated and bounded.
    char ScopedTiny[12]{};
    const std::uint32_t ScopedLength =
        ScopedTree.DescribeEntity(ScopedRegistry, ScopedActor, ScopedTiny, sizeof(ScopedTiny));
    FBY_CHECK(ScopedLength == sizeof(ScopedTiny) - 1);
    FBY_CHECK(ScopedTiny[sizeof(ScopedTiny) - 1] == '\0');

    ScopedTree.SetLocaleFromCode("en-US");
    FBY_CHECK(ScopedTree.Locale() == Ui::NarrativeLocale::English);

    if (GFailures == 0)
    {
        std::printf("narrative_tree_test: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "narrative_tree_test: %u failure(s)\n", GFailures);
    return 1;
}
