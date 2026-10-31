// FemboyEngine2D - navigation tests: A* on rect/hex maps and agent steering.
#include "Engine/ECS/Components.hpp"
#include "Engine/ECS/Tilemap.hpp"
#include "Engine/Navigation/Navigation.hpp"

#include <cmath>
#include <cstdio>

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

[[nodiscard]] Fby::Navigation::NavMap MakeOpenMap(Fby::Grid::ProjectionMode InMode, std::uint32_t InSize)
{
    Fby::Navigation::NavMap ScopedMap;
    Fby::Grid::GridDesc ScopedGrid;
    ScopedGrid.Mode = InMode;
    ScopedGrid.CellSize = Fby::Vector2D{64.0f, 32.0f};
    Fby::Navigation::ClearMap(ScopedMap, InSize, InSize, ScopedGrid);
    for (std::uint32_t ScopedY = 0; ScopedY < InSize; ++ScopedY)
    {
        for (std::uint32_t ScopedX = 0; ScopedX < InSize; ++ScopedX)
        {
            (void)ScopedMap.SetCost(Fby::Grid::Cell{static_cast<std::int32_t>(ScopedX),
                                                    static_cast<std::int32_t>(ScopedY)},
                                    1);
        }
    }
    return ScopedMap;
}

void TestPathOnRectGrid()
{
    Fby::Navigation::NavMap ScopedMap = MakeOpenMap(Fby::Grid::ProjectionMode::TopDown, 16);
    Fby::Navigation::PathFinder ScopedFinder;
    Fby::Navigation::NavPath ScopedPath;
    FBY_CHECK(ScopedFinder.Find(ScopedMap, Fby::Grid::Cell{0, 0}, Fby::Grid::Cell{5, 7}, &ScopedPath));
    FBY_CHECK(ScopedPath.Count >= 13);
    FBY_CHECK(ScopedPath.Cells[0] == (Fby::Grid::Cell{0, 0}));
    FBY_CHECK(ScopedPath.Cells[ScopedPath.Count - 1] == (Fby::Grid::Cell{5, 7}));
    for (std::uint32_t ScopedI = 1; ScopedI < ScopedPath.Count; ++ScopedI)
    {
        const float ScopedDistance =
            Fby::Grid::CellDistance(ScopedMap.Grid.Mode, ScopedPath.Cells[ScopedI - 1], ScopedPath.Cells[ScopedI]);
        FBY_CHECK(ScopedDistance <= 1.0001f);
    }

    // A wall column forces the path around it and never crosses it.
    for (std::uint32_t ScopedY = 0; ScopedY < 8; ++ScopedY)
    {
        (void)ScopedMap.SetCost(Fby::Grid::Cell{5, static_cast<std::int32_t>(ScopedY)}, 0);
    }
    FBY_CHECK(ScopedFinder.Find(ScopedMap, Fby::Grid::Cell{0, 0}, Fby::Grid::Cell{10, 0}, &ScopedPath));
    for (std::uint32_t ScopedI = 0; ScopedI < ScopedPath.Count; ++ScopedI)
    {
        FBY_CHECK(ScopedMap.Passable(ScopedPath.Cells[ScopedI]));
    }
    FBY_CHECK(ScopedPath.Count > 11);

    // Fully walled off: no path, and a partial query still returns a prefix.
    for (std::uint32_t ScopedY = 0; ScopedY < 16; ++ScopedY)
    {
        (void)ScopedMap.SetCost(Fby::Grid::Cell{5, static_cast<std::int32_t>(ScopedY)}, 0);
    }
    FBY_CHECK(!ScopedFinder.Find(ScopedMap, Fby::Grid::Cell{0, 0}, Fby::Grid::Cell{10, 0}, &ScopedPath));
    FBY_CHECK(ScopedFinder.Find(ScopedMap, Fby::Grid::Cell{0, 0}, Fby::Grid::Cell{10, 0}, &ScopedPath, true));

    FBY_CHECK(ScopedFinder.Stats().Queries >= 4);
    FBY_CHECK(ScopedFinder.Stats().Failed >= 1);
}

void TestPathOnHexGrid()
{
    Fby::Navigation::NavMap ScopedMap = MakeOpenMap(Fby::Grid::ProjectionMode::IsoHex, 12);
    Fby::Navigation::PathFinder ScopedFinder;
    Fby::Navigation::NavPath ScopedPath;
    FBY_CHECK(ScopedFinder.Find(ScopedMap, Fby::Grid::Cell{0, 0}, Fby::Grid::Cell{6, 3}, &ScopedPath));
    FBY_CHECK(ScopedPath.Count >= 3);
    for (std::uint32_t ScopedI = 1; ScopedI < ScopedPath.Count; ++ScopedI)
    {
        bool ScopedNeighbour = false;
        for (std::uint32_t ScopedN = 0; ScopedN < 6; ++ScopedN)
        {
            if (Fby::Grid::Neighbor(Fby::Grid::ProjectionMode::IsoHex, ScopedPath.Cells[ScopedI - 1], ScopedN) ==
                ScopedPath.Cells[ScopedI])
            {
                ScopedNeighbour = true;
                break;
            }
        }
        FBY_CHECK(ScopedNeighbour);
    }
    FBY_CHECK(!ScopedFinder.Find(ScopedMap, Fby::Grid::Cell{-1, 0}, Fby::Grid::Cell{3, 3}, &ScopedPath));
}

void TestBuildFromTilemap()
{
    Fby::TilemapComponent ScopedTilemap;
    ScopedTilemap.Width = 8;
    ScopedTilemap.Height = 8;
    ScopedTilemap.Grid.Mode = Fby::Grid::ProjectionMode::IsoRect;
    ScopedTilemap.Grid.CellSize = Fby::Vector2D{64.0f, 32.0f};
    Fby::Tilemap::Clear(ScopedTilemap);
    for (std::uint32_t ScopedX = 0; ScopedX < 8; ++ScopedX)
    {
        (void)Fby::Tilemap::Set(ScopedTilemap,
                                Fby::Grid::Cell{static_cast<std::int32_t>(ScopedX), 3},
                                Fby::MakeTile(1, Fby::kTileFlagSolid));
    }
    Fby::Navigation::NavMap ScopedMap;
    FBY_CHECK(Fby::Navigation::BuildFromTilemap(ScopedTilemap, &ScopedMap));
    FBY_CHECK(ScopedMap.Width == 8 && ScopedMap.Height == 8);
    FBY_CHECK(!ScopedMap.Passable(Fby::Grid::Cell{2, 3}));
    FBY_CHECK(ScopedMap.Passable(Fby::Grid::Cell{2, 2}));
}

void TestAgentSteering()
{
    Fby::Navigation::NavMap ScopedMap = MakeOpenMap(Fby::Grid::ProjectionMode::TopDown, 16);
    Fby::Navigation::NavigationSystem ScopedSystem;
    ScopedSystem.SetMap(&ScopedMap);

    entt::registry ScopedRegistry;
    const Fby::Entity ScopedEntity = ScopedRegistry.create();
    ScopedRegistry.emplace<Fby::Transform2D>(ScopedEntity, Fby::Transform2D{});
    Fby::Navigation::NavAgentSettings ScopedAgent;
    ScopedAgent.Flags = Fby::Navigation::kNavAgentFlagActive;
    ScopedAgent.Speed = 64.0f;
    ScopedAgent.ArrivalRadius = 4.0f;
    ScopedAgent.Target = Fby::Grid::CellToWorld(ScopedMap.Grid, Fby::Grid::Cell{2, 0});
    ScopedRegistry.emplace<Fby::Navigation::NavAgentSettings>(ScopedEntity, ScopedAgent);

    // The system creates the runtime state component for a settings-only agent.
    FBY_CHECK(ScopedRegistry.try_get<Fby::Navigation::NavAgentComponent>(ScopedEntity) == nullptr);
    ScopedSystem.Update(ScopedRegistry, 1.0f / 60.0f);
    FBY_CHECK(ScopedRegistry.try_get<Fby::Navigation::NavAgentComponent>(ScopedEntity) != nullptr);

    for (std::uint32_t ScopedStep = 0; ScopedStep < 240; ++ScopedStep)
    {
        ScopedSystem.Update(ScopedRegistry, 1.0f / 60.0f);
        if ((ScopedRegistry.get<Fby::Navigation::NavAgentComponent>(ScopedEntity).Flags &
             Fby::Navigation::kNavAgentFlagArrived) != 0)
        {
            break;
        }
    }
    const Fby::Transform2D& ScopedTransform = ScopedRegistry.get<Fby::Transform2D>(ScopedEntity);
    const Fby::Navigation::NavAgentComponent& ScopedFinal =
        ScopedRegistry.get<Fby::Navigation::NavAgentComponent>(ScopedEntity);
    FBY_CHECK((ScopedFinal.Flags & Fby::Navigation::kNavAgentFlagArrived) != 0);
    FBY_CHECK(std::fabs(ScopedTransform.Position.X - ScopedAgent.Target.X) <= ScopedAgent.ArrivalRadius + 0.01f);
    FBY_CHECK(std::fabs(ScopedTransform.Position.Y - ScopedAgent.Target.Y) <= ScopedAgent.ArrivalRadius + 0.01f);

    // A blocked goal marks the agent instead of moving it into the wall.
    Fby::Navigation::NavMap ScopedBlocked = MakeOpenMap(Fby::Grid::ProjectionMode::TopDown, 8);
    (void)ScopedBlocked.SetCost(Fby::Grid::Cell{5, 0}, 0);
    ScopedSystem.SetMap(&ScopedBlocked);
    Fby::Navigation::NavAgentSettings ScopedBlockedAgent;
    ScopedBlockedAgent.Flags = Fby::Navigation::kNavAgentFlagActive;
    ScopedBlockedAgent.Speed = 64.0f;
    ScopedBlockedAgent.Target = Fby::Grid::CellToWorld(ScopedBlocked.Grid, Fby::Grid::Cell{5, 0});
    const Fby::Entity ScopedBlockedEntity = ScopedRegistry.create();
    ScopedRegistry.emplace<Fby::Transform2D>(ScopedBlockedEntity, Fby::Transform2D{});
    ScopedRegistry.emplace<Fby::Navigation::NavAgentSettings>(ScopedBlockedEntity, ScopedBlockedAgent);
    FBY_CHECK(ScopedRegistry.try_get<Fby::Navigation::NavAgentComponent>(ScopedBlockedEntity) == nullptr);
    ScopedSystem.Update(ScopedRegistry, 1.0f / 60.0f);
    FBY_CHECK((ScopedRegistry.get<Fby::Navigation::NavAgentComponent>(ScopedBlockedEntity).Flags &
               Fby::Navigation::kNavAgentFlagBlocked) != 0);

    // A repath budget defers agents with a valid path instead of solving every
    // request in the same frame. A fresh system keeps the counter unambiguous.
    Fby::Navigation::NavigationSystem ScopedBudgetSystem;
    ScopedBudgetSystem.SetMap(&ScopedMap);
    ScopedBudgetSystem.MaxRepathsPerFrame = 1;
    entt::registry ScopedBudgetRegistry;
    const Fby::Entity ScopedBudgetEntity = ScopedBudgetRegistry.create();
    ScopedBudgetRegistry.emplace<Fby::Transform2D>(ScopedBudgetEntity, Fby::Transform2D{});
    Fby::Navigation::NavAgentSettings ScopedBudgetSettings;
    ScopedBudgetSettings.Flags = Fby::Navigation::kNavAgentFlagActive;
    ScopedBudgetSettings.Target = Fby::Grid::CellToWorld(ScopedMap.Grid, Fby::Grid::Cell{4, 0});
    ScopedBudgetRegistry.emplace<Fby::Navigation::NavAgentSettings>(ScopedBudgetEntity, ScopedBudgetSettings);
    ScopedBudgetSystem.Update(ScopedBudgetRegistry, 1.0f / 60.0f);
    FBY_CHECK(ScopedBudgetSystem.RepathsThisFrame() == 1);
    Fby::Navigation::NavAgentComponent& ScopedBudgetState =
        ScopedBudgetRegistry.get<Fby::Navigation::NavAgentComponent>(ScopedBudgetEntity);
    ScopedBudgetState.RepathTimer = 0.0f;
    ScopedBudgetState.Flags = 0;
    ScopedBudgetSystem.MaxRepathsPerFrame = 0;
    ScopedBudgetSystem.Update(ScopedBudgetRegistry, 1.0f / 60.0f);
    FBY_CHECK(ScopedBudgetSystem.RepathsThisFrame() == 0);
    FBY_CHECK((ScopedBudgetState.Flags & Fby::Navigation::kNavAgentFlagRepathDeferred) != 0);
}
}

int main()
{
    TestPathOnRectGrid();
    TestPathOnHexGrid();
    TestBuildFromTilemap();
    TestAgentSteering();

    if (GFailures != 0)
    {
        std::fprintf(stderr, "%u navigation test failure(s)\n", GFailures);
        return 1;
    }
    std::printf("navigation tests passed\n");
    return 0;
}
