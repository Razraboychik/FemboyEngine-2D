// FemboyEngine2D - MaterialGraph -> GLSL 450 transpiler.
#pragma once

#include "Engine/RHI/MaterialGraph.hpp"

#include <cstdint>
#include <span>
#include <string>

namespace Fby::Render
{

// Enough for the worst graph the capacity constants allow; the runtime allocates
inline constexpr std::uint32_t kMaterialGlslFragmentBytes = 32u * 1024u;
inline constexpr std::uint32_t kMaterialGlslVertexBytes = 8u * 1024u;

// Which engine shader interface the generated fragment stage has to match. The
// mesh domain replaces mesh.frag, so it may also replace the vertex stage. The
// sprite domain replaces the sampling block of quad.frag and keeps the engine
// quad vertex shader, so its inputs are the quad.vert varyings.
enum class MaterialDomain : std::uint8_t
{
    Mesh = 0,
    Sprite = 1,
};

struct MaterialCompileOptions
{
    // Emit a replacement vertex shader that reproduces engine mesh.vert and adds
    // the varyings the graph needs. Only the mesh domain can do this.
    bool generateVertexStage = false;
    // Which engine shader interface the fragment stage matches.
    MaterialDomain domain = MaterialDomain::Mesh;
    // Optional debug label embedded in the generated comment header only; it
    // never reaches the SPIR-V and never changes the graph hash.
    const char* debugName = nullptr;
};

struct MaterialShaderProgram
{
    // Lengths of the emitted text, without the terminating NUL.
    std::uint32_t vertexLength = 0;
    std::uint32_t fragmentLength = 0;

    // Contract flags for the pipeline layout; see the header comment.
    bool usesGeneratedVertexStage = false;
    // Push constant in the fragment stage
    bool fragmentReadsViewProjection = false;
    // Pc.materialParams (time / viewport)
    bool fragmentReadsMaterialParams = false;
    // Generated vertex writes location 3
    bool usesWorldPositionVarying = false;
    // Generated vertex writes location 4
    bool usesVertexPositionVarying = false;
};

// Stateless transpiler: all scratch memory lives on the stack of Compile().
class MaterialGraphCompiler
{
public:
    // Validates `graph`, then writes a NUL-terminated fragment shader into
    [[nodiscard]] bool Compile(const MaterialGraph& InGraph, const MaterialCompileOptions& InOptions,
                               std::span<char> InVertexBuffer, std::span<char> InFragmentBuffer,
                               MaterialShaderProgram* InProgram, std::string* InError) const;

    // Convenience for fragment-only materials (the common case): the generated
    [[nodiscard]] bool CompileFragment(const MaterialGraph& InGraph, std::span<char> InFragmentBuffer,
                                       MaterialShaderProgram* InProgram, std::string* InError) const;
};
}
