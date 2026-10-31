// FemboyEngine2D - relevance grid implementation.
#include "Engine/Network/RelevanceGrid.hpp"

#include "Engine/ECS/Components.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace Fby::Net
{

bool RelevanceGrid::Initialize(const RelevanceConfig& InConfig, std::string_view* InError)
{
    const auto ScopedFail = [InError](std::string_view InMessage)
    {
        if (InError != nullptr)
        {
            *InError = InMessage;
        }
        return false;
    };

    if (InConfig.cellSize <= 0.0f)
    {
        return ScopedFail("relevance grid: cellSize must be positive");
    }
    if (InConfig.cellsX == 0 || InConfig.cellsY == 0)
    {
        return ScopedFail("relevance grid: a zero-sized grid has no cells");
    }
    const std::uint64_t ScopedCellCount =
        static_cast<std::uint64_t>(InConfig.cellsX) * static_cast<std::uint64_t>(InConfig.cellsY);
    if (ScopedCellCount == 0 || ScopedCellCount > kMaxRelevanceCells)
    {
        return ScopedFail("relevance grid: cellsX * cellsY exceeds kMaxRelevanceCells");
    }
    if (InConfig.nearRadius < InConfig.neighborRadius || InConfig.farRadius < InConfig.nearRadius)
    {
        return ScopedFail("relevance grid: radii must be neighbor <= near <= far");
    }
    if (InConfig.nearInterval == 0 || InConfig.farInterval == 0)
    {
        return ScopedFail("relevance grid: replication intervals must be non-zero");
    }

    MConfig = InConfig;
    MCells.assign(static_cast<std::size_t>(ScopedCellCount), RelevanceCell{});
    MPlayerPositions.fill(Vector2D{});
    MHasPlayer.fill(false);
    MStats = RelevanceGridStats{};
    MInitialized = true;
    return true;
}

RelevanceGrid::CellCoordinate RelevanceGrid::CellOf(Vector2D InPosition) const noexcept
{
    CellCoordinate ScopedCoordinate;
    if (!MInitialized)
    {
        return ScopedCoordinate;
    }
    const double ScopedGridX = std::floor(static_cast<double>(InPosition.X - MConfig.origin.X) / MConfig.cellSize);
    const double ScopedGridY = std::floor(static_cast<double>(InPosition.Y - MConfig.origin.Y) / MConfig.cellSize);
    if (ScopedGridX < 0.0 || ScopedGridY < 0.0 || ScopedGridX >= static_cast<double>(MConfig.cellsX) ||
        ScopedGridY >= static_cast<double>(MConfig.cellsY))
    {
        return ScopedCoordinate;
    }
    ScopedCoordinate.x = static_cast<std::int32_t>(ScopedGridX);
    ScopedCoordinate.y = static_cast<std::int32_t>(ScopedGridY);
    ScopedCoordinate.valid = true;
    return ScopedCoordinate;
}

const RelevanceCell* RelevanceGrid::CellAt(std::uint32_t InX, std::uint32_t InY) const noexcept
{
    if (!MInitialized || InX >= MConfig.cellsX || InY >= MConfig.cellsY)
    {
        return nullptr;
    }
    return &CellRef(InX, InY);
}

RelevanceBand RelevanceGrid::BandFor(std::uint32_t InDx, std::uint32_t InDy) const noexcept
{
    const std::uint32_t ScopedDistance = InDx > InDy ? InDx : InDy;
    if (ScopedDistance <= MConfig.neighborRadius)
    {
        return RelevanceBand::Immediate;
    }
    if (ScopedDistance <= MConfig.nearRadius)
    {
        return RelevanceBand::Near;
    }
    if (ScopedDistance <= MConfig.farRadius)
    {
        return RelevanceBand::Far;
    }
    return RelevanceBand::None;
}

void RelevanceGrid::Rebuild(Ecs::Registry& InRegistry, const NetEntityMap& InEntities) noexcept
{
    MStats = RelevanceGridStats{};
    if (!MInitialized)
    {
        return;
    }
    for (RelevanceCell& ScopedCell : MCells)
    {
        ScopedCell.count = 0;
        ScopedCell.dropped = 0;
    }

    std::uint32_t ScopedSortCount = 0;
    InEntities.ForEach(
        [this, &ScopedSortCount](NetEntityId InNet, Entity InLocal) noexcept
        {
            if (ScopedSortCount >= kMaxNetEntities)
            {
                return;
            }
            MSortScratch[ScopedSortCount].id = InNet.value;
            MSortScratch[ScopedSortCount].local = InLocal;
            ++ScopedSortCount;
        });
    std::sort(MSortScratch.begin(), MSortScratch.begin() + ScopedSortCount,
              [](const SortableEntity& InLeft, const SortableEntity& InRight) noexcept
              { return InLeft.id < InRight.id; });

    for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedSortCount; ++ScopedIndex)
    {
        const SortableEntity& ScopedEntry = MSortScratch[ScopedIndex];
        const Transform2D* ScopedTransform = InRegistry.try_get<Transform2D>(ScopedEntry.local);
        if (ScopedTransform == nullptr)
        {
            continue;
        }
        ++MStats.mappedEntities;
        const CellCoordinate ScopedCell = CellOf(ScopedTransform->Position);
        if (!ScopedCell.valid)
        {
            ++MStats.outOfBounds;
            continue;
        }
        RelevanceCell& ScopedTarget =
            CellRef(static_cast<std::uint32_t>(ScopedCell.x), static_cast<std::uint32_t>(ScopedCell.y));
        if (ScopedTarget.count >= kMaxRelevanceEntitiesPerCell)
        {
            ++ScopedTarget.dropped;
            ++MStats.droppedEntities;
            continue;
        }
        if (ScopedTarget.count == 0)
        {
            ++MStats.occupiedCells;
        }
        ScopedTarget.entities[ScopedTarget.count++] = NetEntityId{ScopedEntry.id};
        ++MStats.placedEntities;
    }
}

bool RelevanceGrid::BuildPlan(NetPlayerId InPlayer, std::uint64_t InTick, RelevancePlan& OutPlan) const noexcept
{
    OutPlan = RelevancePlan{};
    OutPlan.player = InPlayer;
    OutPlan.tick = InTick;
    if (!MInitialized || !InPlayer.IsValid() || !MHasPlayer[InPlayer.value])
    {
        return false;
    }
    const CellCoordinate ScopedPlayerCell = CellOf(MPlayerPositions[InPlayer.value]);
    if (!ScopedPlayerCell.valid)
    {
        return false;
    }

    const std::int32_t ScopedRadius = static_cast<std::int32_t>(MConfig.farRadius);
    const std::int32_t ScopedMinX = std::max<std::int32_t>(0, ScopedPlayerCell.x - ScopedRadius);
    const std::int32_t ScopedMaxX =
        std::min<std::int32_t>(static_cast<std::int32_t>(MConfig.cellsX) - 1, ScopedPlayerCell.x + ScopedRadius);
    const std::int32_t ScopedMinY = std::max<std::int32_t>(0, ScopedPlayerCell.y - ScopedRadius);
    const std::int32_t ScopedMaxY =
        std::min<std::int32_t>(static_cast<std::int32_t>(MConfig.cellsY) - 1, ScopedPlayerCell.y + ScopedRadius);

    for (std::int32_t ScopedCellY = ScopedMinY; ScopedCellY <= ScopedMaxY; ++ScopedCellY)
    {
        for (std::int32_t ScopedCellX = ScopedMinX; ScopedCellX <= ScopedMaxX; ++ScopedCellX)
        {
            const std::uint32_t ScopedDx = static_cast<std::uint32_t>(std::abs(ScopedCellX - ScopedPlayerCell.x));
            const std::uint32_t ScopedDy = static_cast<std::uint32_t>(std::abs(ScopedCellY - ScopedPlayerCell.y));
            const RelevanceBand ScopedBand = BandFor(ScopedDx, ScopedDy);
            if (ScopedBand == RelevanceBand::None)
            {
                continue;
            }
            const std::uint32_t ScopedInterval =
                ScopedBand == RelevanceBand::Immediate
                    ? 1u
                    : (ScopedBand == RelevanceBand::Near ? MConfig.nearInterval : MConfig.farInterval);
            const RelevanceCell& ScopedCell =
                CellRef(static_cast<std::uint32_t>(ScopedCellX), static_cast<std::uint32_t>(ScopedCellY));
            for (std::uint32_t ScopedEntity = 0; ScopedEntity < ScopedCell.count; ++ScopedEntity)
            {
                const NetEntityId ScopedId = ScopedCell.entities[ScopedEntity];
                if (!IsDue(ScopedInterval, ScopedId, InTick))
                {
                    continue;
                }
                if (OutPlan.count >= kMaxRelevanceOutput)
                {
                    OutPlan.truncated = true;
                    break;
                }
                RelevanceEntity& ScopedOutput = OutPlan.items[OutPlan.count++];
                ScopedOutput.id = ScopedId;
                ScopedOutput.cellX = static_cast<std::uint16_t>(ScopedCellX);
                ScopedOutput.cellY = static_cast<std::uint16_t>(ScopedCellY);
                ScopedOutput.band = ScopedBand;
                ScopedOutput.interval = static_cast<std::uint8_t>(ScopedInterval);
                ScopedOutput.phase = ScopedId.value % ScopedInterval;
                OutPlan.Set(ScopedId);
                if (ScopedBand == RelevanceBand::Immediate)
                {
                    ++OutPlan.immediateCount;
                }
                else if (ScopedBand == RelevanceBand::Near)
                {
                    ++OutPlan.nearCount;
                }
                else
                {
                    ++OutPlan.farCount;
                }
            }
        }
    }

    // A deterministic wire order, independent of cell traversal and of the
    std::sort(OutPlan.items.begin(), OutPlan.items.begin() + OutPlan.count,
              [](const RelevanceEntity& InLeft, const RelevanceEntity& InRight) noexcept
              { return InLeft.id.value < InRight.id.value; });
    return true;
}
}
