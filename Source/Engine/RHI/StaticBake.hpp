// FemboyEngine2D - static layer bake tile cache.
#pragma once

#include <cstdint>
#include <map>

namespace Fby::Rhi
{

struct StaticBakeSettings
{
    std::uint32_t tileSize = 256;
    std::uint32_t budgetBytes = 64u << 20;
    std::uint32_t maxBakesPerFrame = 4;
};

struct StaticBakeTileKey
{
    std::uint16_t layer = 0;
    std::int32_t tileX = 0;
    std::int32_t tileY = 0;
    std::uint8_t zoomBucket = 0;
    std::uint32_t lightVersion = 0;

    friend bool operator<(const StaticBakeTileKey& InLeft, const StaticBakeTileKey& InRight) noexcept
    {
        if (InLeft.layer != InRight.layer)
        {
            return InLeft.layer < InRight.layer;
        }
        if (InLeft.zoomBucket != InRight.zoomBucket)
        {
            return InLeft.zoomBucket < InRight.zoomBucket;
        }
        if (InLeft.lightVersion != InRight.lightVersion)
        {
            return InLeft.lightVersion < InRight.lightVersion;
        }
        if (InLeft.tileY != InRight.tileY)
        {
            return InLeft.tileY < InRight.tileY;
        }
        return InLeft.tileX < InRight.tileX;
    }
};

struct StaticBakeStats
{
    std::uint32_t tiles = 0;
    std::uint32_t readyTiles = 0;
    std::uint32_t bakesThisFrame = 0;
    std::uint64_t memoryBytes = 0;
    std::uint64_t evictions = 0;
};

// Tracks which static-layer tiles are ready, in-flight or dirty. It does not
// own GPU memory: the renderer commits the actual byte size after baking a tile.
class StaticBakeCache
{
public:
    void Configure(const StaticBakeSettings& InSettings) noexcept;
    [[nodiscard]] const StaticBakeSettings& Settings() const noexcept
    {
        return MSettings;
    }

    void BeginFrame() noexcept;
    void SetZoom(float InZoom) noexcept;
    void SetLightVersion(std::uint32_t InVersion) noexcept;
    void InvalidateLayer(std::uint16_t InLayer);
    void InvalidateAll() noexcept;

    // Returns true when the tile is ready. Returns false when the caller must
    // bake it now (a slot was reserved) or when the per-frame budget is spent.
    [[nodiscard]] bool Acquire(std::uint16_t InLayer, std::int32_t InTileX, std::int32_t InTileY,
                               StaticBakeTileKey* OutKey = nullptr);
    void Commit(const StaticBakeTileKey& InKey, std::uint32_t InByteSize);

    [[nodiscard]] std::uint8_t ZoomBucket() const noexcept
    {
        return MZoomBucket;
    }
    [[nodiscard]] bool HasBudget() const noexcept;
    [[nodiscard]] const StaticBakeStats& Stats() const noexcept
    {
        return MStats;
    }

private:
    struct Tile
    {
        bool ready = false;
        std::uint64_t lastUse = 0;
        std::uint32_t bytes = 0;
    };

    void EvictUntilBudget(std::uint64_t InExtraBytes, const StaticBakeTileKey* InProtected) noexcept;

    StaticBakeSettings MSettings{};
    std::map<StaticBakeTileKey, Tile> MTiles;
    std::uint8_t MZoomBucket = 0;
    std::uint32_t MLightVersion = 0;
    std::uint32_t MBakesThisFrame = 0;
    std::uint64_t MFrame = 0;
    std::uint64_t MMemoryBytes = 0;
    StaticBakeStats MStats{};
};

[[nodiscard]] std::uint8_t ZoomBucketFor(float InZoom) noexcept;

}
