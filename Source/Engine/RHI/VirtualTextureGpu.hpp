// FemboyEngine2D - bridge from the CPU virtual-texture residency to RHI pages.
#pragma once

#include "Engine/RHI/RHI.hpp"
#include "Engine/RHI/VirtualTextureFormat.hpp"
#include "Engine/RHI/VirtualTextureSystem.hpp"

namespace Fby::Rhi
{

struct VirtualTexturePageLocation
{
    std::uint32_t SlotX = 0;
    std::uint32_t SlotY = 0;
};

// Maps one residency upload (texel origin inside a layer page) to the slot grid.
// Returns false when the upload does not lie on a tile boundary, exceeds a tile,
// or falls outside the page grid.
[[nodiscard]] inline bool MapVirtualTextureTile(const VirtualTextureTileUpload& InUpload, std::uint32_t InTileSize,
                                                std::uint32_t InTilesPerAxis,
                                                VirtualTexturePageLocation* OutLocation) noexcept
{
    if (InTileSize == 0u || InTilesPerAxis == 0u || InUpload.width == 0u || InUpload.height == 0u)
    {
        return false;
    }
    if (InUpload.width > InTileSize || InUpload.height > InTileSize)
    {
        return false;
    }
    if (InUpload.originX % InTileSize != 0u || InUpload.originY % InTileSize != 0u)
    {
        return false;
    }
    if (InUpload.rowPitch < InUpload.width * 4u ||
        InUpload.pixels.size() < static_cast<std::size_t>(InUpload.rowPitch) * InUpload.height)
    {
        return false;
    }
    const std::uint32_t ScopedSlotX = InUpload.originX / InTileSize;
    const std::uint32_t ScopedSlotY = InUpload.originY / InTileSize;
    if (ScopedSlotX >= InTilesPerAxis || ScopedSlotY >= InTilesPerAxis)
    {
        return false;
    }
    if (OutLocation != nullptr)
    {
        OutLocation->SlotX = ScopedSlotX;
        OutLocation->SlotY = ScopedSlotY;
    }
    return true;
}

// CPU mirror of the sprite shader's VT sampling. The shader computes the same
// values with textureSize/texelFetch; keeping the formulas here lets tests cover
// the math without a GPU and keeps the two paths aligned.
struct VirtualTextureSample
{
    std::uint32_t Layer = 0;
    std::uint32_t SlotX = 0;
    std::uint32_t SlotY = 0;
    float LocalU = 0.0f;
    float LocalV = 0.0f;
};

[[nodiscard]] inline bool ResolveVirtualTextureSample(Vector2D InWorld, float InWorldTileSize, Vector2D InTableOrigin,
                                                      std::uint32_t InTableSize,
                                                      std::span<const std::uint32_t> InTable,
                                                      std::uint32_t InTileSize, std::uint32_t InPageSize,
                                                      VirtualTextureSample* OutSample) noexcept
{
    if (!(InWorldTileSize > 0.0f) || InTileSize == 0u || InPageSize < InTileSize || InTableSize == 0u)
    {
        return false;
    }
    const std::int32_t ScopedTileX = VirtualTextureWorldToTile(InWorld.X, InWorldTileSize);
    const std::int32_t ScopedTileY = VirtualTextureWorldToTile(InWorld.Y, InWorldTileSize);
    const std::int64_t ScopedCellX = static_cast<std::int64_t>(ScopedTileX) - static_cast<std::int64_t>(InTableOrigin.X);
    const std::int64_t ScopedCellY = static_cast<std::int64_t>(ScopedTileY) - static_cast<std::int64_t>(InTableOrigin.Y);
    if (ScopedCellX < 0 || ScopedCellY < 0 || ScopedCellX >= static_cast<std::int64_t>(InTableSize) ||
        ScopedCellY >= static_cast<std::int64_t>(InTableSize))
    {
        return false;
    }
    const std::size_t ScopedIndex = static_cast<std::size_t>(ScopedCellY) * InTableSize +
                                    static_cast<std::size_t>(ScopedCellX);
    if (ScopedIndex >= InTable.size())
    {
        return false;
    }
    std::uint32_t ScopedLayer = 0;
    std::uint32_t ScopedSlot = 0;
    if (!UnpackVirtualTexturePageTable(InTable[ScopedIndex], &ScopedLayer, &ScopedSlot))
    {
        return false;
    }
    const std::uint32_t ScopedTilesPerAxis = VirtualTextureTilesPerAxis(InPageSize, InTileSize);
    std::uint32_t ScopedSlotX = 0;
    std::uint32_t ScopedSlotY = 0;
    if (!VirtualTextureCellFromSlot(ScopedTilesPerAxis, ScopedSlot, &ScopedSlotX, &ScopedSlotY))
    {
        return false;
    }
    if (OutSample != nullptr)
    {
        OutSample->Layer = ScopedLayer;
        OutSample->SlotX = ScopedSlotX;
        OutSample->SlotY = ScopedSlotY;
        OutSample->LocalU = VirtualTextureTileLocalUv(InWorld.X, InWorldTileSize);
        OutSample->LocalV = VirtualTextureTileLocalUv(InWorld.Y, InWorldTileSize);
    }
    return true;
}

// The `VirtualTextureUploadFn` adapter: `VirtualTextureSystem` calls this for
// every streaming upload and it forwards the page to the backend.
struct VirtualTextureRhiTarget
{
    RHI* Renderer = nullptr;
    VirtualTextureHandle Handle{};
    std::uint32_t TileSize = 0;
    std::uint32_t TilesPerAxis = 0;
};

[[nodiscard]] inline bool UploadVirtualTextureTileToRhi(void* InUser,
                                                        const VirtualTextureTileUpload& InUpload) noexcept
{
    auto* ScopedTarget = static_cast<VirtualTextureRhiTarget*>(InUser);
    if (ScopedTarget == nullptr || ScopedTarget->Renderer == nullptr || !ScopedTarget->Handle.IsValid())
    {
        return false;
    }
    VirtualTexturePageLocation ScopedLocation;
    if (!MapVirtualTextureTile(InUpload, ScopedTarget->TileSize, ScopedTarget->TilesPerAxis, &ScopedLocation))
    {
        return false;
    }
    return ScopedTarget->Renderer->UploadVirtualTexturePage(ScopedTarget->Handle, InUpload.layer, ScopedLocation.SlotX,
                                                            ScopedLocation.SlotY, InUpload.width, InUpload.height,
                                                            InUpload.rowPitch, InUpload.pixels);
}
}
