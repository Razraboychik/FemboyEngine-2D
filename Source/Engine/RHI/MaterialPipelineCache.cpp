// FemboyEngine2D - engine-side material pipeline cache.
#include "Engine/RHI/MaterialPipelineCache.hpp"

#include "Engine/Core/Log.hpp"

#include <cstdio>
#include <cstring>

namespace Fby::Render
{
namespace
{

constexpr std::uint32_t kInvalidSlot = 0xFFFFFFFFu;
}

std::uint32_t MaterialPipelineCache::FindByHash(std::uint64_t InGraphHash, std::uint16_t InContract) const noexcept
{
    // One graph can live in more than one domain at once: the mesh and the sprite
    // contract are different shaders, so both entries may be resident.
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < kMaxCachedMaterialPipelines; ++ScopedIndex)
    {
        const Entry& ScopedEntry = MEntries[ScopedIndex];
        if (ScopedEntry.occupied && ScopedEntry.graphHash == InGraphHash && ScopedEntry.contract == InContract)
        {
            return ScopedIndex;
        }
    }
    return kInvalidSlot;
}

std::uint32_t MaterialPipelineCache::FindByPipeline(Rhi::ShaderPipelineHandle InPipeline) const noexcept
{
    if (!InPipeline.IsValid())
    {
        return kInvalidSlot;
    }
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < kMaxCachedMaterialPipelines; ++ScopedIndex)
    {
        if (MEntries[ScopedIndex].occupied && MEntries[ScopedIndex].pipeline.id == InPipeline.id)
        {
            return ScopedIndex;
        }
    }
    return kInvalidSlot;
}

std::uint32_t MaterialPipelineCache::FindFreeSlot() const noexcept
{
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < kMaxCachedMaterialPipelines; ++ScopedIndex)
    {
        if (!MEntries[ScopedIndex].occupied)
        {
            return ScopedIndex;
        }
    }
    return kInvalidSlot;
}

std::uint32_t MaterialPipelineCache::FindEvictableSlot() const noexcept
{
    std::uint32_t ScopedBest = kInvalidSlot;
    std::uint32_t ScopedBestTick = 0;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < kMaxCachedMaterialPipelines; ++ScopedIndex)
    {
        const Entry& ScopedEntry = MEntries[ScopedIndex];
        if (!ScopedEntry.occupied || ScopedEntry.referenceCount != 0)
        {
            continue;
        }
        if (ScopedBest == kInvalidSlot || ScopedEntry.lastUsed < ScopedBestTick)
        {
            ScopedBest = ScopedIndex;
            ScopedBestTick = ScopedEntry.lastUsed;
        }
    }
    return ScopedBest;
}

void MaterialPipelineCache::SetError(const char* InText) noexcept
{
    if (InText == nullptr)
    {
        MError[0] = '\0';
        return;
    }
    std::snprintf(MError, sizeof(MError), "%s", InText);
}

void MaterialPipelineCache::ClearError() noexcept
{
    MError[0] = '\0';
}

Rhi::ShaderPipelineHandle MaterialPipelineCache::Acquire(Rhi::RHI& InRhi,
                                                         const CookedMaterialView& InMaterial) noexcept
{
    if (!InMaterial.IsValid())
    {
        ++MStats.failures;
        SetError("material payload is not a cooked material");
        return {};
    }
    if (!InRhi.SupportsRuntimeShaderPipelines())
    {
        ++MStats.failures;
        SetError("this backend has no runtime shader pipelines");
        return {};
    }

    const std::uint32_t ScopedExisting = FindByHash(InMaterial.graphHash, InMaterial.contract);
    if (ScopedExisting != kInvalidSlot)
    {
        Entry& ScopedEntry = MEntries[ScopedExisting];
        ++ScopedEntry.referenceCount;
        ScopedEntry.lastUsed = ++MTick;
        ++MStats.reused;
        SetError(nullptr);
        return ScopedEntry.pipeline;
    }

    std::uint32_t ScopedSlot = FindFreeSlot();
    if (ScopedSlot == kInvalidSlot)
    {
        ScopedSlot = FindEvictableSlot();
        if (ScopedSlot == kInvalidSlot)
        {
            ++MStats.failures;
            LogError("material", "all {} material pipelines are in use", kMaxCachedMaterialPipelines);
            SetError("all material pipelines are in use");
            return {};
        }
        LogWarn("material", "evicting material pipeline for graph {:#x}", MEntries[ScopedSlot].graphHash);
        InRhi.DestroyShaderPipeline(MEntries[ScopedSlot].pipeline);
        ++MStats.destroyed;
        MEntries[ScopedSlot] = Entry{};
        ++MStats.evicted;
    }

    // The debug name only has to outlive the creation call; the backend copies
    // what it keeps and the command buffer records the pipeline, not the label.
    char ScopedDebugName[32] = {};
    std::snprintf(ScopedDebugName, sizeof(ScopedDebugName), "material %016llx",
                  static_cast<unsigned long long>(InMaterial.graphHash));

    Rhi::ShaderPipelineDesc ScopedDesc;
    ScopedDesc.vertexSpirv = InMaterial.vertexSpirv;
    ScopedDesc.fragmentSpirv = InMaterial.fragmentSpirv;
    ScopedDesc.debugName = ScopedDebugName;
    ScopedDesc.domain = MaterialContractIsSprite(InMaterial.contract) ? Rhi::ShaderPipelineDomain::Sprite
                                                                      : Rhi::ShaderPipelineDomain::Mesh;

    const Rhi::ShaderPipelineHandle ScopedPipeline = InRhi.CreateShaderPipeline(ScopedDesc);
    if (!ScopedPipeline.IsValid())
    {
        ++MStats.failures;
        SetError("backend refused the cooked material");
        return {};
    }

    Entry& ScopedEntry = MEntries[ScopedSlot];
    ScopedEntry.graphHash = InMaterial.graphHash;
    ScopedEntry.contract = InMaterial.contract;
    ScopedEntry.referenceCount = 1;
    ScopedEntry.lastUsed = ++MTick;
    ScopedEntry.pipeline = ScopedPipeline;
    ScopedEntry.occupied = true;
    ++MStats.created;
    SetError(nullptr);
    return ScopedPipeline;
}

bool MaterialPipelineCache::Release(Rhi::ShaderPipelineHandle InPipeline) noexcept
{
    const std::uint32_t ScopedSlot = FindByPipeline(InPipeline);
    if (ScopedSlot == kInvalidSlot || MEntries[ScopedSlot].referenceCount == 0)
    {
        ++MStats.failures;
        SetError("material pipeline holds no reference");
        return false;
    }

    // The entry stays resident at zero references: the next frame usually
    // acquires the same material again, and recreating a pipeline per frame
    // would cost more than the slot it occupies.
    Entry& ScopedEntry = MEntries[ScopedSlot];
    --ScopedEntry.referenceCount;
    ScopedEntry.lastUsed = ++MTick;
    ++MStats.released;
    SetError(nullptr);
    return true;
}

void MaterialPipelineCache::ReleaseAll(Rhi::RHI& InRhi) noexcept
{
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < kMaxCachedMaterialPipelines; ++ScopedIndex)
    {
        Entry& ScopedEntry = MEntries[ScopedIndex];
        if (!ScopedEntry.occupied)
        {
            continue;
        }
        InRhi.DestroyShaderPipeline(ScopedEntry.pipeline);
        ++MStats.destroyed;
        MStats.released += ScopedEntry.referenceCount > 0 ? 1u : 0u;
        ScopedEntry = Entry{};
    }
}

void MaterialPipelineCache::Shutdown(Rhi::RHI& InRhi) noexcept
{
    ReleaseAll(InRhi);
    MStats = MaterialPipelineCacheStats{};
    MTick = 0;
    ClearError();
}

std::uint32_t MaterialPipelineCache::LiveCount() const noexcept
{
    std::uint32_t ScopedCount = 0;
    for (const Entry& ScopedEntry : MEntries)
    {
        if (ScopedEntry.occupied)
        {
            ++ScopedCount;
        }
    }
    return ScopedCount;
}

std::uint32_t MaterialPipelineCache::ReferenceCount(Rhi::ShaderPipelineHandle InPipeline) const noexcept
{
    const std::uint32_t ScopedSlot = FindByPipeline(InPipeline);
    return ScopedSlot == kInvalidSlot ? 0 : MEntries[ScopedSlot].referenceCount;
}
}
