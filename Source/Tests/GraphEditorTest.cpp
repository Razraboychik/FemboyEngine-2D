// FemboyEngine2D - Step 2 graph editor tests.
#include "Editor/Infrastructure/CommandStack.hpp"
#include "Editor/Panels/MaterialGraphPanel.hpp"
#include "Editor/Panels/VisualScriptPanel.hpp"
#include "Engine/ECS/Components.hpp"
#include "Engine/RHI/MaterialGraph.hpp"
#include "Engine/RHI/MaterialGraphCompiler.hpp"
#include "Engine/RHI/RHI.hpp"
#include "Engine/Script/AotBuilder.hpp"
#include "Engine/Script/AotGraphCompiler.hpp"
#include "Engine/Script/AotModule.hpp"
#include "Engine/Script/GraphCompiler.hpp"
#include "Engine/Script/ScriptSystem.hpp"

#ifdef FBY_TEST_HAVE_SHADERC
#include "Tools/ShaderCompiler/ShaderCompiler.hpp"
#endif

#include <array>
#include <bit>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
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

// Material graph: the new 2.5D and distortion nodes must validate, emit GLSL
Fby::Render::MaterialGraph BuildTiltDistortionGraph()
{
    using namespace Fby::Render;
    MaterialGraph ScopedGraph;
    const std::uint16_t ScopedUv = ScopedGraph.AddInputNode(MaterialNodeKind::Uv, {20.0f, 20.0f});
    const std::uint16_t ScopedTilt = ScopedGraph.AddTilt25DNode(Fby::Vector2D{0.08f, 0.03f}, 1.5f, {220.0f, 20.0f});
    const std::uint16_t ScopedSampleA = ScopedGraph.AddTextureSampleNode(2, {440.0f, 20.0f});
    const std::uint16_t ScopedDistortion = ScopedGraph.AddDistortionNode(0.02f, 6.0f, {220.0f, 160.0f});
    const std::uint16_t ScopedSampleB = ScopedGraph.AddTextureSampleNode(3, {440.0f, 160.0f});
    const std::uint16_t ScopedAdd = ScopedGraph.AddBinaryNode(MaterialNodeKind::Add, {660.0f, 90.0f});

    FBY_CHECK(ScopedGraph.Connect(ScopedTilt, 0, ScopedUv));
    FBY_CHECK(ScopedGraph.Connect(ScopedSampleA, 0, ScopedTilt));
    FBY_CHECK(ScopedGraph.Connect(ScopedDistortion, 0, ScopedUv));
    FBY_CHECK(ScopedGraph.Connect(ScopedSampleB, 0, ScopedDistortion));
    FBY_CHECK(ScopedGraph.Connect(ScopedAdd, 0, ScopedSampleA));
    FBY_CHECK(ScopedGraph.Connect(ScopedAdd, 1, ScopedSampleB));
    FBY_CHECK(ScopedGraph.Connect(ScopedGraph.OutputNode(), 0, ScopedAdd));
    return ScopedGraph;
}

void TestMaterialNodes()
{
    using namespace Fby::Render;
    MaterialGraph ScopedGraph = BuildTiltDistortionGraph();
    std::string ScopedError;
    FBY_CHECK(ScopedGraph.Validate(&ScopedError));
    if (!ScopedError.empty())
    {
        std::fprintf(stderr, "material validation: %s\n", ScopedError.c_str());
    }
    const MaterialGraphRequirements ScopedRequirements = ScopedGraph.Requirements();
    FBY_CHECK(ScopedRequirements.needsTime);

    char ScopedFragment[kMaterialGlslFragmentBytes] = {};
    MaterialShaderProgram ScopedProgram;
    const MaterialGraphCompiler ScopedCompiler;
    FBY_CHECK(ScopedCompiler.CompileFragment(ScopedGraph, std::span<char>(ScopedFragment, sizeof(ScopedFragment)),
                                             &ScopedProgram, &ScopedError));
    const std::string_view ScopedText(ScopedFragment, ScopedProgram.fragmentLength);
    FBY_CHECK(Contains(ScopedText, "Tilt25D"));
    FBY_CHECK(Contains(ScopedText, "Distortion"));
    FBY_CHECK(Contains(ScopedText, "fbyTime"));
    FBY_CHECK(Contains(ScopedText, "sin("));
    FBY_CHECK(Contains(ScopedText, "cos("));
    FBY_CHECK(Contains(ScopedText, "vec2("));

#ifdef FBY_TEST_HAVE_SHADERC
    std::vector<std::uint32_t> ScopedSpirv;
    FBY_CHECK(Fby::Shaderc::CompileGlslToSpirv(std::string_view(ScopedFragment, ScopedProgram.fragmentLength),
                                               Fby::Shaderc::ShaderStage::Fragment, &ScopedSpirv, &ScopedError));
    FBY_CHECK(!ScopedSpirv.empty());
    FBY_CHECK(ScopedSpirv[0] == 0x07230203u);
#endif

    // A constant edit is semantic; a position edit is not.
    const std::uint64_t ScopedHash = ScopedGraph.Hash();
    ScopedGraph.Node(1).uiPosition = Fby::Vector2D{1.0f, 2.0f};
    FBY_CHECK(ScopedGraph.Hash() == ScopedHash);
    ScopedGraph.Param(ScopedGraph.Node(1).paramIndex).Z = 2.0f;
    FBY_CHECK(ScopedGraph.Hash() != ScopedHash);
}

// Logic graph: GetField/SetField through the VM and a field-aware AOT source.
struct LogicFixture
{
    Fby::Script::LogicGraph graph{};
    std::uint16_t positionField = 0xFFFFu;
    std::uint16_t rotationField = 0xFFFFu;

    LogicFixture()
    {
        positionField = graph.AddFieldBinding(Fby::Script::MakeLogicFieldBinding<Fby::Transform2D>("Position"));
        rotationField = graph.AddFieldBinding(Fby::Script::MakeLogicFieldBinding<Fby::Transform2D>("Rotation"));
    }

    [[nodiscard]] std::uint16_t AddPositionSet()
    {
        using namespace Fby::Script;
        const std::uint16_t ScopedEvent = graph.AddNode(LogicNodeKind::EventUpdate);
        const std::uint16_t ScopedGet = graph.AddFieldNode(LogicNodeKind::GetField, positionField);
        const std::uint16_t ScopedSet = graph.AddFieldNode(LogicNodeKind::SetField, positionField);
        (void)graph.AddLink(ScopedEvent, 0, ScopedSet, 0);
        (void)graph.AddLink(ScopedGet, 0, ScopedSet, 1);
        return ScopedSet;
    }
};

void TestLogicFieldNodes()
{
    using namespace Fby::Script;
    LogicFixture ScopedFixture;
    const std::uint16_t ScopedEvent = ScopedFixture.graph.AddNode(LogicNodeKind::EventUpdate);
    const std::uint16_t ScopedGetRotation =
        ScopedFixture.graph.AddFieldNode(LogicNodeKind::GetField, ScopedFixture.rotationField);
    const std::uint16_t ScopedOne =
        ScopedFixture.graph.AddNode(LogicNodeKind::ConstFloat, std::bit_cast<std::uint32_t>(1.0f));
    const std::uint16_t ScopedMath =
        ScopedFixture.graph.AddNode(LogicNodeKind::Math, static_cast<std::uint32_t>(LogicMathOp::Add));
    const std::uint16_t ScopedSetRotation =
        ScopedFixture.graph.AddFieldNode(LogicNodeKind::SetField, ScopedFixture.rotationField);
    const std::uint16_t ScopedGetPosition =
        ScopedFixture.graph.AddFieldNode(LogicNodeKind::GetField, ScopedFixture.positionField);
    const std::uint16_t ScopedSetPosition =
        ScopedFixture.graph.AddFieldNode(LogicNodeKind::SetField, ScopedFixture.positionField);

    FBY_CHECK(ScopedFixture.graph.AddLink(ScopedEvent, 0, ScopedSetRotation, 0));
    FBY_CHECK(ScopedFixture.graph.AddLink(ScopedGetRotation, 0, ScopedMath, 0));
    FBY_CHECK(ScopedFixture.graph.AddLink(ScopedOne, 0, ScopedMath, 1));
    FBY_CHECK(ScopedFixture.graph.AddLink(ScopedMath, 0, ScopedSetRotation, 1));
    FBY_CHECK(ScopedFixture.graph.AddLink(ScopedSetRotation, 0, ScopedSetPosition, 0));
    FBY_CHECK(ScopedFixture.graph.AddLink(ScopedGetPosition, 0, ScopedSetPosition, 1));

    LogicCompileResult ScopedCompiled;
    FBY_CHECK(CompileLogicGraph(ScopedFixture.graph, &ScopedCompiled));
    if (!ScopedCompiled.error.empty())
    {
        std::fprintf(stderr, "logic compile: %s\n", ScopedCompiled.error.c_str());
    }
    FBY_CHECK(ScopedCompiled.instructionCount > 0);
    FBY_CHECK(ScopedCompiled.fieldBindings == 2);

    Fby::ScriptRuntime ScopedRuntime;
    FBY_CHECK(ScopedRuntime.Initialize());
    const std::uint16_t ScopedProgram = ScopedRuntime.LoadProgram(ScopedCompiled.program);
    FBY_CHECK(ScopedProgram != Fby::kScriptNoProgram);
    entt::registry ScopedRegistry;
    const Fby::Entity ScopedEntity = ScopedRegistry.create();
    ScopedRegistry.emplace<Fby::Transform2D>(
        ScopedEntity, Fby::Transform2D{Fby::Vector2D{3.0f, 4.0f}, Fby::Vector2D{1.0f, 1.0f}, 0.5f});
    VmContext ScopedContext{ScopedRegistry};
    ScopedContext.rng = &ScopedRuntime.Rng();
    FBY_CHECK(RunScriptOnce(ScopedRuntime, ScopedProgram, ScopedEntity, ScriptEvent::Update, ScopedContext));
    const Fby::Transform2D& ScopedTransform = ScopedRegistry.get<Fby::Transform2D>(ScopedEntity);
    FBY_CHECK(ScopedTransform.Position.X == 3.0f && ScopedTransform.Position.Y == 4.0f);
    FBY_CHECK(ScopedTransform.Rotation == 1.5f);

    std::string ScopedSource;
    std::string ScopedError;
    FBY_CHECK(TranspileLogicGraphToCpp(ScopedFixture.graph, &ScopedSource, nullptr, &ScopedError));
    FBY_CHECK(Contains(ScopedSource, "FBY_AOT_EXPORT"));
    FBY_CHECK(Contains(ScopedSource, "ctx.Component("));
    FBY_CHECK(Contains(ScopedSource, "Store<float>"));
    FBY_CHECK(Contains(ScopedSource, "Load<float>"));
    FBY_CHECK(!Contains(ScopedSource, "switch (r.op)"));
}

void TestLogicConditionAndSpawn()
{
    using namespace Fby::Script;
    LogicFixture ScopedFixture;
    const std::uint16_t ScopedEvent = ScopedFixture.graph.AddNode(LogicNodeKind::EventUpdate);
    const std::uint16_t ScopedZero =
        ScopedFixture.graph.AddNode(LogicNodeKind::ConstFloat, std::bit_cast<std::uint32_t>(0.0f));
    const std::uint16_t ScopedCondition =
        ScopedFixture.graph.AddNode(LogicNodeKind::Condition, static_cast<std::uint32_t>(LogicCompareOp::Greater));
    const std::uint16_t ScopedBranch = ScopedFixture.graph.AddNode(LogicNodeKind::Branch);
    FBY_CHECK(ScopedFixture.graph.AddLink(ScopedEvent, 0, ScopedBranch, 0));
    FBY_CHECK(ScopedFixture.graph.AddLink(ScopedZero, 0, ScopedCondition, 0));
    FBY_CHECK(ScopedFixture.graph.AddLink(ScopedZero, 0, ScopedCondition, 1));
    FBY_CHECK(ScopedFixture.graph.AddLink(ScopedCondition, 0, ScopedBranch, 1));

    LogicCompileResult ScopedCompiled;
    FBY_CHECK(CompileLogicGraph(ScopedFixture.graph, &ScopedCompiled));
    FBY_CHECK(ScopedCompiled.instructionCount > 0);

    LogicGraph ScopedSpawnGraph;
    const std::uint16_t ScopedSpawnEvent = ScopedSpawnGraph.AddNode(LogicNodeKind::EventUpdate);
    const std::uint16_t ScopedSpawn = ScopedSpawnGraph.AddNode(LogicNodeKind::SpawnEntity, 7);
    FBY_CHECK(ScopedSpawnGraph.AddLink(ScopedSpawnEvent, 0, ScopedSpawn, 0));
    LogicCompileResult ScopedSpawnCompiled;
    FBY_CHECK(CompileLogicGraph(ScopedSpawnGraph, &ScopedSpawnCompiled));

    Fby::ScriptRuntime ScopedRuntime;
    FBY_CHECK(ScopedRuntime.Initialize());
    const std::uint16_t ScopedProgram = ScopedRuntime.LoadProgram(ScopedSpawnCompiled.program);
    FBY_CHECK(ScopedProgram != Fby::kScriptNoProgram);
    entt::registry ScopedRegistry;
    const Fby::Entity ScopedEntity = ScopedRegistry.create();
    VmContext ScopedContext{ScopedRegistry};
    ScopedContext.rng = &ScopedRuntime.Rng();
    static int ScopedSpawnCalls = 0;
    static std::uint32_t ScopedSpawnPrefab = 0;
    ScopedContext.user = &ScopedSpawnCalls;
    ScopedContext.spawn = [](VmContext& InVm, std::uint32_t InPrefab) noexcept -> std::uint32_t
    {
        ++(*static_cast<int*>(InVm.user));
        ScopedSpawnPrefab = InPrefab;
        return 42u;
    };
    FBY_CHECK(RunScriptOnce(ScopedRuntime, ScopedProgram, ScopedEntity, ScriptEvent::Update, ScopedContext));
    FBY_CHECK(ScopedSpawnCalls == 1);
    FBY_CHECK(ScopedSpawnPrefab == 7u);
}

// Command stack: a graph position drag becomes one sparse undo step.
void TestSparseGraphUndo()
{
    using namespace Fby::Render;
    Fby::Editor::CommandStack ScopedCommands;
    FBY_CHECK(ScopedCommands.Initialize());
    MaterialGraph ScopedGraph = BuildTiltDistortionGraph();
    const std::uint16_t ScopedCodec = ScopedCommands.RegisterExternalBuffer(
        "TestMaterialGraph", &ScopedGraph, static_cast<std::uint32_t>(sizeof(MaterialGraph)));
    FBY_CHECK(ScopedCodec != 0xFFFFu);

    std::array<std::byte, sizeof(MaterialGraph)> ScopedOldBytes{};
    std::array<std::byte, sizeof(MaterialGraph)> ScopedNewBytes{};
    FBY_CHECK(ScopedCommands.CaptureComponent(entt::null, ScopedCodec, ScopedOldBytes) ==
              Fby::Editor::CommandStack::Result::Ok);
    const Fby::Vector2D ScopedOriginal = ScopedGraph.Node(1).uiPosition;
    ScopedGraph.Node(1).uiPosition = Fby::Vector2D{512.0f, 256.0f};
    FBY_CHECK(ScopedCommands.CaptureComponent(entt::null, ScopedCodec, ScopedNewBytes) ==
              Fby::Editor::CommandStack::Result::Ok);

    FBY_CHECK(ScopedCommands.BeginTransaction("Move material node") == Fby::Editor::CommandStack::Result::Ok);
    FBY_CHECK(ScopedCommands.Record(entt::null, ScopedCodec, ScopedOldBytes, ScopedNewBytes) ==
              Fby::Editor::CommandStack::Result::Ok);
    FBY_CHECK(ScopedCommands.CommitTransaction() == Fby::Editor::CommandStack::Result::Ok);
    FBY_CHECK(ScopedCommands.CanUndo());
    FBY_CHECK(ScopedCommands.GetStats().bytesUsed < sizeof(MaterialGraph) / 4u);

    FBY_CHECK(ScopedCommands.Undo());
    FBY_CHECK(ScopedGraph.Node(1).uiPosition == ScopedOriginal);
    FBY_CHECK(ScopedCommands.Redo());
    const Fby::Vector2D ScopedMoved{512.0f, 256.0f};
    FBY_CHECK(ScopedGraph.Node(1).uiPosition == ScopedMoved);
}

// AOT background service failure path and generated source shape.
void TestAotBuilder()
{
    using namespace Fby::Script;
    LogicFixture ScopedFixture;
    (void)ScopedFixture.AddPositionSet();

    AotBuildOptions ScopedOptions;
    ScopedOptions.outputDirectory = std::filesystem::current_path() / "build" / "graph_editor_aot";
    ScopedOptions.includeDirectory = std::filesystem::path(FBY_ENGINE_INCLUDE_DIR);
    ScopedOptions.compiler = "this_compiler_does_not_exist_fby";
    ScopedOptions.moduleName = "graph_editor_test";

    AotBuilder ScopedBuilder;
    std::string ScopedError;
    FBY_CHECK(ScopedBuilder.Start(ScopedFixture.graph, ScopedOptions, &ScopedError));
    if (!ScopedError.empty())
    {
        std::fprintf(stderr, "AOT start: %s\n", ScopedError.c_str());
    }
    const auto ScopedDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (ScopedBuilder.Busy() && std::chrono::steady_clock::now() < ScopedDeadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    FBY_CHECK(!ScopedBuilder.Busy());
    FBY_CHECK(ScopedBuilder.State() == AotBuildState::Failed);
    const AotBuildResult ScopedResult = ScopedBuilder.Snapshot();
    FBY_CHECK(!ScopedResult.error.empty());
    FBY_CHECK(std::filesystem::exists(ScopedResult.sourcePath));
    FBY_CHECK(ScopedResult.instructions > 0);
}

// Optional end-to-end native plugin test. Set FBY_AOT_TEST_COMPILER to a
void TestAotNativeBuildIfRequested()
{
    const char* ScopedCompiler = std::getenv("FBY_AOT_TEST_COMPILER");
    if (ScopedCompiler == nullptr || ScopedCompiler[0] == '\0')
    {
        return;
    }
    using namespace Fby::Script;
    LogicFixture ScopedFixture;
    (void)ScopedFixture.AddPositionSet();

    AotBuildOptions ScopedOptions;
    ScopedOptions.outputDirectory = std::filesystem::current_path() / "build" / "graph_editor_aot_native";
    ScopedOptions.includeDirectory = std::filesystem::path(FBY_ENGINE_INCLUDE_DIR);
    ScopedOptions.compiler = ScopedCompiler;
    ScopedOptions.moduleName = "graph_editor_native";
    AotBuilder ScopedBuilder;
    std::string ScopedError;
    FBY_CHECK(ScopedBuilder.Start(ScopedFixture.graph, ScopedOptions, &ScopedError));
    const auto ScopedDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    while (ScopedBuilder.Busy() && std::chrono::steady_clock::now() < ScopedDeadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    FBY_CHECK(!ScopedBuilder.Busy());
    const AotBuildResult ScopedResult = ScopedBuilder.Snapshot();
    FBY_CHECK(ScopedResult.state == AotBuildState::Succeeded);
    if (ScopedResult.state != AotBuildState::Succeeded)
    {
        std::fprintf(stderr, "native AOT: %s\n", ScopedResult.error.c_str());
        return;
    }

    Fby::ScriptRuntime ScopedRuntime;
    FBY_CHECK(ScopedRuntime.Initialize());
    AotModule ScopedModule;
    FBY_CHECK(ScopedModule.Load(ScopedResult.libraryPath.string().c_str(), ScopedRuntime.Bridge(), &ScopedError));
    if (!ScopedModule.Loaded())
    {
        std::fprintf(stderr, "AOT module load: %s\n", ScopedError.c_str());
        return;
    }
    entt::registry ScopedRegistry;
    const Fby::Entity ScopedEntity = ScopedRegistry.create();
    ScopedRegistry.emplace<Fby::Transform2D>(
        ScopedEntity, Fby::Transform2D{Fby::Vector2D{9.0f, 7.0f}, Fby::Vector2D{1.0f, 1.0f}, 0.0f});
    VmContext ScopedContext{ScopedRegistry};
    FBY_CHECK(ScopedModule.Run(ScriptEvent::Update, ScopedEntity, ScopedContext, &ScopedError));
    const Fby::Transform2D& ScopedTransform = ScopedRegistry.get<Fby::Transform2D>(ScopedEntity);
    FBY_CHECK(ScopedTransform.Position.X == 9.0f && ScopedTransform.Position.Y == 7.0f);
    ScopedModule.Unload();
}

// Panels initialize and drive the pipeline/item path without a window.
void TestPanels()
{
    Fby::Editor::CommandStack ScopedCommands;
    FBY_CHECK(ScopedCommands.Initialize());
    Fby::Rhi::DeviceDesc ScopedDesc;
    ScopedDesc.backend = Fby::Rhi::Backend::Null;
    std::unique_ptr<Fby::Rhi::RHI> ScopedGpu = Fby::Rhi::RHI::Create(ScopedDesc);
    FBY_CHECK(ScopedGpu != nullptr);

    Fby::Editor::MaterialGraphPanel ScopedMaterial;
    FBY_CHECK(ScopedMaterial.Initialize(ScopedCommands, ScopedGpu.get(), 0x54455354u));
    FBY_CHECK(ScopedMaterial.Graph().Validate());
    Fby::Rhi::MeshRenderItem ScopedItem;
    ScopedMaterial.BindRenderItem(&ScopedItem);
    FBY_CHECK(ScopedMaterial.ApplyShader());
    FBY_CHECK(ScopedItem.materialPipeline == ScopedMaterial.PipelineId());

    // The panel writes the same .fgraph payload the cooker reads, so a saved
    // graph round-trips through a second panel without losing identity.
    const std::filesystem::path ScopedGraphPath =
        std::filesystem::current_path() / "graph_editor_material.fgraph";
    const std::uint64_t ScopedGraphHash = ScopedMaterial.Graph().Hash();
    FBY_CHECK(ScopedMaterial.SaveGraph(ScopedGraphPath.string().c_str()));
    Fby::Editor::MaterialGraphPanel ScopedLoaded;
    FBY_CHECK(ScopedLoaded.Initialize(ScopedCommands, ScopedGpu.get(), 0x54455355u));
    FBY_CHECK(ScopedLoaded.LoadGraph(ScopedGraphPath.string().c_str()));
    FBY_CHECK(ScopedLoaded.Graph().Hash() == ScopedGraphHash);
    FBY_CHECK(ScopedLoaded.Graph().NodeCount() == ScopedMaterial.Graph().NodeCount());

    // The domain switches the shader contract of the live pipeline: the sprite
    // contract keeps the quad vertex stage and compiles without a vertex stage.
    FBY_CHECK(ScopedLoaded.Domain() == Fby::Render::MaterialDomain::Mesh);
    ScopedLoaded.SetDomain(Fby::Render::MaterialDomain::Sprite);
    FBY_CHECK(ScopedLoaded.Domain() == Fby::Render::MaterialDomain::Sprite);
    FBY_CHECK(ScopedLoaded.ApplyShader());
    FBY_CHECK(ScopedLoaded.Pipeline().IsValid() || ScopedGpu->GetBackend() == Fby::Rhi::Backend::Null);
    std::filesystem::remove(ScopedGraphPath);

    Fby::Scene::ComponentRegistry ScopedComponents;
    Fby::Scene::RegisterBuiltInComponents(ScopedComponents);
    Fby::Editor::VisualScriptPanel ScopedLogic;
    FBY_CHECK(ScopedLogic.Initialize(ScopedCommands, &ScopedComponents));
    FBY_CHECK(ScopedLogic.CompileResult().instructionCount > 0);
    FBY_CHECK(ScopedLogic.Graph().nodeCount >= 3);
    ScopedLogic.Shutdown();
    ScopedLoaded.Shutdown(*ScopedGpu);
    ScopedMaterial.Shutdown(*ScopedGpu);
    ScopedGpu->Shutdown();
}
}

int main()
{
    TestMaterialNodes();
    TestLogicFieldNodes();
    TestLogicConditionAndSpawn();
    TestSparseGraphUndo();
    TestAotBuilder();
    TestAotNativeBuildIfRequested();
    TestPanels();

    if (GFailures == 0)
    {
        std::printf("graph_editor_test: %d checks passed\n", GChecks);
        return 0;
    }
    std::fprintf(stderr, "graph_editor_test: %d of %d checks failed\n", GFailures, GChecks);
    return 1;
}
