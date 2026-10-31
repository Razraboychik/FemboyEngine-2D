// FemboyEngine2D - FemboyScript debugger panel: source, breakpoints, locals.
#pragma once

#include "Engine/ECS/Schedule.hpp"
#include "Engine/Script/AotBuilder.hpp"
#include "Engine/Script/Bytecode.hpp"
#include "Engine/Script/ScriptSystem.hpp"
#include "Engine/Script/Vm.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace Fby::Scene
{
class ComponentRegistry;
}

namespace Fby::Editor
{

class VisualScriptPanel;

// Editor-side debugger for the FemboyScript VM. It owns its own ScriptRuntime so
// a paused session never disturbs the scripts the game world is running.
class ScriptDebuggerPanel
{
public:
    static constexpr std::uint32_t kSourceCapacity = 8192;
    static constexpr std::uint32_t kMaxBreakpointPcs = 256;

    ScriptDebuggerPanel() = default;

    [[nodiscard]] bool Initialize(const Scene::ComponentRegistry* InComponents);

    void Draw(Ecs::Registry& InRegistry, const Scene::ComponentRegistry& InComponents, Entity InSelected, bool* InOpen,
              const VisualScriptPanel* InGraphPanel = nullptr);

    [[nodiscard]] bool& Open() noexcept
    {
        return MOpen;
    }
    [[nodiscard]] const std::string& Status() const noexcept
    {
        return MStatus;
    }
    [[nodiscard]] bool Paused() const noexcept
    {
        return MDebug.Paused;
    }
    [[nodiscard]] std::uint32_t CurrentLine() const noexcept;
    // Graph programs carry node spans; this is the node that owns the paused pc.
    // Returns kNoGraphNode when nothing is paused or the program is textual.
    static constexpr std::uint32_t kNoGraphNode = 0xFFFFFFFFu;
    [[nodiscard]] std::uint32_t CurrentGraphNode() const noexcept;
    [[nodiscard]] bool IsGraphProgram() const noexcept;
    [[nodiscard]] std::uint16_t ProgramHandle() const noexcept
    {
        return MHandle;
    }

    // Loads bytecode compiled by the LogicGraph panel, so the same debugger can
    // step graph programs and drive the canvas highlight.
    [[nodiscard]] bool LoadGraphProgram(std::span<const std::byte> InProgram, std::string_view InName);

    // Test seam: compile the current source without an ImGui context.
    [[nodiscard]] bool Compile(const Scene::ComponentRegistry& InComponents);
    // Test seam: run until a breakpoint, a fault or Halt.
    [[nodiscard]] bool RunOnce(Ecs::Registry& InRegistry, Entity InEntity);
    // Test seam: line breakpoints without the source view.
    [[nodiscard]] bool SetBreakpointLine(std::uint32_t InLine) noexcept;
    void ClearBreakpoints() noexcept;

    void SetSource(std::string_view InSource);

    // The second compile path: text -> bytecode -> native module in one action.
    // The editor passes the same cook options the LogicGraph panel uses.
    void SetAotOptions(const Script::AotBuildOptions& InAotOptions) noexcept
    {
        MAotOptions = InAotOptions;
    }
    [[nodiscard]] bool CompileAndNativize(const Scene::ComponentRegistry& InComponents);
    [[nodiscard]] Script::AotBuildState AotState() const noexcept
    {
        return MAot.State();
    }
    [[nodiscard]] std::uint64_t AotProgramHash() const noexcept
    {
        return MAotProgramHash;
    }
    [[nodiscard]] const Script::AotBuildResult& AotResult() const noexcept
    {
        return MAotResult;
    }

private:
    void PollAot();
    void DrawToolbar(Ecs::Registry& InRegistry, const Scene::ComponentRegistry& InComponents, Entity InSelected,
                     const VisualScriptPanel* InGraphPanel);
    void DrawSourceView(const Scene::ComponentRegistry& InComponents);
    void DrawGraphNodes();
    void DrawState(Ecs::Registry& InRegistry, const Scene::ComponentRegistry& InComponents, Entity InSelected);
    void DrawBreakpoints();
    void ToggleBreakpoint(std::uint32_t InLine);
    void Resume(Ecs::Registry& InRegistry, Entity InEntity);
    [[nodiscard]] bool HasLineBreakpoint(std::uint32_t InLine) const noexcept;

    [[nodiscard]] std::span<const std::byte> ProgramBytes() const noexcept;
    [[nodiscard]] const Script::BoundProgram* BoundProgram() const noexcept;

    std::array<char, kSourceCapacity> MSource{};
    std::string MStatus{"ready"};
    std::vector<std::byte> MBytes;
    Script::CompiledProgram MParsed{};
    ScriptRuntime MRuntime{};
    Script::VmDebugState MDebug{};
    Script::VmStopInfo MLastStop{};
    // Breakpoints are authored per source line and re-resolved to program
    // counters after every compile, so editing the source keeps them.
    std::array<std::uint32_t, 64> MBreakpointLines{};
    std::uint32_t MBreakpointLineCount = 0;
    std::uint16_t MHandle = kScriptNoProgram;
    std::uint32_t MFrameIndex = 0;
    Script::AotBuilder MAot{};
    Script::AotBuildOptions MAotOptions{};
    Script::AotBuildResult MAotResult{};
    std::uint64_t MLastAotRevision = 0;
    std::uint64_t MAotProgramHash = 0;
    bool MInitialized = false;
    bool MOpen = true;
    bool MLive = false;
};
}
