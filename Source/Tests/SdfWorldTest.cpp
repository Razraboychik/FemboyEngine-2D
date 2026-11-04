// FemboyEngine2D - world distance field tests.
#include "Engine/Physics/SdfWorld.hpp"

#include "Engine/ECS/Components.hpp"
#include "Engine/ECS/Schedule.hpp"
#include "Engine/ECS/Tilemap.hpp"
#include "Engine/Physics/SdfDestructible.hpp"
#include "Engine/Physics/SdfPrimitives.hpp"
#include "Engine/Physics/SdfVolumeAsset.hpp"

#include <cmath>
#include <cstdio>
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

[[nodiscard]] Fby::SdfWorldDesc MakeDesc()
{
    Fby::SdfWorldDesc ScopedDesc;
    ScopedDesc.Origin = Fby::Vector2D{0.0f, 0.0f};
    ScopedDesc.CellSize = Fby::Vector2D{4.0f, 4.0f};
    ScopedDesc.Width = 128;
    ScopedDesc.Height = 128;
    ScopedDesc.NarrowBand = 32.0f;
    ScopedDesc.OutsideDistance = 1000.0f;
    return ScopedDesc;
}

void TestBakeAndSample()
{
    Fby::SdfWorld ScopedWorld;
    Check(ScopedWorld.Configure(MakeDesc()), "configure accepts the grid");
    Check(ScopedWorld.IsConfigured(), "configured world is valid");
    Check(ScopedWorld.Field().Valid(), "field view is valid");
    Check(ScopedWorld.AddBox(Fby::Vector2D{256.0f, 256.0f}, Fby::Vector2D{40.0f, 20.0f}), "add box");
    Check(ScopedWorld.BakeAll(), "full bake succeeds");
    Check(ScopedWorld.FieldVersion() == 1, "first bake bumps the version");
    Check(ScopedWorld.Bricks().BrickCount() == 64, "128x128 grid has 64 bricks");
    Check(ScopedWorld.Bricks().NarrowBrickCount() > 0, "the box produces narrow bricks");
    Check(ScopedWorld.Field().Sample(Fby::Vector2D{256.0f, 256.0f}) < 0.0f, "box centre is solid");
    Check(ScopedWorld.Field().Sample(Fby::Vector2D{0.0f, 0.0f}) > 0.0f, "far corner is outside");
    Check(ScopedWorld.Field().Sample(Fby::Vector2D{256.0f, 300.0f}) > 0.0f, "above the box is outside");
}

void TestIncrementalBake()
{
    Fby::SdfWorld ScopedWorld;
    Check(ScopedWorld.Configure(MakeDesc()), "configure for the incremental case");
    Check(ScopedWorld.AddBox(Fby::Vector2D{128.0f, 128.0f}, Fby::Vector2D{20.0f, 20.0f}), "add small box");
    Check(ScopedWorld.BakeAll(), "bake before the edit");
    const std::uint64_t ScopedVersion = ScopedWorld.FieldVersion();

    Fby::Vector2D ScopedCenter{128.0f, 128.0f};
    Check(ScopedWorld.AddCircle(ScopedCenter, 24.0f), "add circle");
    Check(ScopedWorld.PendingDirtyBricks() > 0, "the edit marks bricks dirty");
    Check(ScopedWorld.ApplyDirtyBake(4096), "dirty bake drains the queue");
    Check(ScopedWorld.PendingDirtyBricks() == 0, "queue is empty after the bake");
    Check(ScopedWorld.FieldVersion() == ScopedVersion + 1, "dirty bake bumps the version");
    Check(ScopedWorld.Field().Sample(ScopedCenter) < 0.0f, "circle centre is solid after the edit");
}

void TestSaveLoad()
{
    Fby::SdfWorld ScopedWorld;
    Check(ScopedWorld.Configure(MakeDesc()), "configure for the save case");
    Check(ScopedWorld.AddBox(Fby::Vector2D{200.0f, 200.0f}, Fby::Vector2D{30.0f, 30.0f}), "add box for save");
    Check(ScopedWorld.BakeAll(), "bake before save");

    std::vector<std::byte> ScopedBytes;
    Check(ScopedWorld.SaveAsset(&ScopedBytes), "world saves to .fsdf bytes");
    Check(ScopedBytes.size() > Fby::SdfVolumeHeaderBytes, "saved image has a payload");

    Fby::SdfVolumeAsset ScopedAsset;
    std::string ScopedError;
    Check(ScopedAsset.Load(ScopedBytes, &ScopedError), "saved image loads back");
    Check(ScopedAsset.Sample(Fby::Vector2D{200.0f, 200.0f}) < 0.0f, "loaded field keeps the solid");
    Check(ScopedAsset.Sample(Fby::Vector2D{0.0f, 0.0f}) > 0.0f, "loaded field keeps the outside");
}

void TestRegistryBake()
{
    Fby::Ecs::Registry ScopedRegistry;
    const Fby::Entity ScopedEntity = ScopedRegistry.create();
    Fby::Transform2D ScopedTransform;
    ScopedTransform.Position = Fby::Vector2D{200.0f, 200.0f};
    ScopedRegistry.emplace<Fby::Transform2D>(ScopedEntity, ScopedTransform);
    Fby::SdfShapeComponent ScopedShape;
    ScopedShape.Shape = Fby::MakeSdfShape(Fby::SdfShapeType::Circle);
    ScopedShape.Shape.Radius = 24.0f;
    ScopedRegistry.emplace<Fby::SdfShapeComponent>(ScopedEntity, ScopedShape);

    Fby::SdfWorld ScopedWorld;
    Check(ScopedWorld.Configure(MakeDesc()), "configure for the registry case");
    Check(ScopedWorld.BakeRegistry(ScopedRegistry), "registry bake succeeds");
    Check(ScopedWorld.Field().Sample(Fby::Vector2D{200.0f, 200.0f}) < 0.0f, "shape entity is solid");
    Check(ScopedWorld.Field().Sample(Fby::Vector2D{400.0f, 400.0f}) > 0.0f, "away from the shape is outside");
}
void TestTilemapSource()
{
    Fby::TilemapComponent ScopedMap;
    Fby::Tilemap::Resize(ScopedMap, 16, 16);
    ScopedMap.Grid.CellSize = Fby::Vector2D{16.0f, 16.0f};
    ScopedMap.Grid.Origin = Fby::Vector2D{0.0f, 0.0f};
    const std::uint32_t ScopedSolid = 1u | Fby::kTileFlagSolid;
    for (std::uint32_t ScopedY = 4; ScopedY < 6; ++ScopedY)
    {
        for (std::uint32_t ScopedX = 4; ScopedX < 8; ++ScopedX)
        {
            Check(Fby::Tilemap::Set(ScopedMap,
                                    Fby::Grid::Cell{static_cast<std::int32_t>(ScopedX),
                                                    static_cast<std::int32_t>(ScopedY)},
                                    ScopedSolid),
                  "solid tile is written");
        }
    }

    Fby::SdfWorldDesc ScopedDesc = MakeDesc();
    ScopedDesc.CellSize = Fby::Vector2D{16.0f, 16.0f};
    ScopedDesc.Width = 32;
    ScopedDesc.Height = 32;
    Fby::SdfWorld ScopedWorld;
    Check(ScopedWorld.Configure(ScopedDesc), "configure for the tilemap case");
    Check(ScopedWorld.AddTilemap(ScopedMap), "tilemap converts to sources");
    Check(ScopedWorld.BakeAll(), "tilemap bake succeeds");
    const Fby::Vector2D ScopedSolidCenter =
        Fby::Tilemap::CellToWorld(ScopedMap, Fby::Grid::Cell{5, 4});
    Check(ScopedWorld.Field().Sample(ScopedSolidCenter) < 0.0f, "solid tile bakes as solid");
    Check(ScopedWorld.Field().Sample(Fby::Vector2D{-200.0f, -200.0f}) > 0.0f, "empty area stays outside");
}
void TestImprint()
{
    Fby::SdfDestructibleDesc ScopedObjectDesc;
    ScopedObjectDesc.Origin = Fby::Vector2D{96.0f, 96.0f};
    ScopedObjectDesc.CellSize = Fby::Vector2D{1.0f, 1.0f};
    ScopedObjectDesc.Width = 64;
    ScopedObjectDesc.Height = 64;
    ScopedObjectDesc.BaseShape = Fby::SdfCarveShape::Box;
    ScopedObjectDesc.HalfExtents = Fby::Vector2D{24.0f, 24.0f};
    ScopedObjectDesc.NarrowBand = 8.0f;
    ScopedObjectDesc.OutsideDistance = 1000.0f;

    Fby::SdfDestructible ScopedObject;
    Check(ScopedObject.Configure(ScopedObjectDesc), "configure the destructible for the imprint");
    Fby::SdfCarveOp ScopedHole;
    ScopedHole.Shape = Fby::SdfCarveShape::Circle;
    ScopedHole.Center = Fby::Vector2D{128.0f, 128.0f};
    ScopedHole.Radius = 8.0f;
    Check(ScopedObject.Subtract(ScopedHole), "carve the destructible");
    Check(ScopedObject.Sample(Fby::Vector2D{128.0f, 128.0f}) > 0.0f, "object centre is carved");

    Fby::SdfWorldDesc ScopedDesc;
    ScopedDesc.CellSize = Fby::Vector2D{4.0f, 4.0f};
    ScopedDesc.Width = 64;
    ScopedDesc.Height = 64;
    ScopedDesc.NarrowBand = 16.0f;
    ScopedDesc.OutsideDistance = 1000.0f;
    Fby::SdfWorld ScopedWorld;
    Check(ScopedWorld.Configure(ScopedDesc), "configure the world for the imprint");
    Check(ScopedWorld.AddBox(Fby::Vector2D{128.0f, 40.0f}, Fby::Vector2D{100.0f, 8.0f}), "add the floor");
    Check(ScopedWorld.BakeAll(), "bake before the imprint");
    const float ScopedRingBefore = ScopedWorld.Field().Sample(Fby::Vector2D{144.0f, 128.0f});
    Check(ScopedRingBefore > 0.0f, "the object area starts empty");

    const std::uint64_t ScopedVersion = ScopedWorld.FieldVersion();
    Check(ScopedWorld.Imprint(ScopedObject), "imprint the destructible");
    Check(ScopedWorld.FieldVersion() > ScopedVersion, "the imprint bumps the world version");
    Check(ScopedWorld.Field().Sample(Fby::Vector2D{144.0f, 128.0f}) < 0.0f, "the object imprint is solid");
    Check(ScopedWorld.Field().Sample(Fby::Vector2D{128.0f, 128.0f}) > 0.0f, "the hole survives the imprint");

    // The same op list imprinted into a second world produces the same field.
    Fby::SdfWorld ScopedSecond;
    Check(ScopedSecond.Configure(ScopedDesc), "configure the second world");
    Check(ScopedSecond.AddBox(Fby::Vector2D{128.0f, 40.0f}, Fby::Vector2D{100.0f, 8.0f}), "second floor");
    Check(ScopedSecond.BakeAll(), "second bake");
    Check(ScopedSecond.Imprint(ScopedObject), "second imprint");
    bool ScopedWorldsMatch = true;
    for (std::uint32_t ScopedY = 0; ScopedY < 64; ++ScopedY)
    {
        for (std::uint32_t ScopedX = 0; ScopedX < 64; ++ScopedX)
        {
            const Fby::Vector2D ScopedPoint{static_cast<float>(ScopedX) * 4.0f, static_cast<float>(ScopedY) * 4.0f};
            if (ScopedWorld.Field().Sample(ScopedPoint) != ScopedSecond.Field().Sample(ScopedPoint))
            {
                ScopedWorldsMatch = false;
            }
        }
    }
    Check(ScopedWorldsMatch, "the same imprint builds the same world");

    ScopedWorld.ClearImprints();
    Check(ScopedWorld.Field().Sample(Fby::Vector2D{144.0f, 128.0f}) > 0.0f, "clearing the imprint removes it");
}
}

int main()
{
    TestBakeAndSample();
    TestIncrementalBake();
    TestSaveLoad();
    TestRegistryBake();
    TestTilemapSource();
    TestImprint();
    if (GFailures != 0)
    {
        std::fprintf(stderr, "%d sdf world test(s) failed\n", GFailures);
        return 1;
    }
    std::printf("sdf_world: OK\n");
    return 0;
}
