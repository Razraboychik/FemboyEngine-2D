// FemboyEngine2D - FemboyScript debugger panel implementation.
#include "Editor/Panels/ScriptDebuggerPanel.hpp"

#include <charconv>

#include "Editor/Infrastructure/EditorTypography.hpp"

#include "Engine/Core/Mirror.hpp"
#include "Engine/ECS/ComponentRegistry.hpp"
#include "Engine/Script/AotBuilder.hpp"
#include "Engine/Script/AotHost.hpp"
#include "Engine/Script/Bytecode.hpp"
#include "Engine/Script/ScriptDebug.hpp"
#include "Engine/Script/TextScript.hpp"

#include "Editor/Panels/VisualScriptPanel.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string_view>

namespace Fby::Editor
{
namespace
{
constexpr std::string_view kDefaultSource = R"FBY(event update {
    let speed = 2.0;
    self.Transform2D.Rotation = self.Transform2D.Rotation + speed * 0.01;
})FBY";

constexpr std::uint32_t kMaxSourceLines = 256;

[[nodiscard]] std::uint32_t SplitLines(const char* InSource, std::array<std::string_view, kMaxSourceLines>& OutLines,
                                       std::array<std::uint32_t, kMaxSourceLines>& OutNumbers) noexcept
{
    std::uint32_t ScopedCount = 0;
    std::string_view ScopedSource(InSource);
    std::size_t ScopedStart = 0;
    std::uint32_t ScopedLine = 1;
    while (ScopedStart <= ScopedSource.size() && ScopedCount < kMaxSourceLines)
    {
        const std::size_t ScopedEnd = ScopedSource.find('\n', ScopedStart);
        const std::size_t ScopedLength =
            ScopedEnd == std::string_view::npos ? ScopedSource.size() - ScopedStart : ScopedEnd - ScopedStart;
        OutLines[ScopedCount] = ScopedSource.substr(ScopedStart, ScopedLength);
        OutNumbers[ScopedCount] = ScopedLine;
        ++ScopedCount;
        if (ScopedEnd == std::string_view::npos)
        {
            break;
        }
        ScopedStart = ScopedEnd + 1;
        ++ScopedLine;
    }
    return ScopedCount;
}

[[nodiscard]] const char* PauseReasonName(Script::VmPauseReason InReason) noexcept
{
    switch (InReason)
    {
        case Script::VmPauseReason::Breakpoint:
            return "breakpoint";
        case Script::VmPauseReason::Step:
            return "step";
        case Script::VmPauseReason::Request:
            return "pause request";
        case Script::VmPauseReason::Entry:
            return "entry";
        case Script::VmPauseReason::None:
            break;
    }
    return "none";
}
}

bool ScriptDebuggerPanel::Initialize(const Scene::ComponentRegistry* InComponents)
{
    (void)InComponents;
    if (!MRuntime.Initialize())
    {
        MStatus = "runtime init failed";
        return false;
    }
    RegisterDefaultScriptNatives(MRuntime);
    MInitialized = true;
    SetSource(kDefaultSource);
    return true;
}

void ScriptDebuggerPanel::SetSource(std::string_view InSource)
{
    const std::size_t ScopedLength = std::min(InSource.size(), static_cast<std::size_t>(kSourceCapacity - 1));
    std::memset(MSource.data(), 0, MSource.size());
    std::memcpy(MSource.data(), InSource.data(), ScopedLength);
}

bool ScriptDebuggerPanel::Compile(const Scene::ComponentRegistry& InComponents)
{
    if (!MInitialized)
    {
        if (!Initialize(&InComponents))
        {
            return false;
        }
    }
    if (MHandle != kScriptNoProgram)
    {
        (void)Runtime().UnloadProgram(MHandle);
        MHandle = kScriptNoProgram;
    }
    MBytes.clear();
    MParsed = {};
    Script::TextCompileOptions ScopedOptions;
    ScopedOptions.sourceHash = Reflect::Fnv1a(MSource.data());
    ScopedOptions.sourceName = "debugger.fbs";
    std::string ScopedError;
    if (!Script::CompileTextScript(MSource.data(), InComponents, ScopedOptions, &MBytes, &ScopedError))
    {
        MStatus = "compile: " + ScopedError;
        return false;
    }
    if (!Script::ParseProgram(MBytes, &MParsed, &ScopedError))
    {
        MStatus = "parse: " + ScopedError;
        return false;
    }
    MHandle = Runtime().LoadProgram(MBytes, &ScopedError);
    if (MHandle == kScriptNoProgram)
    {
        MStatus = "load: " + ScopedError;
        return false;
    }
    MDebug = {};
    MDebug.Enabled = true;
    MLastStop = {};
    for (std::uint32_t ScopedI = 0; ScopedI < MBreakpointLineCount; ++ScopedI)
    {
        std::array<std::uint32_t, kMaxBreakpointPcs> ScopedPcs{};
        const std::uint32_t ScopedPcCount = Script::CollectPcsForLine(MParsed, MBreakpointLines[ScopedI], ScopedPcs);
        for (std::uint32_t ScopedPc = 0; ScopedPc < ScopedPcCount; ++ScopedPc)
        {
            MDebug.SetBreakpoint(ScopedPcs[ScopedPc]);
        }
    }
    char ScopedBuffer[160] = {};
    std::snprintf(ScopedBuffer, sizeof(ScopedBuffer), "compiled: %u instructions, %u spans, %u locals",
                  static_cast<unsigned>(MParsed.header.codeCount),
                  static_cast<unsigned>(MParsed.debug.spans.size()),
                  static_cast<unsigned>(MParsed.debug.variables.size()));
    MStatus = ScopedBuffer;
    return true;
}

std::span<const std::byte> ScriptDebuggerPanel::ProgramBytes() const noexcept
{
    return MBytes;
}

const Script::BoundProgram* ScriptDebuggerPanel::BoundProgram() const noexcept
{
    if (MHandle == kScriptNoProgram)
    {
        return nullptr;
    }
    return Runtime().Program(MHandle);
}

std::uint32_t ScriptDebuggerPanel::CurrentLine() const noexcept
{
    if (!MDebug.Paused || !MParsed.valid)
    {
        return 0;
    }
    const Script::SourceLocation ScopedLocation = Script::LocatePc(MParsed, MDebug.PausePc);
    return ScopedLocation.Valid ? ScopedLocation.Line : 0;
}

std::uint32_t ScriptDebuggerPanel::CurrentGraphNode() const noexcept
{
    if (!MDebug.Paused || !MParsed.valid)
    {
        return kNoGraphNode;
    }
    const Script::SourceLocation ScopedLocation = Script::LocatePc(MParsed, MDebug.PausePc);
    return ScopedLocation.IsGraphNode() ? ScopedLocation.Line : kNoGraphNode;
}

void ScriptDebuggerPanel::DetachProgram() noexcept
{
    MHandle = kScriptNoProgram;
    MDebug = {};
}

bool ScriptDebuggerPanel::AttachProgram(std::uint32_t InHandle)
{
    const std::span<const std::byte> ScopedBytes = Runtime().ProgramBytes(InHandle);
    if (ScopedBytes.empty())
    {
        MStatus = "program is not loaded in the attached runtime";
        return false;
    }
    MBytes.assign(ScopedBytes.begin(), ScopedBytes.end());
    if (!Script::ParseProgram(MBytes, &MParsed, &MStatus))
    {
        return false;
    }
    MHandle = static_cast<std::uint16_t>(InHandle);
    MDebug = {};
    MDebug.Enabled = true;
    for (std::uint32_t ScopedI = 0; ScopedI < MBreakpointLineCount; ++ScopedI)
    {
        std::array<std::uint32_t, kMaxBreakpointPcs> ScopedPcs{};
        const std::uint32_t ScopedPcCount = Script::CollectPcsForLine(MParsed, MBreakpointLines[ScopedI], ScopedPcs);
        for (std::uint32_t ScopedPc = 0; ScopedPc < ScopedPcCount; ++ScopedPc)
        {
            MDebug.SetBreakpoint(ScopedPcs[ScopedPc]);
        }
    }
    MStatus = "attached to live program";
    return true;
}

bool ScriptDebuggerPanel::LoadGraphProgram(std::span<const std::byte> InProgram, std::string_view InName)
{
    if (!MInitialized)
    {
        return false;
    }
    if (InProgram.empty())
    {
        MStatus = "graph program: nothing compiled";
        return false;
    }
    if (MHandle != kScriptNoProgram)
    {
        (void)Runtime().UnloadProgram(MHandle);
        MHandle = kScriptNoProgram;
    }
    MBytes.assign(InProgram.begin(), InProgram.end());
    MParsed = {};
    std::string ScopedError;
    if (!Script::ParseProgram(MBytes, &MParsed, &ScopedError))
    {
        MStatus = "graph parse: " + ScopedError;
        return false;
    }
    MHandle = Runtime().LoadProgram(MBytes, &ScopedError);
    if (MHandle == kScriptNoProgram)
    {
        MStatus = "graph load: " + ScopedError;
        return false;
    }
    MDebug = {};
    MDebug.Enabled = true;
    MLastStop = {};
    for (std::uint32_t ScopedI = 0; ScopedI < MBreakpointLineCount; ++ScopedI)
    {
        std::array<std::uint32_t, kMaxBreakpointPcs> ScopedPcs{};
        const std::uint32_t ScopedPcCount = Script::CollectPcsForLine(MParsed, MBreakpointLines[ScopedI], ScopedPcs);
        for (std::uint32_t ScopedPc = 0; ScopedPc < ScopedPcCount; ++ScopedPc)
        {
            MDebug.SetBreakpoint(ScopedPcs[ScopedPc]);
        }
    }
    char ScopedBuffer[192] = {};
    std::snprintf(ScopedBuffer, sizeof(ScopedBuffer), "loaded %.*s: %u instructions, %u spans",
                  static_cast<int>(InName.size()), InName.data(), static_cast<unsigned>(MParsed.header.codeCount),
                  static_cast<unsigned>(MParsed.debug.spans.size()));
    MStatus = ScopedBuffer;
    return true;
}

bool ScriptDebuggerPanel::CompileAndNativize(const Scene::ComponentRegistry& InComponents)
{
    if (!Compile(InComponents))
    {
        return false;
    }
    if (MAotOptions.outputDirectory.empty() || MAotOptions.includeDirectory.empty() || MAotOptions.compiler.empty())
    {
        MStatus = "AOT: cook options are not configured";
        return false;
    }
    std::string ScopedError;
    if (!MAot.StartProgram(MBytes, MAotOptions, &ScopedError))
    {
        MAotProgramHash = 0;
        MStatus = "AOT: " + ScopedError;
        return false;
    }
    MAotProgramHash = Script::HashProgramBytes(MBytes);
    char ScopedBuffer[160] = {};
    std::snprintf(ScopedBuffer, sizeof(ScopedBuffer), "compiled + nativizing %016llx",
                  static_cast<unsigned long long>(MAotProgramHash));
    MStatus = ScopedBuffer;
    return true;
}

void ScriptDebuggerPanel::PollAot()
{
    const std::uint64_t ScopedRevision = MAot.Revision();
    if (ScopedRevision == MLastAotRevision)
    {
        return;
    }
    const Script::AotBuildResult ScopedSnapshot = MAot.Snapshot();
    if (ScopedSnapshot.state == Script::AotBuildState::Compiling)
    {
        return;
    }
    MLastAotRevision = ScopedRevision;
    MAotResult = ScopedSnapshot;
    if (MAotResult.state == Script::AotBuildState::Succeeded)
    {
        // The manifest is the cook gate's input: program hash -> native library.
        Script::AotProgramRef ScopedRef;
        ScopedRef.Hash = MAotProgramHash;
        ScopedRef.LibraryPath = MAotResult.libraryPath.filename().string();
        ScopedRef.Name = MAotOptions.moduleName;
        const std::array<Script::AotProgramRef, 1> ScopedRefs{ScopedRef};
        const std::string ScopedManifest = Script::AotHost::BuildManifest(ScopedRefs);
        const std::filesystem::path ScopedManifestPath =
            MAotResult.libraryPath.parent_path() / (MAotOptions.moduleName + ".fbyaot");
        std::ofstream ScopedFile(ScopedManifestPath, std::ios::binary | std::ios::trunc);
        bool ScopedManifestWritten = false;
        if (ScopedFile)
        {
            ScopedFile << ScopedManifest;
            ScopedFile.flush();
            ScopedManifestWritten = static_cast<bool>(ScopedFile);
        }
        MStatus = "nativized: " + MAotResult.libraryPath.filename().string() +
                  (ScopedManifestWritten ? " (+ manifest)" : " (manifest write failed)");
    }
    else if (MAotResult.state == Script::AotBuildState::Failed)
    {
        MStatus = "AOT failed: " + MAotResult.error;
    }
}

bool ScriptDebuggerPanel::RunOnce(Ecs::Registry& InRegistry, Entity InEntity)
{
    const Script::BoundProgram* ScopedBound = BoundProgram();
    if (ScopedBound == nullptr)
    {
        MStatus = "no program loaded";
        return false;
    }
    Script::VmContext ScopedContext{InRegistry};
    ScopedContext.dt = 1.0f / 60.0f;
    ScopedContext.frameIndex = MFrameIndex++;
    ScopedContext.rng = &Runtime().Rng();
    for (std::uint32_t ScopedGuard = 0; ScopedGuard < 64; ++ScopedGuard)
    {
        MLastStop = Runtime().Vm().RunDebug(*ScopedBound, Script::ScriptEvent::Update, InEntity, ScopedContext, MDebug);
        if (MLastStop.Halt == Script::VmHalt::Paused)
        {
            const Script::SourceLocation ScopedLocation = Script::LocatePc(MParsed, MLastStop.Pc);
            const std::uint32_t ScopedBreakpoint = BreakpointIndex(ScopedLocation.Line);
            if (ScopedBreakpoint < MBreakpointLogs.size() && MBreakpointLogs[ScopedBreakpoint])
            {
                if (MLogCount < MLogLines.size())
                {
                    char ScopedBuffer[96] = {};
                    std::snprintf(ScopedBuffer, sizeof(ScopedBuffer), "line %u pc %u",
                                  static_cast<unsigned>(ScopedLocation.Line), static_cast<unsigned>(MLastStop.Pc));
                    MLogLines[MLogCount++] = ScopedBuffer;
                }
                MDebug.PauseRequested = false;
                MDebug.StepBudget = 0;
                continue;
            }
            if (!ConditionPasses(ScopedLocation.Line))
            {
                MDebug.PauseRequested = false;
                MDebug.StepBudget = 0;
                continue;
            }
            char ScopedBuffer[160] = {};
            std::snprintf(ScopedBuffer, sizeof(ScopedBuffer), "paused at %s, pc %u, line %u",
                          PauseReasonName(MLastStop.Reason), static_cast<unsigned>(MLastStop.Pc),
                          static_cast<unsigned>(ScopedLocation.Line));
            MStatus = ScopedBuffer;
            return true;
        }
        if (MLastStop.Halt == Script::VmHalt::Faulted)
        {
        const Script::SourceLocation ScopedLocation = Script::LocatePc(MParsed, MLastStop.Fault.pc);
        char ScopedBuffer[200] = {};
        std::snprintf(ScopedBuffer, sizeof(ScopedBuffer), "fault %s at pc %u (line %u)",
                      Script::VmFault::Name(MLastStop.Fault.code), static_cast<unsigned>(MLastStop.Fault.pc),
                      static_cast<unsigned>(ScopedLocation.Line));
            MStatus = ScopedBuffer;
            return false;
        }
        MStatus = "completed";
        return true;
    }
    MStatus = "conditional breakpoint loop limit";
    return false;
}

void ScriptDebuggerPanel::Resume(Ecs::Registry& InRegistry, Entity InEntity)
{
    MDebug.PauseRequested = false;
    MDebug.StepBudget = 0;
    (void)RunOnce(InRegistry, InEntity);
}

bool ScriptDebuggerPanel::HasLineBreakpoint(std::uint32_t InLine) const noexcept
{
    for (std::uint32_t ScopedI = 0; ScopedI < MBreakpointLineCount; ++ScopedI)
    {
        if (MBreakpointLines[ScopedI] == InLine)
        {
            return true;
        }
    }
    return false;
}

void ScriptDebuggerPanel::ToggleBreakpoint(std::uint32_t InLine)
{
    if (InLine == 0)
    {
        return;
    }
    std::array<std::uint32_t, kMaxBreakpointPcs> ScopedPcs{};
    const std::uint32_t ScopedPcCount = MParsed.valid ? Script::CollectPcsForLine(MParsed, InLine, ScopedPcs) : 0;
    for (std::uint32_t ScopedI = 0; ScopedI < MBreakpointLineCount; ++ScopedI)
    {
        if (MBreakpointLines[ScopedI] != InLine)
        {
            continue;
        }
        for (std::uint32_t ScopedPc = 0; ScopedPc < ScopedPcCount; ++ScopedPc)
        {
            MDebug.ClearBreakpoint(ScopedPcs[ScopedPc]);
        }
        for (std::uint32_t ScopedJ = ScopedI; ScopedJ + 1 < MBreakpointLineCount; ++ScopedJ)
        {
            MBreakpointLines[ScopedJ] = MBreakpointLines[ScopedJ + 1];
            MBreakpointConditions[ScopedJ] = MBreakpointConditions[ScopedJ + 1];
            MBreakpointLogs[ScopedJ] = MBreakpointLogs[ScopedJ + 1];
        }
        --MBreakpointLineCount;
        return;
    }
    if (MBreakpointLineCount >= MBreakpointLines.size())
    {
        return;
    }
    MBreakpointConditions[MBreakpointLineCount] = {};
    MBreakpointLogs[MBreakpointLineCount] = false;
    MBreakpointLines[MBreakpointLineCount++] = InLine;
    for (std::uint32_t ScopedPc = 0; ScopedPc < ScopedPcCount; ++ScopedPc)
    {
        MDebug.SetBreakpoint(ScopedPcs[ScopedPc]);
    }
}

void ScriptDebuggerPanel::EvaluateWatch()
{
    std::string_view ScopedText(MWatchInput.data());
    while (!ScopedText.empty() && std::isspace(static_cast<unsigned char>(ScopedText.front())) != 0)
    {
        ScopedText.remove_prefix(1);
    }
    while (!ScopedText.empty() && std::isspace(static_cast<unsigned char>(ScopedText.back())) != 0)
    {
        ScopedText.remove_suffix(1);
    }
    if (ScopedText.empty())
    {
        MWatchResult = "-";
        return;
    }
    std::uint32_t ScopedRegister = 0;
    if (ScopedText.front() == 'r')
    {
        const auto ScopedResult =
            std::from_chars(ScopedText.data() + 1, ScopedText.data() + ScopedText.size(), ScopedRegister);
        if (ScopedResult.ec == std::errc{} && ScopedResult.ptr == ScopedText.data() + ScopedText.size() &&
            ScopedRegister < MParsed.header.registerHighWater)
        {
            char ScopedBuffer[64] = {};
            std::snprintf(ScopedBuffer, sizeof(ScopedBuffer), "%.6f", static_cast<double>(Runtime().Vm().F(ScopedRegister)));
            MWatchResult = ScopedBuffer;
            return;
        }
    }
    const std::string ScopedNumber(ScopedText);
    char* ScopedNumberEnd = nullptr;
    const float ScopedLiteral = std::strtof(ScopedNumber.c_str(), &ScopedNumberEnd);
    if (ScopedNumberEnd == ScopedNumber.c_str() + ScopedNumber.size())
    {
        MWatchResult = std::string(ScopedText);
        return;
    }
    for (const Script::DebugVariable& ScopedVariable : MParsed.debug.variables)
    {
        if (Script::VariableName(MParsed, ScopedVariable.Register) == ScopedText)
        {
            char ScopedBuffer[64] = {};
            std::snprintf(ScopedBuffer, sizeof(ScopedBuffer), "%.6f",
                          static_cast<double>(Runtime().Vm().F(ScopedVariable.Register)));
            MWatchResult = ScopedBuffer;
            return;
        }
    }
    MWatchResult = "unknown register or local";
}

std::uint32_t ScriptDebuggerPanel::BreakpointIndex(std::uint32_t InLine) const noexcept
{
    for (std::uint32_t ScopedI = 0; ScopedI < MBreakpointLineCount; ++ScopedI)
    {
        if (MBreakpointLines[ScopedI] == InLine)
        {
            return ScopedI;
        }
    }
    return 0xFFFFFFFFu;
}

bool ScriptDebuggerPanel::ConditionPasses(std::uint32_t InLine)
{
    for (std::uint32_t ScopedI = 0; ScopedI < MBreakpointLineCount; ++ScopedI)
    {
        if (MBreakpointLines[ScopedI] != InLine)
        {
            continue;
        }
        const std::string_view ScopedText(MBreakpointConditions[ScopedI].data());
        if (ScopedText.empty())
        {
            return true;
        }
        Script::DebuggerCondition ScopedCondition;
        if (!Script::ParseDebuggerCondition(ScopedText, &ScopedCondition))
        {
            return true;
        }
        if (ScopedCondition.Register >= MParsed.header.registerHighWater)
        {
            return true;
        }
        return Script::EvaluateDebuggerCondition(ScopedCondition, Runtime().Vm().F(ScopedCondition.Register));
    }
    return true;
}

bool ScriptDebuggerPanel::SetBreakpointLine(std::uint32_t InLine) noexcept
{
    if (InLine == 0 || HasLineBreakpoint(InLine) || MBreakpointLineCount >= MBreakpointLines.size())
    {
        return false;
    }
    ToggleBreakpoint(InLine);
    return true;
}

void ScriptDebuggerPanel::ClearBreakpoints() noexcept
{
    while (MBreakpointLineCount > 0)
    {
        ToggleBreakpoint(MBreakpointLines[0]);
    }
    MDebug.ClearBreakpoints();
}

void ScriptDebuggerPanel::DrawToolbar(Ecs::Registry& InRegistry, const Scene::ComponentRegistry& InComponents,
                                      Entity InSelected, const VisualScriptPanel* InGraphPanel)
{
    PollAot();
    if (ImGui::Button("Compile"))
    {
        (void)Compile(InComponents);
    }
    ImGui::SameLine();
    if (ImGui::Button("Compile + Nativize"))
    {
        (void)CompileAndNativize(InComponents);
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(BoundProgram() == nullptr);
    if (ImGui::Button("Continue"))
    {
        Resume(InRegistry, InSelected);
    }
    ImGui::SameLine();
    if (ImGui::Button("Step"))
    {
        MDebug.PauseRequested = false;
        MDebug.StepBudget = 1;
        (void)RunOnce(InRegistry, InSelected);
    }
    ImGui::SameLine();
    if (ImGui::Button("Pause"))
    {
        MDebug.PauseRequested = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset"))
    {
        MDebug.ClearPause();
        MLastStop = {};
        MStatus = "reset";
    }
    ImGui::EndDisabled();
    if (InGraphPanel != nullptr)
    {
        ImGui::SameLine();
        const Script::LogicCompileResult& ScopedGraph = InGraphPanel->CompileResult();
        ImGui::BeginDisabled(ScopedGraph.program.empty());
        if (ImGui::Button("Debug graph"))
        {
            (void)LoadGraphProgram(ScopedGraph.program, "LogicGraph");
        }
        ImGui::EndDisabled();
    }
    ImGui::SameLine();
    ImGui::Checkbox("Live", &MLive);
    ImGui::SameLine();
    ImGui::TextDisabled("%s", MStatus.c_str());

    if (MLive && BoundProgram() != nullptr && !MDebug.Paused)
    {
        (void)RunOnce(InRegistry, InSelected);
    }
}

void ScriptDebuggerPanel::DrawSourceView(const Scene::ComponentRegistry& InComponents)
{
    (void)InComponents;
    ImGui::PushID("source_edit");
    ImGui::InputTextMultiline("##script", MSource.data(), MSource.size(), ImVec2(-1.0f, 120.0f));
    ImGui::PopID();

    ImGui::Separator();
    ImGui::TextUnformatted("Source");
    ImGui::SameLine();
    ImGui::TextDisabled("(click a line number to toggle a breakpoint)");

    std::array<std::string_view, kMaxSourceLines> ScopedLines{};
    std::array<std::uint32_t, kMaxSourceLines> ScopedNumbers{};
    const std::uint32_t ScopedCount = SplitLines(MSource.data(), ScopedLines, ScopedNumbers);
    const std::uint32_t ScopedCurrentLine = CurrentLine();

    ImGui::BeginChild("##script_lines", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
    for (std::uint32_t ScopedI = 0; ScopedI < ScopedCount; ++ScopedI)
    {
        const std::uint32_t ScopedLine = ScopedNumbers[ScopedI];
        const bool ScopedHasBreakpoint = HasLineBreakpoint(ScopedLine);
        ImGui::PushID(static_cast<int>(ScopedLine));
        if (ScopedHasBreakpoint)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 90, 120, 255));
        }
        if (ImGui::SmallButton(ScopedHasBreakpoint ? "x" : "-"))
        {
            ToggleBreakpoint(ScopedLine);
        }
        if (ScopedHasBreakpoint)
        {
            ImGui::PopStyleColor();
        }
        ImGui::SameLine();
        if (ScopedLine == ScopedCurrentLine)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 230, 120, 255));
        }
        char ScopedNumber[16] = {};
        std::snprintf(ScopedNumber, sizeof(ScopedNumber), "%4u", static_cast<unsigned>(ScopedLine));
        ImGui::TextUnformatted(ScopedNumber);
        ImGui::SameLine();
        ImGui::TextUnformatted(ScopedLines[ScopedI].data(), ScopedLines[ScopedI].data() + ScopedLines[ScopedI].size());
        if (ScopedLine == ScopedCurrentLine)
        {
            ImGui::PopStyleColor();
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
}

void ScriptDebuggerPanel::DrawBreakpoints()
{
    ImGui::Separator();
    ImGui::Text("Breakpoints: %u", static_cast<unsigned>(MBreakpointLineCount));
    for (std::uint32_t ScopedI = 0; ScopedI < MBreakpointLineCount; ++ScopedI)
    {
        ImGui::PushID(static_cast<int>(ScopedI));
        ImGui::Text("line %u", static_cast<unsigned>(MBreakpointLines[ScopedI]));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(130.0f);
        (void)ImGui::InputText("##condition", MBreakpointConditions[ScopedI].data(),
                              MBreakpointConditions[ScopedI].size());
        ImGui::SameLine();
        (void)ImGui::Checkbox("log", &MBreakpointLogs[ScopedI]);
        ImGui::SameLine();
        if (ImGui::SmallButton("remove"))
        {
            ToggleBreakpoint(MBreakpointLines[ScopedI]);
        }
        ImGui::PopID();
    }
    if (ImGui::SmallButton("clear all"))
    {
        while (MBreakpointLineCount > 0)
        {
            ToggleBreakpoint(MBreakpointLines[0]);
        }
        MDebug.ClearBreakpoints();
    }
}

void ScriptDebuggerPanel::DrawState(Ecs::Registry& InRegistry, const Scene::ComponentRegistry& InComponents,
                                    Entity InSelected)
{
    const Script::BoundProgram* ScopedBound = BoundProgram();
    if (ScopedBound == nullptr)
    {
        ImGui::TextDisabled("compile a program to inspect it");
        return;
    }

    ImGui::Separator();
    if (ImGui::BeginTabBar("##debug_tabs"))
    {
        if (ImGui::BeginTabItem("Locals"))
        {
            const std::uint32_t ScopedRegisterCount = MParsed.header.registerHighWater;
            std::uint32_t ScopedShown = 0;
            for (const Script::DebugVariable& ScopedVariable : MParsed.debug.variables)
            {
                const std::string_view ScopedName = Script::VariableName(MParsed, ScopedVariable.Register);
                ImGui::Text("%.*s = %.4f (r%u)", static_cast<int>(ScopedName.size()), ScopedName.data(),
                            static_cast<double>(Runtime().Vm().F(ScopedVariable.Register)),
                            static_cast<unsigned>(ScopedVariable.Register));
                ++ScopedShown;
            }
            if (ScopedShown == 0)
            {
                ImGui::TextDisabled("no named locals; %u registers in use", static_cast<unsigned>(ScopedRegisterCount));
            }
            ImGui::Separator();
            ImGui::SetNextItemWidth(180.0f);
            (void)ImGui::InputText("watch", MWatchInput.data(), MWatchInput.size());
            ImGui::SameLine();
            if (ImGui::Button("Evaluate"))
            {
                EvaluateWatch();
            }
            ImGui::SameLine();
            ImGui::Text("= %s", MWatchResult.c_str());
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Self fields"))
        {
            std::array<Script::DebugField, 32> ScopedFields{};
            const std::uint32_t ScopedFieldCount = Script::DescribeFields(MParsed, InComponents, ScopedFields);
            if (ScopedFieldCount == 0)
            {
                ImGui::TextDisabled("the program touches no component fields");
            }
            for (std::uint32_t ScopedI = 0; ScopedI < ScopedFieldCount; ++ScopedI)
            {
                const Script::DebugField& ScopedField = ScopedFields[ScopedI];
                float ScopedValue = 0.0f;
                const bool ScopedValid = Script::ReadFieldValue(*ScopedBound, InRegistry, InSelected,
                                                                 static_cast<std::uint32_t>(ScopedI), &ScopedValue);
                char ScopedLabel[96] = {};
                std::snprintf(ScopedLabel, sizeof(ScopedLabel), "%.*s.%.*s", static_cast<int>(ScopedField.Component.size()),
                              ScopedField.Component.data(), static_cast<int>(ScopedField.Field.size()),
                              ScopedField.Field.data());
                ImGui::PushID(static_cast<int>(ScopedI));
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::DragFloat(ScopedLabel, &ScopedValue, 0.01f, -10000.0f, 10000.0f, "%.4f") && ScopedValid)
                {
                    (void)Script::WriteFieldValue(*ScopedBound, InRegistry, InSelected,
                                                  static_cast<std::uint32_t>(ScopedI), ScopedValue);
                }
                ImGui::PopID();
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Stack"))
        {
            const Script::DebugFrame ScopedFrame = Script::MakeFrame(MParsed, Script::ScriptEvent::Update, InSelected,
                                                                     MDebug.PausePc);
            ImGui::Text("Update  entity %u  pc %u", static_cast<unsigned>(static_cast<std::uint32_t>(InSelected)),
                        static_cast<unsigned>(ScopedFrame.Pc));
            ImGui::TextDisabled("line %u, column %u", static_cast<unsigned>(ScopedFrame.Location.Line),
                                static_cast<unsigned>(ScopedFrame.Location.Column));
            for (std::uint32_t ScopedI = MDebug.CallDepth; ScopedI > 0; --ScopedI)
            {
                const Script::VmCallFrame& ScopedCall = MDebug.CallStack[ScopedI - 1];
                const std::uint32_t ScopedPc = ScopedI == MDebug.CallDepth ? MDebug.PausePc : ScopedCall.ReturnPc;
                const Script::DebugFrame ScopedCallFrame =
                    Script::MakeFrame(MParsed, Script::ScriptEvent::Update, InSelected, ScopedPc);
                ImGui::Text("fn #%u  base r%u  return pc %u", static_cast<unsigned>(ScopedCall.Function),
                            static_cast<unsigned>(ScopedCall.Base), static_cast<unsigned>(ScopedCall.ReturnPc));
                ImGui::TextDisabled("line %u, column %u", static_cast<unsigned>(ScopedCallFrame.Location.Line),
                                    static_cast<unsigned>(ScopedCallFrame.Location.Column));
            }
            if (MDebug.CallDepth == 0)
            {
                ImGui::TextDisabled("event frame");
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Program"))
        {
            ImGui::Text("source: %.*s", static_cast<int>(Script::SourceName(MParsed).size()),
                        Script::SourceName(MParsed).data());
            ImGui::Text("instructions: %u", static_cast<unsigned>(MParsed.header.codeCount));
            ImGui::Text("registers: %u", static_cast<unsigned>(MParsed.header.registerHighWater));
            ImGui::Text("spans: %zu", MParsed.debug.spans.size());
            ImGui::Text("executed: %llu",
                        static_cast<unsigned long long>(Runtime().Vm().InstructionsExecuted()));
            ImGui::Text("pauses: %u", static_cast<unsigned>(MDebug.PauseCount));
            if (MLastStop.Faulted())
            {
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "fault: %s",
                                   Script::VmFault::Name(MLastStop.Fault.code));
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

bool ScriptDebuggerPanel::IsGraphProgram() const noexcept
{
    if (!MParsed.debug.present)
    {
        return false;
    }
    for (const Script::DebugSpan& ScopedSpan : MParsed.debug.spans)
    {
        if ((ScopedSpan.Flags & Script::DebugSpanGraphNode) != 0)
        {
            return true;
        }
    }
    return false;
}

void ScriptDebuggerPanel::DrawGraphNodes()
{
    ImGui::TextUnformatted("Graph program");
    ImGui::SameLine();
    ImGui::TextDisabled("(click a node to toggle a breakpoint)");
    ImGui::BeginChild("##graph_nodes", ImVec2(0.0f, 160.0f), ImGuiChildFlags_Borders);
    std::uint32_t ScopedLastNode = ScriptDebuggerPanel::kNoGraphNode;
    std::uint32_t ScopedIndex = 0;
    const std::uint32_t ScopedCurrentNode = CurrentGraphNode();
    for (const Script::DebugSpan& ScopedSpan : MParsed.debug.spans)
    {
        if ((ScopedSpan.Flags & Script::DebugSpanGraphNode) == 0 || ScopedSpan.Line == ScopedLastNode)
        {
            continue;
        }
        ScopedLastNode = ScopedSpan.Line;
        ImGui::PushID(static_cast<int>(ScopedIndex++));
        char ScopedLabel[64] = {};
        std::snprintf(ScopedLabel, sizeof(ScopedLabel), "node %u (pc %u)", static_cast<unsigned>(ScopedSpan.Line),
                      static_cast<unsigned>(ScopedSpan.Pc));
        const bool ScopedBreakpoint = HasLineBreakpoint(ScopedSpan.Line);
        if (ScopedSpan.Line == ScopedCurrentNode)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 220, 120, 255));
        }
        if (ImGui::Selectable(ScopedLabel, ScopedBreakpoint))
        {
            ToggleBreakpoint(ScopedSpan.Line);
        }
        if (ScopedSpan.Line == ScopedCurrentNode)
        {
            ImGui::PopStyleColor();
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
}

void ScriptDebuggerPanel::Draw(Ecs::Registry& InRegistry, const Scene::ComponentRegistry& InComponents,
                               Entity InSelected, bool* InOpen, const VisualScriptPanel* InGraphPanel)
{
    if (!MInitialized)
    {
        (void)Initialize(&InComponents);
    }
    bool ScopedOpen = InOpen != nullptr ? *InOpen : MOpen;
    ImGui::SetNextWindowSize(ImVec2(720.0f, 640.0f), ImGuiCond_FirstUseEver);
    if (!BeginEditorPanel("Script Debugger", &ScopedOpen))
    {
        EndEditorPanel();
        if (InOpen != nullptr)
        {
            *InOpen = ScopedOpen;
        }
        return;
    }
    MOpen = ScopedOpen;

    DrawToolbar(InRegistry, InComponents, InSelected, InGraphPanel);
    DrawBreakpoints();
    ImGui::Separator();
    if (IsGraphProgram())
    {
        DrawGraphNodes();
    }
    else
    {
        DrawSourceView(InComponents);
    }
    DrawState(InRegistry, InComponents, InSelected);

    EndEditorPanel();
    if (InOpen != nullptr)
    {
        *InOpen = ScopedOpen;
    }
}
}
