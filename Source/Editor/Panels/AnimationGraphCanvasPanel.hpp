// FemboyEngine2D - Animation Graph canvas (ImNodes) and its flat compiler.
#pragma once

#include "Editor/Infrastructure/CommandStack.hpp"
#include "Engine/ECS/AnimGraph.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string_view>
#include <type_traits>

struct ImNodesContext;

namespace Fby::Editor
{

inline constexpr std::uint32_t kMaxAnimationGraphNodes = 24;
inline constexpr std::uint32_t kMaxAnimationGraphLinks = 32;
inline constexpr std::uint32_t kMaxAnimationGraphBlends = 8;
inline constexpr std::uint32_t kAnimationGraphStatusBytes = 384;
inline constexpr std::uint32_t kAnimationGraphNameBytes = 32;

enum class AnimationGraphNodeKind : std::uint8_t
{
    Clip = 0,
    FloatParameter,
    Blend,
};

enum class AnimationGraphLinkKind : std::uint8_t
{
    // Clip Out -> Clip In, a direct AnimTransition.
    Transition = 0,
    // Clip Out -> Blend Src.
    BlendSource,
    // FloatParameter Out -> Blend Mod.
    BlendParameter,
    // Blend Out -> Clip In.
    BlendTarget,
};

struct AnimationGraphNode
{
    AnimationGraphNodeKind Kind = AnimationGraphNodeKind::Clip;
    bool Enabled = true;
    std::array<char, kAnimationGraphNameBytes> Name{};
    Vector2D Position{};

    // Clip node payload.
    AnimClip Clip{};

    // Blend node payload. FromState/ToState are derived from the two links at
    AnimTransition Blend{};

    // Float Parameter node payload.
    std::uint16_t ParameterIndex = 0;
    float ParameterValue = 0.0f;
};

static_assert(std::is_trivially_copyable_v<AnimationGraphNode>);

struct AnimationGraphLink
{
    AnimationGraphLinkKind Kind = AnimationGraphLinkKind::Transition;
    bool Enabled = true;
    std::uint16_t FromNode = 0;
    std::uint16_t ToNode = 0;
    std::uint8_t FromPin = 0;
    std::uint8_t ToPin = 0;
    // Used by Transition links only. Blend links read their transition data from
    AnimTransition Transition{};
};

static_assert(std::is_trivially_copyable_v<AnimationGraphLink>);

struct AnimationGraphDocument
{
    std::uint32_t GraphId = 0;
    std::uint16_t NodeCount = 0;
    std::uint16_t LinkCount = 0;
    std::array<AnimationGraphNode, kMaxAnimationGraphNodes> Nodes{};
    std::array<AnimationGraphLink, kMaxAnimationGraphLinks> Links{};
};

static_assert(std::is_trivially_copyable_v<AnimationGraphDocument>);
static_assert(sizeof(AnimationGraphDocument) <= kMaxTrackedComponentBytes,
              "AnimationGraphDocument must fit one CommandStack external buffer");

// Field-wise comparisons used by the inspector. They read the Mirror FieldDesc
[[nodiscard]] bool AnimationGraphTransitionFieldsEqual(const AnimTransition& InA, const AnimTransition& InB) noexcept;
[[nodiscard]] bool AnimationGraphClipFieldsEqual(const AnimClip& InA, const AnimClip& InB) noexcept;

[[nodiscard]] const char* AnimationGraphNodeKindName(AnimationGraphNodeKind InKind) noexcept;
[[nodiscard]] const char* AnimationGraphLinkKindName(AnimationGraphLinkKind InKind) noexcept;
[[nodiscard]] const char* AnimationGraphConditionName(std::uint32_t InCondition) noexcept;

// PIE hand-off buffer
struct PIEAnimFrame
{
    std::uint64_t CurrentState = 0;
    std::uint64_t PreviousState = 0;
    float BlendWeight = 0.0f;
    float BlendTime = 0.0f;
    float StateTime = 0.0f;
    std::uint16_t LastTransition = kNoAnimTransition;
    // Keeps the frame exactly one cache line; also reserves room for future
    std::array<std::byte, 34> Reserved{};
};

static_assert(std::is_trivially_copyable_v<PIEAnimFrame>);
static_assert(sizeof(PIEAnimFrame) == 64);

class AnimationGraphCanvasPanel
{
public:
    enum class EditSource : std::uint8_t
    {
        None = 0,
        CanvasCommand,
        CanvasDrag,
        Inspector,
    };

    AnimationGraphCanvasPanel() = default;
    ~AnimationGraphCanvasPanel();

    AnimationGraphCanvasPanel(const AnimationGraphCanvasPanel&) = delete;
    AnimationGraphCanvasPanel& operator=(const AnimationGraphCanvasPanel&) = delete;

    [[nodiscard]] bool Initialize(CommandStack& InCommands, std::uint32_t InGraphId = 0) noexcept;
    void Shutdown() noexcept;

    void Draw(const char* InWindowTitle, bool* InOpen);

    void SetScenePath(const std::filesystem::path& InPath);
    [[nodiscard]] const std::filesystem::path& ScenePath() const noexcept
    {
        return MScenePath;
    }

    // PIE binding: the runtime world owns AnimStateComponent, the selection
    void BindRuntimeWorld(const Ecs::Registry* InRuntimeWorld, const Ecs::Registry* InSelectionWorld,
                          bool InPieActive) noexcept;

    // Producer side. Single writer at a time; call after the simulation has
    void PublishPIEState(const AnimStateComponent& InState) noexcept;
    // Convenience producer: resolves EditorSelected in the selection world, looks
    [[nodiscard]] bool PublishPIEStateFromWorlds() noexcept;
    void InvalidatePIEState() noexcept;

    // Consumer side. Reads only the seqlock-published snapshot; safe to call
    [[nodiscard]] std::uint32_t RefreshPIEState() noexcept;

    [[nodiscard]] std::uint16_t AddNode(AnimationGraphNodeKind InKind, Vector2D InPosition) noexcept;
    [[nodiscard]] bool DeleteNode(std::uint16_t InNode) noexcept;
    [[nodiscard]] bool AddLink(std::uint16_t InFromNode, std::uint8_t InFromPin, std::uint16_t InToNode,
                               std::uint8_t InToPin) noexcept;
    [[nodiscard]] bool RemoveLink(std::uint16_t InLink) noexcept;
    [[nodiscard]] bool SetNodePosition(std::uint16_t InNode, Vector2D InPosition) noexcept;
    void SetNodeName(std::uint16_t InNode, std::string_view InName) noexcept;

    [[nodiscard]] bool CompileGraph() noexcept;
    [[nodiscard]] bool SaveToSceneToml();
    [[nodiscard]] bool LoadFromSceneToml();

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
    [[nodiscard]] AnimationGraphDocument& Document() noexcept
    {
        return MDocument;
    }
    [[nodiscard]] const AnimationGraphDocument& Document() const noexcept
    {
        return MDocument;
    }
    [[nodiscard]] const AnimStateComponent& CompiledComponent() const noexcept
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
    [[nodiscard]] std::uint16_t SelectedLink() const noexcept
    {
        return MSelectedLink;
    }

    [[nodiscard]] std::uint16_t PIEActiveNode() const noexcept
    {
        return MPieActiveNode;
    }
    [[nodiscard]] std::uint16_t PIEPreviousNode() const noexcept
    {
        return MPiePreviousNode;
    }
    [[nodiscard]] std::uint16_t PIEActiveLink() const noexcept
    {
        return MPieActiveLink;
    }
    [[nodiscard]] float PIEBlendWeight() const noexcept
    {
        return MPieBlendWeight;
    }
    [[nodiscard]] float PIEBlendTime() const noexcept
    {
        return MPieBlendTime;
    }
    [[nodiscard]] bool PIEActive() const noexcept
    {
        return MPieActive;
    }

    // Transaction hooks are public so the host/tests can drive an edit without
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
    void DrawLinkInspector(std::uint16_t InLink);
    void DrawTransitionFields(AnimTransition& InTransition, bool InEditableEndpoints);
    void DrawClipFields(AnimClip& InClip);
    void DrawPIEInspector();
    void ProcessCanvasEvents();
    void SetStatus(const char* InFormat, ...) noexcept;
    void MarkDocumentDirty() noexcept;

    [[nodiscard]] bool BeginInspectorEdit(const char* InLabel) noexcept;
    [[nodiscard]] bool EnsureInspectorEdit(const char* InLabel) noexcept;
    void FinishInspectorEdit(bool InCommitted) noexcept;
    [[nodiscard]] std::uint16_t FindBlendForLink(std::uint16_t InLink) const noexcept;
    [[nodiscard]] std::uint16_t FindClipNodeByState(std::uint64_t InState) const noexcept;
    [[nodiscard]] std::uint16_t FindParameterNode(std::uint16_t InIndex) const noexcept;

    // ImNodes keeps node/link handles in index-addressed pools. DeleteNode() and
    void PurgeCanvasSelection() noexcept;

    // Iterative, fixed-stack validation of the compiled transition graph. Rejects
    [[nodiscard]] bool ValidateCompiledCycles(const AnimStateComponent& InCompiled,
                                              std::uint16_t InTransitionCount) noexcept;

    // Seqlock reader used by RefreshPIEState(). False when no producer has
    [[nodiscard]] bool TryReadPIEState(PIEAnimFrame& OutFrame) const noexcept;

    struct PIEFrameSlot
    {
        PIEAnimFrame Frame{};
        // Even = stable, odd = writer inside the slot.
        std::atomic<std::uint64_t> Sequence{0};
        // Pad the slot to two cache lines so Sequence never shares a line with
        std::array<std::byte, 56> Pad{};
    };
    static_assert(sizeof(PIEFrameSlot) == 128);

    CommandStack* MCommands = nullptr;
    std::uint16_t MCodec = 0xFFFFu;
    std::uint32_t MCodecSize = 0;
    AnimationGraphDocument MDocument{};
    AnimStateComponent MCompiled{};
    std::array<std::uint16_t, kMaxAnimClips> MCompiledClipNode{};
    std::array<std::uint16_t, kMaxAnimTransitions> MCompiledTransitionLink{};
    std::uint16_t MAuthoredTransitionCount = 0;
    std::array<std::byte, kMaxTrackedComponentBytes> MBefore{};
    std::array<std::byte, kMaxTrackedComponentBytes> MAfter{};
    std::array<char, kAnimationGraphNameBytes> MNameEditBuffer{};
    std::uint16_t MNameEditNode = 0xFFFFu;
    std::filesystem::path MScenePath;
    const Ecs::Registry* MRuntimeWorld = nullptr;
    const Ecs::Registry* MSelectionWorld = nullptr;
    std::array<PIEFrameSlot, 2> MPieSlots{};
    std::atomic<std::uint32_t> MPieWriteSlot{0};
    std::atomic<std::uint32_t> MPiePublished{0};
    std::uint16_t MSelectedNode = 0xFFFFu;
    std::uint16_t MSelectedLink = 0xFFFFu;
    std::uint16_t MPieActiveNode = 0xFFFFu;
    std::uint16_t MPiePreviousNode = 0xFFFFu;
    std::uint16_t MPieActiveLink = 0xFFFFu;
    float MPieBlendWeight = 0.0f;
    float MPieBlendTime = 0.0f;
    EditSource MEditSource = EditSource::None;
    ImNodesContext* MImNodesContext = nullptr;
    char MStatus[kAnimationGraphStatusBytes]{};
    bool MInitialized = false;
    bool MEditing = false;
    bool MCanvasDrag = false;
    bool MCompiledValid = false;
    bool MCompiledDirty = true;
    bool MPieActive = false;
};
}
