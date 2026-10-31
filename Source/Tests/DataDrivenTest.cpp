// FemboyEngine2D - data-driven descriptors, prefab spawn, DPI and theme tests.
#include "Engine/Core/FrameArena.hpp"
#include "Engine/Core/JobSystem.hpp"
#include "Engine/ECS/CommandBuffer.hpp"
#include "Engine/ECS/Descriptors.hpp"
#include "Engine/ECS/PrefabSystem.hpp"
#include "Engine/Physics/CollisionFilter.hpp"
#include "Engine/Physics/Physics.hpp"
#include "Engine/UI/PinkUI.hpp"
#include "Engine/UI/UiTheme.hpp"

#include <array>
#include <cstdio>
#include <cstring>
#include <string>
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

const char* kPrefabToml = R"TOML(
name = "shotgun_player"

[[entities]]
name = "player"
Position = [10.0, 20.0]
Scale = [1.0, 1.0]
Rotation = 0.0

[entities.physics]
type = 2
shape = 0
half_extents = [16.0, 16.0]
density = 1.0
friction = 0.25
restitution = 0.0
category = 1
mask = 4294967295
fixed_rotation = true

[entities.character]
health = 100.0
stamina = 100.0
speed = 240.0
acceleration = 2000.0
jump = 500.0
damage = 12.0
fire_interval = 0.1
projectile_speed = 1100.0
invulnerability = 0.2
armor = 2.0
team = 0
weapon = 7

[entities.sprite]
size = [32.0, 32.0]
uv_min = [0.0, 0.0]
uv_max = [1.0, 1.0]
layer = 3
color = 4294967295
)TOML";
}

int main()
{
    using namespace Fby;

    CollisionFilterMatrix ScopedMatrix = CollisionFilterMatrix::Defaults();
    FBY_CHECK(ScopedMatrix.Get(CollisionLayer::Player, CollisionLayer::Enemy));
    FBY_CHECK(!ScopedMatrix.Get(CollisionLayer::Player, CollisionLayer::SdfTerrain));
    ScopedMatrix.Set(CollisionLayer::Player, CollisionLayer::Water, false);
    FBY_CHECK(!ScopedMatrix.Get(CollisionLayer::Player, CollisionLayer::Water));
    ScopedMatrix.Set(CollisionLayer::Player, CollisionLayer::Water, true);
    FBY_CHECK(ScopedMatrix.Get(CollisionLayer::Water, CollisionLayer::Player));
    FBY_CHECK(ScopedMatrix.CategoryBits(2) == 4u);
    FBY_CHECK(CollisionFilterMatrix::LayerFromCategory(4u) == 2u);

    const Reflect::ComponentDesc ScopedBodyDesc = Reflect::Describe<PhysicsBodyDesc>();
    const Reflect::ComponentDesc ScopedCharacterDesc = Reflect::Describe<CharacterDesc>();
    FBY_CHECK(ScopedBodyDesc.fields.size() >= 10);
    FBY_CHECK(ScopedCharacterDesc.fields.size() >= 8);

    PrefabAsset ScopedAsset{};
    std::string ScopedError;
    FBY_CHECK(PrefabSystem::LoadToml(kPrefabToml, ScopedAsset, &ScopedError));
    FBY_CHECK(ScopedAsset.count == 1);
    FBY_CHECK((ScopedAsset.entities[0].flags & PrefabHasPhysics) != 0);
    FBY_CHECK(ScopedAsset.entities[0].character.MoveSpeed == 240.0f);
    FBY_CHECK(ScopedAsset.entities[0].physics.FixedRotation);

    std::vector<std::byte> ScopedBinary(PrefabSystem::BinarySize(ScopedAsset));
    std::size_t ScopedBinaryBytes = 0;
    FBY_CHECK(PrefabSystem::SaveBinary(ScopedAsset, ScopedBinary, &ScopedBinaryBytes));
    PrefabAsset ScopedRoundTrip{};
    FBY_CHECK(PrefabSystem::LoadBinary(std::span<const std::byte>(ScopedBinary).first(ScopedBinaryBytes),
                                       ScopedRoundTrip, &ScopedError));
    FBY_CHECK(ScopedRoundTrip.count == 1);
    FBY_CHECK(std::strcmp(ScopedRoundTrip.entities[0].name, "player") == 0);

    Ecs::Registry ScopedRegistry;
    std::vector<std::byte> ScopedArenaStorage(1u << 20);
    FrameArena ScopedArena;
    ScopedArena.Initialize(ScopedArenaStorage);
    Ecs::CommandBuffer ScopedCommands;
    ScopedCommands.Initialize(&ScopedArena);
    std::array<Entity, 4> ScopedSpawned{};
    FBY_CHECK(PrefabSystem::Spawn(ScopedRegistry, ScopedCommands, ScopedAsset, Vector2D{100.0f, 200.0f},
                                  ScopedSpawned));
    FBY_CHECK(ScopedCommands.Count() == 6);
    ScopedCommands.Play(ScopedRegistry);
    auto ScopedView = ScopedRegistry.view<Transform2D, PhysicsBodyDesc, CharacterDesc, RigidBody2D, Collider2D>();
    std::uint32_t ScopedEntityCount = 0;
    for (const Entity ScopedEntity : ScopedView)
    {
        ++ScopedEntityCount;
        const PhysicsBodyDesc& ScopedPhysics = ScopedView.get<PhysicsBodyDesc>(ScopedEntity);
        const CharacterDesc& ScopedCharacter = ScopedView.get<CharacterDesc>(ScopedEntity);
        const Transform2D& ScopedTransform = ScopedView.get<Transform2D>(ScopedEntity);
        FBY_CHECK(ScopedPhysics.Density == 1.0f);
        FBY_CHECK(ScopedCharacter.Damage == 12.0f);
        FBY_CHECK(ScopedTransform.Position.X == 110.0f);
        FBY_CHECK((ScopedView.get<RigidBody2D>(ScopedEntity).Flags & RigidBodyFixedRotation) != 0);
    }
    FBY_CHECK(ScopedEntityCount == 1);

    // A tiny arena makes the second spawn fail and must leave no half prefab.
    std::array<std::byte, 8> ScopedTinyStorage{};
    FrameArena ScopedTinyArena;
    ScopedTinyArena.Initialize(ScopedTinyStorage);
    Ecs::CommandBuffer ScopedTinyCommands;
    ScopedTinyCommands.Initialize(&ScopedTinyArena);
    const std::size_t ScopedBefore = ScopedRegistry.storage<Transform2D>().size();
    FBY_CHECK(!PrefabSystem::Spawn(ScopedRegistry, ScopedTinyCommands, ScopedAsset, Vector2D{}, {}));
    FBY_CHECK(ScopedTinyCommands.Count() == 0);
    FBY_CHECK(ScopedRegistry.storage<Transform2D>().size() == ScopedBefore);

    PhysicsSystem ScopedPhysics;
    PhysicsDesc ScopedPhysicsDesc{};
    ScopedPhysicsDesc.gravity = Vector2D{};
    FBY_CHECK(ScopedPhysics.Initialize(ScopedPhysicsDesc));
    ScopedPhysics.EnsureBodies(ScopedRegistry);
    CollisionFilterMatrix ScopedLive = CollisionFilterMatrix::Defaults();
    ScopedLive.Set(CollisionLayer::Player, CollisionLayer::Enemy, false);
    ScopedPhysics.ApplyCollisionFilterMatrix(ScopedRegistry, ScopedLive);
    auto ScopedColliderView = ScopedRegistry.view<Collider2D>();
    for (const Entity ScopedEntity : ScopedColliderView)
    {
        const Collider2D& ScopedCollider = ScopedColliderView.get<Collider2D>(ScopedEntity);
        FBY_CHECK((ScopedCollider.MaskBits & (1u << static_cast<std::uint32_t>(CollisionLayer::Enemy))) == 0);
    }

    Ui::UiThemeAsset ScopedTheme = Ui::DefaultUiTheme();
    ScopedTheme.palette.colors[static_cast<std::uint32_t>(Ui::UiPaletteSlot::Primary)] =
        Color32::FromBytes(10, 20, 30, 255);
    Ui::UiThemeAsset ScopedLoadedTheme{};
    FBY_CHECK(Ui::LoadUiThemeToml(R"TOML([theme]
name = "Test"
[theme.palette]
primary = "#102030"
)TOML",
                                  ScopedLoadedTheme));
    FBY_CHECK(ScopedLoadedTheme.Resolve(Ui::UiPaletteSlot::Primary) == Color32::FromBytes(16, 32, 48));
    Ui::WidgetCanvas ScopedCanvas;
    ScopedCanvas.SetCanvasSize(Vector2D{320.0f, 120.0f});
    ScopedCanvas.SetDisplayScale(2.0f);
    ScopedCanvas.SetFollowDisplayScale(false);
    ScopedCanvas.SetTheme(ScopedTheme);
    const std::array<float, 2> ScopedColumns{100.0f, 50.0f};
    const std::array<float, 1> ScopedRows{50.0f};
    FBY_CHECK(ScopedCanvas.SetGrid(2, 1, ScopedColumns, ScopedRows, Vector2D{10.0f, 0.0f}, Vector2D{}));
    const Vector2D ScopedGridPixels = ScopedCanvas.GridPixelExtent(2.0f);
    FBY_CHECK(ScopedGridPixels.X == 320.0f);
    FBY_CHECK(ScopedGridPixels.Y == 100.0f);
    Ui::WidgetDesc ScopedPanel{};
    ScopedPanel.anchorMin = Vector2D{0.0f, 0.0f};
    ScopedPanel.anchorMax = Vector2D{0.0f, 0.0f};
    ScopedPanel.offsetMin = Vector2D{0.0f, 0.0f};
    ScopedPanel.offsetMax = Vector2D{100.0f, 50.0f};
    ScopedPanel.tintPalette = static_cast<std::uint16_t>(Ui::UiPaletteSlot::Primary);
    FBY_CHECK(ScopedCanvas.AddWidget(ScopedPanel) == 0);
    Ecs::Registry ScopedUiRegistry;
    JobSystem ScopedJobs(1);
    Ecs::SystemContext ScopedContext{ScopedUiRegistry, ScopedJobs};
    std::array<Rhi::RenderItem, 8> ScopedItems{};
    std::array<Rhi::RenderKey, 8> ScopedKeys{};
    std::array<Rhi::DrawBatch, 2> ScopedBatches{};
    Ui::FrameOutput ScopedOutput{ScopedItems, ScopedKeys, ScopedBatches, 0};
    FBY_CHECK(Ui::WidgetCanvasSystem::BuildFrame(ScopedContext, ScopedCanvas, ScopedOutput) == Ui::BuildStatus::Ok);
    FBY_CHECK(ScopedOutput.itemsWritten == 1);
    FBY_CHECK(ScopedItems[0].size.X == 200.0f);
    FBY_CHECK(ScopedItems[0].size.Y == 100.0f);
    FBY_CHECK(ScopedItems[0].color == ScopedTheme.Resolve(Ui::UiPaletteSlot::Primary).rgba);
    const Vector2D ScopedScreen = ScopedCanvas.CanvasToScreen(Vector2D{10.0f, 10.0f});
    const Vector2D ScopedBack = ScopedCanvas.ScreenToCanvas(ScopedScreen);
    FBY_CHECK(ScopedBack.X == 10.0f && ScopedBack.Y == 10.0f);

    ScopedPhysics.Shutdown();
    if (GFailures == 0)
    {
        std::printf("data_driven_test: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "data_driven_test: %u failure(s)\n", GFailures);
    return 1;
}
