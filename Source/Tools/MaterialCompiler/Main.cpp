// FemboyEngine2D - MaterialGraph to cooked material compiler.
#include "Engine/RHI/MaterialAsset.hpp"
#include "Engine/RHI/MaterialGraphCompiler.hpp"
#include "Tools/ShaderCompiler/ShaderCompiler.hpp"
#include "Tools/MaterialCompiler/DxcArguments.hpp"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <optional>
#if defined(_WIN32)
#include <windows.h>
#else
#include <spawn.h>
#include <sys/wait.h>
#endif
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#if FBY_WITH_SPIRV_CROSS
#include "spirv_hlsl.hpp"
#endif

#if !defined(_WIN32)
extern char** environ;
#endif

namespace
{

[[nodiscard]] bool ReadFile(const std::string& InPath, std::vector<std::byte>* OutBytes)
{
    if (OutBytes == nullptr)
    {
        return false;
    }
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
    if (!ScopedStream)
    {
        return false;
    }
    ScopedStream.read(reinterpret_cast<char*>(OutBytes->data()), ScopedSize);
    return static_cast<bool>(ScopedStream);
}

[[nodiscard]] bool WriteFile(const std::string& InPath, std::span<const std::byte> InBytes)
{
    if (InPath.empty())
    {
        return false;
    }
    std::ofstream ScopedStream(InPath, std::ios::binary);
    if (!ScopedStream)
    {
        return false;
    }
    ScopedStream.write(reinterpret_cast<const char*>(InBytes.data()), static_cast<std::streamsize>(InBytes.size()));
    ScopedStream.close();
    return static_cast<bool>(ScopedStream);
}

[[nodiscard]] bool IsCompilerCandidate(const std::string& InPath)
{
    if (!Fby::Tools::MaterialCompiler::IsSafeProcessArgument(InPath))
    {
        return false;
    }
    const std::filesystem::path ScopedPath(InPath);
    if (ScopedPath.has_parent_path() || ScopedPath.has_root_path() || InPath.find('\\') != std::string::npos)
    {
        std::error_code ScopedError;
        return std::filesystem::is_regular_file(ScopedPath, ScopedError);
    }
    return true;
}

[[nodiscard]] std::string FindDxc()
{
    if (const char* ScopedExplicit = std::getenv("FBY_DXC"); ScopedExplicit != nullptr)
    {
        if (IsCompilerCandidate(ScopedExplicit))
        {
            return ScopedExplicit;
        }
    }
    if (const char* ScopedVulkan = std::getenv("VULKAN_SDK"); ScopedVulkan != nullptr)
    {
#if defined(_WIN32)
        const std::filesystem::path ScopedPath = std::filesystem::path(ScopedVulkan) / "Bin" / "dxc.exe";
#else
        const std::filesystem::path ScopedPath = std::filesystem::path(ScopedVulkan) / "bin" / "dxc";
#endif
        if (IsCompilerCandidate(ScopedPath.string()))
        {
            return ScopedPath.string();
        }
    }
    return "dxc";
}

#if defined(_WIN32)
[[nodiscard]] std::wstring Utf8ToWide(std::string_view InText)
{
    if (InText.empty())
    {
        return {};
    }
    const int ScopedLength = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, InText.data(),
                                                 static_cast<int>(InText.size()), nullptr, 0);
    if (ScopedLength <= 0)
    {
        return {};
    }
    std::wstring ScopedWide(static_cast<std::size_t>(ScopedLength), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, InText.data(), static_cast<int>(InText.size()),
                            ScopedWide.data(), ScopedLength) != ScopedLength)
    {
        return {};
    }
    return ScopedWide;
}
#endif

[[nodiscard]] bool CompileHlslToDxil(const std::string& InHlslPath, const std::string& InDxilPath,
                                      const char* InProfile)
{
    if (InProfile == nullptr)
    {
        return false;
    }
    const std::string ScopedCompiler = FindDxc();
    const std::optional<std::vector<std::string>> ScopedArguments =
        Fby::Tools::MaterialCompiler::BuildDxcArguments(ScopedCompiler, InHlslPath, InDxilPath, InProfile);
    if (!ScopedArguments.has_value())
    {
        return false;
    }
#if defined(_WIN32)
    const std::wstring ScopedWideCompiler = Utf8ToWide(ScopedCompiler);
    if (ScopedWideCompiler.empty())
    {
        return false;
    }
    std::wstring ScopedCommandLine;
    for (std::size_t ScopedIndex = 0; ScopedIndex < ScopedArguments->size(); ++ScopedIndex)
    {
        if (ScopedIndex != 0)
        {
            ScopedCommandLine.push_back(L' ');
        }
        const std::wstring ScopedWideArgument = Utf8ToWide((*ScopedArguments)[ScopedIndex]);
        if (ScopedWideArgument.empty())
        {
            return false;
        }
        ScopedCommandLine += Fby::Tools::MaterialCompiler::QuoteWindowsArgument(ScopedWideArgument);
    }
    // A bare compiler name is resolved through PATH, so CreateProcess must
    // derive the image from argv[0]. An explicit path is passed separately.
    const wchar_t* ScopedApplication =
        ScopedCompiler.find('/') != std::string::npos || ScopedCompiler.find('\\') != std::string::npos
            ? ScopedWideCompiler.c_str()
            : nullptr;
    STARTUPINFOW ScopedStartup{};
    ScopedStartup.cb = sizeof(ScopedStartup);
    PROCESS_INFORMATION ScopedProcess{};
    if (CreateProcessW(ScopedApplication, ScopedCommandLine.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr,
                       &ScopedStartup, &ScopedProcess) == FALSE)
    {
        return false;
    }
    const DWORD ScopedWait = WaitForSingleObject(ScopedProcess.hProcess, INFINITE);
    DWORD ScopedExit = 1;
    if (ScopedWait != WAIT_OBJECT_0 || GetExitCodeProcess(ScopedProcess.hProcess, &ScopedExit) == FALSE)
    {
        ScopedExit = 1;
    }
    CloseHandle(ScopedProcess.hThread);
    CloseHandle(ScopedProcess.hProcess);
    std::error_code ScopedOutputError;
    return ScopedExit == 0 && std::filesystem::is_regular_file(InDxilPath, ScopedOutputError);
#else
    std::vector<char*> ScopedArgv;
    ScopedArgv.reserve(ScopedArguments->size() + 1);
    for (const std::string& ScopedArgument : *ScopedArguments)
    {
        ScopedArgv.push_back(ScopedArgument.data());
    }
    ScopedArgv.push_back(nullptr);
    pid_t ScopedProcess = 0;
    const int ScopedSpawn = posix_spawnp(&ScopedProcess, ScopedCompiler.c_str(), nullptr, nullptr,
                                         ScopedArgv.data(), environ);
    if (ScopedSpawn != 0)
    {
        return false;
    }
    int ScopedStatus = 0;
    pid_t ScopedWaited = 0;
    do
    {
        ScopedWaited = waitpid(ScopedProcess, &ScopedStatus, 0);
    } while (ScopedWaited < 0 && errno == EINTR);
    std::error_code ScopedOutputError;
    return ScopedWaited == ScopedProcess && WIFEXITED(ScopedStatus) && WEXITSTATUS(ScopedStatus) == 0 &&
           std::filesystem::is_regular_file(InDxilPath, ScopedOutputError);
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
        const std::string ScopedFragmentDxilPath = ScopedOutput + ".frag.dxil";
        std::error_code ScopedFileError;
        if (std::filesystem::exists(ScopedFragmentDxilPath, ScopedFileError))
        {
            if (!ReadFile(ScopedFragmentDxilPath, &ScopedFragmentDxil))
            {
                std::fprintf(stderr, "fby_matc: cannot read '%s'\n", ScopedFragmentDxilPath.c_str());
                return 1;
            }
        }
        else if (ScopedFileError)
        {
            std::fprintf(stderr, "fby_matc: cannot inspect '%s': %s\n", ScopedFragmentDxilPath.c_str(),
                         ScopedFileError.message().c_str());
            return 1;
        }
        if (ScopedProgram.usesGeneratedVertexStage)
        {
            const std::string ScopedVertexDxilPath = ScopedOutput + ".vert.dxil";
            if (std::filesystem::exists(ScopedVertexDxilPath, ScopedFileError))
            {
                if (!ReadFile(ScopedVertexDxilPath, &ScopedVertexDxil))
                {
                    std::fprintf(stderr, "fby_matc: cannot read '%s'\n", ScopedVertexDxilPath.c_str());
                    return 1;
                }
            }
            else if (ScopedFileError)
            {
                std::fprintf(stderr, "fby_matc: cannot inspect '%s': %s\n", ScopedVertexDxilPath.c_str(),
                             ScopedFileError.message().c_str());
                return 1;
            }
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
