// FemboyEngine2D - virtual texture address contract shared by the CPU residency
// system, the page table and the shaders.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>

namespace Fby::Rhi
{

// A page-table entry packs the VRAM layer and the slot inside that layer.
// 0xFFFFFFFF means "not resident"; the shader must fall back to a neutral texel.
inline constexpr std::uint32_t kVirtualTexturePageTableMissing = 0xFFFFFFFFu;
inline constexpr std::uint32_t kVirtualTextureMaxPageTableLayer = 0xFFu;
inline constexpr std::uint32_t kVirtualTextureMaxPageTableSlot = 0xFFFFu;
inline constexpr std::uint32_t kVirtualTextureLayerBits = 8;
inline constexpr std::uint32_t kVirtualTextureSlotBits = 16;

// Feedback is one R32_UINT per sampled pixel: the world tile the pixel wants.
// Bit 31 marks a valid record (empty records are zero), axes are 15-bit signed
// offsets around zero: encodable range is [-16383, 16383] per axis.
inline constexpr std::uint32_t kVirtualTextureFeedbackEmpty = 0u;
inline constexpr std::uint32_t kVirtualTextureFeedbackValidBit = 0x80000000u;
inline constexpr std::int32_t kVirtualTextureMaxFeedbackTile = 16383;
inline constexpr std::int32_t kVirtualTextureFeedbackBias = 0x4000;

// The slot grid of one layer: pageSize / tileSize cells per axis.
[[nodiscard]] constexpr std::uint32_t VirtualTextureTilesPerAxis(std::uint32_t InPageSize,
                                                                std::uint32_t InTileSize) noexcept
{
    if (InTileSize == 0u)
    {
        return 0u;
    }
    return InPageSize / InTileSize;
}

[[nodiscard]] constexpr std::uint32_t VirtualTextureSlotsPerLayer(std::uint32_t InPageSize,
                                                                 std::uint32_t InTileSize) noexcept
{
    const std::uint32_t ScopedAxis = VirtualTextureTilesPerAxis(InPageSize, InTileSize);
    return ScopedAxis * ScopedAxis;
}

[[nodiscard]] constexpr std::uint32_t PackVirtualTexturePageTable(std::uint32_t InLayer,
                                                                  std::uint32_t InSlot) noexcept
{
    if (InLayer > kVirtualTextureMaxPageTableLayer || InSlot > kVirtualTextureMaxPageTableSlot)
    {
        return kVirtualTexturePageTableMissing;
    }
    return (InLayer << kVirtualTextureSlotBits) | InSlot;
}

// Returns false for the missing sentinel (or an entry with reserved bits set).
[[nodiscard]] constexpr bool UnpackVirtualTexturePageTable(std::uint32_t InEntry, std::uint32_t* OutLayer,
                                                           std::uint32_t* OutSlot) noexcept
{
    if (InEntry == kVirtualTexturePageTableMissing || InEntry > 0x00FFFFFFu)
    {
        return false;
    }
    if (OutLayer != nullptr)
    {
        *OutLayer = (InEntry >> kVirtualTextureSlotBits) & 0xFFu;
    }
    if (OutSlot != nullptr)
    {
        *OutSlot = InEntry & 0xFFFFu;
    }
    return true;
}

// Slot index from a cell inside one layer's page grid.
[[nodiscard]] constexpr std::uint32_t VirtualTextureSlotFromCell(std::uint32_t InTilesPerAxis, std::uint32_t InCellX,
                                                                std::uint32_t InCellY) noexcept
{
    if (InTilesPerAxis == 0u || InCellX >= InTilesPerAxis || InCellY >= InTilesPerAxis)
    {
        return kVirtualTextureMaxPageTableSlot + 1u;
    }
    return InCellY * InTilesPerAxis + InCellX;
}

[[nodiscard]] constexpr bool VirtualTextureCellFromSlot(std::uint32_t InTilesPerAxis, std::uint32_t InSlot,
                                                        std::uint32_t* OutCellX, std::uint32_t* OutCellY) noexcept
{
    if (InTilesPerAxis == 0u || InSlot >= InTilesPerAxis * InTilesPerAxis)
    {
        return false;
    }
    if (OutCellX != nullptr)
    {
        *OutCellX = InSlot % InTilesPerAxis;
    }
    if (OutCellY != nullptr)
    {
        *OutCellY = InSlot / InTilesPerAxis;
    }
    return true;
}

// Texel origin of a slot inside its layer page.
[[nodiscard]] constexpr std::uint32_t VirtualTextureSlotOriginX(std::uint32_t InTileSize, std::uint32_t InTilesPerAxis,
                                                                std::uint32_t InSlot) noexcept
{
    std::uint32_t ScopedCellX = 0;
    if (!VirtualTextureCellFromSlot(InTilesPerAxis, InSlot, &ScopedCellX, nullptr))
    {
        return 0;
    }
    return ScopedCellX * InTileSize;
}

[[nodiscard]] constexpr std::uint32_t VirtualTextureSlotOriginY(std::uint32_t InTileSize, std::uint32_t InTilesPerAxis,
                                                                std::uint32_t InSlot) noexcept
{
    std::uint32_t ScopedCellY = 0;
    if (!VirtualTextureCellFromSlot(InTilesPerAxis, InSlot, nullptr, &ScopedCellY))
    {
        return 0;
    }
    return ScopedCellY * InTileSize;
}

// World <-> tile. Tiles are floor-divided so negative coordinates stay on a
// stable grid (a tile always covers [tile * size, (tile + 1) * size)).
[[nodiscard]] inline std::int32_t VirtualTextureWorldToTile(float InWorld, float InWorldTileSize) noexcept
{
    if (!(InWorldTileSize > 0.0f))
    {
        return 0;
    }
    return static_cast<std::int32_t>(std::floor(InWorld / InWorldTileSize));
}

[[nodiscard]] inline float VirtualTextureTileToWorld(std::int32_t InTile, float InWorldTileSize) noexcept
{
    return static_cast<float>(InTile) * InWorldTileSize;
}

// UV inside the tile: [0, 1).
[[nodiscard]] inline float VirtualTextureTileLocalUv(float InWorld, float InWorldTileSize) noexcept
{
    if (!(InWorldTileSize > 0.0f))
    {
        return 0.0f;
    }
    const float ScopedScaled = InWorld / InWorldTileSize;
    return ScopedScaled - std::floor(ScopedScaled);
}

// Layer selection must match the CPU residency system so a tile always predicts
// the same VRAM layer. The hash is FNV-style and independent of the page size.
[[nodiscard]] inline std::uint64_t VirtualTextureTileHash(std::int64_t InTileX, std::int64_t InTileY) noexcept
{
    const std::uint64_t ScopedX = static_cast<std::uint64_t>(InTileX);
    const std::uint64_t ScopedY = static_cast<std::uint64_t>(InTileY);
    // Asymmetric cross term: a plain "x * C1 ^ y * C2" maps a tile and its
    // mirrored neighbour to the same layer (verified collision), which would
    // cluster mirrored chunks in VRAM.
    std::uint64_t ScopedHash = (ScopedX * 0x9E3779B185EBCA87ull) ^ (ScopedY * 0xC2B2AE3D27D4EB4Full);
    ScopedHash ^= (ScopedX + 0x9E3779B97F4A7C15ull) * (ScopedY + 0xC2B2AE3D27D4EB4Full);
    ScopedHash ^= ScopedHash >> 33u;
    ScopedHash *= 0xFF51AFD7ED558CCDull;
    ScopedHash ^= ScopedHash >> 33u;
    ScopedHash *= 0xC4CEB9FE1A85EC53ull;
    ScopedHash ^= ScopedHash >> 33u;
    return ScopedHash;
}

[[nodiscard]] inline std::uint32_t VirtualTextureLayerForTile(std::int64_t InTileX, std::int64_t InTileY,
                                                              std::uint32_t InLayerCount) noexcept
{
    if (InLayerCount == 0u)
    {
        return 0u;
    }
    return static_cast<std::uint32_t>(VirtualTextureTileHash(InTileX, InTileY) % InLayerCount);
}

// Feedback packing. Coordinates are clamped into the encodable int16 range; the
// empty sentinel is never produced by a valid pair.
[[nodiscard]] constexpr std::uint32_t PackVirtualTextureFeedback(std::int32_t InTileX, std::int32_t InTileY) noexcept
{
    const std::int32_t ScopedX = std::clamp(InTileX, -kVirtualTextureMaxFeedbackTile, kVirtualTextureMaxFeedbackTile);
    const std::int32_t ScopedY = std::clamp(InTileY, -kVirtualTextureMaxFeedbackTile, kVirtualTextureMaxFeedbackTile);
    const auto ScopedLow = static_cast<std::uint32_t>((ScopedX + kVirtualTextureFeedbackBias) & 0x7FFF);
    const auto ScopedHigh = static_cast<std::uint32_t>((ScopedY + kVirtualTextureFeedbackBias) & 0x7FFF);
    return kVirtualTextureFeedbackValidBit | ScopedLow | (ScopedHigh << 16u);
}

[[nodiscard]] constexpr bool UnpackVirtualTextureFeedback(std::uint32_t InEntry, std::int32_t* OutTileX,
                                                          std::int32_t* OutTileY) noexcept
{
    if ((InEntry & kVirtualTextureFeedbackValidBit) == 0u)
    {
        return false;
    }
    if (OutTileX != nullptr)
    {
        *OutTileX = static_cast<std::int32_t>(InEntry & 0x7FFFu) - kVirtualTextureFeedbackBias;
    }
    if (OutTileY != nullptr)
    {
        *OutTileY = static_cast<std::int32_t>((InEntry >> 16u) & 0x7FFFu) - kVirtualTextureFeedbackBias;
    }
    return true;
}

// Feedback capacity: one tile id per visible page; the CPU dedups and drops the
// rest instead of growing a container.
inline constexpr std::uint32_t kMaxVirtualTextureFeedbackTiles = 4096;

// Dedups packed feedback texels (as written by the sprite shader) into `OutTiles`
// and returns the number of unique resident requests. Empty records are skipped;
// requests beyond the output capacity are dropped.
[[nodiscard]] inline std::uint32_t DeduplicateVirtualTextureFeedback(std::span<const std::uint32_t> InTexels,
                                                                    std::span<std::uint32_t> OutTiles) noexcept
{
    std::uint32_t ScopedCount = 0;
    for (const std::uint32_t ScopedEntry : InTexels)
    {
        if (ScopedCount >= OutTiles.size() || ScopedCount >= kMaxVirtualTextureFeedbackTiles)
        {
            break;
        }
        std::int32_t ScopedTileX = 0;
        std::int32_t ScopedTileY = 0;
        if (!UnpackVirtualTextureFeedback(ScopedEntry, &ScopedTileX, &ScopedTileY))
        {
            continue;
        }
        const std::uint32_t ScopedPacked = PackVirtualTextureFeedback(ScopedTileX, ScopedTileY);
        bool ScopedSeen = false;
        for (std::uint32_t ScopedI = 0; ScopedI < ScopedCount; ++ScopedI)
        {
            if (OutTiles[ScopedI] == ScopedPacked)
            {
                ScopedSeen = true;
                break;
            }
        }
        if (!ScopedSeen)
        {
            OutTiles[ScopedCount++] = ScopedPacked;
        }
    }
    return ScopedCount;
}
}
