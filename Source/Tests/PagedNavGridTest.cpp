// FemboyEngine2D - paged navigation grid tests.
#include "Engine/Navigation/PagedNavGrid.hpp"

#include "Engine/ECS/Tilemap.hpp"

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

[[nodiscard]] Fby::Navigation::PagedNavGridDesc MakeDesc()
{
    Fby::Navigation::PagedNavGridDesc ScopedDesc;
    ScopedDesc.Origin = Fby::Vector2D{0.0f, 0.0f};
    ScopedDesc.CellSize = Fby::Vector2D{8.0f, 8.0f};
    ScopedDesc.PagesX = 4;
    ScopedDesc.PagesY = 1;
    ScopedDesc.DefaultCost = 1;
    return ScopedDesc;
}

void TestLazyPages()
{
    Fby::Navigation::PagedNavGrid ScopedGrid;
    Check(ScopedGrid.Configure(MakeDesc()), "configure the paged grid");
    Check(ScopedGrid.WidthCells() == 256, "four pages span 256 cells");
    Check(ScopedGrid.HeightCells() == 64, "one page is 64 cells tall");
    Check(ScopedGrid.AllocatedPageCount() == 0, "pages start unallocated");
    Check(ScopedGrid.CostAt(200, 32) == 0, "a missing page reads as blocked");
    Check(ScopedGrid.SetCost(200, 32, 1), "writing a cell allocates its page");
    Check(ScopedGrid.AllocatedPageCount() == 1, "only the touched page is allocated");
    Check(ScopedGrid.CostAt(200, 32) == 1, "the written cell reads back");
    Check(ScopedGrid.CostAt(100, 32) == 0, "untouched pages stay blocked");
}

void TestCorridorAcrossPages()
{
    Fby::Navigation::PagedNavGrid ScopedGrid;
    Check(ScopedGrid.Configure(MakeDesc()), "configure the corridor grid");
    // A one-cell-high corridor runs from page 0 to page 3.
    for (std::uint32_t ScopedX = 0; ScopedX < 256; ++ScopedX)
    {
        Check(ScopedGrid.SetCost(ScopedX, 32, 1), "corridor cell is written");
    }
    for (std::uint32_t ScopedX = 0; ScopedX < 256; ++ScopedX)
    {
        for (std::uint32_t ScopedY = 0; ScopedY < 64; ++ScopedY)
        {
            if (ScopedY != 32)
            {
                Check(ScopedGrid.SetCost(ScopedX, ScopedY, 0), "corridor wall is written");
            }
        }
    }
    Check(ScopedGrid.BuildFlow(0, 32), "build the corridor flow");
    Check(ScopedGrid.IsReachableCell(255, 32), "the far end crosses every page boundary");
    Check(ScopedGrid.PotentialAtCell(255, 32) == 255u * Fby::Navigation::kFlowStepStraight,
          "the corridor potential is exact across pages");
    Check(ScopedGrid.PotentialAtCell(128, 32) == 128u * Fby::Navigation::kFlowStepStraight,
          "the middle of the corridor is exact");
    Check(!ScopedGrid.IsReachableCell(128, 31), "the corridor wall is unreachable");
    const Fby::Vector2D ScopedFlow = ScopedGrid.FlowAtCell(200, 32);
    Check(ScopedFlow.X < -0.9f, "the corridor flow points at the goal");
}

void TestBlockedBoundary()
{
    Fby::Navigation::PagedNavGrid ScopedGrid;
    Check(ScopedGrid.Configure(MakeDesc()), "configure the sealed grid");
    // The first page is passable, the rest is authored but separated by a wall.
    for (std::uint32_t ScopedX = 0; ScopedX < 64; ++ScopedX)
    {
        for (std::uint32_t ScopedY = 0; ScopedY < 64; ++ScopedY)
        {
            Check(ScopedGrid.SetCost(ScopedX, ScopedY, 1), "first page is authored");
        }
    }
    for (std::uint32_t ScopedX = 64; ScopedX < 128; ++ScopedX)
    {
        for (std::uint32_t ScopedY = 0; ScopedY < 64; ++ScopedY)
        {
            Check(ScopedGrid.SetCost(ScopedX, ScopedY, 0), "wall page is authored");
        }
    }
    Check(ScopedGrid.BuildFlow(32, 32), "build the sealed flow");
    Check(ScopedGrid.IsReachableCell(63, 32), "the walled page edge is reachable");
    Check(!ScopedGrid.IsReachableCell(100, 32), "the sealed page is unreachable");
    Check(ScopedGrid.FlowAtCell(100, 32).LengthSquared() == 0.0f, "the sealed page has no flow");
}

void TestBakeFromWorld()
{
    // A wall in the world field must become a navigation wall.
    Fby::SdfWorldDesc ScopedWorldDesc;
    ScopedWorldDesc.Origin = Fby::Vector2D{0.0f, 0.0f};
    ScopedWorldDesc.CellSize = Fby::Vector2D{4.0f, 4.0f};
    ScopedWorldDesc.Width = 64;
    ScopedWorldDesc.Height = 64;
    ScopedWorldDesc.NarrowBand = 16.0f;
    ScopedWorldDesc.OutsideDistance = 1000.0f;
    Fby::SdfWorld ScopedWorld;
    Check(ScopedWorld.Configure(ScopedWorldDesc), "configure the baked world");
    Check(ScopedWorld.AddBox(Fby::Vector2D{128.0f, 64.0f}, Fby::Vector2D{4.0f, 64.0f}), "add the wall");
    Check(ScopedWorld.BakeAll(), "bake the world");

    Fby::Navigation::PagedNavGridDesc ScopedNavDesc;
    ScopedNavDesc.Origin = Fby::Vector2D{0.0f, 0.0f};
    ScopedNavDesc.CellSize = Fby::Vector2D{4.0f, 4.0f};
    ScopedNavDesc.PagesX = 1;
    ScopedNavDesc.PagesY = 1;
    Fby::Navigation::PagedNavGrid ScopedNav;
    Check(ScopedNav.Configure(ScopedNavDesc), "configure the baked nav grid");
    Check(ScopedNav.BakeFromWorld(ScopedWorld, 2.0f), "bake navigation costs from the world");
    Check(ScopedNav.CostAt(32, 8) == 0, "the wall cell is blocked");
    Check(ScopedNav.CostAt(10, 8) > 0, "open ground near the wall is passable");
    Check(ScopedNav.CostAt(50, 8) > 0, "the far side is passable");

    Check(ScopedNav.BuildFlow(4, 8), "build the baked flow");
    Check(ScopedNav.IsReachableCell(10, 8), "the goal side is reachable");
    Check(ScopedNav.IsReachableCell(50, 50), "above the wall the far side is reachable");
}

void TestBakeFromWorldAndTilemap()
{
    Fby::SdfWorldDesc ScopedWorldDesc;
    ScopedWorldDesc.Origin = Fby::Vector2D{0.0f, 0.0f};
    ScopedWorldDesc.CellSize = Fby::Vector2D{4.0f, 4.0f};
    ScopedWorldDesc.Width = 64;
    ScopedWorldDesc.Height = 64;
    ScopedWorldDesc.OutsideDistance = 1000.0f;
    Fby::SdfWorld ScopedWorld;
    Check(ScopedWorld.Configure(ScopedWorldDesc), "configure the tilemap world");
    Check(ScopedWorld.BakeAll(), "bake the empty world");

    Fby::TilemapComponent ScopedTilemap;
    Fby::Tilemap::Resize(ScopedTilemap, 16, 16);
    ScopedTilemap.Grid.CellSize = Fby::Vector2D{16.0f, 16.0f};
    ScopedTilemap.Grid.Origin = Fby::Vector2D{8.0f, 8.0f};
    const std::uint32_t ScopedSolid = 1u | Fby::kTileFlagSolid;
    Check(Fby::Tilemap::Set(ScopedTilemap, Fby::Grid::Cell{4, 4}, ScopedSolid), "set a solid tile");

    Fby::Navigation::PagedNavGridDesc ScopedNavDesc;
    ScopedNavDesc.Origin = Fby::Vector2D{0.0f, 0.0f};
    ScopedNavDesc.CellSize = Fby::Vector2D{4.0f, 4.0f};
    ScopedNavDesc.PagesX = 1;
    ScopedNavDesc.PagesY = 1;
    Fby::Navigation::PagedNavGrid ScopedNav;
    Check(ScopedNav.Configure(ScopedNavDesc), "configure the tilemap nav grid");
    Check(ScopedNav.BakeFromWorld(ScopedWorld, ScopedTilemap, 2.0f), "bake navigation from the tilemap");
    // The tile at cell (4,4) covers world 64..80; nav cell (18,18) sits inside it.
    Check(ScopedNav.CostAt(18, 18) == 0, "the solid tile blocks navigation");
    Check(ScopedNav.CostAt(4, 4) > 0, "empty tiles stay passable");
}
}

int main()
{
    TestLazyPages();
    TestCorridorAcrossPages();
    TestBlockedBoundary();
    TestBakeFromWorld();
    TestBakeFromWorldAndTilemap();
    if (GFailures != 0)
    {
        std::fprintf(stderr, "%d paged nav test(s) failed\n", GFailures);
        return 1;
    }
    std::printf("paged_nav: OK\n");
    return 0;
}
