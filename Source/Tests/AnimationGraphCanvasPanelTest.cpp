// FemboyEngine2D - Animation Graph canvas tests.
#include "Editor/Infrastructure/CommandStack.hpp"
#include "Editor/Panels/AnimationGraphCanvasPanel.hpp"
#include "Engine/Core/Log.hpp"
#include "Engine/ECS/Components.hpp"
#include "Engine/ECS/Scene.hpp"

#include "imgui.h"

#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <thread>

namespace
{

int GFailures = 0;
std::atomic<int> GCycleWarnings{0};

void CycleLogObserver(Fby::LogLevel InLevel, std::string_view InChannel, std::string_view InMessage) noexcept
{
    if (InLevel == Fby::LogLevel::Warn && InChannel == "AnimGraph" &&
        InMessage.find("dead blend cycle") != std::string_view::npos)
    {
        GCycleWarnings.fetch_add(1, std::memory_order_relaxed);
    }
}

void Check(bool InCondition, const char* InExpression, const char* InFile, int InLine)
{
    if (!InCondition)
    {
        ++GFailures;
        std::fprintf(stderr, "FAIL %s:%d: %s\n", InFile, InLine, InExpression);
    }
}

#define FBY_CHECK(expression) Check((expression), #expression, __FILE__, __LINE__)

[[nodiscard]] std::uint16_t FindClipNode(const Fby::Editor::AnimationGraphDocument& InDocument, std::uint64_t InState)
{
    for (std::uint16_t ScopedNode = 0; ScopedNode < InDocument.NodeCount; ++ScopedNode)
    {
        if (InDocument.Nodes[ScopedNode].Kind == Fby::Editor::AnimationGraphNodeKind::Clip &&
            InDocument.Nodes[ScopedNode].Clip.State == InState)
        {
            return ScopedNode;
        }
    }
    return 0xFFFFu;
}

[[nodiscard]] std::uint16_t CountCompiledTransitions(const Fby::AnimStateComponent& InState)
{
    std::uint16_t ScopedCount = 0;
    for (const Fby::AnimTransition& ScopedTransition : InState.Transitions)
    {
        if (ScopedTransition.Conditions != Fby::AnimConditionNone)
        {
            ++ScopedCount;
        }
    }
    return ScopedCount;
}

void TestCompileDefaultGraph()
{
    Fby::Editor::CommandStack ScopedCommands;
    FBY_CHECK(ScopedCommands.Initialize());
    Fby::Editor::AnimationGraphCanvasPanel ScopedPanel;
    FBY_CHECK(ScopedPanel.Initialize(ScopedCommands, 7));
    FBY_CHECK(ScopedPanel.Codec() != 0xFFFFu);
    FBY_CHECK(ScopedPanel.Document().NodeCount == 4);
    FBY_CHECK(ScopedPanel.Document().LinkCount == 4);
    FBY_CHECK(ScopedPanel.CompileGraph());
    FBY_CHECK(ScopedPanel.Compiled());
    const Fby::AnimStateComponent& ScopedCompiled = ScopedPanel.CompiledComponent();
    FBY_CHECK(ScopedCompiled.ClipCount == 2);
    FBY_CHECK(ScopedCompiled.Clips[0].State == 1);
    FBY_CHECK(ScopedCompiled.Clips[1].State == 2);
    FBY_CHECK(ScopedCompiled.Floats[0] == 1.0f);
    FBY_CHECK(CountCompiledTransitions(ScopedCompiled) == 2);
    // The direct Attack -> Idle link is compiled before the Idle -> Attack
    FBY_CHECK(ScopedCompiled.Transitions[0].FromState == 2);
    FBY_CHECK(ScopedCompiled.Transitions[0].ToState == 1);
    FBY_CHECK(ScopedCompiled.Transitions[1].FromState == 1);
    FBY_CHECK(ScopedCompiled.Transitions[1].ToState == 2);
    FBY_CHECK(ScopedCompiled.Transitions[1].FloatParam == 0);
    FBY_CHECK(ScopedCompiled.Transitions[1].BlendDuration == 0.2f);
    // TransitionCount is a runtime diagnostic (fired transitions), so a freshly
    FBY_CHECK(ScopedCompiled.TransitionCount == 0);
    FBY_CHECK(ScopedCompiled.LastTransition == Fby::kNoAnimTransition);
}

void TestFieldComparison()
{
    Fby::AnimTransition ScopedA;
    Fby::AnimTransition ScopedB;
    FBY_CHECK(Fby::Editor::AnimationGraphTransitionFieldsEqual(ScopedA, ScopedB));
    ScopedB.BlendDuration = 0.5f;
    FBY_CHECK(!Fby::Editor::AnimationGraphTransitionFieldsEqual(ScopedA, ScopedB));
    ScopedB = ScopedA;
    ScopedB.Conditions = Fby::AnimConditionFloatAtMost;
    ScopedB.FloatThreshold = 3.0f;
    FBY_CHECK(!Fby::Editor::AnimationGraphTransitionFieldsEqual(ScopedA, ScopedB));
    ScopedB = ScopedA;
    ScopedB.Enabled = false;
    FBY_CHECK(!Fby::Editor::AnimationGraphTransitionFieldsEqual(ScopedA, ScopedB));

    Fby::AnimClip ScopedClipA;
    Fby::AnimClip ScopedClipB;
    FBY_CHECK(Fby::Editor::AnimationGraphClipFieldsEqual(ScopedClipA, ScopedClipB));
    ScopedClipB.SpriteStartFrame = 5;
    FBY_CHECK(!Fby::Editor::AnimationGraphClipFieldsEqual(ScopedClipA, ScopedClipB));
    ScopedClipB = ScopedClipA;
    ScopedClipB.KeyCount = 1;
    FBY_CHECK(!Fby::Editor::AnimationGraphClipFieldsEqual(ScopedClipA, ScopedClipB));
}

void TestCommandStackUndo()
{
    Fby::Editor::CommandStack ScopedCommands;
    FBY_CHECK(ScopedCommands.Initialize());
    Fby::Editor::AnimationGraphCanvasPanel ScopedPanel;
    FBY_CHECK(ScopedPanel.Initialize(ScopedCommands, 8));
    FBY_CHECK(ScopedPanel.CompileGraph());
    const std::uint16_t ScopedBefore = ScopedPanel.Document().NodeCount;

    FBY_CHECK(
        ScopedPanel.BeginEdit("Add animation clip", Fby::Editor::AnimationGraphCanvasPanel::EditSource::CanvasCommand));
    const std::uint16_t ScopedCreated =
        ScopedPanel.AddNode(Fby::Editor::AnimationGraphNodeKind::Clip, Fby::Vector2D{500.0f, 500.0f});
    FBY_CHECK(ScopedCreated != 0xFFFFu);
    ScopedPanel.RecordEdit();
    ScopedPanel.CommitEdit();
    FBY_CHECK(ScopedPanel.Document().NodeCount == ScopedBefore + 1);
    FBY_CHECK(ScopedPanel.CompileDirty());
    FBY_CHECK(ScopedCommands.CanUndo());
    FBY_CHECK(ScopedCommands.GetStats().bytesUsed < 1024);

    FBY_CHECK(ScopedCommands.Undo());
    ScopedPanel.NotifyHistoryChanged();
    FBY_CHECK(ScopedPanel.Document().NodeCount == ScopedBefore);
    FBY_CHECK(ScopedCommands.Redo());
    ScopedPanel.NotifyHistoryChanged();
    FBY_CHECK(ScopedPanel.Document().NodeCount == ScopedBefore + 1);
}

void TestCompileValidation()
{
    Fby::Editor::CommandStack ScopedCommands;
    FBY_CHECK(ScopedCommands.Initialize());
    Fby::Editor::AnimationGraphCanvasPanel ScopedPanel;
    FBY_CHECK(ScopedPanel.Initialize(ScopedCommands, 9));

    // An added clip is isolated until it is linked.
    const std::uint16_t ScopedIsolated =
        ScopedPanel.AddNode(Fby::Editor::AnimationGraphNodeKind::Clip, Fby::Vector2D{});
    FBY_CHECK(ScopedIsolated != 0xFFFFu);
    FBY_CHECK(!ScopedPanel.CompileGraph());
    FBY_CHECK(std::string_view(ScopedPanel.Status()).find("isolated") != std::string_view::npos);

    // Restore the demo and break the first transition's condition set.
    ScopedPanel.BuildDefaultDocument();
    FBY_CHECK(ScopedPanel.CompileGraph());
    Fby::Editor::AnimationGraphLink* ScopedDirect = nullptr;
    for (std::uint16_t ScopedLink = 0; ScopedLink < ScopedPanel.Document().LinkCount; ++ScopedLink)
    {
        if (ScopedPanel.Document().Links[ScopedLink].Kind == Fby::Editor::AnimationGraphLinkKind::Transition)
        {
            ScopedDirect = &ScopedPanel.Document().Links[ScopedLink];
            break;
        }
    }
    FBY_CHECK(ScopedDirect != nullptr);
    if (ScopedDirect != nullptr)
    {
        ScopedDirect->Transition.Conditions = Fby::AnimConditionNone;
        FBY_CHECK(!ScopedPanel.CompileGraph());
        FBY_CHECK(std::string_view(ScopedPanel.Status()).find("no condition") != std::string_view::npos);
    }

    // Duplicate clip state ids are rejected.
    ScopedPanel.BuildDefaultDocument();
    const std::uint16_t ScopedExtra = ScopedPanel.AddNode(Fby::Editor::AnimationGraphNodeKind::Clip, Fby::Vector2D{});
    FBY_CHECK(ScopedExtra != 0xFFFFu);
    ScopedPanel.Document().Nodes[ScopedExtra].Clip.State = 1;
    FBY_CHECK(!ScopedPanel.CompileGraph());
    FBY_CHECK(std::string_view(ScopedPanel.Status()).find("duplicate state id") != std::string_view::npos);

    // A blend node without its target link is not a valid cross-fade.
    ScopedPanel.BuildDefaultDocument();
    std::uint16_t ScopedBlendTarget = 0xFFFFu;
    for (std::uint16_t ScopedLink = 0; ScopedLink < ScopedPanel.Document().LinkCount; ++ScopedLink)
    {
        if (ScopedPanel.Document().Links[ScopedLink].Kind == Fby::Editor::AnimationGraphLinkKind::BlendTarget)
        {
            ScopedBlendTarget = ScopedLink;
            break;
        }
    }
    FBY_CHECK(ScopedBlendTarget != 0xFFFFu);
    if (ScopedBlendTarget != 0xFFFFu)
    {
        FBY_CHECK(ScopedPanel.RemoveLink(ScopedBlendTarget));
        FBY_CHECK(!ScopedPanel.CompileGraph());
        FBY_CHECK(std::string_view(ScopedPanel.Status()).find("exactly one source") != std::string_view::npos);
    }
}

[[nodiscard]] std::string ReadText(const std::filesystem::path& InPath)
{
    std::ifstream ScopedFile(InPath, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(ScopedFile)), std::istreambuf_iterator<char>());
}

void TestSceneTomlRoundTrip()
{
    const std::filesystem::path ScopedDirectory = std::filesystem::current_path() / "anim_graph_panel_test";
    std::error_code ScopedCode;
    std::filesystem::remove_all(ScopedDirectory, ScopedCode);
    std::filesystem::create_directories(ScopedDirectory, ScopedCode);
    const std::filesystem::path ScopedScene = ScopedDirectory / "scene.toml";
    {
        std::ofstream ScopedFile(ScopedScene, std::ios::binary | std::ios::trunc);
        ScopedFile << "version = 1\nname = \"anim_canvas_test\"\n\n[[entity]]\nid = 0\nname = \"hero\"\n";
    }

    Fby::Editor::CommandStack ScopedCommands;
    FBY_CHECK(ScopedCommands.Initialize());
    Fby::Editor::AnimationGraphCanvasPanel ScopedPanel;
    FBY_CHECK(ScopedPanel.Initialize(ScopedCommands, 12));
    ScopedPanel.SetScenePath(ScopedScene);
    FBY_CHECK(ScopedPanel.CompileGraph());
    FBY_CHECK(ScopedPanel.SaveToSceneToml());
    const std::string ScopedFirstText = ReadText(ScopedScene);
    FBY_CHECK(ScopedFirstText.find("FBY ANIM GRAPH BEGIN") != std::string::npos);
    FBY_CHECK(ScopedFirstText.find("name = \"anim_canvas_test\"") != std::string::npos);
    FBY_CHECK(ScopedFirstText.find("name = \"hero\"") != std::string::npos);

    Fby::Scene::ComponentRegistry ScopedSceneComponents;
    Fby::Scene::RegisterBuiltInComponents(ScopedSceneComponents);
    const auto ScopedDecoded = Fby::Scene::DecodeSceneToml(ScopedFirstText, ScopedSceneComponents);
    FBY_CHECK(ScopedDecoded.has_value());
    FBY_CHECK(ScopedDecoded.has_value() && ScopedDecoded->entities.size() == 1);

    FBY_CHECK(ScopedPanel.SaveToSceneToml());
    const std::string ScopedSecondText = ReadText(ScopedScene);
    std::size_t ScopedMarkerCount = 0;
    for (std::size_t ScopedPos = 0;
         (ScopedPos = ScopedSecondText.find("FBY ANIM GRAPH BEGIN", ScopedPos)) != std::string::npos; ScopedPos += 10)
    {
        ++ScopedMarkerCount;
    }
    FBY_CHECK(ScopedMarkerCount == 1);

    Fby::Editor::CommandStack ScopedReloadCommands;
    FBY_CHECK(ScopedReloadCommands.Initialize());
    Fby::Editor::AnimationGraphCanvasPanel ScopedReloaded;
    FBY_CHECK(ScopedReloaded.Initialize(ScopedReloadCommands, 12));
    ScopedReloaded.SetScenePath(ScopedScene);
    FBY_CHECK(ScopedReloaded.LoadFromSceneToml());
    FBY_CHECK(ScopedReloaded.Compiled());
    FBY_CHECK(ScopedReloaded.Document().NodeCount == ScopedPanel.Document().NodeCount);
    FBY_CHECK(ScopedReloaded.Document().LinkCount == ScopedPanel.Document().LinkCount);
    FBY_CHECK(ScopedReloaded.CompiledComponent().ClipCount == 2);
    FBY_CHECK(CountCompiledTransitions(ScopedReloaded.CompiledComponent()) == 2);
    FBY_CHECK(ScopedReloaded.CompiledComponent().Clips[0].State == 1);
    FBY_CHECK(ScopedReloaded.CompiledComponent().Floats[0] == 1.0f);

    std::filesystem::remove_all(ScopedDirectory, ScopedCode);
}

void TestPIEState()
{
    Fby::Editor::CommandStack ScopedCommands;
    FBY_CHECK(ScopedCommands.Initialize());
    Fby::Editor::AnimationGraphCanvasPanel ScopedPanel;
    FBY_CHECK(ScopedPanel.Initialize(ScopedCommands, 13));
    FBY_CHECK(ScopedPanel.CompileGraph());

    entt::registry ScopedRegistry;
    const Fby::Entity ScopedEntity = ScopedRegistry.create();
    ScopedRegistry.emplace<Fby::EditorSelected>(ScopedEntity);
    Fby::AnimStateComponent& ScopedState = ScopedRegistry.emplace<Fby::AnimStateComponent>(ScopedEntity);
    ScopedState.CurrentState = 1;
    ScopedState.PreviousState = 2;
    ScopedState.BlendWeight = 0.42f;
    ScopedState.BlendTime = 0.1f;
    ScopedState.LastTransition = 1;

    ScopedPanel.BindRuntimeWorld(&ScopedRegistry, &ScopedRegistry, true);
    // No producer has published yet: the reader must not touch the live
    FBY_CHECK(ScopedPanel.RefreshPIEState() == 0);
    FBY_CHECK(ScopedPanel.PublishPIEStateFromWorlds());
    FBY_CHECK(ScopedPanel.RefreshPIEState() == 2);
    FBY_CHECK(ScopedPanel.PIEActiveNode() == FindClipNode(ScopedPanel.Document(), 1));
    FBY_CHECK(ScopedPanel.PIEPreviousNode() == FindClipNode(ScopedPanel.Document(), 2));
    FBY_CHECK(ScopedPanel.PIEBlendWeight() == 0.42f);
    FBY_CHECK(ScopedPanel.PIEBlendTime() == 0.1f);
    FBY_CHECK(ScopedPanel.PIEActiveLink() < ScopedPanel.Document().LinkCount);

    // A live mutation is invisible until the simulation side publishes. This is
    ScopedState.CurrentState = 99;
    FBY_CHECK(ScopedPanel.RefreshPIEState() == 2);
    FBY_CHECK(ScopedPanel.PIEActiveNode() == FindClipNode(ScopedPanel.Document(), 1));
    FBY_CHECK(ScopedPanel.PublishPIEStateFromWorlds());
    FBY_CHECK(ScopedPanel.RefreshPIEState() == 0);

    ScopedPanel.BindRuntimeWorld(&ScopedRegistry, &ScopedRegistry, false);
    FBY_CHECK(ScopedPanel.RefreshPIEState() == 0);
}

// Writer/reader stress: the seqlock must never expose a torn frame (a state id
void TestPIEConcurrentPublication()
{
    Fby::Editor::CommandStack ScopedCommands;
    FBY_CHECK(ScopedCommands.Initialize());
    Fby::Editor::AnimationGraphCanvasPanel ScopedPanel;
    FBY_CHECK(ScopedPanel.Initialize(ScopedCommands, 17));
    FBY_CHECK(ScopedPanel.CompileGraph());
    ScopedPanel.BindRuntimeWorld(nullptr, nullptr, true);

    Fby::AnimStateComponent ScopedState;
    std::atomic<bool> ScopedStop{false};
    std::atomic<std::uint32_t> ScopedBadReads{0};
    std::thread ScopedWriter(
        [&]()
        {
            for (std::uint32_t ScopedI = 0; ScopedI < 200000; ++ScopedI)
            {
                const bool ScopedIdle = (ScopedI & 1u) == 0;
                ScopedState.CurrentState = ScopedIdle ? 1 : 2;
                ScopedState.PreviousState = ScopedIdle ? 2 : 1;
                ScopedState.BlendWeight = static_cast<float>(ScopedI & 1023u) / 1023.0f;
                ScopedState.BlendTime = ScopedState.BlendWeight * 0.2f;
                ScopedState.LastTransition = static_cast<std::uint16_t>(ScopedI & 1u);
                ScopedPanel.PublishPIEState(ScopedState);
            }
            ScopedStop.store(true, std::memory_order_release);
        });
    while (!ScopedStop.load(std::memory_order_acquire))
    {
        const std::uint32_t ScopedActive = ScopedPanel.RefreshPIEState();
        if (ScopedActive == 0)
        {
            // A retry can legitimately fail while a slot is mid-update; the
            continue;
        }
        if (ScopedActive > 2)
        {
            ScopedBadReads.fetch_add(1, std::memory_order_relaxed);
        }
    }
    ScopedWriter.join();
    FBY_CHECK(ScopedBadReads.load() == 0);
    FBY_CHECK(ScopedPanel.RefreshPIEState() != 0);
}

void TestBlendCycleValidation()
{
    Fby::Editor::CommandStack ScopedCommands;
    FBY_CHECK(ScopedCommands.Initialize());
    Fby::Editor::AnimationGraphCanvasPanel ScopedPanel;
    FBY_CHECK(ScopedPanel.Initialize(ScopedCommands, 15));
    FBY_CHECK(ScopedPanel.CompileGraph());

    // Make the demo's Idle -> Attack blend immediately hot (StateTime >= 0).
    Fby::Editor::AnimationGraphNode* ScopedFirstBlend = nullptr;
    for (std::uint16_t ScopedNode = 0; ScopedNode < ScopedPanel.Document().NodeCount; ++ScopedNode)
    {
        if (ScopedPanel.Document().Nodes[ScopedNode].Kind == Fby::Editor::AnimationGraphNodeKind::Blend)
        {
            ScopedFirstBlend = &ScopedPanel.Document().Nodes[ScopedNode];
            break;
        }
    }
    FBY_CHECK(ScopedFirstBlend != nullptr);
    if (ScopedFirstBlend != nullptr)
    {
        ScopedFirstBlend->Blend.Conditions = Fby::AnimConditionStateTimeAtLeast;
        ScopedFirstBlend->Blend.StateTimeThreshold = 0.0f;
    }

    // Add the second half of a two-blend loop: Attack -> Blend 2 -> Idle. Its
    const std::uint16_t ScopedSecondBlend =
        ScopedPanel.AddNode(Fby::Editor::AnimationGraphNodeKind::Blend, Fby::Vector2D{});
    FBY_CHECK(ScopedSecondBlend != 0xFFFFu);
    FBY_CHECK(ScopedPanel.AddLink(1, 0, ScopedSecondBlend, 0));
    FBY_CHECK(ScopedPanel.AddLink(ScopedSecondBlend, 0, 0, 0));

    int ScopedWarnings = 0;
    GCycleWarnings.store(0, std::memory_order_relaxed);
    Fby::SetLogObserver(&CycleLogObserver);
    FBY_CHECK(!ScopedPanel.CompileGraph());
    Fby::SetLogObserver(nullptr);
    ScopedWarnings = GCycleWarnings.load(std::memory_order_relaxed);
    FBY_CHECK(std::string_view(ScopedPanel.Status()).find("dead blend cycle") != std::string_view::npos);
    FBY_CHECK(ScopedWarnings >= 1);

    // The same hot cycle becomes legal as soon as one enabled transition leaves
    const std::uint16_t ScopedExitClip =
        ScopedPanel.AddNode(Fby::Editor::AnimationGraphNodeKind::Clip, Fby::Vector2D{});
    FBY_CHECK(ScopedExitClip != 0xFFFFu);
    FBY_CHECK(ScopedPanel.AddLink(1, 0, ScopedExitClip, 0));
    FBY_CHECK(ScopedPanel.CompileGraph());

    // The unmodified demo remains valid: a gated blend and a gated return are a
    ScopedPanel.BuildDefaultDocument();
    FBY_CHECK(ScopedPanel.CompileGraph());
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
    Fby::Editor::AnimationGraphCanvasPanel ScopedPanel;
    FBY_CHECK(ScopedPanel.Initialize(ScopedCommands, 14));
    bool ScopedOpen = true;
    for (int ScopedFrame = 0; ScopedFrame < 3; ++ScopedFrame)
    {
        ImGui::NewFrame();
        ScopedPanel.Draw("Animation Graph", &ScopedOpen);
        ImGui::Render();
    }
    // Structural edits after a live ImNodes context exists must purge the
    ScopedPanel.NotifyHistoryChanged();
    FBY_CHECK(ScopedPanel.RemoveLink(0));
    FBY_CHECK(ScopedPanel.DeleteNode(0));
    ImGui::NewFrame();
    ScopedPanel.Draw("Animation Graph", &ScopedOpen);
    ImGui::Render();
    ScopedPanel.Shutdown();
    ImGui::DestroyContext();
}
}

int main()
{
    TestCompileDefaultGraph();
    TestFieldComparison();
    TestCommandStackUndo();
    TestCompileValidation();
    TestBlendCycleValidation();
    TestSceneTomlRoundTrip();
    TestPIEState();
    TestPIEConcurrentPublication();
    TestPanelDrawHeadless();

    if (GFailures == 0)
    {
        std::printf("AnimationGraphCanvasPanelTest: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "AnimationGraphCanvasPanelTest: %d check(s) failed\n", GFailures);
    return 1;
}
