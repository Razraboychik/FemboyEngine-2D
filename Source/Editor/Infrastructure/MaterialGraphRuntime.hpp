// FemboyEngine2D - live MaterialGraph -> GPU pipeline driver (editor library).
#pragma once

#include "Engine/RHI/MaterialAsset.hpp"
#include "Engine/RHI/MaterialGraph.hpp"
#include "Engine/RHI/MaterialGraphCompiler.hpp"
#include "Engine/RHI/MaterialPipelineCache.hpp"
#include "Engine/RHI/RHI.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace Fby::Editor
{

// Editor materials that may be live at once (one per open material tab plus a
// handful of viewport previews). Pipelines do not get their own capacity: the
// engine-side cache owns them, so an editor material and a shipped material
// share one reference-counted pool.
inline constexpr std::uint32_t kMaxMaterialSlots = 64;
inline constexpr std::uint32_t kMaxMaterialPipelines = Render::kMaxCachedMaterialPipelines;
inline constexpr std::uint32_t kMaterialErrorBytes = 512;

struct MaterialRuntimeStats
{
    // Graph -> GLSL attempts
    std::uint32_t compiles = 0;
    // Update() with an unchanged hash
    std::uint32_t cacheHits = 0;
    // Validation / glslang / cache errors
    std::uint32_t failures = 0;
    // Pipelines the engine cache created for this run
    std::uint32_t pipelinesCreated = 0;
    // Pipelines the engine cache destroyed
    std::uint32_t pipelinesDestroyed = 0;
    // Acquires that reused a resident pipeline
    std::uint32_t pipelinesShared = 0;
};

class MaterialGraphRuntime
{
public:
    static constexpr std::uint32_t kInvalidSlot = 0xFFFFFFFFu;
    static constexpr std::uint32_t kInvalidEntry = 0xFFFFFFFFu;
    // A material id of kDefaultMaterialPipeline is reserved by MeshRenderItem.
    static constexpr std::uint32_t kReservedMaterialId = Rhi::kDefaultMaterialPipeline;

    MaterialGraphRuntime();
    ~MaterialGraphRuntime() = default;
    MaterialGraphRuntime(const MaterialGraphRuntime&) = delete;
    MaterialGraphRuntime& operator=(const MaterialGraphRuntime&) = delete;

    // Binds `materialId` to a graph owned by the editor. The graph may be edited
    // in place; Update() recompiles it when its hash changes.
    [[nodiscard]] bool RegisterMaterial(std::uint32_t InMaterialId, Render::MaterialGraph* InGraph);
    // Drops the binding and its pipeline reference. The pipeline stays resident
    // in the engine cache so re-registering the same graph reuses it.
    void UnregisterMaterial(std::uint32_t InMaterialId);
    // Recompiles `graph` when its hash changed since the last attempt. Returns
    // false when validation or the backend refused the material.
    [[nodiscard]] bool Update(Rhi::RHI& InRhi, Render::MaterialGraph& InGraph);
    // Calls Update() for every slot that still has a graph pointer.
    void UpdateAll(Rhi::RHI& InRhi);
    // Marks the slot dirty without touching the graph (e.g. after a hot reload
    // of a texture the graph samples).
    void Invalidate(std::uint32_t InMaterialId) noexcept;

    // Mesh or sprite shader contract. Switching recompiles every registered
    // material; the previous variant stays resident in the engine cache, so the
    // mesh and the sprite edition of one graph can be live at the same time.
    void SetDomain(Render::MaterialDomain InDomain) noexcept
    {
        if (MDomain == InDomain)
        {
            return;
        }
        MDomain = InDomain;
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < MSlotCount; ++ScopedIndex)
        {
            MSlots[ScopedIndex].dirty = true;
        }
    }
    [[nodiscard]] Render::MaterialDomain Domain() const noexcept
    {
        return MDomain;
    }

    // Material graphs that use Time - or that shade world position/Fresnel in
    // the fragment stage - need the material push-constant block.
    void SetMaterialPushConstantsSupported(bool InSupported) noexcept;
    [[nodiscard]] bool MaterialPushConstantsSupported() const noexcept
    {
        return MMaterialPushConstantsSupported;
    }

    // Result accessors. Both are safe for unregistered ids.
    [[nodiscard]] Rhi::ShaderPipelineHandle Pipeline(std::uint32_t InMaterialId) const noexcept;
    // Value to write into MeshRenderItem::materialPipeline: the pipeline handle
    // id, or kReservedMaterialId when the material has no pipeline yet.
    [[nodiscard]] std::uint32_t MaterialPipelineId(std::uint32_t InMaterialId) const noexcept;
    [[nodiscard]] const char* LastError(std::uint32_t InMaterialId) const noexcept;
    [[nodiscard]] bool IsRegistered(std::uint32_t InMaterialId) const noexcept;
    [[nodiscard]] std::uint32_t SlotCount() const noexcept
    {
        return MSlotCount;
    }
    [[nodiscard]] const MaterialRuntimeStats& Stats() const noexcept
    {
        return MStats;
    }

    // Destroys every cached GPU pipeline. Must be called while `rhi` is still
    // alive; the editor calls it from Shutdown().
    void Shutdown(Rhi::RHI& InRhi);

private:
    struct Slot
    {
        std::uint32_t materialId = kReservedMaterialId;
        Render::MaterialGraph* graph = nullptr;
        std::uint64_t lastAttemptHash = 0;
        std::uint64_t lastGoodHash = 0;
        Rhi::ShaderPipelineHandle pipeline{};
        // True while this slot holds one reference in the engine cache.
        bool holdsPipeline = false;
        bool dirty = true;
        bool failed = false;
        char debugName[48] = {};
        char error[kMaterialErrorBytes] = {};
    };

    [[nodiscard]] std::uint32_t FindSlot(std::uint32_t InMaterialId) const noexcept;
    [[nodiscard]] std::uint32_t FindSlotForGraph(const Render::MaterialGraph* InGraph) const noexcept;
    void ReleaseSlotPipeline(Slot& InSlot) noexcept;
    void SetSlotError(Slot& InSlot, std::string_view InText) noexcept;

    std::array<Slot, kMaxMaterialSlots> MSlots{};
    Render::MaterialPipelineCache MPipelineCache;
    MaterialRuntimeStats MStats{};
    std::uint32_t MSlotCount = 0;
    // RHI exposes the 80-byte mesh material block (view-projection + params)
    bool MMaterialPushConstantsSupported = true;
    // Mesh or sprite fragment contract; sprite uses the quad camera push block.
    Render::MaterialDomain MDomain = Render::MaterialDomain::Mesh;

    // Reused compile scratch: reserved once, cleared (never freed) per compile.
    std::vector<std::uint32_t> MFragmentSpirv;
    std::vector<std::uint32_t> MVertexSpirv;
    char MFragmentGlsl[Render::kMaterialGlslFragmentBytes] = {};
    char MVertexGlsl[Render::kMaterialGlslVertexBytes] = {};
};
}
