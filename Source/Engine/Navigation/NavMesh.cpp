// FemboyEngine2D - greedy rectangle navmesh and portal funnel.
#include "Engine/Navigation/NavMesh.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace Fby::Navigation
{
namespace
{
constexpr float kInfinity = std::numeric_limits<float>::infinity();
constexpr std::uint32_t kInvalid = 0xFFFFFFFFu;

[[nodiscard]] float Cross(Vector2D InA, Vector2D InB, Vector2D InC) noexcept
{
    return (InB.X - InA.X) * (InC.Y - InA.Y) - (InB.Y - InA.Y) * (InC.X - InA.X);
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

[[nodiscard]] bool PointPassable(const NavMap& InMap, Vector2D InPoint) noexcept
{
    return InMap.Passable(Grid::WorldToCell(InMap.Grid, InPoint));
}

[[nodiscard]] bool HasLineOfSight(const NavMap& InMap, Vector2D InFrom, Vector2D InTo) noexcept
{
    const Vector2D ScopedDelta = InTo - InFrom;
    const float ScopedDistance = std::sqrt(ScopedDelta.X * ScopedDelta.X + ScopedDelta.Y * ScopedDelta.Y);
    const float ScopedCell = std::max(std::min(InMap.Grid.CellSize.X, InMap.Grid.CellSize.Y), 0.001f);
    const std::uint32_t ScopedSteps =
        std::max(1u, static_cast<std::uint32_t>(std::ceil(ScopedDistance / (ScopedCell * 0.25f))));
    for (std::uint32_t ScopedStep = 0; ScopedStep <= ScopedSteps; ++ScopedStep)
    {
        const float ScopedT = static_cast<float>(ScopedStep) / static_cast<float>(ScopedSteps);
        if (!PointPassable(InMap, Vector2D{InFrom.X + ScopedDelta.X * ScopedT, InFrom.Y + ScopedDelta.Y * ScopedT}))
        {
            return false;
        }
    }
    return true;
}

struct PortalSide
{
    Vector2D Left{};
    Vector2D Right{};
};

// Keeps only anchors that see each other through walkable ground. Used as a
// deterministic fallback if a degenerate funnel would cut a wall.
void SimplifyVisible(const NavMap& InMap, const std::array<Vector2D, kMaxPathCells>& InRaw, std::uint16_t InCount,
                     std::array<Vector2D, kMaxPathCells>& OutPoints, std::uint16_t& OutCount) noexcept
{
    OutCount = 0;
    if (InCount == 0)
    {
        return;
    }
    OutPoints[OutCount++] = InRaw[0];
    std::uint16_t ScopedAnchor = 0;
    while (ScopedAnchor + 1 < InCount)
    {
        std::uint16_t ScopedBest = static_cast<std::uint16_t>(ScopedAnchor + 1);
        for (std::uint16_t ScopedCandidate = static_cast<std::uint16_t>(ScopedAnchor + 2); ScopedCandidate < InCount;
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
        OutPoints[OutCount++] = InRaw[ScopedBest];
        ScopedAnchor = ScopedBest;
    }
}
}

void NavMesh::Clear() noexcept
{
    MMap = nullptr;
    MRectCount = 0;
    MPortalCount = 0;
    MRects = {};
    MPortals = {};
}

std::uint16_t NavMesh::FindRect(Grid::Cell InCell) const noexcept
{
    for (std::uint32_t ScopedI = 0; ScopedI < MRectCount; ++ScopedI)
    {
        if (MRects[ScopedI].Contains(InCell))
        {
            return static_cast<std::uint16_t>(ScopedI);
        }
    }
    return static_cast<std::uint16_t>(kInvalid);
}

std::uint16_t NavMesh::FindPortal(std::uint16_t InA, std::uint16_t InB) const noexcept
{
    for (std::uint32_t ScopedI = 0; ScopedI < MPortalCount; ++ScopedI)
    {
        if ((MPortals[ScopedI].RectA == InA && MPortals[ScopedI].RectB == InB) ||
            (MPortals[ScopedI].RectA == InB && MPortals[ScopedI].RectB == InA))
        {
            return static_cast<std::uint16_t>(ScopedI);
        }
    }
    return static_cast<std::uint16_t>(kInvalid);
}

bool NavMesh::Build(const NavMap& InMap) noexcept
{
    Clear();
    if (InMap.Width == 0 || InMap.Height == 0 || InMap.Width > kNavMaxWidth || InMap.Height > kNavMaxHeight)
    {
        return false;
    }
    MMap = &InMap;

    std::array<std::uint16_t, kMaxNavMeshRects> ScopedPrevious{};
    std::uint32_t ScopedPreviousCount = 0;
    for (std::int32_t ScopedY = 0; ScopedY < static_cast<std::int32_t>(InMap.Height); ++ScopedY)
    {
        std::array<std::uint16_t, kMaxNavMeshRects> ScopedNext{};
        std::uint32_t ScopedNextCount = 0;
        std::uint32_t ScopedX = 0;
        while (ScopedX < InMap.Width)
        {
            if (!InMap.Passable(Grid::Cell{static_cast<std::int32_t>(ScopedX), ScopedY}))
            {
                ++ScopedX;
                continue;
            }
            const std::uint32_t ScopedMinX = ScopedX;
            while (ScopedX + 1 < InMap.Width &&
                   InMap.Passable(Grid::Cell{static_cast<std::int32_t>(ScopedX + 1), ScopedY}))
            {
                ++ScopedX;
            }
            const std::uint32_t ScopedMaxX = ScopedX;
            ++ScopedX;

            std::uint16_t ScopedMerged = static_cast<std::uint16_t>(kInvalid);
            for (std::uint32_t ScopedP = 0; ScopedP < ScopedPreviousCount; ++ScopedP)
            {
                NavMeshRect& ScopedRect = MRects[ScopedPrevious[ScopedP]];
                if (ScopedRect.MinX == static_cast<std::int32_t>(ScopedMinX) &&
                    ScopedRect.MaxX == static_cast<std::int32_t>(ScopedMaxX))
                {
                    ScopedRect.MaxY = ScopedY;
                    ScopedMerged = ScopedPrevious[ScopedP];
                    break;
                }
            }
            if (ScopedMerged == static_cast<std::uint16_t>(kInvalid))
            {
                if (MRectCount >= kMaxNavMeshRects)
                {
                    Clear();
                    return false;
                }
                NavMeshRect& ScopedRect = MRects[MRectCount];
                ScopedRect.MinX = static_cast<std::int32_t>(ScopedMinX);
                ScopedRect.MaxX = static_cast<std::int32_t>(ScopedMaxX);
                ScopedRect.MinY = ScopedY;
                ScopedRect.MaxY = ScopedY;
                ScopedRect.Id = MRectCount;
                ScopedMerged = static_cast<std::uint16_t>(MRectCount++);
            }
            if (ScopedNextCount < ScopedNext.size())
            {
                ScopedNext[ScopedNextCount++] = ScopedMerged;
            }
        }
        ScopedPrevious = ScopedNext;
        ScopedPreviousCount = ScopedNextCount;
    }

    BuildPortals();
    return MRectCount > 0;
}

void NavMesh::BuildPortals() noexcept
{
    MPortalCount = 0;
    if (MMap == nullptr)
    {
        return;
    }
    for (std::uint32_t ScopedA = 0; ScopedA < MRectCount; ++ScopedA)
    {
        for (std::uint32_t ScopedB = ScopedA + 1; ScopedB < MRectCount; ++ScopedB)
        {
            if (MPortalCount >= kMaxNavMeshPortals)
            {
                return;
            }
            const NavMeshRect& ScopedRectA = MRects[ScopedA];
            const NavMeshRect& ScopedRectB = MRects[ScopedB];
            const bool ScopedVertical =
                ScopedRectA.MaxX + 1 == ScopedRectB.MinX || ScopedRectB.MaxX + 1 == ScopedRectA.MinX;
            const bool ScopedHorizontal =
                ScopedRectA.MaxY + 1 == ScopedRectB.MinY || ScopedRectB.MaxY + 1 == ScopedRectA.MinY;
            if (!ScopedVertical && !ScopedHorizontal)
            {
                continue;
            }
            NavMeshPortal ScopedPortal;
            ScopedPortal.RectA = static_cast<std::uint16_t>(ScopedA);
            ScopedPortal.RectB = static_cast<std::uint16_t>(ScopedB);
            if (ScopedVertical)
            {
                const std::int32_t ScopedLow = std::max(ScopedRectA.MinY, ScopedRectB.MinY);
                const std::int32_t ScopedHigh = std::min(ScopedRectA.MaxY, ScopedRectB.MaxY);
                if (ScopedLow > ScopedHigh)
                {
                    continue;
                }
                const std::int32_t ScopedBoundaryX =
                    ScopedRectA.MaxX + 1 == ScopedRectB.MinX ? ScopedRectA.MaxX : ScopedRectB.MaxX;
                const Vector2D ScopedFirst = Grid::CellToWorld(MMap->Grid, Grid::Cell{ScopedBoundaryX, ScopedLow});
                const Vector2D ScopedSecond = Grid::CellToWorld(MMap->Grid, Grid::Cell{ScopedBoundaryX, ScopedHigh});
                const Vector2D ScopedOffset{MMap->Grid.CellSize.X * 0.5f, 0.0f};
                ScopedPortal.Left = ScopedFirst + ScopedOffset;
                ScopedPortal.Right = ScopedSecond + ScopedOffset;
            }
            else
            {
                const std::int32_t ScopedLow = std::max(ScopedRectA.MinX, ScopedRectB.MinX);
                const std::int32_t ScopedHigh = std::min(ScopedRectA.MaxX, ScopedRectB.MaxX);
                if (ScopedLow > ScopedHigh)
                {
                    continue;
                }
                const std::int32_t ScopedBoundaryY =
                    ScopedRectA.MaxY + 1 == ScopedRectB.MinY ? ScopedRectA.MaxY : ScopedRectB.MaxY;
                const Vector2D ScopedFirst = Grid::CellToWorld(MMap->Grid, Grid::Cell{ScopedLow, ScopedBoundaryY});
                const Vector2D ScopedSecond = Grid::CellToWorld(MMap->Grid, Grid::Cell{ScopedHigh, ScopedBoundaryY});
                const Vector2D ScopedOffset{0.0f, MMap->Grid.CellSize.Y * 0.5f};
                ScopedPortal.Left = ScopedFirst + ScopedOffset;
                ScopedPortal.Right = ScopedSecond + ScopedOffset;
            }
            MPortals[MPortalCount++] = ScopedPortal;
        }
    }
}

bool NavMesh::FindPath(Vector2D InStart, Vector2D InGoal, const NavPathOptions& InOptions,
                       NavPath* OutPath) const noexcept
{
    if (OutPath == nullptr || MMap == nullptr || MRectCount == 0)
    {
        return false;
    }
    *OutPath = NavPath{};
    const Grid::Cell ScopedStartCell = Grid::WorldToCell(MMap->Grid, InStart);
    const Grid::Cell ScopedGoalCell = Grid::WorldToCell(MMap->Grid, InGoal);
    const std::uint16_t ScopedStartRect = FindRect(ScopedStartCell);
    const std::uint16_t ScopedGoalRect = FindRect(ScopedGoalCell);
    if (ScopedStartRect == static_cast<std::uint16_t>(kInvalid) ||
        ScopedGoalRect == static_cast<std::uint16_t>(kInvalid))
    {
        return false;
    }

    // A* over polygons.
    std::array<float, kMaxNavMeshRects> ScopedG{};
    std::array<float, kMaxNavMeshRects> ScopedF{};
    std::array<std::uint16_t, kMaxNavMeshRects> ScopedParent{};
    std::array<bool, kMaxNavMeshRects> ScopedClosed{};
    std::array<bool, kMaxNavMeshRects> ScopedOpen{};
    ScopedG.fill(kInfinity);
    ScopedF.fill(kInfinity);
    for (std::uint16_t& ScopedValue : ScopedParent)
    {
        ScopedValue = static_cast<std::uint16_t>(kInvalid);
    }
    ScopedG[ScopedStartRect] = 0.0f;
    ScopedF[ScopedStartRect] = 0.0f;
    ScopedOpen[ScopedStartRect] = true;

    std::uint32_t ScopedOpenCount = 1;
    while (ScopedOpenCount > 0)
    {
        std::uint16_t ScopedCurrent = static_cast<std::uint16_t>(kInvalid);
        float ScopedBest = kInfinity;
        for (std::uint32_t ScopedI = 0; ScopedI < MRectCount; ++ScopedI)
        {
            if (ScopedOpen[ScopedI] && !ScopedClosed[ScopedI] && ScopedF[ScopedI] < ScopedBest)
            {
                ScopedBest = ScopedF[ScopedI];
                ScopedCurrent = static_cast<std::uint16_t>(ScopedI);
            }
        }
        if (ScopedCurrent == static_cast<std::uint16_t>(kInvalid))
        {
            break;
        }
        if (ScopedCurrent == ScopedGoalRect)
        {
            break;
        }
        ScopedOpen[ScopedCurrent] = false;
        ScopedClosed[ScopedCurrent] = true;
        --ScopedOpenCount;
        for (std::uint32_t ScopedPortalIndex = 0; ScopedPortalIndex < MPortalCount; ++ScopedPortalIndex)
        {
            const NavMeshPortal& ScopedPortal = MPortals[ScopedPortalIndex];
            std::uint16_t ScopedNeighbour = static_cast<std::uint16_t>(kInvalid);
            if (ScopedPortal.RectA == ScopedCurrent)
            {
                ScopedNeighbour = ScopedPortal.RectB;
            }
            else if (ScopedPortal.RectB == ScopedCurrent)
            {
                ScopedNeighbour = ScopedPortal.RectA;
            }
            else
            {
                continue;
            }
            if (ScopedClosed[ScopedNeighbour])
            {
                continue;
            }
            const Vector2D ScopedCurrentCenter = MRects[ScopedCurrent].Center(MMap->Grid);
            const Vector2D ScopedNextCenter = MRects[ScopedNeighbour].Center(MMap->Grid);
            const Vector2D ScopedDelta = ScopedNextCenter - ScopedCurrentCenter;
            const float ScopedStep = std::sqrt(ScopedDelta.X * ScopedDelta.X + ScopedDelta.Y * ScopedDelta.Y);
            const float ScopedCandidate = ScopedG[ScopedCurrent] + ScopedStep;
            if (ScopedCandidate < ScopedG[ScopedNeighbour])
            {
                ScopedG[ScopedNeighbour] = ScopedCandidate;
                const Vector2D ScopedGoalCenter = MRects[ScopedGoalRect].Center(MMap->Grid);
                const Vector2D ScopedGoalDelta = ScopedGoalCenter - ScopedNextCenter;
                ScopedF[ScopedNeighbour] = ScopedCandidate + std::sqrt(ScopedGoalDelta.X * ScopedGoalDelta.X +
                                                                       ScopedGoalDelta.Y * ScopedGoalDelta.Y);
                ScopedParent[ScopedNeighbour] = ScopedCurrent;
                if (!ScopedOpen[ScopedNeighbour])
                {
                    ScopedOpen[ScopedNeighbour] = true;
                    ++ScopedOpenCount;
                }
            }
        }
    }
    if (ScopedStartRect != ScopedGoalRect && ScopedG[ScopedGoalRect] == kInfinity)
    {
        return false;
    }

    std::array<std::uint16_t, kMaxNavMeshRects> ScopedRects{};
    std::uint32_t ScopedRectCount = 0;
    for (std::uint16_t ScopedCursor = ScopedGoalRect; ScopedCursor != static_cast<std::uint16_t>(kInvalid);
         ScopedCursor = ScopedParent[ScopedCursor])
    {
        if (ScopedRectCount >= ScopedRects.size())
        {
            break;
        }
        ScopedRects[ScopedRectCount++] = ScopedCursor;
        if (ScopedCursor == ScopedStartRect)
        {
            break;
        }
    }
    std::reverse(ScopedRects.begin(), ScopedRects.begin() + ScopedRectCount);
    if (ScopedRectCount == 0 || ScopedRects[0] != ScopedStartRect)
    {
        return false;
    }

    std::array<PortalSide, kMaxNavMeshPortals> ScopedSides{};
    std::uint32_t ScopedSideCount = 0;
    Vector2D ScopedReference = InStart;
    for (std::uint32_t ScopedI = 0; ScopedI + 1 < ScopedRectCount; ++ScopedI)
    {
        const std::uint16_t ScopedPortalIndex = FindPortal(ScopedRects[ScopedI], ScopedRects[ScopedI + 1]);
        if (ScopedPortalIndex == static_cast<std::uint16_t>(kInvalid) || ScopedSideCount >= ScopedSides.size())
        {
            return false;
        }
        const NavMeshPortal& ScopedPortal = MPortals[ScopedPortalIndex];
        const Vector2D ScopedNextReference = MRects[ScopedRects[ScopedI + 1]].Center(MMap->Grid);
        Vector2D ScopedA = ScopedPortal.Left;
        Vector2D ScopedB = ScopedPortal.Right;
        // Orient the portal consistently for the funnel: the first point must
        // lie to the left of the direction of travel.
        if (Cross(ScopedReference, ScopedNextReference, ScopedA) < 0.0f)
        {
            std::swap(ScopedA, ScopedB);
        }
        ScopedSides[ScopedSideCount].Left = ScopedA;
        ScopedSides[ScopedSideCount].Right = ScopedB;
        ++ScopedSideCount;
        ScopedReference = ScopedNextReference;
    }

    // Simple Stupid Funnel.
    std::array<Vector2D, kMaxPathCells> ScopedPoints{};
    std::uint16_t ScopedPointCount = 0;
    ScopedPoints[ScopedPointCount++] = InStart;
    Vector2D ScopedApex = InStart;
    Vector2D ScopedLeft = InStart;
    Vector2D ScopedRight = InStart;
    std::uint32_t ScopedPortalIndex = 0;
    std::uint32_t ScopedGuard = 0;
    while (ScopedPortalIndex < ScopedSideCount && ScopedPointCount + 2 < kMaxPathCells && ScopedGuard++ < 4096)
    {
        const Vector2D ScopedNewLeft = ScopedSides[ScopedPortalIndex].Left;
        const Vector2D ScopedNewRight = ScopedSides[ScopedPortalIndex].Right;
        // Update the right side.
        if (Cross(ScopedApex, ScopedRight, ScopedNewRight) <= 0.0f)
        {
            if (ScopedApex == ScopedRight || Cross(ScopedApex, ScopedLeft, ScopedNewRight) > 0.0f)
            {
                ScopedRight = ScopedNewRight;
            }
            else
            {
                ScopedPoints[ScopedPointCount++] = ScopedLeft;
                ScopedApex = ScopedLeft;
                ScopedLeft = ScopedApex;
                ScopedRight = ScopedApex;
                continue;
            }
        }
        // Update the left side.
        if (Cross(ScopedApex, ScopedLeft, ScopedNewLeft) >= 0.0f)
        {
            if (ScopedApex == ScopedLeft || Cross(ScopedApex, ScopedRight, ScopedNewLeft) < 0.0f)
            {
                ScopedLeft = ScopedNewLeft;
            }
            else
            {
                ScopedPoints[ScopedPointCount++] = ScopedRight;
                ScopedApex = ScopedRight;
                ScopedLeft = ScopedApex;
                ScopedRight = ScopedApex;
                continue;
            }
        }
        ++ScopedPortalIndex;
    }
    ScopedPoints[ScopedPointCount++] = InGoal;

    // Funnel output is already taut; validate it, then offer the optional
    // waypoint jitter used by the grid path options.
    bool ScopedValid = true;
    for (std::uint16_t ScopedI = 0; ScopedI + 1 < ScopedPointCount; ++ScopedI)
    {
        if (!HasLineOfSight(*MMap, ScopedPoints[ScopedI], ScopedPoints[ScopedI + 1]))
        {
            ScopedValid = false;
            break;
        }
    }
    if (!ScopedValid)
    {
        std::array<Vector2D, kMaxPathCells> ScopedFallback{};
        std::uint16_t ScopedFallbackCount = 0;
        ScopedFallback[ScopedFallbackCount++] = InStart;
        for (std::uint32_t ScopedI = 0; ScopedI < ScopedSideCount && ScopedFallbackCount < kMaxPathCells; ++ScopedI)
        {
            ScopedFallback[ScopedFallbackCount++] = (ScopedSides[ScopedI].Left + ScopedSides[ScopedI].Right) * 0.5f;
        }
        ScopedFallback[ScopedFallbackCount++] = InGoal;
        SimplifyVisible(*MMap, ScopedFallback, ScopedFallbackCount, ScopedPoints, ScopedPointCount);
    }

    if (InOptions.RandomWaypointCells > 0.0f && ScopedPointCount > 2)
    {
        for (std::uint16_t ScopedI = 1; ScopedI + 1 < ScopedPointCount; ++ScopedI)
        {
            const float ScopedRandomX =
                static_cast<float>(Hash32(InOptions.RandomSeed ^ (ScopedI * 2u)) & 0xFFFFu) / 65535.0f - 0.5f;
            const float ScopedRandomY =
                static_cast<float>(Hash32(InOptions.RandomSeed ^ (ScopedI * 2u + 1u)) & 0xFFFFu) / 65535.0f - 0.5f;
            const Vector2D ScopedCandidate{
                ScopedPoints[ScopedI].X + ScopedRandomX * InOptions.RandomWaypointCells * MMap->Grid.CellSize.X,
                ScopedPoints[ScopedI].Y + ScopedRandomY * InOptions.RandomWaypointCells * MMap->Grid.CellSize.Y};
            if (PointPassable(*MMap, ScopedCandidate) &&
                HasLineOfSight(*MMap, ScopedPoints[ScopedI - 1], ScopedCandidate) &&
                HasLineOfSight(*MMap, ScopedCandidate, ScopedPoints[ScopedI + 1]))
            {
                ScopedPoints[ScopedI] = ScopedCandidate;
            }
        }
    }

    OutPath->Count = ScopedPointCount;
    OutPath->Points = ScopedPoints;
    OutPath->HasPoints = true;
    for (std::uint16_t ScopedI = 0; ScopedI < ScopedPointCount; ++ScopedI)
    {
        OutPath->Cells[ScopedI] = Grid::WorldToCell(MMap->Grid, ScopedPoints[ScopedI]);
    }
    return ScopedPointCount > 0;
}
}
