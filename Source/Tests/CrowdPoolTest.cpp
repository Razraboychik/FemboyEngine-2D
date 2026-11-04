// FemboyEngine2D - crowd pool tests.
#include "Engine/Navigation/CrowdPool.hpp"

#include <cmath>
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

[[nodiscard]] Fby::Navigation::CrowdSettings MakeSettings()
{
    Fby::Navigation::CrowdSettings ScopedSettings;
    ScopedSettings.PerceptionRadius = 24.0f;
    ScopedSettings.MaxNeighbours = 16;
    return ScopedSettings;
}

void TestSeek()
{
    Fby::Navigation::CrowdPool ScopedPool;
    Check(ScopedPool.Configure(4), "configure the seek pool");
    const std::uint32_t ScopedIndex = ScopedPool.Add(Fby::Vector2D{0.0f, 0.0f}, 0, Fby::Vector2D{100.0f, 0.0f});
    Check(ScopedIndex == 0, "the first agent index is zero");
    const Fby::Navigation::CrowdSettings ScopedSettings = MakeSettings();
    for (std::uint32_t ScopedStep = 0; ScopedStep < 60; ++ScopedStep)
    {
        ScopedPool.Step(1.0f / 60.0f, ScopedSettings);
    }
    const Fby::Navigation::CrowdAgent* ScopedAgent = ScopedPool.At(ScopedIndex);
    Check(ScopedAgent != nullptr, "the seeking agent exists");
    Check(ScopedAgent->Position.X > 20.0f, "the agent moved toward its target");
    Check(ScopedAgent->Velocity.X > 0.0f, "the velocity points at the target");
    Check(std::abs(ScopedAgent->Position.Y) < 1.0e-3f, "the agent stays on the target line");
}

void TestSeparation()
{
    Fby::Navigation::CrowdPool ScopedPool;
    Check(ScopedPool.Configure(4), "configure the separation pool");
    (void)ScopedPool.Add(Fby::Vector2D{0.0f, 0.0f}, 0, Fby::Vector2D{0.0f, 200.0f});
    (void)ScopedPool.Add(Fby::Vector2D{1.0f, 0.0f}, 0, Fby::Vector2D{0.0f, 200.0f});
    const Fby::Navigation::CrowdSettings ScopedSettings = MakeSettings();
    for (std::uint32_t ScopedStep = 0; ScopedStep < 30; ++ScopedStep)
    {
        ScopedPool.Step(1.0f / 60.0f, ScopedSettings);
    }
    const Fby::Navigation::CrowdAgent* ScopedFirst = ScopedPool.At(0);
    const Fby::Navigation::CrowdAgent* ScopedSecond = ScopedPool.At(1);
    const float ScopedDistance = (ScopedSecond->Position - ScopedFirst->Position).LengthSquared();
    Check(ScopedDistance > 1.0f, "separation pushes the agents apart");
}

void TestNeighbourCap()
{
    Fby::Navigation::CrowdPool ScopedPool;
    Check(ScopedPool.Configure(64), "configure the cap pool");
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < 40; ++ScopedIndex)
    {
        const float ScopedOffset = static_cast<float>(ScopedIndex % 8) * 1.5f;
        (void)ScopedPool.Add(Fby::Vector2D{ScopedOffset, static_cast<float>(ScopedIndex / 8) * 1.5f}, 0,
                             Fby::Vector2D{100.0f, 100.0f});
    }
    Fby::Navigation::CrowdSettings ScopedSettings = MakeSettings();
    ScopedSettings.MaxNeighbours = 8;
    ScopedPool.Step(1.0f / 60.0f, ScopedSettings);
    bool ScopedCapHolds = true;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedPool.Count(); ++ScopedIndex)
    {
        if (ScopedPool.NeighbourCount(ScopedIndex) > 8)
        {
            ScopedCapHolds = false;
        }
    }
    Check(ScopedCapHolds, "the neighbour cap holds for every agent");
}

void TestDeterminism()
{
    Fby::Navigation::CrowdPool ScopedFirst;
    Fby::Navigation::CrowdPool ScopedSecond;
    Check(ScopedFirst.Configure(32), "configure the first deterministic pool");
    Check(ScopedSecond.Configure(32), "configure the second deterministic pool");
    const Fby::Navigation::CrowdSettings ScopedSettings = MakeSettings();
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < 24; ++ScopedIndex)
    {
        const Fby::Vector2D ScopedPosition{static_cast<float>(ScopedIndex % 6) * 4.0f,
                                           static_cast<float>(ScopedIndex / 6) * 4.0f};
        (void)ScopedFirst.Add(ScopedPosition, ScopedIndex % 3, Fby::Vector2D{60.0f, 60.0f});
        (void)ScopedSecond.Add(ScopedPosition, ScopedIndex % 3, Fby::Vector2D{60.0f, 60.0f});
    }
    for (std::uint32_t ScopedStep = 0; ScopedStep < 30; ++ScopedStep)
    {
        ScopedFirst.Step(1.0f / 60.0f, ScopedSettings);
        ScopedSecond.Step(1.0f / 60.0f, ScopedSettings);
    }
    bool ScopedMatches = true;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedFirst.Count(); ++ScopedIndex)
    {
        if (ScopedFirst.At(ScopedIndex)->Position.X != ScopedSecond.At(ScopedIndex)->Position.X ||
            ScopedFirst.At(ScopedIndex)->Position.Y != ScopedSecond.At(ScopedIndex)->Position.Y)
        {
            ScopedMatches = false;
        }
    }
    Check(ScopedMatches, "the same setup produces the same crowd");
}

void TestFlowSteering()
{
    // 16x16 field with an 8-unit cell, a wall along y=8 and a single gap at x=8.
    constexpr std::uint32_t kSide = 16;
    Fby::Navigation::FlowFieldDesc ScopedFieldDesc;
    ScopedFieldDesc.Width = kSide;
    ScopedFieldDesc.Height = kSide;
    Fby::Navigation::FlowField ScopedField;
    Check(ScopedField.Configure(ScopedFieldDesc), "configure the steering field");
    for (std::uint32_t ScopedX = 0; ScopedX < kSide; ++ScopedX)
    {
        if (ScopedX != 8)
        {
            Check(ScopedField.SetCost(ScopedX, 8, 0), "steering wall cell is blocked");
        }
    }
    const std::uint32_t ScopedGoalCell = 0;
    ScopedField.Build(std::span<const std::uint32_t>(&ScopedGoalCell, 1));

    Fby::Navigation::CrowdPool ScopedPool;
    Check(ScopedPool.Configure(4), "configure the steering pool");
    const Fby::Vector2D ScopedGoal{4.0f, 4.0f};
    const std::uint32_t ScopedIndex =
        ScopedPool.Add(Fby::Vector2D{68.0f, 124.0f}, 0, ScopedGoal, 80.0f);
    Fby::Navigation::CrowdSettings ScopedSettings = MakeSettings();
    ScopedSettings.Field = &ScopedField;
    ScopedSettings.FieldOrigin = Fby::Vector2D{0.0f, 0.0f};
    ScopedSettings.FieldCellSize = 8.0f;
    bool ScopedCrossed = false;
    bool ScopedThroughGap = false;
    float ScopedPreviousY = ScopedPool.At(ScopedIndex)->Position.Y;
    for (std::uint32_t ScopedStep = 0; ScopedStep < 240; ++ScopedStep)
    {
        ScopedPool.Step(1.0f / 60.0f, ScopedSettings);
        const Fby::Navigation::CrowdAgent* ScopedCurrent = ScopedPool.At(ScopedIndex);
        if (!ScopedCrossed && ScopedPreviousY > 64.0f && ScopedCurrent->Position.Y <= 64.0f)
        {
            ScopedCrossed = true;
            ScopedThroughGap = ScopedCurrent->Position.X > 56.0f && ScopedCurrent->Position.X < 80.0f;
        }
        ScopedPreviousY = ScopedCurrent->Position.Y;
    }
    Check(ScopedCrossed, "the agent crosses the wall line");
    Check(ScopedThroughGap, "the crossing happens through the gap");
    Check(ScopedPool.At(ScopedIndex)->Position.Y < 64.0f, "the agent ends beyond the wall");
}

void TestGroupTarget()
{
    Fby::Navigation::CrowdPool ScopedPool;
    Check(ScopedPool.Configure(8), "configure the group pool");
    (void)ScopedPool.Add(Fby::Vector2D{0.0f, 0.0f}, 0, Fby::Vector2D{10.0f, 0.0f});
    (void)ScopedPool.Add(Fby::Vector2D{0.0f, 0.0f}, 1, Fby::Vector2D{10.0f, 0.0f});
    (void)ScopedPool.Add(Fby::Vector2D{0.0f, 0.0f}, 0, Fby::Vector2D{10.0f, 0.0f});
    const std::uint32_t ScopedUpdated = ScopedPool.SetGroupTarget(0, Fby::Vector2D{50.0f, 60.0f});
    Check(ScopedUpdated == 2, "only the group members are updated");
    Check(ScopedPool.At(0)->Target.X == 50.0f && ScopedPool.At(0)->Target.Y == 60.0f, "group zero takes the order");
    Check(ScopedPool.At(1)->Target.X == 10.0f, "group one keeps its own target");
    Check(ScopedPool.At(2)->Target.X == 50.0f, "both group zero agents take the order");
}
}

int main()
{
    TestSeek();
    TestSeparation();
    TestNeighbourCap();
    TestDeterminism();
    TestFlowSteering();
    TestGroupTarget();
    if (GFailures != 0)
    {
        std::fprintf(stderr, "%d crowd pool test(s) failed\n", GFailures);
        return 1;
    }
    std::printf("crowd_pool: OK\n");
    return 0;
}
