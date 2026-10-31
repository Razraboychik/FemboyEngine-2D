// FemboyEngine2D - VisualScript editor (LogicGraph) + AOT cook surface.
#pragma once

#include "Editor/Panels/NodeCanvasPanel.hpp"
#include "Engine/Script/AotBuilder.hpp"
#include "Engine/Script/GraphCompiler.hpp"
#include "Engine/Script/TextScript.hpp"
#include "Engine/ECS/ComponentRegistry.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <type_traits>
#include <vector>

namespace Fby::Editor
{

class EditorFonts;

// The editor document is the runtime LogicGraph plus session/editor layout
struct LogicGraphDocument
{
    Script::LogicGraph graph{};
    std::array<Fby::Vector2D, Script::kMaxLogicNodes> nodePositions{};
};

static_assert(std::is_trivially_copyable_v<LogicGraphDocument>);
static_assert(sizeof(LogicGraphDocument) <= 32768, "LogicGraphDocument must fit one CommandStack buffer");

class VisualScriptPanel
{
public:
    VisualScriptPanel() = default;
    ~VisualScriptPanel() = default;

    VisualScriptPanel(const VisualScriptPanel&) = delete;
    VisualScriptPanel& operator=(const VisualScriptPanel&) = delete;

    [[nodiscard]] bool Initialize(CommandStack& InCommands, const Scene::ComponentRegistry* InComponents,
                                  const Script::AotBuildOptions& InAotOptions = {});
    void Shutdown();

    void Draw(const char* InWindowTitle, bool* InOpen);
    void SetFonts(const EditorFonts* InFonts) noexcept;

    // PIE path: graph -> canonical bytecode. Safe to call every edit; the panel
    [[nodiscard]] bool Compile();
    [[nodiscard]] bool CompileText();
    // Release path: graph -> C++26 -> native library on a background jthread.
    [[nodiscard]] bool StartAotBuild();

    [[nodiscard]] Script::LogicGraph& Graph() noexcept
    {
        return MDocument.graph;
    }
    [[nodiscard]] const Script::LogicGraph& Graph() const noexcept
    {
        return MDocument.graph;
    }
    [[nodiscard]] LogicGraphDocument& Document() noexcept
    {
        return MDocument;
    }
    [[nodiscard]] const Script::LogicCompileResult& CompileResult() const noexcept
    {
        return MCompiled;
    }
    [[nodiscard]] const Script::AotBuildResult& AotResult() const noexcept
    {
        return MAotResult;
    }
    [[nodiscard]] Script::AotBuilder& Aot() noexcept
    {
        return MAot;
    }
    [[nodiscard]] std::uint16_t Codec() const noexcept
    {
        return MCodec;
    }
    // Two compile paths: "Compile" prepares bytecode for the PIE VM, "Compile +
    // Nativize" additionally cooks the program into a native module and writes
    // its `.fbyaot` manifest entry.
    [[nodiscard]] bool CompileAndNativize();
    [[nodiscard]] std::uint64_t AotProgramHash() const noexcept
    {
        return MAotProgramHash;
    }
    [[nodiscard]] NodeCanvasPanel& Canvas() noexcept
    {
        return MCanvas;
    }
    [[nodiscard]] const char* Status() const noexcept
    {
        return MStatus.data();
    }
    [[nodiscard]] bool Initialized() const noexcept
    {
        return MInitialized;
    }

    // Debugger hook: highlights one node on the canvas while it is the paused
    // instruction's owner. `kNoNode` clears the highlight.
    static constexpr std::uint32_t kNoDebugNode = 0xFFFFFFFFu;
    void SetDebugHighlight(std::uint32_t InNodeId) noexcept
    {
        MDebugNode = InNodeId;
    }
    [[nodiscard]] std::uint32_t DebugHighlight() const noexcept
    {
        return MDebugNode;
    }

private:
    static void SetStatusText(char* InDestination, std::size_t InCapacity, const std::string& InText) noexcept;
    void BuildDefaultGraph();
    void WriteAotManifest();
    [[nodiscard]] std::uint16_t FindOrAddField(std::string_view InComponentName, std::string_view InFieldName);
    [[nodiscard]] std::uint16_t DefaultField();
    static bool RebuildWithoutNodes(LogicGraphDocument& InDocument, std::span<const std::uint16_t> InNodes,
                                    std::string* InError);
    void DrawNodeInspector();
    void DrawAotStatus();
    void PollAot();

    static std::uint32_t ModelNodeCount(void* InUser) noexcept;
    static const char* ModelNodeTitle(void* InUser, std::uint16_t InNode) noexcept;
    static ImVec2 ModelNodePosition(void* InUser, std::uint16_t InNode) noexcept;
    static void ModelSetNodePosition(void* InUser, std::uint16_t InNode, ImVec2 InPosition) noexcept;
    static std::uint8_t ModelInputCount(void* InUser, std::uint16_t InNode) noexcept;
    static const char* ModelInputName(void* InUser, std::uint16_t InNode, std::uint8_t InPin) noexcept;
    static std::uint8_t ModelOutputCount(void* InUser, std::uint16_t InNode) noexcept;
    static const char* ModelOutputName(void* InUser, std::uint16_t InNode, std::uint8_t InPin) noexcept;
    static std::uint32_t ModelLinkCount(void* InUser) noexcept;
    static void ModelLinkEndpoints(void* InUser, std::uint32_t InLink, std::uint16_t* InFromNode,
                                   std::uint8_t* InFromPin, std::uint16_t* InToNode, std::uint8_t* InToPin) noexcept;
    static bool ModelConnect(void* InUser, std::uint16_t InToNode, std::uint8_t InToPin, std::uint16_t InFromNode,
                             std::uint8_t InFromPin, std::string* InError);
    static void ModelDisconnect(void* InUser, std::uint16_t InToNode, std::uint8_t InToPin) noexcept;
    static bool ModelDeleteNodes(void* InUser, std::span<const std::uint16_t> InNodes, std::string* InError);
    static std::uint32_t ModelAddableCount(void* InUser) noexcept;
    static const char* ModelAddableName(void* InUser, std::uint32_t InAddable) noexcept;
    static bool ModelAddNode(void* InUser, std::uint32_t InAddable, ImVec2 InPosition, std::uint16_t* OutNode,
                             std::string* InError);
    static void ModelDrawNodeBody(void* InUser, std::uint16_t InNode) noexcept;
    static void ModelDrawPanelTools(void* InUser) noexcept;

    LogicGraphDocument MDocument{};
    NodeCanvasPanel MCanvas{};
    const EditorFonts* MFonts = nullptr;
    const Scene::ComponentRegistry* MComponents = nullptr;
    Script::AotBuilder MAot{};
    Script::AotBuildOptions MAotOptions{};
    Script::LogicCompileResult MCompiled{};
    Script::AotBuildResult MAotResult{};
    std::uint16_t MCodec = 0xFFFFu;
    std::uint16_t MDefaultField = 0xFFFFu;
    std::uint64_t MLastCompiledHash = 0;
    std::uint64_t MLastAotRevision = 0;
    std::uint64_t MAotProgramHash = 0;
    std::array<char, 8192> MTextSource{};
    std::vector<std::byte> MTextProgram{};
    bool MTextMode = false;
    bool MInitialized = false;
    bool MAutoCompile = true;
    std::uint32_t MDebugNode = kNoDebugNode;
    std::array<char, kCanvasStatusBytes> MStatus{};
};
}
