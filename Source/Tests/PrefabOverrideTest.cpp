// FemboyEngine2D - per-instance prefab overrides.
#include "Engine/Core/FrameArena.hpp"
#include "Engine/Assets/AssetDatabase.hpp"
#include "Engine/Assets/VFS.hpp"
#include "Engine/ECS/CommandBuffer.hpp"
#include "Engine/ECS/ComponentRegistry.hpp"
#include "Engine/ECS/PrefabInstance.hpp"
#include "Engine/ECS/PrefabSystem.hpp"
#include "Engine/ECS/Scene.hpp"

#include <filesystem>
#include <fstream>

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

    // Persistent instance: proxy + transient children, scene captures only the proxy.
    Ecs::Registry ScopedInstanceRegistry;
    std::vector<std::byte> ScopedInstanceStorage(1u << 20);
    FrameArena ScopedInstanceArena;
    ScopedInstanceArena.Initialize(ScopedInstanceStorage);
    Ecs::CommandBuffer ScopedInstanceCommands;
    ScopedInstanceCommands.Initialize(&ScopedInstanceArena);
    const Entity ScopedInstance = ScopedInstanceRegistry.create();
    PrefabInstanceComponent ScopedLink;
    ScopedLink.Origin = Vector2D{5.0f, 5.0f};
    ScopedInstanceRegistry.emplace<PrefabInstanceComponent>(ScopedInstance, ScopedLink);
    PrefabEntityOverride ScopedInstanceOverride;
    ScopedInstanceOverride.entityIndex = 0;
    ScopedInstanceOverride.overridePosition = true;
    ScopedInstanceOverride.position = Vector2D{50.0f, 60.0f};
    ScopedInstanceOverride.overrideScale = true;
    ScopedInstanceOverride.scale = Vector2D{2.0f, 2.0f};
    ScopedInstanceOverride.overrideRotation = true;
    ScopedInstanceOverride.rotation = 0.25f;
    Check(PrefabInstance::SetOverride(ScopedInstanceRegistry, ScopedInstance, ScopedInstanceOverride),
          "the persistent instance stores overrides");
    std::array<Entity, 4> ScopedInstanceChildren{};
    Check(PrefabInstance::Expand(ScopedInstanceRegistry, ScopedInstanceCommands, ScopedInstance, ScopedAsset,
                                 ScopedInstanceChildren),
          "the prefab instance expands");
    ScopedInstanceCommands.Play(ScopedInstanceRegistry);
    Check(ScopedInstanceRegistry.get<PrefabInstanceMemberComponent>(ScopedInstanceChildren[0]).Instance ==
                  ScopedInstance &&
              ScopedInstanceRegistry.get<PrefabInstanceMemberComponent>(ScopedInstanceChildren[1]).PrefabIndex == 1,
          "instance children carry the link and prefab index");
    const Transform2D& ScopedInstanceChild = ScopedInstanceRegistry.get<Transform2D>(ScopedInstanceChildren[0]);
    Check(ScopedInstanceChild.Position.X == 55.0f && ScopedInstanceChild.Position.Y == 65.0f,
          "persistent position override applies relative to the origin");
    Check(ScopedInstanceChild.Scale.X == 2.0f && ScopedInstanceChild.Rotation == 0.25f,
          "persistent scale and rotation overrides apply");

    Scene::ComponentRegistry ScopedComponents;
    Scene::RegisterBuiltInComponents(ScopedComponents);
    const Scene::SceneData ScopedCaptured = Scene::CaptureScene(ScopedInstanceRegistry, ScopedComponents);
    Check(ScopedCaptured.entities.size() == 1, "scene capture skips transient prefab children");
    PrefabInstance::ClearChildren(ScopedInstanceRegistry, ScopedInstance);
    Check(!ScopedInstanceRegistry.valid(ScopedInstanceChildren[0]) &&
              !ScopedInstanceRegistry.valid(ScopedInstanceChildren[1]),
          "clearing an instance destroys its children");

    // Save/load: only the proxy is serialized, children come back from the asset.
    const std::filesystem::path ScopedRoot =
        std::filesystem::current_path() / "fby_prefab_instance_roundtrip";
    std::error_code ScopedFsError;
    std::filesystem::remove_all(ScopedRoot, ScopedFsError);
    std::filesystem::create_directories(ScopedRoot, ScopedFsError);
    const std::filesystem::path ScopedPrefabPath = ScopedRoot / "prefab.fbpref";
    std::vector<std::byte> ScopedPrefabBytes(PrefabSystem::BinarySize(ScopedAsset));
    std::size_t ScopedPrefabSize = 0;
    Check(PrefabSystem::SaveBinary(ScopedAsset, ScopedPrefabBytes, &ScopedPrefabSize),
          "the prefab binary encodes");
    {
        std::ofstream ScopedOut(ScopedPrefabPath, std::ios::binary | std::ios::trunc);
        ScopedOut.write(reinterpret_cast<const char*>(ScopedPrefabBytes.data()),
                        static_cast<std::streamsize>(ScopedPrefabSize));
    }
    Assets::AssetDatabase ScopedDb;
    const Assets::AssetHandle ScopedPrefabHandle = ScopedDb.EnsureMeta(
        AssetId::FromPath("prefab.fbpref", AssetKind::Scene), AssetKind::Scene, "prefab.fbpref");
    Check(ScopedPrefabHandle.IsValid(), "the prefab asset registers");
    VirtualFileSystem ScopedVfs;
    Check(ScopedVfs.MountDirectory(ScopedRoot.string(), 0), "the prefab directory mounts");
    Ecs::Registry ScopedSavedRegistry;
    const Entity ScopedSavedInstance = ScopedSavedRegistry.create();
    PrefabInstanceComponent ScopedSavedLink;
    ScopedSavedLink.Prefab = ScopedPrefabHandle;
    ScopedSavedLink.Origin = Vector2D{2.0f, 3.0f};
    ScopedSavedRegistry.emplace<PrefabInstanceComponent>(ScopedSavedInstance, ScopedSavedLink);
    PrefabEntityOverride ScopedSavedOverride;
    ScopedSavedOverride.entityIndex = 0;
    ScopedSavedOverride.overridePosition = true;
    ScopedSavedOverride.position = Vector2D{50.0f, 60.0f};
    Check(PrefabInstance::SetOverride(ScopedSavedRegistry, ScopedSavedInstance, ScopedSavedOverride),
          "the saved instance stores the override");
    const std::string ScopedSceneText = Scene::SaveSceneToml(ScopedSavedRegistry, ScopedComponents);
    Ecs::Registry ScopedLoadedRegistry;
    Scene::SceneLoadOptions ScopedOptions;
    ScopedOptions.assets = &ScopedVfs;
    ScopedOptions.assetDb = &ScopedDb;
    const Scene::SceneLoadResult ScopedLoaded =
        Scene::LoadSceneToml(ScopedLoadedRegistry, ScopedComponents, ScopedSceneText, ScopedOptions);
    Check(ScopedLoaded.has_value(), "the scene with a prefab instance reloads");
    std::size_t ScopedChildCount = 0;
    auto ScopedLoadedView = ScopedLoadedRegistry.view<PrefabInstanceMemberComponent>();
    for (const Entity ScopedChild : ScopedLoadedView)
    {
        (void)ScopedChild;
        ++ScopedChildCount;
    }
    Check(ScopedChildCount == 2, "scene load rebuilds the prefab children");
    for (const Entity ScopedChild : ScopedLoadedView)
    {
        if (ScopedLoadedView.get<PrefabInstanceMemberComponent>(ScopedChild).PrefabIndex == 0)
        {
            Check(ScopedLoadedRegistry.get<Transform2D>(ScopedChild).Position.X == 52.0f,
                  "scene load restores the persistent position override");
        }
    }
    std::filesystem::remove_all(ScopedRoot, ScopedFsError);

    if (GFailures == 0)
    {
        std::printf("prefab_overrides: OK\n");
        return 0;
    }
    std::fprintf(stderr, "prefab_overrides: %d failure(s)\n", GFailures);
    return 1;
}
