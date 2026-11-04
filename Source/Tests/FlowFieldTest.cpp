// FemboyEngine2D - flow field tests.
#include "Engine/Navigation/FlowField.hpp"

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

[[nodiscard]] Fby::Navigation::FlowFieldDesc MakeDesc(std::uint32_t InSide)
{
    Fby::Navigation::FlowFieldDesc ScopedDesc;
    ScopedDesc.Width = InSide;
    ScopedDesc.Height = InSide;
    ScopedDesc.DefaultCost = 1;
    return ScopedDesc;
}

void TestOpenField()
{
    Fby::Navigation::FlowField ScopedField;
    Check(ScopedField.Configure(MakeDesc(16)), "configure an open field");
    const std::uint32_t ScopedGoal = 0;
    ScopedField.Build(std::span<const std::uint32_t>(&ScopedGoal, 1));
    Check(ScopedField.GoalCount() == 1, "one goal is accepted");
    Check(ScopedField.PotentialAt(0, 0) == 0, "the goal has zero potential");
    Check(ScopedField.PotentialAt(15, 15) == 14u * 15u, "the diagonal potential is exact");
    Check(ScopedField.PotentialAt(15, 0) == 10u * 15u, "the straight potential is exact");
    Check(ScopedField.ReachableCount() == 16u * 16u, "the open field is fully reachable");

    const Fby::Vector2D ScopedFlow = ScopedField.FlowAt(1, 1);
    Check(ScopedFlow.X < -0.6f && ScopedFlow.Y < -0.6f, "the flow points at the goal");
    const Fby::Vector2D ScopedGoalFlow = ScopedField.FlowAt(0, 0);
    Check(ScopedGoalFlow.X == 0.0f && ScopedGoalFlow.Y == 0.0f, "the goal has no flow");
}

void TestWalls()
{
    Fby::Navigation::FlowField ScopedField;
    Check(ScopedField.Configure(MakeDesc(16)), "configure a walled field");
    for (std::uint32_t ScopedX = 0; ScopedX < 16; ++ScopedX)
    {
        if (ScopedX != 8)
        {
            Check(ScopedField.SetCost(ScopedX, 8, 0), "wall cell is blocked");
        }
    }
    const std::uint32_t ScopedGoal = 0;
    ScopedField.Build(std::span<const std::uint32_t>(&ScopedGoal, 1));
    Check(ScopedField.IsReachable(8, 8), "the gap stays reachable");
    Check(ScopedField.IsReachable(8, 15), "the far side is reachable through the gap");
    const Fby::Vector2D ScopedFarFlow = ScopedField.FlowAt(8, 15);
    Check(ScopedFarFlow.LengthSquared() > 0.5f, "the far side has a flow");

    // A full wall makes everything behind it unreachable.
    Fby::Navigation::FlowField ScopedSealed;
    Check(ScopedSealed.Configure(MakeDesc(16)), "configure the sealed field");
    for (std::uint32_t ScopedX = 0; ScopedX < 16; ++ScopedX)
    {
        Check(ScopedSealed.SetCost(ScopedX, 8, 0), "sealing wall cell is blocked");
    }
    ScopedSealed.Build(std::span<const std::uint32_t>(&ScopedGoal, 1));
    Check(!ScopedSealed.IsReachable(8, 15), "the sealed side is unreachable");
    Check(ScopedSealed.FlowAt(8, 15).LengthSquared() == 0.0f, "unreachable cells have no flow");
}

void TestCosts()
{
    Fby::Navigation::FlowField ScopedField;
    Check(ScopedField.Configure(MakeDesc(16)), "configure the cost field");
    // A rough strip on the straight path makes the diagonal cheaper.
    for (std::uint32_t ScopedX = 1; ScopedX < 15; ++ScopedX)
    {
        Check(ScopedField.SetCost(ScopedX, 0, 200), "rough cell is written");
    }
    const std::uint32_t ScopedGoal = 0;
    ScopedField.Build(std::span<const std::uint32_t>(&ScopedGoal, 1));
    Check(ScopedField.PotentialAt(15, 0) > 10u * 15u, "rough ground raises the potential");
    Check(ScopedField.IsReachable(15, 1), "the off-strip cell stays reachable");
}

void TestSdfCosts()
{
    // A vertical wall in a distance field: the flow field must route around it.
    constexpr std::uint32_t kSide = 16;
    std::vector<float> ScopedSamples(static_cast<std::size_t>(kSide) * kSide, 100.0f);
    for (std::uint32_t ScopedY = 0; ScopedY < kSide; ++ScopedY)
    {
        for (std::uint32_t ScopedX = 0; ScopedX < kSide; ++ScopedX)
        {
            const float ScopedDx = std::abs(static_cast<float>(ScopedX) - 8.0f) - 1.0f;
            const float ScopedDy = std::abs(static_cast<float>(ScopedY) - 8.0f) - 6.0f;
            const float ScopedOutsideX = std::max(ScopedDx, 0.0f);
            const float ScopedOutsideY = std::max(ScopedDy, 0.0f);
            ScopedSamples[static_cast<std::size_t>(ScopedY) * kSide + ScopedX] =
                std::sqrt(ScopedOutsideX * ScopedOutsideX + ScopedOutsideY * ScopedOutsideY) +
                std::min(std::max(ScopedDx, ScopedDy), 0.0f);
        }
    }
    Fby::SdfField ScopedField;
    ScopedField.samples = ScopedSamples.data();
    ScopedField.width = kSide;
    ScopedField.height = kSide;
    ScopedField.cellSize = Fby::Vector2D{1.0f, 1.0f};
    ScopedField.outsideDistance = 100.0f;

    Fby::Navigation::FlowField ScopedFlow;
    Check(ScopedFlow.Configure(MakeDesc(kSide)), "configure the SDF-cost field");
    Check(ScopedFlow.BakeCostsFromField(ScopedField, 0.25f), "costs bake from the distance field");
    Check(ScopedFlow.CostAt(8, 8) == 0, "the wall centre is blocked");
    Check(ScopedFlow.CostAt(8, 1) > 0, "just above the wall is passable");
    Check(ScopedFlow.CostAt(0, 0) == 1, "far cells keep the base cost");

    const std::uint32_t ScopedGoal = 0;
    ScopedFlow.Build(std::span<const std::uint32_t>(&ScopedGoal, 1));
    Check(ScopedFlow.IsReachable(15, 0), "the goal row is reachable");
    Check(ScopedFlow.IsReachable(15, 15), "the far side is reachable around the wall");
    Check(!ScopedFlow.IsReachable(8, 8), "the wall itself is unreachable");
}
}

int main()
{
    TestOpenField();
    TestWalls();
    TestCosts();
    TestSdfCosts();
    if (GFailures != 0)
    {
        std::fprintf(stderr, "%d flow field test(s) failed\n", GFailures);
        return 1;
    }
    std::printf("flow_field: OK\n");
    return 0;
}
