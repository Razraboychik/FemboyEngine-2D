// FemboyEngine2D - crowd render extract tests.
#include "Engine/Navigation/CrowdRender.hpp"

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

void TestExtract()
{
    Fby::Navigation::CrowdPool ScopedPool;
    Check(ScopedPool.Configure(8), "configure the extract pool");
    (void)ScopedPool.Add(Fby::Vector2D{10.0f, 20.0f}, 0, Fby::Vector2D{100.0f, 100.0f});
    (void)ScopedPool.Add(Fby::Vector2D{30.0f, 40.0f}, 1, Fby::Vector2D{100.0f, 100.0f});
    (void)ScopedPool.Add(Fby::Vector2D{50.0f, 60.0f}, 9, Fby::Vector2D{100.0f, 100.0f});

    Fby::Navigation::CrowdRenderSettings ScopedSettings;
    ScopedSettings.Size = Fby::Vector2D{8.0f, 8.0f};
    ScopedSettings.Layer = 3;
    std::vector<Fby::Rhi::RenderItem> ScopedItems(8);
    const std::uint32_t ScopedWritten =
        Fby::Navigation::BuildCrowdRenderItems(ScopedPool, ScopedSettings, ScopedItems);
    Check(ScopedWritten == 3, "one item per agent");
    Check(ScopedItems[0].position.X == 10.0f && ScopedItems[0].position.Y == 20.0f, "the first position matches");
    Check(ScopedItems[0].size.X == 8.0f && ScopedItems[0].size.Y == 8.0f, "the size comes from the settings");
    Check(ScopedItems[0].layer == 3, "the layer comes from the settings");
    Check(ScopedItems[0].color == ScopedSettings.GroupColors[0], "the group color is applied");
    Check(ScopedItems[1].color == ScopedSettings.GroupColors[1], "the second group gets its color");
    Check(ScopedItems[2].color == ScopedSettings.GroupColors[1], "groups wrap modulo eight");

    // A span smaller than the pool caps the output.
    std::vector<Fby::Rhi::RenderItem> ScopedSmall(2);
    Check(Fby::Navigation::BuildCrowdRenderItems(ScopedPool, ScopedSettings, ScopedSmall) == 2,
          "the output span caps the extract");
}

void TestRotation()
{
    Fby::Navigation::CrowdPool ScopedPool;
    Check(ScopedPool.Configure(4), "configure the rotation pool");
    (void)ScopedPool.Add(Fby::Vector2D{0.0f, 0.0f}, 0, Fby::Vector2D{100.0f, 0.0f});
    Fby::Navigation::CrowdSettings ScopedSimSettings;
    for (std::uint32_t ScopedStep = 0; ScopedStep < 10; ++ScopedStep)
    {
        ScopedPool.Step(1.0f / 60.0f, ScopedSimSettings);
    }
    Fby::Navigation::CrowdRenderSettings ScopedSettings;
    std::vector<Fby::Rhi::RenderItem> ScopedItems(1);
    (void)Fby::Navigation::BuildCrowdRenderItems(ScopedPool, ScopedSettings, ScopedItems);
    Check(std::abs(ScopedItems[0].rotation) < 1.0e-3f, "a rightward agent faces zero radians");
}
}

int main()
{
    TestExtract();
    TestRotation();
    if (GFailures != 0)
    {
        std::fprintf(stderr, "%d crowd render test(s) failed\n", GFailures);
        return 1;
    }
    std::printf("crowd_render: OK\n");
    return 0;
}
