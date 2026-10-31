// FemboyEngine2D - MaterialGraph to cooked material compiler.
#include "Engine/RHI/MaterialAsset.hpp"
#include "Engine/RHI/MaterialGraphCompiler.hpp"
#include "Tools/ShaderCompiler/ShaderCompiler.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace
{

[[nodiscard]] bool ReadFile(const std::string& InPath, std::vector<std::byte>* OutBytes)
{
    std::ifstream ScopedStream(InPath, std::ios::binary | std::ios::ate);
    if (!ScopedStream)
    {
        return false;
    }
    const std::streamsize ScopedSize = ScopedStream.tellg();
    if (ScopedSize <= 0)
    {
        return false;
    }
    OutBytes->resize(static_cast<std::size_t>(ScopedSize));
    ScopedStream.seekg(0, std::ios::beg);
    ScopedStream.read(reinterpret_cast<char*>(OutBytes->data()), ScopedSize);
    return static_cast<bool>(ScopedStream);
}

[[nodiscard]] bool WriteFile(const std::string& InPath, std::span<const std::byte> InBytes)
{
    std::ofstream ScopedStream(InPath, std::ios::binary);
    if (!ScopedStream)
    {
        return false;
    }
    ScopedStream.write(reinterpret_cast<const char*>(InBytes.data()), static_cast<std::streamsize>(InBytes.size()));
    return static_cast<bool>(ScopedStream);
}

[[nodiscard]] std::span<const std::byte> AsBytes(const std::vector<std::uint32_t>& InWords)
{
    return {reinterpret_cast<const std::byte*>(InWords.data()), InWords.size() * sizeof(std::uint32_t)};
}

void PrintUsage()
{
    std::fprintf(stderr,
                 "usage: fby_matc <input.fgraph> [-o <output.femmat>]\n"
                 "       compiles an authored MaterialGraph into a cooked material\n");
}
}

int main(int argc, char** argv)
{
    std::string ScopedInput;
    std::string ScopedOutput;
    Fby::Render::MaterialDomain ScopedDomain = Fby::Render::MaterialDomain::Mesh;
    for (int ScopedIndex = 1; ScopedIndex < argc; ++ScopedIndex)
    {
        if (std::strcmp(argv[ScopedIndex], "-o") == 0 && ScopedIndex + 1 < argc)
        {
            ScopedOutput = argv[++ScopedIndex];
        }
        else if (std::strcmp(argv[ScopedIndex], "--sprite") == 0)
        {
            ScopedDomain = Fby::Render::MaterialDomain::Sprite;
        }
        else
        {
            ScopedInput = argv[ScopedIndex];
        }
    }

    if (ScopedInput.empty())
    {
        PrintUsage();
        return 1;
    }
    if (ScopedOutput.empty())
    {
        const std::size_t ScopedDot = ScopedInput.find_last_of('.');
        ScopedOutput = (ScopedDot == std::string::npos ? ScopedInput : ScopedInput.substr(0, ScopedDot)) + ".femmat";
    }

    std::vector<std::byte> ScopedGraphBytes;
    if (!ReadFile(ScopedInput, &ScopedGraphBytes))
    {
        std::fprintf(stderr, "fby_matc: cannot read '%s'\n", ScopedInput.c_str());
        return 1;
    }

    Fby::Render::MaterialGraph ScopedGraph;
    Fby::Render::MaterialAssetError ScopedError = Fby::Render::MaterialAssetError::None;
    if (!Fby::Render::DecodeMaterialGraph(ScopedGraphBytes, &ScopedGraph, &ScopedError))
    {
        std::fprintf(stderr, "fby_matc: '%s': %s\n", ScopedInput.c_str(),
                     Fby::Render::MaterialAssetErrorName(ScopedError));
        return 1;
    }

    std::string ScopedText;
    if (!ScopedGraph.Validate(&ScopedText))
    {
        std::fprintf(stderr, "fby_matc: '%s' is not a valid material graph: %s\n", ScopedInput.c_str(),
                     ScopedText.c_str());
        return 1;
    }

    Fby::Render::MaterialCompileOptions ScopedOptions;
    ScopedOptions.domain = ScopedDomain;
    // The sprite domain always keeps the engine quad vertex stage.
    ScopedOptions.generateVertexStage =
        ScopedDomain == Fby::Render::MaterialDomain::Mesh && ScopedGraph.Requirements().needsVertexStage;
    ScopedOptions.debugName = ScopedInput.c_str();

    std::vector<char> ScopedFragmentGlsl(Fby::Render::kMaterialGlslFragmentBytes);
    std::vector<char> ScopedVertexGlsl(Fby::Render::kMaterialGlslVertexBytes);
    Fby::Render::MaterialShaderProgram ScopedProgram;
    const Fby::Render::MaterialGraphCompiler ScopedCompiler;
    const std::span<char> ScopedVertexBuffer = ScopedOptions.generateVertexStage
                                                   ? std::span<char>(ScopedVertexGlsl.data(), ScopedVertexGlsl.size())
                                                   : std::span<char>{};
    if (!ScopedCompiler.Compile(ScopedGraph, ScopedOptions, ScopedVertexBuffer,
                                std::span<char>(ScopedFragmentGlsl.data(), ScopedFragmentGlsl.size()), &ScopedProgram,
                                &ScopedText))
    {
        std::fprintf(stderr, "fby_matc: '%s' did not compile: %s\n", ScopedInput.c_str(), ScopedText.c_str());
        return 1;
    }

    std::vector<std::uint32_t> ScopedFragmentSpirv;
    if (!Fby::Shaderc::CompileGlslToSpirv(std::string_view(ScopedFragmentGlsl.data(), ScopedProgram.fragmentLength),
                                          Fby::Shaderc::ShaderStage::Fragment, &ScopedFragmentSpirv, &ScopedText))
    {
        std::fprintf(stderr, "fby_matc: fragment glslang failed: %s\n", ScopedText.c_str());
        return 1;
    }

    std::vector<std::uint32_t> ScopedVertexSpirv;
    if (ScopedProgram.usesGeneratedVertexStage &&
        !Fby::Shaderc::CompileGlslToSpirv(std::string_view(ScopedVertexGlsl.data(), ScopedProgram.vertexLength),
                                          Fby::Shaderc::ShaderStage::Vertex, &ScopedVertexSpirv, &ScopedText))
    {
        std::fprintf(stderr, "fby_matc: vertex glslang failed: %s\n", ScopedText.c_str());
        return 1;
    }

    const std::uint16_t ScopedContract = Fby::Render::MaterialContractFromProgram(ScopedProgram, ScopedDomain);
    std::vector<std::byte> ScopedCookedBytes;
    if (!Fby::Render::EncodeCookedMaterial(ScopedGraph.Hash(), ScopedContract, AsBytes(ScopedVertexSpirv),
                                           AsBytes(ScopedFragmentSpirv), &ScopedCookedBytes))
    {
        std::fprintf(stderr, "fby_matc: '%s' produced a payload the runtime would reject\n", ScopedInput.c_str());
        return 1;
    }
    if (!WriteFile(ScopedOutput, ScopedCookedBytes))
    {
        std::fprintf(stderr, "fby_matc: cannot write '%s'\n", ScopedOutput.c_str());
        return 1;
    }

    std::printf("fby_matc: %s -> %s (hash %016llx, %zu + %zu words)\n", ScopedInput.c_str(), ScopedOutput.c_str(),
                static_cast<unsigned long long>(ScopedGraph.Hash()), ScopedVertexSpirv.size(),
                ScopedFragmentSpirv.size());
    return 0;
}
