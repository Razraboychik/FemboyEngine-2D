// FemboyEngine2D - FemboyScript debugger tests: spans, breakpoints, stepping.
#include "Engine/ECS/ComponentRegistry.hpp"
#include "Engine/ECS/Components.hpp"
#include "Engine/Script/GraphCompiler.hpp"
#include "Engine/Script/ScriptDebug.hpp"
#include "Engine/Script/ScriptSystem.hpp"
#include "Engine/Script/TextScript.hpp"
#include "Engine/Script/Vm.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

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

bool CompileAndLoad(Fby::Scene::ComponentRegistry& InComponents, std::vector<std::byte>& OutBytes,
                    std::string& OutError, Fby::Script::CompiledProgram& OutParsed)
{
    Fby::Script::TextCompileOptions ScopedOptions;
    ScopedOptions.sourceName = "test.fbs";
    if (!Fby::Script::CompileTextScript(kSource, InComponents, ScopedOptions, &OutBytes, &OutError))
    {
        std::fprintf(stderr, "text compile failed: %s\n", OutError.c_str());
        return false;
    }
    return Fby::Script::ParseProgram(OutBytes, &OutParsed, &OutError);
}

void TestDebugSection()
{
    Fby::Scene::ComponentRegistry ScopedComponents;
    Fby::Scene::RegisterBuiltInComponents(ScopedComponents);
    std::vector<std::byte> ScopedBytes;
    std::string ScopedError;
    Fby::Script::CompiledProgram ScopedProgram;
    FBY_CHECK(CompileAndLoad(ScopedComponents, ScopedBytes, ScopedError, ScopedProgram));
    FBY_CHECK(ScopedProgram.debug.present);
    FBY_CHECK(Fby::Script::SourceName(ScopedProgram) == "test.fbs");
    FBY_CHECK(!ScopedProgram.debug.spans.empty());
    FBY_CHECK(ScopedProgram.debug.variables.size() == 1);
    FBY_CHECK(ScopedProgram.debug.spans.size() <= ScopedProgram.code.size());

    std::uint32_t ScopedPreviousPc = 0;
    for (std::size_t ScopedI = 0; ScopedI < ScopedProgram.debug.spans.size(); ++ScopedI)
    {
        const Fby::Script::DebugSpan& ScopedSpan = ScopedProgram.debug.spans[ScopedI];
        FBY_CHECK(ScopedSpan.Line >= 1 && ScopedSpan.Line <= 4);
        FBY_CHECK(ScopedSpan.Column >= 1);
        if (ScopedI > 0)
        {
            FBY_CHECK(ScopedSpan.Pc > ScopedPreviousPc);
        }
        ScopedPreviousPc = ScopedSpan.Pc;
    }

    const std::uint16_t ScopedRegister = ScopedProgram.debug.variables[0].Register;
    FBY_CHECK(Fby::Script::VariableName(ScopedProgram, ScopedRegister) == "half");
    FBY_CHECK(Fby::Script::VariableName(ScopedProgram, static_cast<std::uint8_t>(ScopedRegister + 5)).empty());

    const std::uint32_t ScopedAssignPc = Fby::Script::FirstPcForLine(ScopedProgram, 3);
    FBY_CHECK(ScopedAssignPc != Fby::Script::kInvalidPc);
    const Fby::Script::SourceLocation ScopedLocation = Fby::Script::LocatePc(ScopedProgram, ScopedAssignPc);
    FBY_CHECK(ScopedLocation.Valid);
    FBY_CHECK(ScopedLocation.Line == 3);
    FBY_CHECK(!ScopedLocation.IsGraphNode());

    const Fby::Script::SourceLocation ScopedPast = Fby::Script::LocatePc(ScopedProgram, 0);
    FBY_CHECK(!ScopedPast.Valid || ScopedPast.Line <= 3);
}

void TestBreakpointsAndResume()
{
    Fby::Scene::ComponentRegistry ScopedComponents;
    Fby::Scene::RegisterBuiltInComponents(ScopedComponents);
    std::vector<std::byte> ScopedBytes;
    std::string ScopedError;
    Fby::Script::CompiledProgram ScopedParsed;
    if (!CompileAndLoad(ScopedComponents, ScopedBytes, ScopedError, ScopedParsed))
    {
        FBY_CHECK(false);
        return;
    }

    Fby::ScriptRuntime ScopedRuntime;
    FBY_CHECK(ScopedRuntime.Initialize());
    const std::uint16_t ScopedHandle = ScopedRuntime.LoadProgram(ScopedBytes, &ScopedError);
    FBY_CHECK(ScopedHandle != Fby::kScriptNoProgram);
    const Fby::Script::BoundProgram* ScopedBound = ScopedRuntime.Program(ScopedHandle);
    FBY_CHECK(ScopedBound != nullptr);
    if (ScopedBound == nullptr)
    {
        return;
    }

    entt::registry ScopedRegistry;
    const Fby::Entity ScopedEntity = ScopedRegistry.create();
    ScopedRegistry.emplace<Fby::Transform2D>(ScopedEntity);
    Fby::Script::VmContext ScopedContext{ScopedRegistry};

    Fby::Script::VmDebugState ScopedDebug;
    ScopedDebug.Enabled = true;
    const std::uint32_t ScopedAssignPc = Fby::Script::FirstPcForLine(ScopedParsed, 3);
    ScopedDebug.SetBreakpoint(ScopedAssignPc);
    FBY_CHECK(ScopedDebug.BreakpointCount == 1);

    const Fby::Script::VmStopInfo ScopedFirst =
        ScopedRuntime.Vm().RunDebug(*ScopedBound, Fby::Script::ScriptEvent::Update, ScopedEntity, ScopedContext,
                                    ScopedDebug);
    FBY_CHECK(ScopedFirst.Halt == Fby::Script::VmHalt::Paused);
    FBY_CHECK(ScopedFirst.Reason == Fby::Script::VmPauseReason::Breakpoint);
    FBY_CHECK(ScopedFirst.Pc == ScopedAssignPc);
    FBY_CHECK(ScopedDebug.Paused);
    FBY_CHECK(ScopedRuntime.Vm().F(static_cast<std::uint8_t>(ScopedParsed.debug.variables[0].Register)) == 0.5f);
    FBY_CHECK(ScopedRegistry.get<Fby::Transform2D>(ScopedEntity).Rotation == 0.0f);

    // Resuming skips the breakpoint once, so the assignment completes.
    const Fby::Script::VmStopInfo ScopedSecond =
        ScopedRuntime.Vm().RunDebug(*ScopedBound, Fby::Script::ScriptEvent::Update, ScopedEntity, ScopedContext,
                                    ScopedDebug);
    FBY_CHECK(ScopedSecond.Halt == Fby::Script::VmHalt::Completed);
    FBY_CHECK(!ScopedDebug.Paused);
    FBY_CHECK(ScopedRegistry.get<Fby::Transform2D>(ScopedEntity).Rotation == 0.5f);

    // A breakpoint at the entry instruction pauses before anything ran.
    Fby::Script::VmDebugState ScopedEntryDebug;
    ScopedEntryDebug.Enabled = true;
    const std::uint32_t ScopedEntryPc = ScopedBound->program.functions[0].codeOffset;
    ScopedEntryDebug.SetBreakpoint(ScopedEntryPc);
    const Fby::Script::VmStopInfo ScopedEntry =
        ScopedRuntime.Vm().RunDebug(*ScopedBound, Fby::Script::ScriptEvent::Update, ScopedEntity, ScopedContext,
                                    ScopedEntryDebug);
    FBY_CHECK(ScopedEntry.Halt == Fby::Script::VmHalt::Paused);
    FBY_CHECK(ScopedEntry.Pc == ScopedEntryPc);
}

void TestStepping()
{
    Fby::Scene::ComponentRegistry ScopedComponents;
    Fby::Scene::RegisterBuiltInComponents(ScopedComponents);
    std::vector<std::byte> ScopedBytes;
    std::string ScopedError;
    Fby::Script::CompiledProgram ScopedParsed;
    if (!CompileAndLoad(ScopedComponents, ScopedBytes, ScopedError, ScopedParsed))
    {
        FBY_CHECK(false);
        return;
    }
    Fby::ScriptRuntime ScopedRuntime;
    FBY_CHECK(ScopedRuntime.Initialize());
    const std::uint16_t ScopedHandle = ScopedRuntime.LoadProgram(ScopedBytes, &ScopedError);
    const Fby::Script::BoundProgram* ScopedBound = ScopedRuntime.Program(ScopedHandle);
    FBY_CHECK(ScopedBound != nullptr);
    if (ScopedBound == nullptr)
    {
        return;
    }
    entt::registry ScopedRegistry;
    const Fby::Entity ScopedEntity = ScopedRegistry.create();
    ScopedRegistry.emplace<Fby::Transform2D>(ScopedEntity);
    Fby::Script::VmContext ScopedContext{ScopedRegistry};

    Fby::Script::VmDebugState ScopedDebug;
    ScopedDebug.Enabled = true;
    ScopedRuntime.Vm().RequestPauseAtEntry(ScopedDebug);
    const Fby::Script::VmStopInfo ScopedEntry =
        ScopedRuntime.Vm().RunDebug(*ScopedBound, Fby::Script::ScriptEvent::Update, ScopedEntity, ScopedContext,
                                    ScopedDebug);
    FBY_CHECK(ScopedEntry.Halt == Fby::Script::VmHalt::Paused);
    FBY_CHECK(ScopedEntry.Reason == Fby::Script::VmPauseReason::Request);
    const std::uint32_t ScopedEntryPc = ScopedEntry.Pc;

    ScopedDebug.StepBudget = 1;
    const Fby::Script::VmStopInfo ScopedStep =
        ScopedRuntime.Vm().RunDebug(*ScopedBound, Fby::Script::ScriptEvent::Update, ScopedEntity, ScopedContext,
                                    ScopedDebug);
    FBY_CHECK(ScopedStep.Halt == Fby::Script::VmHalt::Paused);
    FBY_CHECK(ScopedStep.Reason == Fby::Script::VmPauseReason::Step);
    FBY_CHECK(ScopedStep.Pc > ScopedEntryPc);

    // Step to the end: the loop is bounded so a stuck VM fails the test instead
    // of hanging the suite.
    bool ScopedCompleted = false;
    for (std::uint32_t ScopedAttempt = 0; ScopedAttempt < 256; ++ScopedAttempt)
    {
        ScopedDebug.StepBudget = 1;
        const Fby::Script::VmStopInfo ScopedNext =
            ScopedRuntime.Vm().RunDebug(*ScopedBound, Fby::Script::ScriptEvent::Update, ScopedEntity, ScopedContext,
                                        ScopedDebug);
        if (ScopedNext.Halt == Fby::Script::VmHalt::Completed)
        {
            ScopedCompleted = true;
            break;
        }
        FBY_CHECK(ScopedNext.Halt == Fby::Script::VmHalt::Paused);
    }
    FBY_CHECK(ScopedCompleted);
    FBY_CHECK(ScopedRegistry.get<Fby::Transform2D>(ScopedEntity).Rotation == 0.5f);

    Fby::Script::VmDebugState ScopedClean;
    ScopedClean.Enabled = true;
    ScopedClean.SetBreakpoint(3);
    ScopedClean.ClearBreakpoint(3);
    FBY_CHECK(ScopedClean.BreakpointCount == 0);
    ScopedClean.SetBreakpoint(3);
    ScopedClean.ClearBreakpoints();
    FBY_CHECK(ScopedClean.BreakpointCount == 0);
}

void TestGraphSpansAndFields()
{
    using namespace Fby::Script;
    LogicGraph ScopedGraph;
    const std::uint16_t ScopedEvent = ScopedGraph.AddNode(LogicNodeKind::EventUpdate);
    const std::uint16_t ScopedSetVelocity = ScopedGraph.AddNode(LogicNodeKind::SetVelocity);
    const std::uint16_t ScopedSelfVelocity = ScopedGraph.AddNode(LogicNodeKind::SelfVelocity);
    (void)ScopedGraph.AddLink(ScopedEvent, 0, ScopedSetVelocity, 0);
    (void)ScopedGraph.AddLink(ScopedSelfVelocity, 0, ScopedSetVelocity, 1);
    LogicCompileResult ScopedCompiled;
    FBY_CHECK(CompileLogicGraph(ScopedGraph, &ScopedCompiled));
    CompiledProgram ScopedParsed;
    std::string ScopedError;
    FBY_CHECK(ParseProgram(ScopedCompiled.program, &ScopedParsed, &ScopedError));
    FBY_CHECK(ScopedParsed.debug.present);
    FBY_CHECK(!ScopedParsed.debug.spans.empty());

    bool ScopedSawGraphNode = false;
    for (const DebugSpan& ScopedSpan : ScopedParsed.debug.spans)
    {
        if ((ScopedSpan.Flags & DebugSpanGraphNode) != 0)
        {
            ScopedSawGraphNode = true;
        }
    }
    FBY_CHECK(ScopedSawGraphNode);
}

void TestFieldAccess()
{
    Fby::Scene::ComponentRegistry ScopedComponents;
    Fby::Scene::RegisterBuiltInComponents(ScopedComponents);
    std::vector<std::byte> ScopedBytes;
    std::string ScopedError;
    Fby::Script::CompiledProgram ScopedParsed;
    if (!CompileAndLoad(ScopedComponents, ScopedBytes, ScopedError, ScopedParsed))
    {
        FBY_CHECK(false);
        return;
    }
    Fby::ScriptRuntime ScopedRuntime;
    FBY_CHECK(ScopedRuntime.Initialize());
    const std::uint16_t ScopedHandle = ScopedRuntime.LoadProgram(ScopedBytes, &ScopedError);
    const Fby::Script::BoundProgram* ScopedBound = ScopedRuntime.Program(ScopedHandle);
    FBY_CHECK(ScopedBound != nullptr);
    if (ScopedBound == nullptr)
    {
        return;
    }
    entt::registry ScopedRegistry;
    const Fby::Entity ScopedEntity = ScopedRegistry.create();
    ScopedRegistry.emplace<Fby::Transform2D>(ScopedEntity);
    ScopedRegistry.get<Fby::Transform2D>(ScopedEntity).Rotation = 0.25f;
    Fby::Script::VmContext ScopedContext{ScopedRegistry};
    (void)ScopedContext;

    std::array<Fby::Script::DebugField, 32> ScopedFields{};
    const std::uint32_t ScopedFieldCount =
        Fby::Script::DescribeFields(ScopedParsed, ScopedComponents, ScopedFields);
    FBY_CHECK(ScopedFieldCount >= 1);
    bool ScopedFoundRotation = false;
    std::uint32_t ScopedRotationField = 0;
    for (std::uint32_t ScopedI = 0; ScopedI < ScopedFieldCount; ++ScopedI)
    {
        if (ScopedFields[ScopedI].Component == "Transform2D" && ScopedFields[ScopedI].Field == "Rotation")
        {
            ScopedFoundRotation = true;
            ScopedRotationField = ScopedI;
        }
    }
    FBY_CHECK(ScopedFoundRotation);

    float ScopedValue = 0.0f;
    FBY_CHECK(Fby::Script::ReadFieldValue(*ScopedBound, ScopedRegistry, ScopedEntity, ScopedRotationField, &ScopedValue));
    FBY_CHECK(std::fabs(ScopedValue - 0.25f) < 1.0e-6f);
    FBY_CHECK(Fby::Script::WriteFieldValue(*ScopedBound, ScopedRegistry, ScopedEntity, ScopedRotationField, 1.5f));
    FBY_CHECK(ScopedRegistry.get<Fby::Transform2D>(ScopedEntity).Rotation == 1.5f);

    const Fby::Script::DebugFrame ScopedFrame =
        Fby::Script::MakeFrame(ScopedParsed, Fby::Script::ScriptEvent::Update, ScopedEntity,
                               Fby::Script::FirstPcForLine(ScopedParsed, 3));
    FBY_CHECK(ScopedFrame.Event == Fby::Script::ScriptEvent::Update);
    FBY_CHECK(ScopedFrame.Self == ScopedEntity);
    FBY_CHECK(ScopedFrame.Location.Line == 3);
}
}

int main()
{
    TestDebugSection();
    TestBreakpointsAndResume();
    TestStepping();
    TestGraphSpansAndFields();
    TestFieldAccess();

    if (GFailures != 0)
    {
        std::fprintf(stderr, "%u script debug test failure(s)\n", GFailures);
        return 1;
    }
    std::printf("script debug tests passed\n");
    return 0;
}
