// FemboyEngine2D - Animation Graph canvas implementation.
#include "Editor/Panels/AnimationGraphCanvasPanel.hpp"

#include "Editor/Infrastructure/EditorTypography.hpp"
#include "Editor/Infrastructure/ProjectConfig.hpp"
#include "Engine/Core/Log.hpp"
#include "Engine/Core/Mirror.hpp"
#include "Engine/ECS/Components.hpp"

#include "imnodes.h"
#include "imgui.h"

#include <toml.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>

namespace Fby::Editor
{
namespace
{

constexpr int kNodeIdBase = 1;
constexpr int kInputIdBase = 1'000'000;
constexpr int kOutputIdBase = 2'000'000;
constexpr int kLinkIdBase = 3'000'000;
constexpr int kPinsPerNode = 4;
constexpr float kPositionEpsilon = 0.01f;

constexpr ImU32 kPreviousNeon = IM_COL32(80, 200, 255, 255);
constexpr ImU32 kClipTitle = IM_COL32(64, 38, 54, 255);
constexpr ImU32 kParamTitle = IM_COL32(34, 52, 74, 255);
constexpr ImU32 kBlendTitle = IM_COL32(58, 40, 76, 255);
constexpr ImU32 kTransitionLink = IM_COL32(150, 150, 162, 190);
constexpr ImU32 kBlendLink = IM_COL32(156, 116, 210, 210);
constexpr ImU32 kParameterLink = IM_COL32(90, 190, 220, 210);

constexpr std::string_view kBlockBegin = "# === FBY ANIM GRAPH BEGIN ===";
constexpr std::string_view kBlockEnd = "# === FBY ANIM GRAPH END ===";

[[nodiscard]] int NodeId(std::uint16_t InNode) noexcept
{
    return kNodeIdBase + static_cast<int>(InNode);
}

[[nodiscard]] int InputId(std::uint16_t InNode, std::uint8_t InPin) noexcept
{
    return kInputIdBase + static_cast<int>(InNode) * kPinsPerNode + static_cast<int>(InPin);
}

[[nodiscard]] int OutputId(std::uint16_t InNode, std::uint8_t InPin) noexcept
{
    return kOutputIdBase + static_cast<int>(InNode) * kPinsPerNode + static_cast<int>(InPin);
}

[[nodiscard]] int LinkId(std::uint16_t InLink) noexcept
{
    return kLinkIdBase + static_cast<int>(InLink);
}

[[nodiscard]] bool DecodeAttribute(int InId, bool* OutIsOutput, std::uint16_t* OutNode, std::uint8_t* OutPin) noexcept
{
    const bool ScopedOutput = InId >= kOutputIdBase && InId < kLinkIdBase;
    const bool ScopedInput = InId >= kInputIdBase && InId < kOutputIdBase;
    if (!ScopedOutput && !ScopedInput)
    {
        return false;
    }
    const int ScopedLocal = InId - (ScopedOutput ? kOutputIdBase : kInputIdBase);
    *OutIsOutput = ScopedOutput;
    *OutNode = static_cast<std::uint16_t>(ScopedLocal / kPinsPerNode);
    *OutPin = static_cast<std::uint8_t>(ScopedLocal % kPinsPerNode);
    return true;
}

[[nodiscard]] bool DecodeLink(int InId, std::uint16_t* OutLink) noexcept
{
    if (InId < kLinkIdBase)
    {
        return false;
    }
    *OutLink = static_cast<std::uint16_t>(InId - kLinkIdBase);
    return true;
}

[[nodiscard]] bool Near(ImVec2 InA, ImVec2 InB) noexcept
{
    const float ScopedDx = InA.x - InB.x;
    const float ScopedDy = InA.y - InB.y;
    return ScopedDx * ScopedDx + ScopedDy * ScopedDy <= kPositionEpsilon * kPositionEpsilon;
}

// The PIE hand-off is a seqlock over two fixed slots. Every payload member is
template <typename T> void PIEStoreRelaxed(T& InDestination, T InValue) noexcept
{
    std::atomic_ref<T>(InDestination).store(InValue, std::memory_order_relaxed);
}

template <typename T> [[nodiscard]] T PIELoadRelaxed(const T& InValue) noexcept
{
    return std::atomic_ref<T>(const_cast<T&>(InValue)).load(std::memory_order_relaxed);
}

void CopyName(std::array<char, kAnimationGraphNameBytes>& InDestination, std::string_view InName) noexcept
{
    const std::size_t ScopedLength = std::min(InName.size(), InDestination.size() - 1);
    if (ScopedLength > 0)
    {
        std::memcpy(InDestination.data(), InName.data(), ScopedLength);
    }
    InDestination[ScopedLength] = '\0';
}

[[nodiscard]] const char* NameOrFallback(const std::array<char, kAnimationGraphNameBytes>& InName) noexcept
{
    return InName[0] != '\0' ? InName.data() : "(unnamed)";
}

// Mirror-backed field comparison
template <std::size_t N>
[[nodiscard]] bool FieldsEqual(const std::array<Reflect::FieldDesc, N>& InFields, const void* InA,
                               const void* InB) noexcept
{
    const auto* ScopedA = static_cast<const std::byte*>(InA);
    const auto* ScopedB = static_cast<const std::byte*>(InB);
    for (const Reflect::FieldDesc& ScopedField : InFields)
    {
        if (!Reflect::FieldBytesEqual(ScopedField, ScopedA + ScopedField.offset, ScopedB + ScopedField.offset))
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::array<Reflect::FieldDesc, Reflect::Fields<AnimTransition>().size()> TransitionFieldArray()
{
    std::array<Reflect::FieldDesc, Reflect::Fields<AnimTransition>().size()> ScopedFields{};
    const auto ScopedSource = Reflect::Fields<AnimTransition>();
    for (std::size_t ScopedI = 0; ScopedI < ScopedSource.size(); ++ScopedI)
    {
        ScopedFields[ScopedI] = ScopedSource[ScopedI];
    }
    return ScopedFields;
}

[[nodiscard]] std::array<Reflect::FieldDesc, Reflect::Fields<AnimClip>().size()> ClipFieldArray()
{
    std::array<Reflect::FieldDesc, Reflect::Fields<AnimClip>().size()> ScopedFields{};
    const auto ScopedSource = Reflect::Fields<AnimClip>();
    for (std::size_t ScopedI = 0; ScopedI < ScopedSource.size(); ++ScopedI)
    {
        ScopedFields[ScopedI] = ScopedSource[ScopedI];
    }
    return ScopedFields;
}

// TOML helpers (editor IO path; allocations are allowed here)
[[nodiscard]] std::int64_t ReadInt(const toml::table& InTable, std::string_view InKey, std::int64_t InFallback) noexcept
{
    const toml::node* ScopedNode = InTable.get(InKey);
    if (ScopedNode == nullptr)
    {
        return InFallback;
    }
    if (const auto ScopedValue = ScopedNode->value<std::int64_t>())
    {
        return *ScopedValue;
    }
    return InFallback;
}

[[nodiscard]] bool ReadBool(const toml::table& InTable, std::string_view InKey, bool InFallback) noexcept
{
    const toml::node* ScopedNode = InTable.get(InKey);
    if (ScopedNode == nullptr)
    {
        return InFallback;
    }
    if (const auto ScopedValue = ScopedNode->value<bool>())
    {
        return *ScopedValue;
    }
    return InFallback;
}

[[nodiscard]] float ReadFloat(const toml::table& InTable, std::string_view InKey, float InFallback) noexcept
{
    const toml::node* ScopedNode = InTable.get(InKey);
    if (ScopedNode == nullptr)
    {
        return InFallback;
    }
    if (const auto ScopedValue = ScopedNode->value<double>())
    {
        return static_cast<float>(*ScopedValue);
    }
    if (const auto ScopedValue = ScopedNode->value<std::int64_t>())
    {
        return static_cast<float>(*ScopedValue);
    }
    return InFallback;
}

[[nodiscard]] std::uint64_t ReadMask(const toml::table& InTable, std::string_view InKey,
                                     std::uint64_t InFallback) noexcept
{
    const toml::node* ScopedNode = InTable.get(InKey);
    if (ScopedNode == nullptr)
    {
        return InFallback;
    }
    if (const auto ScopedText = ScopedNode->value<std::string>())
    {
        std::string_view ScopedView(*ScopedText);
        if (ScopedView.starts_with("0x") || ScopedView.starts_with("0X"))
        {
            ScopedView.remove_prefix(2);
        }
        std::uint64_t ScopedValue = 0;
        const auto ScopedResult =
            std::from_chars(ScopedView.data(), ScopedView.data() + ScopedView.size(), ScopedValue, 16);
        if (ScopedResult.ec == std::errc{} && ScopedResult.ptr == ScopedView.data() + ScopedView.size())
        {
            return ScopedValue;
        }
        return InFallback;
    }
    if (const auto ScopedValue = ScopedNode->value<std::int64_t>())
    {
        return static_cast<std::uint64_t>(*ScopedValue);
    }
    return InFallback;
}

[[nodiscard]] std::string ReadString(const toml::table& InTable, std::string_view InKey, std::string_view InFallback)
{
    const toml::node* ScopedNode = InTable.get(InKey);
    if (ScopedNode == nullptr)
    {
        return std::string(InFallback);
    }
    if (const auto ScopedValue = ScopedNode->value<std::string>())
    {
        return *ScopedValue;
    }
    return std::string(InFallback);
}

void AppendInt(std::string& InDestination, std::int64_t InValue)
{
    char ScopedBuffer[32]{};
    std::snprintf(ScopedBuffer, sizeof(ScopedBuffer), "%lld", static_cast<long long>(InValue));
    InDestination += ScopedBuffer;
}

void AppendMask(std::string& InDestination, std::uint64_t InValue)
{
    char ScopedBuffer[32]{};
    std::snprintf(ScopedBuffer, sizeof(ScopedBuffer), "0x%016llx", static_cast<unsigned long long>(InValue));
    InDestination += '"';
    InDestination += ScopedBuffer;
    InDestination += '"';
}

void AppendFloat(std::string& InDestination, float InValue)
{
    if (!std::isfinite(InValue))
    {
        InDestination += "0.0";
        return;
    }
    char ScopedBuffer[48]{};
    std::snprintf(ScopedBuffer, sizeof(ScopedBuffer), "%.9g", static_cast<double>(InValue));
    std::string_view ScopedView(ScopedBuffer);
    InDestination += ScopedView;
    if (ScopedView.find('.') == std::string_view::npos && ScopedView.find('e') == std::string_view::npos &&
        ScopedView.find('E') == std::string_view::npos)
    {
        InDestination += ".0";
    }
}

void AppendBool(std::string& InDestination, bool InValue)
{
    InDestination += InValue ? "true" : "false";
}

void AppendTransitionFields(std::string& InDestination, const AnimTransition& InTransition, std::string_view InPrefix)
{
    InDestination += '\n';
    InDestination += InPrefix;
    InDestination += "from_state = ";
    AppendInt(InDestination, static_cast<std::int64_t>(InTransition.FromState));
    InDestination += '\n';
    InDestination += InPrefix;
    InDestination += "to_state = ";
    AppendInt(InDestination, static_cast<std::int64_t>(InTransition.ToState));
    InDestination += '\n';
    InDestination += InPrefix;
    InDestination += "tags_any = ";
    AppendMask(InDestination, InTransition.TagsAny);
    InDestination += '\n';
    InDestination += InPrefix;
    InDestination += "tags_all = ";
    AppendMask(InDestination, InTransition.TagsAll);
    InDestination += '\n';
    InDestination += InPrefix;
    InDestination += "state_time = ";
    AppendFloat(InDestination, InTransition.StateTimeThreshold);
    InDestination += '\n';
    InDestination += InPrefix;
    InDestination += "float_threshold = ";
    AppendFloat(InDestination, InTransition.FloatThreshold);
    InDestination += '\n';
    InDestination += InPrefix;
    InDestination += "duration = ";
    AppendFloat(InDestination, InTransition.BlendDuration);
    InDestination += '\n';
    InDestination += InPrefix;
    InDestination += "float_param = ";
    AppendInt(InDestination, InTransition.FloatParam);
    InDestination += '\n';
    InDestination += InPrefix;
    InDestination += "conditions = ";
    AppendInt(InDestination, InTransition.Conditions);
    InDestination += '\n';
    InDestination += InPrefix;
    InDestination += "enabled = ";
    AppendBool(InDestination, InTransition.Enabled);
    InDestination += '\n';
}

void ReadTransitionFields(const toml::table& InTable, std::string_view InPrefix, AnimTransition& OutTransition)
{
    const auto ScopedKey = [InPrefix](const char* InSuffix) { return std::string(InPrefix) + InSuffix; };
    OutTransition.FromState = ReadMask(InTable, ScopedKey("from_state"), OutTransition.FromState);
    OutTransition.ToState = ReadMask(InTable, ScopedKey("to_state"), OutTransition.ToState);
    OutTransition.TagsAny = ReadMask(InTable, ScopedKey("tags_any"), OutTransition.TagsAny);
    OutTransition.TagsAll = ReadMask(InTable, ScopedKey("tags_all"), OutTransition.TagsAll);
    OutTransition.StateTimeThreshold = ReadFloat(InTable, ScopedKey("state_time"), OutTransition.StateTimeThreshold);
    OutTransition.FloatThreshold = ReadFloat(InTable, ScopedKey("float_threshold"), OutTransition.FloatThreshold);
    OutTransition.BlendDuration = ReadFloat(InTable, ScopedKey("duration"), OutTransition.BlendDuration);
    OutTransition.FloatParam =
        static_cast<std::uint16_t>(ReadInt(InTable, ScopedKey("float_param"), OutTransition.FloatParam));
    OutTransition.Conditions =
        static_cast<std::uint32_t>(ReadInt(InTable, ScopedKey("conditions"), OutTransition.Conditions));
    OutTransition.Enabled = ReadBool(InTable, ScopedKey("enabled"), OutTransition.Enabled);
}

[[nodiscard]] bool RemoveExistingBlock(std::string& InDestination)
{
    std::size_t ScopedBegin = InDestination.find(kBlockBegin);
    while (ScopedBegin != std::string::npos)
    {
        const std::size_t ScopedEnd = InDestination.find(kBlockEnd, ScopedBegin);
        if (ScopedEnd == std::string::npos)
        {
            return false;
        }
        InDestination.erase(ScopedBegin, ScopedEnd + kBlockEnd.size() - ScopedBegin);
        ScopedBegin = InDestination.find(kBlockBegin);
    }
    return true;
}
}

bool AnimationGraphTransitionFieldsEqual(const AnimTransition& InA, const AnimTransition& InB) noexcept
{
    const auto ScopedFields = TransitionFieldArray();
    return FieldsEqual(ScopedFields, &InA, &InB);
}

bool AnimationGraphClipFieldsEqual(const AnimClip& InA, const AnimClip& InB) noexcept
{
    const auto ScopedFields = ClipFieldArray();
    return FieldsEqual(ScopedFields, &InA, &InB);
}

const char* AnimationGraphNodeKindName(AnimationGraphNodeKind InKind) noexcept
{
    switch (InKind)
    {
        case AnimationGraphNodeKind::Clip:
            return "Anim Clip";
        case AnimationGraphNodeKind::FloatParameter:
            return "Anim Float";
        case AnimationGraphNodeKind::Blend:
            return "Anim Blend";
    }
    return "Anim ?";
}

const char* AnimationGraphLinkKindName(AnimationGraphLinkKind InKind) noexcept
{
    switch (InKind)
    {
        case AnimationGraphLinkKind::Transition:
            return "Transition";
        case AnimationGraphLinkKind::BlendSource:
            return "Blend Source";
        case AnimationGraphLinkKind::BlendParameter:
            return "Blend Parameter";
        case AnimationGraphLinkKind::BlendTarget:
            return "Blend Target";
    }
    return "?";
}

const char* AnimationGraphConditionName(std::uint32_t InCondition) noexcept
{
    switch (InCondition)
    {
        case AnimConditionStateTimeAtLeast:
            return "StateTimeAtLeast";
        case AnimConditionStateTimeAtMost:
            return "StateTimeAtMost";
        case AnimConditionTagsAny:
            return "TagsAny";
        case AnimConditionTagsAll:
            return "TagsAll";
        case AnimConditionTagsNone:
            return "TagsNone";
        case AnimConditionFloatAtLeast:
            return "FloatAtLeast";
        case AnimConditionFloatAtMost:
            return "FloatAtMost";
        case AnimConditionNone:
            return "None";
        default:
            return "Mixed";
    }
}

// Lifetime and document editing
AnimationGraphCanvasPanel::~AnimationGraphCanvasPanel()
{
    Shutdown();
}

bool AnimationGraphCanvasPanel::Initialize(CommandStack& InCommands, std::uint32_t InGraphId) noexcept
{
    if (MInitialized)
    {
        return true;
    }
    MCommands = &InCommands;
    MDocument.GraphId = InGraphId;
    MCodec = InCommands.RegisterExternalBuffer("AnimationGraphDocument", &MDocument,
                                               static_cast<std::uint32_t>(sizeof(AnimationGraphDocument)));
    if (MCodec == 0xFFFFu)
    {
        SetStatus("Anim graph: CommandStack external buffer rejected");
        MCommands = nullptr;
        return false;
    }
    MCodecSize = InCommands.CodecSize(MCodec);
    if (MCodecSize == 0)
    {
        SetStatus("Anim graph: document codec has no size");
        MCommands = nullptr;
        MCodec = 0xFFFFu;
        return false;
    }
    MInitialized = true;
    BuildDefaultDocument();
    return true;
}

void AnimationGraphCanvasPanel::Shutdown() noexcept
{
    if (MEditing)
    {
        CommitEdit();
    }
    if (MImNodesContext != nullptr)
    {
        auto* ScopedPrevious = ImNodes::GetCurrentContext();
        auto* ScopedContext = static_cast<ImNodesContext*>(MImNodesContext);
        ImNodes::SetCurrentContext(ScopedContext);
        ImNodes::DestroyContext(ScopedContext);
        ImNodes::SetCurrentContext(ScopedPrevious);
        MImNodesContext = nullptr;
    }
    MCommands = nullptr;
    MCodec = 0xFFFFu;
    MCodecSize = 0;
    MInitialized = false;
    MEditing = false;
    MCanvasDrag = false;
    MEditSource = EditSource::None;
    MSelectedNode = 0xFFFFu;
    MSelectedLink = 0xFFFFu;
    InvalidatePIEState();
}

void AnimationGraphCanvasPanel::ResetDocument() noexcept
{
    const std::uint32_t ScopedGraphId = MDocument.GraphId;
    MDocument = AnimationGraphDocument{};
    MDocument.GraphId = ScopedGraphId;
    MCompiled = AnimStateComponent{};
    MCompiledClipNode.fill(0xFFFFu);
    MCompiledTransitionLink.fill(0xFFFFu);
    MAuthoredTransitionCount = 0;
    MCompiledValid = false;
    MCompiledDirty = true;
    MSelectedNode = 0xFFFFu;
    MSelectedLink = 0xFFFFu;
    MNameEditNode = 0xFFFFu;
    MPieActiveNode = 0xFFFFu;
    MPiePreviousNode = 0xFFFFu;
    MPieActiveLink = 0xFFFFu;
    MPieBlendWeight = 0.0f;
    MPieBlendTime = 0.0f;
    PurgeCanvasSelection();
}

void AnimationGraphCanvasPanel::BuildDefaultDocument() noexcept
{
    ResetDocument();
    const std::uint16_t ScopedIdle = AddNode(AnimationGraphNodeKind::Clip, Vector2D{-260.0f, 40.0f});
    const std::uint16_t ScopedAttack = AddNode(AnimationGraphNodeKind::Clip, Vector2D{300.0f, 40.0f});
    const std::uint16_t ScopedParameter = AddNode(AnimationGraphNodeKind::FloatParameter, Vector2D{-260.0f, 300.0f});
    const std::uint16_t ScopedBlend = AddNode(AnimationGraphNodeKind::Blend, Vector2D{30.0f, 210.0f});
    if (ScopedIdle == 0xFFFFu || ScopedAttack == 0xFFFFu || ScopedParameter == 0xFFFFu || ScopedBlend == 0xFFFFu)
    {
        SetStatus("Anim graph: could not create the demo graph");
        return;
    }

    SetNodeName(ScopedIdle, "Idle");
    SetNodeName(ScopedAttack, "Attack");
    SetNodeName(ScopedParameter, "Speed");
    SetNodeName(ScopedBlend, "Idle -> Attack");

    AnimationGraphNode& ScopedIdleNode = MDocument.Nodes[ScopedIdle];
    ScopedIdleNode.Clip.State = 1;
    ScopedIdleNode.Clip.KeyCount = 1;
    ScopedIdleNode.Clip.Keys[0].Bone = 1;
    ScopedIdleNode.Clip.Keys[0].Rotation = 0.5f;
    ScopedIdleNode.Clip.Keys[0].Scale = 1.0f;
    ScopedIdleNode.Clip.SpriteStartFrame = 0;
    ScopedIdleNode.Clip.SpriteFrameCount = 4;
    ScopedIdleNode.Clip.SpriteFrameDuration = 0.1f;

    AnimationGraphNode& ScopedAttackNode = MDocument.Nodes[ScopedAttack];
    ScopedAttackNode.Clip.State = 2;
    ScopedAttackNode.Clip.KeyCount = 1;
    ScopedAttackNode.Clip.Keys[0].Bone = 1;
    ScopedAttackNode.Clip.Keys[0].Rotation = 1.25f;
    ScopedAttackNode.Clip.Keys[0].Scale = 1.0f;
    ScopedAttackNode.Clip.SpriteStartFrame = 8;
    ScopedAttackNode.Clip.SpriteFrameCount = 2;
    ScopedAttackNode.Clip.SpriteFrameDuration = 0.08f;

    AnimationGraphNode& ScopedParameterNode = MDocument.Nodes[ScopedParameter];
    ScopedParameterNode.ParameterIndex = 0;
    ScopedParameterNode.ParameterValue = 1.0f;

    AnimationGraphNode& ScopedBlendNode = MDocument.Nodes[ScopedBlend];
    ScopedBlendNode.Blend.Conditions = AnimConditionFloatAtLeast;
    ScopedBlendNode.Blend.FloatParam = 0;
    ScopedBlendNode.Blend.FloatThreshold = 1.0f;
    ScopedBlendNode.Blend.BlendDuration = 0.2f;
    ScopedBlendNode.Blend.Enabled = true;

    // Idle -> Blend -> Attack, plus the Speed parameter binding.
    (void)AddLink(ScopedIdle, 0, ScopedBlend, 0);
    (void)AddLink(ScopedParameter, 0, ScopedBlend, 1);
    (void)AddLink(ScopedBlend, 0, ScopedAttack, 0);

    // Direct return transition: Attack -> Idle after one second.
    if (AddLink(ScopedAttack, 0, ScopedIdle, 0))
    {
        AnimTransition& ScopedReturn = MDocument.Links[MDocument.LinkCount - 1].Transition;
        ScopedReturn.Conditions = AnimConditionStateTimeAtLeast;
        ScopedReturn.StateTimeThreshold = 1.0f;
        ScopedReturn.BlendDuration = 0.15f;
    }

    MSelectedNode = ScopedIdle;
    MSelectedLink = 0xFFFFu;
    MCompiledValid = false;
    MCompiledDirty = true;
    SetStatus("Anim graph: demo loaded - press Compile Animations");
}

std::uint16_t AnimationGraphCanvasPanel::AddNode(AnimationGraphNodeKind InKind, Vector2D InPosition) noexcept
{
    if (MDocument.NodeCount >= kMaxAnimationGraphNodes)
    {
        SetStatus("Anim graph: node table is full");
        return 0xFFFFu;
    }
    std::uint32_t ScopedKindCount = 0;
    for (std::uint16_t ScopedNode = 0; ScopedNode < MDocument.NodeCount; ++ScopedNode)
    {
        if (MDocument.Nodes[ScopedNode].Kind == InKind)
        {
            ++ScopedKindCount;
        }
    }
    if ((InKind == AnimationGraphNodeKind::Clip && ScopedKindCount >= kMaxAnimClips) ||
        (InKind == AnimationGraphNodeKind::FloatParameter && ScopedKindCount >= kMaxAnimParams) ||
        (InKind == AnimationGraphNodeKind::Blend && ScopedKindCount >= kMaxAnimationGraphBlends))
    {
        SetStatus("Anim graph: fixed capacity for this node kind is full");
        return 0xFFFFu;
    }

    const auto ScopedIndex = static_cast<std::uint16_t>(MDocument.NodeCount);
    AnimationGraphNode ScopedNode;
    ScopedNode.Kind = InKind;
    ScopedNode.Position = InPosition;
    switch (InKind)
    {
        case AnimationGraphNodeKind::Clip:
        {
            char ScopedName[32]{};
            std::snprintf(ScopedName, sizeof(ScopedName), "Clip %u", static_cast<unsigned>(ScopedKindCount));
            CopyName(ScopedNode.Name, ScopedName);
            // First unused logical state id; 0 is the rest pose.
            std::uint64_t ScopedState = 1;
            for (std::uint16_t ScopedOther = 0; ScopedOther < MDocument.NodeCount; ++ScopedOther)
            {
                if (MDocument.Nodes[ScopedOther].Kind == AnimationGraphNodeKind::Clip)
                {
                    ScopedState = std::max(ScopedState, MDocument.Nodes[ScopedOther].Clip.State + 1);
                }
            }
            ScopedNode.Clip.State = ScopedState;
            break;
        }
        case AnimationGraphNodeKind::FloatParameter:
        {
            std::uint16_t ScopedParameter = 0;
            while (ScopedParameter < kMaxAnimParams)
            {
                bool ScopedUsed = false;
                for (std::uint16_t ScopedOther = 0; ScopedOther < MDocument.NodeCount; ++ScopedOther)
                {
                    if (MDocument.Nodes[ScopedOther].Kind == AnimationGraphNodeKind::FloatParameter &&
                        MDocument.Nodes[ScopedOther].ParameterIndex == ScopedParameter)
                    {
                        ScopedUsed = true;
                        break;
                    }
                }
                if (!ScopedUsed)
                {
                    break;
                }
                ++ScopedParameter;
            }
            char ScopedName[32]{};
            std::snprintf(ScopedName, sizeof(ScopedName), "Float %u", static_cast<unsigned>(ScopedParameter));
            CopyName(ScopedNode.Name, ScopedName);
            ScopedNode.ParameterIndex = ScopedParameter;
            ScopedNode.ParameterValue = 0.0f;
            break;
        }
        case AnimationGraphNodeKind::Blend:
        {
            char ScopedName[32]{};
            std::snprintf(ScopedName, sizeof(ScopedName), "Blend %u", static_cast<unsigned>(ScopedKindCount));
            CopyName(ScopedNode.Name, ScopedName);
            ScopedNode.Blend.Conditions = AnimConditionStateTimeAtLeast;
            ScopedNode.Blend.StateTimeThreshold = 0.0f;
            ScopedNode.Blend.BlendDuration = 0.15f;
            ScopedNode.Blend.Enabled = true;
            break;
        }
    }
    MDocument.Nodes[MDocument.NodeCount++] = ScopedNode;
    MSelectedNode = ScopedIndex;
    MSelectedLink = 0xFFFFu;
    MarkDocumentDirty();
    return ScopedIndex;
}

bool AnimationGraphCanvasPanel::DeleteNode(std::uint16_t InNode) noexcept
{
    if (InNode >= MDocument.NodeCount)
    {
        return false;
    }
    std::array<bool, kMaxAnimationGraphNodes> ScopedRemoved{};
    ScopedRemoved[InNode] = true;
    std::array<AnimationGraphNode, kMaxAnimationGraphNodes> ScopedOldNodes = MDocument.Nodes;
    std::array<AnimationGraphLink, kMaxAnimationGraphLinks> ScopedOldLinks = MDocument.Links;
    std::array<std::uint16_t, kMaxAnimationGraphNodes> ScopedNodeMap{};
    ScopedNodeMap.fill(0xFFFFu);

    AnimationGraphDocument ScopedRebuilt = MDocument;
    ScopedRebuilt.NodeCount = 0;
    for (std::uint16_t ScopedOld = 0; ScopedOld < MDocument.NodeCount; ++ScopedOld)
    {
        if (ScopedRemoved[ScopedOld])
        {
            continue;
        }
        ScopedNodeMap[ScopedOld] = ScopedRebuilt.NodeCount;
        ScopedRebuilt.Nodes[ScopedRebuilt.NodeCount++] = ScopedOldNodes[ScopedOld];
    }
    ScopedRebuilt.LinkCount = 0;
    for (std::uint16_t ScopedOldLink = 0; ScopedOldLink < MDocument.LinkCount; ++ScopedOldLink)
    {
        const AnimationGraphLink& ScopedLink = ScopedOldLinks[ScopedOldLink];
        if (ScopedLink.FromNode >= MDocument.NodeCount || ScopedLink.ToNode >= MDocument.NodeCount ||
            ScopedRemoved[ScopedLink.FromNode] || ScopedRemoved[ScopedLink.ToNode])
        {
            continue;
        }
        AnimationGraphLink ScopedCopy = ScopedLink;
        ScopedCopy.FromNode = ScopedNodeMap[ScopedLink.FromNode];
        ScopedCopy.ToNode = ScopedNodeMap[ScopedLink.ToNode];
        ScopedRebuilt.Links[ScopedRebuilt.LinkCount++] = ScopedCopy;
    }
    MDocument = ScopedRebuilt;
    MSelectedNode = 0xFFFFu;
    MSelectedLink = 0xFFFFu;
    MNameEditNode = 0xFFFFu;
    MarkDocumentDirty();
    // Node ids are indices: deleting node i shifts every later node down. Purge
    PurgeCanvasSelection();
    SetStatus("Anim graph: node deleted");
    return true;
}

bool AnimationGraphCanvasPanel::AddLink(std::uint16_t InFromNode, std::uint8_t InFromPin, std::uint16_t InToNode,
                                        std::uint8_t InToPin) noexcept
{
    if (InFromNode >= MDocument.NodeCount || InToNode >= MDocument.NodeCount || InFromNode == InToNode)
    {
        SetStatus("Anim graph: invalid link endpoint");
        return false;
    }
    if (MDocument.LinkCount >= kMaxAnimationGraphLinks)
    {
        SetStatus("Anim graph: link table is full");
        return false;
    }
    const AnimationGraphNode& ScopedFrom = MDocument.Nodes[InFromNode];
    const AnimationGraphNode& ScopedTo = MDocument.Nodes[InToNode];

    AnimationGraphLinkKind ScopedKind = AnimationGraphLinkKind::Transition;
    bool ScopedKnown = false;
    if (ScopedFrom.Kind == AnimationGraphNodeKind::Clip && ScopedTo.Kind == AnimationGraphNodeKind::Clip &&
        InFromPin == 0 && InToPin == 0)
    {
        ScopedKind = AnimationGraphLinkKind::Transition;
        ScopedKnown = true;
    }
    else if (ScopedFrom.Kind == AnimationGraphNodeKind::Clip && ScopedTo.Kind == AnimationGraphNodeKind::Blend &&
             InFromPin == 0 && InToPin == 0)
    {
        ScopedKind = AnimationGraphLinkKind::BlendSource;
        ScopedKnown = true;
    }
    else if (ScopedFrom.Kind == AnimationGraphNodeKind::FloatParameter &&
             ScopedTo.Kind == AnimationGraphNodeKind::Blend && InFromPin == 0 && InToPin == 1)
    {
        ScopedKind = AnimationGraphLinkKind::BlendParameter;
        ScopedKnown = true;
    }
    else if (ScopedFrom.Kind == AnimationGraphNodeKind::Blend && ScopedTo.Kind == AnimationGraphNodeKind::Clip &&
             InFromPin == 0 && InToPin == 0)
    {
        ScopedKind = AnimationGraphLinkKind::BlendTarget;
        ScopedKnown = true;
    }
    if (!ScopedKnown)
    {
        SetStatus("Anim graph: connection types are incompatible");
        return false;
    }
    for (std::uint16_t ScopedLink = 0; ScopedLink < MDocument.LinkCount; ++ScopedLink)
    {
        const AnimationGraphLink& ScopedExisting = MDocument.Links[ScopedLink];
        if (ScopedExisting.Kind == ScopedKind && ScopedExisting.FromNode == InFromNode &&
            ScopedExisting.ToNode == InToNode && ScopedExisting.FromPin == InFromPin && ScopedExisting.ToPin == InToPin)
        {
            SetStatus("Anim graph: identical link already exists");
            return false;
        }
    }

    AnimationGraphLink ScopedLink;
    ScopedLink.Kind = ScopedKind;
    ScopedLink.FromNode = InFromNode;
    ScopedLink.ToNode = InToNode;
    ScopedLink.FromPin = InFromPin;
    ScopedLink.ToPin = InToPin;
    if (ScopedKind == AnimationGraphLinkKind::Transition)
    {
        ScopedLink.Transition.FromState = ScopedFrom.Clip.State;
        ScopedLink.Transition.ToState = ScopedTo.Clip.State;
        ScopedLink.Transition.Conditions = AnimConditionStateTimeAtLeast;
        ScopedLink.Transition.StateTimeThreshold = 0.0f;
        ScopedLink.Transition.BlendDuration = 0.1f;
        ScopedLink.Transition.Enabled = true;
    }
    MDocument.Links[MDocument.LinkCount++] = ScopedLink;
    MSelectedLink = static_cast<std::uint16_t>(MDocument.LinkCount - 1);
    MSelectedNode = 0xFFFFu;
    MarkDocumentDirty();
    return true;
}

bool AnimationGraphCanvasPanel::RemoveLink(std::uint16_t InLink) noexcept
{
    if (InLink >= MDocument.LinkCount)
    {
        return false;
    }
    for (std::uint16_t ScopedI = static_cast<std::uint16_t>(InLink + 1); ScopedI < MDocument.LinkCount; ++ScopedI)
    {
        MDocument.Links[ScopedI - 1] = MDocument.Links[ScopedI];
    }
    --MDocument.LinkCount;
    MSelectedLink = 0xFFFFu;
    MarkDocumentDirty();
    // Link ids are indices too. Without this purge a selected link could decode
    PurgeCanvasSelection();
    return true;
}

bool AnimationGraphCanvasPanel::SetNodePosition(std::uint16_t InNode, Vector2D InPosition) noexcept
{
    if (InNode >= MDocument.NodeCount)
    {
        return false;
    }
    if (MDocument.Nodes[InNode].Position.X == InPosition.X && MDocument.Nodes[InNode].Position.Y == InPosition.Y)
    {
        return false;
    }
    MDocument.Nodes[InNode].Position = InPosition;
    MarkDocumentDirty();
    return true;
}

void AnimationGraphCanvasPanel::SetNodeName(std::uint16_t InNode, std::string_view InName) noexcept
{
    if (InNode >= MDocument.NodeCount)
    {
        return;
    }
    CopyName(MDocument.Nodes[InNode].Name, InName);
    MarkDocumentDirty();
}

void AnimationGraphCanvasPanel::NotifyHistoryChanged() noexcept
{
    if (MCompiledValid)
    {
        MCompiledDirty = true;
    }
    MNameEditNode = 0xFFFFu;
    // Undo/redo restores the whole document buffer, so indices may have moved
    PurgeCanvasSelection();
}

void AnimationGraphCanvasPanel::MarkDocumentDirty() noexcept
{
    MCompiledDirty = true;
}

// Transactions
bool AnimationGraphCanvasPanel::BeginEdit(const char* InLabel, EditSource InSource) noexcept
{
    if (!MInitialized || MCommands == nullptr || MCodec == 0xFFFFu)
    {
        SetStatus("Anim graph: canvas is not initialized");
        return false;
    }
    if (MEditing)
    {
        if (MEditSource == InSource)
        {
            return true;
        }
        CommitEdit();
    }
    if (MCommands->CaptureComponent(entt::null, MCodec, std::span<std::byte>(MBefore.data(), MCodecSize)) !=
        CommandStack::Result::Ok)
    {
        SetStatus("Anim graph: cannot capture document for undo");
        return false;
    }
    const auto ScopedBegun = MCommands->BeginTransaction(InLabel);
    if (ScopedBegun != CommandStack::Result::Ok)
    {
        SetStatus("Anim graph: transaction rejected: %s", CommandStack::ResultName(ScopedBegun));
        return false;
    }
    MEditing = true;
    MEditSource = InSource;
    MCanvasDrag = InSource == EditSource::CanvasDrag;
    return true;
}

void AnimationGraphCanvasPanel::RecordEdit() noexcept
{
    if (!MEditing || MCommands == nullptr)
    {
        return;
    }
    if (MCommands->CaptureComponent(entt::null, MCodec, std::span<std::byte>(MAfter.data(), MCodecSize)) !=
        CommandStack::Result::Ok)
    {
        SetStatus("Anim graph: cannot capture edited document");
        return;
    }
    const auto ScopedRecorded =
        MCommands->Record(entt::null, MCodec, std::span<const std::byte>(MBefore.data(), MCodecSize),
                          std::span<const std::byte>(MAfter.data(), MCodecSize));
    if (ScopedRecorded != CommandStack::Result::Ok)
    {
        SetStatus("Anim graph: document delta rejected: %s", CommandStack::ResultName(ScopedRecorded));
    }
}

void AnimationGraphCanvasPanel::CommitEdit() noexcept
{
    if (!MEditing || MCommands == nullptr)
    {
        return;
    }
    RecordEdit();
    const bool ScopedChanged = std::memcmp(MBefore.data(), MAfter.data(), MCodecSize) != 0;
    const auto ScopedCommitted = MCommands->CommitTransaction();
    MEditing = false;
    MCanvasDrag = false;
    MEditSource = EditSource::None;
    if (ScopedCommitted != CommandStack::Result::Ok)
    {
        SetStatus("Anim graph: transaction commit rejected: %s", CommandStack::ResultName(ScopedCommitted));
        return;
    }
    if (ScopedChanged)
    {
        MarkDocumentDirty();
    }
}

void AnimationGraphCanvasPanel::CancelEdit() noexcept
{
    if (MEditing && MCommands != nullptr)
    {
        MCommands->CancelTransaction();
    }
    MEditing = false;
    MCanvasDrag = false;
    MEditSource = EditSource::None;
}

bool AnimationGraphCanvasPanel::BeginInspectorEdit(const char* InLabel) noexcept
{
    return BeginEdit(InLabel, EditSource::Inspector);
}

bool AnimationGraphCanvasPanel::EnsureInspectorEdit(const char* InLabel) noexcept
{
    if (MEditing)
    {
        return MEditSource == EditSource::Inspector;
    }
    return BeginInspectorEdit(InLabel);
}

void AnimationGraphCanvasPanel::FinishInspectorEdit(bool InCommitted) noexcept
{
    if (!MEditing || MEditSource != EditSource::Inspector)
    {
        return;
    }
    if (InCommitted)
    {
        CommitEdit();
    }
    else
    {
        CancelEdit();
    }
}

void AnimationGraphCanvasPanel::PurgeCanvasSelection() noexcept
{
    MSelectedNode = 0xFFFFu;
    MSelectedLink = 0xFFFFu;
    if (MImNodesContext == nullptr)
    {
        return;
    }
    // NotifyHistoryChanged() can run outside a Draw() pass, so make the panel's
    auto* ScopedPrevious = ImNodes::GetCurrentContext();
    auto* ScopedContext = static_cast<ImNodesContext*>(MImNodesContext);
    ImNodes::SetCurrentContext(ScopedContext);
    ImNodes::ClearNodeSelection();
    ImNodes::ClearLinkSelection();
    ImNodes::SetCurrentContext(ScopedPrevious);
}

// CompileGraph: flat AnimStateComponent, fixed storage, no allocation
bool AnimationGraphCanvasPanel::ValidateCompiledCycles(const AnimStateComponent& InCompiled,
                                                        std::uint16_t InTransitionCount) noexcept
{
    const std::uint16_t ScopedStateCount =
        std::min<std::uint16_t>(static_cast<std::uint16_t>(InCompiled.ClipCount), kMaxAnimClips);
    if (ScopedStateCount == 0 || InTransitionCount == 0)
    {
        return true;
    }

    std::array<std::uint64_t, kMaxAnimClips> ScopedStateIds{};
    for (std::uint16_t ScopedI = 0; ScopedI < ScopedStateCount; ++ScopedI)
    {
        ScopedStateIds[ScopedI] = InCompiled.Clips[ScopedI].State;
    }
    const auto ScopedIndexOf = [&ScopedStateIds, ScopedStateCount](std::uint64_t InState) -> std::int32_t
    {
        for (std::uint16_t ScopedI = 0; ScopedI < ScopedStateCount; ++ScopedI)
        {
            if (ScopedStateIds[ScopedI] == InState)
            {
                return static_cast<std::int32_t>(ScopedI);
            }
        }
        return -1;
    };

    // Adjacency of the "immediately hot" transitions: no tag/float dependency and
    std::array<std::uint32_t, kMaxAnimClips> ScopedHotEdges{};
    constexpr std::uint32_t kTimeBits = AnimConditionStateTimeAtLeast | AnimConditionStateTimeAtMost;
    constexpr std::uint32_t kExternalBits =
        AnimConditionTagsAny | AnimConditionTagsAll | AnimConditionTagsNone | AnimConditionFloatAtLeast |
        AnimConditionFloatAtMost;
    for (std::uint16_t ScopedI = 0; ScopedI < InTransitionCount; ++ScopedI)
    {
        const AnimTransition& ScopedTransition = InCompiled.Transitions[ScopedI];
        if (!ScopedTransition.Enabled || (ScopedTransition.Conditions & kExternalBits) != 0)
        {
            continue;
        }
        const std::uint32_t ScopedTime = ScopedTransition.Conditions & kTimeBits;
        if (ScopedTime == 0)
        {
            continue;
        }
        bool ScopedHot = true;
        if ((ScopedTime & AnimConditionStateTimeAtLeast) != 0 && !(ScopedTransition.StateTimeThreshold <= 0.0f))
        {
            ScopedHot = false;
        }
        if ((ScopedTime & AnimConditionStateTimeAtMost) != 0 && !(ScopedTransition.StateTimeThreshold >= 0.0f))
        {
            ScopedHot = false;
        }
        if (!ScopedHot)
        {
            continue;
        }
        const std::int32_t ScopedTo = ScopedIndexOf(ScopedTransition.ToState);
        if (ScopedTo < 0)
        {
            continue;
        }
        const std::uint32_t ScopedTargetBit = 1u << static_cast<std::uint32_t>(ScopedTo);
        if (ScopedTransition.FromState == kAnimAnyState)
        {
            for (std::uint16_t ScopedFrom = 0; ScopedFrom < ScopedStateCount; ++ScopedFrom)
            {
                ScopedHotEdges[ScopedFrom] |= ScopedTargetBit;
            }
        }
        else
        {
            const std::int32_t ScopedFrom = ScopedIndexOf(ScopedTransition.FromState);
            if (ScopedFrom >= 0)
            {
                ScopedHotEdges[static_cast<std::uint16_t>(ScopedFrom)] |= ScopedTargetBit;
            }
        }
    }

    // Iterative three-colour DFS over at most kMaxAnimClips == 8 vertices. The
    const std::array<std::uint32_t, kMaxAnimClips> ScopedHotOriginal = ScopedHotEdges;
    std::array<std::uint8_t, kMaxAnimClips> ScopedColor{};
    std::array<std::uint16_t, kMaxAnimClips> ScopedStack{};
    for (std::uint16_t ScopedStart = 0; ScopedStart < ScopedStateCount; ++ScopedStart)
    {
        ScopedColor.fill(0);
        std::array<std::uint32_t, kMaxAnimClips> ScopedEdges = ScopedHotOriginal;
        std::int32_t ScopedTop = 0;
        ScopedStack[0] = ScopedStart;
        ScopedColor[ScopedStart] = 1;
        while (ScopedTop >= 0)
        {
            const std::uint16_t ScopedVertex = ScopedStack[static_cast<std::size_t>(ScopedTop)];
            std::uint32_t& ScopedVertexEdges = ScopedEdges[ScopedVertex];
            if (ScopedVertexEdges == 0)
            {
                ScopedColor[ScopedVertex] = 2;
                --ScopedTop;
                continue;
            }
            std::uint16_t ScopedNext = 0;
            while (((ScopedVertexEdges >> ScopedNext) & 1u) == 0)
            {
                ++ScopedNext;
            }
            ScopedVertexEdges &= ~(1u << ScopedNext);
            if (ScopedColor[ScopedNext] == 0)
            {
                ScopedColor[ScopedNext] = 1;
                ++ScopedTop;
                ScopedStack[static_cast<std::size_t>(ScopedTop)] = ScopedNext;
                continue;
            }
            if (ScopedColor[ScopedNext] != 1)
            {
                continue;
            }

            std::int32_t ScopedCycleStart = -1;
            for (std::int32_t ScopedI = 0; ScopedI <= ScopedTop; ++ScopedI)
            {
                if (ScopedStack[static_cast<std::size_t>(ScopedI)] == ScopedNext)
                {
                    ScopedCycleStart = ScopedI;
                    break;
                }
            }
            if (ScopedCycleStart < 0)
            {
                continue;
            }

            std::array<bool, kMaxAnimClips> ScopedInCycle{};
            for (std::int32_t ScopedI = ScopedCycleStart; ScopedI <= ScopedTop; ++ScopedI)
            {
                ScopedInCycle[ScopedStack[static_cast<std::size_t>(ScopedI)]] = true;
            }

            // A cycle is only a "dead loop without an exit" when no enabled
            bool ScopedHasExit = false;
            for (std::uint16_t ScopedI = 0; ScopedI < InTransitionCount && !ScopedHasExit; ++ScopedI)
            {
                const AnimTransition& ScopedTransition = InCompiled.Transitions[ScopedI];
                if (!ScopedTransition.Enabled)
                {
                    continue;
                }
                const std::int32_t ScopedTo = ScopedIndexOf(ScopedTransition.ToState);
                if (ScopedTransition.FromState == kAnimAnyState)
                {
                    // Applies from every cycle member; an outside target is an exit.
                    if (ScopedTo < 0 || !ScopedInCycle[static_cast<std::size_t>(ScopedTo)])
                    {
                        ScopedHasExit = true;
                    }
                }
                else
                {
                    const std::int32_t ScopedFrom = ScopedIndexOf(ScopedTransition.FromState);
                    if (ScopedFrom >= 0 && ScopedInCycle[static_cast<std::size_t>(ScopedFrom)] &&
                        (ScopedTo < 0 || !ScopedInCycle[static_cast<std::size_t>(ScopedTo)]))
                    {
                        ScopedHasExit = true;
                    }
                }
            }
            if (ScopedHasExit)
            {
                continue;
            }

            char ScopedPath[192]{};
            std::size_t ScopedUsed = 0;
            for (std::int32_t ScopedI = ScopedCycleStart; ScopedI <= ScopedTop; ++ScopedI)
            {
                const std::uint16_t ScopedState = ScopedStack[static_cast<std::size_t>(ScopedI)];
                const int ScopedWritten =
                    std::snprintf(ScopedPath + ScopedUsed, sizeof(ScopedPath) - ScopedUsed, "%llu -> ",
                                  static_cast<unsigned long long>(ScopedStateIds[ScopedState]));
                if (ScopedWritten > 0)
                {
                    ScopedUsed += std::min<std::size_t>(static_cast<std::size_t>(ScopedWritten),
                                                        sizeof(ScopedPath) - 1 - ScopedUsed);
                }
            }
            (void)std::snprintf(ScopedPath + ScopedUsed, sizeof(ScopedPath) - ScopedUsed, "%llu",
                                static_cast<unsigned long long>(
                                    ScopedStateIds[ScopedStack[static_cast<std::size_t>(ScopedCycleStart)]]));
            SetStatus("Anim compile: dead blend cycle without an exit: %s", ScopedPath);
            // LogBoy subscribes to the engine log; LogLine is allocation-free and
            LogLine(LogLevel::Warn, "AnimGraph", MStatus);
            return false;
        }
    }
    return true;
}

bool AnimationGraphCanvasPanel::CompileGraph() noexcept
{
    MCompiledDirty = true;
    if (MDocument.NodeCount == 0)
    {
        SetStatus("Anim compile: graph is empty");
        return false;
    }
    AnimStateComponent ScopedCompiled;
    std::array<std::uint16_t, kMaxAnimationGraphNodes> ScopedDegree{};
    std::array<bool, kMaxAnimParams> ScopedParamUsed{};
    std::array<std::uint64_t, kMaxAnimClips> ScopedStateIds{};
    std::uint16_t ScopedClipCount = 0;
    std::uint16_t ScopedParamCount = 0;
    std::uint16_t ScopedTransitionCount = 0;
    MCompiledClipNode.fill(0xFFFFu);
    MCompiledTransitionLink.fill(0xFFFFu);

    for (std::uint16_t ScopedNode = 0; ScopedNode < MDocument.NodeCount; ++ScopedNode)
    {
        const AnimationGraphNode& ScopedSource = MDocument.Nodes[ScopedNode];
        if (ScopedSource.Kind == AnimationGraphNodeKind::Clip)
        {
            if (ScopedClipCount >= kMaxAnimClips)
            {
                SetStatus("Anim compile: more than %u clip nodes", static_cast<unsigned>(kMaxAnimClips));
                return false;
            }
            if (ScopedSource.Clip.State == 0 || ScopedSource.Clip.State == kAnimAnyState)
            {
                SetStatus("Anim compile: clip node %u needs a non-zero state id", static_cast<unsigned>(ScopedNode));
                return false;
            }
            for (std::uint16_t ScopedOther = 0; ScopedOther < ScopedClipCount; ++ScopedOther)
            {
                if (ScopedStateIds[ScopedOther] == ScopedSource.Clip.State)
                {
                    SetStatus("Anim compile: duplicate state id %llu",
                              static_cast<unsigned long long>(ScopedSource.Clip.State));
                    return false;
                }
            }
            if (ScopedSource.Clip.KeyCount > kMaxAnimPoseKeys)
            {
                SetStatus("Anim compile: clip node %u has too many bone keys", static_cast<unsigned>(ScopedNode));
                return false;
            }
            for (std::uint16_t ScopedKey = 0; ScopedKey < ScopedSource.Clip.KeyCount; ++ScopedKey)
            {
                if (ScopedSource.Clip.Keys[ScopedKey].Bone >= kMaxBones)
                {
                    SetStatus("Anim compile: clip node %u references bone %u outside 0..%u",
                              static_cast<unsigned>(ScopedNode),
                              static_cast<unsigned>(ScopedSource.Clip.Keys[ScopedKey].Bone),
                              static_cast<unsigned>(kMaxBones - 1));
                    return false;
                }
            }
            ScopedStateIds[ScopedClipCount] = ScopedSource.Clip.State;
            MCompiledClipNode[ScopedClipCount] = ScopedNode;
            ScopedCompiled.Clips[ScopedClipCount] = ScopedSource.Clip;
            ++ScopedClipCount;
        }
        else if (ScopedSource.Kind == AnimationGraphNodeKind::FloatParameter)
        {
            if (ScopedSource.ParameterIndex >= kMaxAnimParams)
            {
                SetStatus("Anim compile: float node %u has an invalid parameter index",
                          static_cast<unsigned>(ScopedNode));
                return false;
            }
            if (ScopedParamUsed[ScopedSource.ParameterIndex])
            {
                SetStatus("Anim compile: duplicate parameter index %u",
                          static_cast<unsigned>(ScopedSource.ParameterIndex));
                return false;
            }
            ScopedParamUsed[ScopedSource.ParameterIndex] = true;
            ScopedCompiled.Floats[ScopedSource.ParameterIndex] = ScopedSource.ParameterValue;
            ++ScopedParamCount;
        }
        else if (ScopedSource.Kind != AnimationGraphNodeKind::Blend)
        {
            SetStatus("Anim compile: node %u has an unknown kind", static_cast<unsigned>(ScopedNode));
            return false;
        }
    }
    if (ScopedClipCount == 0)
    {
        SetStatus("Anim compile: graph has no clip node");
        return false;
    }
    if (ScopedParamCount > kMaxAnimParams)
    {
        SetStatus("Anim compile: too many float parameter nodes");
        return false;
    }

    // Validate every link and accumulate node degree for the isolated-node check.
    for (std::uint16_t ScopedLink = 0; ScopedLink < MDocument.LinkCount; ++ScopedLink)
    {
        const AnimationGraphLink& ScopedLinkData = MDocument.Links[ScopedLink];
        if (ScopedLinkData.FromNode >= MDocument.NodeCount || ScopedLinkData.ToNode >= MDocument.NodeCount ||
            ScopedLinkData.FromNode == ScopedLinkData.ToNode)
        {
            SetStatus("Anim compile: link %u has an invalid endpoint", static_cast<unsigned>(ScopedLink));
            return false;
        }
        ++ScopedDegree[ScopedLinkData.FromNode];
        ++ScopedDegree[ScopedLinkData.ToNode];
        const AnimationGraphNode& ScopedFrom = MDocument.Nodes[ScopedLinkData.FromNode];
        const AnimationGraphNode& ScopedTo = MDocument.Nodes[ScopedLinkData.ToNode];
        bool ScopedValid = false;
        switch (ScopedLinkData.Kind)
        {
            case AnimationGraphLinkKind::Transition:
                ScopedValid =
                    ScopedFrom.Kind == AnimationGraphNodeKind::Clip && ScopedTo.Kind == AnimationGraphNodeKind::Clip;
                break;
            case AnimationGraphLinkKind::BlendSource:
                ScopedValid =
                    ScopedFrom.Kind == AnimationGraphNodeKind::Clip && ScopedTo.Kind == AnimationGraphNodeKind::Blend;
                break;
            case AnimationGraphLinkKind::BlendParameter:
                ScopedValid = ScopedFrom.Kind == AnimationGraphNodeKind::FloatParameter &&
                              ScopedTo.Kind == AnimationGraphNodeKind::Blend;
                break;
            case AnimationGraphLinkKind::BlendTarget:
                ScopedValid =
                    ScopedFrom.Kind == AnimationGraphNodeKind::Blend && ScopedTo.Kind == AnimationGraphNodeKind::Clip;
                break;
        }
        if (!ScopedValid)
        {
            SetStatus("Anim compile: link %u has incompatible node kinds", static_cast<unsigned>(ScopedLink));
            return false;
        }
    }
    for (std::uint16_t ScopedNode = 0; ScopedNode < MDocument.NodeCount; ++ScopedNode)
    {
        if (ScopedDegree[ScopedNode] == 0)
        {
            SetStatus("Anim compile: node %u (%s) is isolated", static_cast<unsigned>(ScopedNode),
                      NameOrFallback(MDocument.Nodes[ScopedNode].Name));
            return false;
        }
    }

    // A helper that validates one authored transition before it enters the flat
    const auto ScopedValidateTransition = [this](const AnimTransition& InTransition, const char* InOrigin) -> bool
    {
        constexpr std::uint32_t ScopedKnownConditions =
            AnimConditionStateTimeAtLeast | AnimConditionStateTimeAtMost | AnimConditionTagsAny | AnimConditionTagsAll |
            AnimConditionTagsNone | AnimConditionFloatAtLeast | AnimConditionFloatAtMost;
        if (InTransition.Conditions == AnimConditionNone)
        {
            SetStatus("Anim compile: %s has no condition and would be inert", InOrigin);
            return false;
        }
        if ((InTransition.Conditions & ~ScopedKnownConditions) != 0)
        {
            SetStatus("Anim compile: %s has unknown condition bits", InOrigin);
            return false;
        }
        if ((InTransition.Conditions & (AnimConditionTagsAny | AnimConditionTagsNone)) != 0 &&
            InTransition.TagsAny == 0)
        {
            SetStatus("Anim compile: %s has TagsAny/TagsNone with an empty mask", InOrigin);
            return false;
        }
        if ((InTransition.Conditions & AnimConditionTagsAll) != 0 && InTransition.TagsAll == 0)
        {
            SetStatus("Anim compile: %s has TagsAll with an empty mask", InOrigin);
            return false;
        }
        if ((InTransition.Conditions & (AnimConditionFloatAtLeast | AnimConditionFloatAtMost)) != 0 &&
            InTransition.FloatParam >= kMaxAnimParams)
        {
            SetStatus("Anim compile: %s reads float parameter %u outside 0..%u", InOrigin,
                      static_cast<unsigned>(InTransition.FloatParam), static_cast<unsigned>(kMaxAnimParams - 1));
            return false;
        }
        if (!std::isfinite(InTransition.StateTimeThreshold) || !std::isfinite(InTransition.FloatThreshold) ||
            !std::isfinite(InTransition.BlendDuration) || InTransition.BlendDuration < 0.0f)
        {
            SetStatus("Anim compile: %s has a non-finite or negative parameter", InOrigin);
            return false;
        }
        return true;
    };
    const auto ScopedAppendTransition = [&](const AnimTransition& InTransition, std::uint16_t InLink) -> bool
    {
        if (ScopedTransitionCount >= kMaxAnimTransitions)
        {
            SetStatus("Anim compile: more than %u compiled transitions", static_cast<unsigned>(kMaxAnimTransitions));
            return false;
        }
        ScopedCompiled.Transitions[ScopedTransitionCount] = InTransition;
        MCompiledTransitionLink[ScopedTransitionCount] = InLink;
        ++ScopedTransitionCount;
        return true;
    };

    // Direct Clip -> Clip links.
    for (std::uint16_t ScopedLink = 0; ScopedLink < MDocument.LinkCount; ++ScopedLink)
    {
        const AnimationGraphLink& ScopedLinkData = MDocument.Links[ScopedLink];
        if (ScopedLinkData.Kind != AnimationGraphLinkKind::Transition)
        {
            continue;
        }
        AnimTransition ScopedTransition = ScopedLinkData.Transition;
        ScopedTransition.FromState = MDocument.Nodes[ScopedLinkData.FromNode].Clip.State;
        ScopedTransition.ToState = MDocument.Nodes[ScopedLinkData.ToNode].Clip.State;
        char ScopedOrigin[64]{};
        std::snprintf(ScopedOrigin, sizeof(ScopedOrigin), "transition link %u", static_cast<unsigned>(ScopedLink));
        if (!ScopedValidateTransition(ScopedTransition, ScopedOrigin) ||
            !ScopedAppendTransition(ScopedTransition, ScopedLink))
        {
            return false;
        }
    }

    // Blend paths: exactly one source and one target per blend node; the node's
    for (std::uint16_t ScopedNode = 0; ScopedNode < MDocument.NodeCount; ++ScopedNode)
    {
        const AnimationGraphNode& ScopedBlendNode = MDocument.Nodes[ScopedNode];
        if (ScopedBlendNode.Kind != AnimationGraphNodeKind::Blend)
        {
            continue;
        }
        std::uint16_t ScopedSourceLink = 0xFFFFu;
        std::uint16_t ScopedTargetLink = 0xFFFFu;
        std::uint16_t ScopedParamLink = 0xFFFFu;
        std::uint16_t ScopedSourceCount = 0;
        std::uint16_t ScopedTargetCount = 0;
        std::uint16_t ScopedParamLinkCount = 0;
        for (std::uint16_t ScopedLink = 0; ScopedLink < MDocument.LinkCount; ++ScopedLink)
        {
            const AnimationGraphLink& ScopedLinkData = MDocument.Links[ScopedLink];
            if (ScopedLinkData.Kind == AnimationGraphLinkKind::BlendSource && ScopedLinkData.ToNode == ScopedNode)
            {
                ScopedSourceLink = ScopedLink;
                ++ScopedSourceCount;
            }
            else if (ScopedLinkData.Kind == AnimationGraphLinkKind::BlendTarget &&
                     ScopedLinkData.FromNode == ScopedNode)
            {
                ScopedTargetLink = ScopedLink;
                ++ScopedTargetCount;
            }
            else if (ScopedLinkData.Kind == AnimationGraphLinkKind::BlendParameter &&
                     ScopedLinkData.ToNode == ScopedNode)
            {
                ScopedParamLink = ScopedLink;
                ++ScopedParamLinkCount;
            }
        }
        if (ScopedSourceCount != 1 || ScopedTargetCount != 1 || ScopedParamLinkCount > 1)
        {
            SetStatus("Anim compile: blend node %u needs exactly one source, one target and at most one parameter",
                      static_cast<unsigned>(ScopedNode));
            return false;
        }
        AnimTransition ScopedTransition = ScopedBlendNode.Blend;
        ScopedTransition.FromState = MDocument.Nodes[MDocument.Links[ScopedSourceLink].FromNode].Clip.State;
        ScopedTransition.ToState = MDocument.Nodes[MDocument.Links[ScopedTargetLink].ToNode].Clip.State;
        if (ScopedTransition.FromState == ScopedTransition.ToState)
        {
            SetStatus("Anim compile: blend node %u cross-fades state %llu into itself",
                      static_cast<unsigned>(ScopedNode),
                      static_cast<unsigned long long>(ScopedTransition.FromState));
            return false;
        }
        if (ScopedParamLink != 0xFFFFu)
        {
            ScopedTransition.FloatParam = MDocument.Nodes[MDocument.Links[ScopedParamLink].FromNode].ParameterIndex;
        }
        char ScopedOrigin[64]{};
        std::snprintf(ScopedOrigin, sizeof(ScopedOrigin), "blend node %u", static_cast<unsigned>(ScopedNode));
        if (!ScopedValidateTransition(ScopedTransition, ScopedOrigin) ||
            !ScopedAppendTransition(ScopedTransition, ScopedTargetLink))
        {
            return false;
        }
    }

    ScopedCompiled.ClipCount = ScopedClipCount;
    // Authored transitions are counted by MAuthoredTransitionCount. The
    ScopedCompiled.TransitionCount = 0;
    // Topology gate: iterative, fixed-stack, no recursion and no allocation. It
    if (!ValidateCompiledCycles(ScopedCompiled, ScopedTransitionCount))
    {
        return false;
    }

    for (std::uint16_t ScopedI = ScopedTransitionCount; ScopedI < kMaxAnimTransitions; ++ScopedI)
    {
        ScopedCompiled.Transitions[ScopedI] = AnimTransition{};
        ScopedCompiled.Transitions[ScopedI].Enabled = false;
        ScopedCompiled.Transitions[ScopedI].Conditions = AnimConditionNone;
    }
    ScopedCompiled.CurrentState = 0;
    ScopedCompiled.PreviousState = 0;
    ScopedCompiled.StateTime = 0.0f;
    ScopedCompiled.BlendDuration = 0.0f;
    ScopedCompiled.BlendTime = 0.0f;
    ScopedCompiled.BlendWeight = 1.0f;
    ScopedCompiled.LastTransition = kNoAnimTransition;
    ScopedCompiled.RestPoseCaptured = false;

    MCompiled = ScopedCompiled;
    MAuthoredTransitionCount = ScopedTransitionCount;
    MCompiledValid = true;
    MCompiledDirty = false;
    SetStatus("Anim compile: %u clips, %u transitions, %u float nodes", static_cast<unsigned>(ScopedClipCount),
              static_cast<unsigned>(ScopedTransitionCount), static_cast<unsigned>(ScopedParamCount));
    return true;
}

// PIE active state
void AnimationGraphCanvasPanel::BindRuntimeWorld(const Ecs::Registry* InRuntimeWorld,
                                                 const Ecs::Registry* InSelectionWorld, bool InPieActive) noexcept
{
    const bool ScopedRebound =
        MRuntimeWorld != InRuntimeWorld || MSelectionWorld != InSelectionWorld || MPieActive != InPieActive;
    MRuntimeWorld = InRuntimeWorld;
    MSelectionWorld = InSelectionWorld;
    MPieActive = InPieActive;
    // BindRuntimeWorld is called every editor frame, so only an actual rebind
    if (!MPieActive || ScopedRebound)
    {
        InvalidatePIEState();
    }
}

void AnimationGraphCanvasPanel::InvalidatePIEState() noexcept
{
    MPiePublished.store(0, std::memory_order_release);
    MPieActiveNode = 0xFFFFu;
    MPiePreviousNode = 0xFFFFu;
    MPieActiveLink = 0xFFFFu;
    MPieBlendWeight = 0.0f;
    MPieBlendTime = 0.0f;
}

void AnimationGraphCanvasPanel::PublishPIEState(const AnimStateComponent& InState) noexcept
{
    // The previous slot stays readable while this one is filled. The writer is
    const std::uint32_t ScopedWrite = MPieWriteSlot.load(std::memory_order_relaxed);
    const std::uint32_t ScopedNext = ScopedWrite ^ 1u;
    PIEFrameSlot& ScopedSlot = MPieSlots[ScopedNext];
    const std::uint64_t ScopedSequence = ScopedSlot.Sequence.load(std::memory_order_relaxed);

    ScopedSlot.Sequence.store(ScopedSequence + 1u, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);

    PIEStoreRelaxed(ScopedSlot.Frame.CurrentState, InState.CurrentState);
    PIEStoreRelaxed(ScopedSlot.Frame.PreviousState, InState.PreviousState);
    PIEStoreRelaxed(ScopedSlot.Frame.BlendWeight, InState.BlendWeight);
    PIEStoreRelaxed(ScopedSlot.Frame.BlendTime, InState.BlendTime);
    PIEStoreRelaxed(ScopedSlot.Frame.StateTime, InState.StateTime);
    PIEStoreRelaxed(ScopedSlot.Frame.LastTransition, InState.LastTransition);

    std::atomic_thread_fence(std::memory_order_release);
    ScopedSlot.Sequence.store(ScopedSequence + 2u, std::memory_order_release);
    MPieWriteSlot.store(ScopedNext, std::memory_order_release);
    MPiePublished.store(1u, std::memory_order_release);
}

bool AnimationGraphCanvasPanel::PublishPIEStateFromWorlds() noexcept
{
    if (!MPieActive || MRuntimeWorld == nullptr)
    {
        return false;
    }
    // This runs on the simulation thread at a quiescent point: after the fixed
    const Ecs::Registry& ScopedSelection = MSelectionWorld != nullptr ? *MSelectionWorld : *MRuntimeWorld;
    Entity ScopedSelected = entt::null;
    const auto ScopedView = ScopedSelection.view<EditorSelected>();
    const auto ScopedIt = ScopedView.begin();
    if (ScopedIt != ScopedView.end())
    {
        ScopedSelected = *ScopedIt;
    }
    if (ScopedSelected == entt::null)
    {
        return false;
    }
    const AnimStateComponent* ScopedState = MRuntimeWorld->try_get<AnimStateComponent>(ScopedSelected);
    if (ScopedState == nullptr)
    {
        return false;
    }
    PublishPIEState(*ScopedState);
    return true;
}

bool AnimationGraphCanvasPanel::TryReadPIEState(PIEAnimFrame& OutFrame) const noexcept
{
    if (MPiePublished.load(std::memory_order_acquire) == 0)
    {
        return false;
    }
    // A writer flips the slot only after the sequence is even. If the writer
    for (std::uint32_t ScopedAttempt = 0; ScopedAttempt < 4; ++ScopedAttempt)
    {
        const std::uint32_t ScopedIndex = MPieWriteSlot.load(std::memory_order_acquire);
        const PIEFrameSlot& ScopedSlot = MPieSlots[ScopedIndex];
        const std::uint64_t ScopedBefore = ScopedSlot.Sequence.load(std::memory_order_acquire);
        if ((ScopedBefore & 1u) != 0)
        {
            continue;
        }
        PIEAnimFrame ScopedFrame;
        // Every member goes through atomic_ref, so this is not a data race even
        ScopedFrame.CurrentState = PIELoadRelaxed(ScopedSlot.Frame.CurrentState);
        ScopedFrame.PreviousState = PIELoadRelaxed(ScopedSlot.Frame.PreviousState);
        ScopedFrame.BlendWeight = PIELoadRelaxed(ScopedSlot.Frame.BlendWeight);
        ScopedFrame.BlendTime = PIELoadRelaxed(ScopedSlot.Frame.BlendTime);
        ScopedFrame.StateTime = PIELoadRelaxed(ScopedSlot.Frame.StateTime);
        ScopedFrame.LastTransition = PIELoadRelaxed(ScopedSlot.Frame.LastTransition);
        std::atomic_thread_fence(std::memory_order_acquire);
        const std::uint64_t ScopedAfter = ScopedSlot.Sequence.load(std::memory_order_relaxed);
        if (ScopedBefore == ScopedAfter)
        {
            OutFrame = ScopedFrame;
            return true;
        }
    }
    return false;
}

std::uint16_t AnimationGraphCanvasPanel::FindClipNodeByState(std::uint64_t InState) const noexcept
{
    if (InState == 0)
    {
        return 0xFFFFu;
    }
    for (std::uint16_t ScopedNode = 0; ScopedNode < MDocument.NodeCount; ++ScopedNode)
    {
        if (MDocument.Nodes[ScopedNode].Kind == AnimationGraphNodeKind::Clip &&
            MDocument.Nodes[ScopedNode].Clip.State == InState)
        {
            return ScopedNode;
        }
    }
    return 0xFFFFu;
}

std::uint16_t AnimationGraphCanvasPanel::FindParameterNode(std::uint16_t InIndex) const noexcept
{
    for (std::uint16_t ScopedNode = 0; ScopedNode < MDocument.NodeCount; ++ScopedNode)
    {
        if (MDocument.Nodes[ScopedNode].Kind == AnimationGraphNodeKind::FloatParameter &&
            MDocument.Nodes[ScopedNode].ParameterIndex == InIndex)
        {
            return ScopedNode;
        }
    }
    return 0xFFFFu;
}

std::uint16_t AnimationGraphCanvasPanel::FindBlendForLink(std::uint16_t InLink) const noexcept
{
    if (InLink >= MDocument.LinkCount)
    {
        return 0xFFFFu;
    }
    const AnimationGraphLink& ScopedLink = MDocument.Links[InLink];
    switch (ScopedLink.Kind)
    {
        case AnimationGraphLinkKind::BlendSource:
        case AnimationGraphLinkKind::BlendParameter:
            return ScopedLink.ToNode;
        case AnimationGraphLinkKind::BlendTarget:
            return ScopedLink.FromNode;
        case AnimationGraphLinkKind::Transition:
            return 0xFFFFu;
    }
    return 0xFFFFu;
}

std::uint32_t AnimationGraphCanvasPanel::RefreshPIEState() noexcept
{
    MPieActiveNode = 0xFFFFu;
    MPiePreviousNode = 0xFFFFu;
    MPieActiveLink = 0xFFFFu;
    MPieBlendWeight = 0.0f;
    MPieBlendTime = 0.0f;
    if (!MPieActive || !MCompiledValid || MCompiledDirty)
    {
        return 0;
    }
    // No registry and no AnimStateComponent access on this path. The only
    PIEAnimFrame ScopedFrame;
    if (!TryReadPIEState(ScopedFrame))
    {
        return 0;
    }
    MPieBlendWeight = ScopedFrame.BlendWeight;
    MPieBlendTime = ScopedFrame.BlendTime;
    MPieActiveNode = FindClipNodeByState(ScopedFrame.CurrentState);
    MPiePreviousNode = FindClipNodeByState(ScopedFrame.PreviousState);
    if (MPiePreviousNode == MPieActiveNode)
    {
        MPiePreviousNode = 0xFFFFu;
    }
    if (ScopedFrame.LastTransition < kMaxAnimTransitions)
    {
        MPieActiveLink = MCompiledTransitionLink[ScopedFrame.LastTransition];
    }
    return MPieActiveNode != 0xFFFFu ? (MPiePreviousNode != 0xFFFFu ? 2u : 1u) : 0u;
}

// Canvas
void AnimationGraphCanvasPanel::Draw(const char* InWindowTitle, bool* InOpen)
{
    if (!MInitialized || InWindowTitle == nullptr)
    {
        return;
    }
    if (InOpen != nullptr && !*InOpen)
    {
        return;
    }
    if (MImNodesContext == nullptr)
    {
        MImNodesContext = ImNodes::CreateContext();
    }
    auto* ScopedPrevious = ImNodes::GetCurrentContext();
    auto* ScopedContext = static_cast<ImNodesContext*>(MImNodesContext);
    ImNodes::SetCurrentContext(ScopedContext);

    if (BeginEditorPanel(InWindowTitle, InOpen, ImGuiWindowFlags_NoScrollbar))
    {
        DrawToolbar();
        (void)RefreshPIEState();
        const ImVec2 ScopedAvailable = ImGui::GetContentRegionAvail();
        const bool ScopedSplit = ScopedAvailable.x >= 760.0f;
        if (ScopedSplit)
        {
            const float ScopedCanvasWidth = std::max(360.0f, ScopedAvailable.x - 400.0f);
            if (ImGui::BeginChild("##anim_graph_canvas", ImVec2(ScopedCanvasWidth, 0.0f), ImGuiChildFlags_Borders,
                                  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
            {
                DrawCanvas();
            }
            ImGui::EndChild();
            ImGui::SameLine();
            if (ImGui::BeginChild("##anim_graph_inspector", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders))
            {
                DrawInspector();
            }
            ImGui::EndChild();
        }
        else
        {
            const float ScopedCanvasHeight = std::max(220.0f, ScopedAvailable.y * 0.56f);
            if (ImGui::BeginChild("##anim_graph_canvas", ImVec2(0.0f, ScopedCanvasHeight), ImGuiChildFlags_Borders,
                                  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
            {
                DrawCanvas();
            }
            ImGui::EndChild();
            if (ImGui::BeginChild("##anim_graph_inspector", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders))
            {
                DrawInspector();
            }
            ImGui::EndChild();
        }
    }
    EndEditorPanel();
    ImNodes::SetCurrentContext(ScopedPrevious);
}

void AnimationGraphCanvasPanel::DrawToolbar()
{
    ImGui::TextUnformatted("Animation Graph");
    ImGui::SameLine();
    ImGui::TextDisabled("%u / %u nodes, %u links", static_cast<unsigned>(MDocument.NodeCount),
                        static_cast<unsigned>(kMaxAnimationGraphNodes), static_cast<unsigned>(MDocument.LinkCount));
    ImGui::SameLine();
    if (ImGui::Button("Compile Animations"))
    {
        if (CompileGraph())
        {
            (void)SaveToSceneToml();
        }
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!MCompiledValid);
    if (ImGui::Button("Save Anim TOML"))
    {
        (void)SaveToSceneToml();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("New Graph"))
    {
        if (BeginEdit("Reset animation graph", EditSource::CanvasCommand))
        {
            BuildDefaultDocument();
            RecordEdit();
            CommitEdit();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Add Clip"))
    {
        if (BeginEdit("Add animation clip", EditSource::CanvasCommand))
        {
            if (AddNode(AnimationGraphNodeKind::Clip, Vector2D{0.0f, 0.0f}) != 0xFFFFu)
            {
                RecordEdit();
                CommitEdit();
            }
            else
            {
                CancelEdit();
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Add Float Param"))
    {
        if (BeginEdit("Add animation float parameter", EditSource::CanvasCommand))
        {
            if (AddNode(AnimationGraphNodeKind::FloatParameter, Vector2D{0.0f, 160.0f}) != 0xFFFFu)
            {
                RecordEdit();
                CommitEdit();
            }
            else
            {
                CancelEdit();
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Add Blend"))
    {
        if (BeginEdit("Add animation blend", EditSource::CanvasCommand))
        {
            if (AddNode(AnimationGraphNodeKind::Blend, Vector2D{140.0f, 160.0f}) != 0xFFFFu)
            {
                RecordEdit();
                CommitEdit();
            }
            else
            {
                CancelEdit();
            }
        }
    }
    if (MCompiledDirty)
    {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.30f, 1.0f), "compiled image stale");
    }
    ImGui::TextDisabled("Clip Out -> Clip In: transition. Clip Out -> Blend Src + Blend Out -> Clip In: cross-fade.");
    if (MStatus[0] != '\0')
    {
        ImGui::TextWrapped("%s", MStatus);
    }
    ImGui::Separator();
}

void AnimationGraphCanvasPanel::DrawCanvas()
{
    const float ScopedPulse = 0.5f + 0.5f * std::sin(static_cast<float>(ImGui::GetTime()) * 6.0f);
    const auto ScopedActiveColor = [ScopedPulse]()
    {
        const auto ScopedAlpha = static_cast<ImU32>(180.0f + 75.0f * ScopedPulse);
        return IM_COL32(80, 255, 170, ScopedAlpha);
    };
    ImNodes::BeginNodeEditor();
    for (std::uint16_t ScopedNode = 0; ScopedNode < MDocument.NodeCount; ++ScopedNode)
    {
        const AnimationGraphNode& ScopedNodeData = MDocument.Nodes[ScopedNode];
        const ImVec2 ScopedStored{ScopedNodeData.Position.X, ScopedNodeData.Position.Y};
        if (!MCanvasDrag || !MEditing)
        {
            ImNodes::SetNodeGridSpacePos(NodeId(ScopedNode), ScopedStored);
        }
        const bool ScopedCurrent = MPieActiveNode == ScopedNode;
        const bool ScopedPrevious = MPiePreviousNode == ScopedNode;
        if (ScopedCurrent)
        {
            ImNodes::PushColorStyle(ImNodesCol_NodeOutline, ScopedActiveColor());
            ImNodes::PushColorStyle(ImNodesCol_TitleBar, IM_COL32(18, 80, 54, 255));
            ImNodes::PushColorStyle(ImNodesCol_NodeBackground, IM_COL32(16, 44, 32, 255));
        }
        else if (ScopedPrevious)
        {
            ImNodes::PushColorStyle(ImNodesCol_NodeOutline, kPreviousNeon);
            ImNodes::PushColorStyle(ImNodesCol_TitleBar, IM_COL32(18, 48, 82, 255));
            ImNodes::PushColorStyle(ImNodesCol_NodeBackground, IM_COL32(16, 32, 52, 255));
        }
        else
        {
            const ImU32 ScopedTitle =
                ScopedNodeData.Kind == AnimationGraphNodeKind::Clip
                    ? kClipTitle
                    : (ScopedNodeData.Kind == AnimationGraphNodeKind::FloatParameter ? kParamTitle : kBlendTitle);
            ImNodes::PushColorStyle(ImNodesCol_TitleBar, ScopedTitle);
        }

        ImNodes::BeginNode(NodeId(ScopedNode));
        ImNodes::BeginNodeTitleBar();
        ImGui::TextUnformatted(AnimationGraphNodeKindName(ScopedNodeData.Kind));
        ImGui::SameLine();
        ImGui::TextDisabled("%s", NameOrFallback(ScopedNodeData.Name));
        ImNodes::EndNodeTitleBar();

        switch (ScopedNodeData.Kind)
        {
            case AnimationGraphNodeKind::Clip:
            {
                ImNodes::BeginInputAttribute(InputId(ScopedNode, 0), ImNodesPinShape_CircleFilled);
                ImGui::TextUnformatted("In");
                ImNodes::EndInputAttribute();
                ImGui::TextDisabled("state %llu", static_cast<unsigned long long>(ScopedNodeData.Clip.State));
                ImGui::TextDisabled("bones %u / spriteAnimation %u+%u", static_cast<unsigned>(ScopedNodeData.Clip.KeyCount),
                                    static_cast<unsigned>(ScopedNodeData.Clip.SpriteStartFrame),
                                    static_cast<unsigned>(ScopedNodeData.Clip.SpriteFrameCount));
                if (ScopedCurrent)
                {
                    ImGui::ProgressBar(MPieBlendWeight, ImVec2(120.0f, 0.0f), "");
                    ImGui::TextColored(ImVec4(0.35f, 1.0f, 0.68f, 1.0f), "BlendWeight %.3f", MPieBlendWeight);
                }
                else if (ScopedPrevious)
                {
                    ImGui::TextColored(ImVec4(0.35f, 0.78f, 1.0f, 1.0f), "PreviousState");
                }
                ImNodes::BeginOutputAttribute(OutputId(ScopedNode, 0), ImNodesPinShape_TriangleFilled);
                ImGui::TextUnformatted("Out");
                ImNodes::EndOutputAttribute();
                break;
            }
            case AnimationGraphNodeKind::FloatParameter:
            {
                ImGui::TextDisabled("float[%u] = %.3f", static_cast<unsigned>(ScopedNodeData.ParameterIndex),
                                    static_cast<double>(ScopedNodeData.ParameterValue));
                ImNodes::BeginOutputAttribute(OutputId(ScopedNode, 0), ImNodesPinShape_TriangleFilled);
                ImGui::TextUnformatted("Float");
                ImNodes::EndOutputAttribute();
                break;
            }
            case AnimationGraphNodeKind::Blend:
            {
                ImNodes::BeginInputAttribute(InputId(ScopedNode, 0), ImNodesPinShape_CircleFilled);
                ImGui::TextUnformatted("Src");
                ImNodes::EndInputAttribute();
                ImNodes::BeginInputAttribute(InputId(ScopedNode, 1), ImNodesPinShape_CircleFilled);
                ImGui::TextUnformatted("Mod");
                ImNodes::EndInputAttribute();
                ImGui::TextDisabled("blend %.3fs | conditions 0x%x",
                                    static_cast<double>(ScopedNodeData.Blend.BlendDuration),
                                    ScopedNodeData.Blend.Conditions);
                ImNodes::BeginOutputAttribute(OutputId(ScopedNode, 0), ImNodesPinShape_TriangleFilled);
                ImGui::TextUnformatted("Out");
                ImNodes::EndOutputAttribute();
                break;
            }
        }
        ImNodes::EndNode();
        if (ScopedCurrent || ScopedPrevious)
        {
            ImNodes::PopColorStyle();
            ImNodes::PopColorStyle();
            ImNodes::PopColorStyle();
        }
        else
        {
            ImNodes::PopColorStyle();
        }
    }

    for (std::uint16_t ScopedLink = 0; ScopedLink < MDocument.LinkCount; ++ScopedLink)
    {
        const AnimationGraphLink& ScopedLinkData = MDocument.Links[ScopedLink];
        if (ScopedLinkData.FromNode >= MDocument.NodeCount || ScopedLinkData.ToNode >= MDocument.NodeCount)
        {
            continue;
        }
        ImU32 ScopedColor = kTransitionLink;
        switch (ScopedLinkData.Kind)
        {
            case AnimationGraphLinkKind::Transition:
                ScopedColor = kTransitionLink;
                break;
            case AnimationGraphLinkKind::BlendSource:
            case AnimationGraphLinkKind::BlendTarget:
                ScopedColor = kBlendLink;
                break;
            case AnimationGraphLinkKind::BlendParameter:
                ScopedColor = kParameterLink;
                break;
        }
        if (MPieActiveLink == ScopedLink)
        {
            ScopedColor = ScopedActiveColor();
        }
        ImNodes::PushColorStyle(ImNodesCol_Link, ScopedColor);
        ImNodes::Link(LinkId(ScopedLink), OutputId(ScopedLinkData.FromNode, ScopedLinkData.FromPin),
                      InputId(ScopedLinkData.ToNode, ScopedLinkData.ToPin));
        ImNodes::PopColorStyle();
    }
    ImNodes::MiniMap(0.18f, ImNodesMiniMapLocation_BottomRight);
    ImNodes::EndNodeEditor();
    ProcessCanvasEvents();
}

void AnimationGraphCanvasPanel::ProcessCanvasEvents()
{
    bool ScopedMoved = false;
    for (std::uint16_t ScopedNode = 0; ScopedNode < MDocument.NodeCount; ++ScopedNode)
    {
        const ImVec2 ScopedCurrent = ImNodes::GetNodeGridSpacePos(NodeId(ScopedNode));
        const ImVec2 ScopedStored{MDocument.Nodes[ScopedNode].Position.X, MDocument.Nodes[ScopedNode].Position.Y};
        if (Near(ScopedCurrent, ScopedStored))
        {
            continue;
        }
        if (!MEditing && !BeginEdit("Move animation node", EditSource::CanvasDrag))
        {
            break;
        }
        if (MEditSource == EditSource::CanvasDrag)
        {
            (void)SetNodePosition(ScopedNode, Vector2D{ScopedCurrent.x, ScopedCurrent.y});
            RecordEdit();
            ScopedMoved = true;
        }
    }
    if (MEditing && MEditSource == EditSource::CanvasDrag && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        CommitEdit();
    }
    if (ScopedMoved)
    {
        SetStatus("Anim graph: node moved");
    }

    int ScopedDestroyed = 0;
    if (ImNodes::IsLinkDestroyed(&ScopedDestroyed))
    {
        std::uint16_t ScopedLink = 0xFFFFu;
        if (DecodeLink(ScopedDestroyed, &ScopedLink) && BeginEdit("Delete animation link", EditSource::CanvasCommand))
        {
            if (RemoveLink(ScopedLink))
            {
                RecordEdit();
                CommitEdit();
                SetStatus("Anim graph: link deleted");
            }
            else
            {
                CancelEdit();
            }
        }
    }

    int ScopedFromAttribute = 0;
    int ScopedToAttribute = 0;
    if (ImNodes::IsLinkCreated(&ScopedFromAttribute, &ScopedToAttribute))
    {
        bool ScopedFromIsOutput = false;
        bool ScopedToIsOutput = false;
        std::uint16_t ScopedFromNode = 0xFFFFu;
        std::uint16_t ScopedToNode = 0xFFFFu;
        std::uint8_t ScopedFromPin = 0;
        std::uint8_t ScopedToPin = 0;
        if (DecodeAttribute(ScopedFromAttribute, &ScopedFromIsOutput, &ScopedFromNode, &ScopedFromPin) &&
            DecodeAttribute(ScopedToAttribute, &ScopedToIsOutput, &ScopedToNode, &ScopedToPin))
        {
            if (!ScopedFromIsOutput && ScopedToIsOutput)
            {
                std::swap(ScopedFromNode, ScopedToNode);
                std::swap(ScopedFromPin, ScopedToPin);
                ScopedFromIsOutput = true;
                ScopedToIsOutput = false;
            }
            if (ScopedFromIsOutput && !ScopedToIsOutput)
            {
                if (BeginEdit("Create animation link", EditSource::CanvasCommand))
                {
                    if (AddLink(ScopedFromNode, ScopedFromPin, ScopedToNode, ScopedToPin))
                    {
                        RecordEdit();
                        CommitEdit();
                        SetStatus("Anim graph: link created");
                    }
                    else
                    {
                        CancelEdit();
                    }
                }
            }
        }
        else
        {
            SetStatus("Anim graph: a link needs one output and one input");
        }
    }

    const bool ScopedDeletePressed =
        ImGui::IsKeyPressed(ImGuiKey_Delete, false) && !ImGui::GetIO().WantTextInput &&
        (ImNodes::IsEditorHovered() || ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows));
    if (ScopedDeletePressed)
    {
        std::array<int, kMaxAnimationGraphLinks> ScopedLinks{};
        std::array<int, kMaxAnimationGraphNodes> ScopedNodes{};
        const int ScopedLinkCount = std::min(ImNodes::NumSelectedLinks(), static_cast<int>(ScopedLinks.size()));
        const int ScopedNodeCount = std::min(ImNodes::NumSelectedNodes(), static_cast<int>(ScopedNodes.size()));
        if (ScopedLinkCount > 0)
        {
            ImNodes::GetSelectedLinks(ScopedLinks.data());
        }
        if (ScopedNodeCount > 0)
        {
            ImNodes::GetSelectedNodes(ScopedNodes.data());
        }
        if (ScopedLinkCount > 0)
        {
            if (BeginEdit("Delete animation links", EditSource::CanvasCommand))
            {
                // RemoveLink() compacts the table, so decode every id first and
                std::array<std::uint16_t, kMaxAnimationGraphLinks> ScopedIds{};
                std::size_t ScopedIdCount = 0;
                for (int ScopedI = 0; ScopedI < ScopedLinkCount; ++ScopedI)
                {
                    std::uint16_t ScopedLink = 0xFFFFu;
                    if (DecodeLink(ScopedLinks[static_cast<std::size_t>(ScopedI)], &ScopedLink) &&
                        ScopedLink < MDocument.LinkCount && ScopedIdCount < ScopedIds.size())
                    {
                        ScopedIds[ScopedIdCount++] = ScopedLink;
                    }
                }
                std::sort(ScopedIds.begin(), ScopedIds.begin() + static_cast<std::ptrdiff_t>(ScopedIdCount),
                          [](std::uint16_t InLeft, std::uint16_t InRight) { return InLeft > InRight; });
                for (std::size_t ScopedI = 0; ScopedI < ScopedIdCount; ++ScopedI)
                {
                    (void)RemoveLink(ScopedIds[ScopedI]);
                }
                RecordEdit();
                CommitEdit();
                ImNodes::ClearLinkSelection();
            }
        }
        else if (ScopedNodeCount > 0)
        {
            if (ScopedNodes[0] >= kNodeIdBase && ScopedNodes[0] < kInputIdBase)
            {
                const auto ScopedNode = static_cast<std::uint16_t>(ScopedNodes[0] - kNodeIdBase);
                if (BeginEdit("Delete animation node", EditSource::CanvasCommand))
                {
                    if (DeleteNode(ScopedNode))
                    {
                        RecordEdit();
                        CommitEdit();
                        ImNodes::ClearNodeSelection();
                    }
                    else
                    {
                        CancelEdit();
                    }
                }
            }
        }
    }

    const int ScopedSelectedNodeCount = ImNodes::NumSelectedNodes();
    const int ScopedSelectedLinkCount = ImNodes::NumSelectedLinks();
    if (ScopedSelectedNodeCount > 0)
    {
        int ScopedSelected = 0;
        ImNodes::GetSelectedNodes(&ScopedSelected);
        if (ScopedSelected >= kNodeIdBase && ScopedSelected < kInputIdBase)
        {
            MSelectedNode = static_cast<std::uint16_t>(ScopedSelected - kNodeIdBase);
            MSelectedLink = 0xFFFFu;
        }
    }
    else if (ScopedSelectedLinkCount > 0)
    {
        int ScopedSelected = 0;
        ImNodes::GetSelectedLinks(&ScopedSelected);
        std::uint16_t ScopedLink = 0xFFFFu;
        if (DecodeLink(ScopedSelected, &ScopedLink) && ScopedLink < MDocument.LinkCount)
        {
            MSelectedLink = ScopedLink;
            MSelectedNode = 0xFFFFu;
        }
    }
    else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImNodes::IsEditorHovered())
    {
        int ScopedHovered = 0;
        if (!ImNodes::IsNodeHovered(&ScopedHovered) && !ImNodes::IsLinkHovered(&ScopedHovered))
        {
            MSelectedNode = 0xFFFFu;
            MSelectedLink = 0xFFFFu;
        }
    }
}

void AnimationGraphCanvasPanel::DrawInspector()
{
    ImGui::TextUnformatted("Anim Inspector");
    if (MCompiledDirty)
    {
        ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.30f, 1.0f), "Compile Animations to refresh the runtime image.");
    }
    ImGui::Separator();
    if (MSelectedLink < MDocument.LinkCount)
    {
        DrawLinkInspector(MSelectedLink);
    }
    else if (MSelectedNode < MDocument.NodeCount)
    {
        DrawNodeInspector(MSelectedNode);
    }
    else
    {
        ImGui::TextDisabled("Select an animation clip, parameter, blend node or transition link.");
    }
    if (MPieActive)
    {
        ImGui::Separator();
        DrawPIEInspector();
    }
}

void AnimationGraphCanvasPanel::DrawNodeInspector(std::uint16_t InNode)
{
    if (InNode >= MDocument.NodeCount)
    {
        return;
    }
    AnimationGraphNode& ScopedNode = MDocument.Nodes[InNode];
    ImGui::Text("%s #%u", AnimationGraphNodeKindName(ScopedNode.Kind), static_cast<unsigned>(InNode));
    ImGui::TextDisabled("name: %s", NameOrFallback(ScopedNode.Name));

    bool ScopedEnabled = ScopedNode.Enabled;
    if (ImGui::Checkbox("Enabled", &ScopedEnabled))
    {
        if (BeginInspectorEdit("Toggle animation node"))
        {
            ScopedNode.Enabled = ScopedEnabled;
            RecordEdit();
            FinishInspectorEdit(true);
        }
    }

    if (MNameEditNode != InNode)
    {
        MNameEditNode = InNode;
        std::snprintf(MNameEditBuffer.data(), MNameEditBuffer.size(), "%s", ScopedNode.Name.data());
    }
    (void)ImGui::InputText("Name", MNameEditBuffer.data(), MNameEditBuffer.size());
    if (ImGui::IsItemDeactivatedAfterEdit())
    {
        if (EnsureInspectorEdit("Rename animation node"))
        {
            CopyName(ScopedNode.Name, MNameEditBuffer.data());
            RecordEdit();
            FinishInspectorEdit(true);
        }
        MNameEditNode = 0xFFFFu;
    }

    switch (ScopedNode.Kind)
    {
        case AnimationGraphNodeKind::Clip:
        {
            ImU64 ScopedState = static_cast<ImU64>(ScopedNode.Clip.State);
            if (ImGui::InputScalar("State id", ImGuiDataType_U64, &ScopedState, nullptr, nullptr, "%llx",
                                   ImGuiInputTextFlags_CharsHexadecimal) &&
                EnsureInspectorEdit("Edit animation state id"))
            {
                ScopedNode.Clip.State = static_cast<std::uint64_t>(ScopedState);
                RecordEdit();
            }
            if (ImGui::IsItemDeactivated())
            {
                FinishInspectorEdit(true);
            }
            DrawClipFields(ScopedNode.Clip);
            break;
        }
        case AnimationGraphNodeKind::FloatParameter:
        {
            int ScopedIndex = static_cast<int>(ScopedNode.ParameterIndex);
            if (ImGui::DragInt("Parameter index", &ScopedIndex, 0.1f, 0, static_cast<int>(kMaxAnimParams - 1)) &&
                EnsureInspectorEdit("Edit animation parameter index"))
            {
                ScopedNode.ParameterIndex = static_cast<std::uint16_t>(ScopedIndex);
                RecordEdit();
            }
            if (ImGui::IsItemDeactivated())
            {
                FinishInspectorEdit(true);
            }
            float ScopedValue = ScopedNode.ParameterValue;
            if (ImGui::DragFloat("Default value", &ScopedValue, 0.01f, -100000.0f, 100000.0f, "%.4f") &&
                EnsureInspectorEdit("Edit animation parameter default"))
            {
                ScopedNode.ParameterValue = ScopedValue;
                RecordEdit();
            }
            if (ImGui::IsItemDeactivated())
            {
                FinishInspectorEdit(true);
            }
            break;
        }
        case AnimationGraphNodeKind::Blend:
        {
            AnimTransition& ScopedBlend = ScopedNode.Blend;
            ImGui::SeparatorText("Cross-fade");
            DrawTransitionFields(ScopedBlend, false);
            break;
        }
    }

    ImGui::Separator();
    if (ImGui::Button("Delete animation node"))
    {
        if (BeginEdit("Delete animation node", EditSource::CanvasCommand))
        {
            if (DeleteNode(InNode))
            {
                RecordEdit();
                CommitEdit();
                return;
            }
            CancelEdit();
        }
    }
}

void AnimationGraphCanvasPanel::DrawLinkInspector(std::uint16_t InLink)
{
    if (InLink >= MDocument.LinkCount)
    {
        return;
    }
    AnimationGraphLink& ScopedLink = MDocument.Links[InLink];
    ImGui::Text("Anim Link #%u: %s", static_cast<unsigned>(InLink), AnimationGraphLinkKindName(ScopedLink.Kind));
    const AnimationGraphNode& ScopedFrom = MDocument.Nodes[ScopedLink.FromNode];
    const AnimationGraphNode& ScopedTo = MDocument.Nodes[ScopedLink.ToNode];
    ImGui::TextDisabled("%s -> %s", NameOrFallback(ScopedFrom.Name), NameOrFallback(ScopedTo.Name));

    if (ScopedLink.Kind == AnimationGraphLinkKind::Transition)
    {
        ImGui::TextDisabled("state %llu -> %llu", static_cast<unsigned long long>(ScopedFrom.Clip.State),
                            static_cast<unsigned long long>(ScopedTo.Clip.State));
        DrawTransitionFields(ScopedLink.Transition, false);
    }
    else
    {
        const std::uint16_t ScopedBlend = FindBlendForLink(InLink);
        if (ScopedBlend < MDocument.NodeCount)
        {
            AnimationGraphNode& ScopedBlendNode = MDocument.Nodes[ScopedBlend];
            ImGui::TextDisabled("blend node %s", NameOrFallback(ScopedBlendNode.Name));
            DrawTransitionFields(ScopedBlendNode.Blend, false);
            if (ScopedLink.Kind == AnimationGraphLinkKind::BlendParameter)
            {
                ImGui::TextDisabled("binds float[%u] to this cross-fade",
                                    static_cast<unsigned>(ScopedFrom.ParameterIndex));
            }
        }
    }

    ImGui::Separator();
    if (ImGui::Button("Delete animation link"))
    {
        if (BeginEdit("Delete animation link", EditSource::CanvasCommand))
        {
            (void)RemoveLink(InLink);
            RecordEdit();
            CommitEdit();
            ImNodes::ClearLinkSelection();
            return;
        }
    }
}

void AnimationGraphCanvasPanel::DrawTransitionFields(AnimTransition& InTransition, bool InEditableEndpoints)
{
    (void)InEditableEndpoints;
    float ScopedDuration = InTransition.BlendDuration;
    const bool ScopedDurationChanged =
        ImGui::DragFloat("BlendDuration", &ScopedDuration, 0.005f, 0.0f, 100.0f, "%.3f s");
    if (ScopedDurationChanged && EnsureInspectorEdit("Edit animation BlendDuration"))
    {
        InTransition.BlendDuration = ScopedDuration;
        RecordEdit();
    }
    if (ImGui::IsItemDeactivated())
    {
        FinishInspectorEdit(true);
    }

    ImGui::Text("BlendTime: %.3f s", static_cast<double>(MPieActive ? MPieBlendTime : 0.0f));
    ImGui::Text("BlendWeight: %.3f", static_cast<double>(MPieActive ? MPieBlendWeight : 0.0f));

    ImGui::SeparatorText("AnimTransition conditions");
    const auto ConditionCheckbox = [this, &InTransition](const char* InLabel, std::uint32_t InFlag)
    {
        bool ScopedEnabled = (InTransition.Conditions & InFlag) != 0;
        if (ImGui::Checkbox(InLabel, &ScopedEnabled))
        {
            const AnimTransition ScopedBefore = InTransition;
            if (BeginInspectorEdit("Edit animation transition condition"))
            {
                if (ScopedEnabled)
                {
                    InTransition.Conditions |= InFlag;
                }
                else
                {
                    InTransition.Conditions &= ~InFlag;
                }
                if (!AnimationGraphTransitionFieldsEqual(ScopedBefore, InTransition))
                {
                    RecordEdit();
                    FinishInspectorEdit(true);
                }
                else
                {
                    FinishInspectorEdit(false);
                }
            }
        }
    };
    ConditionCheckbox("StateTimeAtLeast", AnimConditionStateTimeAtLeast);
    ImGui::SameLine();
    ConditionCheckbox("StateTimeAtMost", AnimConditionStateTimeAtMost);
    ConditionCheckbox("TagsAny", AnimConditionTagsAny);
    ImGui::SameLine();
    ConditionCheckbox("TagsAll", AnimConditionTagsAll);
    ConditionCheckbox("TagsNone", AnimConditionTagsNone);
    ImGui::SameLine();
    ConditionCheckbox("FloatAtLeast", AnimConditionFloatAtLeast);
    ConditionCheckbox("FloatAtMost", AnimConditionFloatAtMost);

    float ScopedStateTime = InTransition.StateTimeThreshold;
    const bool ScopedStateTimeChanged =
        ImGui::DragFloat("StateTimeThreshold", &ScopedStateTime, 0.01f, 0.0f, 100000.0f, "%.3f");
    if (ScopedStateTimeChanged && EnsureInspectorEdit("Edit animation state-time threshold"))
    {
        InTransition.StateTimeThreshold = ScopedStateTime;
        RecordEdit();
    }
    if (ImGui::IsItemDeactivated())
    {
        FinishInspectorEdit(true);
    }

    ImU64 ScopedTagsAny = static_cast<ImU64>(InTransition.TagsAny);
    const bool ScopedTagsAnyChanged = ImGui::InputScalar("TagsAny", ImGuiDataType_U64, &ScopedTagsAny, nullptr, nullptr,
                                                         "%016llx", ImGuiInputTextFlags_CharsHexadecimal);
    if (ScopedTagsAnyChanged && EnsureInspectorEdit("Edit animation TagsAny"))
    {
        InTransition.TagsAny = static_cast<std::uint64_t>(ScopedTagsAny);
        RecordEdit();
    }
    if (ImGui::IsItemDeactivated())
    {
        FinishInspectorEdit(true);
    }

    ImU64 ScopedTagsAll = static_cast<ImU64>(InTransition.TagsAll);
    const bool ScopedTagsAllChanged = ImGui::InputScalar("TagsAll", ImGuiDataType_U64, &ScopedTagsAll, nullptr, nullptr,
                                                         "%016llx", ImGuiInputTextFlags_CharsHexadecimal);
    if (ScopedTagsAllChanged && EnsureInspectorEdit("Edit animation TagsAll"))
    {
        InTransition.TagsAll = static_cast<std::uint64_t>(ScopedTagsAll);
        RecordEdit();
    }
    if (ImGui::IsItemDeactivated())
    {
        FinishInspectorEdit(true);
    }

    int ScopedFloatParam = static_cast<int>(InTransition.FloatParam);
    if (ImGui::DragInt("FloatParam", &ScopedFloatParam, 0.1f, 0, static_cast<int>(kMaxAnimParams - 1)) &&
        EnsureInspectorEdit("Edit animation FloatParam"))
    {
        InTransition.FloatParam = static_cast<std::uint16_t>(ScopedFloatParam);
        RecordEdit();
    }
    if (ImGui::IsItemDeactivated())
    {
        FinishInspectorEdit(true);
    }
    float ScopedFloatThreshold = InTransition.FloatThreshold;
    const bool ScopedFloatChanged =
        ImGui::DragFloat("FloatThreshold", &ScopedFloatThreshold, 0.01f, -100000.0f, 100000.0f, "%.3f");
    if (ScopedFloatChanged && EnsureInspectorEdit("Edit animation float threshold"))
    {
        InTransition.FloatThreshold = ScopedFloatThreshold;
        RecordEdit();
    }
    if (ImGui::IsItemDeactivated())
    {
        FinishInspectorEdit(true);
    }

    bool ScopedEnabled = InTransition.Enabled;
    if (ImGui::Checkbox("TransitionEnabled", &ScopedEnabled))
    {
        if (BeginInspectorEdit("Toggle animation transition"))
        {
            InTransition.Enabled = ScopedEnabled;
            RecordEdit();
            FinishInspectorEdit(true);
        }
    }
    if (InTransition.Conditions == AnimConditionNone)
    {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f),
                           "No conditions: Compile Animations will reject this transition.");
    }

    if (ImGui::CollapsingHeader("AnimTransition (Mirror)"))
    {
        const auto ScopedFields = Reflect::Fields<AnimTransition>();
        const auto* ScopedBytes = reinterpret_cast<const std::byte*>(&InTransition);
        for (const Reflect::FieldDesc& ScopedField : ScopedFields)
        {
            switch (ScopedField.kind)
            {
                case Reflect::FieldKind::Float:
                {
                    float ScopedValue = 0.0f;
                    std::memcpy(&ScopedValue, ScopedBytes + ScopedField.offset, sizeof(ScopedValue));
                    ImGui::Text("%.*s: %.4f", static_cast<int>(ScopedField.name.size()), ScopedField.name.data(),
                                static_cast<double>(ScopedValue));
                    break;
                }
                case Reflect::FieldKind::Bool:
                {
                    bool ScopedValue = false;
                    std::memcpy(&ScopedValue, ScopedBytes + ScopedField.offset, sizeof(ScopedValue));
                    ImGui::Text("%.*s: %s", static_cast<int>(ScopedField.name.size()), ScopedField.name.data(),
                                ScopedValue ? "true" : "false");
                    break;
                }
                case Reflect::FieldKind::UInt64:
                {
                    std::uint64_t ScopedValue = 0;
                    std::memcpy(&ScopedValue, ScopedBytes + ScopedField.offset, sizeof(ScopedValue));
                    ImGui::Text("%.*s: 0x%016llx", static_cast<int>(ScopedField.name.size()), ScopedField.name.data(),
                                static_cast<unsigned long long>(ScopedValue));
                    break;
                }
                default:
                {
                    std::uint32_t ScopedValue = 0;
                    std::memcpy(&ScopedValue, ScopedBytes + ScopedField.offset,
                                std::min<std::uint32_t>(ScopedField.size, sizeof(ScopedValue)));
                    ImGui::Text("%.*s: %u", static_cast<int>(ScopedField.name.size()), ScopedField.name.data(),
                                ScopedValue);
                    break;
                }
            }
        }
    }
}

void AnimationGraphCanvasPanel::DrawClipFields(AnimClip& InClip)
{
    int ScopedKeyCount = static_cast<int>(InClip.KeyCount);
    if (ImGui::DragInt("Bone key count", &ScopedKeyCount, 0.1f, 0, static_cast<int>(kMaxAnimPoseKeys)) &&
        EnsureInspectorEdit("Edit animation bone key count"))
    {
        const auto ScopedNewCount = static_cast<std::uint16_t>(ScopedKeyCount);
        for (std::uint16_t ScopedI = InClip.KeyCount; ScopedI < ScopedNewCount; ++ScopedI)
        {
            InClip.Keys[ScopedI] = AnimBoneKey{};
            InClip.Keys[ScopedI].Scale = 1.0f;
        }
        InClip.KeyCount = ScopedNewCount;
        RecordEdit();
    }
    if (ImGui::IsItemDeactivated())
    {
        FinishInspectorEdit(true);
    }
    for (std::uint16_t ScopedKey = 0; ScopedKey < InClip.KeyCount; ++ScopedKey)
    {
        ImGui::PushID(static_cast<int>(ScopedKey));
        AnimBoneKey& ScopedEntry = InClip.Keys[ScopedKey];
        int ScopedBone = static_cast<int>(ScopedEntry.Bone);
        if (ImGui::DragInt("Bone", &ScopedBone, 0.1f, 0, static_cast<int>(kMaxBones - 1)) &&
            EnsureInspectorEdit("Edit animation bone key"))
        {
            ScopedEntry.Bone = static_cast<std::uint16_t>(ScopedBone);
            RecordEdit();
        }
        if (ImGui::IsItemDeactivated())
        {
            FinishInspectorEdit(true);
        }
        float ScopedRotation = ScopedEntry.Rotation;
        if (ImGui::DragFloat("Rotation", &ScopedRotation, 0.005f, -1000.0f, 1000.0f, "%.3f") &&
            EnsureInspectorEdit("Edit animation bone rotation"))
        {
            ScopedEntry.Rotation = ScopedRotation;
            RecordEdit();
        }
        if (ImGui::IsItemDeactivated())
        {
            FinishInspectorEdit(true);
        }
        float ScopedPosition[2] = {ScopedEntry.Position.X, ScopedEntry.Position.Y};
        if (ImGui::DragFloat2("Position", ScopedPosition, 0.1f, -100000.0f, 100000.0f, "%.2f") &&
            EnsureInspectorEdit("Edit animation bone position"))
        {
            ScopedEntry.Position = Vector2D{ScopedPosition[0], ScopedPosition[1]};
            RecordEdit();
        }
        if (ImGui::IsItemDeactivated())
        {
            FinishInspectorEdit(true);
        }
        float ScopedScale = ScopedEntry.Scale;
        if (ImGui::DragFloat("Scale", &ScopedScale, 0.005f, -1000.0f, 1000.0f, "%.3f") &&
            EnsureInspectorEdit("Edit animation bone scale"))
        {
            ScopedEntry.Scale = ScopedScale;
            RecordEdit();
        }
        if (ImGui::IsItemDeactivated())
        {
            FinishInspectorEdit(true);
        }
        ImGui::PopID();
    }

    ImGui::SeparatorText("SpriteAnimation range");
    int ScopedStart = static_cast<int>(InClip.SpriteStartFrame);
    if (ImGui::DragInt("Start frame", &ScopedStart, 0.1f, 0, 65535) &&
        EnsureInspectorEdit("Edit animation SpriteAnimation start"))
    {
        InClip.SpriteStartFrame = static_cast<std::uint32_t>(ScopedStart);
        RecordEdit();
    }
    if (ImGui::IsItemDeactivated())
    {
        FinishInspectorEdit(true);
    }
    int ScopedFrameCount = static_cast<int>(InClip.SpriteFrameCount);
    if (ImGui::DragInt("Frame count", &ScopedFrameCount, 0.1f, 0, 65535) &&
        EnsureInspectorEdit("Edit animation SpriteAnimation count"))
    {
        InClip.SpriteFrameCount = static_cast<std::uint32_t>(ScopedFrameCount);
        RecordEdit();
    }
    if (ImGui::IsItemDeactivated())
    {
        FinishInspectorEdit(true);
    }
    float ScopedFrameDuration = InClip.SpriteFrameDuration;
    if (ImGui::DragFloat("Frame duration", &ScopedFrameDuration, 0.001f, 0.001f, 1000.0f, "%.3f") &&
        EnsureInspectorEdit("Edit animation SpriteAnimation duration"))
    {
        InClip.SpriteFrameDuration = ScopedFrameDuration;
        RecordEdit();
    }
    if (ImGui::IsItemDeactivated())
    {
        FinishInspectorEdit(true);
    }
    bool ScopedLooping = InClip.SpriteLooping;
    if (ImGui::Checkbox("Looping", &ScopedLooping))
    {
        if (BeginInspectorEdit("Toggle animation SpriteAnimation looping"))
        {
            InClip.SpriteLooping = ScopedLooping;
            RecordEdit();
            FinishInspectorEdit(true);
        }
    }
    bool ScopedPlaying = InClip.SpritePlaying;
    if (ImGui::Checkbox("Playing", &ScopedPlaying))
    {
        if (BeginInspectorEdit("Toggle animation SpriteAnimation playing"))
        {
            InClip.SpritePlaying = ScopedPlaying;
            RecordEdit();
            FinishInspectorEdit(true);
        }
    }
}

void AnimationGraphCanvasPanel::DrawPIEInspector()
{
    ImGui::TextColored(ImVec4(0.35f, 1.0f, 0.68f, 1.0f), "PIE Animation State");
    if (MRuntimeWorld == nullptr)
    {
        ImGui::TextDisabled("no runtime world bound");
        return;
    }
    // Same seqlock buffer as RefreshPIEState(): the inspector never dereferences
    PIEAnimFrame ScopedFrame;
    if (!TryReadPIEState(ScopedFrame))
    {
        ImGui::TextDisabled("waiting for a PIE snapshot (publish after the simulation step)");
        return;
    }
    ImGui::Text("CurrentState %llu | PreviousState %llu",
                static_cast<unsigned long long>(ScopedFrame.CurrentState),
                static_cast<unsigned long long>(ScopedFrame.PreviousState));
    ImGui::Text("BlendTime %.3f | BlendWeight %.3f", static_cast<double>(ScopedFrame.BlendTime),
                static_cast<double>(ScopedFrame.BlendWeight));
    ImGui::ProgressBar(ScopedFrame.BlendWeight, ImVec2(-1.0f, 0.0f), "");
    if (MPieActiveNode < MDocument.NodeCount)
    {
        ImGui::Text("active: %s", NameOrFallback(MDocument.Nodes[MPieActiveNode].Name));
    }
    if (MPiePreviousNode < MDocument.NodeCount)
    {
        ImGui::Text("previous: %s", NameOrFallback(MDocument.Nodes[MPiePreviousNode].Name));
    }

    if (ImGui::CollapsingHeader("Published AnimState snapshot", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Text("StateTime: %.4f", static_cast<double>(ScopedFrame.StateTime));
        ImGui::Text("LastTransition: %u", static_cast<unsigned>(ScopedFrame.LastTransition));
        ImGui::TextDisabled("producer: %s", MPieActive ? "bound" : "inactive");
    }
}

void AnimationGraphCanvasPanel::SetStatus(const char* InFormat, ...) noexcept
{
    // NOLINT(cppcoreguidelines-init-variables)
    va_list ScopedArguments;
    va_start(ScopedArguments, InFormat);
    std::vsnprintf(MStatus, sizeof(MStatus), InFormat, ScopedArguments);
    va_end(ScopedArguments);
}

void AnimationGraphCanvasPanel::SetScenePath(const std::filesystem::path& InPath)
{
    MScenePath = InPath;
}

// TOML persistence
namespace
{
void AppendQuoted(std::string& InDestination, std::string_view InText)
{
    InDestination += '"';
    for (const char ScopedCharacter : InText)
    {
        switch (ScopedCharacter)
        {
            case '"':
                InDestination += "\\\"";
                break;
            case '\\':
                InDestination += "\\\\";
                break;
            case '\n':
                InDestination += "\\n";
                break;
            case '\r':
                InDestination += "\\r";
                break;
            case '\t':
                InDestination += "\\t";
                break;
            default:
                InDestination += ScopedCharacter;
                break;
        }
    }
    InDestination += '"';
}
}

bool AnimationGraphCanvasPanel::SaveToSceneToml()
{
    if (!MCompiledValid)
    {
        SetStatus("Anim save: compile the graph first");
        return false;
    }
    if (MScenePath.empty())
    {
        SetStatus("Anim save: no scene path is bound");
        return false;
    }
    std::string ScopedText;
    {
        std::ifstream ScopedFile(MScenePath, std::ios::binary);
        if (ScopedFile)
        {
            ScopedText.assign(std::istreambuf_iterator<char>(ScopedFile), std::istreambuf_iterator<char>());
        }
    }
    if (ScopedText.empty())
    {
        SetStatus("Anim save: scene TOML is missing or empty");
        return false;
    }
    if (!RemoveExistingBlock(ScopedText))
    {
        SetStatus("Anim save: scene has a truncated animation graph block");
        return false;
    }

    ScopedText += "\n\n";
    ScopedText += kBlockBegin;
    ScopedText += "\n# Generated by AnimationGraphCanvasPanel::CompileGraph(). Do not edit by hand.\n";
    ScopedText += "[anim_graph]\nversion = 1\ngraph_id = ";
    AppendInt(ScopedText, MDocument.GraphId);
    ScopedText += "\nnode_count = ";
    AppendInt(ScopedText, MDocument.NodeCount);
    ScopedText += "\nlink_count = ";
    AppendInt(ScopedText, MDocument.LinkCount);
    ScopedText += '\n';

    for (std::uint16_t ScopedNode = 0; ScopedNode < MDocument.NodeCount; ++ScopedNode)
    {
        const AnimationGraphNode& ScopedData = MDocument.Nodes[ScopedNode];
        ScopedText += "\n[[anim_graph.node]]\nkind = ";
        AppendInt(ScopedText, static_cast<std::int64_t>(ScopedData.Kind));
        ScopedText += "\nenabled = ";
        AppendBool(ScopedText, ScopedData.Enabled);
        ScopedText += "\nname = ";
        AppendQuoted(ScopedText, NameOrFallback(ScopedData.Name));
        ScopedText += "\nposition = [";
        AppendFloat(ScopedText, ScopedData.Position.X);
        ScopedText += ", ";
        AppendFloat(ScopedText, ScopedData.Position.Y);
        ScopedText += "]\nclip_state = ";
        AppendMask(ScopedText, ScopedData.Clip.State);
        ScopedText += "\nclip_key_count = ";
        AppendInt(ScopedText, ScopedData.Clip.KeyCount);
        ScopedText += "\nclip_sprite_start = ";
        AppendInt(ScopedText, ScopedData.Clip.SpriteStartFrame);
        ScopedText += "\nclip_sprite_count = ";
        AppendInt(ScopedText, ScopedData.Clip.SpriteFrameCount);
        ScopedText += "\nclip_sprite_duration = ";
        AppendFloat(ScopedText, ScopedData.Clip.SpriteFrameDuration);
        ScopedText += "\nclip_sprite_looping = ";
        AppendBool(ScopedText, ScopedData.Clip.SpriteLooping);
        ScopedText += "\nclip_sprite_playing = ";
        AppendBool(ScopedText, ScopedData.Clip.SpritePlaying);
        ScopedText += "\nparam_index = ";
        AppendInt(ScopedText, ScopedData.ParameterIndex);
        ScopedText += "\nparam_value = ";
        AppendFloat(ScopedText, ScopedData.ParameterValue);
        AppendTransitionFields(ScopedText, ScopedData.Blend, "blend_");
        for (std::uint16_t ScopedKey = 0; ScopedKey < ScopedData.Clip.KeyCount; ++ScopedKey)
        {
            ScopedText += "\n[[anim_graph.node.key]]\nbone = ";
            AppendInt(ScopedText, ScopedData.Clip.Keys[ScopedKey].Bone);
            ScopedText += "\nrotation = ";
            AppendFloat(ScopedText, ScopedData.Clip.Keys[ScopedKey].Rotation);
            ScopedText += "\nposition = [";
            AppendFloat(ScopedText, ScopedData.Clip.Keys[ScopedKey].Position.X);
            ScopedText += ", ";
            AppendFloat(ScopedText, ScopedData.Clip.Keys[ScopedKey].Position.Y);
            ScopedText += "]\nscale = ";
            AppendFloat(ScopedText, ScopedData.Clip.Keys[ScopedKey].Scale);
            ScopedText += '\n';
        }
    }
    for (std::uint16_t ScopedLink = 0; ScopedLink < MDocument.LinkCount; ++ScopedLink)
    {
        const AnimationGraphLink& ScopedData = MDocument.Links[ScopedLink];
        ScopedText += "\n[[anim_graph.link]]\nkind = ";
        AppendInt(ScopedText, static_cast<std::int64_t>(ScopedData.Kind));
        ScopedText += "\nenabled = ";
        AppendBool(ScopedText, ScopedData.Enabled);
        ScopedText += "\nfrom = ";
        AppendInt(ScopedText, ScopedData.FromNode);
        ScopedText += "\nto = ";
        AppendInt(ScopedText, ScopedData.ToNode);
        ScopedText += "\nfrom_pin = ";
        AppendInt(ScopedText, ScopedData.FromPin);
        ScopedText += "\nto_pin = ";
        AppendInt(ScopedText, ScopedData.ToPin);
        AppendTransitionFields(ScopedText, ScopedData.Transition, "t_");
        ScopedText += '\n';
    }
    ScopedText += kBlockEnd;
    ScopedText += '\n';

    std::string ScopedError;
    if (!WriteFileAtomically(MScenePath, ScopedText, &ScopedError))
    {
        SetStatus("Anim save: %s", ScopedError.c_str());
        return false;
    }
    SetStatus("Anim save: %u nodes written to the scene TOML", static_cast<unsigned>(MDocument.NodeCount));
    return true;
}

bool AnimationGraphCanvasPanel::LoadFromSceneToml()
{
    if (MScenePath.empty())
    {
        SetStatus("Anim load: no scene path is bound");
        return false;
    }
    std::ifstream ScopedFile(MScenePath, std::ios::binary);
    if (!ScopedFile)
    {
        BuildDefaultDocument();
        SetStatus("Anim load: scene file not found; demo loaded");
        return false;
    }
    std::string ScopedText((std::istreambuf_iterator<char>(ScopedFile)), std::istreambuf_iterator<char>());
    if (ScopedText.find(kBlockBegin) == std::string::npos)
    {
        BuildDefaultDocument();
        SetStatus("Anim load: no animation graph block in the scene; demo loaded");
        return false;
    }

    toml::table ScopedRoot;
    try
    {
        ScopedRoot = toml::parse(ScopedText);
    }
    catch (const toml::parse_error& ScopedParseError)
    {
        BuildDefaultDocument();
        SetStatus("Anim load: TOML error: %s", std::string(ScopedParseError.description()).c_str());
        return false;
    }
    const toml::table* ScopedGraph = ScopedRoot["anim_graph"].as_table();
    if (ScopedGraph == nullptr)
    {
        BuildDefaultDocument();
        SetStatus("Anim load: anim_graph section is missing");
        return false;
    }
    const toml::array* ScopedNodes = (*ScopedGraph)["node"].as_array();
    const toml::array* ScopedLinks = (*ScopedGraph)["link"].as_array();
    if (ScopedNodes == nullptr || ScopedNodes->size() > kMaxAnimationGraphNodes)
    {
        BuildDefaultDocument();
        SetStatus("Anim load: invalid anim_graph.node array");
        return false;
    }
    if (ScopedLinks != nullptr && ScopedLinks->size() > kMaxAnimationGraphLinks)
    {
        BuildDefaultDocument();
        SetStatus("Anim load: too many anim_graph.link entries");
        return false;
    }

    AnimationGraphDocument ScopedDocument;
    ScopedDocument.GraphId = static_cast<std::uint32_t>(ReadInt(*ScopedGraph, "graph_id", MDocument.GraphId));
    ScopedDocument.NodeCount = static_cast<std::uint16_t>(ScopedNodes->size());
    for (std::size_t ScopedI = 0; ScopedI < ScopedNodes->size(); ++ScopedI)
    {
        const toml::table* ScopedTable = ScopedNodes->get(ScopedI)->as_table();
        if (ScopedTable == nullptr)
        {
            BuildDefaultDocument();
            SetStatus("Anim load: node %u is not a table", static_cast<unsigned>(ScopedI));
            return false;
        }
        AnimationGraphNode& ScopedNode = ScopedDocument.Nodes[ScopedI];
        ScopedNode.Kind = static_cast<AnimationGraphNodeKind>(ReadInt(*ScopedTable, "kind", 0));
        ScopedNode.Enabled = ReadBool(*ScopedTable, "enabled", true);
        const std::string ScopedName = ReadString(*ScopedTable, "name", "(unnamed)");
        CopyName(ScopedNode.Name, ScopedName);
        if (const toml::array* ScopedPosition = (*ScopedTable)["position"].as_array();
            ScopedPosition != nullptr && ScopedPosition->size() >= 2)
        {
            ScopedNode.Position.X = static_cast<float>(ScopedPosition->get(0)->value<double>().value_or(0.0));
            ScopedNode.Position.Y = static_cast<float>(ScopedPosition->get(1)->value<double>().value_or(0.0));
        }
        ScopedNode.Clip.State = ReadMask(*ScopedTable, "clip_state", 0);
        ScopedNode.Clip.KeyCount = static_cast<std::uint16_t>(ReadInt(*ScopedTable, "clip_key_count", 0));
        ScopedNode.Clip.SpriteStartFrame =
            static_cast<std::uint32_t>(ReadInt(*ScopedTable, "clip_sprite_start", 0));
        ScopedNode.Clip.SpriteFrameCount =
            static_cast<std::uint32_t>(ReadInt(*ScopedTable, "clip_sprite_count", 0));
        ScopedNode.Clip.SpriteFrameDuration = ReadFloat(*ScopedTable, "clip_sprite_duration", 0.1f);
        ScopedNode.Clip.SpriteLooping = ReadBool(*ScopedTable, "clip_sprite_looping", true);
        ScopedNode.Clip.SpritePlaying = ReadBool(*ScopedTable, "clip_sprite_playing", true);
        ScopedNode.ParameterIndex = static_cast<std::uint16_t>(ReadInt(*ScopedTable, "param_index", 0));
        ScopedNode.ParameterValue = ReadFloat(*ScopedTable, "param_value", 0.0f);
        ReadTransitionFields(*ScopedTable, "blend_", ScopedNode.Blend);
        if (ScopedNode.Clip.KeyCount > kMaxAnimPoseKeys)
        {
            ScopedNode.Clip.KeyCount = kMaxAnimPoseKeys;
        }
        if (const toml::array* ScopedKeys = (*ScopedTable)["key"].as_array(); ScopedKeys != nullptr)
        {
            const std::size_t ScopedKeyCount = std::min<std::size_t>(ScopedKeys->size(), kMaxAnimPoseKeys);
            for (std::size_t ScopedKey = 0; ScopedKey < ScopedKeyCount; ++ScopedKey)
            {
                const toml::table* ScopedKeyTable = ScopedKeys->get(ScopedKey)->as_table();
                if (ScopedKeyTable == nullptr)
                {
                    continue;
                }
                AnimBoneKey& ScopedBone = ScopedNode.Clip.Keys[ScopedKey];
                ScopedBone.Bone = static_cast<std::uint16_t>(ReadInt(*ScopedKeyTable, "bone", 0));
                ScopedBone.Rotation = ReadFloat(*ScopedKeyTable, "rotation", 0.0f);
                ScopedBone.Scale = ReadFloat(*ScopedKeyTable, "scale", 1.0f);
                if (const toml::array* ScopedKeyPosition = (*ScopedKeyTable)["position"].as_array();
                    ScopedKeyPosition != nullptr && ScopedKeyPosition->size() >= 2)
                {
                    ScopedBone.Position.X =
                        static_cast<float>(ScopedKeyPosition->get(0)->value<double>().value_or(0.0));
                    ScopedBone.Position.Y =
                        static_cast<float>(ScopedKeyPosition->get(1)->value<double>().value_or(0.0));
                }
            }
            ScopedNode.Clip.KeyCount = static_cast<std::uint16_t>(ScopedKeyCount);
        }
    }
    if (ScopedLinks != nullptr)
    {
        ScopedDocument.LinkCount = static_cast<std::uint16_t>(ScopedLinks->size());
        for (std::size_t ScopedI = 0; ScopedI < ScopedLinks->size(); ++ScopedI)
        {
            const toml::table* ScopedTable = ScopedLinks->get(ScopedI)->as_table();
            if (ScopedTable == nullptr)
            {
                BuildDefaultDocument();
                SetStatus("Anim load: link %u is not a table", static_cast<unsigned>(ScopedI));
                return false;
            }
            AnimationGraphLink& ScopedLink = ScopedDocument.Links[ScopedI];
            ScopedLink.Kind = static_cast<AnimationGraphLinkKind>(ReadInt(*ScopedTable, "kind", 0));
            ScopedLink.Enabled = ReadBool(*ScopedTable, "enabled", true);
            ScopedLink.FromNode = static_cast<std::uint16_t>(ReadInt(*ScopedTable, "from", 0));
            ScopedLink.ToNode = static_cast<std::uint16_t>(ReadInt(*ScopedTable, "to", 0));
            ScopedLink.FromPin = static_cast<std::uint8_t>(ReadInt(*ScopedTable, "from_pin", 0));
            ScopedLink.ToPin = static_cast<std::uint8_t>(ReadInt(*ScopedTable, "to_pin", 0));
            ReadTransitionFields(*ScopedTable, "t_", ScopedLink.Transition);
        }
    }

    MDocument = ScopedDocument;
    MCompiledValid = false;
    MCompiledDirty = true;
    PurgeCanvasSelection();
    if (!CompileGraph())
    {
        char ScopedError[kAnimationGraphStatusBytes]{};
        std::snprintf(ScopedError, sizeof(ScopedError), "%s", MStatus);
        BuildDefaultDocument();
        SetStatus("Anim load rejected: %s", ScopedError);
        return false;
    }
    SetStatus("Anim load: %u clips, %u transitions restored", static_cast<unsigned>(MCompiled.ClipCount),
              static_cast<unsigned>(MAuthoredTransitionCount));
    return true;
}
}
