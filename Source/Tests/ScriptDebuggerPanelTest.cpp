// FemboyEngine2D - script debugger panel tests (no ImGui frame is drawn).
#include "Editor/Panels/ScriptDebuggerPanel.hpp"
#include "Editor/Panels/AnimationTimelinePanel.hpp"

#include "Engine/ECS/ComponentRegistry.hpp"
#include "Engine/ECS/Components.hpp"
#include "Engine/Script/GraphCompiler.hpp"

#include <bit>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace
{
std::uint32_t GFailures = 0;

#define FBY_CHECK(condition)                                                                                           \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);                                  \
            ++GFailures;                                                                                              \
        }                                                                                                              \
    } while (false)

constexpr std::string_view kSource = R"FBY(event update {
    let half = 0.5;
    self.Transform2D.Rotation = half;
})FBY";

void TestCompileAndBreakpoint()
{
    Fby::Scene::ComponentRegistry ScopedComponents;
    Fby::Scene::RegisterBuiltInComponents(ScopedComponents);

    Fby::Editor::ScriptDebuggerPanel ScopedPanel;
    FBY_CHECK(ScopedPanel.Initialize(&ScopedComponents));
    ScopedPanel.SetSource(kSource);
    FBY_CHECK(ScopedPanel.Compile(ScopedComponents));
    FBY_CHECK(ScopedPanel.ProgramHandle() != Fby::kScriptNoProgram);
    FBY_CHECK(ScopedPanel.Status().find("compiled") != std::string::npos);
    FBY_CHECK(ScopedPanel.SetBreakpointLine(3));
    FBY_CHECK(!ScopedPanel.SetBreakpointLine(3));

    entt::registry ScopedRegistry;
    const Fby::Entity ScopedEntity = ScopedRegistry.create();
    ScopedRegistry.emplace<Fby::Transform2D>(ScopedEntity);

    FBY_CHECK(ScopedPanel.RunOnce(ScopedRegistry, ScopedEntity));
    FBY_CHECK(ScopedPanel.Paused());
    FBY_CHECK(ScopedPanel.CurrentLine() == 3);
    FBY_CHECK(ScopedPanel.Status().find("breakpoint") != std::string::npos);
    FBY_CHECK(ScopedRegistry.get<Fby::Transform2D>(ScopedEntity).Rotation == 0.0f);

    FBY_CHECK(ScopedPanel.RunOnce(ScopedRegistry, ScopedEntity));
    FBY_CHECK(!ScopedPanel.Paused());
    FBY_CHECK(ScopedPanel.Status() == "completed");
    FBY_CHECK(ScopedRegistry.get<Fby::Transform2D>(ScopedEntity).Rotation == 0.5f);

    ScopedPanel.ClearBreakpoints();
}

void TestCompileError()
{
    Fby::Scene::ComponentRegistry ScopedComponents;
    Fby::Scene::RegisterBuiltInComponents(ScopedComponents);
    Fby::Editor::ScriptDebuggerPanel ScopedPanel;
    FBY_CHECK(ScopedPanel.Initialize(&ScopedComponents));
    ScopedPanel.SetSource("event update {\n    let x = ;\n}\n");
    FBY_CHECK(!ScopedPanel.Compile(ScopedComponents));
    FBY_CHECK(ScopedPanel.Status().find("line") != std::string::npos);
    FBY_CHECK(ScopedPanel.ProgramHandle() == Fby::kScriptNoProgram);
}

void TestFaultReporting()
{
    Fby::Scene::ComponentRegistry ScopedComponents;
    Fby::Scene::RegisterBuiltInComponents(ScopedComponents);
    Fby::Editor::ScriptDebuggerPanel ScopedPanel;
    FBY_CHECK(ScopedPanel.Initialize(&ScopedComponents));
    ScopedPanel.SetSource(kSource);
    FBY_CHECK(ScopedPanel.Compile(ScopedComponents));

    entt::registry ScopedRegistry;
    const Fby::Entity ScopedEntity = ScopedRegistry.create();
    // No Transform2D on purpose: the program must fault instead of crashing.
    FBY_CHECK(!ScopedPanel.RunOnce(ScopedRegistry, ScopedEntity));
    FBY_CHECK(ScopedPanel.Status().find("fault") != std::string::npos);
    FBY_CHECK(ScopedPanel.Status().find("missing-component") != std::string::npos);
}

void TestGraphProgramDebugging()
{
    using namespace Fby::Script;
    LogicGraph ScopedGraph;
    const std::uint16_t ScopedEvent = ScopedGraph.AddNode(LogicNodeKind::EventUpdate);
    const std::uint16_t ScopedSetVelocity = ScopedGraph.AddNode(LogicNodeKind::SetVelocity);
    const std::uint16_t ScopedValue =
        ScopedGraph.AddNode(LogicNodeKind::ConstVec2, std::bit_cast<std::uint32_t>(10.0f));
    ScopedGraph.nodes[ScopedValue].param2 = std::bit_cast<std::uint32_t>(20.0f);
    FBY_CHECK(ScopedGraph.AddLink(ScopedEvent, 0, ScopedSetVelocity, 0));
    FBY_CHECK(ScopedGraph.AddLink(ScopedValue, 0, ScopedSetVelocity, 1));
    LogicCompileResult ScopedCompiled;
    FBY_CHECK(CompileLogicGraph(ScopedGraph, &ScopedCompiled));
    FBY_CHECK(!ScopedCompiled.program.empty());

    Fby::Scene::ComponentRegistry ScopedComponents;
    Fby::Scene::RegisterBuiltInComponents(ScopedComponents);
    Fby::Editor::ScriptDebuggerPanel ScopedPanel;
    FBY_CHECK(ScopedPanel.Initialize(&ScopedComponents));
    FBY_CHECK(ScopedPanel.LoadGraphProgram(ScopedCompiled.program, "LogicGraph"));
    FBY_CHECK(ScopedPanel.IsGraphProgram());
    FBY_CHECK(ScopedPanel.Status().find("LogicGraph") != std::string::npos);
    FBY_CHECK(ScopedPanel.SetBreakpointLine(ScopedSetVelocity));

    entt::registry ScopedRegistry;
    const Fby::Entity ScopedEntity = ScopedRegistry.create();
    ScopedRegistry.emplace<Fby::Velocity>(ScopedEntity, Fby::Velocity{});
    FBY_CHECK(ScopedPanel.RunOnce(ScopedRegistry, ScopedEntity));
    FBY_CHECK(ScopedPanel.Paused());
    FBY_CHECK(ScopedPanel.CurrentGraphNode() == ScopedSetVelocity);
    FBY_CHECK(ScopedRegistry.get<Fby::Velocity>(ScopedEntity).Value.X == 0.0f);

    FBY_CHECK(ScopedPanel.RunOnce(ScopedRegistry, ScopedEntity));
    FBY_CHECK(!ScopedPanel.Paused());
    FBY_CHECK(ScopedPanel.CurrentGraphNode() == Fby::Editor::ScriptDebuggerPanel::kNoGraphNode);
    const Fby::Vector2D ScopedVelocity = ScopedRegistry.get<Fby::Velocity>(ScopedEntity).Value;
    FBY_CHECK(ScopedVelocity.X == 10.0f);
    FBY_CHECK(ScopedVelocity.Y == 20.0f);
}

void TestTimelineClipFile()
{
    Fby::Animation::AnimationTimeline ScopedTimeline;
    ScopedTimeline.Clear();
    ScopedTimeline.duration = 1.0f;
    const std::uint16_t ScopedTrack =
        ScopedTimeline.AddTrack(Fby::Animation::TrackKind::BoneRotation, 0x77u, 0xFFFFFFFFu);
    (void)ScopedTimeline.AddKey(ScopedTrack, 0.0f, Fby::Vector2D{0.0f, 0.0f}, Fby::Animation::Interpolation::Linear);
    (void)ScopedTimeline.AddKey(ScopedTrack, 1.0f, Fby::Vector2D{1.0f, 0.0f}, Fby::Animation::Interpolation::Linear);

    Fby::Editor::AnimationTimelinePanel ScopedWriter;
    Fby::Editor::TimelinePanelContext ScopedWriterContext;
    ScopedWriterContext.timeline = &ScopedTimeline;
    ScopedWriter.SetContext(ScopedWriterContext);

    const std::filesystem::path ScopedPath = std::filesystem::current_path() / "fby_clip_panel_test.fanim";
    FBY_CHECK(ScopedWriter.SaveClipFile(ScopedPath.string().c_str()));
    FBY_CHECK(std::filesystem::is_regular_file(ScopedPath));

    Fby::Animation::AnimationTimeline ScopedLoaded;
    Fby::Editor::AnimationTimelinePanel ScopedReader;
    Fby::Editor::TimelinePanelContext ScopedReaderContext;
    ScopedReaderContext.timeline = &ScopedLoaded;
    ScopedReader.SetContext(ScopedReaderContext);
    FBY_CHECK(ScopedReader.LoadClipFile(ScopedPath.string().c_str()));
    FBY_CHECK(ScopedLoaded.trackCount == 1);
    FBY_CHECK(ScopedLoaded.keyCount == 2);
    const Fby::Vector2D ScopedSample = ScopedLoaded.EvaluateTrack(0, 0.5f);
    FBY_CHECK(std::fabs(ScopedSample.X - 0.5f) < 0.05f);

    // A truncated file is rejected with a status instead of a partial timeline.
    {
        std::ofstream ScopedFile(ScopedPath, std::ios::binary | std::ios::trunc);
        ScopedFile << "not-a-clip";
    }
    FBY_CHECK(!ScopedReader.LoadClipFile(ScopedPath.string().c_str()));
    std::error_code ScopedError;
    std::filesystem::remove(ScopedPath, ScopedError);
}
}

int main()
{
    TestCompileAndBreakpoint();
    TestCompileError();
    TestFaultReporting();
    TestGraphProgramDebugging();
    TestTimelineClipFile();

    if (GFailures != 0)
    {
        std::fprintf(stderr, "%u script debugger panel test failure(s)\n", GFailures);
        return 1;
    }
    std::printf("script debugger panel tests passed\n");
    return 0;
}
