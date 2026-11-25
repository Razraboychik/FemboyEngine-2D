// FemboyEngine2D - end-to-end material cook test: fby_matc plus the engine loader.
#include "Engine/RHI/MaterialAsset.hpp"
#include "Engine/RHI/MaterialGraph.hpp"
#include "Tools/MaterialCompiler/DxcArguments.hpp"

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#if defined(_WIN32)
#include <windows.h>
#else
#include <spawn.h>
#include <sys/wait.h>
#endif

#if !defined(_WIN32)
extern char** environ;
#endif

namespace
{

int GChecks = 0;
int GFailures = 0;

void Check(bool InCondition, const char* InExpression, const char* InFile, int InLine)
{
    ++GChecks;
    if (!InCondition)
    {
        ++GFailures;
        std::fprintf(stderr, "FAIL %s:%d: %s\n", InFile, InLine, InExpression);
    }
}

#define FBY_CHECK(expression) Check((expression), #expression, __FILE__, __LINE__)

using namespace Fby::Render;

// Output <- Mul(TextureSample(layer 1), Color). A fragment-only graph keeps the
// cook independent of the generated vertex stage.
MaterialGraph BuildTintedGraph()
{
    MaterialGraph ScopedGraph;
    const std::uint16_t ScopedUv = ScopedGraph.AddInputNode(MaterialNodeKind::Uv);
    const std::uint16_t ScopedSample = ScopedGraph.AddTextureSampleNode(1);
    const std::uint16_t ScopedTint = ScopedGraph.AddColorNode(0.75f, 0.5f, 0.25f, 1.0f);
    const std::uint16_t ScopedMultiply = ScopedGraph.AddBinaryNode(MaterialNodeKind::Mul);

    FBY_CHECK(ScopedUv != kInvalidMaterialNode);
    FBY_CHECK(ScopedSample != kInvalidMaterialNode);
    FBY_CHECK(ScopedTint != kInvalidMaterialNode);
    FBY_CHECK(ScopedMultiply != kInvalidMaterialNode);

    FBY_CHECK(ScopedGraph.Connect(ScopedSample, 0, ScopedUv));
    FBY_CHECK(ScopedGraph.Connect(ScopedMultiply, 0, ScopedSample));
    FBY_CHECK(ScopedGraph.Connect(ScopedMultiply, 1, ScopedTint));
    FBY_CHECK(ScopedGraph.Connect(ScopedGraph.OutputNode(), 0, ScopedMultiply));
    return ScopedGraph;
}

bool WriteBytes(const std::filesystem::path& InPath, std::span<const std::byte> InBytes)
{
    std::ofstream ScopedStream(InPath, std::ios::binary);
    if (!ScopedStream)
    {
        return false;
    }
    ScopedStream.write(reinterpret_cast<const char*>(InBytes.data()), static_cast<std::streamsize>(InBytes.size()));
    ScopedStream.close();
    return static_cast<bool>(ScopedStream);
}

bool ReadBytes(const std::filesystem::path& InPath, std::vector<std::byte>* OutBytes)
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
    if (!ScopedStream)
    {
        return false;
    }
    ScopedStream.read(reinterpret_cast<char*>(OutBytes->data()), ScopedSize);
    return static_cast<bool>(ScopedStream);
}

int RunCooker(const std::filesystem::path& InInput, const std::filesystem::path& InCookedPath,
              bool InSprite = false, bool InDx12 = false)
{
    std::vector<std::string> ScopedArguments{FBY_MATC_PATH, InInput.string(), "-o", InCookedPath.string()};
    if (InSprite)
    {
        ScopedArguments.push_back("--sprite");
    }
    if (InDx12)
    {
        ScopedArguments.push_back("--dx12");
    }
#if defined(_WIN32)
    const auto ToWide = [](std::string_view InText)
    {
        if (InText.empty())
        {
            return std::wstring{};
        }
        const int ScopedLength = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, InText.data(),
                                                     static_cast<int>(InText.size()), nullptr, 0);
        if (ScopedLength <= 0)
        {
            return std::wstring{};
        }
        std::wstring ScopedWide(static_cast<std::size_t>(ScopedLength), L'\0');
        if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, InText.data(), static_cast<int>(InText.size()),
                                ScopedWide.data(), ScopedLength) != ScopedLength)
        {
            return std::wstring{};
        }
        return ScopedWide;
    };
    std::wstring ScopedCommandLine;
    for (std::size_t ScopedIndex = 0; ScopedIndex < ScopedArguments.size(); ++ScopedIndex)
    {
        if (ScopedIndex != 0)
        {
            ScopedCommandLine.push_back(L' ');
        }
        const std::wstring ScopedWide = ToWide(ScopedArguments[ScopedIndex]);
        if (ScopedWide.empty())
        {
            return 127;
        }
        ScopedCommandLine += Fby::Tools::MaterialCompiler::QuoteWindowsArgument(ScopedWide);
    }
    STARTUPINFOW ScopedStartup{};
    ScopedStartup.cb = sizeof(ScopedStartup);
    PROCESS_INFORMATION ScopedProcess{};
    if (CreateProcessW(nullptr, ScopedCommandLine.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr,
                       &ScopedStartup, &ScopedProcess) == FALSE)
    {
        return 127;
    }
    const DWORD ScopedWait = WaitForSingleObject(ScopedProcess.hProcess, INFINITE);
    DWORD ScopedExit = 127;
    if (ScopedWait != WAIT_OBJECT_0 || GetExitCodeProcess(ScopedProcess.hProcess, &ScopedExit) == FALSE)
    {
        ScopedExit = 127;
    }
    CloseHandle(ScopedProcess.hThread);
    CloseHandle(ScopedProcess.hProcess);
    return static_cast<int>(ScopedExit);
#else
    std::vector<char*> ScopedArgv;
    ScopedArgv.reserve(ScopedArguments.size() + 1);
    for (std::string& ScopedArgument : ScopedArguments)
    {
        ScopedArgv.push_back(ScopedArgument.data());
    }
    ScopedArgv.push_back(nullptr);
    pid_t ScopedProcess = 0;
    if (posix_spawnp(&ScopedProcess, FBY_MATC_PATH, nullptr, nullptr, ScopedArgv.data(), environ) != 0)
    {
        return 127;
    }
    int ScopedStatus = 0;
    pid_t ScopedWaited = 0;
    do
    {
        ScopedWaited = waitpid(ScopedProcess, &ScopedStatus, 0);
    } while (ScopedWaited < 0 && errno == EINTR);
    if (ScopedWaited != ScopedProcess || !WIFEXITED(ScopedStatus))
    {
        return 127;
    }
    return WEXITSTATUS(ScopedStatus);
#endif
}
}

int main()
{
    std::error_code ScopedError;
    const std::filesystem::path ScopedRoot = std::filesystem::current_path(ScopedError) / "fby_material_cook_test";
    if (ScopedError)
    {
        std::fprintf(stderr, "material_cook: no working directory\n");
        return 1;
    }
    std::filesystem::remove_all(ScopedRoot, ScopedError);
    ScopedError.clear();
    std::filesystem::create_directories(ScopedRoot, ScopedError);
    if (ScopedError)
    {
        std::fprintf(stderr, "material_cook: cannot create %s\n", ScopedRoot.string().c_str());
        return 1;
    }

    const MaterialGraph ScopedGraph = BuildTintedGraph();
    std::vector<std::byte> ScopedGraphBytes;
    FBY_CHECK(EncodeMaterialGraph(ScopedGraph, &ScopedGraphBytes));

    const std::filesystem::path ScopedGraphPath = ScopedRoot / "tinted.fgraph";
    const std::filesystem::path ScopedCookedPath = ScopedRoot / "tinted.femmat";
    FBY_CHECK(WriteBytes(ScopedGraphPath, ScopedGraphBytes));

    FBY_CHECK(RunCooker(ScopedGraphPath, ScopedCookedPath) == 0);

    std::vector<std::byte> ScopedCookedBytes;
    FBY_CHECK(ReadBytes(ScopedCookedPath, &ScopedCookedBytes));
    FBY_CHECK(ScopedCookedBytes.size() > kMaterialAssetHeaderBytes);

    CookedMaterialView ScopedView;
    MaterialAssetError ScopedPayloadError = MaterialAssetError::BadMagic;
    FBY_CHECK(DecodeCookedMaterial(ScopedCookedBytes, &ScopedView, &ScopedPayloadError));
    FBY_CHECK(ScopedPayloadError == MaterialAssetError::None);
    FBY_CHECK(ScopedView.IsValid());
    FBY_CHECK(ScopedView.graphHash == ScopedGraph.Hash());
    FBY_CHECK(ScopedView.vertexSpirv.empty());
    FBY_CHECK((ScopedView.contract & MaterialContract::MaterialContractGeneratedVertexStage) == 0);
    FBY_CHECK(ScopedView.fragmentSpirv.size() >= kSpirvMinimumWords * sizeof(std::uint32_t));

    // HLSL/DX12 cooking is an offline extra: the material compiler reports it
    // as unavailable when SPIRV-Cross was not fetched by setup. The Vulkan cook
    // above is the mandatory path, so a missing DX12 toolchain is a skip.
    const std::filesystem::path ScopedDx12Path = ScopedRoot / "tinted_dx12.femmat";
    if (RunCooker(ScopedGraphPath, ScopedDx12Path, false, true) != 0)
    {
        std::printf("material_cook: DX12/HLSL cooking is unavailable on this host; skipping\n");
    }
    else
    {
        std::vector<std::byte> ScopedDx12Bytes;
        FBY_CHECK(ReadBytes(ScopedDx12Path, &ScopedDx12Bytes));
        CookedMaterialView ScopedDx12View;
        FBY_CHECK(DecodeCookedMaterial(ScopedDx12Bytes, &ScopedDx12View));
        if (std::getenv("FBY_DXC") != nullptr)
        {
            FBY_CHECK(!ScopedDx12View.fragmentDxil.empty());
        }
        const std::filesystem::path ScopedHlslPath = ScopedDx12Path.string() + ".hlsl";
        std::vector<std::byte> ScopedHlslBytes;
        FBY_CHECK(ReadBytes(ScopedHlslPath, &ScopedHlslBytes));
        const std::string ScopedHlsl(reinterpret_cast<const char*>(ScopedHlslBytes.data()), ScopedHlslBytes.size());
        FBY_CHECK(ScopedHlsl.find("SV_Target") != std::string::npos);
        FBY_CHECK(ScopedHlsl.find("Texture2DArray") != std::string::npos);
    }

    // The sprite domain: the contract names the quad interface, and a sprite
    // payload never needs a vertex stage.
    const std::filesystem::path ScopedSpritePath = ScopedRoot / "tinted_sprite.femmat";
    FBY_CHECK(RunCooker(ScopedGraphPath, ScopedSpritePath, true) == 0);
    std::vector<std::byte> ScopedSpriteBytes;
    FBY_CHECK(ReadBytes(ScopedSpritePath, &ScopedSpriteBytes));
    CookedMaterialView ScopedSpriteView;
    FBY_CHECK(DecodeCookedMaterial(ScopedSpriteBytes, &ScopedSpriteView));
    FBY_CHECK(ScopedSpriteView.IsValid());
    FBY_CHECK(MaterialContractIsSprite(ScopedSpriteView.contract));
    FBY_CHECK(ScopedSpriteView.vertexSpirv.empty());
    // The same graph cooks to the same hash in both domains; the contract, not
    // the hash, selects the pipeline the loader builds.
    FBY_CHECK(ScopedSpriteView.graphHash == ScopedView.graphHash);
    FBY_CHECK(ScopedSpriteView.contract != ScopedView.contract);

    // A payload the tool cannot decode must fail instead of producing an asset.
    const std::filesystem::path ScopedBrokenPath = ScopedRoot / "broken.fgraph";
    const std::filesystem::path ScopedRejectedPath = ScopedRoot / "broken.femmat";
    const std::vector<std::byte> ScopedBroken(32, std::byte{0x11});
    FBY_CHECK(WriteBytes(ScopedBrokenPath, ScopedBroken));
    FBY_CHECK(RunCooker(ScopedBrokenPath, ScopedRejectedPath) != 0);
    FBY_CHECK(!std::filesystem::exists(ScopedRejectedPath));

    std::filesystem::remove_all(ScopedRoot, ScopedError);

    if (GFailures != 0)
    {
        std::fprintf(stderr, "material_cook: %d/%d checks failed\n", GFailures, GChecks);
        return 1;
    }
    std::printf("material_cook: %d checks passed\n", GChecks);
    return 0;
}
