// FemboyEngine2D - end-to-end material cook test: fby_matc plus the engine loader.
#include "Engine/RHI/MaterialAsset.hpp"
#include "Engine/RHI/MaterialGraph.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

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
    ScopedStream.read(reinterpret_cast<char*>(OutBytes->data()), ScopedSize);
    return static_cast<bool>(ScopedStream);
}

int RunCooker(const std::filesystem::path& InInput, const std::filesystem::path& InCookedPath,
              bool InSprite = false)
{
    // FBY_MATC_PATH arrives quoted from CMake, so the argument list only needs
    // quotes around the two paths.
    const std::string ScopedCommand = std::string(FBY_MATC_PATH) + " \"" + InInput.string() + "\" -o \"" +
                                      InCookedPath.string() + "\"" + (InSprite ? " --sprite" : "");
    return std::system(ScopedCommand.c_str());
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
