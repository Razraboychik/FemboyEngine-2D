// FemboyEngine2D - integer flow field implementation.
#include "Engine/Navigation/FlowField.hpp"

#include <algorithm>
#include <cmath>

namespace Fby::Navigation
{

bool FlowField::Configure(const FlowFieldDesc& InDesc) noexcept
{
    if (InDesc.Width == 0 || InDesc.Height == 0 || InDesc.Width > kFlowFieldMaxSide ||
        InDesc.Height > kFlowFieldMaxSide)
    {
        return false;
    }
    MDesc = InDesc;
    const std::size_t ScopedCells = static_cast<std::size_t>(InDesc.Width) * InDesc.Height;
    MCosts.assign(ScopedCells, InDesc.DefaultCost);
    MPotential.assign(ScopedCells, kFlowFieldInfinite);
    MHeap.reserve(ScopedCells);
    MReachableCount = 0;
    MGoalCount = 0;
    return true;
}

void FlowField::Clear(std::uint8_t InCost) noexcept
{
    std::fill(MCosts.begin(), MCosts.end(), InCost);
    std::fill(MPotential.begin(), MPotential.end(), kFlowFieldInfinite);
    MHeap.clear();
    MReachableCount = 0;
    MGoalCount = 0;
}

bool FlowField::SetCost(std::uint32_t InX, std::uint32_t InY, std::uint8_t InCost) noexcept
{
    if (!IsConfigured() || InX >= MDesc.Width || InY >= MDesc.Height)
    {
        return false;
    }
    MCosts[static_cast<std::size_t>(InY) * MDesc.Width + InX] = InCost;
    return true;
}

std::uint8_t FlowField::CostAt(std::uint32_t InX, std::uint32_t InY) const noexcept
{
    if (!IsConfigured() || InX >= MDesc.Width || InY >= MDesc.Height)
    {
        return 0;
    }
    return MCosts[static_cast<std::size_t>(InY) * MDesc.Width + InX];
}

void FlowField::Build(std::span<const std::uint32_t> InGoals) noexcept
{
    if (!IsConfigured())
    {
        return;
    }
    std::fill(MPotential.begin(), MPotential.end(), kFlowFieldInfinite);
    MHeap.clear();
    MReachableCount = 0;
    MGoalCount = 0;

    const auto ScopedLess = [](const Entry& InLeft, const Entry& InRight)
    {
        // Min-heap with a stable tie-break on the cell index.
        if (InLeft.Distance != InRight.Distance)
        {
            return InLeft.Distance > InRight.Distance;
        }
        return InLeft.Index > InRight.Index;
    };

    for (const std::uint32_t ScopedIndex : InGoals)
    {
        if (ScopedIndex >= MCosts.size() || MCosts[ScopedIndex] == 0)
        {
            continue;
        }
        if (MPotential[ScopedIndex] == 0)
        {
            continue;
        }
        MPotential[ScopedIndex] = 0;
        ++MGoalCount;
        MHeap.push_back(Entry{0, ScopedIndex});
        std::push_heap(MHeap.begin(), MHeap.end(), ScopedLess);
    }

    const std::uint32_t ScopedWidth = MDesc.Width;
    const std::uint32_t ScopedHeight = MDesc.Height;
    while (!MHeap.empty())
    {
        std::pop_heap(MHeap.begin(), MHeap.end(), ScopedLess);
        const Entry ScopedCurrent = MHeap.back();
        MHeap.pop_back();
        if (ScopedCurrent.Distance > MPotential[ScopedCurrent.Index])
        {
            continue;
        }
        const std::uint32_t ScopedX = ScopedCurrent.Index % ScopedWidth;
        const std::uint32_t ScopedY = ScopedCurrent.Index / ScopedWidth;
        for (std::int32_t ScopedDy = -1; ScopedDy <= 1; ++ScopedDy)
        {
            for (std::int32_t ScopedDx = -1; ScopedDx <= 1; ++ScopedDx)
            {
                if (ScopedDx == 0 && ScopedDy == 0)
                {
                    continue;
                }
                const std::int32_t ScopedNeighborX = static_cast<std::int32_t>(ScopedX) + ScopedDx;
                const std::int32_t ScopedNeighborY = static_cast<std::int32_t>(ScopedY) + ScopedDy;
                if (ScopedNeighborX < 0 || ScopedNeighborY < 0 ||
                    ScopedNeighborX >= static_cast<std::int32_t>(ScopedWidth) ||
                    ScopedNeighborY >= static_cast<std::int32_t>(ScopedHeight))
                {
                    continue;
                }
                const std::uint32_t ScopedNeighborIndex =
                    static_cast<std::uint32_t>(ScopedNeighborY) * ScopedWidth +
                    static_cast<std::uint32_t>(ScopedNeighborX);
                const std::uint8_t ScopedCost = MCosts[ScopedNeighborIndex];
                if (ScopedCost == 0)
                {
                    continue;
                }
                const bool ScopedDiagonal = ScopedDx != 0 && ScopedDy != 0;
                const std::uint32_t ScopedStep = ScopedDiagonal ? kFlowStepDiagonal : kFlowStepStraight;
                const std::uint32_t ScopedNewDistance =
                    ScopedCurrent.Distance + ScopedStep * static_cast<std::uint32_t>(ScopedCost);
                if (ScopedNewDistance >= MPotential[ScopedNeighborIndex])
                {
                    continue;
                }
                MPotential[ScopedNeighborIndex] = ScopedNewDistance;
                MHeap.push_back(Entry{ScopedNewDistance, ScopedNeighborIndex});
                std::push_heap(MHeap.begin(), MHeap.end(), ScopedLess);
            }
        }
    }

    for (const std::uint32_t ScopedPotential : MPotential)
    {
        if (ScopedPotential != kFlowFieldInfinite)
        {
            ++MReachableCount;
        }
    }
}

std::uint32_t FlowField::PotentialAt(std::uint32_t InX, std::uint32_t InY) const noexcept
{
    if (!IsConfigured() || InX >= MDesc.Width || InY >= MDesc.Height)
    {
        return kFlowFieldInfinite;
    }
    return MPotential[static_cast<std::size_t>(InY) * MDesc.Width + InX];
}

bool FlowField::IsReachable(std::uint32_t InX, std::uint32_t InY) const noexcept
{
    return PotentialAt(InX, InY) != kFlowFieldInfinite;
}

Vector2D FlowField::FlowAt(std::uint32_t InX, std::uint32_t InY) const noexcept
{
    const std::uint32_t ScopedCurrent = PotentialAt(InX, InY);
    if (ScopedCurrent == kFlowFieldInfinite || ScopedCurrent == 0)
    {
        return Vector2D{0.0f, 0.0f};
    }
    std::uint32_t ScopedBest = kFlowFieldInfinite;
    std::int32_t ScopedBestX = 0;
    std::int32_t ScopedBestY = 0;
    for (std::int32_t ScopedDy = -1; ScopedDy <= 1; ++ScopedDy)
    {
        for (std::int32_t ScopedDx = -1; ScopedDx <= 1; ++ScopedDx)
        {
            if (ScopedDx == 0 && ScopedDy == 0)
            {
                continue;
            }
            const std::int32_t ScopedNeighborX = static_cast<std::int32_t>(InX) + ScopedDx;
            const std::int32_t ScopedNeighborY = static_cast<std::int32_t>(InY) + ScopedDy;
            if (ScopedNeighborX < 0 || ScopedNeighborY < 0 ||
                ScopedNeighborX >= static_cast<std::int32_t>(MDesc.Width) ||
                ScopedNeighborY >= static_cast<std::int32_t>(MDesc.Height))
            {
                continue;
            }
            const std::uint32_t ScopedNeighbor = PotentialAt(static_cast<std::uint32_t>(ScopedNeighborX),
                                                             static_cast<std::uint32_t>(ScopedNeighborY));
            if (ScopedNeighbor != kFlowFieldInfinite && ScopedNeighbor < ScopedBest)
            {
                ScopedBest = ScopedNeighbor;
                ScopedBestX = ScopedDx;
                ScopedBestY = ScopedDy;
            }
        }
    }
    if (ScopedBest == kFlowFieldInfinite)
    {
        return Vector2D{0.0f, 0.0f};
    }
    const float ScopedLength = std::sqrt(static_cast<float>(ScopedBestX * ScopedBestX + ScopedBestY * ScopedBestY));
    if (ScopedLength <= 0.0f)
    {
        return Vector2D{0.0f, 0.0f};
    }
    return Vector2D{static_cast<float>(ScopedBestX) / ScopedLength, static_cast<float>(ScopedBestY) / ScopedLength};
}

bool FlowField::BakeCostsFromField(const SdfField& InField, float InAgentRadius, std::uint8_t InNearCost) noexcept
{
    if (!IsConfigured() || !InField.Valid() || InField.width != MDesc.Width || InField.height != MDesc.Height)
    {
        return false;
    }
    const float ScopedNearBand = std::max(InAgentRadius, 0.0f) + std::max(InField.cellSize.X, InField.cellSize.Y);
    for (std::uint32_t ScopedY = 0; ScopedY < MDesc.Height; ++ScopedY)
    {
        for (std::uint32_t ScopedX = 0; ScopedX < MDesc.Width; ++ScopedX)
        {
            const std::size_t ScopedIndex = static_cast<std::size_t>(ScopedY) * MDesc.Width + ScopedX;
            const float ScopedDistance = InField.samples[ScopedIndex];
            if (ScopedDistance <= InAgentRadius)
            {
                MCosts[ScopedIndex] = 0;
            }
            else if (ScopedDistance <= ScopedNearBand)
            {
                MCosts[ScopedIndex] = InNearCost;
            }
            else
            {
                MCosts[ScopedIndex] = 1;
            }
        }
    }
    return true;
}

}
