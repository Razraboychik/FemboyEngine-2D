// FemboyEngine2D - cooked material and material graph payload tests.
#include "Engine/RHI/MaterialAsset.hpp"
#include "Engine/RHI/MaterialGraph.hpp"
#include "Engine/RHI/MaterialGraphCompiler.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
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

// Output <- Mul(TextureSample(layer 3), Color): the canonical textured material.
MaterialGraph BuildTintedGraph()
{
    MaterialGraph ScopedGraph;
    const std::uint16_t ScopedUv = ScopedGraph.AddInputNode(MaterialNodeKind::Uv);
    const std::uint16_t ScopedSample = ScopedGraph.AddTextureSampleNode(3);
    const std::uint16_t ScopedTint = ScopedGraph.AddColorNode(1.0f, 0.5f, 0.25f, 1.0f);
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

// A minimal module: magic plus the four header words. The loader only checks the
std::vector<std::uint32_t> MinimalSpirv()
{
    return {kSpirvMagicWord, 0u, 0u, 0u, 0u};
}

std::span<const std::byte> AsBytes(const std::vector<std::uint32_t>& InWords)
{
    return {reinterpret_cast<const std::byte*>(InWords.data()), InWords.size() * sizeof(std::uint32_t)};
}

void TestGraphRoundTrip()
{
    const MaterialGraph ScopedSource = BuildTintedGraph();
    std::vector<std::byte> ScopedBytes;
    FBY_CHECK(EncodeMaterialGraph(ScopedSource, &ScopedBytes));
    FBY_CHECK(ScopedBytes.size() == static_cast<std::size_t>(kMaterialGraphHeaderBytes) +
                                       ScopedSource.NodeCount() * kMaterialGraphNodeBytes +
                                       ScopedSource.PinCount() * kMaterialGraphPinBytes +
                                       ScopedSource.ParamCount() * kMaterialGraphParamBytes);

    MaterialGraph ScopedCopy;
    MaterialAssetError ScopedError = MaterialAssetError::BadMagic;
    FBY_CHECK(DecodeMaterialGraph(ScopedBytes, &ScopedCopy, &ScopedError));
    FBY_CHECK(ScopedError == MaterialAssetError::None);
    FBY_CHECK(ScopedCopy.NodeCount() == ScopedSource.NodeCount());
    FBY_CHECK(ScopedCopy.PinCount() == ScopedSource.PinCount());
    FBY_CHECK(ScopedCopy.ParamCount() == ScopedSource.ParamCount());
    FBY_CHECK(ScopedCopy.Hash() == ScopedSource.Hash());

    for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedSource.NodeCount(); ++ScopedIndex)
    {
        const std::uint16_t ScopedNode = static_cast<std::uint16_t>(ScopedIndex);
        FBY_CHECK(ScopedCopy.Node(ScopedNode).kind == ScopedSource.Node(ScopedNode).kind);
        FBY_CHECK(ScopedCopy.Node(ScopedNode).firstInput == ScopedSource.Node(ScopedNode).firstInput);
        FBY_CHECK(ScopedCopy.Node(ScopedNode).inputCount == ScopedSource.Node(ScopedNode).inputCount);
        FBY_CHECK(ScopedCopy.Node(ScopedNode).paramIndex == ScopedSource.Node(ScopedNode).paramIndex);
        FBY_CHECK(ScopedCopy.Node(ScopedNode).uiPosition.X == ScopedSource.Node(ScopedNode).uiPosition.X);
    }
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedSource.PinCount(); ++ScopedIndex)
    {
        const std::uint16_t ScopedPin = static_cast<std::uint16_t>(ScopedIndex);
        FBY_CHECK(ScopedCopy.Pin(ScopedPin).sourceNode == ScopedSource.Pin(ScopedPin).sourceNode);
        FBY_CHECK(ScopedCopy.Pin(ScopedPin).type == ScopedSource.Pin(ScopedPin).type);
    }
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedSource.ParamCount(); ++ScopedIndex)
    {
        const std::uint16_t ScopedParam = static_cast<std::uint16_t>(ScopedIndex);
        FBY_CHECK(ScopedCopy.Param(ScopedParam).kind == ScopedSource.Param(ScopedParam).kind);
        FBY_CHECK(ScopedCopy.Param(ScopedParam).textureLayer == ScopedSource.Param(ScopedParam).textureLayer);
        FBY_CHECK(ScopedCopy.Param(ScopedParam).X == ScopedSource.Param(ScopedParam).X);
        FBY_CHECK(ScopedCopy.Param(ScopedParam).Y == ScopedSource.Param(ScopedParam).Y);
    }

    // Positions are editor state, so they travel but must not change the hash.
    MaterialGraph ScopedMoved = ScopedSource;
    ScopedMoved.Node(1).uiPosition = Fby::Vector2D{400.0f, 300.0f};
    FBY_CHECK(ScopedMoved.Hash() == ScopedSource.Hash());
}

void TestGraphCorruption()
{
    const MaterialGraph ScopedSource = BuildTintedGraph();
    std::vector<std::byte> ScopedBytes;
    FBY_CHECK(EncodeMaterialGraph(ScopedSource, &ScopedBytes));

    MaterialGraph ScopedCopy;
    MaterialAssetError ScopedError = MaterialAssetError::None;

    std::vector<std::byte> ScopedTruncated = ScopedBytes;
    ScopedTruncated.resize(kMaterialGraphHeaderBytes - 1);
    FBY_CHECK(!DecodeMaterialGraph(ScopedTruncated, &ScopedCopy, &ScopedError));
    FBY_CHECK(ScopedError == MaterialAssetError::TooSmall);

    std::vector<std::byte> ScopedShort = ScopedBytes;
    ScopedShort.pop_back();
    FBY_CHECK(!DecodeMaterialGraph(ScopedShort, &ScopedCopy, &ScopedError));
    FBY_CHECK(ScopedError == MaterialAssetError::BadPayloadSize);

    std::vector<std::byte> ScopedMagic = ScopedBytes;
    ScopedMagic[0] = std::byte{0};
    FBY_CHECK(!DecodeMaterialGraph(ScopedMagic, &ScopedCopy, &ScopedError));
    FBY_CHECK(ScopedError == MaterialAssetError::BadMagic);

    std::vector<std::byte> ScopedVersion = ScopedBytes;
    ScopedVersion[4] = std::byte{2};
    FBY_CHECK(!DecodeMaterialGraph(ScopedVersion, &ScopedCopy, &ScopedError));
    FBY_CHECK(ScopedError == MaterialAssetError::UnsupportedVersion);

    // The first node kind sits directly after the fixed header.
    std::vector<std::byte> ScopedKind = ScopedBytes;
    ScopedKind[kMaterialGraphHeaderBytes] = std::byte{0xFE};
    FBY_CHECK(!DecodeMaterialGraph(ScopedKind, &ScopedCopy, &ScopedError));
    FBY_CHECK(ScopedError == MaterialAssetError::BadCounts);
}

void TestCookedRoundTrip()
{
    const std::vector<std::uint32_t> ScopedFragment = MinimalSpirv();
    const std::vector<std::uint32_t> ScopedVertex = MinimalSpirv();
    const std::uint16_t ScopedContract = static_cast<std::uint16_t>(MaterialContract::MaterialContractGeneratedVertexStage) |
                                         static_cast<std::uint16_t>(MaterialContract::MaterialContractFragmentMaterialParams);

    std::vector<std::byte> ScopedBytes;
    FBY_CHECK(EncodeCookedMaterial(0x0123456789ABCDEFull, ScopedContract, AsBytes(ScopedVertex),
                                   AsBytes(ScopedFragment), &ScopedBytes));
    FBY_CHECK(ScopedBytes.size() == kMaterialAssetHeaderBytes + AsBytes(ScopedVertex).size() +
                                       AsBytes(ScopedFragment).size());

    // A fresh buffer separates the decoded view from the encoder's storage.
    const std::vector<std::byte> ScopedStored = ScopedBytes;
    CookedMaterialView ScopedView;
    MaterialAssetError ScopedError = MaterialAssetError::BadMagic;
    FBY_CHECK(DecodeCookedMaterial(ScopedStored, &ScopedView, &ScopedError));
    FBY_CHECK(ScopedError == MaterialAssetError::None);
    FBY_CHECK(ScopedView.IsValid());
    FBY_CHECK(ScopedView.graphHash == 0x0123456789ABCDEFull);
    FBY_CHECK(ScopedView.contract == ScopedContract);
    FBY_CHECK(ScopedView.vertexSpirv.size() == AsBytes(ScopedVertex).size());
    FBY_CHECK(ScopedView.fragmentSpirv.size() == AsBytes(ScopedFragment).size());
    FBY_CHECK(ScopedView.vertexSpirv.data() >= ScopedStored.data());
    FBY_CHECK(ScopedView.vertexSpirv.data() < ScopedStored.data() + ScopedStored.size());

    const MaterialShaderProgram ScopedProgram = MaterialProgramFromContract(ScopedContract);
    FBY_CHECK(ScopedProgram.usesGeneratedVertexStage);
    FBY_CHECK(ScopedProgram.fragmentReadsMaterialParams);
    FBY_CHECK(!ScopedProgram.fragmentReadsViewProjection);
    FBY_CHECK(MaterialContractFromProgram(ScopedProgram) == ScopedContract);
}

void TestCookedCorruption()
{
    const std::vector<std::uint32_t> ScopedFragment = MinimalSpirv();
    const std::uint16_t ScopedVertexContract = static_cast<std::uint16_t>(
        MaterialContract::MaterialContractGeneratedVertexStage);

    std::vector<std::byte> ScopedNoVertex;
    FBY_CHECK(!EncodeCookedMaterial(7u, ScopedVertexContract, {}, AsBytes(ScopedFragment), &ScopedNoVertex));
    FBY_CHECK(ScopedNoVertex.empty());

    std::vector<std::byte> ScopedEmptyFragment;
    FBY_CHECK(!EncodeCookedMaterial(7u, MaterialContract::MaterialContractNone, {}, {}, &ScopedEmptyFragment));

    std::vector<std::byte> ScopedUnknownContract;
    FBY_CHECK(!EncodeCookedMaterial(7u, 0x8000u, {}, AsBytes(ScopedFragment), &ScopedUnknownContract));

    std::vector<std::byte> ScopedBadStage;
    FBY_CHECK(!EncodeCookedMaterial(7u, MaterialContract::MaterialContractNone, {},
                                    std::span<const std::byte>(AsBytes(ScopedFragment).data(), 8), &ScopedBadStage));

    std::vector<std::byte> ScopedBytes;
    FBY_CHECK(EncodeCookedMaterial(7u, MaterialContract::MaterialContractNone, {}, AsBytes(ScopedFragment),
                                   &ScopedBytes));

    CookedMaterialView ScopedView;
    MaterialAssetError ScopedError = MaterialAssetError::None;

    std::vector<std::byte> ScopedShort = ScopedBytes;
    ScopedShort.resize(kMaterialAssetHeaderBytes - 1);
    FBY_CHECK(!DecodeCookedMaterial(ScopedShort, &ScopedView, &ScopedError));
    FBY_CHECK(ScopedError == MaterialAssetError::TooSmall);

    std::vector<std::byte> ScopedMagic = ScopedBytes;
    ScopedMagic[0] = std::byte{0x42};
    FBY_CHECK(!DecodeCookedMaterial(ScopedMagic, &ScopedView, &ScopedError));
    FBY_CHECK(ScopedError == MaterialAssetError::BadMagic);

    std::vector<std::byte> ScopedVersion = ScopedBytes;
    ScopedVersion[4] = std::byte{9};
    FBY_CHECK(!DecodeCookedMaterial(ScopedVersion, &ScopedView, &ScopedError));
    FBY_CHECK(ScopedError == MaterialAssetError::UnsupportedVersion);

    std::vector<std::byte> ScopedTrailing = ScopedBytes;
    ScopedTrailing.push_back(std::byte{0});
    FBY_CHECK(!DecodeCookedMaterial(ScopedTrailing, &ScopedView, &ScopedError));
    FBY_CHECK(ScopedError == MaterialAssetError::BadPayloadSize);

    // The contract bit says the vertex stage exists, so clearing its length must
    // be rejected instead of producing a pipeline without a vertex shader.
    std::vector<std::byte> ScopedContract = ScopedBytes;
    ScopedContract[6] = ScopedContract[6] | std::byte{0x01};
    FBY_CHECK(!DecodeCookedMaterial(ScopedContract, &ScopedView, &ScopedError));
    FBY_CHECK(ScopedError == MaterialAssetError::BadContract);
}
}

int main()
{
    TestGraphRoundTrip();
    TestGraphCorruption();
    TestCookedRoundTrip();
    TestCookedCorruption();

    if (GFailures != 0)
    {
        std::fprintf(stderr, "material_asset: %d/%d checks failed\n", GFailures, GChecks);
        return 1;
    }
    std::printf("material_asset: %d checks passed\n", GChecks);
    return 0;
}
