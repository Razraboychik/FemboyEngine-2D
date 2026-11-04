// FemboyEngine2D - integer flow field for paged navigation.
#pragma once

#include "Engine/Core/Types.hpp"
#include "Engine/Physics/SdfCollision.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace Fby::Navigation
{

// Upper bound for one field; a page is 64x64, a region is at most 512x512.
inline constexpr std::uint32_t kFlowFieldMaxSide = 512;
// Sentinel potential for a cell the goal cannot reach.
inline constexpr std::uint32_t kFlowFieldInfinite = 0xFFFFFFFFu;
// Straight and diagonal step weights in tenths, so all math stays integer.
inline constexpr std::uint32_t kFlowStepStraight = 10;
inline constexpr std::uint32_t kFlowStepDiagonal = 14;

struct FlowFieldDesc
{
    std::uint32_t Width = 0;
    std::uint32_t Height = 0;
    // Cost assigned by Clear: 0 blocks, 1..255 is the traversal cost of entering.
    std::uint8_t DefaultCost = 1;
};

// Multi-source Dijkstra potential over a cost grid. Cells also carry the cost of
// entering them, so rough terrain simply costs more. The field is deterministic:
// ties resolve by cell index.
class FlowField
{
public:
    [[nodiscard]] bool Configure(const FlowFieldDesc& InDesc) noexcept;
    [[nodiscard]] bool IsConfigured() const noexcept
    {
        return MDesc.Width > 0 && MDesc.Height > 0 && !MCosts.empty();
    }
    [[nodiscard]] std::uint32_t Width() const noexcept
    {
        return MDesc.Width;
    }
    [[nodiscard]] std::uint32_t Height() const noexcept
    {
        return MDesc.Height;
    }

    void Clear(std::uint8_t InCost = 1) noexcept;
    [[nodiscard]] bool SetCost(std::uint32_t InX, std::uint32_t InY, std::uint8_t InCost) noexcept;
    [[nodiscard]] std::uint8_t CostAt(std::uint32_t InX, std::uint32_t InY) const noexcept;

    // Builds the potential from every goal cell (flat indices). A blocked goal is
    // ignored. Unreachable cells keep kFlowFieldInfinite.
    void Build(std::span<const std::uint32_t> InGoals) noexcept;

    [[nodiscard]] std::uint32_t PotentialAt(std::uint32_t InX, std::uint32_t InY) const noexcept;
    [[nodiscard]] bool IsReachable(std::uint32_t InX, std::uint32_t InY) const noexcept;
    // Unit direction to the cheapest neighbour, or zero at the goal and on
    // unreachable cells.
    [[nodiscard]] Vector2D FlowAt(std::uint32_t InX, std::uint32_t InY) const noexcept;
    [[nodiscard]] std::uint32_t ReachableCount() const noexcept
    {
        return MReachableCount;
    }
    [[nodiscard]] std::uint32_t GoalCount() const noexcept
    {
        return MGoalCount;
    }

    // Derives the costs from a world distance field whose grid matches the flow
    // field: solid cells and cells closer than the agent radius become walls,
    // cells inside the narrow band cost more, the rest stays passable.
    [[nodiscard]] bool BakeCostsFromField(const SdfField& InField, float InAgentRadius,
                                          std::uint8_t InNearCost = 4) noexcept;

private:
    struct Entry
    {
        std::uint32_t Distance = 0;
        std::uint32_t Index = 0;
    };

    FlowFieldDesc MDesc{};
    std::vector<std::uint8_t> MCosts;
    std::vector<std::uint32_t> MPotential;
    std::vector<Entry> MHeap;
    std::uint32_t MReachableCount = 0;
    std::uint32_t MGoalCount = 0;
};

}
