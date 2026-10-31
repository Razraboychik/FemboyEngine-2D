// FemboyEngine2D - AI State Tree canvas tests.
#include "Editor/Infrastructure/CommandStack.hpp"
#include "Editor/Panels/AIStateTreeCanvasPanel.hpp"
#include "Engine/ECS/Components.hpp"
#include "Engine/ECS/Scene.hpp"
#include "Engine/ECS/StateTree.hpp"

#include "imgui.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace
{

int GFailures = 0;

void Check(bool InCondition, const char* InExpression, const char* InFile, int InLine)
{
    if (!InCondition)
    {
        ++GFailures;
        std::fprintf(stderr, "FAIL %s:%d: %s\n", InFile, InLine, InExpression);
    }
}

#define FBY_CHECK(expression) Check((expression), #expression, __FILE__, __LINE__)

[[nodiscard]] std::uint16_t FindNodeOfKind(const Fby::StateTreeDefinition& InTree, Fby::StateTreeNodeKind InKind,
                                           std::uint8_t InDepth = 0)
{
    for (std::uint16_t ScopedNode = 0; ScopedNode < InTree.NodeCount; ++ScopedNode)
    {
        if (InTree.Nodes[ScopedNode].Kind == InKind && (InDepth == 0 || InTree.Nodes[ScopedNode].Depth == InDepth))
        {
            return ScopedNode;
        }
    }
    return Fby::kStateTreeNone;
}

void TestCompileDefaultTree()
{
    Fby::Editor::CommandStack ScopedCommands;
    FBY_CHECK(ScopedCommands.Initialize());
    Fby::Editor::AIStateTreeCanvasPanel ScopedPanel;
    FBY_CHECK(ScopedPanel.Initialize(ScopedCommands, 77));
    FBY_CHECK(ScopedPanel.Codec() != 0xFFFFu);
    FBY_CHECK(ScopedPanel.Document().Tree.NodeCount >= 5);
    FBY_CHECK(ScopedPanel.Document().Tree.Transitions.size() == Fby::kMaxStateTreeTransitions);

    FBY_CHECK(ScopedPanel.CompileTree());
    FBY_CHECK(ScopedPanel.Compiled());
    const Fby::Editor::AIStateTreeCompiled& ScopedCompiled = ScopedPanel.CompiledImage();
    FBY_CHECK(ScopedCompiled.Tree.TreeId == 77);
    FBY_CHECK(ScopedCompiled.Tree.Root == 0);
    FBY_CHECK(ScopedCompiled.Tree.NodeCount == ScopedPanel.Document().Tree.NodeCount);
    FBY_CHECK(ScopedCompiled.Tree.TransitionCount == ScopedPanel.Document().Tree.TransitionCount);
    FBY_CHECK(ScopedCompiled.LeafCount == 3);
    FBY_CHECK(ScopedCompiled.MaxDepth == 3);
    FBY_CHECK(ScopedCompiled.Tree.Nodes[ScopedCompiled.Tree.Root].Kind == Fby::StateTreeNodeKind::Root);

    // Every compiled node must have a reverse mapping for PIE highlighting.
    for (std::uint16_t ScopedNode = 0; ScopedNode < ScopedCompiled.Tree.NodeCount; ++ScopedNode)
    {
        const std::uint16_t ScopedEditor = ScopedCompiled.EditorIndex[ScopedNode];
        FBY_CHECK(ScopedEditor < ScopedPanel.Document().Tree.NodeCount);
        if (ScopedEditor < ScopedPanel.Document().Tree.NodeCount)
        {
            FBY_CHECK(ScopedPanel.Document().Tree.Nodes[ScopedEditor].Kind ==
                      ScopedCompiled.Tree.Nodes[ScopedNode].Kind);
            FBY_CHECK(ScopedPanel.Document().Tree.Nodes[ScopedEditor].Depth ==
                      ScopedCompiled.Tree.Nodes[ScopedNode].Depth);
        }
    }
}

void TestFieldWiseComparison()
{
    Fby::StateTreeEvaluator ScopedA;
    Fby::StateTreeEvaluator ScopedB;
    FBY_CHECK(Fby::Editor::AIStateTreeEvaluatorFieldsEqual(ScopedA, ScopedB));
    // Reserved is deliberately not in the inspector's field view.
    ScopedB.Reserved = 0x7FFF;
    FBY_CHECK(Fby::Editor::AIStateTreeEvaluatorFieldsEqual(ScopedA, ScopedB));
    ScopedB.Threshold = 12.0f;
    FBY_CHECK(!Fby::Editor::AIStateTreeEvaluatorFieldsEqual(ScopedA, ScopedB));
    ScopedB = ScopedA;
    ScopedB.Kind = Fby::StateTreeEvaluatorKind::TagsAll;
    FBY_CHECK(!Fby::Editor::AIStateTreeEvaluatorFieldsEqual(ScopedA, ScopedB));

    Fby::StateTreeTask ScopedTaskA;
    Fby::StateTreeTask ScopedTaskB;
    FBY_CHECK(Fby::Editor::AIStateTreeTaskFieldsEqual(ScopedTaskA, ScopedTaskB));
    ScopedTaskB.Range = 64.0f;
    FBY_CHECK(!Fby::Editor::AIStateTreeTaskFieldsEqual(ScopedTaskA, ScopedTaskB));

    const Fby::AICommandComponent ScopedMove = Fby::Editor::AIStateTreeCommandFor(Fby::StateTreeTask{}, 3);
    FBY_CHECK(ScopedMove.Kind == static_cast<std::uint32_t>(Fby::AICommandKind::None));
    Fby::StateTreeTask ScopedAttack;
    ScopedAttack.Kind = Fby::StateTreeTaskKind::AttackTarget;
    ScopedAttack.Range = 42.0f;
    const Fby::AICommandComponent ScopedCommand = Fby::Editor::AIStateTreeCommandFor(ScopedAttack, 9);
    FBY_CHECK(ScopedCommand.Kind == static_cast<std::uint32_t>(Fby::AICommandKind::Attack));
    FBY_CHECK(ScopedCommand.Range == 42.0f);
    FBY_CHECK(ScopedCommand.TaskIndex == 9);
}

void TestDepthLimit()
{
    Fby::Editor::CommandStack ScopedCommands;
    FBY_CHECK(ScopedCommands.Initialize());
    Fby::Editor::AIStateTreeCanvasPanel ScopedPanel;
    FBY_CHECK(ScopedPanel.Initialize(ScopedCommands, 1));

    const std::uint16_t ScopedRoot = ScopedPanel.Document().Tree.Root;
    FBY_CHECK(ScopedRoot != Fby::kStateTreeNone);
    const std::uint16_t ScopedSelector =
        ScopedPanel.AddAINode(Fby::StateTreeNodeKind::Selector, ScopedRoot, Fby::Vector2D{0.0f, 100.0f});
    FBY_CHECK(ScopedSelector != Fby::kStateTreeNone);
    const std::uint16_t ScopedDepth3 =
        ScopedPanel.AddAINode(Fby::StateTreeNodeKind::Selector, ScopedSelector, Fby::Vector2D{0.0f, 220.0f});
    FBY_CHECK(ScopedDepth3 != Fby::kStateTreeNone);
    const std::uint16_t ScopedDepth4 =
        ScopedPanel.AddAINode(Fby::StateTreeNodeKind::Leaf, ScopedDepth3, Fby::Vector2D{0.0f, 340.0f});
    FBY_CHECK(ScopedDepth4 != Fby::kStateTreeNone);
    FBY_CHECK(ScopedPanel.Document().Tree.Nodes[ScopedDepth4].Depth == Fby::kMaxStateTreeDepth);
    FBY_CHECK(ScopedPanel.AddAINode(Fby::StateTreeNodeKind::Leaf, ScopedDepth4, Fby::Vector2D{}) ==
              Fby::kStateTreeNone);
    // A selector at depth 4 would be an empty un-compilable group; the panel
    FBY_CHECK(ScopedPanel.AddAINode(Fby::StateTreeNodeKind::Selector, ScopedDepth3, Fby::Vector2D{}) ==
              Fby::kStateTreeNone);
}

void TestCommandStackTransaction()
{
    Fby::Editor::CommandStack ScopedCommands;
    FBY_CHECK(ScopedCommands.Initialize());
    Fby::Editor::AIStateTreeCanvasPanel ScopedPanel;
    FBY_CHECK(ScopedPanel.Initialize(ScopedCommands, 5));
    FBY_CHECK(ScopedPanel.CompileTree());
    const std::uint16_t ScopedBefore = ScopedPanel.Document().Tree.NodeCount;
    const std::uint16_t ScopedRoot = ScopedPanel.Document().Tree.Root;

    FBY_CHECK(ScopedPanel.BeginEdit("Add AI state", Fby::Editor::AIStateTreeCanvasPanel::EditSource::CanvasCommand));
    const std::uint16_t ScopedCreated =
        ScopedPanel.AddAINode(Fby::StateTreeNodeKind::Leaf, ScopedRoot, Fby::Vector2D{500.0f, 500.0f});
    FBY_CHECK(ScopedCreated != Fby::kStateTreeNone);
    ScopedPanel.RecordEdit();
    ScopedPanel.CommitEdit();
    FBY_CHECK(ScopedPanel.Document().Tree.NodeCount == ScopedBefore + 1);
    FBY_CHECK(ScopedPanel.CompileDirty());
    FBY_CHECK(ScopedCommands.CanUndo());
    FBY_CHECK(ScopedCommands.GetStats().bytesUsed < 512);

    FBY_CHECK(ScopedCommands.Undo());
    ScopedPanel.NotifyHistoryChanged();
    FBY_CHECK(ScopedPanel.Document().Tree.NodeCount == ScopedBefore);
    FBY_CHECK(ScopedCommands.Redo());
    ScopedPanel.NotifyHistoryChanged();
    FBY_CHECK(ScopedPanel.Document().Tree.NodeCount == ScopedBefore + 1);
}

void TestDeadLoopCompile()
{
    Fby::Editor::CommandStack ScopedCommands;
    FBY_CHECK(ScopedCommands.Initialize());
    Fby::Editor::AIStateTreeCanvasPanel ScopedPanel;
    FBY_CHECK(ScopedPanel.Initialize(ScopedCommands, 9));
    ScopedPanel.ResetDocument();
    const std::uint16_t ScopedRoot =
        ScopedPanel.AddAINode(Fby::StateTreeNodeKind::Root, Fby::kStateTreeNone, Fby::Vector2D{});
    const std::uint16_t ScopedA =
        ScopedPanel.AddAINode(Fby::StateTreeNodeKind::Leaf, ScopedRoot, Fby::Vector2D{-100.0f, 120.0f});
    const std::uint16_t ScopedB =
        ScopedPanel.AddAINode(Fby::StateTreeNodeKind::Leaf, ScopedRoot, Fby::Vector2D{100.0f, 120.0f});
    FBY_CHECK(ScopedRoot != Fby::kStateTreeNone && ScopedA != Fby::kStateTreeNone && ScopedB != Fby::kStateTreeNone);
    ScopedPanel.Document().Tree.Nodes[ScopedA].Task.Kind = Fby::StateTreeTaskKind::None;
    ScopedPanel.Document().Tree.Nodes[ScopedB].Task.Kind = Fby::StateTreeTaskKind::None;
    FBY_CHECK(ScopedPanel.AddAITransition(ScopedA, ScopedB));
    FBY_CHECK(ScopedPanel.AddAITransition(ScopedB, ScopedA));
    FBY_CHECK(!ScopedPanel.CompileTree());
    FBY_CHECK(std::string_view(ScopedPanel.Status()).find("dead loop") != std::string_view::npos);

    // A closed loop that emits a task is a valid AI behaviour loop.
    ScopedPanel.Document().Tree.Nodes[ScopedB].Task.Kind = Fby::StateTreeTaskKind::AttackTarget;
    FBY_CHECK(ScopedPanel.CompileTree());
}

void TestHierarchyCycleCompile()
{
    Fby::Editor::CommandStack ScopedCommands;
    FBY_CHECK(ScopedCommands.Initialize());
    Fby::Editor::AIStateTreeCanvasPanel ScopedPanel;
    FBY_CHECK(ScopedPanel.Initialize(ScopedCommands, 11));
    const std::uint16_t ScopedRoot = ScopedPanel.Document().Tree.Root;
    FBY_CHECK(ScopedRoot != Fby::kStateTreeNone);
    const std::uint16_t ScopedFirstChild = ScopedPanel.Document().Tree.Nodes[ScopedRoot].FirstChild;
    FBY_CHECK(ScopedFirstChild != Fby::kStateTreeNone);
    // A self-referential sibling list is a hierarchy cycle that still has
    ScopedPanel.Document().Tree.Nodes[ScopedFirstChild].NextSibling = ScopedFirstChild;
    FBY_CHECK(!ScopedPanel.CompileTree());
    FBY_CHECK(std::string_view(ScopedPanel.Status()).find("cycle") != std::string_view::npos);
}

void TestDepthMismatchCompile()
{
    Fby::Editor::CommandStack ScopedCommands;
    FBY_CHECK(ScopedCommands.Initialize());
    Fby::Editor::AIStateTreeCanvasPanel ScopedPanel;
    FBY_CHECK(ScopedPanel.Initialize(ScopedCommands, 10));
    const std::uint16_t ScopedLeaf = FindNodeOfKind(ScopedPanel.Document().Tree, Fby::StateTreeNodeKind::Leaf);
    FBY_CHECK(ScopedLeaf != Fby::kStateTreeNone);
    ScopedPanel.Document().Tree.Nodes[ScopedLeaf].Depth = 5;
    FBY_CHECK(!ScopedPanel.CompileTree());
    FBY_CHECK(std::string_view(ScopedPanel.Status()).find("depth") != std::string_view::npos);
}

[[nodiscard]] std::string ReadText(const std::filesystem::path& InPath)
{
    std::ifstream ScopedFile(InPath, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(ScopedFile)), std::istreambuf_iterator<char>());
}

void TestSceneTomlRoundTrip()
{
    const std::filesystem::path ScopedDirectory = std::filesystem::current_path() / "ai_state_tree_panel_test";
    std::error_code ScopedCode;
    std::filesystem::remove_all(ScopedDirectory, ScopedCode);
    std::filesystem::create_directories(ScopedDirectory, ScopedCode);
    const std::filesystem::path ScopedScene = ScopedDirectory / "scene.toml";
    {
        std::ofstream ScopedFile(ScopedScene, std::ios::binary | std::ios::trunc);
        ScopedFile << "version = 1\nname = \"ai_canvas_test\"\n\n[[entity]]\nid = 0\nname = \"enemy\"\n";
    }

    Fby::Editor::CommandStack ScopedCommands;
    FBY_CHECK(ScopedCommands.Initialize());
    Fby::Editor::AIStateTreeCanvasPanel ScopedPanel;
    FBY_CHECK(ScopedPanel.Initialize(ScopedCommands, 21));
    ScopedPanel.SetScenePath(ScopedScene);
    FBY_CHECK(ScopedPanel.CompileTree());
    const std::uint16_t ScopedNodeCount = ScopedPanel.CompiledImage().Tree.NodeCount;
    const std::uint16_t ScopedTransitionCount = ScopedPanel.CompiledImage().Tree.TransitionCount;
    FBY_CHECK(ScopedPanel.SaveCompiledToSceneToml());
    const std::string ScopedFirstText = ReadText(ScopedScene);
    FBY_CHECK(ScopedFirstText.find("~~FBY AI STATE TREE~~") == std::string::npos);
    FBY_CHECK(ScopedFirstText.find("FBY AI STATE TREE BEGIN") != std::string::npos);
    FBY_CHECK(ScopedFirstText.find("name = \"ai_canvas_test\"") != std::string::npos);
    FBY_CHECK(ScopedFirstText.find("name = \"enemy\"") != std::string::npos);

    // The engine scene reader must still accept the file: the AI block is a
    Fby::Scene::ComponentRegistry ScopedSceneComponents;
    Fby::Scene::RegisterBuiltInComponents(ScopedSceneComponents);
    const auto ScopedDecoded = Fby::Scene::DecodeSceneToml(ScopedFirstText, ScopedSceneComponents);
    FBY_CHECK(ScopedDecoded.has_value());
    FBY_CHECK(ScopedDecoded.has_value() && ScopedDecoded->entities.size() == 1);

    // Saving twice must replace the marked block, never duplicate it.
    FBY_CHECK(ScopedPanel.SaveCompiledToSceneToml());
    const std::string ScopedSecondText = ReadText(ScopedScene);
    std::size_t ScopedMarkerCount = 0;
    for (std::size_t ScopedPos = 0;
         (ScopedPos = ScopedSecondText.find("FBY AI STATE TREE BEGIN", ScopedPos)) != std::string::npos;
         ScopedPos += 10)
    {
        ++ScopedMarkerCount;
    }
    FBY_CHECK(ScopedMarkerCount == 1);

    Fby::Editor::CommandStack ScopedReloadCommands;
    FBY_CHECK(ScopedReloadCommands.Initialize());
    Fby::Editor::AIStateTreeCanvasPanel ScopedReloaded;
    FBY_CHECK(ScopedReloaded.Initialize(ScopedReloadCommands, 21));
    ScopedReloaded.SetScenePath(ScopedScene);
    FBY_CHECK(ScopedReloaded.LoadCompiledFromSceneToml());
    FBY_CHECK(ScopedReloaded.Compiled());
    FBY_CHECK(ScopedReloaded.CompiledImage().Tree.NodeCount == ScopedNodeCount);
    FBY_CHECK(ScopedReloaded.CompiledImage().Tree.TransitionCount == ScopedTransitionCount);
    FBY_CHECK(ScopedReloaded.CompiledImage().Tree.TreeId == 21);
    FBY_CHECK(ScopedReloaded.CompiledImage().Tree.Nodes[0].Kind == Fby::StateTreeNodeKind::Root);
    FBY_CHECK(ReadText(ScopedScene).find("name = \"ai_canvas_test\"") != std::string::npos);

    std::filesystem::remove_all(ScopedDirectory, ScopedCode);
}

void TestPIEActivePath()
{
    Fby::Editor::CommandStack ScopedCommands;
    FBY_CHECK(ScopedCommands.Initialize());
    Fby::Editor::AIStateTreeCanvasPanel ScopedPanel;
    FBY_CHECK(ScopedPanel.Initialize(ScopedCommands, 33));
    FBY_CHECK(ScopedPanel.CompileTree());
    const Fby::Editor::AIStateTreeCompiled& ScopedCompiled = ScopedPanel.CompiledImage();

    std::uint16_t ScopedLeaf = Fby::kStateTreeNone;
    for (std::uint16_t ScopedNode = 0; ScopedNode < ScopedCompiled.Tree.NodeCount; ++ScopedNode)
    {
        if (ScopedCompiled.Tree.Nodes[ScopedNode].Kind == Fby::StateTreeNodeKind::Leaf)
        {
            ScopedLeaf = ScopedNode;
            break;
        }
    }
    FBY_CHECK(ScopedLeaf != Fby::kStateTreeNone);

    std::array<std::uint16_t, Fby::kMaxStateTreeDepth> ScopedPath{Fby::kStateTreeNone, Fby::kStateTreeNone,
                                                                  Fby::kStateTreeNone, Fby::kStateTreeNone};
    std::uint32_t ScopedDepth = 0;
    std::uint16_t ScopedWalk = ScopedLeaf;
    while (ScopedWalk != Fby::kStateTreeNone && ScopedDepth < Fby::kMaxStateTreeDepth)
    {
        ScopedPath[Fby::kMaxStateTreeDepth - 1 - ScopedDepth] = ScopedWalk;
        ++ScopedDepth;
        ScopedWalk = ScopedCompiled.Tree.Nodes[ScopedWalk].Parent;
    }
    FBY_CHECK(ScopedWalk == Fby::kStateTreeNone);
    // Shift the path to index 0 by rotating the fixed array.
    std::array<std::uint16_t, Fby::kMaxStateTreeDepth> ScopedOrdered{};
    std::uint32_t ScopedOut = 0;
    for (std::uint32_t ScopedI = 0; ScopedI < Fby::kMaxStateTreeDepth; ++ScopedI)
    {
        if (ScopedPath[ScopedI] != Fby::kStateTreeNone)
        {
            ScopedOrdered[ScopedOut++] = ScopedPath[ScopedI];
        }
    }

    entt::registry ScopedRegistry;
    const Fby::Entity ScopedEntity = ScopedRegistry.create();
    ScopedRegistry.emplace<Fby::EditorSelected>(ScopedEntity);
    Fby::StateTreeComponent& ScopedState = ScopedRegistry.emplace<Fby::StateTreeComponent>(ScopedEntity);
    ScopedState.TreeId = 33;
    ScopedState.ActiveDepth = ScopedDepth;
    ScopedState.CurrentLeaf = ScopedLeaf;
    ScopedState.ActiveStates = ScopedOrdered;

    ScopedPanel.BindRuntimeWorld(&ScopedRegistry, &ScopedRegistry, true);
    FBY_CHECK(ScopedPanel.RefreshPIEActivePath() == ScopedDepth);
    FBY_CHECK(ScopedPanel.PIEActiveNodeCount() == ScopedDepth);
    FBY_CHECK(ScopedPanel.PIEActiveLeaf() == ScopedCompiled.EditorIndex[ScopedLeaf]);
    for (std::uint32_t ScopedI = 0; ScopedI < ScopedDepth; ++ScopedI)
    {
        FBY_CHECK(ScopedPanel.IsAINodePIEActive(ScopedCompiled.EditorIndex[ScopedOrdered[ScopedI]]));
    }

    // A different TreeId is not the compiled tree: no false highlight.
    ScopedState.TreeId = 34;
    FBY_CHECK(ScopedPanel.RefreshPIEActivePath() == 0);
    ScopedState.TreeId = 33;
    ScopedPanel.BindRuntimeWorld(&ScopedRegistry, &ScopedRegistry, false);
    FBY_CHECK(ScopedPanel.RefreshPIEActivePath() == 0);
}

void TestPanelDrawHeadless()
{
    ImGui::CreateContext();
    ImGuiIO& ScopedIo = ImGui::GetIO();
    ScopedIo.DisplaySize = ImVec2(1280.0f, 720.0f);
    ScopedIo.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
    ScopedIo.DeltaTime = 1.0f / 60.0f;
    ScopedIo.Fonts->AddFontDefault();
    (void)ScopedIo.Fonts->Build();

    Fby::Editor::CommandStack ScopedCommands;
    FBY_CHECK(ScopedCommands.Initialize());
    Fby::Editor::AIStateTreeCanvasPanel ScopedPanel;
    FBY_CHECK(ScopedPanel.Initialize(ScopedCommands, 44));
    bool ScopedOpen = true;
    for (int ScopedFrame = 0; ScopedFrame < 3; ++ScopedFrame)
    {
        ImGui::NewFrame();
        ScopedPanel.Draw("AI State Tree", &ScopedOpen);
        ImGui::Render();
    }
    ScopedPanel.Shutdown();
    ImGui::DestroyContext();
}
}

int main()
{
    TestCompileDefaultTree();
    TestFieldWiseComparison();
    TestDepthLimit();
    TestCommandStackTransaction();
    TestDeadLoopCompile();
    TestHierarchyCycleCompile();
    TestDepthMismatchCompile();
    TestSceneTomlRoundTrip();
    TestPIEActivePath();
    TestPanelDrawHeadless();

    if (GFailures == 0)
    {
        std::printf("AIStateTreeCanvasPanelTest: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "AIStateTreeCanvasPanelTest: %d check(s) failed\n", GFailures);
    return 1;
}
