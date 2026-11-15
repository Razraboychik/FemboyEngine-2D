// FemboyEngine2D - MaterialGraph to cooked material compiler.
#include "Engine/RHI/MaterialAsset.hpp"
#include "Engine/RHI/MaterialGraphCompiler.hpp"
#include "Tools/ShaderCompiler/ShaderCompiler.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#if defined(_WIN32)
#include <windows.h>
#endif
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#if FBY_WITH_SPIRV_CROSS
#include "spirv_hlsl.hpp"
#endif

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

[[nodiscard]] std::string FindDxc()
{
    if (const char* ScopedExplicit = std::getenv("FBY_DXC"); ScopedExplicit != nullptr)
    {
        return ScopedExplicit;
    }
    if (const char* ScopedVulkan = std::getenv("VULKAN_SDK"); ScopedVulkan != nullptr)
    {
        const std::filesystem::path ScopedPath =
            std::filesystem::path(ScopedVulkan) / "Bin" / "dxc.exe";
        if (std::filesystem::exists(ScopedPath))
        {
            return ScopedPath.string();
        }
    }
    return "dxc";
}

[[nodiscard]] bool CompileHlslToDxil(const std::string& InHlslPath, const std::string& InDxilPath,
                                      const char* InProfile)
{
    const std::string ScopedCompiler = FindDxc();
    const std::string ScopedCommand = "\"" + ScopedCompiler + "\" -T " + InProfile + " -E main -Fo \"" +
                                      InDxilPath + "\" \"" + InHlslPath + "\"";
#if defined(_WIN32)
    const int ScopedWideLength =
        MultiByteToWideChar(CP_UTF8, 0, ScopedCommand.c_str(), -1, nullptr, 0);
    std::wstring ScopedWide(static_cast<std::size_t>(ScopedWideLength), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, ScopedCommand.c_str(), -1, ScopedWide.data(), ScopedWideLength);
    STARTUPINFOW ScopedStartup{};
    ScopedStartup.cb = sizeof(ScopedStartup);
    PROCESS_INFORMATION ScopedProcess{};
    if (CreateProcessW(nullptr, ScopedWide.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &ScopedStartup,
                       &ScopedProcess) == FALSE)
    {
        return false;
    }
    WaitForSingleObject(ScopedProcess.hProcess, INFINITE);
    DWORD ScopedExit = 1;
    GetExitCodeProcess(ScopedProcess.hProcess, &ScopedExit);
    CloseHandle(ScopedProcess.hThread);
    CloseHandle(ScopedProcess.hProcess);
    return ScopedExit == 0 && std::filesystem::exists(InDxilPath);
#else
    return std::system(ScopedCommand.c_str()) == 0 && std::filesystem::exists(InDxilPath);
#endif
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
    bool ScopedDx12 = false;
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
        else if (std::strcmp(argv[ScopedIndex], "--dx12") == 0)
        {
            ScopedDx12 = true;
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

    if (ScopedDx12)
    {
#if FBY_WITH_SPIRV_CROSS
        try
        {
            spirv_cross::CompilerHLSL ScopedFragmentHlsl(ScopedFragmentSpirv);
            auto ScopedFragmentOptions = ScopedFragmentHlsl.get_hlsl_options();
            ScopedFragmentOptions.shader_model = 60;
            ScopedFragmentHlsl.set_hlsl_options(ScopedFragmentOptions);
            ScopedFragmentHlsl.set_entry_point("main", spv::ExecutionModelFragment);
            std::string ScopedFragmentText = ScopedFragmentHlsl.compile();
            const std::size_t ScopedInput = ScopedFragmentText.find("struct SPIRV_Cross_Input");
            if (ScopedInput != std::string::npos)
            {
                const std::size_t ScopedBrace = ScopedFragmentText.find('{', ScopedInput);
                if (ScopedBrace != std::string::npos)
                {
                    ScopedFragmentText.insert(ScopedBrace + 1, "\n    float4 fbyPosition : SV_Position;");
                }
            }
            const std::string ScopedHlslPath = ScopedOutput + ".hlsl";
            if (!WriteFile(ScopedHlslPath, std::span<const std::byte>(
                                               reinterpret_cast<const std::byte*>(ScopedFragmentText.data()),
                                               ScopedFragmentText.size())))
            {
                std::fprintf(stderr, "fby_matc: cannot write '%s'\n", ScopedHlslPath.c_str());
                return 1;
            }
            std::printf("fby_matc: dx12 fragment HLSL -> %s\n", ScopedHlslPath.c_str());
            const std::string ScopedFragmentDxil = ScopedOutput + ".frag.dxil";
            if (!CompileHlslToDxil(ScopedHlslPath, ScopedFragmentDxil, "ps_6_0"))
            {
                std::fprintf(stderr, "fby_matc: dxc unavailable; HLSL remains for an explicit DXIL step\n");
            }
            if (ScopedProgram.usesGeneratedVertexStage)
            {
                spirv_cross::CompilerHLSL ScopedVertexHlsl(ScopedVertexSpirv);
                auto ScopedVertexOptions = ScopedVertexHlsl.get_hlsl_options();
                ScopedVertexOptions.shader_model = 60;
                ScopedVertexHlsl.set_hlsl_options(ScopedVertexOptions);
                ScopedVertexHlsl.set_entry_point("main", spv::ExecutionModelVertex);
                const std::string ScopedVertexText = ScopedVertexHlsl.compile();
                const std::string ScopedVertexPath = ScopedOutput + ".vert.hlsl";
                if (!WriteFile(ScopedVertexPath, std::span<const std::byte>(
                                                     reinterpret_cast<const std::byte*>(ScopedVertexText.data()),
                                                     ScopedVertexText.size())))
                {
                    std::fprintf(stderr, "fby_matc: cannot write '%s'\n", ScopedVertexPath.c_str());
                    return 1;
                }
                std::printf("fby_matc: dx12 vertex HLSL -> %s\n", ScopedVertexPath.c_str());
                const std::string ScopedVertexDxil = ScopedOutput + ".vert.dxil";
                if (!CompileHlslToDxil(ScopedVertexPath, ScopedVertexDxil, "vs_6_0"))
                {
                    std::fprintf(stderr, "fby_matc: dxc unavailable for the vertex stage\n");
                }
            }
        }
        catch (const std::exception& ScopedException)
        {
            std::fprintf(stderr, "fby_matc: DX12 HLSL translation failed: %s\n", ScopedException.what());
            return 1;
        }
#else
        std::fprintf(stderr, "fby_matc: --dx12 requires SPIRV-Cross (third_party/spirv-cross)\n");
        return 1;
#endif
    }

    const std::uint16_t ScopedContract = Fby::Render::MaterialContractFromProgram(ScopedProgram, ScopedDomain);
    std::vector<std::byte> ScopedVertexDxil;
    std::vector<std::byte> ScopedFragmentDxil;
    if (ScopedDx12)
    {
        (void)ReadFile(ScopedOutput + ".frag.dxil", &ScopedFragmentDxil);
        if (ScopedProgram.usesGeneratedVertexStage)
        {
            (void)ReadFile(ScopedOutput + ".vert.dxil", &ScopedVertexDxil);
        }
    }
    std::vector<std::byte> ScopedCookedBytes;
    const bool ScopedEncoded =
        !ScopedFragmentDxil.empty()
            ? Fby::Render::EncodeCookedMaterial(ScopedGraph.Hash(), ScopedContract, AsBytes(ScopedVertexSpirv),
                                                AsBytes(ScopedFragmentSpirv), ScopedVertexDxil, ScopedFragmentDxil,
                                                &ScopedCookedBytes)
            : Fby::Render::EncodeCookedMaterial(ScopedGraph.Hash(), ScopedContract, AsBytes(ScopedVertexSpirv),
                                                AsBytes(ScopedFragmentSpirv), &ScopedCookedBytes);
    if (!ScopedEncoded)
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
