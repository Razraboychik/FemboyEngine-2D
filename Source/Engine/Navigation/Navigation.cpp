// FemboyEngine2D - navigation maps, A* and agent movement.
#include "Engine/Navigation/Navigation.hpp"

#include "Engine/ECS/Components.hpp"
#include "Engine/ECS/SystemGraph.hpp"
#include "Engine/Navigation/NavMesh.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Fby::Navigation
{
namespace
{
constexpr float kInfinity = std::numeric_limits<float>::infinity();
constexpr std::uint32_t kInvalidNode = 0xFFFFFFFFu;
// Upper bound on the structural pass that creates runtime state components; a
// scene with more settings-only agents creates the rest on the next frame.
constexpr std::uint32_t kMaxDeferredStates = 64;

[[nodiscard]] float Heuristic(const Grid::GridDesc& InGrid, Grid::Cell InFrom, Grid::Cell InTo) noexcept
{
    return Grid::CellDistance(InGrid.Mode, InFrom, InTo);
}

[[nodiscard]] std::uint32_t Hash32(std::uint32_t InValue) noexcept
{
    InValue ^= InValue >> 16u;
    InValue *= 0x7FEB352Du;
    InValue ^= InValue >> 15u;
    InValue *= 0x846CA68Bu;
    InValue ^= InValue >> 16u;
    return InValue;
}

[[nodiscard]] float RandomUnit(std::uint32_t InSeed, std::uint32_t InIndex) noexcept
{
    return static_cast<float>(Hash32(InSeed ^ (InIndex * 0x9E3779B9u)) & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
}

// Samples the segment often enough that a cell-sized wall is never skipped.
[[nodiscard]] bool HasLineOfSight(const NavMap& InMap, Vector2D InFrom, Vector2D InTo) noexcept
{
    const float ScopedDx = InTo.X - InFrom.X;
    const float ScopedDy = InTo.Y - InFrom.Y;
    const float ScopedDistance = std::sqrt(ScopedDx * ScopedDx + ScopedDy * ScopedDy);
    const float ScopedCell = std::max(std::min(InMap.Grid.CellSize.X, InMap.Grid.CellSize.Y), 0.001f);
    const std::uint32_t ScopedSteps =
        std::max(1u, static_cast<std::uint32_t>(std::ceil(ScopedDistance / (ScopedCell * 0.25f))));
    for (std::uint32_t ScopedStep = 0; ScopedStep <= ScopedSteps; ++ScopedStep)
    {
        const float ScopedT = static_cast<float>(ScopedStep) / static_cast<float>(ScopedSteps);
        const Vector2D ScopedSample{InFrom.X + ScopedDx * ScopedT, InFrom.Y + ScopedDy * ScopedT};
        if (!InMap.Passable(Grid::WorldToCell(InMap.Grid, ScopedSample)))
        {
            return false;
        }
    }
    return true;
}

void DeduplicatePoints(std::array<Vector2D, kMaxPathCells>& InPoints, std::uint16_t& InCount) noexcept
{
    std::uint16_t ScopedWrite = 0;
    for (std::uint16_t ScopedRead = 0; ScopedRead < InCount; ++ScopedRead)
    {
        if (ScopedWrite > 0)
        {
            const Vector2D ScopedDelta = InPoints[ScopedRead] - InPoints[ScopedWrite - 1];
            if (ScopedDelta.LengthSquared() <= 1.0e-6f)
            {
                continue;
            }
        }
        InPoints[ScopedWrite++] = InPoints[ScopedRead];
    }
    InCount = ScopedWrite;
}

// Replaces points with the shortest in-corridor sequence of anchors. The
// resulting polyline keeps the same walkable corridor as raw A* but drops every
// redundant stair-step cell.
void SimplifyLineOfSight(const NavMap& InMap, const std::array<Vector2D, kMaxPathCells>& InRaw,
                         std::uint16_t InRawCount, std::array<Vector2D, kMaxPathCells>& OutPoints,
                         std::uint16_t& OutCount) noexcept
{
    OutCount = 0;
    if (InRawCount == 0)
    {
        return;
    }
    std::uint16_t ScopedAnchor = 0;
    OutPoints[OutCount++] = InRaw[0];
    while (ScopedAnchor + 1 < InRawCount)
    {
        std::uint16_t ScopedBest = ScopedAnchor + 1;
        for (std::uint16_t ScopedCandidate = static_cast<std::uint16_t>(ScopedAnchor + 2); ScopedCandidate < InRawCount;
             ++ScopedCandidate)
        {
            if (HasLineOfSight(InMap, InRaw[ScopedAnchor], InRaw[ScopedCandidate]))
            {
                ScopedBest = ScopedCandidate;
            }
            else
            {
                break;
            }
        }
        if (OutPoints[OutCount - 1] != InRaw[ScopedBest])
        {
            OutPoints[OutCount++] = InRaw[ScopedBest];
        }
        ScopedAnchor = ScopedBest;
    }
}

void SmoothChaikin(const std::array<Vector2D, kMaxPathCells>& InRaw, std::uint16_t InRawCount, float InStrength,
                   std::uint8_t InIterations, std::array<Vector2D, kMaxPathCells>& OutPoints,
                   std::uint16_t& OutCount) noexcept
{
    std::array<Vector2D, kMaxPathCells> ScopedScratch = InRaw;
    std::uint16_t ScopedCount = InRawCount;
    const float ScopedStrength = std::clamp(InStrength, 0.0f, 1.0f);
    for (std::uint8_t ScopedIteration = 0; ScopedIteration < InIterations && ScopedCount > 2; ++ScopedIteration)
    {
        std::array<Vector2D, kMaxPathCells> ScopedNext{};
        std::uint16_t ScopedWrite = 0;
        ScopedNext[ScopedWrite++] = ScopedScratch[0];
        for (std::uint16_t ScopedI = 0; ScopedI + 1 < ScopedCount && ScopedWrite + 2 < kMaxPathCells; ++ScopedI)
        {
            const Vector2D ScopedP0 = ScopedScratch[ScopedI];
            const Vector2D ScopedP1 = ScopedScratch[ScopedI + 1];
            ScopedNext[ScopedWrite++] = ScopedP0 + (ScopedP1 - ScopedP0) * (0.25f * ScopedStrength);
            ScopedNext[ScopedWrite++] = ScopedP0 + (ScopedP1 - ScopedP0) * (0.75f * ScopedStrength);
        }
        if (ScopedWrite < kMaxPathCells)
        {
            ScopedNext[ScopedWrite++] = ScopedScratch[ScopedCount - 1];
        }
        ScopedScratch = ScopedNext;
        ScopedCount = ScopedWrite;
    }
    OutPoints = ScopedScratch;
    OutCount = ScopedCount;
}

void SmoothCatmullRom(const std::array<Vector2D, kMaxPathCells>& InRaw, std::uint16_t InRawCount, float InStrength,
                      std::uint8_t InIterations, std::array<Vector2D, kMaxPathCells>& OutPoints,
                      std::uint16_t& OutCount) noexcept
{
    if (InRawCount < 2)
    {
        OutPoints = InRaw;
        OutCount = InRawCount;
        return;
    }
    const float ScopedStrength = std::clamp(InStrength, 0.0f, 1.0f);
    const std::uint32_t ScopedSubdivisions = std::clamp<std::uint32_t>(InIterations + 1u, 2u, 8u);
    std::uint16_t ScopedWrite = 0;
    OutPoints[ScopedWrite++] = InRaw[0];
    for (std::uint16_t ScopedI = 0; ScopedI + 1 < InRawCount && ScopedWrite + ScopedSubdivisions < kMaxPathCells;
         ++ScopedI)
    {
        const Vector2D ScopedP0 = InRaw[ScopedI == 0 ? 0 : static_cast<std::uint16_t>(ScopedI - 1)];
        const Vector2D ScopedP1 = InRaw[ScopedI];
        const Vector2D ScopedP2 = InRaw[ScopedI + 1];
        const Vector2D ScopedP3 = InRaw[ScopedI + 2 < InRawCount ? static_cast<std::uint16_t>(ScopedI + 2)
                                                                 : static_cast<std::uint16_t>(InRawCount - 1)];
        for (std::uint32_t ScopedStep = 1; ScopedStep <= ScopedSubdivisions; ++ScopedStep)
        {
            const float ScopedT = static_cast<float>(ScopedStep) / static_cast<float>(ScopedSubdivisions);
            const float ScopedT2 = ScopedT * ScopedT;
            const float ScopedT3 = ScopedT2 * ScopedT;
            const Vector2D ScopedCurve = (ScopedP1 * 2.0f + (ScopedP2 - ScopedP0) * ScopedT +
                                          (ScopedP0 * 2.0f - ScopedP1 * 5.0f + ScopedP2 * 4.0f - ScopedP3) * ScopedT2 +
                                          (ScopedP1 * 3.0f - ScopedP0 - ScopedP2 * 3.0f + ScopedP3) * ScopedT3) *
                                         0.5f;
            const Vector2D ScopedLinear = ScopedP1 + (ScopedP2 - ScopedP1) * ScopedT;
            OutPoints[ScopedWrite++] = ScopedLinear + (ScopedCurve - ScopedLinear) * ScopedStrength;
        }
    }
    OutCount = ScopedWrite;
}

// Smoothing must never cut a wall. If any generated segment is blocked, keep
// the previously validated point list.
void ApplySmoothingAndJitter(const NavMap& InMap, const NavPathOptions& InOptions, NavPath& InPath) noexcept
{
    if (InPath.Count == 0)
    {
        return;
    }
    std::array<Vector2D, kMaxPathCells> ScopedRaw{};
    for (std::uint16_t ScopedI = 0; ScopedI < InPath.Count; ++ScopedI)
    {
        ScopedRaw[ScopedI] = Grid::CellToWorld(InMap.Grid, InPath.Cells[ScopedI]);
    }
    std::array<Vector2D, kMaxPathCells> ScopedPoints = ScopedRaw;
    std::uint16_t ScopedPointCount = InPath.Count;

    if (InOptions.Smoothing == NavSmoothingMode::LineOfSight)
    {
        SimplifyLineOfSight(InMap, ScopedRaw, InPath.Count, ScopedPoints, ScopedPointCount);
    }
    else if (InOptions.Smoothing == NavSmoothingMode::Chaikin)
    {
        std::array<Vector2D, kMaxPathCells> ScopedSimplified{};
        std::uint16_t ScopedSimplifiedCount = 0;
        SimplifyLineOfSight(InMap, ScopedRaw, InPath.Count, ScopedSimplified, ScopedSimplifiedCount);
        SmoothChaikin(ScopedSimplified, ScopedSimplifiedCount, InOptions.SmoothingStrength,
                      InOptions.SmoothingIterations, ScopedPoints, ScopedPointCount);
    }
    else if (InOptions.Smoothing == NavSmoothingMode::CatmullRom)
    {
        std::array<Vector2D, kMaxPathCells> ScopedSimplified{};
        std::uint16_t ScopedSimplifiedCount = 0;
        SimplifyLineOfSight(InMap, ScopedRaw, InPath.Count, ScopedSimplified, ScopedSimplifiedCount);
        SmoothCatmullRom(ScopedSimplified, ScopedSimplifiedCount, InOptions.SmoothingStrength,
                         InOptions.SmoothingIterations, ScopedPoints, ScopedPointCount);
    }

    // Validate each generated segment; a blocked segment falls back to the
    // previous (already valid) point list.
    bool ScopedValid = true;
    for (std::uint16_t ScopedI = 0; ScopedI + 1 < ScopedPointCount; ++ScopedI)
    {
        if (!HasLineOfSight(InMap, ScopedPoints[ScopedI], ScopedPoints[ScopedI + 1]))
        {
            ScopedValid = false;
            break;
        }
    }
    if (!ScopedValid)
    {
        ScopedPoints = ScopedRaw;
        ScopedPointCount = InPath.Count;
    }

    if (InOptions.RandomWaypointCells > 0.0f && ScopedPointCount > 2)
    {
        const float ScopedJitterX = InOptions.RandomWaypointCells * InMap.Grid.CellSize.X;
        const float ScopedJitterY = InOptions.RandomWaypointCells * InMap.Grid.CellSize.Y;
        for (std::uint16_t ScopedI = 1; ScopedI + 1 < ScopedPointCount; ++ScopedI)
        {
            const float ScopedRandomX = RandomUnit(InOptions.RandomSeed, ScopedI * 2u) * 2.0f - 1.0f;
            const float ScopedRandomY = RandomUnit(InOptions.RandomSeed, ScopedI * 2u + 1u) * 2.0f - 1.0f;
            const Vector2D ScopedCandidate{ScopedPoints[ScopedI].X + ScopedRandomX * ScopedJitterX,
                                           ScopedPoints[ScopedI].Y + ScopedRandomY * ScopedJitterY};
            if (InMap.Passable(Grid::WorldToCell(InMap.Grid, ScopedCandidate)) &&
                HasLineOfSight(InMap, ScopedPoints[ScopedI - 1], ScopedCandidate) &&
                HasLineOfSight(InMap, ScopedCandidate, ScopedPoints[ScopedI + 1]))
            {
                ScopedPoints[ScopedI] = ScopedCandidate;
            }
        }
    }

    DeduplicatePoints(ScopedPoints, ScopedPointCount);
    InPath.Points = ScopedPoints;
    InPath.Count = ScopedPointCount;
    InPath.HasPoints = true;
    InPath.Truncated = InPath.Truncated || ScopedPointCount >= kMaxPathCells;
}
}

void ClearMap(NavMap& InMap, std::uint32_t InWidth, std::uint32_t InHeight, const Grid::GridDesc& InGrid) noexcept
{
    InMap.Width = std::min(InWidth, kNavMaxWidth);
    InMap.Height = std::min(InHeight, kNavMaxHeight);
    InMap.Grid = InGrid;
    InMap.Cost.fill(0);
}

bool BuildFromTilemap(const TilemapComponent& InTilemap, NavMap* OutMap) noexcept
{
    if (OutMap == nullptr)
    {
        return false;
    }
    if (InTilemap.Width == 0 || InTilemap.Height == 0)
    {
        return false;
    }
    ClearMap(*OutMap, InTilemap.Width, InTilemap.Height, InTilemap.Grid);
    for (std::uint32_t ScopedY = 0; ScopedY < OutMap->Height; ++ScopedY)
    {
        for (std::uint32_t ScopedX = 0; ScopedX < OutMap->Width; ++ScopedX)
        {
            const Grid::Cell ScopedCell{static_cast<std::int32_t>(ScopedX), static_cast<std::int32_t>(ScopedY)};
            const std::uint8_t ScopedCost = Tilemap::IsSolid(InTilemap, ScopedCell) ? 0 : 1;
            (void)OutMap->SetCost(ScopedCell, ScopedCost);
        }
    }
    return true;
}

void PathFinder::ClearScratch(std::uint32_t InCellCount) noexcept
{
    const std::uint32_t ScopedCount = std::min(InCellCount, kNavMaxCells);
    std::fill(MGScore.begin(), MGScore.begin() + ScopedCount, kInfinity);
    std::fill(MFScore.begin(), MFScore.begin() + ScopedCount, kInfinity);
    std::fill(MParent.begin(), MParent.begin() + ScopedCount, kInvalidNode);
    std::fill(MClosed.begin(), MClosed.begin() + ScopedCount, 0);
    std::fill(MInOpen.begin(), MInOpen.begin() + ScopedCount, 0);
    MHeapSize = 0;
}

void PathFinder::Push(std::uint32_t InNode) noexcept
{
    if (MHeapSize >= kNavMaxCells)
    {
        return;
    }
    MHeap[MHeapSize] = InNode;
    MInOpen[InNode] = 1;
    std::uint32_t ScopedIndex = MHeapSize;
    ++MHeapSize;
    while (ScopedIndex > 0)
    {
        const std::uint32_t ScopedParent = (ScopedIndex - 1) / 2;
        if (MFScore[MHeap[ScopedParent]] <= MFScore[MHeap[ScopedIndex]])
        {
            break;
        }
        const std::uint32_t ScopedSwap = MHeap[ScopedParent];
        MHeap[ScopedParent] = MHeap[ScopedIndex];
        MHeap[ScopedIndex] = ScopedSwap;
        ScopedIndex = ScopedParent;
    }
    ++MStats.Pushed;
}

std::uint32_t PathFinder::Pop() noexcept
{
    if (MHeapSize == 0)
    {
        return kInvalidNode;
    }
    const std::uint32_t ScopedResult = MHeap[0];
    MInOpen[ScopedResult] = 0;
    --MHeapSize;
    if (MHeapSize > 0)
    {
        MHeap[0] = MHeap[MHeapSize];
        std::uint32_t ScopedIndex = 0;
        for (;;)
        {
            const std::uint32_t ScopedLeft = ScopedIndex * 2 + 1;
            const std::uint32_t ScopedRight = ScopedLeft + 1;
            std::uint32_t ScopedSmallest = ScopedIndex;
            if (ScopedLeft < MHeapSize && MFScore[MHeap[ScopedLeft]] < MFScore[MHeap[ScopedSmallest]])
            {
                ScopedSmallest = ScopedLeft;
            }
            if (ScopedRight < MHeapSize && MFScore[MHeap[ScopedRight]] < MFScore[MHeap[ScopedSmallest]])
            {
                ScopedSmallest = ScopedRight;
            }
            if (ScopedSmallest == ScopedIndex)
            {
                break;
            }
            const std::uint32_t ScopedSwap = MHeap[ScopedIndex];
            MHeap[ScopedIndex] = MHeap[ScopedSmallest];
            MHeap[ScopedSmallest] = ScopedSwap;
            ScopedIndex = ScopedSmallest;
        }
    }
    return ScopedResult;
}

void PathFinder::UpdateOpen(std::uint32_t InNode) noexcept
{
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < MHeapSize; ++ScopedIndex)
    {
        if (MHeap[ScopedIndex] != InNode)
        {
            continue;
        }
        std::uint32_t ScopedCursor = ScopedIndex;
        while (ScopedCursor > 0)
        {
            const std::uint32_t ScopedParent = (ScopedCursor - 1) / 2;
            if (MFScore[MHeap[ScopedParent]] <= MFScore[MHeap[ScopedCursor]])
            {
                break;
            }
            const std::uint32_t ScopedSwap = MHeap[ScopedParent];
            MHeap[ScopedParent] = MHeap[ScopedCursor];
            MHeap[ScopedCursor] = ScopedSwap;
            ScopedCursor = ScopedParent;
        }
        return;
    }
}

bool PathFinder::Find(const NavMap& InMap, Grid::Cell InStart, Grid::Cell InGoal, NavPath* OutPath,
                      bool InAllowPartial) noexcept
{
    if (OutPath == nullptr)
    {
        return false;
    }
    OutPath->Count = 0;
    OutPath->Truncated = false;
    if (InMap.Width == 0 || InMap.Height == 0 || InMap.Width > kNavMaxWidth || InMap.Height > kNavMaxHeight)
    {
        return false;
    }
    ++MStats.Queries;
    MWidth = InMap.Width;
    MHeight = InMap.Height;
    const std::uint32_t ScopedCellCount = MWidth * MHeight;
    ClearScratch(ScopedCellCount);
    if (!InMap.Contains(InStart) || !InMap.Contains(InGoal))
    {
        ++MStats.Failed;
        return false;
    }
    if (!InMap.Passable(InStart) || !InMap.Passable(InGoal))
    {
        ++MStats.Failed;
        return false;
    }

    const std::uint32_t ScopedStart = NodeIndex(InStart);
    const std::uint32_t ScopedGoal = NodeIndex(InGoal);
    MGScore[ScopedStart] = 0.0f;
    MFScore[ScopedStart] = Heuristic(InMap.Grid, InStart, InGoal);
    MParent[ScopedStart] = kInvalidNode;
    Push(ScopedStart);

    std::uint32_t ScopedClosest = ScopedStart;
    float ScopedClosestDistance = Heuristic(InMap.Grid, InStart, InGoal);
    bool ScopedFound = false;
    std::uint32_t ScopedExpanded = 0;

    while (MHeapSize > 0)
    {
        const std::uint32_t ScopedCurrent = Pop();
        if (ScopedCurrent == kInvalidNode || MClosed[ScopedCurrent] != 0)
        {
            continue;
        }
        MClosed[ScopedCurrent] = 1;
        ++ScopedExpanded;
        if (ScopedCurrent == ScopedGoal)
        {
            ScopedFound = true;
            break;
        }
        const Grid::Cell ScopedCell = NodeCell(ScopedCurrent);
        const float ScopedDistance = Heuristic(InMap.Grid, ScopedCell, InGoal);
        if (ScopedDistance < ScopedClosestDistance)
        {
            ScopedClosestDistance = ScopedDistance;
            ScopedClosest = ScopedCurrent;
        }
        const std::uint32_t ScopedNeighbourCount = Grid::NeighborCount(InMap.Grid.Mode);
        for (std::uint32_t ScopedI = 0; ScopedI < ScopedNeighbourCount; ++ScopedI)
        {
            const Grid::Cell ScopedNeighbour = Grid::Neighbor(InMap.Grid.Mode, ScopedCell, ScopedI);
            if (!InMap.Contains(ScopedNeighbour))
            {
                continue;
            }
            const std::uint8_t ScopedCost = InMap.CostAt(ScopedNeighbour);
            if (ScopedCost == 0)
            {
                continue;
            }
            const std::uint32_t ScopedNeighbourNode = NodeIndex(ScopedNeighbour);
            if (MClosed[ScopedNeighbourNode] != 0)
            {
                continue;
            }
            const float ScopedStep = Grid::CellDistance(InMap.Grid.Mode, ScopedCell, ScopedNeighbour);
            float ScopedCandidate = MGScore[ScopedCurrent] + ScopedStep * static_cast<float>(ScopedCost);
            if (MRandomCost > 0.0f)
            {
                ScopedCandidate += RandomUnit(MRandomSeed, ScopedNeighbourNode) * MRandomCost;
            }
            if (ScopedCandidate >= MGScore[ScopedNeighbourNode])
            {
                continue;
            }
            MGScore[ScopedNeighbourNode] = ScopedCandidate;
            MFScore[ScopedNeighbourNode] = ScopedCandidate + Heuristic(InMap.Grid, ScopedNeighbour, InGoal);
            MParent[ScopedNeighbourNode] = ScopedCurrent;
            if (MInOpen[ScopedNeighbourNode] != 0)
            {
                UpdateOpen(ScopedNeighbourNode);
            }
            else
            {
                Push(ScopedNeighbourNode);
            }
        }
    }

    MStats.Expanded += ScopedExpanded;
    MStats.MaxExpanded = std::max(MStats.MaxExpanded, ScopedExpanded);

    std::uint32_t ScopedEnd = kInvalidNode;
    if (ScopedFound)
    {
        ScopedEnd = ScopedGoal;
    }
    else if (InAllowPartial && ScopedClosest != ScopedStart)
    {
        ScopedEnd = ScopedClosest;
    }
    if (ScopedEnd == kInvalidNode)
    {
        ++MStats.Failed;
        return false;
    }

    std::uint32_t ScopedCursor = ScopedEnd;
    std::uint16_t ScopedCount = 0;
    bool ScopedTruncated = false;
    while (ScopedCursor != kInvalidNode)
    {
        if (ScopedCount >= kMaxPathCells)
        {
            ScopedTruncated = true;
            break;
        }
        OutPath->Cells[ScopedCount++] = NodeCell(ScopedCursor);
        if (ScopedCursor == ScopedStart)
        {
            break;
        }
        ScopedCursor = MParent[ScopedCursor];
    }
    if (ScopedCursor != ScopedStart)
    {
        ScopedTruncated = true;
    }
    std::reverse(OutPath->Cells.begin(), OutPath->Cells.begin() + ScopedCount);
    OutPath->Count = ScopedCount;
    OutPath->Truncated = ScopedTruncated;
    OutPath->QueryId = MStats.Queries;
    return ScopedCount > 0;
}

bool PathFinder::FindWithOptions(const NavMap& InMap, Grid::Cell InStart, Grid::Cell InGoal,
                                 const NavPathOptions& InOptions, NavPath* OutPath, bool InAllowPartial) noexcept
{
    MRandomSeed = InOptions.RandomSeed;
    MRandomCost = InOptions.RandomizeRoute ? std::max(InOptions.RandomCost, 0.0f) : 0.0f;
    const bool ScopedFound = Find(InMap, InStart, InGoal, OutPath, InAllowPartial);
    MRandomSeed = 0;
    MRandomCost = 0.0f;
    if (!ScopedFound || OutPath == nullptr || OutPath->Count == 0)
    {
        return false;
    }
    // Find() wrote the raw cell polyline. Only the movement representation is
    // replaced; the raw cells stay available for debug overlays and tests.
    const std::uint16_t ScopedRawCount = OutPath->Count;
    std::array<Grid::Cell, kMaxPathCells> ScopedRawCells = OutPath->Cells;
    ApplySmoothingAndJitter(InMap, InOptions, *OutPath);
    if (InOptions.Smoothing == NavSmoothingMode::None && InOptions.RandomWaypointCells <= 0.0f)
    {
        OutPath->Cells = ScopedRawCells;
        OutPath->Count = ScopedRawCount;
        OutPath->HasPoints = false;
    }
    return true;
}

bool PathFinder::FindLimited(const NavMap& InMap, Grid::Cell InStart, Grid::Cell InGoal, std::uint16_t InMaxCells,
                             NavPath* OutPath) noexcept
{
    if (OutPath == nullptr || InMaxCells == 0)
    {
        return false;
    }
    NavPath ScopedFull;
    if (!Find(InMap, InStart, InGoal, &ScopedFull, false))
    {
        OutPath->Count = 0;
        return false;
    }
    const std::uint16_t ScopedCount = std::min(InMaxCells, ScopedFull.Count);
    OutPath->Count = ScopedCount;
    OutPath->Truncated = ScopedCount < ScopedFull.Count || ScopedFull.Truncated;
    OutPath->QueryId = ScopedFull.QueryId;
    for (std::uint16_t ScopedI = 0; ScopedI < ScopedCount; ++ScopedI)
    {
        OutPath->Cells[ScopedI] = ScopedFull.Cells[ScopedI];
    }
    return true;
}

Vector2D FollowPath(const NavMap& InMap, const NavPath& InPath, std::uint16_t* OutIndex, Vector2D InPosition,
                    float InSpeed, float InDeltaSeconds, bool* OutArrived) noexcept
{
    if (OutArrived != nullptr)
    {
        *OutArrived = false;
    }
    if (OutIndex == nullptr || InPath.Count == 0)
    {
        if (OutArrived != nullptr)
        {
            *OutArrived = true;
        }
        return InPosition;
    }
    float ScopedRemaining = std::max(InSpeed, 0.0f) * std::max(InDeltaSeconds, 0.0f);
    Vector2D ScopedPosition = InPosition;
    std::uint16_t ScopedIndex = *OutIndex;
    while (ScopedIndex < InPath.Count)
    {
        const Vector2D ScopedWaypoint =
            InPath.HasPoints ? InPath.Points[ScopedIndex] : Grid::CellToWorld(InMap.Grid, InPath.Cells[ScopedIndex]);
        const Vector2D ScopedDelta = ScopedWaypoint - ScopedPosition;
        const float ScopedDistance = std::sqrt(ScopedDelta.X * ScopedDelta.X + ScopedDelta.Y * ScopedDelta.Y);
        if (ScopedDistance <= ScopedRemaining)
        {
            ScopedPosition = ScopedWaypoint;
            ScopedRemaining -= ScopedDistance;
            ++ScopedIndex;
            continue;
        }
        if (ScopedDistance > 0.0f)
        {
            ScopedPosition = ScopedPosition + ScopedDelta * (ScopedRemaining / ScopedDistance);
            ScopedRemaining = 0.0f;
        }
        break;
    }
    *OutIndex = ScopedIndex;
    if (ScopedIndex >= InPath.Count && OutArrived != nullptr)
    {
        *OutArrived = true;
    }
    return ScopedPosition;
}

void NavigationSystem::EnsureRuntimeState(Ecs::Registry& InRegistry) noexcept
{
    // Structural changes must happen outside a view iteration. An entity with
    // authored settings but no runtime state gets an empty one; a state whose
    // entity lost its settings is dropped by the caller's view.
    std::array<Entity, kMaxDeferredStates> ScopedMissing{};
    std::uint32_t ScopedMissingCount = 0;
    auto ScopedView = InRegistry.view<NavAgentSettings>();
    for (auto [ScopedEntity, ScopedSettings] : ScopedView.each())
    {
        (void)ScopedSettings;
        if (InRegistry.try_get<NavAgentComponent>(ScopedEntity) == nullptr)
        {
            if (ScopedMissingCount < ScopedMissing.size())
            {
                ScopedMissing[ScopedMissingCount++] = ScopedEntity;
            }
            else
            {
                ++MDroppedRuntimeStates;
            }
        }
    }
    for (std::uint32_t ScopedI = 0; ScopedI < ScopedMissingCount; ++ScopedI)
    {
        (void)InRegistry.emplace<NavAgentComponent>(ScopedMissing[ScopedI]);
    }
}

void NavigationSystem::Update(Ecs::Registry& InRegistry, float InDeltaSeconds) noexcept
{
    if (MMap == nullptr)
    {
        return;
    }
    EnsureRuntimeState(InRegistry);
    MRepathsThisFrame = 0;
    auto ScopedView = InRegistry.view<Transform2D, NavAgentSettings, NavAgentComponent>();
    for (auto [ScopedEntity, ScopedTransform, ScopedSettings, ScopedAgent] : ScopedView.each())
    {
        (void)ScopedEntity;
        if ((ScopedSettings.Flags & kNavAgentFlagActive) == 0)
        {
            continue;
        }
        ScopedAgent.RepathTimer -= InDeltaSeconds;
        const Grid::Cell ScopedGoal = Grid::WorldToCell(MMap->Grid, ScopedSettings.Target);
        const Grid::Cell ScopedStart = Grid::WorldToCell(MMap->Grid, ScopedTransform.Position);
        const Vector2D ScopedTargetDelta = ScopedSettings.Target - ScopedAgent.LastTarget;
        const bool ScopedTargetMoved =
            ScopedTargetDelta.LengthSquared() > ScopedSettings.RepathDistance * ScopedSettings.RepathDistance;
        // An agent standing on its target does not need a new path: this keeps
        // idle agents out of the per-frame A* budget entirely.
        const bool ScopedAtTarget = (ScopedAgent.Flags & kNavAgentFlagArrived) != 0 && !ScopedTargetMoved;
        const bool ScopedNeedsPath =
            !ScopedAtTarget && (ScopedAgent.Path.Empty() || ScopedAgent.RepathTimer <= 0.0f || ScopedTargetMoved);
        if (ScopedNeedsPath)
        {
            ScopedAgent.Flags &= ~kNavAgentFlagRepathDeferred;
            if (MRepathsThisFrame >= MaxRepathsPerFrame)
            {
                // Budget exhausted: keep the current path and try again next frame.
                ScopedAgent.Flags |= kNavAgentFlagRepathDeferred;
                ScopedAgent.RepathTimer = 0.0f;
            }
            else
            {
                ++MRepathsThisFrame;
                ScopedAgent.RepathTimer = ScopedSettings.RepathInterval;
                if (MMap->Passable(ScopedGoal) && MMap->Passable(ScopedStart))
                {
                    const auto* ScopedPathSettings = InRegistry.try_get<NavAgentPathSettings>(ScopedEntity);
                    const NavPathOptions ScopedPathOptions =
                        ScopedPathSettings != nullptr ? ScopedPathSettings->ToOptions() : NavPathOptions{};
                    const bool ScopedPathFound =
                        MNavMesh != nullptr && MNavMesh->Ready()
                            ? MNavMesh->FindPath(ScopedTransform.Position, ScopedSettings.Target, ScopedPathOptions,
                                                 &ScopedAgent.Path)
                            : MFinder.FindWithOptions(*MMap, ScopedStart, ScopedGoal, ScopedPathOptions,
                                                      &ScopedAgent.Path, true);
                    if (ScopedPathFound)
                    {
                        // The first waypoint is the centre of the cell the agent
                        // is already standing in. Skipping it prevents a periodic
                        // repath from pulling the agent backwards.
                        ScopedAgent.PathIndex = ScopedAgent.Path.Count > 1 ? 1 : 0;
                        ScopedAgent.LastTarget = ScopedSettings.Target;
                        ScopedAgent.Flags &= ~kNavAgentFlagBlocked;
                        ScopedAgent.Flags &= ~kNavAgentFlagArrived;
                    }
                    else
                    {
                        ScopedAgent.Flags |= kNavAgentFlagBlocked;
                    }
                }
                else
                {
                    ScopedAgent.Flags |= kNavAgentFlagBlocked;
                }
            }
        }

        bool ScopedArrived = false;
        if (ScopedAgent.PathIndex < ScopedAgent.Path.Count)
        {
            ScopedTransform.Position =
                FollowPath(*MMap, ScopedAgent.Path, &ScopedAgent.PathIndex, ScopedTransform.Position,
                           ScopedSettings.Speed, InDeltaSeconds, &ScopedArrived);
        }
        const Vector2D ScopedRemainingDelta = ScopedTransform.Position - ScopedSettings.Target;
        if (ScopedArrived ||
            ScopedRemainingDelta.LengthSquared() <= ScopedSettings.ArrivalRadius * ScopedSettings.ArrivalRadius)
        {
            ScopedAgent.Flags |= kNavAgentFlagArrived;
            ScopedAgent.Path.Count = 0;
            ScopedAgent.PathIndex = 0;
            ScopedAgent.LastTarget = ScopedSettings.Target;
        }
    }
}

void NavigationSystem::Tick(Ecs::SystemContext& InContext, void* InUser)
{
    if (InUser == nullptr)
    {
        return;
    }
    static_cast<NavigationSystem*>(InUser)->Update(InContext.registry, InContext.dt);
}

bool NavigationSystem::Register(Ecs::SystemGraph& InGraph, NavigationSystem& InSystem)
{
    Ecs::SystemAccess ScopedAccess;
    ScopedAccess.ComponentReads =
        Ecs::MakeComponentAccessMask<Transform2D, NavAgentSettings, NavAgentComponent, NavAgentPathSettings>();
    ScopedAccess.ComponentWrites = Ecs::MakeComponentAccessMask<Transform2D, NavAgentComponent>();
    ScopedAccess.Writes = Ecs::kServiceNavigation;
    return InGraph.Add(Ecs::Phase::FixedUpdate, "NavigationSystem", &NavigationSystem::Tick, &InSystem, 0, 0,
                       ScopedAccess);
}
}
