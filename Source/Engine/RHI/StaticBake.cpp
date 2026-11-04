// FemboyEngine2D - static layer bake tile cache implementation.
#include "Engine/RHI/StaticBake.hpp"

#include <algorithm>
#include <cmath>

namespace Fby::Rhi
{

std::uint8_t ZoomBucketFor(float InZoom) noexcept
{
    const float ScopedZoom = std::max(InZoom, 0.0001f);
    const int ScopedBucket = static_cast<int>(std::floor(std::log2(ScopedZoom))) + 4;
    return static_cast<std::uint8_t>(std::clamp(ScopedBucket, 0, 15));
}

void StaticBakeCache::Configure(const StaticBakeSettings& InSettings) noexcept
{
    MSettings = InSettings;
    MSettings.tileSize = std::max(MSettings.tileSize, 32u);
    MSettings.budgetBytes = std::max(MSettings.budgetBytes, 64u << 10);
    MSettings.maxBakesPerFrame = std::max(MSettings.maxBakesPerFrame, 1u);
    InvalidateAll();
}

void StaticBakeCache::BeginFrame() noexcept
{
    ++MFrame;
    MBakesThisFrame = 0;
    MStats.bakesThisFrame = 0;
    MStats.tiles = static_cast<std::uint32_t>(MTiles.size());
    MStats.readyTiles = 0;
    MStats.memoryBytes = MMemoryBytes;
    for (const auto& [ScopedKey, ScopedTile] : MTiles)
    {
        (void)ScopedKey;
        if (ScopedTile.ready)
        {
            ++MStats.readyTiles;
        }
    }
}

void StaticBakeCache::SetZoom(float InZoom) noexcept
{
    const std::uint8_t ScopedBucket = ZoomBucketFor(InZoom);
    if (ScopedBucket != MZoomBucket)
    {
        MZoomBucket = ScopedBucket;
    }
}

void StaticBakeCache::SetLightVersion(std::uint32_t InVersion) noexcept
{
    if (InVersion == MLightVersion)
    {
        return;
    }
    MLightVersion = InVersion;
    // Static albedo is reusable across light changes; tiles that baked lighting
    // are invalidated by the caller through InvalidateLayer/InvalidateAll.
}

void StaticBakeCache::InvalidateLayer(std::uint16_t InLayer)
{
    for (auto ScopedIt = MTiles.begin(); ScopedIt != MTiles.end();)
    {
        if (ScopedIt->first.layer == InLayer)
        {
            MMemoryBytes -= ScopedIt->second.bytes;
            ScopedIt = MTiles.erase(ScopedIt);
        }
        else
        {
            ++ScopedIt;
        }
    }
}

void StaticBakeCache::InvalidateAll() noexcept
{
    MTiles.clear();
    MMemoryBytes = 0;
    MBakesThisFrame = 0;
    MStats = {};
}

bool StaticBakeCache::Acquire(std::uint16_t InLayer, std::int32_t InTileX, std::int32_t InTileY,
                              StaticBakeTileKey* OutKey)
{
    StaticBakeTileKey ScopedKey{InLayer, InTileX, InTileY, MZoomBucket, MLightVersion};
    if (OutKey != nullptr)
    {
        *OutKey = ScopedKey;
    }

    const auto ScopedFound = MTiles.find(ScopedKey);
    if (ScopedFound != MTiles.end())
    {
        ScopedFound->second.lastUse = MFrame;
        return ScopedFound->second.ready;
    }

    if (MBakesThisFrame >= MSettings.maxBakesPerFrame)
    {
        return false;
    }

    const std::uint64_t ScopedEstimate =
        static_cast<std::uint64_t>(MSettings.tileSize) * MSettings.tileSize * 4u;
    EvictUntilBudget(ScopedEstimate, &ScopedKey);
    if (MMemoryBytes + ScopedEstimate > MSettings.budgetBytes)
    {
        return false;
    }

    Tile ScopedTile;
    ScopedTile.ready = false;
    ScopedTile.lastUse = MFrame;
    ScopedTile.bytes = static_cast<std::uint32_t>(ScopedEstimate);
    MTiles.emplace(ScopedKey, ScopedTile);
    MMemoryBytes += ScopedEstimate;
    ++MBakesThisFrame;
    MStats.bakesThisFrame = MBakesThisFrame;
    return false;
}

void StaticBakeCache::Commit(const StaticBakeTileKey& InKey, std::uint32_t InByteSize)
{
    const auto ScopedFound = MTiles.find(InKey);
    if (ScopedFound == MTiles.end())
    {
        return;
    }
    const std::uint64_t ScopedOldBytes = ScopedFound->second.bytes;
    ScopedFound->second.bytes = InByteSize;
    ScopedFound->second.ready = true;
    ScopedFound->second.lastUse = MFrame;
    MMemoryBytes = MMemoryBytes - ScopedOldBytes + InByteSize;
    EvictUntilBudget(0, &InKey);
}

bool StaticBakeCache::HasBudget() const noexcept
{
    return MBakesThisFrame < MSettings.maxBakesPerFrame;
}

void StaticBakeCache::EvictUntilBudget(std::uint64_t InExtraBytes, const StaticBakeTileKey* InProtected) noexcept
{
    while (MMemoryBytes + InExtraBytes > MSettings.budgetBytes && !MTiles.empty())
    {
        auto ScopedVictim = MTiles.end();
        for (auto ScopedIt = MTiles.begin(); ScopedIt != MTiles.end(); ++ScopedIt)
        {
            if (!ScopedIt->second.ready)
            {
                continue;
            }
            if (InProtected != nullptr && ScopedIt->first.layer == InProtected->layer &&
                ScopedIt->first.tileX == InProtected->tileX && ScopedIt->first.tileY == InProtected->tileY &&
                ScopedIt->first.zoomBucket == InProtected->zoomBucket &&
                ScopedIt->first.lightVersion == InProtected->lightVersion)
            {
                continue;
            }
            if (ScopedVictim == MTiles.end() || ScopedIt->second.lastUse < ScopedVictim->second.lastUse)
            {
                ScopedVictim = ScopedIt;
            }
        }
        if (ScopedVictim == MTiles.end())
        {
            break;
        }
        MMemoryBytes -= ScopedVictim->second.bytes;
        MTiles.erase(ScopedVictim);
        ++MStats.evictions;
    }
}

}
