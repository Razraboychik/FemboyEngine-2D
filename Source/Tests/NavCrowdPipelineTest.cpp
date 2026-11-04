// FemboyEngine2D - navigation and crowd integration: world, paged flow, agents.
#include "Engine/Navigation/CrowdPool.hpp"
#include "Engine/Navigation/PagedNavGrid.hpp"
#include "Engine/Physics/SdfWorld.hpp"

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

void TestAgentsFollowThePagedField()
{
    // A wall at x = 128 with a gap between y = 96 and y = 160.
    Fby::SdfWorldDesc ScopedWorldDesc;
    ScopedWorldDesc.Origin = Fby::Vector2D{0.0f, 0.0f};
    ScopedWorldDesc.CellSize = Fby::Vector2D{4.0f, 4.0f};
    ScopedWorldDesc.Width = 64;
    ScopedWorldDesc.Height = 64;
    ScopedWorldDesc.NarrowBand = 16.0f;
    ScopedWorldDesc.OutsideDistance = 1000.0f;
    Fby::SdfWorld ScopedWorld;
    Check(ScopedWorld.Configure(ScopedWorldDesc), "configure the world");
    Check(ScopedWorld.AddBox(Fby::Vector2D{128.0f, 48.0f}, Fby::Vector2D{8.0f, 48.0f}), "add the lower wall");
    Check(ScopedWorld.AddBox(Fby::Vector2D{128.0f, 208.0f}, Fby::Vector2D{8.0f, 48.0f}), "add the upper wall");
    Check(ScopedWorld.BakeAll(), "bake the world");

    Fby::Navigation::PagedNavGridDesc ScopedNavDesc;
    ScopedNavDesc.Origin = Fby::Vector2D{0.0f, 0.0f};
    ScopedNavDesc.CellSize = Fby::Vector2D{4.0f, 4.0f};
    ScopedNavDesc.PagesX = 1;
    ScopedNavDesc.PagesY = 1;
    Fby::Navigation::PagedNavGrid ScopedNav;
    Check(ScopedNav.Configure(ScopedNavDesc), "configure the nav grid");
    Check(ScopedNav.BakeFromWorld(ScopedWorld, 2.0f), "bake nav costs from the world");
    Check(ScopedNav.CostAt(32, 8) == 0, "the lower wall is blocked");
    Check(ScopedNav.CostAt(32, 32) > 0, "the gap is open");
    Check(ScopedNav.BuildFlow(4, 32), "build the flow to the left side");

    // The crowd steers by the paged field: its window origin becomes the field
    // origin of the crowd settings.
    Fby::Navigation::CrowdSettings ScopedCrowdSettings;
    ScopedCrowdSettings.Field = &ScopedNav.Field();
    ScopedCrowdSettings.FieldOrigin = Fby::Vector2D{static_cast<float>(ScopedNav.WindowOriginX()) * 4.0f,
                                                    static_cast<float>(ScopedNav.WindowOriginY()) * 4.0f};
    ScopedCrowdSettings.FieldCellSize = 4.0f;
    ScopedCrowdSettings.PerceptionRadius = 12.0f;
    ScopedCrowdSettings.MaxNeighbours = 4;

    Fby::Navigation::CrowdPool ScopedPool;
    Check(ScopedPool.Configure(4), "configure the crowd pool");
    const std::uint32_t ScopedAgent =
        ScopedPool.Add(Fby::Vector2D{200.0f, 128.0f}, 0, Fby::Vector2D{16.0f, 128.0f}, 80.0f);
    Check(ScopedAgent == 0, "the agent is added");
    bool ScopedCrossed = false;
    bool ScopedThroughGap = false;
    for (std::uint32_t ScopedStep = 0; ScopedStep < 600; ++ScopedStep)
    {
        ScopedPool.Step(1.0f / 60.0f, ScopedCrowdSettings);
        const Fby::Navigation::CrowdAgent* ScopedCurrent = ScopedPool.At(ScopedAgent);
        if (!ScopedCrossed && ScopedCurrent->Position.X < 128.0f)
        {
            ScopedCrossed = true;
            ScopedThroughGap = ScopedCurrent->Position.Y > 96.0f && ScopedCurrent->Position.Y < 160.0f;
        }
    }
    Check(ScopedCrossed, "the agent crosses the wall line");
    Check(ScopedThroughGap, "the agent crosses through the gap");
    Check(ScopedPool.At(ScopedAgent)->Position.X < 128.0f, "the agent ends on the goal side");
}
}

int main()
{
    TestAgentsFollowThePagedField();
    if (GFailures != 0)
    {
        std::fprintf(stderr, "%d nav crowd pipeline test(s) failed\n", GFailures);
        return 1;
    }
    std::printf("nav_crowd_pipeline: OK\n");
    return 0;
}
