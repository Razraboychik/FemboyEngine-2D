// FemboyEngine2D - MaterialGraphRuntime implementation.
#include "Editor/Infrastructure/MaterialGraphRuntime.hpp"

#include "Engine/Core/Log.hpp"
#include "Tools/ShaderCompiler/ShaderCompiler.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <span>
#include <string_view>
#include <utility>

namespace Fby::Editor
{

namespace
{

void CopyError(char* InDestination, std::size_t InCapacity, std::string_view InText) noexcept
{
    if (InDestination == nullptr || InCapacity == 0)
    {
        return;
    }
    const std::size_t ScopedLength = std::min(InText.size(), InCapacity - 1);
    if (ScopedLength > 0)
    {
        std::memcpy(InDestination, InText.data(), ScopedLength);
    }
    InDestination[ScopedLength] = '\0';
}

[[nodiscard]] std::span<const std::byte> SpirvBytes(const std::vector<std::uint32_t>& InWords) noexcept
{
    return {reinterpret_cast<const std::byte*>(InWords.data()), InWords.size() * sizeof(std::uint32_t)};
}
}

MaterialGraphRuntime::MaterialGraphRuntime()
{
    // Reserved once so a live recompile reuses the word storage instead of
    // growing it in the frame in which the artist drags a node.
    MFragmentSpirv.reserve(1u << 14);
    MVertexSpirv.reserve(1u << 12);
}

// Registration
bool MaterialGraphRuntime::RegisterMaterial(std::uint32_t InMaterialId, Render::MaterialGraph* InGraph)
{
    if (InMaterialId == kReservedMaterialId)
    {
        return false;
    }
    if (InGraph == nullptr || FindSlot(InMaterialId) != kInvalidSlot || MSlotCount >= kMaxMaterialSlots)
    {
        return false;
    }
    Slot& ScopedSlot = MSlots[MSlotCount];
    ScopedSlot = Slot{};
    ScopedSlot.materialId = InMaterialId;
    ScopedSlot.graph = InGraph;
    ScopedSlot.dirty = true;
    std::snprintf(ScopedSlot.debugName, sizeof(ScopedSlot.debugName), "material-%u", InMaterialId);
    ++MSlotCount;
    return true;
}

void MaterialGraphRuntime::UnregisterMaterial(std::uint32_t InMaterialId)
{
    const std::uint32_t ScopedIndex = FindSlot(InMaterialId);
    if (ScopedIndex == kInvalidSlot)
    {
        return;
    }
    // The pipeline stays in the engine cache with a zero reference count, so
    // registering the same graph again reuses it instead of recompiling.
    ReleaseSlotPipeline(MSlots[ScopedIndex]);
    MSlots[ScopedIndex] = MSlots[MSlotCount - 1];
    --MSlotCount;
}

void MaterialGraphRuntime::Invalidate(std::uint32_t InMaterialId) noexcept
{
    const std::uint32_t ScopedIndex = FindSlot(InMaterialId);
    if (ScopedIndex != kInvalidSlot)
    {
        MSlots[ScopedIndex].dirty = true;
    }
}

void MaterialGraphRuntime::SetMaterialPushConstantsSupported(bool InSupported) noexcept
{
    MMaterialPushConstantsSupported = InSupported;
    // The generated shader differs depending on the contract, so every material
    // recompiles. The resident pipelines become stale; the cache drops each of
    // them on the next acquire because those entries hold no reference then.
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < MSlotCount; ++ScopedIndex)
    {
        MSlots[ScopedIndex].dirty = true;
    }
}

void MaterialGraphRuntime::ReleaseSlotPipeline(Slot& InSlot) noexcept
{
    if (!InSlot.holdsPipeline)
    {
        InSlot.pipeline = Rhi::ShaderPipelineHandle{};
        return;
    }
    MPipelineCache.Release(InSlot.pipeline);
    InSlot.pipeline = Rhi::ShaderPipelineHandle{};
    InSlot.holdsPipeline = false;
}

// Update: the live recompile driver
bool MaterialGraphRuntime::Update(Rhi::RHI& InRhi, Render::MaterialGraph& InGraph)
{
    const std::uint32_t ScopedSlotIndex = FindSlotForGraph(&InGraph);
    if (ScopedSlotIndex == kInvalidSlot)
    {
        return false;
    }
    Slot& ScopedSlot = MSlots[ScopedSlotIndex];

    const std::uint64_t ScopedHash = InGraph.Hash();
    if (!ScopedSlot.dirty && ScopedSlot.lastAttemptHash == ScopedHash)
    {
        ++MStats.cacheHits;
        return !ScopedSlot.failed;
    }
    if (!ScopedSlot.dirty && ScopedSlot.lastGoodHash == ScopedHash)
    {
        ++MStats.cacheHits;
        return true;
    }
    ScopedSlot.dirty = false;
    ScopedSlot.lastAttemptHash = ScopedHash;
    ++MStats.compiles;

    // A VertexPosition graph needs the generated vertex stage; every other
    // graph stays on the engine vertex shader.
    const Render::MaterialGraphRequirements ScopedRequirements = InGraph.Requirements();
    Render::MaterialCompileOptions ScopedOptions;
    ScopedOptions.domain = MDomain;
    ScopedOptions.generateVertexStage =
        MDomain == Render::MaterialDomain::Mesh &&
        (ScopedRequirements.needsVertexStage ||
         (!MMaterialPushConstantsSupported && InGraph.UsesNodeKind(Render::MaterialNodeKind::WorldPosition)));
    ScopedOptions.debugName = ScopedSlot.debugName;

    const Render::MaterialGraphCompiler ScopedCompiler;
    Render::MaterialShaderProgram ScopedProgram;
    std::string ScopedError;
    const std::span<char> ScopedFragmentBuffer(MFragmentGlsl, sizeof(MFragmentGlsl));
    const std::span<char> ScopedVertexBuffer(MVertexGlsl, sizeof(MVertexGlsl));
    if (!ScopedCompiler.Compile(InGraph, ScopedOptions,
                                ScopedOptions.generateVertexStage ? ScopedVertexBuffer : std::span<char>{},
                                ScopedFragmentBuffer, &ScopedProgram, &ScopedError))
    {
        // Keep the last good pipeline: an edit in progress must not blink out.
        ++MStats.failures;
        ScopedSlot.failed = true;
        SetSlotError(ScopedSlot, ScopedError);
        LogWarn("material", "material {} did not compile: {}", ScopedSlot.materialId, ScopedSlot.error);
        return false;
    }

    if (MDomain == Render::MaterialDomain::Mesh && !MMaterialPushConstantsSupported &&
        (ScopedProgram.fragmentReadsViewProjection || ScopedProgram.fragmentReadsMaterialParams))
    {
        // Time, and Fresnel shaded in the fragment stage, need the 80-byte
        // block this backend does not expose.
        ++MStats.failures;
        ScopedSlot.failed = true;
        SetSlotError(ScopedSlot,
                     "material graph needs the material push-constant extension (Time, or Fresnel in the fragment "
                     "stage); this RHI backend does not expose it yet");
        LogWarn("material", "material {}: {}", ScopedSlot.materialId, ScopedSlot.error);
        return false;
    }

    MFragmentSpirv.clear();
    if (!Fby::Shaderc::CompileGlslToSpirv(std::string_view(MFragmentGlsl, ScopedProgram.fragmentLength),
                                          Fby::Shaderc::ShaderStage::Fragment, &MFragmentSpirv, &ScopedError))
    {
        ++MStats.failures;
        ScopedSlot.failed = true;
        SetSlotError(ScopedSlot, ScopedError);
        LogWarn("material", "material {} fragment glslang failed: {}", ScopedSlot.materialId, ScopedSlot.error);
        return false;
    }
    MVertexSpirv.clear();
    if (ScopedProgram.usesGeneratedVertexStage &&
        !Fby::Shaderc::CompileGlslToSpirv(std::string_view(MVertexGlsl, ScopedProgram.vertexLength),
                                          Fby::Shaderc::ShaderStage::Vertex, &MVertexSpirv, &ScopedError))
    {
        ++MStats.failures;
        ScopedSlot.failed = true;
        SetSlotError(ScopedSlot, ScopedError);
        LogWarn("material", "material {} vertex glslang failed: {}", ScopedSlot.materialId, ScopedSlot.error);
        return false;
    }

    // The engine cache owns the pipeline from here on. Releasing first keeps the
    // reference count honest when the graph changed its contract (a different
    // closure needs a different pipeline) while the resident entry survives.
    ReleaseSlotPipeline(ScopedSlot);

    if (!InRhi.SupportsRuntimeShaderPipelines())
    {
        // A backend without runtime shader compilation (Null/headless) keeps the
        // engine shaders; the material still validates and compiles to SPIR-V.
        ScopedSlot.lastGoodHash = ScopedHash;
        ScopedSlot.failed = false;
        ScopedSlot.error[0] = '\0';
        MStats.pipelinesCreated = MPipelineCache.Stats().created;
        MStats.pipelinesDestroyed = MPipelineCache.Stats().destroyed;
        MStats.pipelinesShared = MPipelineCache.Stats().reused;
        LogInfo("material", "material {} compiled, backend has no runtime pipelines (using the default shader)",
                ScopedSlot.materialId);
        return true;
    }

    Render::CookedMaterialView ScopedMaterial;
    ScopedMaterial.graphHash = ScopedHash;
    ScopedMaterial.contract = Render::MaterialContractFromProgram(ScopedProgram);
    ScopedMaterial.vertexSpirv = SpirvBytes(MVertexSpirv);
    ScopedMaterial.fragmentSpirv = SpirvBytes(MFragmentSpirv);

    const Rhi::ShaderPipelineHandle ScopedPipeline = MPipelineCache.Acquire(InRhi, ScopedMaterial);
    MStats.pipelinesCreated = MPipelineCache.Stats().created;
    MStats.pipelinesDestroyed = MPipelineCache.Stats().destroyed;
    MStats.pipelinesShared = MPipelineCache.Stats().reused;
    if (!ScopedPipeline.IsValid())
    {
        ++MStats.failures;
        ScopedSlot.failed = true;
        SetSlotError(ScopedSlot, MPipelineCache.LastError());
        LogError("material", "material {} pipeline creation failed: {}", ScopedSlot.materialId, ScopedSlot.error);
        return false;
    }

    ScopedSlot.pipeline = ScopedPipeline;
    ScopedSlot.holdsPipeline = true;
    ScopedSlot.lastGoodHash = ScopedHash;
    ScopedSlot.failed = false;
    ScopedSlot.error[0] = '\0';
    return true;
}

void MaterialGraphRuntime::UpdateAll(Rhi::RHI& InRhi)
{
    // A failing material keeps its last good pipeline and reports through
    // LastError(); the loop never stops at the first failure.
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < MSlotCount; ++ScopedIndex)
    {
        if (MSlots[ScopedIndex].graph != nullptr)
        {
            static_cast<void>(Update(InRhi, *MSlots[ScopedIndex].graph));
        }
    }
}

Rhi::ShaderPipelineHandle MaterialGraphRuntime::Pipeline(std::uint32_t InMaterialId) const noexcept
{
    const std::uint32_t ScopedIndex = FindSlot(InMaterialId);
    return ScopedIndex == kInvalidSlot ? Rhi::ShaderPipelineHandle{} : MSlots[ScopedIndex].pipeline;
}

std::uint32_t MaterialGraphRuntime::MaterialPipelineId(std::uint32_t InMaterialId) const noexcept
{
    const Rhi::ShaderPipelineHandle ScopedPipeline = Pipeline(InMaterialId);
    return ScopedPipeline.IsValid() ? ScopedPipeline.id : kReservedMaterialId;
}

const char* MaterialGraphRuntime::LastError(std::uint32_t InMaterialId) const noexcept
{
    const std::uint32_t ScopedIndex = FindSlot(InMaterialId);
    return ScopedIndex == kInvalidSlot ? "" : MSlots[ScopedIndex].error;
}

bool MaterialGraphRuntime::IsRegistered(std::uint32_t InMaterialId) const noexcept
{
    return FindSlot(InMaterialId) != kInvalidSlot;
}

std::uint32_t MaterialGraphRuntime::FindSlot(std::uint32_t InMaterialId) const noexcept
{
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < MSlotCount; ++ScopedIndex)
    {
        if (MSlots[ScopedIndex].materialId == InMaterialId)
        {
            return ScopedIndex;
        }
    }
    return kInvalidSlot;
}

std::uint32_t MaterialGraphRuntime::FindSlotForGraph(const Render::MaterialGraph* InGraph) const noexcept
{
    if (InGraph == nullptr)
    {
        return kInvalidSlot;
    }
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < MSlotCount; ++ScopedIndex)
    {
        if (MSlots[ScopedIndex].graph == InGraph)
        {
            return ScopedIndex;
        }
    }
    return kInvalidSlot;
}

void MaterialGraphRuntime::Shutdown(Rhi::RHI& InRhi)
{
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < MSlotCount; ++ScopedIndex)
    {
        ReleaseSlotPipeline(MSlots[ScopedIndex]);
        MSlots[ScopedIndex] = Slot{};
    }
    MPipelineCache.Shutdown(InRhi);
    MStats.pipelinesCreated = 0;
    MStats.pipelinesDestroyed = 0;
    MStats.pipelinesShared = 0;
    MSlotCount = 0;
}

void MaterialGraphRuntime::SetSlotError(Slot& InSlot, std::string_view InText) noexcept
{
    CopyError(InSlot.error, sizeof(InSlot.error), InText);
}
}
