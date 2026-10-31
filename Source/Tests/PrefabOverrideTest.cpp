// FemboyEngine2D - per-instance prefab overrides.
#include "Engine/Core/FrameArena.hpp"
#include "Engine/ECS/CommandBuffer.hpp"
#include "Engine/ECS/PrefabSystem.hpp"

#include <array>
#include <cstdio>
#include <cstring>
#include <vector>

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
}

int main()
{
    using namespace Fby;

    PrefabAsset ScopedAsset{};
    std::memcpy(ScopedAsset.name, "room", 4);
    ScopedAsset.count = 2;
    ScopedAsset.entities[0].transform.Position = Vector2D{10.0f, 20.0f};
    ScopedAsset.entities[1].transform.Position = Vector2D{30.0f, 40.0f};
    ScopedAsset.entities[1].flags |= PrefabHasSprite;
    ScopedAsset.entities[1].sprite.Color = Color32::FromBytes(1, 2, 3, 255);

    Ecs::Registry ScopedRegistry;
    std::vector<std::byte> ScopedStorage(1u << 20);
    FrameArena ScopedArena;
    ScopedArena.Initialize(ScopedStorage);
    Ecs::CommandBuffer ScopedCommands;
    ScopedCommands.Initialize(&ScopedArena);

    const std::array<PrefabEntityOverride, 2> ScopedOverrides{{
        {.entityIndex = 0,
         .overridePosition = true,
         .position = Vector2D{100.0f, 200.0f},
         .overrideScale = true,
         .scale = Vector2D{3.0f, 3.0f},
         .overrideRotation = true,
         .rotation = 0.5f},
        {.entityIndex = 1,
         .overrideColor = true,
         .color = Color32::FromBytes(200, 100, 50, 255)},
    }};
    std::array<Entity, 4> ScopedSpawned{};
    Check(PrefabSystem::Spawn(ScopedRegistry, ScopedCommands, ScopedAsset, Vector2D{5.0f, 5.0f}, ScopedSpawned,
                              PrefabOverrides{ScopedOverrides}),
          "the prefab spawns with overrides");
    ScopedCommands.Play(ScopedRegistry);

    const Transform2D& ScopedFirst = ScopedRegistry.get<Transform2D>(ScopedSpawned[0]);
    Check(ScopedFirst.Position.X == 105.0f && ScopedFirst.Position.Y == 205.0f,
          "the override position is relative to the spawn origin");
    Check(ScopedFirst.Scale.X == 3.0f && ScopedFirst.Scale.Y == 3.0f, "the override scale applies");
    Check(ScopedFirst.Rotation == 0.5f, "the override rotation applies");

    const Transform2D& ScopedSecond = ScopedRegistry.get<Transform2D>(ScopedSpawned[1]);
    Check(ScopedSecond.Position.X == 35.0f && ScopedSecond.Position.Y == 45.0f,
          "an entity without a transform override keeps the prefab value");
    const Sprite2D& ScopedSprite = ScopedRegistry.get<Sprite2D>(ScopedSpawned[1]);
    Check(ScopedSprite.Color.rgba == Color32::FromBytes(200, 100, 50, 255).rgba, "the sprite colour override applies");

    // No overrides means the prefab values are used unchanged.
    std::array<Entity, 4> ScopedPlainSpawned{};
    Check(PrefabSystem::Spawn(ScopedRegistry, ScopedCommands, ScopedAsset, Vector2D{0.0f, 0.0f}, ScopedPlainSpawned),
          "the prefab spawns without overrides");
    ScopedCommands.Play(ScopedRegistry);
    const Transform2D& ScopedPlain = ScopedRegistry.get<Transform2D>(ScopedPlainSpawned[0]);
    Check(ScopedPlain.Position.X == 10.0f && ScopedPlain.Position.Y == 20.0f, "the plain spawn keeps prefab values");

    if (GFailures == 0)
    {
        std::printf("prefab_overrides: OK\n");
        return 0;
    }
    std::fprintf(stderr, "prefab_overrides: %d failure(s)\n", GFailures);
    return 1;
}
