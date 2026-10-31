// FemboyEngine2D - cooked material and material graph wire formats.
#pragma once

#include "Engine/Core/Types.hpp"
#include "Engine/RHI/MaterialGraph.hpp"
#include "Engine/RHI/MaterialGraphCompiler.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace Fby::Render
{

// Payload magics are stored little endian so a payload cooked on one host reads
// back byte for byte on another; "FBYG" and "FBYM" in file order.
inline constexpr std::uint32_t kMaterialGraphMagic = 0x47594246u;
inline constexpr std::uint32_t kMaterialAssetMagic = 0x4D594246u;
inline constexpr std::uint16_t kMaterialGraphVersion = 1;
inline constexpr std::uint16_t kMaterialAssetVersion = 1;

// Fixed headers. The runtime rejects a payload whose size does not match the
// counts before it touches any node, pin or stage byte.
inline constexpr std::uint32_t kMaterialGraphHeaderBytes = 16;
inline constexpr std::uint32_t kMaterialAssetHeaderBytes = 32;
inline constexpr std::uint32_t kMaterialGraphNodeBytes = 17;
inline constexpr std::uint32_t kMaterialGraphPinBytes = 8;
inline constexpr std::uint32_t kMaterialGraphParamBytes = 24;

// Hard cap on one cooked stage. A material that exceeds it is a design smell,
// not a reason to make the loader allocate without a bound.
inline constexpr std::uint32_t kMaxCookedStageBytes = 128u * 1024u;

// SPIR-V's first word and the smallest legal module. The engine repeats them
// instead of linking the build-time shader compiler into the shipped runtime.
inline constexpr std::uint32_t kSpirvMagicWord = 0x07230203u;
inline constexpr std::uint32_t kSpirvMinimumWords = 5;

// Why a payload was rejected. The loader reports one code for the whole decode
// instead of a string, so the frame path never formats an error message.
enum class MaterialAssetError : std::uint8_t
{
    None = 0,
    TooSmall,
    BadMagic,
    UnsupportedVersion,
    BadCounts,
    BadPayloadSize,
    BadSpirv,
    BadContract,
    Count
};

[[nodiscard]] constexpr const char* MaterialAssetErrorName(MaterialAssetError InError) noexcept
{
    switch (InError)
    {
        case MaterialAssetError::None:
            return "none";
        case MaterialAssetError::TooSmall:
            return "payload is too small";
        case MaterialAssetError::BadMagic:
            return "payload magic does not match";
        case MaterialAssetError::UnsupportedVersion:
            return "payload version is not supported";
        case MaterialAssetError::BadCounts:
            return "payload counts are out of range";
        case MaterialAssetError::BadPayloadSize:
            return "payload size does not match its counts";
        case MaterialAssetError::BadSpirv:
            return "payload does not contain a SPIR-V module";
        case MaterialAssetError::BadContract:
            return "payload shader contract is inconsistent";
        case MaterialAssetError::Count:
            break;
    }
    return "unknown";
}

// Shader contract bits. The cooked payload carries them so a runtime can build a
// pipeline without re-deriving the graph's requirements.
enum MaterialContract : std::uint16_t
{
    MaterialContractNone = 0,
    MaterialContractGeneratedVertexStage = 1u << 0,
    MaterialContractFragmentViewProjection = 1u << 1,
    MaterialContractFragmentMaterialParams = 1u << 2,
    MaterialContractWorldPositionVarying = 1u << 3,
    MaterialContractVertexPositionVarying = 1u << 4,
    // The fragment stage matches the sprite (quad.vert) interface, so the
    // pipeline links against the engine quad vertex shader.
    MaterialContractSpriteDomain = 1u << 5,
    MaterialContractKnown = (1u << 6) - 1u,
};

// True when the cooked payload targets the sprite domain.
[[nodiscard]] constexpr bool MaterialContractIsSprite(std::uint16_t InContract) noexcept
{
    return (InContract & static_cast<std::uint16_t>(MaterialContract::MaterialContractSpriteDomain)) != 0;
}

[[nodiscard]] constexpr std::uint16_t MaterialContractFromProgram(
    const MaterialShaderProgram& InProgram, MaterialDomain InDomain = MaterialDomain::Mesh) noexcept
{
    std::uint16_t ScopedContract = static_cast<std::uint16_t>(MaterialContract::MaterialContractNone);
    if (InProgram.usesGeneratedVertexStage)
    {
        ScopedContract |= static_cast<std::uint16_t>(MaterialContract::MaterialContractGeneratedVertexStage);
    }
    if (InProgram.fragmentReadsViewProjection)
    {
        ScopedContract |= static_cast<std::uint16_t>(MaterialContract::MaterialContractFragmentViewProjection);
    }
    if (InProgram.fragmentReadsMaterialParams)
    {
        ScopedContract |= static_cast<std::uint16_t>(MaterialContract::MaterialContractFragmentMaterialParams);
    }
    if (InProgram.usesWorldPositionVarying)
    {
        ScopedContract |= static_cast<std::uint16_t>(MaterialContract::MaterialContractWorldPositionVarying);
    }
    if (InProgram.usesVertexPositionVarying)
    {
        ScopedContract |= static_cast<std::uint16_t>(MaterialContract::MaterialContractVertexPositionVarying);
    }
    if (InDomain == MaterialDomain::Sprite)
    {
        ScopedContract |= static_cast<std::uint16_t>(MaterialContract::MaterialContractSpriteDomain);
    }
    return ScopedContract;
}

[[nodiscard]] constexpr MaterialShaderProgram MaterialProgramFromContract(std::uint16_t InContract) noexcept
{
    MaterialShaderProgram ScopedProgram;
    ScopedProgram.usesGeneratedVertexStage =
        (InContract & static_cast<std::uint16_t>(MaterialContract::MaterialContractGeneratedVertexStage)) != 0;
    ScopedProgram.fragmentReadsViewProjection =
        (InContract & static_cast<std::uint16_t>(MaterialContract::MaterialContractFragmentViewProjection)) != 0;
    ScopedProgram.fragmentReadsMaterialParams =
        (InContract & static_cast<std::uint16_t>(MaterialContract::MaterialContractFragmentMaterialParams)) != 0;
    ScopedProgram.usesWorldPositionVarying =
        (InContract & static_cast<std::uint16_t>(MaterialContract::MaterialContractWorldPositionVarying)) != 0;
    ScopedProgram.usesVertexPositionVarying =
        (InContract & static_cast<std::uint16_t>(MaterialContract::MaterialContractVertexPositionVarying)) != 0;
    return ScopedProgram;
}

// A decoded cooked material. Both spans point into the caller's payload and stay
// valid exactly as long as that payload does; decoding never copies or allocates.
struct CookedMaterialView
{
    // MaterialGraph::Hash() of the authored graph; also the pipeline cache key.
    std::uint64_t graphHash = 0;
    // MaterialContract mask.
    std::uint16_t contract = MaterialContract::MaterialContractNone;
    // Empty when the material uses the engine vertex stage.
    std::span<const std::byte> vertexSpirv{};
    // Always present for a cooked material.
    std::span<const std::byte> fragmentSpirv{};

    [[nodiscard]] bool IsValid() const noexcept
    {
        return graphHash != 0 && !fragmentSpirv.empty();
    }
};

// Writes a .femmat payload: the header, the vertex stage, then the fragment
// stage. `OutBytes` is replaced, so the caller can reuse one buffer per cook.
[[nodiscard]] bool EncodeCookedMaterial(std::uint64_t InGraphHash, std::uint16_t InContract,
                                        std::span<const std::byte> InVertexSpirv,
                                        std::span<const std::byte> InFragmentSpirv,
                                        std::vector<std::byte>* OutBytes) noexcept;

// Validates and views a .femmat payload without allocating. Returns false and
// writes `OutError` when the payload is malformed; `OutView` is then untouched.
[[nodiscard]] bool DecodeCookedMaterial(std::span<const std::byte> InBytes, CookedMaterialView* OutView,
                                        MaterialAssetError* OutError = nullptr) noexcept;

// Writes a .fgraph payload: the graph's counts, nodes, pins and params in
// dependency-independent storage order. Editor positions travel with it because
// a re-cook should reproduce the same canvas.
[[nodiscard]] bool EncodeMaterialGraph(const MaterialGraph& InGraph, std::vector<std::byte>* OutBytes) noexcept;

// Restores a graph written by EncodeMaterialGraph. The graph is only structurally
// checked here; Validate() and the compiler still own the semantic checks.
[[nodiscard]] bool DecodeMaterialGraph(std::span<const std::byte> InBytes, MaterialGraph* OutGraph,
                                       MaterialAssetError* OutError = nullptr) noexcept;
}
