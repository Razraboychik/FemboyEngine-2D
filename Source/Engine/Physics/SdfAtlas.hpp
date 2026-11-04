// FemboyEngine2D - brick atlas packing for the GPU SDF field.
#pragma once

#include "Engine/Core/Types.hpp"
#include "Engine/Physics/SdfBrickMap.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace Fby
{

// A GPU-ready mirror of a brick map: a dense 16x16 tile per narrow brick packed
// into one square texture, plus the table that maps a brick to its tile. Empty
// and full bricks need no tile and are resolved from the table alone.
struct SdfAtlasImage
{
    static constexpr std::uint32_t kMaxTiles = 4096;

    std::uint32_t Width = 0;
    std::uint32_t Height = 0;
    std::uint32_t Columns = 0;
    std::uint32_t Rows = 0;
    std::vector<float> Texels;
    std::vector<SdfBrickGpu> Table;
};

// Packs every narrow brick into the atlas. The table keeps the brick grid and
// the min/max constants; PayloadOffset becomes the tile index.
[[nodiscard]] inline bool PackSdfAtlas(const SdfBrickMap& InMap, SdfAtlasImage* OutImage) noexcept
{
    if (OutImage == nullptr)
    {
        return false;
    }
    const std::span<const SdfBrickHeader> ScopedBricks = InMap.BrickView();
    const std::span<const float> ScopedPayload = InMap.PayloadView();
    const std::uint32_t ScopedNarrow = static_cast<std::uint32_t>(InMap.NarrowBrickCount());
    if (ScopedNarrow > SdfAtlasImage::kMaxTiles)
    {
        return false;
    }
    // A square atlas; a map with a single narrow brick still gets one tile.
    const std::uint32_t ScopedColumns = ScopedNarrow == 0
                                            ? 1u
                                            : static_cast<std::uint32_t>(
                                                  std::ceil(std::sqrt(static_cast<double>(ScopedNarrow))));
    const std::uint32_t ScopedRows = ScopedColumns;
    if (ScopedColumns > 64)
    {
        return false;
    }
    OutImage->Columns = ScopedColumns;
    OutImage->Rows = ScopedRows;
    OutImage->Width = ScopedColumns * SdfBrickSize;
    OutImage->Height = ScopedRows * SdfBrickSize;
    OutImage->Texels.assign(static_cast<std::size_t>(OutImage->Width) * OutImage->Height, 0.0f);
    OutImage->Table.assign(ScopedBricks.size(), SdfBrickGpu{});

    std::uint32_t ScopedTile = 0;
    for (std::size_t ScopedIndex = 0; ScopedIndex < ScopedBricks.size(); ++ScopedIndex)
    {
        const SdfBrickHeader& ScopedBrick = ScopedBricks[ScopedIndex];
        SdfBrickGpu& ScopedGpu = OutImage->Table[ScopedIndex];
        ScopedGpu.MinDistance = ScopedBrick.MinDistance;
        ScopedGpu.MaxDistance = ScopedBrick.MaxDistance;
        ScopedGpu.BrickX = ScopedBrick.BrickX;
        ScopedGpu.BrickY = ScopedBrick.BrickY;
        ScopedGpu.StateAndFlags = static_cast<std::uint32_t>(ScopedBrick.State);
        if (ScopedBrick.State != SdfBrickState::Narrow)
        {
            ScopedGpu.PayloadOffset = 0xFFFFFFFFu;
            continue;
        }
        const std::uint32_t ScopedTileX = ScopedTile % ScopedColumns;
        const std::uint32_t ScopedTileY = ScopedTile / ScopedColumns;
        for (std::uint32_t ScopedY = 0; ScopedY < SdfBrickSize; ++ScopedY)
        {
            for (std::uint32_t ScopedX = 0; ScopedX < SdfBrickSize; ++ScopedX)
            {
                const std::size_t ScopedSource = static_cast<std::size_t>(ScopedBrick.PayloadOffset) +
                                                 static_cast<std::size_t>(ScopedY) * SdfBrickSize + ScopedX;
                const std::size_t ScopedTarget = static_cast<std::size_t>(ScopedTileY * SdfBrickSize + ScopedY) *
                                                     OutImage->Width +
                                                 (ScopedTileX * SdfBrickSize + ScopedX);
                OutImage->Texels[ScopedTarget] = ScopedPayload[ScopedSource];
            }
        }
        ScopedGpu.PayloadOffset = ScopedTile;
        ++ScopedTile;
    }
    return true;
}

// CPU reference sampler for the packed atlas. It is the contract the GPU path
// must reproduce; tests compare it against SdfBrickMap::Sample.
[[nodiscard]] inline float SampleSdfAtlas(const SdfAtlasImage& InImage, const SdfBrickMap& InMap,
                                          Vector2D InWorld) noexcept
{
    const SdfBrickMapDesc ScopedDesc = InMap.Desc();
    if (ScopedDesc.SourceWidth < 2 || ScopedDesc.SourceHeight < 2 || InImage.Table.empty())
    {
        return ScopedDesc.OutsideDistance;
    }
    const float ScopedX = (InWorld.X - ScopedDesc.Origin.X) / ScopedDesc.CellSize.X;
    const float ScopedY = (InWorld.Y - ScopedDesc.Origin.Y) / ScopedDesc.CellSize.Y;
    if (ScopedX < 0.0f || ScopedY < 0.0f || ScopedX >= static_cast<float>(ScopedDesc.SourceWidth) ||
        ScopedY >= static_cast<float>(ScopedDesc.SourceHeight))
    {
        return ScopedDesc.OutsideDistance;
    }
    const std::uint32_t ScopedBrickX = static_cast<std::uint32_t>(ScopedX) / SdfBrickSize;
    const std::uint32_t ScopedBrickY = static_cast<std::uint32_t>(ScopedY) / SdfBrickSize;
    const std::size_t ScopedBrickIndex = static_cast<std::size_t>(ScopedBrickY) * ScopedDesc.BricksX + ScopedBrickX;
    if (ScopedBrickIndex >= InImage.Table.size())
    {
        return ScopedDesc.OutsideDistance;
    }
    const SdfBrickGpu& ScopedGpu = InImage.Table[ScopedBrickIndex];
    if (ScopedGpu.PayloadOffset == 0xFFFFFFFFu)
    {
        return ScopedGpu.StateAndFlags == static_cast<std::uint32_t>(SdfBrickState::Full) ? ScopedGpu.MaxDistance
                                                                                        : ScopedGpu.MinDistance;
    }
    const float ScopedLocalX = ScopedX - static_cast<float>(ScopedBrickX * SdfBrickSize);
    const float ScopedLocalY = ScopedY - static_cast<float>(ScopedBrickY * SdfBrickSize);
    const std::uint32_t ScopedX0 = std::min(static_cast<std::uint32_t>(ScopedLocalX), SdfBrickSize - 1u);
    const std::uint32_t ScopedY0 = std::min(static_cast<std::uint32_t>(ScopedLocalY), SdfBrickSize - 1u);
    const std::uint32_t ScopedX1 = std::min(ScopedX0 + 1u, SdfBrickSize - 1u);
    const std::uint32_t ScopedY1 = std::min(ScopedY0 + 1u, SdfBrickSize - 1u);
    const float ScopedFx = ScopedLocalX - static_cast<float>(ScopedX0);
    const float ScopedFy = ScopedLocalY - static_cast<float>(ScopedY0);
    const std::uint32_t ScopedTileX = ScopedGpu.PayloadOffset % InImage.Columns;
    const std::uint32_t ScopedTileY = ScopedGpu.PayloadOffset / InImage.Columns;
    const auto ScopedAt = [&InImage, ScopedTileX, ScopedTileY](std::uint32_t InX, std::uint32_t InY)
    {
        const std::size_t ScopedIndex =
            static_cast<std::size_t>(ScopedTileY * SdfBrickSize + InY) * InImage.Width + ScopedTileX * SdfBrickSize + InX;
        return InImage.Texels[ScopedIndex];
    };
    const float ScopedTop = ScopedAt(ScopedX0, ScopedY0) + (ScopedAt(ScopedX1, ScopedY0) - ScopedAt(ScopedX0, ScopedY0)) *
                                                               ScopedFx;
    const float ScopedBottom = ScopedAt(ScopedX0, ScopedY1) +
                               (ScopedAt(ScopedX1, ScopedY1) - ScopedAt(ScopedX0, ScopedY1)) * ScopedFx;
    return ScopedTop + (ScopedBottom - ScopedTop) * ScopedFy;
}

}
