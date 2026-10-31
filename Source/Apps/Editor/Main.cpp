// FemboyEngine2D - editor application.
#include "Engine/Core/CrashHandler.hpp"
#include "Engine/Core/AssetPaths.hpp"
#include "Engine/Core/StoragePaths.hpp"
#include "Engine/Core/Time.hpp"
#include "Engine/Assets/AssetDatabase.hpp"
#include "Engine/Assets/TextureArray.hpp"
#include "Engine/Assets/VFS.hpp"
#include "Engine/Audio/Audio.hpp"
#include "Engine/ECS/AnimationEvents.hpp"
#include "Engine/Physics/Cloth.hpp"
#include "Engine/Physics/ClothRenderSystem.hpp"
#include "Engine/Physics/Physics.hpp"
#include "Engine/Core/Log.hpp"
#include "Engine/ECS/Components.hpp"
#include "Engine/ECS/Skeleton.hpp"
#include "Engine/ECS/SkeletonIk.hpp"
#include "Engine/ECS/Lighting.hpp"
#include "Engine/ECS/LightSource.hpp"
#include "Engine/Physics/SdfCollision.hpp"
#include "Engine/ECS/SystemGraph.hpp"
#include "Engine/ECS/SpriteAnimation.hpp"
#include "Engine/ECS/Tilemap.hpp"
#include "Engine/Script/ScriptSystem.hpp"
#include "Engine/RHI/SkinningBridge.hpp"
#include "Editor/Infrastructure/EditorSystem.hpp"
#include "Editor/Infrastructure/FrameComposer.hpp"
#include "Editor/Infrastructure/ProjectConfig.hpp"
#include "Editor/Panels/ProjectDashboard.hpp"
#include "Editor/Panels/SettingsPanels.hpp"
#include "Engine/Core/JobSystem.hpp"
#include "Engine/Core/WindowManager.hpp"
#include "Engine/Core/PluginHost.hpp"
#include "Engine/RHI/RHI.hpp"
#include "Engine/RHI/RenderGraph.hpp"
#include "Engine/UI/AccessibilityOverlay.hpp"
#include "Engine/RHI/RenderPasses.hpp"
#include "Engine/RHI/VirtualTextureFormat.hpp"
#include "Engine/RHI/VirtualTextureGpu.hpp"
#include "Engine/RHI/VirtualTextureSystem.hpp"
#include "Engine/RHI/RenderItem.hpp"
#include "Engine/RHI/Sort.hpp"
#include "Engine/Text/TextFontStack.hpp"
#include "Engine/Text/TextRuntime.hpp"
#include "Engine/UI/PinkUI.hpp"
#include "Engine/ECS/Scene.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace
{
constexpr int kMaxFixedStepsPerFrame = 8;
constexpr float kMaxFrameTime = 0.25f;

// Demonstration: a three-bone chain with a two-bone skinned quad. The right
struct SkeletonDemo
{
    std::int16_t arm = -1;
    std::int16_t hand = -1;
    float time = 0.0f;
};

// State for the game systems. SystemGraph stores `void* user`, so a system that
struct EditorGameSystems
{
    Fby::SpriteAnimationEventQueue spriteAnimationEvents;
    Fby::SkeletonSystem skeletonSystem;
};

void EventsClear(Fby::Ecs::SystemContext&, void* InUser)
{
    static_cast<EditorGameSystems*>(InUser)->spriteAnimationEvents.Clear();
}

void MovementSystem(Fby::Ecs::SystemContext& InContext, void*)
{
    auto ScopedView = InContext.registry.view<Fby::Transform2D, Fby::Velocity>();
    for (auto [ScopedEntity, ScopedTransform, ScopedVelocity] : ScopedView.each())
    {
        (void)ScopedEntity;
        ScopedTransform.Position += ScopedVelocity.Value * InContext.dt;
    }
}

void PhysicsStepSystem(Fby::Ecs::SystemContext& InContext, void*)
{
    if (InContext.physics != nullptr)
    {
        InContext.physics->Step(InContext.registry, InContext.dt);
    }
}

void ContactToSpriteAnimationSystem(Fby::Ecs::SystemContext& InContext, void* InUser)
{
    if (InContext.physics != nullptr)
    {
        Fby::Gameplay::RouteContactEvents(InContext.physics->Contacts(), InContext.registry,
                                          static_cast<EditorGameSystems*>(InUser)->spriteAnimationEvents);
    }
}

void ScriptSystem(Fby::Ecs::SystemContext& InContext, void* InUser)
{
    Fby::FemboyScriptSystem(InContext, *static_cast<Fby::ScriptRuntime*>(InUser));
}

void IkSystem(Fby::Ecs::SystemContext& InContext, void*)
{
    Fby::Skeletal::IkSystem(InContext);
}

// Both lighting solvers write the same result component, so one bridge decides
// which one runs for the current world.
struct LightingBridge
{
    Fby::LightingSystem* Legacy = nullptr;
    Fby::LightSourceSystem* Unified = nullptr;
};

void LightingStepSystem(Fby::Ecs::SystemContext& InContext, void* InUser)
{
    auto* ScopedBridge = static_cast<LightingBridge*>(InUser);
    if (ScopedBridge == nullptr)
    {
        return;
    }
    // A scene migrates from the legacy radial component to unified light
    // sources one entity at a time; the presence of any unified source selects
    // the new solver so the two never write the same result component.
    bool ScopedUnified = false;
    auto ScopedView = InContext.registry.view<Fby::LightSourceComponent>();
    for (const Fby::Entity ScopedEntity : ScopedView)
    {
        (void)ScopedEntity;
        ScopedUnified = true;
        break;
    }
    if (ScopedUnified && ScopedBridge->Unified != nullptr)
    {
        (void)ScopedBridge->Unified->Run(InContext.registry, ScopedBridge->Unified->Settings,
                                         ScopedBridge->Unified->SdfFieldView());
        return;
    }
    if (ScopedBridge->Legacy != nullptr)
    {
        (void)ScopedBridge->Legacy->Run(InContext.registry, ScopedBridge->Legacy->Settings,
                                        ScopedBridge->Legacy->SdfFieldView());
    }
}

void SpriteAnimationStepSystem(Fby::Ecs::SystemContext& InContext, void* InUser)
{
    Fby::SpriteAnimationSystem{}.Run(InContext, static_cast<EditorGameSystems*>(InUser)->spriteAnimationEvents);
}

void AudioUpdateSystem(Fby::Ecs::SystemContext& InContext, void*)
{
    if (InContext.audio != nullptr)
    {
        InContext.audio->Update(InContext.registry);
    }
}

void SkeletalAnimateSystem(Fby::Ecs::SystemContext& InContext, void*)
{
    auto ScopedView = InContext.registry.view<Fby::SkeletonComponent, SkeletonDemo>();
    for (auto [ScopedEntity, ScopedSkeleton, ScopedDemo] : ScopedView.each())
    {
        (void)ScopedEntity;
        ScopedDemo.time += InContext.dt;
        ScopedSkeleton.LocalRotations[ScopedDemo.arm] = std::sin(ScopedDemo.time * 1.4f) * 0.42f;
        ScopedSkeleton.LocalRotations[ScopedDemo.hand] = std::sin(ScopedDemo.time * 2.3f) * 0.75f;
    }
}

void SkeletalEvaluateSystem(Fby::Ecs::SystemContext& InContext, void* InUser)
{
    static_cast<EditorGameSystems*>(InUser)->skeletonSystem.Run(InContext.registry);
}

void PhysicsInterpolateSystem(Fby::Ecs::SystemContext& InContext, void*)
{
    if (InContext.physics != nullptr)
    {
        InContext.physics->Interpolate(InContext.registry, InContext.interpolationAlpha);
    }
}

#if defined(_WIN32)
constexpr const char* kPluginSuffix = ".dll";
constexpr const char* kExecutableSuffix = ".exe";
#elif defined(__APPLE__)
constexpr const char* kPluginSuffix = ".dylib";
constexpr const char* kExecutableSuffix = "";
#else
constexpr const char* kPluginSuffix = ".so";
constexpr const char* kExecutableSuffix = "";
#endif

[[nodiscard]] bool LoadSceneFromVfs(Fby::VirtualFileSystem& InVfs, Fby::Assets::AssetDatabase& InAssetDb,
                                    Fby::Scene::ComponentRegistry& InComponents, Fby::Ecs::Registry& InRegistry,
                                    std::string_view InPath)
{
    const auto ScopedBytes = InVfs.Read(InPath);
    if (!ScopedBytes)
    {
        return false;
    }
    const std::string_view ScopedText(reinterpret_cast<const char*>(ScopedBytes->data()), ScopedBytes->size());
    Fby::Scene::SceneLoadOptions ScopedOptions;
    ScopedOptions.clearExisting = true;
    // Resolve SkeletonAssetRef entries while the scene is applied, so a scene
    ScopedOptions.assets = &InVfs;
    ScopedOptions.assetDb = &InAssetDb;
    auto ScopedResult = Fby::Scene::LoadSceneToml(InRegistry, InComponents, ScopedText, ScopedOptions);
    if (!ScopedResult)
    {
        Fby::LogWarn("editor_app", "scene '{}' rejected: {}", InPath, ScopedResult.error());
        return false;
    }
    Fby::LogInfo("editor_app", "scene '{}' loaded: {} entities", InPath, ScopedResult->Size());
    return true;
}

void SpawnFallbackWorld(Fby::Ecs::Registry& InRegistry)
{
    const Fby::Entity ScopedCamera = InRegistry.create();
    InRegistry.emplace<Fby::Camera2D>(
        ScopedCamera, Fby::Camera2D{Fby::Vector2D{640.0f, 360.0f}, Fby::Vector2D{1280.0f, 720.0f}, 1.0f, 0.0f});
    for (int ScopedI = 0; ScopedI < 24; ++ScopedI)
    {
        const Fby::Entity ScopedEntity = InRegistry.create();
        Fby::Transform2D ScopedTransform;
        ScopedTransform.Position = {160.0f + static_cast<float>((ScopedI % 6) * 160),
                                    120.0f + static_cast<float>((ScopedI / 6) * 140)};
        ScopedTransform.Scale = {2.0f, 2.0f};
        InRegistry.emplace<Fby::Transform2D>(ScopedEntity, ScopedTransform);
        InRegistry.emplace<Fby::Sprite2D>(ScopedEntity, Fby::Sprite2D{Fby::Vector2D{64.0f, 64.0f},
                                                                      {0.0f, 0.0f},
                                                                      {1.0f, 1.0f},
                                                                      static_cast<std::uint32_t>(1 + (ScopedI % 2)),
                                                                      Fby::Color32::FromBytes(255, 180, 220),
                                                                      0});
        InRegistry.emplace<Fby::DrawOrder>(ScopedEntity, Fby::DrawOrder{static_cast<std::uint16_t>(ScopedI % 2), 0.0f});
    }
}

// A four-cell horizontal flipbook cut from texture-array layer 1. The
[[nodiscard]] Fby::Entity SpawnSpriteAnimationDemo(Fby::Ecs::Registry& InRegistry, Fby::Assets::AssetHandle InAtlasHandle)
{
    const Fby::Entity ScopedEntity = InRegistry.create();
    Fby::Transform2D ScopedTransform;
    ScopedTransform.Position = {900.0f, 220.0f};
    InRegistry.emplace<Fby::Transform2D>(ScopedEntity, ScopedTransform);

    Fby::Sprite2D ScopedSprite;
    ScopedSprite.Size = {96.0f, 96.0f};
    ScopedSprite.Layer = 1;
    ScopedSprite.Color = Fby::Color32::FromBytes(255, 255, 255);
    InRegistry.emplace<Fby::Sprite2D>(ScopedEntity, ScopedSprite);
    InRegistry.emplace<Fby::DrawOrder>(ScopedEntity, Fby::DrawOrder{1, 2.0f});

    Fby::SpriteAnimationComponent ScopedSpriteAnimation;
    ScopedSpriteAnimation.TextureAtlas = InAtlasHandle;
    ScopedSpriteAnimation.StartFrame = 0;
    ScopedSpriteAnimation.FrameCount = 4;
    ScopedSpriteAnimation.FrameDuration = 0.12f;
    ScopedSpriteAnimation.IsLooping = true;
    ScopedSpriteAnimation.IsPlaying = true;
    InRegistry.emplace<Fby::SpriteAnimationComponent>(ScopedEntity, ScopedSpriteAnimation);

    Fby::SpriteSheetComponent ScopedGrid;
    ScopedGrid.Columns = 4;
    ScopedGrid.Rows = 1;
    ScopedGrid.Layer = 1;
    InRegistry.emplace<Fby::SpriteSheetComponent>(ScopedEntity, ScopedGrid);
    Fby::ApplySpriteFrame(ScopedSpriteAnimation, &ScopedGrid, InRegistry.get<Fby::Sprite2D>(ScopedEntity));
    return ScopedEntity;
}

[[nodiscard]] Fby::Entity SpawnPhysicsDemo(Fby::Ecs::Registry& InRegistry)
{
    const Fby::Entity ScopedGround = InRegistry.create();
    Fby::Transform2D ScopedGroundTransform;
    ScopedGroundTransform.Position = {640.0f, 700.0f};
    InRegistry.emplace<Fby::Transform2D>(ScopedGround, ScopedGroundTransform);
    Fby::Sprite2D ScopedGroundSprite;
    ScopedGroundSprite.Size = {1280.0f, 40.0f};
    ScopedGroundSprite.Layer = 2;
    ScopedGroundSprite.Color = Fby::Color32::FromBytes(70, 70, 80);
    InRegistry.emplace<Fby::Sprite2D>(ScopedGround, ScopedGroundSprite);
    InRegistry.emplace<Fby::DrawOrder>(ScopedGround, Fby::DrawOrder{1, 0.0f});
    Fby::RigidBody2D ScopedGroundBody;
    ScopedGroundBody.Type = Fby::BodyType::Static;
    InRegistry.emplace<Fby::RigidBody2D>(ScopedGround, ScopedGroundBody);
    Fby::Collider2D ScopedGroundCollider;
    ScopedGroundCollider.HalfExtents = {640.0f, 20.0f};
    InRegistry.emplace<Fby::Collider2D>(ScopedGround, ScopedGroundCollider);

    const Fby::Entity ScopedCrate = InRegistry.create();
    Fby::Transform2D ScopedCrateTransform;
    ScopedCrateTransform.Position = {900.0f, 520.0f};
    InRegistry.emplace<Fby::Transform2D>(ScopedCrate, ScopedCrateTransform);
    Fby::Sprite2D ScopedCrateSprite;
    ScopedCrateSprite.Size = {48.0f, 48.0f};
    ScopedCrateSprite.Layer = 1;
    ScopedCrateSprite.Color = Fby::Color32::FromBytes(210, 120, 160);
    InRegistry.emplace<Fby::Sprite2D>(ScopedCrate, ScopedCrateSprite);
    InRegistry.emplace<Fby::DrawOrder>(ScopedCrate, Fby::DrawOrder{1, 1.0f});
    Fby::RigidBody2D ScopedCrateBody;
    ScopedCrateBody.Type = Fby::BodyType::Dynamic;
    InRegistry.emplace<Fby::RigidBody2D>(ScopedCrate, ScopedCrateBody);
    Fby::Collider2D ScopedCrateCollider;
    ScopedCrateCollider.HalfExtents = {24.0f, 24.0f};
    ScopedCrateCollider.Density = 1.0f;
    ScopedCrateCollider.Friction = 0.5f;
    InRegistry.emplace<Fby::Collider2D>(ScopedCrate, ScopedCrateCollider);

    // Lighting demo: the ground and the crate are lit targets, a warm lamp sits
    // above the crate. Ambient in LightingSettings keeps the rest readable.
    Fby::Light2DTargetComponent ScopedGroundTarget;
    ScopedGroundTarget.BaseColor = Fby::Color32::FromBytes(70, 70, 80);
    InRegistry.emplace<Fby::Light2DTargetComponent>(ScopedGround, ScopedGroundTarget);
    Fby::Light2DTargetComponent ScopedCrateTarget;
    ScopedCrateTarget.BaseColor = Fby::Color32::FromBytes(210, 120, 160);
    InRegistry.emplace<Fby::Light2DTargetComponent>(ScopedCrate, ScopedCrateTarget);
    // The crate is also a dynamic shadow caster: it is lit itself (an entity never
    // shadows itself) and throws a moving shadow onto the ground slab below.
    Fby::Light2DOccluderComponent ScopedCrateOccluder;
    ScopedCrateOccluder.HalfExtents = {24.0f, 24.0f};
    ScopedCrateOccluder.Flags = Fby::kLight2DOccluderFlagBox;
    InRegistry.emplace<Fby::Light2DOccluderComponent>(ScopedCrate, ScopedCrateOccluder);

    const Fby::Entity ScopedLamp = InRegistry.create();
    Fby::Transform2D ScopedLampTransform;
    ScopedLampTransform.Position = {900.0f, 440.0f};
    InRegistry.emplace<Fby::Transform2D>(ScopedLamp, ScopedLampTransform);
    Fby::Light2DComponent ScopedLampLight;
    ScopedLampLight.Color = Fby::Color32::FromBytes(255, 196, 140);
    ScopedLampLight.Radius = 520.0f;
    ScopedLampLight.Intensity = 1.35f;
    InRegistry.emplace<Fby::Light2DComponent>(ScopedLamp, ScopedLampLight);
    // The sun drives the renderer's per-pixel SDF shadows (static field and the
    // moving crate occluder); it is authored like any other component.
    Fby::SdfSunComponent ScopedSun;
    ScopedSun.Direction = {-0.45f, -1.0f};
    ScopedSun.Softness = 18.0f;
    ScopedSun.Ambient = 0.35f;
    ScopedSun.OcclusionStrength = 0.25f;
    InRegistry.emplace<Fby::SdfSunComponent>(ScopedLamp, ScopedSun);

    // Isometric tilemap demo: a small diamond floor with a solid border, so the
    // viewport exercises TilemapRenderSystem's painter order on startup.
    const Fby::Entity ScopedTilemapEntity = InRegistry.create();
    Fby::TilemapComponent ScopedTilemap;
    ScopedTilemap.Grid.Mode = Fby::Grid::ProjectionMode::IsoRect;
    ScopedTilemap.Grid.CellSize = {64.0f, 32.0f};
    ScopedTilemap.Grid.Origin = {640.0f, 420.0f};
    ScopedTilemap.Grid.HeightStep = 8.0f;
    ScopedTilemap.AtlasColumns = 4;
    ScopedTilemap.AtlasRows = 4;
    ScopedTilemap.TextureLayer = 2;
    Fby::Tilemap::Resize(ScopedTilemap, 12, 12);
    for (std::int32_t ScopedY = 0; ScopedY < 12; ++ScopedY)
    {
        for (std::int32_t ScopedX = 0; ScopedX < 12; ++ScopedX)
        {
            const bool ScopedBorder = ScopedX == 0 || ScopedY == 0 || ScopedX == 11 || ScopedY == 11;
            const std::uint32_t ScopedFlags = ScopedBorder ? Fby::kTileFlagSolid : 0u;
            const std::uint16_t ScopedIndex = ScopedBorder ? 2 : ((ScopedX + ScopedY) % 2 == 0 ? 1 : 3);
            (void)Fby::Tilemap::Set(ScopedTilemap, Fby::Grid::Cell{ScopedX, ScopedY},
                                    Fby::MakeTile(ScopedIndex, ScopedFlags));
        }
    }
    InRegistry.emplace<Fby::TilemapComponent>(ScopedTilemapEntity, ScopedTilemap);
    return ScopedCrate;
}

[[nodiscard]] Fby::Entity SpawnSkeletonDemo(Fby::Ecs::Registry& InRegistry)
{
    const Fby::Entity ScopedEntity = InRegistry.create();
    auto& ScopedSkeleton = InRegistry.emplace<Fby::SkeletonComponent>(ScopedEntity);
    Fby::SkeletonReset(ScopedSkeleton);

    const std::int16_t ScopedRoot =
        Fby::SkeletonAddBone(ScopedSkeleton, "root", -1, Fby::Vector2D{640.0f, 360.0f}, 0.0f);
    const std::int16_t ScopedArm =
        Fby::SkeletonAddBone(ScopedSkeleton, "arm", ScopedRoot, Fby::Vector2D{0.0f, 0.0f}, 0.0f);
    const std::int16_t ScopedHand =
        Fby::SkeletonAddBone(ScopedSkeleton, "hand", ScopedArm, Fby::Vector2D{48.0f, 0.0f}, 0.0f);
    Fby::SkeletonBindPose(ScopedSkeleton);

    auto& ScopedSkin = InRegistry.emplace<Fby::SkinComponent>(ScopedEntity);
    ScopedSkin.VertexCount = 4;
    const Fby::Vector2D ScopedCorners[4] = {
        {580.0f, 300.0f},
        {700.0f, 300.0f},
        {700.0f, 420.0f},
        {580.0f, 420.0f},
    };
    for (std::uint32_t ScopedVertex = 0; ScopedVertex < ScopedSkin.VertexCount; ++ScopedVertex)
    {
        ScopedSkin.BindPositions[ScopedVertex] = ScopedCorners[ScopedVertex];
        const bool ScopedRightHalf = ScopedVertex == 1 || ScopedVertex == 2;
        ScopedSkin.BoneIndices[ScopedVertex * Fby::kMaxBoneInfluences] = ScopedRightHalf ? ScopedHand : ScopedArm;
        ScopedSkin.BoneWeights[ScopedVertex * Fby::kMaxBoneInfluences] = 1.0f;
        ScopedSkin.BoneIndices[ScopedVertex * Fby::kMaxBoneInfluences + 1] = ScopedRightHalf ? ScopedArm : ScopedHand;
        ScopedSkin.BoneWeights[ScopedVertex * Fby::kMaxBoneInfluences + 1] = 0.0f;
        ScopedSkin.SkinnedPositions[ScopedVertex] = ScopedCorners[ScopedVertex];
    }

    Fby::SkinnedSprite ScopedSprite;
    // White layer: a solid, clearly visible deformed quad
    ScopedSprite.Layer = 0;
    ScopedSprite.SortLayer = 2;
    ScopedSprite.Color = Fby::Color32::FromBytes(255, 150, 205);
    ScopedSprite.Depth = 1.0f;
    InRegistry.emplace<Fby::SkinnedSprite>(ScopedEntity, ScopedSprite);
    InRegistry.emplace<SkeletonDemo>(ScopedEntity, SkeletonDemo{ScopedArm, ScopedHand, 0.0f});
    return ScopedEntity;
}
}

int main(int argc, char** argv)
{
    Fby::SetLogLevel(Fby::LogLevel::Info);
    Fby::LogInfo("editor_app", "FemboyEngine2D {} editor", FBY_ENGINE_VERSION);

    // A project path on the command line (a plain first argument or
    // `--project <path>`) opens that project directly. Explorer's "Open with"
    // and a .femproj file association pass the file path this way.
    std::filesystem::path ScopedInitialProject;
    for (int ScopedIndex = 1; ScopedIndex < argc; ++ScopedIndex)
    {
        if (argv[ScopedIndex] == nullptr)
        {
            continue;
        }
        const std::string_view ScopedArgument = argv[ScopedIndex];
        if (ScopedArgument == "--project" && ScopedIndex + 1 < argc && argv[ScopedIndex + 1] != nullptr)
        {
            ScopedInitialProject = std::filesystem::path(argv[++ScopedIndex]);
        }
        else if (!ScopedArgument.empty() && ScopedArgument[0] != '-')
        {
            ScopedInitialProject = std::filesystem::path(ScopedArgument);
        }
    }
    if (!ScopedInitialProject.empty())
    {
        Fby::LogInfo("editor_app", "command line project: {}", ScopedInitialProject.string());
    }

    Fby::WindowManager ScopedWindow;
    if (!ScopedWindow.Initialize())
    {
        return 1;
    }
    Fby::WindowDesc ScopedWindowDesc;
    ScopedWindowDesc.title = "FemboyEngine2D 1.0 - editor";
    ScopedWindowDesc.width = 1600;
    ScopedWindowDesc.height = 900;
    // Measurement/debug hook: force a 1600x900 window instead of the borderless
    // fullscreen default (used to read frame times at a fixed resolution).
    if (std::getenv("FBY_EDITOR_WINDOWED") != nullptr)
    {
        ScopedWindowDesc.mode = Fby::WindowMode::Windowed;
    }
    if (!ScopedWindow.Create(ScopedWindowDesc))
    {
        return 1;
    }

    Fby::Rhi::DeviceDesc ScopedDeviceDesc;
    ScopedDeviceDesc.backend = Fby::Rhi::SelectDefaultBackend();
    ScopedDeviceDesc.nativeWindow = ScopedWindow.NativeWindow();
    ScopedDeviceDesc.size = {ScopedWindow.Size().width, ScopedWindow.Size().height};
    ScopedDeviceDesc.vsync = std::getenv("FBY_EDITOR_NOVSYNC") == nullptr;
    ScopedDeviceDesc.enableValidation = std::getenv("FBY_VULKAN_VALIDATION") != nullptr;
    ScopedDeviceDesc.maxItemsPerFrame = 1u << 16;

    std::unique_ptr<Fby::Rhi::RHI> ScopedGpu = Fby::Rhi::RHI::Create(ScopedDeviceDesc);
    if (!ScopedGpu)
    {
        Fby::LogError("editor_app", "no rendering backend could be created");
        return 2;
    }
    Fby::LogInfo("editor_app", "backend: {} ({})", Fby::Rhi::BackendName(ScopedGpu->GetBackend()),
                 ScopedGpu->Info().name);

    // Diagnostic VT demo (FBY_VT_SELFTEST=1): one orange page in layer 0, slot 0,
    // and a page-table cell that makes the world tile under the camera resident.
    // The sprite built below samples it, and a captured viewport pixel proves the
    // whole path (page table -> slot -> page array -> blend) end to end.
    Fby::VirtualFileSystem ScopedVfs;
    std::string ScopedVfsError;
    if (ScopedVfs.MountPak("assets.fempak", 0, &ScopedVfsError))
    {
        Fby::LogInfo("editor_app", "mounted assets.fempak");
    }
    else
    {
        Fby::LogWarn("editor_app", "assets.fempak not mounted: {}", ScopedVfsError);
    }
    // Loose files override the pack: artists and modders iterate without a cook.
    const std::filesystem::path ScopedAssetRoot =
        Fby::Core::FindAssetRoot(argc > 0 && argv[0] != nullptr ? argv[0] : "");
    if (ScopedAssetRoot.empty())
    {
        Fby::LogWarn("editor_app", "assets directory not found; set FBY_ASSET_DIR or run from the repository root");
    }
    else if (!ScopedVfs.MountDirectory(ScopedAssetRoot.string(), 10, &ScopedVfsError))
    {
        Fby::LogWarn("editor_app", "assets directory not mounted at '{}': {}", ScopedAssetRoot.string(),
                     ScopedVfsError);
    }
    else
    {
        Fby::LogInfo("editor_app", "assets: {}", ScopedAssetRoot.string());
    }

    Fby::Assets::TextureArrayAsset ScopedAtlas;
    {
        Fby::LogInfo("editor_app", "loading texture array 'atlas/atlas.toml'");
        std::string ScopedAtlasError;
        if (!ScopedAtlas.Load(ScopedVfs, *ScopedGpu, "atlas/atlas.toml", &ScopedAtlasError))
        {
            Fby::LogWarn("editor_app", "atlas not loaded ({}); using the built-in procedural atlas", ScopedAtlasError);
        }
        else
        {
            Fby::LogInfo("editor_app", "atlas loaded as texture array #{}", ScopedAtlas.Handle().id);
        }
    }
    const std::uint32_t ScopedTextureArray = ScopedAtlas.IsValid() ? ScopedAtlas.Handle().id : 0u;

    Fby::Assets::AssetDatabase ScopedAssetDb;
    const Fby::AssetId ScopedAtlasId =
        ScopedAssetDb.RegisterPath("atlas/main", Fby::AssetKind::TextureArray, ScopedTextureArray);

    Fby::AudioSystem ScopedAudio;
    if (!ScopedAudio.Initialize())
    {
        Fby::LogWarn("editor_app", "audio device unavailable; running silent");
    }
    Fby::PhysicsSystem ScopedPhysics;
    if (!ScopedPhysics.Initialize())
    {
        Fby::LogWarn("editor_app", "Box2D world initialization failed");
    }
    // Cloth integration: the simulation writes a CPU mesh, ClothRenderSystem
    Fby::ClothSystem ScopedCloth;
    if (!ScopedCloth.Initialize(&ScopedPhysics))
    {
        Fby::LogWarn("editor_app", "Verlet cloth initialization failed");
    }
    Fby::ClothRenderSystem ScopedClothRender;
    ScopedClothRender.Initialize(&ScopedCloth);

    // Scripts: the editor world and the PIE clone both run the same VM. The
    // script debugger panel owns a separate runtime so a paused session never
    // stalls gameplay scripts.
    Fby::ScriptRuntime ScopedScripts;
    if (!ScopedScripts.Initialize())
    {
        Fby::LogWarn("editor_app", "FemboyScript runtime initialization failed");
    }
    Fby::RegisterDefaultScriptNatives(ScopedScripts);

    Fby::JobSystem ScopedJobs;
    Fby::Ecs::Registry ScopedRegistry;
    Fby::Scene::ComponentRegistry ScopedSceneComponents;
    Fby::Scene::RegisterBuiltInComponents(ScopedSceneComponents);

    if (!LoadSceneFromVfs(ScopedVfs, ScopedAssetDb, ScopedSceneComponents, ScopedRegistry, "scenes/demo.toml"))
    {
        Fby::LogWarn("editor_app", "demo scene unavailable; spawning the fallback world");
        SpawnFallbackWorld(ScopedRegistry);
    }
    const Fby::Entity ScopedSkeletalAnimationEntity = SpawnSkeletonDemo(ScopedRegistry);
    Fby::LogInfo("editor_app", "Skeleton demo entity {}: 3 bones, 4 skinned vertices",
                 entt::to_integral(ScopedSkeletalAnimationEntity));
    const std::string_view ScopedAtlasPath = ScopedAssetDb.Path(ScopedAtlasId);
    const Fby::Assets::AssetHandle ScopedAtlasHandle =
        ScopedAtlasPath.empty() ? Fby::Assets::AssetHandle{}
                                : ScopedAssetDb.EnsureMeta(ScopedAtlasId, Fby::AssetKind::SpriteSheet, ScopedAtlasPath);
    const Fby::Entity ScopedSpriteAnimationEntity = SpawnSpriteAnimationDemo(ScopedRegistry, ScopedAtlasHandle);
    Fby::LogInfo("editor_app", "SpriteAnimation demo entity {}: 4 cells, 0.12 s/frame", entt::to_integral(ScopedSpriteAnimationEntity));
    const Fby::Entity ScopedPhysicsEntity = SpawnPhysicsDemo(ScopedRegistry);
    Fby::LogInfo("editor_app", "Box2D demo entity {}: dynamic crate over static ground",
                 entt::to_integral(ScopedPhysicsEntity));

    Fby::Ecs::SystemGraph ScopedGraph;
    // Host-owned strict text runtime. The editor tries the same asset TTF stack
    // before falling back to the compatibility shaper.
    Fby::Text::TextRuntime ScopedTextRuntime;
    Fby::Text::TextFontStack ScopedUiFontStack;
    Fby::Text::CodepointShaper ScopedUiFallbackShaper;
    std::string ScopedUiFontError;
    bool ScopedUiFontsReady = false;
    if (ScopedUiFontStack.AddFont(ScopedVfs, "fonts/IBMPlexSans-Regular.ttf", 0, Fby::Text::TextScript::Latin,
                                  32.0f, 1024, false, &ScopedUiFontError))
    {
        ScopedUiFontsReady = ScopedUiFontStack.Fonts().MountToTextureArray(*ScopedGpu);
    }
    if (ScopedUiFontsReady)
    {
        ScopedTextRuntime.SetShaper(&ScopedUiFontStack.Shaper());
    }
    else
    {
        Fby::LogWarn("editor_app", "UI text font stack unavailable ({}); using the codepoint shaper", ScopedUiFontError);
        ScopedTextRuntime.SetShaper(&ScopedUiFallbackShaper);
    }
    Fby::Ecs::SystemContext ScopedContext{ScopedRegistry,  ScopedJobs,    0.0f,       0,
                                          ScopedGpu.get(), &ScopedWindow, &ScopedVfs, &ScopedAssetDb,
                                          &ScopedAudio,    &ScopedPhysics};
    ScopedContext.cloth = &ScopedCloth;
    ScopedContext.text = &ScopedTextRuntime.Service();
    ScopedContext.textShapes = &ScopedTextRuntime.Shapes();
    ScopedContext.textShaper = ScopedTextRuntime.Shaper();
    if (ScopedUiFontsReady)
    {
        ScopedContext.fonts = &ScopedUiFontStack.Fonts();
    }
    EditorGameSystems ScopedGameSystems;

    // The whole frame graph is function-pointer based. Dependencies are data:
    constexpr std::uint64_t kAfterEvents = 1ull << 0;
    constexpr std::uint64_t kAfterMovement = 1ull << 1;
    constexpr std::uint64_t kAfterPhysics = 1ull << 2;
    constexpr std::uint64_t kAfterContacts = 1ull << 3;
    constexpr std::uint64_t kAfterSpriteAnimation = 1ull << 4;
    (void)ScopedGraph.Add(Fby::Ecs::Phase::FixedUpdate, "events_clear", &EventsClear, &ScopedGameSystems);
    (void)ScopedGraph.Add(Fby::Ecs::Phase::FixedUpdate, "movement", &MovementSystem, nullptr, kAfterEvents);
    (void)ScopedGraph.Add(Fby::Ecs::Phase::FixedUpdate, "physics", &PhysicsStepSystem, nullptr, kAfterMovement);
    (void)ScopedGraph.Add(Fby::Ecs::Phase::FixedUpdate, "contact_to_spriteAnimation", &ContactToSpriteAnimationSystem, &ScopedGameSystems,
                          kAfterPhysics);
    (void)ScopedGraph.Add(Fby::Ecs::Phase::FixedUpdate, "spriteAnimation", &SpriteAnimationStepSystem, &ScopedGameSystems, kAfterContacts);
    (void)ScopedGraph.Add(Fby::Ecs::Phase::FixedUpdate, "skeletal_animate", &SkeletalAnimateSystem, nullptr, kAfterSpriteAnimation);
    (void)ScopedGraph.Add(Fby::Ecs::Phase::FixedUpdate, "ik", &IkSystem, nullptr);
    (void)ScopedGraph.Add(Fby::Ecs::Phase::PreUpdate, "audio", &AudioUpdateSystem);
    (void)ScopedGraph.Add(Fby::Ecs::Phase::PostUpdate, "skeletal_evaluate", &SkeletalEvaluateSystem, &ScopedGameSystems);
    (void)ScopedGraph.Add(Fby::Ecs::Phase::PostUpdate, "physics_interpolate", &PhysicsInterpolateSystem);
    Fby::LightingSystem ScopedLighting;
    // A small authored SDF (a plank above the crate and the ground slab) so the
    // demo scene shows the SDF soft-shadow path: the lamp above the plank does
    // not light the crate underneath it. 8 world units per cell, origin (400,
    // 250). The plank sits above the crate sprite's centre, so the crate is not
    // inside the solid itself; the ground slab is below the ground sprite.
    std::array<float, 128 * 96> ScopedSdfSamples{};
    for (std::uint32_t ScopedY = 0; ScopedY < 96; ++ScopedY)
    {
        for (std::uint32_t ScopedX = 0; ScopedX < 128; ++ScopedX)
        {
            const float ScopedWorldX = 400.0f + (static_cast<float>(ScopedX) + 0.5f) * 8.0f;
            const float ScopedWorldY = 250.0f + (static_cast<float>(ScopedY) + 0.5f) * 8.0f;
            const auto ScopedBox = [](float InX, float InY, float InCenterX, float InCenterY, float InHalfX,
                                      float InHalfY)
            {
                const float ScopedQx = std::fabs(InX - InCenterX) - InHalfX;
                const float ScopedQy = std::fabs(InY - InCenterY) - InHalfY;
                const float ScopedOutX = std::max(ScopedQx, 0.0f);
                const float ScopedOutY = std::max(ScopedQy, 0.0f);
                return std::sqrt(ScopedOutX * ScopedOutX + ScopedOutY * ScopedOutY) +
                       std::min(std::max(ScopedQx, ScopedQy), 0.0f);
            };
            const float ScopedCrate = ScopedBox(ScopedWorldX, ScopedWorldY, 900.0f, 478.0f, 44.0f, 8.0f);
            const float ScopedGround = ScopedBox(ScopedWorldX, ScopedWorldY, 640.0f, 730.0f, 640.0f, 16.0f);
            ScopedSdfSamples[static_cast<std::size_t>(ScopedY) * 128 + ScopedX] =
                std::min(ScopedCrate, ScopedGround);
        }
    }
    Fby::SdfField ScopedSdf;
    ScopedSdf.samples = ScopedSdfSamples.data();
    ScopedSdf.width = 128;
    ScopedSdf.height = 96;
    ScopedSdf.origin = Fby::Vector2D{400.0f, 250.0f};
    ScopedSdf.cellSize = Fby::Vector2D{8.0f, 8.0f};
    ScopedSdf.outsideDistance = 1000.0f;
    ScopedLighting.SetSdfField(&ScopedSdf);
    Fby::LightSourceSystem ScopedLightSources;
    ScopedLightSources.SetSdfField(&ScopedSdf);
    ScopedLightSources.Settings.SdfAmbientOcclusion = 0.0f;
    ScopedLightSources.Settings.SdfShadows = false;
    ScopedLightSources.Settings.SdfSoftness = 18.0f;
    LightingBridge ScopedLightingBridge{&ScopedLighting, &ScopedLightSources};
    (void)ScopedLightingBridge;
    // Keep the demo readable: the shadow term is the point, the AO term is mild.
    ScopedLighting.Settings.SdfAmbientOcclusion = 0.15f;
    ScopedLighting.Settings.SdfSoftness = 18.0f;
    // SDF shadows are per-pixel in the renderer now; the CPU pass keeps only the
    // point-light tint so shadows are not applied twice.
    ScopedLighting.Settings.SdfShadows = false;
    ScopedLighting.Settings.SdfAmbientOcclusion = 0.0f;
    (void)ScopedGraph.Add(Fby::Ecs::Phase::PostUpdate, "lighting", &LightingStepSystem, &ScopedLightingBridge);
    (void)Fby::ClothRenderSystem::Register(ScopedGraph, ScopedClothRender);
    (void)ScopedGraph.Add(Fby::Ecs::Phase::FixedUpdate, "scripts", &ScriptSystem, &ScopedScripts, kAfterSpriteAnimation);

    Fby::Editor::EditorSystem ScopedEditor;
    Fby::Editor::EditorDesc ScopedEditorDesc;
    Fby::Core::InstallCrashHandler("fby_editor");
    Fby::SetLogCategory("editor");
    (void)Fby::Core::EnsureDirectory(Fby::Core::ConfigDirectory());
    ScopedEditorDesc.iniPath = Fby::Core::ConfigDirectory() / "editor.ini";
    ScopedEditorDesc.projectRoot = std::filesystem::current_path();
    ScopedEditorDesc.initialProjectPath = ScopedInitialProject;
    ScopedEditorDesc.scenePath = "assets/scenes/editor_scene.toml";
    ScopedEditorDesc.textureArray = ScopedAtlas.Handle();
    ScopedEditorDesc.textureArrayFormat =
        ScopedAtlas.IsValid() ? ScopedAtlas.Description().format : Fby::Assets::TextureFormat::Rgba8;
    ScopedEditorDesc.defaultTextureFormat = Fby::Assets::TextureFormat::Bc7Srgb;
    ScopedEditorDesc.watchAssets = true;
    ScopedEditorDesc.showProjectDashboard = true;
    // Play Multiplayer launches the sandbox from the editor's own directory, so
    // a second copy always matches this build; a missing binary falls back to
    // the local client world.
    const std::filesystem::path ScopedEditorDirectory =
        std::filesystem::path(argv[0]).has_parent_path() ? std::filesystem::path(argv[0]).parent_path()
                                                         : std::filesystem::current_path();
    const std::filesystem::path ScopedSandboxPath =
        ScopedEditorDirectory / (std::string("fby_sandbox") + kExecutableSuffix);
    std::error_code ScopedPathError;
    const std::filesystem::path ScopedAbsoluteSandbox = std::filesystem::absolute(ScopedSandboxPath, ScopedPathError);
    ScopedEditorDesc.pieClientExecutable = ScopedPathError ? ScopedSandboxPath : ScopedAbsoluteSandbox;
    if (!ScopedEditor.Initialize(ScopedContext, ScopedEditorDesc))
    {
        Fby::LogError("editor_app", "editor initialization failed");
        return 3;
    }
    if (!ScopedEditor.Register(ScopedGraph))
    {
        Fby::LogError("editor_app", "SystemGraph rejected the editor Render node");
        return 3;
    }
    Fby::LogInfo("editor_app", "editor registered as a Render-phase SystemGraph node");

    // Automation hook for the editor smoke job: create the project tree when it
    // is missing and open it through the same OpenProject path the dashboard
    // uses, so "create then open" is exercised without a human click.
    if (const char* ScopedSmoke = std::getenv("FBY_EDITOR_SMOKE_PROJECT");
        ScopedSmoke != nullptr && ScopedSmoke[0] != '\0')
    {
        const std::filesystem::path ScopedSmokePath(ScopedSmoke);
        Fby::Editor::ProjectPaths ScopedSmokePaths;
        Fby::Editor::ProjectConfig ScopedSmokeConfig;
        std::string ScopedSmokeError;
        bool ScopedSmokeReady =
            Fby::Editor::LoadProject(ScopedSmokePath, &ScopedSmokePaths, &ScopedSmokeConfig, &ScopedSmokeError);
        if (!ScopedSmokeReady)
        {
            ScopedSmokePaths = Fby::Editor::ProjectPaths{};
            if (!Fby::Editor::CreateProject(ScopedSmokePath.parent_path(), ScopedSmokePath.filename().string(),
                                            &ScopedSmokePaths, &ScopedSmokeConfig, &ScopedSmokeError))
            {
                Fby::LogError("editor_app", "smoke: project creation failed: {}", ScopedSmokeError);
                return 5;
            }
            Fby::LogInfo("editor_app", "smoke: created project {}", ScopedSmokePaths.root.string());
        }
        if (!ScopedEditor.OpenProject(ScopedContext, ScopedSmokePaths, ScopedSmokeConfig, &ScopedSmokeError, true))
        {
            Fby::LogError("editor_app", "smoke: project open failed: {}", ScopedSmokeError);
            return 5;
        }
        Fby::LogInfo("editor_app", "smoke: project opened: {}", ScopedSmokePaths.root.string());
    }

    ScopedEditor.PrintLine(
        std::format("scene: scenes/demo.toml, {} entities", ScopedRegistry.storage<Fby::Entity>().size()));
    ScopedEditor.PrintLine("Skeleton: 3-bone chain, 4 skinned vertices, 2 influences/vertex");

    const auto ScopedCameraView = ScopedRegistry.view<Fby::Camera2D>();
    if (ScopedCameraView.begin() != ScopedCameraView.end())
    {
        const Fby::Camera2D& ScopedCamera = ScopedRegistry.get<Fby::Camera2D>(*ScopedCameraView.begin());
        ScopedEditor.Camera().center = ScopedCamera.Center;
        ScopedEditor.Camera().zoom = ScopedCamera.Zoom > 0.0f ? ScopedCamera.Zoom : 1.0f;
    }

    Fby::Plugin::ComponentRegistry ScopedPluginComponents;
    ScopedPluginComponents.Register<Fby::Transform2D>("Transform2D");
    ScopedPluginComponents.Register<Fby::Velocity>("Velocity");
    ScopedPluginComponents.Register<Fby::Sprite2D>("Sprite2D");
    ScopedPluginComponents.Register<Fby::DrawOrder>("DrawOrder");
    ScopedPluginComponents.Register<Fby::Camera2D>("Camera2D");

    Fby::Plugin::PluginHost ScopedPluginHost(ScopedPluginComponents, &ScopedRegistry);
    ScopedPluginHost.BindSystemGraph(ScopedGraph);
    const std::filesystem::path ScopedExecutableDir = std::filesystem::path(argv[0]).has_parent_path()
                                                          ? std::filesystem::path(argv[0]).parent_path()
                                                          : std::filesystem::path(".");
    const std::filesystem::path ScopedPluginsDir = ScopedExecutableDir / "plugins";
    Fby::LogInfo("editor_app", "plugin directory: {}", ScopedPluginsDir.string());

    {
        std::string ScopedError;
        const std::filesystem::path ScopedGood = ScopedPluginsDir / (std::string("fby_hello_mod") + kPluginSuffix);
        if (!ScopedPluginHost.Load(ScopedGood.string(), &ScopedError))
        {
            Fby::LogWarn("editor_app", "test plugin not loaded: {}", ScopedError);
            ScopedEditor.PrintLine("plugin rejected: " + ScopedError);
        }
        else
        {
            ScopedEditor.PrintLine("plugin loaded: hello_mod (Transform2D, Velocity)");
        }
    }
    {
        // This is the ABI test: the library is valid, but its Transform2D hash
        std::string ScopedError;
        const std::filesystem::path ScopedBad = ScopedPluginsDir / (std::string("fby_hello_mod_bad") + kPluginSuffix);
        if (ScopedPluginHost.Load(ScopedBad.string(), &ScopedError))
        {
            Fby::LogError("editor_app", "BAD PLUGIN WAS ACCEPTED - layout checking is broken");
        }
        else
        {
            Fby::LogInfo("editor_app", "bad-layout plugin rejected as expected: {}", ScopedError);
            ScopedEditor.PrintLine("plugin rejected as expected: " + ScopedError);
        }
    }

    // Plugins registered directly into the graph during Load(). Build once,
    if (!ScopedGraph.Build())
    {
        Fby::LogError("editor_app", "SystemGraph build failed (cycle or phase-crossing dependency)");
        return 4;
    }
    Fby::LogInfo("editor_app", "SystemGraph: {} nodes", ScopedGraph.Count());

    // Diagnostic VT demo (FBY_VT_SELFTEST=1). The layout comes from the
    // project's virtual-texture settings, so structural changes apply on the
    // next launch; budget, prefetch, streaming and debug are applied live below.
    Fby::Rhi::VirtualTextureHandle ScopedVtHandle;
    bool ScopedVtDemo = false;
    Fby::VirtualFileSystem ScopedVtVfs;
    std::unique_ptr<Fby::Rhi::VirtualTextureSystem> ScopedVtSystem;
    std::vector<std::uint32_t> ScopedVtTable;
    Fby::Rhi::VirtualTextureRhiTarget ScopedVtTarget;
    std::uint64_t ScopedVtTotalUploads = 0;
    std::uint64_t ScopedVtTotalEvictions = 0;
    std::uint64_t ScopedVtTotalMisses = 0;
    std::uint32_t ScopedVtDebugMode = 0;
    bool ScopedVtDebugApplied = false;
    const bool ScopedVtStress = std::getenv("FBY_VT_STRESS") != nullptr;
    std::uint32_t ScopedVtTableSize = 64;
    std::uint32_t ScopedVtPageSize = 128;
    std::uint32_t ScopedVtTileSize = 64;
    float ScopedVtWorldTile = 256.0f;
    if (std::getenv("FBY_VT_SELFTEST") != nullptr)
    {
        Fby::Editor::VirtualTextureSettings ScopedVtCfg = ScopedEditor.VirtualTextureConfig();
        // Optional project file for automated checks of the structural settings.
        if (const char* ScopedCfgPath = std::getenv("FBY_VT_CONFIG"); ScopedCfgPath != nullptr)
        {
            Fby::Editor::ProjectPaths ScopedPaths;
            Fby::Editor::ProjectConfig ScopedConfig;
            std::string ScopedCfgError;
            if (Fby::Editor::LoadProject(ScopedCfgPath, &ScopedPaths, &ScopedConfig, &ScopedCfgError))
            {
                ScopedVtCfg = ScopedConfig.project.virtualTexture;
            }
            else
            {
                Fby::LogWarn("editor_app", "VT config '{}': {}", ScopedCfgPath, ScopedCfgError);
            }
        }
        ScopedVtTableSize = ScopedVtCfg.pageTableSize;
        ScopedVtPageSize = ScopedVtCfg.pageSize;
        ScopedVtTileSize = ScopedVtCfg.tileSize;
        ScopedVtWorldTile = ScopedVtCfg.worldTileSize;
        Fby::Rhi::VirtualTextureResourceDesc ScopedVtDesc;
        ScopedVtDesc.layerCount = ScopedVtCfg.layers;
        ScopedVtDesc.pageSize = ScopedVtCfg.pageSize;
        ScopedVtDesc.tileSize = ScopedVtCfg.tileSize;
        ScopedVtDesc.pageTableSize = ScopedVtCfg.pageTableSize;
        ScopedVtHandle = ScopedGpu->CreateVirtualTexture(ScopedVtDesc);

        std::vector<std::uint32_t> ScopedTable(static_cast<std::size_t>(ScopedVtTableSize) * ScopedVtTableSize,
                                               Fby::Rhi::kVirtualTexturePageTableMissing);
        ScopedTable[0] = Fby::Rhi::PackVirtualTexturePageTable(0, 0);
        std::vector<std::byte> ScopedPage(static_cast<std::size_t>(ScopedVtCfg.tileSize) * ScopedVtCfg.tileSize * 4u);
        for (std::size_t ScopedI = 0; ScopedI < ScopedPage.size(); ScopedI += 4)
        {
            ScopedPage[ScopedI + 0] = std::byte{240};
            ScopedPage[ScopedI + 1] = std::byte{96};
            ScopedPage[ScopedI + 2] = std::byte{32};
            ScopedPage[ScopedI + 3] = std::byte{255};
        }
        bool ScopedVtOk =
            ScopedVtHandle.IsValid() &&
            ScopedGpu->UploadVirtualTexturePage(ScopedVtHandle, 0, 0, 0, ScopedVtCfg.tileSize, ScopedVtCfg.tileSize,
                                                ScopedVtCfg.tileSize * 4u, ScopedPage) &&
            ScopedGpu->UpdateVirtualTexturePageTable(ScopedVtHandle, ScopedVtTableSize, ScopedVtTableSize, ScopedTable);

        // Synthetic orange chunks, laid out with the configured tile/chunk size.
        const std::uint32_t ScopedChunkPixels = ScopedVtCfg.tileSize * ScopedVtCfg.chunkTiles;
        const std::filesystem::path ScopedChunkRoot = std::filesystem::current_path() / "build" / "_vt_demo";
        std::error_code ScopedFsError;
        std::filesystem::create_directories(ScopedChunkRoot, ScopedFsError);
        for (std::int64_t ScopedChunkY = -1; ScopedChunkY <= 8; ++ScopedChunkY)
        {
            for (std::int64_t ScopedChunkX = -1; ScopedChunkX <= 8; ++ScopedChunkX)
            {
                std::vector<std::byte> ScopedChunk(32ull + static_cast<std::size_t>(ScopedChunkPixels) * ScopedChunkPixels * 4u);
                std::memcpy(ScopedChunk.data(), Fby::Rhi::kVirtualTextureChunkMagic, 8);
                const std::uint32_t ScopedHeaderValues[6] = {1u, ScopedVtCfg.tileSize, ScopedVtCfg.chunkTiles, 0u, 0u, 0u};
                std::memcpy(ScopedChunk.data() + 8, ScopedHeaderValues, sizeof(ScopedHeaderValues));
                for (std::uint32_t ScopedY = 0; ScopedY < ScopedChunkPixels; ++ScopedY)
                {
                    for (std::uint32_t ScopedX = 0; ScopedX < ScopedChunkPixels; ++ScopedX)
                    {
                        const std::size_t ScopedOffset =
                            32u + (static_cast<std::size_t>(ScopedY) * ScopedChunkPixels + ScopedX) * 4u;
                        ScopedChunk[ScopedOffset + 0] = static_cast<std::byte>(210 + (ScopedX % 40));
                        ScopedChunk[ScopedOffset + 1] = static_cast<std::byte>(70 + (ScopedY % 40));
                        ScopedChunk[ScopedOffset + 2] = std::byte{30};
                        ScopedChunk[ScopedOffset + 3] = std::byte{255};
                    }
                }
                char ScopedChunkName[64]{};
                std::snprintf(ScopedChunkName, sizeof(ScopedChunkName), "chunk_%lld_%lld.fvtc",
                              static_cast<long long>(ScopedChunkX), static_cast<long long>(ScopedChunkY));
                std::ofstream ScopedFile(ScopedChunkRoot / ScopedChunkName, std::ios::binary | std::ios::trunc);
                ScopedFile.write(reinterpret_cast<const char*>(ScopedChunk.data()),
                                 static_cast<std::streamsize>(ScopedChunk.size()));
            }
        }
        std::string ScopedVtVfsError;
        if (ScopedVtOk &&
            !ScopedVtVfs.MountDirectory(ScopedChunkRoot.string(), 10, &ScopedVtVfsError))
        {
            Fby::LogWarn("editor_app", "VT demo chunk mount failed: {}", ScopedVtVfsError);
            ScopedVtOk = false;
        }
        if (ScopedVtOk)
        {
            Fby::Rhi::VirtualTextureDesc ScopedStreamDesc;
            ScopedStreamDesc.layerCount = ScopedVtCfg.layers;
            ScopedStreamDesc.pageSize = ScopedVtCfg.pageSize;
            ScopedStreamDesc.tileSize = ScopedVtCfg.tileSize;
            ScopedStreamDesc.chunkTiles = ScopedVtCfg.chunkTiles;
            ScopedStreamDesc.worldTileSize = ScopedVtCfg.worldTileSize;
            ScopedStreamDesc.prefetchTiles = ScopedVtCfg.prefetchTiles;
            ScopedVtTarget.Renderer = ScopedGpu.get();
            ScopedVtTarget.Handle = ScopedVtHandle;
            ScopedVtTarget.TileSize = ScopedVtCfg.tileSize;
            ScopedVtTarget.TilesPerAxis = ScopedVtCfg.pageSize / ScopedVtCfg.tileSize;
            ScopedVtSystem = std::make_unique<Fby::Rhi::VirtualTextureSystem>();
            std::string ScopedStreamError;
            if (!ScopedVtSystem->Initialize(ScopedStreamDesc, ScopedVtVfs, ".", &Fby::Rhi::UploadVirtualTextureTileToRhi,
                                            &ScopedVtTarget))
            {
                Fby::LogWarn("editor_app", "VT demo streaming init failed: {}", ScopedStreamError);
                ScopedVtSystem.reset();
                ScopedVtOk = false;
            }
            else
            {
                ScopedVtSystem->SetMaxLoadsPerUpdate(std::max(1u, ScopedVtCfg.budgetLoadsPerFrame));
                ScopedVtSystem->SetPrefetchTiles(ScopedVtCfg.prefetchTiles);
                ScopedVtTable.assign(static_cast<std::size_t>(ScopedVtTableSize) * ScopedVtTableSize,
                                     Fby::Rhi::kVirtualTexturePageTableMissing);
                if (const char* ScopedDebug = std::getenv("FBY_VT_DEBUG"); ScopedDebug != nullptr)
                {
                    ScopedVtDebugMode = static_cast<std::uint32_t>(std::clamp(std::atoi(ScopedDebug), 0, 2));
                }
                else
                {
                    ScopedVtDebugMode = ScopedVtCfg.debugMode;
                }
            }
        }
        if (!ScopedVtOk)
        {
            ScopedGpu->DestroyVirtualTexture(ScopedVtHandle);
            ScopedVtHandle = {};
            ScopedVtSystem.reset();
        }
        ScopedVtDemo = ScopedVtOk;
        Fby::LogInfo("editor_app", "VT self-test: {} ({}x{} pages x {}, {}x{} table)", ScopedVtOk ? "ok" : "failed",
                     ScopedVtCfg.pageSize, ScopedVtCfg.pageSize, ScopedVtCfg.layers, ScopedVtTableSize,
                     ScopedVtTableSize);
    }

    Fby::Entity ScopedVtSprite = entt::null;
    if (ScopedVtDemo)
    {
        ScopedVtSprite = ScopedRegistry.create();
        Fby::Transform2D ScopedVtTransform;
        ScopedVtTransform.Position = ScopedEditor.Camera().center;
        ScopedRegistry.emplace<Fby::Transform2D>(ScopedVtSprite, ScopedVtTransform);
        Fby::Sprite2D ScopedVtSpriteComponent;
        ScopedVtSpriteComponent.Size = Fby::Vector2D{384.0f, 384.0f};
        ScopedVtSpriteComponent.Flags = Fby::Rhi::kRenderFlagVirtualTexture;
        ScopedRegistry.emplace<Fby::Sprite2D>(ScopedVtSprite, ScopedVtSpriteComponent);
        ScopedRegistry.emplace<Fby::DrawOrder>(ScopedVtSprite, Fby::DrawOrder{9, 0.0f});
    }

    Fby::LogInfo("editor_app", "entering main loop");
    Fby::Core::Clock ScopedClock;
    float ScopedAccumulator = 0.0f;
    std::uint64_t ScopedFrameIndex = 0;
    bool ScopedSdfLogged = false;
    bool ScopedSdfMaskLogged = false;
    bool ScopedVtChecked = !ScopedVtDemo;
    std::vector<std::byte> ScopedVtCapture;
    Fby::Rhi::RenderGraph ScopedShadowGraph;
    Fby::Rhi::SdfShadowPass ScopedShadowPass;
    Fby::Editor::FrameComposer ScopedExtract;
    Fby::Editor::FrameComposer ScopedPieExtract;
    // PIE is the shipped frame in the editor: captions compose into it and the
    // accessibility profile is applied exactly like a game applies it.
    Fby::AccessibilityOverlay ScopedPieOverlay;
    std::vector<Fby::Rhi::RenderItem> ScopedPieItems;
    std::vector<Fby::Rhi::RenderKey> ScopedPieKeys;
    std::vector<Fby::Rhi::DrawBatch> ScopedPieBatches;
    Fby::TilemapRenderSystem ScopedTilemapRender;
    Fby::TilemapRenderSystem ScopedPieTilemapRender;
    double ScopedStatsTimer = 0.0;
    std::uint32_t ScopedStatsFrames = 0;
    // Automation hook: run a fixed number of frames and shut down, so a smoke
    // job can time startup and teardown without a human closing the window.
    std::uint64_t ScopedAutoQuitFrames = 0;
    if (const char* ScopedFrames = std::getenv("FBY_EDITOR_QUIT_AFTER_FRAMES"); ScopedFrames != nullptr)
    {
        ScopedAutoQuitFrames = static_cast<std::uint64_t>(std::strtoull(ScopedFrames, nullptr, 10));
    }
    // Automation hook: click "Open" on the dashboard once, from the main loop.
    const char* ScopedSmokeOpenPath = std::getenv("FBY_EDITOR_SMOKE_DASHBOARD_OPEN");
    std::uint64_t ScopedSmokeOpenFrame = 600;
    if (const char* ScopedOpenFrame = std::getenv("FBY_EDITOR_SMOKE_DASHBOARD_FRAME"); ScopedOpenFrame != nullptr)
    {
        ScopedSmokeOpenFrame = static_cast<std::uint64_t>(std::strtoull(ScopedOpenFrame, nullptr, 10));
    }
    bool ScopedSmokeOpenDone = false;
    bool ScopedCloseHandled = false;

    while (!ScopedEditor.ShouldQuit() && ScopedWindow.PumpEvents())
    {
        // The window close button goes through the editor so an unsaved scene
        // can ask Save/Discard/Cancel instead of disappearing silently.
        if (ScopedWindow.ShouldClose() && !ScopedCloseHandled)
        {
            ScopedCloseHandled = true;
            ScopedEditor.RequestQuit();
        }
        ScopedClock.Tick();
        float ScopedDt = static_cast<float>(ScopedClock.Delta());
        ScopedDt = std::min(ScopedDt, kMaxFrameTime);

        // Escape is no longer an application shortcut: it used to close the
        // whole editor and could lose unsaved work. Quitting is File > Quit.
        // F11 toggles borderless fullscreen. The dashboard and a project window
        // both honour it, so the editor is never stuck at the authored size.
        if (ScopedWindow.Input().WasPressed(Fby::Key::F11) && !ScopedEditor.WantsKeyboard())
        {
            const Fby::WindowMode ScopedNextMode =
                ScopedWindow.Mode() == Fby::WindowMode::BorderlessFullscreen ? Fby::WindowMode::Windowed
                                                                             : Fby::WindowMode::BorderlessFullscreen;
            if (!ScopedWindow.SetWindowMode(ScopedNextMode))
            {
                Fby::LogWarn("editor_app", "could not toggle the window mode");
            }
        }

        if (!ScopedEditor.WantsKeyboard())
        {
            const Fby::InputState& ScopedInput = ScopedWindow.Input();
            const float ScopedPanSpeed = 900.0f / std::max(ScopedEditor.Camera().zoom, 0.05f) * ScopedDt *
                                         std::max(ScopedEditor.Camera().panSpeed, 0.01f);
            if (ScopedInput.IsDown(Fby::Key::Left))
            {
                ScopedEditor.Camera().center.X -= ScopedPanSpeed;
            }
            if (ScopedInput.IsDown(Fby::Key::Right))
            {
                ScopedEditor.Camera().center.X += ScopedPanSpeed;
            }
            if (ScopedInput.IsDown(Fby::Key::Up))
            {
                ScopedEditor.Camera().center.Y -= ScopedPanSpeed;
            }
            if (ScopedInput.IsDown(Fby::Key::Down))
            {
                ScopedEditor.Camera().center.Y += ScopedPanSpeed;
            }
        }

        const Fby::WindowExtent ScopedSize = ScopedWindow.Size();
        if (ScopedSize.width > 0 && ScopedSize.height > 0 &&
            (ScopedSize.width != ScopedDeviceDesc.size.width || ScopedSize.height != ScopedDeviceDesc.size.height))
        {
            ScopedDeviceDesc.size = {ScopedSize.width, ScopedSize.height};
            ScopedGpu->Resize(ScopedDeviceDesc.size);
        }

        ++ScopedFrameIndex;
        ScopedContext.frameIndex = ScopedFrameIndex;
        ScopedContext.dt = ScopedDt;
        ScopedGraph.Run(Fby::Ecs::Phase::PreUpdate, ScopedContext);

        // Project Settings owns the FixedUpdate rate. Reading it every frame
        const float ScopedFixedUpdateHz =
            std::clamp(ScopedEditor.ProjectSettings().Values().fixedUpdateHz, 15.0f, 240.0f);
        const float ScopedFixedStep = 1.0f / ScopedFixedUpdateHz;
        ScopedAccumulator += ScopedDt;
        int ScopedSteps = 0;
        while (ScopedAccumulator >= ScopedFixedStep && ScopedSteps < kMaxFixedStepsPerFrame)
        {
            ScopedContext.dt = ScopedFixedStep;
            ScopedGraph.Run(Fby::Ecs::Phase::FixedUpdate, ScopedContext);
            ScopedAccumulator -= ScopedFixedStep;
            ++ScopedSteps;
        }
        if (ScopedAccumulator >= ScopedFixedStep)
        {
            ScopedAccumulator = std::fmod(ScopedAccumulator, ScopedFixedStep);
        }

        ScopedContext.interpolationAlpha = std::clamp(ScopedAccumulator / ScopedFixedStep, 0.0f, 1.0f);
        ScopedContext.dt = ScopedDt;
        ScopedGraph.Run(Fby::Ecs::Phase::PostUpdate, ScopedContext);
        ScopedGraph.Run(Fby::Ecs::Phase::Render, ScopedContext);

        const Fby::Rhi::Extent2D ScopedViewport =
            ScopedEditor.ViewportSize().width > 0
                ? ScopedEditor.ViewportSize()
                : Fby::Rhi::Extent2D{ScopedDeviceDesc.size.width, ScopedDeviceDesc.size.height};
        const Fby::Rhi::CameraData ScopedCamera{
            ScopedEditor.Camera().center,
            {static_cast<float>(ScopedViewport.width), static_cast<float>(ScopedViewport.height)},
            ScopedEditor.Camera().zoom};
        // Sample the PIE clone while playing, the authoring world otherwise.
        Fby::Ecs::Registry* ScopedPlayWorld = ScopedEditor.ActiveWorld();
        Fby::Ecs::Registry& ScopedRenderWorld = ScopedPlayWorld != nullptr ? *ScopedPlayWorld : ScopedRegistry;
        const float ScopedZoom = std::max(ScopedEditor.Camera().zoom, 0.05f);
        const Fby::Vector2D ScopedViewHalf{
            static_cast<float>(ScopedViewport.width) * 0.5f / ScopedZoom,
            static_cast<float>(ScopedViewport.height) * 0.5f / ScopedZoom};
        const Fby::Vector2D ScopedViewMin = ScopedEditor.Camera().center - ScopedViewHalf;
        const Fby::Vector2D ScopedViewMax = ScopedEditor.Camera().center + ScopedViewHalf;
        Fby::Rhi::SdfLightingFrame ScopedSdfFrame;
        ScopedTilemapRender.Collect(ScopedRenderWorld, ScopedViewMin, ScopedViewMax);
        ScopedExtract.SetViewportMode(static_cast<Fby::Rhi::ViewportMode>(
            Fby::Editor::ClampViewportViewMode(ScopedEditor.Config().Editor().viewportViewMode)));
        ScopedExtract.SetSdfField(&ScopedSdf);
        ScopedExtract.BuildFramePacket(ScopedRenderWorld, ScopedCamera, Fby::Color32::FromBytes(12, 14, 22),
                                       ScopedTextureArray, ScopedClothRender.Items(),
                                       ScopedTilemapRender.Quads());
        if (ScopedVtDemo)
        {
            const Fby::Vector2D ScopedVtCenter = ScopedEditor.Camera().center;
            if (ScopedVtSystem != nullptr)
            {
                ScopedEditor.SetVirtualTextureSource(ScopedVtSystem.get());
                if (!ScopedVtDebugApplied)
                {
                    ScopedEditor.SetVirtualTextureDebugMode(ScopedVtDebugMode);
                    ScopedVtDebugApplied = true;
                }
                const Fby::Editor::VirtualTextureSettings& ScopedVtRuntime = ScopedEditor.VirtualTextureConfig();
                ScopedVtSystem->SetMaxLoadsPerUpdate(std::max(1u, ScopedVtRuntime.budgetLoadsPerFrame));
                ScopedVtSystem->SetPrefetchTiles(ScopedVtRuntime.prefetchTiles);
                // Feedback -> requests -> VFS chunk -> page upload -> page table.
                ScopedVtSystem->BeginFrame();
                ScopedVtSystem->SetFocus(ScopedVtCenter);
                std::array<std::uint32_t, 256> ScopedFeedback{};
                std::array<Fby::Rhi::VirtualTextureTileId, 256> ScopedRequests{};
                std::uint32_t ScopedRequestCount = 0;
                const std::uint32_t ScopedFeedbackCount =
                    ScopedGpu->ReadVirtualTextureFeedback(ScopedFeedback);
                for (std::uint32_t ScopedI = 0; ScopedI < ScopedFeedbackCount && ScopedRequestCount < ScopedRequests.size();
                     ++ScopedI)
                {
                    std::int32_t ScopedTileX = 0;
                    std::int32_t ScopedTileY = 0;
                    if (Fby::Rhi::UnpackVirtualTextureFeedback(ScopedFeedback[ScopedI], &ScopedTileX, &ScopedTileY))
                    {
                        ScopedRequests[ScopedRequestCount++] = Fby::Rhi::VirtualTextureTileId{ScopedTileX, ScopedTileY};
                    }
                }
                if (ScopedFrameIndex < 2)
                {
                    const std::int32_t ScopedTileX =
                        static_cast<std::int32_t>(std::floor(ScopedVtCenter.X / ScopedVtWorldTile));
                    const std::int32_t ScopedTileY =
                        static_cast<std::int32_t>(std::floor(ScopedVtCenter.Y / ScopedVtWorldTile));
                    for (std::int32_t ScopedY = -1; ScopedY <= 2; ++ScopedY)
                    {
                        for (std::int32_t ScopedX = -1; ScopedX <= 2; ++ScopedX)
                        {
                            if (ScopedRequestCount < ScopedRequests.size())
                            {
                                ScopedRequests[ScopedRequestCount++] =
                                    Fby::Rhi::VirtualTextureTileId{ScopedTileX + ScopedX, ScopedTileY + ScopedY};
                            }
                        }
                    }
                }
                if (ScopedVtRuntime.streamingEnabled || ScopedVtStress)
                {
                    ScopedVtSystem->RequestTiles(
                        std::span<const Fby::Rhi::VirtualTextureTileId>(ScopedRequests.data(), ScopedRequestCount));
                }

                bool ScopedTableChanged = false;
                for (const Fby::Rhi::VirtualTextureTableUpdate& ScopedUpdate : ScopedVtSystem->PendingTableUpdates())
                {
                    const std::int32_t ScopedCellX = ScopedUpdate.TileX;
                    const std::int32_t ScopedCellY = ScopedUpdate.TileY;
                    if (ScopedCellX < 0 || ScopedCellY < 0 ||
                        ScopedCellX >= static_cast<std::int32_t>(ScopedVtTableSize) ||
                        ScopedCellY >= static_cast<std::int32_t>(ScopedVtTableSize))
                    {
                        continue;
                    }
                    ScopedVtTable[static_cast<std::size_t>(ScopedCellY) * ScopedVtTableSize +
                                  static_cast<std::size_t>(ScopedCellX)] = ScopedUpdate.Entry;
                    ScopedTableChanged = true;
                }
                ScopedVtSystem->ClearPendingTableUpdates();
                if (ScopedTableChanged)
                {
                    (void)ScopedGpu->UpdateVirtualTexturePageTable(ScopedVtHandle, ScopedVtTableSize, ScopedVtTableSize,
                                                                   ScopedVtTable);
                }
                if (ScopedVtStress)
                {
                    // Stress: two cameras ping-pong while the zoom sweeps.
                    const bool ScopedSide = (ScopedFrameIndex / 45) % 2 == 0;
                    ScopedEditor.Camera().center = ScopedSide ? Fby::Vector2D{1.5f * 256.0f, 1.5f * 256.0f}
                                                              : Fby::Vector2D{6.5f * 256.0f, 1.5f * 256.0f};
                    ScopedEditor.Camera().zoom =
                        0.4f + 0.9f * (0.5f + 0.5f * std::sin(static_cast<float>(ScopedFrameIndex) * 0.15f));
                }
                else if (ScopedFrameIndex > 60)
                {
                    // Pan the demo camera so pages stream in and out of the pool.
                    ScopedEditor.Camera().center.X += ScopedDt * 160.0f;
                    if (ScopedEditor.Camera().center.X > 8.0f * 256.0f)
                    {
                        ScopedEditor.Camera().center.X = 0.5f * 256.0f;
                    }
                }
                const Fby::Rhi::VirtualTextureStats& ScopedVtStats = ScopedVtSystem->Stats();
                ScopedVtTotalUploads += ScopedVtStats.uploads;
                ScopedVtTotalEvictions += ScopedVtStats.evictions;
                ScopedVtTotalMisses += ScopedVtStats.vfsMisses;
                if (ScopedFrameIndex % 120 == 0)
                {
                    Fby::LogInfo("editor_app",
                                 "VT stream: resident={} total uploads={} evictions={} misses={} deferred={} fps={:.1f}",
                                 ScopedVtStats.residentTiles, ScopedVtTotalUploads, ScopedVtTotalEvictions,
                                 ScopedVtTotalMisses, ScopedVtStats.spills,
                                 1.0f / std::max(static_cast<float>(ScopedDt), 1.0e-6f));
                }
            }
            // Keep the demo quad under the camera and make the tile under the
            // camera the resident page-table cell.
            if (ScopedRegistry.valid(ScopedVtSprite))
            {
                ScopedRegistry.get<Fby::Transform2D>(ScopedVtSprite).Position = ScopedVtCenter;
            }
            const float ScopedVtTileWorld = ScopedVtWorldTile;
            ScopedExtract.SetVirtualTexture(
                ScopedVtHandle,
                Fby::Vector4D{ScopedVtTileWorld, 0.0f, 0.0f, 1.0f},
                Fby::Vector4D{static_cast<float>(ScopedVtTileSize), static_cast<float>(ScopedVtPageSize),
                              static_cast<float>(ScopedVtTableSize), static_cast<float>(ScopedVtTableSize)});
            ScopedExtract.SetVirtualTextureDebugMode(ScopedEditor.VirtualTextureDebugMode());
        }

        // Per-pixel SDF shadows: the static field is uploaded once, dynamic
        // casters are gathered from the render world every frame.
        {
            if (Fby::BuildSdfLightingFrame(ScopedRenderWorld, &ScopedSdf, &ScopedSdfFrame))
            {
                ScopedExtract.SetSdfLighting(ScopedSdfFrame);
                if (!ScopedSdfLogged)
                {
                    ScopedSdfLogged = true;
                    Fby::LogInfo("editor_app", "SDF shadows: {}x{} field, {} dynamic occluders, {} steps",
                                 ScopedSdfFrame.width, ScopedSdfFrame.height, ScopedSdfFrame.occluderCount,
                                 ScopedSdfFrame.steps);
                }
            }
        }

        if (ScopedGpu->BeginFrame())
        {
            // The SDF shadow mask is a render-graph compute pass: the graph owns
            // the ordering, the backend records the dispatch into this frame's
            // command buffer before the sprite pass samples it.
            ScopedShadowGraph.Clear();
            Fby::Rhi::SdfShadowDesc ScopedShadowDesc;
            ScopedShadowDesc.lightDirection = ScopedSdfFrame.sunDirection;
            ScopedShadowDesc.softness = ScopedSdfFrame.softness;
            ScopedShadowDesc.ambient = ScopedSdfFrame.ambient;
            ScopedShadowDesc.steps = ScopedSdfFrame.steps;
            ScopedShadowDesc.fieldOrigin = ScopedSdfFrame.origin;
            ScopedShadowDesc.fieldCellSize = ScopedSdfFrame.cellSize;
            ScopedShadowDesc.fieldWidth = ScopedSdfFrame.width;
            ScopedShadowDesc.fieldHeight = ScopedSdfFrame.height;
            ScopedShadowDesc.resolution = Fby::Rhi::Extent2D{ScopedViewport.width / 2u, ScopedViewport.height / 2u};
            ScopedShadowPass.Initialize(ScopedShadowDesc);
            if (ScopedSdfFrame.enabled && ScopedShadowPass.Build(ScopedShadowGraph) && ScopedShadowGraph.Compile())
            {
                std::array<Fby::Rhi::RenderCommandRecord, 8> ScopedRecords{};
                std::uint32_t ScopedRecordCount = 0;
                Fby::Rhi::RenderGraphContext ScopedGraphContext;
                ScopedGraphContext.renderer = ScopedGpu.get();
                ScopedGraphContext.frame = &ScopedExtract.Packet();
                ScopedGraphContext.extent = ScopedViewport;
                ScopedGraphContext.frameIndex = ScopedFrameIndex;
                ScopedGraphContext.records = ScopedRecords;
                ScopedGraphContext.recordCount = &ScopedRecordCount;
                ScopedShadowGraph.Execute(ScopedGraphContext);
                for (std::uint32_t ScopedI = 0; ScopedI < ScopedRecordCount; ++ScopedI)
                {
                    if (ScopedRecords[ScopedI].kind == Fby::Rhi::RenderPassKind::Compute)
                    {
                        if (ScopedGpu->RecordComputePass(ScopedRecords[ScopedI].pipelineName,
                                                         ScopedRecords[ScopedI].constants,
                                                         ScopedRecords[ScopedI].constantsBytes, ScopedViewport) &&
                            !ScopedSdfMaskLogged)
                        {
                            ScopedSdfMaskLogged = true;
                            Fby::LogInfo("editor_app", "SDF shadow mask recorded via RenderGraph ({})",
                                         ScopedRecords[ScopedI].pipelineName);
                        }
                    }
                }
            }

            const Fby::Rhi::RenderTargetHandle ScopedTarget = ScopedEditor.ViewportTarget();
            // PIE multiplayer presents a second world. The client packet is
            // recorded before the editor target so the main viewport stays the
            // last writer of the shared virtual-texture feedback image.
            Fby::Ecs::Registry* ScopedPieWorld = ScopedEditor.PieClientWorld();
            const Fby::Rhi::RenderTargetHandle ScopedPieTarget = ScopedEditor.PieClientTarget();
            if (ScopedPieTarget.IsValid() && ScopedPieWorld != nullptr)
            {
                const Fby::Editor::EditorCamera& ScopedPieState = ScopedEditor.PieClientCamera();
                const Fby::Rhi::Extent2D ScopedPieViewport = ScopedEditor.PieClientSize();
                const float ScopedPieZoom = std::max(ScopedPieState.zoom, 0.05f);
                const Fby::Vector2D ScopedPieHalf{static_cast<float>(ScopedPieViewport.width) * 0.5f / ScopedPieZoom,
                                                  static_cast<float>(ScopedPieViewport.height) * 0.5f / ScopedPieZoom};
                ScopedPieTilemapRender.Collect(*ScopedPieWorld, ScopedPieState.center - ScopedPieHalf,
                                               ScopedPieState.center + ScopedPieHalf);
                const Fby::Rhi::CameraData ScopedPieCamera{
                    ScopedPieState.center,
                    {static_cast<float>(ScopedPieViewport.width), static_cast<float>(ScopedPieViewport.height)},
                    ScopedPieState.zoom};
                ScopedPieExtract.BuildFramePacket(*ScopedPieWorld, ScopedPieCamera,
                                                  Fby::Color32::FromBytes(12, 14, 22), ScopedTextureArray, {},
                                                  ScopedPieTilemapRender.Quads());
                // The PIE viewport is the shipped frame: it gets the project
                // accessibility profile, while the authoring viewport keeps its
                // own editor-window policy.
                Fby::Rhi::FramePacket ScopedPiePacket = ScopedPieExtract.Packet();
                // The shipped viewport inherits the virtual-texturing binding the
                // authoring pass selected. Without this the PIE packet kept the
                // default handle and VT sprites sampled the fallback page.
                const Fby::Rhi::FramePacket& ScopedEditorPacket = ScopedExtract.Packet();
                ScopedPiePacket.virtualTexture = ScopedEditorPacket.virtualTexture;
                ScopedPiePacket.virtualTextureParams = ScopedEditorPacket.virtualTextureParams;
                ScopedPiePacket.virtualTextureLayout = ScopedEditorPacket.virtualTextureLayout;
                ScopedPiePacket.virtualTextureDebugMode = ScopedEditorPacket.virtualTextureDebugMode;
                if (ScopedContext.accessibility != nullptr)
                {
                    Fby::AccessibilityKernel& ScopedKernel = *ScopedContext.accessibility;
                    ScopedKernel.AdvanceCaptionClock(ScopedDt);
                    (void)ScopedPieOverlay.Build(
                        ScopedContext, ScopedKernel,
                        Fby::Vector2D{static_cast<float>(ScopedPieViewport.width),
                                      static_cast<float>(ScopedPieViewport.height)});
                    ScopedPieItems.resize(ScopedPiePacket.items.size() +
                                          Fby::AccessibilityOverlay::kMaxOverlayItems);
                    ScopedPieKeys.resize(ScopedPiePacket.keys.size() +
                                         Fby::AccessibilityOverlay::kMaxOverlayItems);
                    ScopedPieBatches.resize(ScopedPiePacket.batches.size() +
                                            Fby::AccessibilityOverlay::kMaxOverlayBatches);
                    (void)ScopedPieOverlay.Compose(ScopedPiePacket, ScopedPieItems, ScopedPieKeys,
                                                   ScopedPieBatches);
                    // The offscreen target path runs the GPU composite pass, so the
                    // colour transform travels as packet flags only.
                    ScopedKernel.ApplyToFrame(ScopedPiePacket);
                }
                (void)ScopedGpu->RenderToTarget(ScopedPieTarget, ScopedPiePacket);
            }
            if (ScopedTarget.IsValid())
            {
                (void)ScopedGpu->RenderToTarget(ScopedTarget, ScopedExtract.Packet());
            }

            // The swapchain pass is UI-only: RenderToTarget already drew the
            Fby::Rhi::FramePacket ScopedMainPass;
            ScopedMainPass.clear = Fby::Color32::FromBytes(18, 19, 26);
            ScopedGpu->Submit(ScopedMainPass);
            ScopedGpu->EndFrame();

            if (ScopedVtDemo && !ScopedVtChecked && ScopedFrameIndex > 30 &&
                ScopedEditor.ViewportTarget().IsValid())
            {
                ScopedVtChecked = true;
                const Fby::Rhi::Extent2D ScopedVtExtent = ScopedEditor.ViewportSize();
                if (ScopedVtExtent.width > 0 && ScopedVtExtent.height > 0)
                {
                    ScopedVtCapture.resize(static_cast<std::size_t>(ScopedVtExtent.width) * ScopedVtExtent.height * 4u);
                    Fby::Rhi::Extent2D ScopedCaptured{};
                    if (ScopedGpu->CaptureRenderTarget(ScopedEditor.ViewportTarget(), ScopedVtCapture, &ScopedCaptured) &&
                        ScopedCaptured.width > 1 && ScopedCaptured.height > 1)
                    {
                        // The demo has a skinned sprite over the camera centre, so
                        // probe four points inside the 384x384 VT quad and accept
                        // the first one that shows the page's orange hue.
                        const std::int32_t ScopedCenterX = static_cast<std::int32_t>(ScopedCaptured.width / 2u);
                        const std::int32_t ScopedCenterY = static_cast<std::int32_t>(ScopedCaptured.height / 2u);
                        const std::array<std::array<std::int32_t, 2>, 4> ScopedProbes{{
                            {{0, 120}},
                            {{120, 0}},
                            {{-120, 0}},
                            {{-120, -120}},
                        }};
                        bool ScopedPixelOk = false;
                        for (const auto& ScopedProbe : ScopedProbes)
                        {
                            const std::int32_t ScopedX =
                                std::clamp(ScopedCenterX + ScopedProbe[0], 0,
                                           static_cast<std::int32_t>(ScopedCaptured.width) - 1);
                            const std::int32_t ScopedY =
                                std::clamp(ScopedCenterY + ScopedProbe[1], 0,
                                           static_cast<std::int32_t>(ScopedCaptured.height) - 1);
                            const std::size_t ScopedIndex =
                                (static_cast<std::size_t>(ScopedY) * ScopedCaptured.width +
                                 static_cast<std::size_t>(ScopedX)) * 4u;
                            const auto ScopedRed = static_cast<int>(ScopedVtCapture[ScopedIndex + 0]);
                            const auto ScopedGreen = static_cast<int>(ScopedVtCapture[ScopedIndex + 1]);
                            const auto ScopedBlue = static_cast<int>(ScopedVtCapture[ScopedIndex + 2]);
                            const bool ScopedProbeOk = ScopedRed > 40 && ScopedRed > ScopedGreen * 3 / 2 &&
                                                       ScopedRed > ScopedBlue * 2;
                            ScopedPixelOk = ScopedPixelOk || ScopedProbeOk;
                            Fby::LogInfo("editor_app", "VT sprite probe ({}, {}): r={} g={} b={} ({})",
                                         ScopedProbe[0], ScopedProbe[1], ScopedRed, ScopedGreen, ScopedBlue,
                                         ScopedProbeOk ? "ok" : "other");
                        }
                        Fby::LogInfo("editor_app", "VT sprite visual check: {} (items={})",
                                     ScopedPixelOk ? "ok" : "failed", ScopedExtract.Packet().items.size());
                        std::array<std::uint32_t, 64> ScopedFeedbackTiles{};
                        const std::uint32_t ScopedFeedbackCount =
                            ScopedGpu->ReadVirtualTextureFeedback(ScopedFeedbackTiles);
                        std::int32_t ScopedFeedbackX = 0;
                        std::int32_t ScopedFeedbackY = 0;
                        if (ScopedFeedbackCount > 0)
                        {
                            (void)Fby::Rhi::UnpackVirtualTextureFeedback(ScopedFeedbackTiles[0], &ScopedFeedbackX,
                                                                        &ScopedFeedbackY);
                        }
                        Fby::LogInfo("editor_app", "VT feedback: {} unique tiles (first {},{})",
                                     ScopedFeedbackCount, ScopedFeedbackX, ScopedFeedbackY);
                    }
                }
            }

        }

        if (ScopedSmokeOpenPath != nullptr && ScopedSmokeOpenPath[0] != '\0' && !ScopedSmokeOpenDone &&
            ScopedFrameIndex >= ScopedSmokeOpenFrame)
        {
            ScopedSmokeOpenDone = true;
            Fby::LogInfo("editor_app", "smoke: dashboard open requested: {}", ScopedSmokeOpenPath);
            ScopedEditor.Dashboard().RequestOpen(ScopedSmokeOpenPath);
        }

        ScopedStatsTimer += ScopedDt;
        ++ScopedStatsFrames;
        if (ScopedStatsTimer >= 0.5)
        {
            const double ScopedFps = ScopedStatsFrames / ScopedStatsTimer;
            if (std::getenv("FBY_EDITOR_STATS") != nullptr)
            {
                Fby::LogInfo("editor_app", "fps={:.1f}", ScopedFps);
            }
            ScopedStatsTimer = 0.0;
            ScopedStatsFrames = 0;
        }
        if (ScopedAutoQuitFrames != 0 && ScopedFrameIndex >= ScopedAutoQuitFrames)
        {
            Fby::LogInfo("editor_app", "auto-quit after {} frames", ScopedFrameIndex);
            break;
        }
    }

    // Teardown timings make a slow or stuck exit diagnosable from the log: the
    // last "shutdown: ..." line names the phase that is still running.
    std::uint64_t ScopedPhaseStart = Fby::Core::Clock::NowNanoseconds();
    const std::uint64_t ScopedShutdownStart = ScopedPhaseStart;
    const auto LogShutdownPhase = [&ScopedPhaseStart](const char* InPhase)
    {
        const std::uint64_t ScopedNow = Fby::Core::Clock::NowNanoseconds();
        Fby::LogInfo("editor_app", "shutdown: {} {:.1f} ms", InPhase,
                     static_cast<double>(ScopedNow - ScopedPhaseStart) / 1.0e6);
        ScopedPhaseStart = ScopedNow;
    };

    ScopedGpu->WaitIdle();
    LogShutdownPhase("gpu idle");
    ScopedEditor.Shutdown();
    LogShutdownPhase("editor");
    ScopedAudio.Shutdown();
    LogShutdownPhase("audio");
    ScopedCloth.Shutdown(ScopedRegistry);
    ScopedPhysics.Shutdown();
    LogShutdownPhase("simulation");
    ScopedPluginHost.UnloadAll();
    LogShutdownPhase("plugins");
    if (ScopedAtlas.IsValid())
    {
        ScopedAtlas.Release(*ScopedGpu);
    }
    ScopedGpu->Shutdown();
    LogShutdownPhase("gpu");
    ScopedWindow.Destroy();
    LogShutdownPhase("window");
    Fby::LogInfo("editor_app", "shutdown complete in {:.1f} ms",
                 static_cast<double>(Fby::Core::Clock::NowNanoseconds() - ScopedShutdownStart) / 1.0e6);
    return 0;
}
