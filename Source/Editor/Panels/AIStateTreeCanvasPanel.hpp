// FemboyEngine2D - AI State Tree canvas (ImNodes) and its DOD compiler.
#pragma once

#include "Editor/Infrastructure/CommandStack.hpp"
#include "Engine/ECS/StateTree.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <type_traits>

struct ImNodesContext;

namespace Fby::Editor
{

inline constexpr std::uint32_t kAIMaxStateTreeNodes = kMaxStateTreeNodes;
inline constexpr std::uint32_t kAIMaxStateTreeTransitions = kMaxStateTreeTransitions;
inline constexpr std::uint32_t kAIMaxStateTreeDepth = kMaxStateTreeDepth;
inline constexpr std::uint32_t kAIStateTreeStatusBytes = 320;

// Canvas-only document. StateTreeDefinition is already a fixed-capacity POD;
struct AIStateTreeDocument
{
    StateTreeDefinition Tree{};
    std::array<Vector2D, kMaxStateTreeNodes> Positions{};
};

static_assert(std::is_trivially_copyable_v<AIStateTreeDocument>);
static_assert(sizeof(AIStateTreeDocument) <= kMaxTrackedComponentBytes,
              "AIStateTreeDocument must fit one CommandStack external buffer");

// Result of a successful CompileTree(). The runtime consumes `tree`; `map`
struct AIStateTreeCompiled
{
    StateTreeDefinition Tree{};
    std::array<std::uint16_t, kMaxStateTreeNodes> EditorIndex{};
    std::uint32_t LeafCount = 0;
    std::uint32_t MaxDepth = 0;
};

// Field-wise comparison used by the inspector. Both functions read only the
[[nodiscard]] bool AIStateTreeEvaluatorFieldsEqual(const StateTreeEvaluator& InA,
                                                   const StateTreeEvaluator& InB) noexcept;
[[nodiscard]] bool AIStateTreeTaskFieldsEqual(const StateTreeTask& InA, const StateTreeTask& InB) noexcept;

[[nodiscard]] const char* AIStateTreeEvaluatorName(StateTreeEvaluatorKind InKind) noexcept;
[[nodiscard]] const char* AIStateTreeTaskName(StateTreeTaskKind InKind) noexcept;
[[nodiscard]] const char* AIStateTreeKindName(StateTreeNodeKind InKind) noexcept;

// The inspector preview uses the same translation the runtime task runner uses:
[[nodiscard]] AICommandComponent AIStateTreeCommandFor(const StateTreeTask& InTask, std::uint16_t InNode) noexcept;

class AIStateTreeCanvasPanel
{
public:
    enum class EditSource : std::uint8_t
    {
        None = 0,
        CanvasCommand,
        CanvasDrag,
        Inspector,
    };

    AIStateTreeCanvasPanel() = default;
    ~AIStateTreeCanvasPanel();

    AIStateTreeCanvasPanel(const AIStateTreeCanvasPanel&) = delete;
    AIStateTreeCanvasPanel& operator=(const AIStateTreeCanvasPanel&) = delete;

    // Registers the document as one external CommandStack codec and builds a
    [[nodiscard]] bool Initialize(CommandStack& InCommands, std::uint32_t InTreeId = 0) noexcept;
    void Shutdown() noexcept;

    // One ImGui/ImNodes frame. `InOpen` is the usual window open flag.
    void Draw(const char* InWindowTitle, bool* InOpen);

    // Scene binding. The panel writes its compiled tree into a marked TOML
    void SetScenePath(const std::filesystem::path& InPath);
    [[nodiscard]] const std::filesystem::path& ScenePath() const noexcept
    {
        return MScenePath;
    }

    // PIE binding. `InRuntimeWorld` is the Play-In-Editor clone (or the live
    void BindRuntimeWorld(const Ecs::Registry* InRuntimeWorld, const Ecs::Registry* InSelectionWorld,
                          bool InPieActive) noexcept;
    [[nodiscard]] std::uint32_t RefreshPIEActivePath() noexcept;

    [[nodiscard]] std::uint16_t AddAINode(StateTreeNodeKind InKind, std::uint16_t InParent,
                                          Vector2D InPosition) noexcept;
    [[nodiscard]] bool DeleteAINode(std::uint16_t InNode) noexcept;
    [[nodiscard]] bool AddAITransition(std::uint16_t InFromLeaf, std::uint16_t InToNode) noexcept;
    [[nodiscard]] bool RemoveAITransition(std::uint16_t InTransition) noexcept;
    [[nodiscard]] bool SetAINodePosition(std::uint16_t InNode, Vector2D InPosition) noexcept;

    // Allocation-free: validates depth, parent/child cycles, unreachable nodes
    [[nodiscard]] bool CompileTree() noexcept;
    [[nodiscard]] bool SaveCompiledToSceneToml();
    [[nodiscard]] bool LoadCompiledFromSceneToml();

    // Called by the host after Undo()/Redo(); the compiled image is now stale
    void NotifyHistoryChanged() noexcept;
    void ResetDocument() noexcept;
    void BuildDefaultDocument() noexcept;

    [[nodiscard]] bool Initialized() const noexcept
    {
        return MInitialized;
    }
    [[nodiscard]] bool Compiled() const noexcept
    {
        return MCompiledValid && !MCompiledDirty;
    }
    [[nodiscard]] bool HasCompiledImage() const noexcept
    {
        return MCompiledValid;
    }
    [[nodiscard]] bool CompileDirty() const noexcept
    {
        return MCompiledDirty;
    }
    [[nodiscard]] std::uint16_t Codec() const noexcept
    {
        return MCodec;
    }
    [[nodiscard]] AIStateTreeDocument& Document() noexcept
    {
        return MDocument;
    }
    [[nodiscard]] const AIStateTreeDocument& Document() const noexcept
    {
        return MDocument;
    }
    [[nodiscard]] const AIStateTreeCompiled& CompiledImage() const noexcept
    {
        return MCompiled;
    }
    [[nodiscard]] const char* Status() const noexcept
    {
        return MStatus;
    }
    [[nodiscard]] std::uint16_t SelectedNode() const noexcept
    {
        return MSelectedNode;
    }
    [[nodiscard]] std::uint16_t SelectedTransition() const noexcept
    {
        return MSelectedTransition;
    }
    [[nodiscard]] std::uint32_t PIEActiveNodeCount() const noexcept
    {
        return MPieActiveCount;
    }
    [[nodiscard]] std::uint16_t PIEActiveLeaf() const noexcept
    {
        return MPieActiveLeaf;
    }
    [[nodiscard]] bool IsAINodePIEActive(std::uint16_t InNode) const noexcept;
    [[nodiscard]] bool PIEActive() const noexcept
    {
        return MPieActive;
    }

    // Transaction hooks, public because the host may drive an edit from a
    [[nodiscard]] bool BeginEdit(const char* InLabel, EditSource InSource) noexcept;
    void RecordEdit() noexcept;
    void CommitEdit() noexcept;
    void CancelEdit() noexcept;
    [[nodiscard]] bool Editing() const noexcept
    {
        return MEditing;
    }

private:
    void DrawToolbar();
    void DrawCanvas();
    void DrawInspector();
    void DrawNodeInspector(std::uint16_t InNode);
    void DrawTransitionInspector(std::uint16_t InTransition);
    void DrawPIEInspector();
    void ProcessCanvasEvents();
    void SetStatus(const char* InFormat, ...) noexcept;
    void MarkDocumentDirty() noexcept;

    [[nodiscard]] bool BeginInspectorEdit(const char* InLabel) noexcept;
    void FinishInspectorEdit(bool InCommitted) noexcept;

    CommandStack* MCommands = nullptr;
    std::uint16_t MCodec = 0xFFFFu;
    std::uint32_t MCodecSize = 0;
    AIStateTreeDocument MDocument{};
    AIStateTreeCompiled MCompiled{};
    std::array<std::byte, kMaxTrackedComponentBytes> MBefore{};
    std::array<std::byte, kMaxTrackedComponentBytes> MAfter{};
    std::filesystem::path MScenePath;
    const Ecs::Registry* MRuntimeWorld = nullptr;
    const Ecs::Registry* MSelectionWorld = nullptr;
    std::array<bool, kMaxStateTreeNodes> MPieActiveSet{};
    std::uint16_t MPieActiveLeaf = kStateTreeNone;
    std::uint32_t MPieActiveCount = 0;
    std::uint16_t MSelectedNode = kStateTreeNone;
    std::uint16_t MSelectedTransition = 0xFFFFu;
    EditSource MEditSource = EditSource::None;
    ImNodesContext* MImNodesContext = nullptr;
    char MStatus[kAIStateTreeStatusBytes]{};
    bool MInitialized = false;
    bool MEditing = false;
    bool MCanvasDrag = false;
    bool MCompiledValid = false;
    bool MCompiledDirty = true;
    bool MPieActive = false;
};
}
