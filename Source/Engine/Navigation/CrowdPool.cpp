// FemboyEngine2D - crowd pool implementation.
#include "Engine/Navigation/CrowdPool.hpp"

#include <algorithm>
#include <cmath>

namespace Fby::Navigation
{
namespace
{

constexpr std::uint32_t kInvalidAgent = 0xFFFFFFFFu;
// Hard ceiling for the per-agent neighbour list; the settings cap is clamped to it.
constexpr std::uint32_t kMaxNeighbourSlots = 32;

[[nodiscard]] Vector2D NormalizeOrZero(Vector2D InValue) noexcept
{
    const float ScopedLengthSquared = InValue.LengthSquared();
    if (ScopedLengthSquared <= 1.0e-12f)
    {
        return Vector2D{0.0f, 0.0f};
    }
    const float ScopedInverse = 1.0f / std::sqrt(ScopedLengthSquared);
    return Vector2D{InValue.X * ScopedInverse, InValue.Y * ScopedInverse};
}

}

bool CrowdPool::Configure(std::uint32_t InCapacity, float InCellSize) noexcept
{
    if (InCapacity == 0)
    {
        return false;
    }
    const std::uint32_t ScopedCapacity = std::min(InCapacity, kMaxAgents);
    MAgents.assign(ScopedCapacity, CrowdAgent{});
    MSteer.assign(ScopedCapacity, Vector2D{});
    MNeighbourCounts.assign(ScopedCapacity, 0);
    MCount = 0;
    MCellSize = InCellSize > 0.0f ? InCellSize : 24.0f;
    return true;
}

void CrowdPool::Clear() noexcept
{
    MCount = 0;
    std::fill(MNeighbourCounts.begin(), MNeighbourCounts.end(), 0);
}

std::uint32_t CrowdPool::Add(Vector2D InPosition, std::uint32_t InGroup, Vector2D InTarget,
                             float InMaxSpeed) noexcept
{
    if (MCount >= MAgents.size())
    {
        return kInvalidAgent;
    }
    CrowdAgent& ScopedAgent = MAgents[MCount];
    ScopedAgent.Position = InPosition;
    ScopedAgent.Velocity = Vector2D{0.0f, 0.0f};
    ScopedAgent.Target = InTarget;
    ScopedAgent.Group = InGroup;
    ScopedAgent.MaxSpeed = InMaxSpeed > 0.0f ? InMaxSpeed : 80.0f;
    ScopedAgent.WanderPhase = static_cast<float>(MCount) * 0.37f;
    ScopedAgent.Flags = 0;
    return MCount++;
}

CrowdAgent* CrowdPool::At(std::uint32_t InIndex) noexcept
{
    return InIndex < MCount ? &MAgents[InIndex] : nullptr;
}

const CrowdAgent* CrowdPool::At(std::uint32_t InIndex) const noexcept
{
    return InIndex < MCount ? &MAgents[InIndex] : nullptr;
}

void CrowdPool::SetTarget(std::uint32_t InIndex, Vector2D InTarget) noexcept
{
    if (InIndex < MCount)
    {
        MAgents[InIndex].Target = InTarget;
    }
}

std::uint32_t CrowdPool::SetGroupTarget(std::uint32_t InGroup, Vector2D InTarget) noexcept
{
    std::uint32_t ScopedUpdated = 0;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < MCount; ++ScopedIndex)
    {
        if (MAgents[ScopedIndex].Group == InGroup)
        {
            MAgents[ScopedIndex].Target = InTarget;
            ++ScopedUpdated;
        }
    }
    return ScopedUpdated;
}

std::uint32_t CrowdPool::NeighbourCount(std::uint32_t InIndex) const noexcept
{
    return InIndex < MCount ? MNeighbourCounts[InIndex] : 0;
}

void CrowdPool::RebuildGrid() noexcept
{
    if (MCount == 0)
    {
        MGridWidth = 0;
        MGridHeight = 0;
        return;
    }
    MCellSize = std::max(MCellSize, 1.0f);
    Vector2D ScopedMin = MAgents[0].Position;
    Vector2D ScopedMax = MAgents[0].Position;
    for (std::uint32_t ScopedIndex = 1; ScopedIndex < MCount; ++ScopedIndex)
    {
        ScopedMin.X = std::min(ScopedMin.X, MAgents[ScopedIndex].Position.X);
        ScopedMin.Y = std::min(ScopedMin.Y, MAgents[ScopedIndex].Position.Y);
        ScopedMax.X = std::max(ScopedMax.X, MAgents[ScopedIndex].Position.X);
        ScopedMax.Y = std::max(ScopedMax.Y, MAgents[ScopedIndex].Position.Y);
    }
    // One cell of padding so a neighbour outside the bounds still lands in range.
    ScopedMin = Vector2D{ScopedMin.X - MCellSize, ScopedMin.Y - MCellSize};
    const std::uint32_t ScopedWidth =
        static_cast<std::uint32_t>((ScopedMax.X - ScopedMin.X) / MCellSize) + 3u;
    const std::uint32_t ScopedHeight =
        static_cast<std::uint32_t>((ScopedMax.Y - ScopedMin.Y) / MCellSize) + 3u;
    MGridOrigin = ScopedMin;
    MGridWidth = std::min(ScopedWidth, 4096u);
    MGridHeight = std::min(ScopedHeight, 4096u);
    const std::size_t ScopedCells = static_cast<std::size_t>(MGridWidth) * MGridHeight;
    MCellStart.assign(ScopedCells + 1, 0);
    MCellCount.assign(ScopedCells, 0);
    MSorted.assign(MCount, 0);

    for (std::uint32_t ScopedIndex = 0; ScopedIndex < MCount; ++ScopedIndex)
    {
        const Vector2D ScopedPosition = MAgents[ScopedIndex].Position;
        const std::uint32_t ScopedX = std::min(
            static_cast<std::uint32_t>((ScopedPosition.X - MGridOrigin.X) / MCellSize), MGridWidth - 1u);
        const std::uint32_t ScopedY = std::min(
            static_cast<std::uint32_t>((ScopedPosition.Y - MGridOrigin.Y) / MCellSize), MGridHeight - 1u);
        ++MCellCount[CellOf(ScopedX, ScopedY)];
    }
    for (std::size_t ScopedIndex = 0; ScopedIndex < ScopedCells; ++ScopedIndex)
    {
        MCellStart[ScopedIndex + 1] = MCellStart[ScopedIndex] + MCellCount[ScopedIndex];
    }
    MCellCursor.assign(MCellStart.begin(), MCellStart.end() - 1);
    // Scatter in agent index order, so the sorted array is deterministic.
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < MCount; ++ScopedIndex)
    {
        const Vector2D ScopedPosition = MAgents[ScopedIndex].Position;
        const std::uint32_t ScopedX = std::min(
            static_cast<std::uint32_t>((ScopedPosition.X - MGridOrigin.X) / MCellSize), MGridWidth - 1u);
        const std::uint32_t ScopedY = std::min(
            static_cast<std::uint32_t>((ScopedPosition.Y - MGridOrigin.Y) / MCellSize), MGridHeight - 1u);
        const std::uint32_t ScopedCell = CellOf(ScopedX, ScopedY);
        MSorted[MCellCursor[ScopedCell]++] = ScopedIndex;
    }
}

void CrowdPool::Step(float InDt, const CrowdSettings& InSettings) noexcept
{
    if (MCount == 0 || InDt <= 0.0f)
    {
        return;
    }
    MCellSize = std::max(InSettings.PerceptionRadius, 1.0f);
    RebuildGrid();
    const std::uint32_t ScopedNeighbourCap =
        std::min(std::max(InSettings.MaxNeighbours, 1u), kMaxNeighbourSlots);

    // Pass one computes steering from the previous state only.
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < MCount; ++ScopedIndex)
    {
        const CrowdAgent& ScopedAgent = MAgents[ScopedIndex];
        const std::uint32_t ScopedCellX = std::min(
            static_cast<std::uint32_t>((ScopedAgent.Position.X - MGridOrigin.X) / MCellSize), MGridWidth - 1u);
        const std::uint32_t ScopedCellY = std::min(
            static_cast<std::uint32_t>((ScopedAgent.Position.Y - MGridOrigin.Y) / MCellSize), MGridHeight - 1u);
        const float ScopedRadius = InSettings.PerceptionRadius;
        const float ScopedRadiusSquared = ScopedRadius * ScopedRadius;
        Vector2D ScopedSeparation{0.0f, 0.0f};
        Vector2D ScopedCohesion{0.0f, 0.0f};
        Vector2D ScopedAlignment{0.0f, 0.0f};
        std::uint32_t ScopedNeighbours = 0;
        for (std::int32_t ScopedDy = -1; ScopedDy <= 1 && ScopedNeighbours < ScopedNeighbourCap; ++ScopedDy)
        {
            for (std::int32_t ScopedDx = -1; ScopedDx <= 1 && ScopedNeighbours < ScopedNeighbourCap; ++ScopedDx)
            {
                const std::int32_t ScopedNeighborX = static_cast<std::int32_t>(ScopedCellX) + ScopedDx;
                const std::int32_t ScopedNeighborY = static_cast<std::int32_t>(ScopedCellY) + ScopedDy;
                if (ScopedNeighborX < 0 || ScopedNeighborY < 0 ||
                    ScopedNeighborX >= static_cast<std::int32_t>(MGridWidth) ||
                    ScopedNeighborY >= static_cast<std::int32_t>(MGridHeight))
                {
                    continue;
                }
                const std::uint32_t ScopedCell = CellOf(static_cast<std::uint32_t>(ScopedNeighborX),
                                                        static_cast<std::uint32_t>(ScopedNeighborY));
                const std::uint32_t ScopedBegin = MCellStart[ScopedCell];
                const std::uint32_t ScopedEnd = MCellStart[ScopedCell + 1];
                for (std::uint32_t ScopedCursor = ScopedBegin;
                     ScopedCursor < ScopedEnd && ScopedNeighbours < ScopedNeighbourCap; ++ScopedCursor)
                {
                    const std::uint32_t ScopedOther = MSorted[ScopedCursor];
                    if (ScopedOther == ScopedIndex)
                    {
                        continue;
                    }
                    const Vector2D ScopedDelta = MAgents[ScopedOther].Position - ScopedAgent.Position;
                    const float ScopedDistanceSquared = ScopedDelta.LengthSquared();
                    if (ScopedDistanceSquared > ScopedRadiusSquared || ScopedDistanceSquared <= 1.0e-9f)
                    {
                        continue;
                    }
                    const float ScopedDistance = std::sqrt(ScopedDistanceSquared);
                    ScopedSeparation -= ScopedDelta * (1.0f / (ScopedDistance * ScopedDistance));
                    ScopedCohesion += MAgents[ScopedOther].Position;
                    ScopedAlignment += MAgents[ScopedOther].Velocity;
                    ++ScopedNeighbours;
                }
            }
        }
        MNeighbourCounts[ScopedIndex] = ScopedNeighbours;
        Vector2D ScopedSteering{0.0f, 0.0f};
        if (ScopedNeighbours > 0)
        {
            const float ScopedInverse = 1.0f / static_cast<float>(ScopedNeighbours);
            ScopedCohesion = ScopedCohesion * ScopedInverse - ScopedAgent.Position;
            ScopedAlignment = ScopedAlignment * ScopedInverse - ScopedAgent.Velocity;
            ScopedSteering += ScopedSeparation * InSettings.SeparationWeight;
            ScopedSteering += ScopedCohesion * InSettings.CohesionWeight;
            ScopedSteering += ScopedAlignment * InSettings.AlignmentWeight;
        }
        // Flow steering when a field is attached; the target direction is the
        // fallback on unreachable cells, at the goal and without a field.
        Vector2D ScopedDesired{0.0f, 0.0f};
        bool ScopedUsesFlow = false;
        if (InSettings.Field != nullptr && InSettings.FieldCellSize > 0.0f)
        {
            const std::int32_t ScopedCellX = static_cast<std::int32_t>(
                std::floor((ScopedAgent.Position.X - InSettings.FieldOrigin.X) / InSettings.FieldCellSize));
            const std::int32_t ScopedCellY = static_cast<std::int32_t>(
                std::floor((ScopedAgent.Position.Y - InSettings.FieldOrigin.Y) / InSettings.FieldCellSize));
            if (ScopedCellX >= 0 && ScopedCellY >= 0 &&
                ScopedCellX < static_cast<std::int32_t>(InSettings.Field->Width()) &&
                ScopedCellY < static_cast<std::int32_t>(InSettings.Field->Height()))
            {
                const Vector2D ScopedFlow =
                    InSettings.Field->FlowAt(static_cast<std::uint32_t>(ScopedCellX),
                                             static_cast<std::uint32_t>(ScopedCellY));
                if (ScopedFlow.LengthSquared() > 0.0f)
                {
                    ScopedDesired = ScopedFlow * ScopedAgent.MaxSpeed;
                    ScopedUsesFlow = true;
                }
            }
        }
        if (!ScopedUsesFlow)
        {
            ScopedDesired = NormalizeOrZero(ScopedAgent.Target - ScopedAgent.Position) * ScopedAgent.MaxSpeed;
        }
        ScopedSteering += (ScopedDesired - ScopedAgent.Velocity) * InSettings.TargetWeight;
        MSteer[ScopedIndex] = ScopedSteering;
    }

    // Pass two integrates every agent from its own steering.
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < MCount; ++ScopedIndex)
    {
        CrowdAgent& ScopedAgent = MAgents[ScopedIndex];
        Vector2D ScopedAcceleration = MSteer[ScopedIndex];
        const float ScopedAccelerationSquared = ScopedAcceleration.LengthSquared();
        const float ScopedLimit = InSettings.Acceleration;
        if (ScopedAccelerationSquared > ScopedLimit * ScopedLimit)
        {
            ScopedAcceleration = NormalizeOrZero(ScopedAcceleration) * ScopedLimit;
        }
        ScopedAgent.Velocity += ScopedAcceleration * InDt;
        const float ScopedSpeedSquared = ScopedAgent.Velocity.LengthSquared();
        if (ScopedSpeedSquared > ScopedAgent.MaxSpeed * ScopedAgent.MaxSpeed)
        {
            ScopedAgent.Velocity = NormalizeOrZero(ScopedAgent.Velocity) * ScopedAgent.MaxSpeed;
        }
        ScopedAgent.Position += ScopedAgent.Velocity * InDt;
        ScopedAgent.WanderPhase += InDt;
    }
}

}
