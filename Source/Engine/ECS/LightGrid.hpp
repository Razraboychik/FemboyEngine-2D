// FemboyEngine2D - screen-space light grid for CPU light culling.
#pragma once

#include "Engine/Core/Types.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <vector>

namespace Fby
{

// One light footprint inserted into the grid.
struct LightGridInput
{
    Vector2D Position{};
    float Radius = 1.0f;
};

// A dense uniform grid over the union of light footprints. A query returns only
// the lights whose footprint overlaps the queried cell, so the lighting loop no
// longer walks every light for every sprite. Oversized grids fall back to the
// full light list instead of allocating an unbounded table.
class LightGrid2D
{
public:
    static constexpr std::uint32_t MaxCells = 8192u;

    void Build(std::span<const LightGridInput> InLights, float InCellSize = 128.0f)
    {
        MCellOffsets.clear();
        MIndices.clear();
        MFallback.clear();
        MFallbackMode = false;
        MCellSize = std::max(InCellSize, 1.0f);
        MCellsX = 0;
        MCellsY = 0;
        if (InLights.empty())
        {
            return;
        }

        Vector2D ScopedMin = InLights[0].Position;
        Vector2D ScopedMax = InLights[0].Position;
        for (const LightGridInput& ScopedLight : InLights)
        {
            if (!std::isfinite(ScopedLight.Position.X) || !std::isfinite(ScopedLight.Position.Y) ||
                !std::isfinite(ScopedLight.Radius))
            {
                MFallbackMode = true;
                MFallback.resize(InLights.size());
                for (std::uint32_t ScopedIndex = 0; ScopedIndex < InLights.size(); ++ScopedIndex)
                {
                    MFallback[ScopedIndex] = ScopedIndex;
                }
                return;
            }
            const float ScopedRadius = std::max(ScopedLight.Radius, 1.0f);
            ScopedMin.X = std::min(ScopedMin.X, ScopedLight.Position.X - ScopedRadius);
            ScopedMin.Y = std::min(ScopedMin.Y, ScopedLight.Position.Y - ScopedRadius);
            ScopedMax.X = std::max(ScopedMax.X, ScopedLight.Position.X + ScopedRadius);
            ScopedMax.Y = std::max(ScopedMax.Y, ScopedLight.Position.Y + ScopedRadius);
        }
        MMin = ScopedMin;
        MMax = ScopedMax;
        const float ScopedWidth = std::max(MMax.X - MMin.X, MCellSize);
        const float ScopedHeight = std::max(MMax.Y - MMin.Y, MCellSize);
        if (!std::isfinite(ScopedWidth) || !std::isfinite(ScopedHeight))
        {
            MFallbackMode = true;
            MFallback.resize(InLights.size());
            for (std::uint32_t ScopedIndex = 0; ScopedIndex < InLights.size(); ++ScopedIndex)
            {
                MFallback[ScopedIndex] = ScopedIndex;
            }
            return;
        }
        const double ScopedCellsXDouble = std::ceil(ScopedWidth / MCellSize) + 1.0;
        const double ScopedCellsYDouble = std::ceil(ScopedHeight / MCellSize) + 1.0;
        if (ScopedCellsXDouble > MaxCells || ScopedCellsYDouble > MaxCells ||
            ScopedCellsXDouble * ScopedCellsYDouble > MaxCells)
        {
            MFallbackMode = true;
            MFallback.resize(InLights.size());
            for (std::uint32_t ScopedIndex = 0; ScopedIndex < InLights.size(); ++ScopedIndex)
            {
                MFallback[ScopedIndex] = ScopedIndex;
            }
            return;
        }
        MCellsX = static_cast<std::uint32_t>(ScopedCellsXDouble);
        MCellsY = static_cast<std::uint32_t>(ScopedCellsYDouble);
        const std::uint32_t ScopedCellCount = MCellsX * MCellsY;
        std::vector<std::uint32_t> ScopedCounts(ScopedCellCount, 0u);
        std::vector<std::uint32_t> ScopedRanges(InLights.size() * 2u, 0u);
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < InLights.size(); ++ScopedIndex)
        {
            const LightGridInput& ScopedLight = InLights[ScopedIndex];
            const std::uint32_t ScopedMinX = CellX(ScopedLight.Position.X - ScopedLight.Radius);
            const std::uint32_t ScopedMaxX = CellX(ScopedLight.Position.X + ScopedLight.Radius);
            const std::uint32_t ScopedMinY = CellY(ScopedLight.Position.Y - ScopedLight.Radius);
            const std::uint32_t ScopedMaxY = CellY(ScopedLight.Position.Y + ScopedLight.Radius);
            ScopedRanges[ScopedIndex * 2u] = (ScopedMinY * MCellsX) + ScopedMinX;
            ScopedRanges[ScopedIndex * 2u + 1u] = (ScopedMaxY * MCellsX) + ScopedMaxX;
            for (std::uint32_t ScopedY = ScopedMinY; ScopedY <= ScopedMaxY; ++ScopedY)
            {
                for (std::uint32_t ScopedX = ScopedMinX; ScopedX <= ScopedMaxX; ++ScopedX)
                {
                    ++ScopedCounts[ScopedY * MCellsX + ScopedX];
                }
            }
        }
        MCellOffsets.resize(ScopedCellCount + 1u, 0u);
        for (std::uint32_t ScopedCell = 0; ScopedCell < ScopedCellCount; ++ScopedCell)
        {
            MCellOffsets[ScopedCell + 1u] = MCellOffsets[ScopedCell] + ScopedCounts[ScopedCell];
        }
        MIndices.resize(MCellOffsets.back());
        std::vector<std::uint32_t> ScopedCursor(MCellOffsets.begin(), MCellOffsets.end() - 1);
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < InLights.size(); ++ScopedIndex)
        {
            const std::uint32_t ScopedMinCell = ScopedRanges[ScopedIndex * 2u];
            const std::uint32_t ScopedMaxCell = ScopedRanges[ScopedIndex * 2u + 1u];
            const std::uint32_t ScopedMinX = ScopedMinCell % MCellsX;
            const std::uint32_t ScopedMaxX = ScopedMaxCell % MCellsX;
            const std::uint32_t ScopedMinY = ScopedMinCell / MCellsX;
            const std::uint32_t ScopedMaxY = ScopedMaxCell / MCellsX;
            for (std::uint32_t ScopedY = ScopedMinY; ScopedY <= ScopedMaxY; ++ScopedY)
            {
                for (std::uint32_t ScopedX = ScopedMinX; ScopedX <= ScopedMaxX; ++ScopedX)
                {
                    const std::uint32_t ScopedCell = ScopedY * MCellsX + ScopedX;
                    MIndices[ScopedCursor[ScopedCell]++] = ScopedIndex;
                }
            }
        }
    }

    [[nodiscard]] std::span<const std::uint32_t> Query(Vector2D InPoint) const noexcept
    {
        if (MFallbackMode)
        {
            return MFallback;
        }
        if (MCellOffsets.empty() || InPoint.X < MMin.X || InPoint.Y < MMin.Y || InPoint.X > MMax.X ||
            InPoint.Y > MMax.Y)
        {
            return {};
        }
        const std::uint32_t ScopedCell = CellY(InPoint.Y) * MCellsX + CellX(InPoint.X);
        const std::uint32_t ScopedBegin = MCellOffsets[ScopedCell];
        const std::uint32_t ScopedEnd = MCellOffsets[ScopedCell + 1u];
        return std::span<const std::uint32_t>(MIndices.data() + ScopedBegin, ScopedEnd - ScopedBegin);
    }

    [[nodiscard]] bool Fallback() const noexcept
    {
        return MFallbackMode;
    }

    [[nodiscard]] std::uint32_t CellCount() const noexcept
    {
        return MCellsX * MCellsY;
    }

private:
    [[nodiscard]] std::uint32_t CellX(float InX) const noexcept
    {
        if (MCellsX == 0)
        {
            return 0;
        }
        const float ScopedValue = (InX - MMin.X) / MCellSize;
        if (!std::isfinite(ScopedValue) || ScopedValue <= 0.0f)
        {
            return 0;
        }
        if (ScopedValue >= static_cast<float>(MCellsX - 1u))
        {
            return MCellsX - 1u;
        }
        return static_cast<std::uint32_t>(ScopedValue);
    }

    [[nodiscard]] std::uint32_t CellY(float InY) const noexcept
    {
        if (MCellsY == 0)
        {
            return 0;
        }
        const float ScopedValue = (InY - MMin.Y) / MCellSize;
        if (!std::isfinite(ScopedValue) || ScopedValue <= 0.0f)
        {
            return 0;
        }
        if (ScopedValue >= static_cast<float>(MCellsY - 1u))
        {
            return MCellsY - 1u;
        }
        return static_cast<std::uint32_t>(ScopedValue);
    }

    Vector2D MMin{};
    Vector2D MMax{};
    float MCellSize = 128.0f;
    std::uint32_t MCellsX = 0;
    std::uint32_t MCellsY = 0;
    bool MFallbackMode = false;
    std::vector<std::uint32_t> MCellOffsets;
    std::vector<std::uint32_t> MIndices;
    std::vector<std::uint32_t> MFallback;
};
}
