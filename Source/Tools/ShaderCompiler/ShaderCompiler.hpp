// FemboyEngine2D - reusable in-process GLSL -> SPIR-V compiler core.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Fby::Shaderc
{

// SPIR-V's first word. Tests and the runtime use this to reject a compiler that
inline constexpr std::uint32_t kSpirvMagic = 0x07230203u;
// Smallest legal module: magic, version, generator, bound, schema.
inline constexpr std::uint32_t kSpirvMinimumWords = 5;

enum class ShaderStage : std::uint8_t
{
    Vertex = 0,
    Fragment,
    Compute,
    Geometry,
};

[[nodiscard]] constexpr const char* ShaderStageName(ShaderStage InStage) noexcept
{
    switch (InStage)
    {
        case ShaderStage::Vertex:
            return "vertex";
        case ShaderStage::Fragment:
            return "fragment";
        case ShaderStage::Compute:
            return "compute";
        case ShaderStage::Geometry:
            return "geometry";
    }
    return "?";
}

// True for a structurally plausible SPIR-V module. Used by the runtime before it
[[nodiscard]] bool IsSpirvModule(const std::uint32_t* InWords, std::size_t InWordCount) noexcept;

// Compiles one GLSL 450 shader stage to SPIR-V. Returns false and fills `error`
[[nodiscard]] bool CompileGlslToSpirv(std::string_view InSource, ShaderStage InStage,
                                      std::vector<std::uint32_t>* InSpirv, std::string* InError) noexcept;
}
