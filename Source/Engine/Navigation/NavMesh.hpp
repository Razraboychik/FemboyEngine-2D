// FemboyEngine2D - convex navmesh generated from the cost grid.
#pragma once

#include "Engine/Navigation/Navigation.hpp"

#include <array>
#include <cstdint>
#include <type_traits>

namespace Fby::Navigation
{

inline constexpr std::uint32_t kMaxNavMeshRects = 1024;
inline constexpr std::uint32_t kMaxNavMeshPortals = 4096;

// Axis-aligned convex polygon in cell coordinates. Greedy row merging keeps the
// decomposition stable: a rectangular room becomes one polygon, corridors become
// a chain of rectangles, and SDF-baked maps work the same as tilemaps.
struct NavMeshRect
{
    std::int32_t MinX = 0;
    std::int32_t MinY = 0;
    std::int32_t MaxX = 0;
    std::int32_t MaxY = 0;
    std::uint32_t Id = 0;

    [[nodiscard]] bool Contains(Grid::Cell InCell) const noexcept
    {
        return InCell.X >= MinX && InCell.X <= MaxX && InCell.Y >= MinY && InCell.Y <= MaxY;
    }
    [[nodiscard]] Vector2D Center(const Grid::GridDesc& InGrid) const noexcept
    {
        return Vector2D{
            (InGrid.Origin.X + (static_cast<float>(MinX) + static_cast<float>(MaxX)) * 0.5f * InGrid.CellSize.X),
            (InGrid.Origin.Y + (static_cast<float>(MinY) + static_cast<float>(MaxY)) * 0.5f * InGrid.CellSize.Y)};
    }
};

struct NavMeshPortal
{
    std::uint16_t RectA = 0;
    std::uint16_t RectB = 0;
    Vector2D Left{};
    Vector2D Right{};
};

static_assert(std::is_trivially_copyable_v<NavMeshPortal>);

class NavMesh
{
public:
    [[nodiscard]] bool Build(const NavMap& InMap) noexcept;
    void Clear() noexcept;

    [[nodiscard]] std::uint32_t RectCount() const noexcept
    {
        return MRectCount;
    }
    [[nodiscard]] std::uint32_t PortalCount() const noexcept
    {
        return MPortalCount;
    }
    [[nodiscard]] const NavMeshRect* RectAt(std::uint32_t InIndex) const noexcept
    {
        return InIndex < MRectCount ? &MRects[InIndex] : nullptr;
    }
    [[nodiscard]] const NavMeshPortal* PortalAt(std::uint32_t InIndex) const noexcept
    {
        return InIndex < MPortalCount ? &MPortals[InIndex] : nullptr;
    }
    [[nodiscard]] const NavMap* Map() const noexcept
    {
        return MMap;
    }
    [[nodiscard]] bool Ready() const noexcept
    {
        return MMap != nullptr && MRectCount > 0;
    }

    // Portal A* plus a taut funnel. The result is written as NavPath points
    // (HasPoints = true) so any path follower can consume it.
    [[nodiscard]] bool FindPath(Vector2D InStart, Vector2D InGoal, const NavPathOptions& InOptions,
                                NavPath* OutPath) const noexcept;

private:
    [[nodiscard]] std::uint16_t FindRect(Grid::Cell InCell) const noexcept;
    [[nodiscard]] std::uint16_t FindPortal(std::uint16_t InA, std::uint16_t InB) const noexcept;
    void BuildPortals() noexcept;

    const NavMap* MMap = nullptr;
    std::array<NavMeshRect, kMaxNavMeshRects> MRects{};
    std::uint32_t MRectCount = 0;
    std::array<NavMeshPortal, kMaxNavMeshPortals> MPortals{};
    std::uint32_t MPortalCount = 0;
};
}
