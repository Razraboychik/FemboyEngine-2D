// FemboyEngine2D - pre-fracture: split a destructible into grid pieces.
#pragma once

#include "Engine/Physics/SdfDestructible.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace Fby
{

// Splits a destructible into a Columns x Rows grid of independent pieces. Each
// piece keeps the parent base shape (pinned at the parent centre) and the full op
// list, so its field equals the parent field inside its own footprint and a hole
// that crosses a piece border still lands in both pieces.
[[nodiscard]] inline bool FractureGrid(const SdfDestructible& InSource, std::uint32_t InColumns, std::uint32_t InRows,
                                       std::vector<SdfDestructible>* OutPieces) noexcept
{
    if (OutPieces == nullptr || !InSource.IsConfigured() || InColumns == 0 || InRows == 0)
    {
        return false;
    }
    const SdfField& ScopedField = InSource.Field();
    const std::uint32_t ScopedWidth = ScopedField.width;
    const std::uint32_t ScopedHeight = ScopedField.height;
    // A piece needs at least two cells so its field can interpolate.
    const std::uint32_t ScopedColumns = std::min(InColumns, ScopedWidth / 2u);
    const std::uint32_t ScopedRows = std::min(InRows, ScopedHeight / 2u);
    if (ScopedColumns == 0 || ScopedRows == 0)
    {
        return false;
    }
    const Vector2D ScopedBaseCenter{
        InSource.Desc().HasBaseCenter
            ? InSource.Desc().BaseCenter
            : Vector2D{ScopedField.origin.X + static_cast<float>(ScopedWidth) * ScopedField.cellSize.X * 0.5f,
                       ScopedField.origin.Y + static_cast<float>(ScopedHeight) * ScopedField.cellSize.Y * 0.5f}};

    std::vector<SdfDestructible> ScopedPieces;
    ScopedPieces.reserve(static_cast<std::size_t>(ScopedColumns) * ScopedRows);
    for (std::uint32_t ScopedRow = 0; ScopedRow < ScopedRows; ++ScopedRow)
    {
        const std::uint32_t ScopedY0 = ScopedRow * ScopedHeight / ScopedRows;
        const std::uint32_t ScopedY1 = (ScopedRow + 1u) * ScopedHeight / ScopedRows;
        for (std::uint32_t ScopedColumn = 0; ScopedColumn < ScopedColumns; ++ScopedColumn)
        {
            const std::uint32_t ScopedX0 = ScopedColumn * ScopedWidth / ScopedColumns;
            const std::uint32_t ScopedX1 = (ScopedColumn + 1u) * ScopedWidth / ScopedColumns;

            SdfDestructibleDesc ScopedDesc;
            ScopedDesc.Origin = Vector2D{ScopedField.origin.X + static_cast<float>(ScopedX0) * ScopedField.cellSize.X,
                                         ScopedField.origin.Y + static_cast<float>(ScopedY0) * ScopedField.cellSize.Y};
            ScopedDesc.CellSize = ScopedField.cellSize;
            ScopedDesc.Width = ScopedX1 - ScopedX0;
            ScopedDesc.Height = ScopedY1 - ScopedY0;
            ScopedDesc.BaseShape = InSource.Desc().BaseShape;
            ScopedDesc.BaseCenter = ScopedBaseCenter;
            ScopedDesc.HasBaseCenter = true;
            ScopedDesc.HalfExtents = InSource.Desc().HalfExtents;
            ScopedDesc.Radius = InSource.Desc().Radius;
            ScopedDesc.NarrowBand = InSource.Desc().NarrowBand;
            ScopedDesc.OutsideDistance = InSource.Desc().OutsideDistance;

            SdfDestructible ScopedPiece;
            if (!ScopedPiece.Configure(ScopedDesc))
            {
                return false;
            }
            for (const SdfCarveOp& ScopedOp : InSource.Ops())
            {
                if (!ScopedPiece.Subtract(ScopedOp))
                {
                    return false;
                }
            }
            ScopedPieces.push_back(std::move(ScopedPiece));
        }
    }
    *OutPieces = std::move(ScopedPieces);
    return true;
}

}
