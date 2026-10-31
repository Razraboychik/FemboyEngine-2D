// FemboyEngine2D - grid projections, cell/world conversion and depth order.
#pragma once

#include "Engine/Core/Types.hpp"

#include <cmath>
#include <cstdint>
#include <string_view>
#include <type_traits>

namespace Fby::Grid
{

// How a 2D world is projected onto the screen. `Side` and `TopDown` share the
// same axis-aligned math; they differ in the default sort axis and in the
// camera the editor proposes for them.
enum class ProjectionMode : std::uint8_t
{
    Side = 0,
    TopDown,
    IsoRect,
    IsoHex,
    Count
};

[[nodiscard]] constexpr const char* ProjectionModeName(ProjectionMode InMode) noexcept
{
    switch (InMode)
    {
        case ProjectionMode::Side:
            return "side";
        case ProjectionMode::TopDown:
            return "top-down";
        case ProjectionMode::IsoRect:
            return "iso-rect";
        case ProjectionMode::IsoHex:
            return "iso-hex";
        case ProjectionMode::Count:
            break;
    }
    return "?";
}

[[nodiscard]] constexpr bool ParseProjectionMode(std::string_view InText, ProjectionMode* OutMode) noexcept
{
    if (OutMode == nullptr)
    {
        return false;
    }
    if (InText == "side")
    {
        *OutMode = ProjectionMode::Side;
        return true;
    }
    if (InText == "top-down" || InText == "top_down" || InText == "topdown")
    {
        *OutMode = ProjectionMode::TopDown;
        return true;
    }
    if (InText == "iso-rect" || InText == "iso_rect" || InText == "isometric")
    {
        *OutMode = ProjectionMode::IsoRect;
        return true;
    }
    if (InText == "iso-hex" || InText == "iso_hex" || InText == "hex")
    {
        *OutMode = ProjectionMode::IsoHex;
        return true;
    }
    return false;
}

// Integer cell coordinate. Iso-hex uses axial coordinates (X = q, Y = r).
struct Cell
{
    std::int32_t X = 0;
    std::int32_t Y = 0;

    [[nodiscard]] friend constexpr bool operator==(const Cell&, const Cell&) = default;
};

[[nodiscard]] constexpr Cell MakeCell(std::int32_t InX, std::int32_t InY) noexcept
{
    return Cell{InX, InY};
}

// Grid description shared by the renderer, the editor and the navigation code.
// `CellSize` is the full cell extent on screen: a square for side/top-down, the
// full diamond (width, height) for iso-rect, and the full hex (width, height)
// for iso-hex. A regular hex assumes Height == Width * 2 / sqrt(3).
struct GridDesc
{
    ProjectionMode Mode = ProjectionMode::TopDown;
    Vector2D CellSize{32.0f, 32.0f};
    // World position of the center of cell (0, 0).
    Vector2D Origin{};
    // World units one cell of height adds to the draw depth. Used to lift
    // walls and props above the floor layer of the same cell.
    float HeightStep = 8.0f;

    [[nodiscard]] friend constexpr bool operator==(const GridDesc&, const GridDesc&) = default;
};

static_assert(std::is_trivially_copyable_v<GridDesc>);

[[nodiscard]] constexpr float HexSize(float InCellHeight) noexcept
{
    return InCellHeight * 0.5f;
}

[[nodiscard]] constexpr float RegularHexWidth(float InCellHeight) noexcept
{
    return InCellHeight * 0.86602540378f;
}

[[nodiscard]] inline Vector2D CellToWorld(const GridDesc& InGrid, Cell InCell) noexcept
{
    const float ScopedX = static_cast<float>(InCell.X);
    const float ScopedY = static_cast<float>(InCell.Y);
    switch (InGrid.Mode)
    {
        case ProjectionMode::Side:
        case ProjectionMode::TopDown:
        {
            return Vector2D{InGrid.Origin.X + ScopedX * InGrid.CellSize.X,
                            InGrid.Origin.Y + ScopedY * InGrid.CellSize.Y};
        }
        case ProjectionMode::IsoRect:
        {
            const float ScopedHalfWidth = InGrid.CellSize.X * 0.5f;
            const float ScopedHalfHeight = InGrid.CellSize.Y * 0.5f;
            return Vector2D{InGrid.Origin.X + (ScopedX - ScopedY) * ScopedHalfWidth,
                            InGrid.Origin.Y + (ScopedX + ScopedY) * ScopedHalfHeight};
        }
        case ProjectionMode::IsoHex:
        {
            const float ScopedSize = HexSize(InGrid.CellSize.Y);
            const float ScopedWidth = InGrid.CellSize.X > 0.0f ? InGrid.CellSize.X : RegularHexWidth(InGrid.CellSize.Y);
            const float ScopedHalfWidth = ScopedWidth * 0.5f;
            return Vector2D{InGrid.Origin.X + ScopedHalfWidth * (ScopedX + ScopedY * 0.5f),
                            InGrid.Origin.Y + ScopedSize * 1.5f * ScopedY};
        }
        case ProjectionMode::Count:
            break;
    }
    return InGrid.Origin;
}

namespace Detail
{
[[nodiscard]] inline Cell RoundAxial(float InQ, float InR) noexcept
{
    const float ScopedS = -InQ - InR;
    float ScopedQ = std::round(InQ);
    float ScopedR = std::round(InR);
    const float ScopedSum = std::round(ScopedS);
    const float ScopedQDiff = std::fabs(ScopedQ - InQ);
    const float ScopedRDiff = std::fabs(ScopedR - InR);
    const float ScopedSDiff = std::fabs(ScopedSum - ScopedS);
    if (ScopedQDiff > ScopedRDiff && ScopedQDiff > ScopedSDiff)
    {
        ScopedQ = -ScopedR - ScopedSum;
    }
    else if (ScopedRDiff > ScopedSDiff)
    {
        ScopedR = -ScopedQ - ScopedSum;
    }
    return Cell{static_cast<std::int32_t>(ScopedQ), static_cast<std::int32_t>(ScopedR)};
}
}

// Inverse of CellToWorld. Rounding selects the cell whose center is nearest, so
// the result is stable along the cell borders.
[[nodiscard]] inline Cell WorldToCell(const GridDesc& InGrid, Vector2D InWorld) noexcept
{
    const float ScopedDx = InWorld.X - InGrid.Origin.X;
    const float ScopedDy = InWorld.Y - InGrid.Origin.Y;
    switch (InGrid.Mode)
    {
        case ProjectionMode::Side:
        case ProjectionMode::TopDown:
        {
            const float ScopedCellWidth = InGrid.CellSize.X != 0.0f ? InGrid.CellSize.X : 1.0f;
            const float ScopedCellHeight = InGrid.CellSize.Y != 0.0f ? InGrid.CellSize.Y : 1.0f;
            return Cell{static_cast<std::int32_t>(std::lround(ScopedDx / ScopedCellWidth)),
                        static_cast<std::int32_t>(std::lround(ScopedDy / ScopedCellHeight))};
        }
        case ProjectionMode::IsoRect:
        {
            const float ScopedHalfWidth = InGrid.CellSize.X > 0.0f ? InGrid.CellSize.X * 0.5f : 1.0f;
            const float ScopedHalfHeight = InGrid.CellSize.Y > 0.0f ? InGrid.CellSize.Y * 0.5f : 1.0f;
            const float ScopedA = ScopedDx / ScopedHalfWidth;
            const float ScopedB = ScopedDy / ScopedHalfHeight;
            const float ScopedCellX = (ScopedA + ScopedB) * 0.5f;
            const float ScopedCellY = (ScopedB - ScopedA) * 0.5f;
            return Cell{static_cast<std::int32_t>(std::lround(ScopedCellX)),
                        static_cast<std::int32_t>(std::lround(ScopedCellY))};
        }
        case ProjectionMode::IsoHex:
        {
            const float ScopedSize = HexSize(InGrid.CellSize.Y) > 0.0f ? HexSize(InGrid.CellSize.Y) : 1.0f;
            const float ScopedWidth = InGrid.CellSize.X > 0.0f ? InGrid.CellSize.X : RegularHexWidth(InGrid.CellSize.Y);
            const float ScopedHalfWidth = ScopedWidth * 0.5f;
            const float ScopedQ = (ScopedDx / ScopedHalfWidth - ScopedDy / (ScopedSize * 1.5f) * 0.5f);
            const float ScopedR = ScopedDy / (ScopedSize * 1.5f);
            return Detail::RoundAxial(ScopedQ, ScopedR);
        }
        case ProjectionMode::Count:
            break;
    }
    return Cell{};
}

// Painter's-algorithm depth for a cell. Ascending values draw first, so a
// larger value is closer to the camera. The formula is monotonic in the
// projection's screen Y and adds a deterministic tie-break for the cells that
// share a row on the iso diamond.
[[nodiscard]] inline float SortDepth(const GridDesc& InGrid, Cell InCell) noexcept
{
    const Vector2D ScopedWorld = CellToWorld(InGrid, InCell);
    switch (InGrid.Mode)
    {
        case ProjectionMode::Side:
        {
            // Side view: cells with a larger Y are nearer to the floor.
            return ScopedWorld.Y;
        }
        case ProjectionMode::TopDown:
        {
            return ScopedWorld.Y;
        }
        case ProjectionMode::IsoRect:
        {
            const float ScopedRow = static_cast<float>(InCell.X + InCell.Y);
            const float ScopedTieBreak = static_cast<float>(InCell.X) * 0.01f;
            return ScopedRow * (InGrid.CellSize.Y * 0.5f) + ScopedTieBreak;
        }
        case ProjectionMode::IsoHex:
        {
            const float ScopedRow = ScopedWorld.Y;
            const float ScopedTieBreak = static_cast<float>(InCell.X) * 0.01f;
            return ScopedRow + ScopedTieBreak;
        }
        case ProjectionMode::Count:
            break;
    }
    return 0.0f;
}

// Half extent of one cell quad on screen. A diamond tile and a hex tile are
// both drawn as one quad centered on CellToWorld.
[[nodiscard]] inline Vector2D CellExtent(const GridDesc& InGrid) noexcept
{
    if (InGrid.Mode == ProjectionMode::IsoHex && InGrid.CellSize.X <= 0.0f)
    {
        return Vector2D{RegularHexWidth(InGrid.CellSize.Y), InGrid.CellSize.Y};
    }
    return InGrid.CellSize;
}

[[nodiscard]] constexpr std::uint32_t NeighborCount(ProjectionMode InMode) noexcept
{
    switch (InMode)
    {
        case ProjectionMode::IsoHex:
            return 6;
        case ProjectionMode::Side:
        case ProjectionMode::TopDown:
        case ProjectionMode::IsoRect:
            return 4;
        case ProjectionMode::Count:
            break;
    }
    return 4;
}

// Neighbour `InIndex` in [0, NeighborCount). Rect modes use the four axis
// neighbours; iso-hex uses the six axial neighbours.
[[nodiscard]] inline Cell Neighbor(ProjectionMode InMode, Cell InCell, std::uint32_t InIndex) noexcept
{
    static constexpr Cell kRect[4] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    static constexpr Cell kHex[6] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, -1}, {-1, 1}};
    if (InMode == ProjectionMode::IsoHex)
    {
        const Cell ScopedDelta = kHex[InIndex % 6];
        return Cell{InCell.X + ScopedDelta.X, InCell.Y + ScopedDelta.Y};
    }
    const Cell ScopedDelta = kRect[InIndex % 4];
    return Cell{InCell.X + ScopedDelta.X, InCell.Y + ScopedDelta.Y};
}

// Distance between two cells in cell units. Rect modes use the octile metric
// (diagonal moves cost sqrt(2)); hex uses the cube distance.
[[nodiscard]] inline float CellDistance(ProjectionMode InMode, Cell InA, Cell InB) noexcept
{
    const float ScopedDx = static_cast<float>(InA.X - InB.X);
    const float ScopedDy = static_cast<float>(InA.Y - InB.Y);
    if (InMode == ProjectionMode::IsoHex)
    {
        const float ScopedDz = static_cast<float>((InA.X - InB.X) + (InA.Y - InB.Y));
        return (std::fabs(ScopedDx) + std::fabs(ScopedDy) + std::fabs(ScopedDz)) * 0.5f;
    }
    const float ScopedAbsX = std::fabs(ScopedDx);
    const float ScopedAbsY = std::fabs(ScopedDy);
    const float ScopedMin = ScopedAbsX < ScopedAbsY ? ScopedAbsX : ScopedAbsY;
    const float ScopedMax = ScopedAbsX < ScopedAbsY ? ScopedAbsY : ScopedAbsX;
    return ScopedMax + ScopedMin * 0.41421356f;
}

// Cell of the entity under a world position, clamped to a grid rectangle.
[[nodiscard]] inline Cell ClampCell(Cell InCell, std::int32_t InWidth, std::int32_t InHeight) noexcept
{
    Cell ScopedCell = InCell;
    if (ScopedCell.X < 0)
    {
        ScopedCell.X = 0;
    }
    if (ScopedCell.Y < 0)
    {
        ScopedCell.Y = 0;
    }
    if (InWidth > 0 && ScopedCell.X >= InWidth)
    {
        ScopedCell.X = InWidth - 1;
    }
    if (InHeight > 0 && ScopedCell.Y >= InHeight)
    {
        ScopedCell.Y = InHeight - 1;
    }
    return ScopedCell;
}

// A 2D orthographic camera: the world point at the viewport centre, the
// viewport in pixels and a pixels-per-world-unit zoom. The projection itself is
// orthographic for every layout; isometry lives in the cell/world mapping, so a
// camera only needs this mapping plus the grid.
struct CameraView
{
    Vector2D Center{};
    Vector2D ViewportSize{1.0f, 1.0f};
    float Zoom = 1.0f;
};

[[nodiscard]] inline Vector2D WorldToScreen(const CameraView& InCamera, Vector2D InWorld) noexcept
{
    const float ScopedZoom = InCamera.Zoom > 0.0001f ? InCamera.Zoom : 0.0001f;
    return Vector2D{(InWorld.X - InCamera.Center.X) * ScopedZoom + InCamera.ViewportSize.X * 0.5f,
                    (InWorld.Y - InCamera.Center.Y) * ScopedZoom + InCamera.ViewportSize.Y * 0.5f};
}

[[nodiscard]] inline Vector2D ScreenToWorld(const CameraView& InCamera, Vector2D InScreen) noexcept
{
    const float ScopedZoom = InCamera.Zoom > 0.0001f ? InCamera.Zoom : 0.0001f;
    return Vector2D{InCamera.Center.X + (InScreen.X - InCamera.ViewportSize.X * 0.5f) / ScopedZoom,
                    InCamera.Center.Y + (InScreen.Y - InCamera.ViewportSize.Y * 0.5f) / ScopedZoom};
}

// Screen pixel to cell for the editor's painting and picking paths.
[[nodiscard]] inline Cell ScreenToCell(const GridDesc& InGrid, const CameraView& InCamera,
                                       Vector2D InScreen) noexcept
{
    return WorldToCell(InGrid, ScreenToWorld(InCamera, InScreen));
}
}
