// FemboyEngine2D - MaterialGraph / MaterialGraphCompiler tests.
#include "Engine/RHI/MaterialAsset.hpp"
#include "Engine/RHI/MaterialGraph.hpp"
#include "Engine/RHI/MaterialGraphCompiler.hpp"

#ifdef FBY_TEST_HAVE_SHADERC
#include "Tools/ShaderCompiler/ShaderCompiler.hpp"
#endif

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <type_traits>
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

[[nodiscard]] bool Contains(std::string_view InHaystack, std::string_view InNeedle)
{
    return InHaystack.find(InNeedle) != std::string_view::npos;
}

using namespace Fby::Render;

// Graph fixtures

// Output <- Mul(TextureSample(layer 3), Color). The canonical "textured, tinted"
MaterialGraph BuildAtlasMaterialGraph()
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

// A graph that touches every interesting code path: atlas sample with an
MaterialGraph BuildRichMaterialGraph()
{
    MaterialGraph ScopedGraph;
    const std::uint16_t ScopedUv = ScopedGraph.AddInputNode(MaterialNodeKind::Uv);
    const std::uint16_t ScopedSample = ScopedGraph.AddTextureSampleNode(2);
    const std::uint16_t ScopedVertexColor = ScopedGraph.AddInputNode(MaterialNodeKind::VertexColor);
    const std::uint16_t ScopedTint = ScopedGraph.AddBinaryNode(MaterialNodeKind::Mul);
    FBY_CHECK(ScopedGraph.Connect(ScopedSample, 0, ScopedUv));
    FBY_CHECK(ScopedGraph.Connect(ScopedTint, 0, ScopedSample));
    FBY_CHECK(ScopedGraph.Connect(ScopedTint, 1, ScopedVertexColor));

    const std::uint16_t ScopedTime = ScopedGraph.AddInputNode(MaterialNodeKind::Time);
    const std::uint16_t ScopedPulse = ScopedGraph.AddUnaryNode(MaterialNodeKind::Sin);
    const std::uint16_t ScopedOneMinus = ScopedGraph.AddUnaryNode(MaterialNodeKind::OneMinus);
    const std::uint16_t ScopedOpacity = ScopedGraph.AddUnaryNode(MaterialNodeKind::Saturate);
    FBY_CHECK(ScopedGraph.Connect(ScopedPulse, 0, ScopedTime));
    FBY_CHECK(ScopedGraph.Connect(ScopedOneMinus, 0, ScopedPulse));
    FBY_CHECK(ScopedGraph.Connect(ScopedOpacity, 0, ScopedOneMinus));

    const std::uint16_t ScopedFresnel = ScopedGraph.AddFresnelNode();
    const std::uint16_t ScopedEmissive = ScopedGraph.AddUnaryNode(MaterialNodeKind::OneMinus);
    FBY_CHECK(ScopedGraph.Connect(ScopedEmissive, 0, ScopedFresnel));

    const std::uint16_t ScopedOutput = ScopedGraph.OutputNode();
    FBY_CHECK(ScopedGraph.Connect(ScopedOutput, 0, ScopedTint));
    FBY_CHECK(ScopedGraph.Connect(ScopedOutput, 1, ScopedOpacity));
    FBY_CHECK(ScopedGraph.Connect(ScopedOutput, 2, ScopedEmissive));
    return ScopedGraph;
}

// A graph that needs the generated vertex stage: VertexPosition is model space
MaterialGraph BuildVertexPositionGraph()
{
    MaterialGraph ScopedGraph;
    const std::uint16_t ScopedVertexPosition = ScopedGraph.AddInputNode(MaterialNodeKind::VertexPosition);
    const std::uint16_t ScopedTime = ScopedGraph.AddInputNode(MaterialNodeKind::Time);
    const std::uint16_t ScopedDisplacement = ScopedGraph.AddBinaryNode(MaterialNodeKind::Add);
    FBY_CHECK(ScopedGraph.Connect(ScopedDisplacement, 0, ScopedVertexPosition));
    FBY_CHECK(ScopedGraph.Connect(ScopedDisplacement, 1, ScopedTime));
    FBY_CHECK(ScopedGraph.Connect(ScopedGraph.OutputNode(), 0, ScopedDisplacement));
    return ScopedGraph;
}

// Tests
void TestDefaultLayout()
{
    MaterialGraph ScopedGraph;
    FBY_CHECK(ScopedGraph.NodeCount() == 1);
    FBY_CHECK(ScopedGraph.PinCount() == 3);
    FBY_CHECK(ScopedGraph.OutputNode() == 0);
    FBY_CHECK(ScopedGraph.Node(0).kind == MaterialNodeKind::Output);

    // A fresh graph has no BaseColor: it is a dangling required pin, which is
    std::string ScopedError;
    FBY_CHECK(!ScopedGraph.Validate(&ScopedError));
    FBY_CHECK(Contains(ScopedError, "unconnected"));

    // Clear() restores the same initial layout.
    ScopedGraph.Clear();
    FBY_CHECK(ScopedGraph.NodeCount() == 1);
    FBY_CHECK(ScopedGraph.PinCount() == 3);
}

void TestValidGraph()
{
    MaterialGraph ScopedGraph = BuildAtlasMaterialGraph();
    std::string ScopedError;
    FBY_CHECK(ScopedGraph.Validate(&ScopedError));
    if (!ScopedError.empty())
    {
        std::fprintf(stderr, "unexpected validation error: %s\n", ScopedError.c_str());
    }

    // Output must be the last node of the evaluation order.
    std::uint16_t ScopedOrder[kMaxMaterialNodes] = {};
    const std::uint32_t ScopedOrderCount = ScopedGraph.BuildEvaluationOrder(ScopedOrder);
    FBY_CHECK(ScopedOrderCount == ScopedGraph.NodeCount());
    FBY_CHECK(ScopedOrder[ScopedOrderCount - 1] == ScopedGraph.OutputNode());

    // Every node must resolve to a concrete type except the Output node itself.
    MaterialValueType ScopedTypes[kMaxMaterialNodes] = {};
    FBY_CHECK(ScopedGraph.ResolveTypes(ScopedTypes));
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedGraph.NodeCount(); ++ScopedIndex)
    {
        if (ScopedGraph.Node(static_cast<std::uint16_t>(ScopedIndex)).kind == MaterialNodeKind::Output)
        {
            FBY_CHECK(ScopedTypes[ScopedIndex] == MaterialValueType::Count);
        }
        else
        {
            FBY_CHECK(ScopedTypes[ScopedIndex] < MaterialValueType::Count);
        }
    }
}

void TestStableHash()
{
    MaterialGraph ScopedFirst = BuildAtlasMaterialGraph();
    MaterialGraph ScopedSecond = BuildAtlasMaterialGraph();
    FBY_CHECK(ScopedFirst.Hash() == ScopedSecond.Hash());

    // Moving a node in the editor is not a semantic change: no recompile.
    const std::uint64_t ScopedBeforeMove = ScopedFirst.Hash();
    ScopedFirst.Node(1).uiPosition = Fby::Vector2D{240.0f, -80.0f};
    FBY_CHECK(ScopedFirst.Hash() == ScopedBeforeMove);

    // Editing a constant is.
    MaterialParam& ScopedTint = ScopedFirst.Param(ScopedFirst.Node(3).paramIndex);
    ScopedTint.X = 0.25f;
    FBY_CHECK(ScopedFirst.Hash() != ScopedBeforeMove);

    // Rewiring an edge is too.
    const std::uint64_t ScopedBeforeRewire = ScopedFirst.Hash();
    ScopedFirst.Disconnect(4, 1);
    FBY_CHECK(ScopedFirst.Hash() != ScopedBeforeRewire);
}

void TestDanglingPinRejected()
{
    MaterialGraph ScopedGraph;
    const std::uint16_t ScopedBase = ScopedGraph.AddVec3Node(0.1f, 0.2f, 0.3f);
    const std::uint16_t ScopedAdd = ScopedGraph.AddBinaryNode(MaterialNodeKind::Add);
    FBY_CHECK(ScopedGraph.Connect(ScopedAdd, 0, ScopedBase));
    // Add -> vec3 (base + unconnected scalar)
    FBY_CHECK(ScopedGraph.Connect(ScopedGraph.OutputNode(), 0, ScopedAdd));

    std::string ScopedError;
    FBY_CHECK(!ScopedGraph.Validate(&ScopedError));
    FBY_CHECK(Contains(ScopedError, "unconnected"));
}

void TestExactlyOneOutput()
{
    MaterialGraph ScopedGraph = BuildAtlasMaterialGraph();
    // Corrupt the graph the way a bad deserializer would: a second Output node.
    ScopedGraph.Node(1).kind = MaterialNodeKind::Output;
    std::string ScopedError;
    FBY_CHECK(!ScopedGraph.Validate(&ScopedError));
    FBY_CHECK(Contains(ScopedError, "exactly one Output"));
}

void TestTypeMismatchesRejected()
{
    MaterialGraph ScopedGraph;
    const std::uint16_t ScopedUv = ScopedGraph.AddInputNode(MaterialNodeKind::Uv);
    const std::uint16_t ScopedScalar = ScopedGraph.AddScalarNode(2.0f);
    const std::uint16_t ScopedVec2Node = ScopedGraph.AddVec2Node(Fby::Vector2D{1.0f, 2.0f});
    const std::uint16_t ScopedVec3Node = ScopedGraph.AddVec3Node(1.0f, 2.0f, 3.0f);
    const std::uint16_t ScopedDot = ScopedGraph.AddBinaryNode(MaterialNodeKind::Dot);
    const std::uint16_t ScopedAdd = ScopedGraph.AddBinaryNode(MaterialNodeKind::Add);

    // Opacity takes a float; a vec2 input must be refused at connect time.
    std::string ScopedError;
    FBY_CHECK(!ScopedGraph.Connect(ScopedGraph.OutputNode(), 1, ScopedUv, &ScopedError));
    FBY_CHECK(!ScopedError.empty());

    // Dot yields a float; a TextureSample UV slot wants a vec2.
    FBY_CHECK(ScopedGraph.Connect(ScopedDot, 0, ScopedVec3Node));
    FBY_CHECK(ScopedGraph.Connect(ScopedDot, 1, ScopedVec3Node));
    const std::uint16_t ScopedSample = ScopedGraph.AddTextureSampleNode(0);
    ScopedError.clear();
    FBY_CHECK(!ScopedGraph.Connect(ScopedSample, 0, ScopedDot, &ScopedError));
    FBY_CHECK(!ScopedError.empty());

    // Cross-slot rule: Add cannot mix vec2 and vec3.
    FBY_CHECK(ScopedGraph.Connect(ScopedAdd, 0, ScopedVec2Node));
    ScopedError.clear();
    FBY_CHECK(!ScopedGraph.Connect(ScopedAdd, 1, ScopedVec3Node, &ScopedError));
    FBY_CHECK(!ScopedError.empty());
    // A scalar broadcasts into the vec2, so this one is legal.
    FBY_CHECK(ScopedGraph.Connect(ScopedAdd, 1, ScopedScalar));

    // Validation must also catch a hand-edited pin that bypassed Connect.
    MaterialGraph ScopedCorrupted = BuildAtlasMaterialGraph();
    const MaterialNode& ScopedOutput = ScopedCorrupted.Node(ScopedCorrupted.OutputNode());
    ScopedCorrupted.Pin(static_cast<std::uint16_t>(ScopedOutput.firstInput + 1)).sourceNode =
        // The vec4 Color node, into Opacity
        static_cast<std::uint16_t>(3);
    ScopedError.clear();
    FBY_CHECK(!ScopedCorrupted.Validate(&ScopedError));
    FBY_CHECK(Contains(ScopedError, "accept"));
}

void TestCycleDetection()
{
    MaterialGraph ScopedGraph;
    const std::uint16_t ScopedBase = ScopedGraph.AddVec3Node(0.5f, 0.5f, 0.5f);
    const std::uint16_t ScopedNodeA = ScopedGraph.AddBinaryNode(MaterialNodeKind::Add);
    const std::uint16_t ScopedNodeB = ScopedGraph.AddBinaryNode(MaterialNodeKind::Add);

    FBY_CHECK(ScopedGraph.Connect(ScopedNodeB, 0, ScopedBase));
    FBY_CHECK(ScopedGraph.Connect(ScopedNodeA, 0, ScopedBase));
    FBY_CHECK(ScopedGraph.Connect(ScopedNodeA, 1, ScopedNodeB));
    FBY_CHECK(ScopedGraph.Connect(ScopedGraph.OutputNode(), 0, ScopedNodeA));
    // B now consumes A, closing A -> B -> A.
    FBY_CHECK(ScopedGraph.Connect(ScopedNodeB, 1, ScopedNodeA));

    std::string ScopedError;
    FBY_CHECK(!ScopedGraph.Validate(&ScopedError));
    FBY_CHECK(Contains(ScopedError, "cycle"));

    std::uint16_t ScopedOrder[kMaxMaterialNodes] = {};
    ScopedError.clear();
    FBY_CHECK(
        ScopedGraph.BuildEvaluationOrder(std::span<std::uint16_t>(ScopedOrder, kMaxMaterialNodes), &ScopedError) == 0);
    FBY_CHECK(Contains(ScopedError, "cycle"));
}

void TestFragmentSnapshot()
{
    MaterialGraph ScopedGraph = BuildRichMaterialGraph();
    std::string ScopedError;
    FBY_CHECK(ScopedGraph.Validate(&ScopedError));

    char ScopedFragment[kMaterialGlslFragmentBytes] = {};
    MaterialShaderProgram ScopedProgram;
    const MaterialGraphCompiler ScopedCompiler;
    FBY_CHECK(ScopedCompiler.CompileFragment(ScopedGraph, std::span<char>(ScopedFragment, sizeof(ScopedFragment)),
                                             &ScopedProgram, &ScopedError));
    if (!ScopedError.empty())
    {
        std::fprintf(stderr, "unexpected compile error: %s\n", ScopedError.c_str());
    }
    FBY_CHECK(ScopedProgram.fragmentLength > 0);
    FBY_CHECK(!ScopedProgram.usesGeneratedVertexStage);
    // Time
    FBY_CHECK(ScopedProgram.fragmentReadsMaterialParams);
    // Fresnel needs the inverse
    FBY_CHECK(ScopedProgram.fragmentReadsViewProjection);

    const std::string_view ScopedText(ScopedFragment, ScopedProgram.fragmentLength);
    FBY_CHECK(ScopedText.size() == ScopedProgram.fragmentLength);
    FBY_CHECK(std::strlen(ScopedFragment) == ScopedProgram.fragmentLength);

    // Engine contract.
    FBY_CHECK(Contains(ScopedText, "#version 450"));
    FBY_CHECK(Contains(ScopedText, "layout(location = 0) in vec2 vUv;"));
    FBY_CHECK(Contains(ScopedText, "layout(location = 1) in vec4 vColor;"));
    FBY_CHECK(Contains(ScopedText, "layout(location = 2) flat in uint vLayer;"));
    FBY_CHECK(Contains(ScopedText, "layout(set = 0, binding = 0) uniform sampler2DArray uAtlas;"));
    FBY_CHECK(Contains(ScopedText, "layout(location = 0) out vec4 outColor;"));
    FBY_CHECK(Contains(ScopedText, "outColor = fbyColor;"));
    FBY_CHECK(Contains(ScopedText, "discard"));

    // Graph semantics: atlas layer 2, clock, world reconstruct, Fresnel, math.
    FBY_CHECK(Contains(ScopedText, "texture(uAtlas, vec3("));
    FBY_CHECK(Contains(ScopedText, ", 2.0))"));
    FBY_CHECK(Contains(ScopedText, "pc.materialParams"));
    FBY_CHECK(Contains(ScopedText, "fbyTime"));
    FBY_CHECK(Contains(ScopedText, "fbyInverseMat4"));
    FBY_CHECK(Contains(ScopedText, "fbyReconstructWorldPosition"));
    FBY_CHECK(Contains(ScopedText, "fbyFresnel"));
    FBY_CHECK(Contains(ScopedText, "sin("));
    FBY_CHECK(Contains(ScopedText, "clamp("));
    FBY_CHECK(Contains(ScopedText, "1.0 - "));

    // Deterministic: a second compile produces byte-identical source.
    char ScopedSecond[kMaterialGlslFragmentBytes] = {};
    MaterialShaderProgram ScopedSecondProgram;
    FBY_CHECK(ScopedCompiler.CompileFragment(ScopedGraph, std::span<char>(ScopedSecond, sizeof(ScopedSecond)),
                                             &ScopedSecondProgram, &ScopedError));
    FBY_CHECK(ScopedSecondProgram.fragmentLength == ScopedProgram.fragmentLength);
    FBY_CHECK(std::string_view(ScopedSecond, ScopedSecondProgram.fragmentLength) == ScopedText);
}

void TestAtlasDefaultUv()
{
    MaterialGraph ScopedGraph;
    const std::uint16_t ScopedSample = ScopedGraph.AddTextureSampleNode(0);
    FBY_CHECK(ScopedGraph.Connect(ScopedGraph.OutputNode(), 0, ScopedSample));

    char ScopedFragment[kMaterialGlslFragmentBytes] = {};
    MaterialShaderProgram ScopedProgram;
    const MaterialGraphCompiler ScopedCompiler;
    std::string ScopedError;
    FBY_CHECK(ScopedCompiler.CompileFragment(ScopedGraph, std::span<char>(ScopedFragment, sizeof(ScopedFragment)),
                                             &ScopedProgram, &ScopedError));
    const std::string_view ScopedText(ScopedFragment, ScopedProgram.fragmentLength);

    // Unconnected optional UV slot samples the engine varying at layer 0.
    FBY_CHECK(Contains(ScopedText, "texture(uAtlas, vec3(vUv, 0.0))"));
    FBY_CHECK(!ScopedProgram.fragmentReadsMaterialParams);
    FBY_CHECK(!ScopedProgram.fragmentReadsViewProjection);
    FBY_CHECK(std::strlen(ScopedFragment) == ScopedProgram.fragmentLength);
}

void TestVertexStage()
{
    MaterialGraph ScopedGraph = BuildVertexPositionGraph();
    std::string ScopedError;

    // VertexPosition cannot be reconstructed from the fragment stage.
    char ScopedFragment[kMaterialGlslFragmentBytes] = {};
    MaterialShaderProgram ScopedProgram;
    const MaterialGraphCompiler ScopedCompiler;
    FBY_CHECK(!ScopedCompiler.CompileFragment(ScopedGraph, std::span<char>(ScopedFragment, sizeof(ScopedFragment)),
                                              &ScopedProgram, &ScopedError));
    FBY_CHECK(Contains(ScopedError, "VertexPosition"));

    // With the generated stage it compiles, and the vertex shader owns the full
    char ScopedVertex[kMaterialGlslVertexBytes] = {};
    MaterialCompileOptions ScopedOptions;
    ScopedOptions.generateVertexStage = true;
    ScopedError.clear();
    FBY_CHECK(ScopedCompiler.Compile(ScopedGraph, ScopedOptions, std::span<char>(ScopedVertex, sizeof(ScopedVertex)),
                                     std::span<char>(ScopedFragment, sizeof(ScopedFragment)), &ScopedProgram,
                                     &ScopedError));
    if (!ScopedError.empty())
    {
        std::fprintf(stderr, "unexpected vertex compile error: %s\n", ScopedError.c_str());
    }
    const std::string_view ScopedVertexText(ScopedVertex, ScopedProgram.vertexLength);
    const std::string_view ScopedFragmentText(ScopedFragment, ScopedProgram.fragmentLength);
    FBY_CHECK(ScopedProgram.usesGeneratedVertexStage);
    FBY_CHECK(ScopedProgram.usesVertexPositionVarying);
    FBY_CHECK(Contains(ScopedVertexText, "#version 450"));
    FBY_CHECK(Contains(ScopedVertexText, "layout(push_constant) uniform Push"));
    FBY_CHECK(Contains(ScopedVertexText, "const vec4 world = draw.model * vertex.position;"));
    FBY_CHECK(Contains(ScopedVertexText, "gl_Position = pc.viewProjection * world;"));
    FBY_CHECK(Contains(ScopedVertexText, "layout(location = 4) out vec3 vVertexPosition;"));
    FBY_CHECK(Contains(ScopedVertexText, "vVertexPosition = vertex.position.xyz / vertex.position.w;"));
    FBY_CHECK(Contains(ScopedFragmentText, "layout(location = 4) in vec3 vVertexPosition;"));
    FBY_CHECK(!Contains(ScopedFragmentText, "fbyReconstructWorldPosition"));
}

void TestWorldPositionVaryingVersusFallback()
{
    MaterialGraph ScopedGraph;
    const std::uint16_t ScopedWorldPosition = ScopedGraph.AddInputNode(MaterialNodeKind::WorldPosition);
    const std::uint16_t ScopedOutput = ScopedGraph.OutputNode();
    FBY_CHECK(ScopedGraph.Connect(ScopedOutput, 0, ScopedWorldPosition));

    MaterialGraphCompiler ScopedCompiler;
    char ScopedFragment[kMaterialGlslFragmentBytes] = {};
    MaterialShaderProgram ScopedProgram;
    std::string ScopedError;

    // Fragment-only material: reconstruct world position from the inverse
    FBY_CHECK(ScopedCompiler.CompileFragment(ScopedGraph, std::span<char>(ScopedFragment, sizeof(ScopedFragment)),
                                             &ScopedProgram, &ScopedError));
    const std::string_view ScopedFallback(ScopedFragment, ScopedProgram.fragmentLength);
    FBY_CHECK(Contains(ScopedFallback, "fbyReconstructWorldPosition"));
    FBY_CHECK(Contains(ScopedFallback, "gl_FragCoord"));
    FBY_CHECK(Contains(ScopedFallback, "pc.materialParams"));

    // Generated vertex stage: world position arrives as a varying instead.
    char ScopedVertex[kMaterialGlslVertexBytes] = {};
    MaterialCompileOptions ScopedOptions;
    ScopedOptions.generateVertexStage = true;
    ScopedError.clear();
    FBY_CHECK(ScopedCompiler.Compile(ScopedGraph, ScopedOptions, std::span<char>(ScopedVertex, sizeof(ScopedVertex)),
                                     std::span<char>(ScopedFragment, sizeof(ScopedFragment)), &ScopedProgram,
                                     &ScopedError));
    const std::string_view ScopedWithVarying(ScopedFragment, ScopedProgram.fragmentLength);
    FBY_CHECK(ScopedProgram.usesWorldPositionVarying);
    FBY_CHECK(Contains(ScopedWithVarying, "layout(location = 3) in vec3 vWorldPosition;"));
    FBY_CHECK(!Contains(ScopedWithVarying, "fbyReconstructWorldPosition"));
    FBY_CHECK(Contains(std::string_view(ScopedVertex, ScopedProgram.vertexLength),
                       "layout(location = 3) out vec3 vWorldPosition;"));
}

void TestBufferOverflowRejected()
{
    MaterialGraph ScopedGraph = BuildRichMaterialGraph();
    char ScopedTiny[64] = {};
    MaterialShaderProgram ScopedProgram;
    std::string ScopedError;
    const MaterialGraphCompiler ScopedCompiler;
    FBY_CHECK(!ScopedCompiler.CompileFragment(ScopedGraph, std::span<char>(ScopedTiny, sizeof(ScopedTiny)),
                                              &ScopedProgram, &ScopedError));
    FBY_CHECK(Contains(ScopedError, "too small"));

    // An empty buffer is rejected the same way, and no length is reported.
    ScopedError.clear();
    FBY_CHECK(!ScopedCompiler.CompileFragment(ScopedGraph, std::span<char>{}, &ScopedProgram, &ScopedError));
    FBY_CHECK(ScopedProgram.fragmentLength == 0);
}

void TestCapacityLimits()
{
    MaterialGraph ScopedGraph;
    std::uint32_t ScopedAdded = 0;
    std::uint32_t ScopedPreviousValue = ScopedGraph.NodeCount();
    while (true)
    {
        const std::uint16_t ScopedNode = ScopedGraph.AddScalarNode(1.0f);
        if (ScopedNode == kInvalidMaterialNode)
        {
            break;
        }
        ++ScopedAdded;
        if (ScopedNode < ScopedPreviousValue)
        {
            // Indices must be monotonic
            FBY_CHECK(false);
            break;
        }
        ScopedPreviousValue = ScopedNode;
    }
    FBY_CHECK(ScopedAdded > 0);
    FBY_CHECK(ScopedGraph.NodeCount() <= kMaxMaterialNodes);

    // A fresh graph still has exactly one Output.
    MaterialGraph ScopedFresh;
    FBY_CHECK(ScopedFresh.OutputNode() == 0);
    FBY_CHECK(ScopedFresh.AddNode(MaterialNodeKind::Output) == kInvalidMaterialNode);
}

#ifdef FBY_TEST_HAVE_SHADERC
void TestSpirvCompilation()
{
    MaterialGraphCompiler ScopedCompiler;
    char ScopedFragment[kMaterialGlslFragmentBytes] = {};
    char ScopedVertex[kMaterialGlslVertexBytes] = {};
    MaterialShaderProgram ScopedProgram;
    std::string ScopedError;

    // Fragment-only material, exactly the path MaterialGraphRuntime uses.
    MaterialGraph ScopedRich = BuildRichMaterialGraph();
    FBY_CHECK(ScopedCompiler.CompileFragment(ScopedRich, std::span<char>(ScopedFragment, sizeof(ScopedFragment)),
                                             &ScopedProgram, &ScopedError));
    std::vector<std::uint32_t> ScopedFragmentSpirv;
    FBY_CHECK(Fby::Shaderc::CompileGlslToSpirv(std::string_view(ScopedFragment, ScopedProgram.fragmentLength),
                                               Fby::Shaderc::ShaderStage::Fragment, &ScopedFragmentSpirv,
                                               &ScopedError));
    if (!ScopedError.empty())
    {
        std::fprintf(stderr, "unexpected glslang error: %s\n", ScopedError.c_str());
    }
    FBY_CHECK(!ScopedFragmentSpirv.empty());
    FBY_CHECK(ScopedFragmentSpirv.size() >= Fby::Shaderc::kSpirvMinimumWords);
    FBY_CHECK(ScopedFragmentSpirv[0] == 0x07230203u);
    FBY_CHECK(Fby::Shaderc::IsSpirvModule(ScopedFragmentSpirv.data(), ScopedFragmentSpirv.size()));

    // Generated vertex + fragment pair.
    MaterialGraph ScopedVertexGraph = BuildVertexPositionGraph();
    MaterialCompileOptions ScopedOptions;
    ScopedOptions.generateVertexStage = true;
    ScopedError.clear();
    FBY_CHECK(
        ScopedCompiler.Compile(ScopedVertexGraph, ScopedOptions, std::span<char>(ScopedVertex, sizeof(ScopedVertex)),
                               std::span<char>(ScopedFragment, sizeof(ScopedFragment)), &ScopedProgram, &ScopedError));
    std::vector<std::uint32_t> ScopedVertexSpirv;
    FBY_CHECK(Fby::Shaderc::CompileGlslToSpirv(std::string_view(ScopedVertex, ScopedProgram.vertexLength),
                                               Fby::Shaderc::ShaderStage::Vertex, &ScopedVertexSpirv, &ScopedError));
    FBY_CHECK(!ScopedVertexSpirv.empty());
    FBY_CHECK(ScopedVertexSpirv[0] == 0x07230203u);

    // The compiler must reject nonsense instead of returning an empty module.
    std::vector<std::uint32_t> ScopedBroken;
    ScopedError.clear();
    FBY_CHECK(!Fby::Shaderc::CompileGlslToSpirv("this is not glsl", Fby::Shaderc::ShaderStage::Fragment, &ScopedBroken,
                                                &ScopedError));
    FBY_CHECK(!ScopedError.empty());
    FBY_CHECK(ScopedBroken.empty());
}
#endif
}

void TestSpriteDomain()
{
    // A sprite graph replaces the sampling block of quad.frag: the item's atlas
    // slice comes from vLayer and there is no push-constant block at all.
    MaterialGraph ScopedGraph;
    const std::uint16_t ScopedUv = ScopedGraph.AddInputNode(MaterialNodeKind::Uv);
    const std::uint16_t ScopedSample = ScopedGraph.AddTextureSampleNode(0);
    const std::uint16_t ScopedTint = ScopedGraph.AddColorNode(1.0f, 0.5f, 0.25f, 1.0f);
    const std::uint16_t ScopedMultiply = ScopedGraph.AddBinaryNode(MaterialNodeKind::Mul);
    FBY_CHECK(ScopedGraph.Connect(ScopedSample, 0, ScopedUv));
    FBY_CHECK(ScopedGraph.Connect(ScopedMultiply, 0, ScopedSample));
    FBY_CHECK(ScopedGraph.Connect(ScopedMultiply, 1, ScopedTint));
    FBY_CHECK(ScopedGraph.Connect(ScopedGraph.OutputNode(), 0, ScopedMultiply));

    char ScopedFragment[kMaterialGlslFragmentBytes] = {};
    MaterialShaderProgram ScopedProgram;
    MaterialCompileOptions ScopedOptions;
    ScopedOptions.domain = MaterialDomain::Sprite;
    const MaterialGraphCompiler ScopedCompiler;
    std::string ScopedError;
    FBY_CHECK(ScopedCompiler.Compile(ScopedGraph, ScopedOptions, {},
                                     std::span<char>(ScopedFragment, sizeof(ScopedFragment)), &ScopedProgram,
                                     &ScopedError));
    const std::string_view ScopedText(ScopedFragment, ScopedProgram.fragmentLength);
    FBY_CHECK(Contains(ScopedText, "layout(location = 2) flat in uint vLayer;"));
    FBY_CHECK(Contains(ScopedText, "float(vLayer)"));
    FBY_CHECK(!Contains(ScopedText, "push_constant"));
    FBY_CHECK(!Contains(ScopedText, "pc."));
    FBY_CHECK(!ScopedProgram.fragmentReadsMaterialParams);
    FBY_CHECK(!ScopedProgram.fragmentReadsViewProjection);
    FBY_CHECK(!ScopedProgram.usesGeneratedVertexStage);
    const std::uint16_t ScopedContract = MaterialContractFromProgram(ScopedProgram, MaterialDomain::Sprite);
    FBY_CHECK(MaterialContractIsSprite(ScopedContract));
    FBY_CHECK(!MaterialContractIsSprite(MaterialContractFromProgram(ScopedProgram)));

#ifdef FBY_TEST_HAVE_SHADERC
    std::vector<std::uint32_t> ScopedSpirv;
    FBY_CHECK(Fby::Shaderc::CompileGlslToSpirv(ScopedText, Fby::Shaderc::ShaderStage::Fragment, &ScopedSpirv,
                                               &ScopedError));
#endif

    // World position comes from the quad varying; no inverse matrix is emitted.
    MaterialGraph ScopedWorld;
    const std::uint16_t ScopedWorldNode = ScopedWorld.AddInputNode(MaterialNodeKind::WorldPosition);
    const std::uint16_t ScopedWorldTint = ScopedWorld.AddScalarNode(1.0f);
    const std::uint16_t ScopedWorldAdd = ScopedWorld.AddBinaryNode(MaterialNodeKind::Add);
    FBY_CHECK(ScopedWorld.Connect(ScopedWorldAdd, 0, ScopedWorldNode));
    FBY_CHECK(ScopedWorld.Connect(ScopedWorldAdd, 1, ScopedWorldTint));
    FBY_CHECK(ScopedWorld.Connect(ScopedWorld.OutputNode(), 0, ScopedWorldAdd));
    char ScopedWorldFragment[kMaterialGlslFragmentBytes] = {};
    MaterialShaderProgram ScopedWorldProgram;
    FBY_CHECK(ScopedCompiler.Compile(ScopedWorld, ScopedOptions, {},
                                     std::span<char>(ScopedWorldFragment, sizeof(ScopedWorldFragment)),
                                     &ScopedWorldProgram, &ScopedError));
    const std::string_view ScopedWorldText(ScopedWorldFragment, ScopedWorldProgram.fragmentLength);
    FBY_CHECK(Contains(ScopedWorldText, "layout(location = 3) in vec2 vWorld;"));
    FBY_CHECK(Contains(ScopedWorldText, "vec3(vWorld, 0.0)"));
    FBY_CHECK(!Contains(ScopedWorldText, "fbyInverseMat4"));
    FBY_CHECK(!Contains(ScopedWorldText, "push_constant"));

    // Mesh-only nodes are refused with a sprite-specific reason, never silently
    // compiled into a shader the sprite draw cannot bind.
    const auto ScopedRefused = [&ScopedCompiler, &ScopedOptions](const MaterialGraph& InGraph,
                                                                 const char* InExpected)
    {
        char ScopedBuffer[kMaterialGlslFragmentBytes] = {};
        MaterialShaderProgram ScopedRefusedProgram;
        MaterialCompileOptions ScopedRefusedOptions = ScopedOptions;
        std::string ScopedRefusedError;
        const bool ScopedCompiled = ScopedCompiler.Compile(
            InGraph, ScopedRefusedOptions, {}, std::span<char>(ScopedBuffer, sizeof(ScopedBuffer)),
            &ScopedRefusedProgram, &ScopedRefusedError);
        FBY_CHECK(!ScopedCompiled);
        FBY_CHECK(Contains(ScopedRefusedError, InExpected));
    };

    // Time is available in the sprite domain: the quad push block gained a
    // material clock at offset 80, and the generated stage reads it.
    MaterialGraph ScopedTime;
    const std::uint16_t ScopedClock = ScopedTime.AddInputNode(MaterialNodeKind::Time);
    const std::uint16_t ScopedClockColor = ScopedTime.AddColorNode(1.0f, 0.0f, 0.0f, 1.0f);
    FBY_CHECK(ScopedTime.Connect(ScopedTime.OutputNode(), 0, ScopedClockColor));
    FBY_CHECK(ScopedTime.Connect(ScopedTime.OutputNode(), 1, ScopedClock));
    char ScopedClockFragment[kMaterialGlslFragmentBytes] = {};
    MaterialShaderProgram ScopedClockProgram;
    FBY_CHECK(ScopedCompiler.Compile(ScopedTime, ScopedOptions, {},
                                     std::span<char>(ScopedClockFragment, sizeof(ScopedClockFragment)),
                                     &ScopedClockProgram, &ScopedError));
    const std::string_view ScopedClockText(ScopedClockFragment, ScopedClockProgram.fragmentLength);
    FBY_CHECK(Contains(ScopedClockText, "layout(push_constant) uniform Camera"));
    FBY_CHECK(Contains(ScopedClockText, "vec4 materialParams;"));
    FBY_CHECK(Contains(ScopedClockText, "const float fbyTime = pc.materialParams.x;"));
    FBY_CHECK(ScopedClockProgram.fragmentReadsMaterialParams);
    FBY_CHECK(!ScopedClockProgram.fragmentReadsViewProjection);
    FBY_CHECK((MaterialContractFromProgram(ScopedClockProgram, MaterialDomain::Sprite) &
               MaterialContract::MaterialContractFragmentMaterialParams) != 0);
#ifdef FBY_TEST_HAVE_SHADERC
    std::vector<std::uint32_t> ScopedClockSpirv;
    FBY_CHECK(Fby::Shaderc::CompileGlslToSpirv(ScopedClockText, Fby::Shaderc::ShaderStage::Fragment,
                                               &ScopedClockSpirv, &ScopedError));
#endif

    MaterialGraph ScopedFresnel;
    const std::uint16_t ScopedRim = ScopedFresnel.AddFresnelNode();
    const std::uint16_t ScopedRimColor = ScopedFresnel.AddColorNode(1.0f, 0.0f, 0.0f, 1.0f);
    FBY_CHECK(ScopedFresnel.Connect(ScopedFresnel.OutputNode(), 0, ScopedRimColor));
    FBY_CHECK(ScopedFresnel.Connect(ScopedFresnel.OutputNode(), 1, ScopedRim));
    ScopedRefused(ScopedFresnel, "Fresnel");

    MaterialGraph ScopedVertex = BuildVertexPositionGraph();
    ScopedRefused(ScopedVertex, "VertexPosition");

    // The sprite domain always uses the engine quad vertex stage.
    MaterialCompileOptions ScopedVertexOptions = ScopedOptions;
    ScopedVertexOptions.generateVertexStage = true;
    char ScopedVertexBuffer[kMaterialGlslVertexBytes] = {};
    char ScopedVertexFragment[kMaterialGlslFragmentBytes] = {};
    MaterialShaderProgram ScopedVertexProgram;
    std::string ScopedVertexError;
    FBY_CHECK(!ScopedCompiler.Compile(ScopedGraph, ScopedVertexOptions,
                                      std::span<char>(ScopedVertexBuffer, sizeof(ScopedVertexBuffer)),
                                      std::span<char>(ScopedVertexFragment, sizeof(ScopedVertexFragment)),
                                      &ScopedVertexProgram, &ScopedVertexError));
    FBY_CHECK(Contains(ScopedVertexError, "sprite domain"));
}

int main()
{
    static_assert(std::is_trivially_copyable_v<MaterialGraph>);
    static_assert(std::is_trivially_copyable_v<MaterialNode>);
    static_assert(std::is_trivially_copyable_v<MaterialPin>);
    static_assert(std::is_trivially_copyable_v<MaterialParam>);

    TestDefaultLayout();
    TestValidGraph();
    TestStableHash();
    TestDanglingPinRejected();
    TestExactlyOneOutput();
    TestTypeMismatchesRejected();
    TestCycleDetection();
    TestFragmentSnapshot();
    TestAtlasDefaultUv();
    TestVertexStage();
    TestSpriteDomain();
    TestWorldPositionVaryingVersusFallback();
    TestBufferOverflowRejected();
    TestCapacityLimits();
#ifdef FBY_TEST_HAVE_SHADERC
    TestSpirvCompilation();
#endif

    if (GFailures == 0)
    {
        std::printf("material_graph_test: %d checks passed\n", GChecks);
        return 0;
    }
    std::fprintf(stderr, "material_graph_test: %d of %d checks failed\n", GFailures, GChecks);
    return 1;
}
