// FemboyEngine2D - engine-side material pipeline cache.
#pragma once

#include "Engine/RHI/MaterialAsset.hpp"
#include "Engine/RHI/RHI.hpp"

#include <array>
#include <cstdint>

namespace Fby::Render
{

// Upper bound on live material pipelines. The backend keeps its own slot pool,
// so a cache that stays below this bound never sees a backend rejection.
inline constexpr std::uint32_t kMaxCachedMaterialPipelines = 128;
inline constexpr std::uint32_t kMaterialCacheErrorBytes = 160;

struct MaterialPipelineCacheStats
{
    // Pipelines the backend created
    std::uint32_t created = 0;
    // Acquires that reused a live pipeline
    std::uint32_t reused = 0;
    // Releases that dropped one reference
    std::uint32_t released = 0;
    // Entries destroyed to make room for a new one
    std::uint32_t evicted = 0;
    // Pipelines the backend destroyed on eviction or release
    std::uint32_t destroyed = 0;
    // Acquires the cache or the backend refused
    std::uint32_t failures = 0;
};

// Shares cooked material pipelines between every render item that uses the same
// graph and shader contract. The cache owns the reference count, so a sprite and
// a mesh can acquire the same graph in both domains without either of them
// knowing about the other.
class MaterialPipelineCache
{
public:
    MaterialPipelineCache() = default;
    ~MaterialPipelineCache() = default;
    MaterialPipelineCache(const MaterialPipelineCache&) = delete;
    MaterialPipelineCache& operator=(const MaterialPipelineCache&) = delete;

    // Returns the pipeline for the cooked material and adds one reference. The
    // pipeline is created on the first acquire; the key is the graph hash.
    [[nodiscard]] Rhi::ShaderPipelineHandle Acquire(Rhi::RHI& InRhi, const CookedMaterialView& InMaterial) noexcept;

    // Drops one reference. A pipeline whose count reaches zero stays resident so
    // the next frame reuses it, and is destroyed only by eviction, ReleaseAll()
    // or Shutdown(). Returns false for a handle that holds no reference.
    [[nodiscard]] bool Release(Rhi::ShaderPipelineHandle InPipeline) noexcept;

    // Drops every reference the cache still holds and destroys the pipelines.
    void ReleaseAll(Rhi::RHI& InRhi) noexcept;

    // ReleaseAll() plus a statistics reset. Call while the backend is alive.
    void Shutdown(Rhi::RHI& InRhi) noexcept;

    [[nodiscard]] std::uint32_t LiveCount() const noexcept;
    [[nodiscard]] std::uint32_t ReferenceCount(Rhi::ShaderPipelineHandle InPipeline) const noexcept;
    [[nodiscard]] const MaterialPipelineCacheStats& Stats() const noexcept
    {
        return MStats;
    }
    [[nodiscard]] const char* LastError() const noexcept
    {
        return MError;
    }
    void ClearError() noexcept;

private:
    struct Entry
    {
        std::uint64_t graphHash = 0;
        // MaterialContract of the cached pipeline; a hash with a different
        // contract means a different shader and is refused, not reused.
        std::uint16_t contract = 0;
        std::uint32_t referenceCount = 0;
        std::uint32_t lastUsed = 0;
        Rhi::ShaderPipelineHandle pipeline{};
        bool occupied = false;
    };

    [[nodiscard]] std::uint32_t FindByHash(std::uint64_t InGraphHash, std::uint16_t InContract) const noexcept;
    [[nodiscard]] std::uint32_t FindByPipeline(Rhi::ShaderPipelineHandle InPipeline) const noexcept;
    [[nodiscard]] std::uint32_t FindFreeSlot() const noexcept;
    // Oldest unreferenced entry, or kInvalidSlot when every entry is in use.
    [[nodiscard]] std::uint32_t FindEvictableSlot() const noexcept;
    void SetError(const char* InText) noexcept;

    std::array<Entry, kMaxCachedMaterialPipelines> MEntries{};
    MaterialPipelineCacheStats MStats{};
    char MError[kMaterialCacheErrorBytes]{};
    std::uint32_t MTick = 0;
};
}
