// FemboyEngine2D - AI State Tree canvas implementation.
#include "Editor/Panels/AIStateTreeCanvasPanel.hpp"

#include "Editor/Infrastructure/EditorTypography.hpp"
#include "Editor/Infrastructure/ProjectConfig.hpp"
#include "Engine/Core/Mirror.hpp"
#include "Engine/ECS/Components.hpp"

#include "imnodes.h"
#include "imgui.h"

#include <toml.hpp>

#include <algorithm>
#include <array>
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

constexpr int kAINodeIdBase = 1;
constexpr int kAIInputIdBase = 1'000'000;
constexpr int kAIOutputIdBase = 2'000'000;
constexpr int kAITransitionIdBase = 3'000'000;
constexpr float kAIPositionEpsilon = 0.01f;

constexpr ImU32 kAINeon = IM_COL32(64, 255, 160, 255);
constexpr ImU32 kAINeonTitle = IM_COL32(18, 74, 46, 255);
constexpr ImU32 kAINeonBackground = IM_COL32(18, 44, 32, 255);
constexpr ImU32 kAIMutedLink = IM_COL32(120, 126, 138, 190);

constexpr std::string_view kAIBlockBegin = "# === FBY AI STATE TREE BEGIN ===";
constexpr std::string_view kAIBlockEnd = "# === FBY AI STATE TREE END ===";

[[nodiscard]] int NodeId(std::uint16_t InNode) noexcept
{
    return kAINodeIdBase + static_cast<int>(InNode);
}

[[nodiscard]] int InputId(std::uint16_t InNode) noexcept
{
    return kAIInputIdBase + static_cast<int>(InNode);
}

[[nodiscard]] int OutputId(std::uint16_t InNode) noexcept
{
    return kAIOutputIdBase + static_cast<int>(InNode);
}

[[nodiscard]] int TransitionLinkId(std::uint16_t InTransition) noexcept
{
    return kAITransitionIdBase + static_cast<int>(InTransition);
}

[[nodiscard]] bool DecodeInput(int InId, std::uint16_t* OutNode) noexcept
{
    if (InId < kAIInputIdBase || InId >= kAIOutputIdBase)
    {
        return false;
    }
    *OutNode = static_cast<std::uint16_t>(InId - kAIInputIdBase);
    return true;
}

[[nodiscard]] bool DecodeOutput(int InId, std::uint16_t* OutNode) noexcept
{
    if (InId < kAIOutputIdBase || InId >= kAITransitionIdBase)
    {
        return false;
    }
    *OutNode = static_cast<std::uint16_t>(InId - kAIOutputIdBase);
    return true;
}

[[nodiscard]] bool DecodeTransitionLink(int InId, std::uint16_t* OutTransition) noexcept
{
    if (InId < kAITransitionIdBase)
    {
        return false;
    }
    *OutTransition = static_cast<std::uint16_t>(InId - kAITransitionIdBase);
    return true;
}

[[nodiscard]] bool Near(ImVec2 InA, ImVec2 InB) noexcept
{
    const float ScopedDx = InA.x - InB.x;
    const float ScopedDy = InA.y - InB.y;
    return ScopedDx * ScopedDx + ScopedDy * ScopedDy <= kAIPositionEpsilon * kAIPositionEpsilon;
}

// Field-wise views used by the inspector
static_assert(std::is_standard_layout_v<StateTreeEvaluator>);
static_assert(std::is_standard_layout_v<StateTreeTask>);

constexpr std::array<Reflect::FieldDesc, 4> kAIEvaluatorFields{{
    {"Kind", Reflect::FieldKind::UInt32, static_cast<std::uint32_t>(offsetof(StateTreeEvaluator, Kind)),
     static_cast<std::uint32_t>(sizeof(StateTreeEvaluator::Kind))},
    {"TagMask", Reflect::FieldKind::UInt64, static_cast<std::uint32_t>(offsetof(StateTreeEvaluator, TagMask)),
     static_cast<std::uint32_t>(sizeof(StateTreeEvaluator::TagMask))},
    {"Threshold", Reflect::FieldKind::Float, static_cast<std::uint32_t>(offsetof(StateTreeEvaluator, Threshold)),
     static_cast<std::uint32_t>(sizeof(StateTreeEvaluator::Threshold))},
    {"FloatIndex", Reflect::FieldKind::UInt32, static_cast<std::uint32_t>(offsetof(StateTreeEvaluator, FloatIndex)),
     static_cast<std::uint32_t>(sizeof(StateTreeEvaluator::FloatIndex))},
}};

constexpr std::array<Reflect::FieldDesc, 5> kAITaskFields{{
    {"Kind", Reflect::FieldKind::UInt32, static_cast<std::uint32_t>(offsetof(StateTreeTask, Kind)),
     static_cast<std::uint32_t>(sizeof(StateTreeTask::Kind))},
    {"TagMask", Reflect::FieldKind::UInt64, static_cast<std::uint32_t>(offsetof(StateTreeTask, TagMask)),
     static_cast<std::uint32_t>(sizeof(StateTreeTask::TagMask))},
    {"Speed", Reflect::FieldKind::Float, static_cast<std::uint32_t>(offsetof(StateTreeTask, Speed)),
     static_cast<std::uint32_t>(sizeof(StateTreeTask::Speed))},
    {"Range", Reflect::FieldKind::Float, static_cast<std::uint32_t>(offsetof(StateTreeTask, Range)),
     static_cast<std::uint32_t>(sizeof(StateTreeTask::Range))},
    {"Flags", Reflect::FieldKind::UInt32, static_cast<std::uint32_t>(offsetof(StateTreeTask, Flags)),
     static_cast<std::uint32_t>(sizeof(StateTreeTask::Flags))},
}};

template <std::size_t N>
[[nodiscard]] bool AIFieldsEqual(const std::array<Reflect::FieldDesc, N>& InFields, const void* InA,
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

// Compile helpers. Everything here is fixed-capacity stack storage.
struct AICompileScratch
{
    StateTreeDefinition Out{};
    std::array<std::uint16_t, kMaxStateTreeNodes> Map{};
    std::array<std::uint8_t, kMaxStateTreeNodes> Visit{};
    std::uint32_t LeafCount = 0;
    std::uint32_t MaxDepth = 0;
    char Error[192]{};
};

[[nodiscard]] bool AICompileFail(AICompileScratch& InScratch, const char* InFormat, ...) noexcept
{
    // NOLINT(cppcoreguidelines-init-variables)
    va_list ScopedArguments;
    va_start(ScopedArguments, InFormat);
    std::vsnprintf(InScratch.Error, sizeof(InScratch.Error), InFormat, ScopedArguments);
    va_end(ScopedArguments);
    return false;
}

[[nodiscard]] bool AICompileEmit(const StateTreeDefinition& InDocument, std::uint16_t InEditorNode,
                                 std::uint16_t InEditorParent, std::uint16_t InCanonicalParent, std::uint8_t InDepth,
                                 AICompileScratch& InScratch) noexcept
{
    if (InEditorNode >= InDocument.NodeCount)
    {
        return AICompileFail(InScratch, "node %u is outside the document", static_cast<unsigned>(InEditorNode));
    }
    if (InScratch.Visit[InEditorNode] != 0)
    {
        return AICompileFail(InScratch, "hierarchy cycle or shared child at AI node %u",
                             static_cast<unsigned>(InEditorNode));
    }
    if (InDepth > kMaxStateTreeDepth)
    {
        return AICompileFail(InScratch, "AI node %u exceeds depth %u", static_cast<unsigned>(InEditorNode),
                             static_cast<unsigned>(kMaxStateTreeDepth));
    }

    const StateTreeNode& ScopedNode = InDocument.Nodes[InEditorNode];
    switch (ScopedNode.Kind)
    {
        case StateTreeNodeKind::Root:
        case StateTreeNodeKind::Selector:
        case StateTreeNodeKind::Leaf:
            break;
        default:
            return AICompileFail(InScratch, "AI node %u has an unknown node kind", static_cast<unsigned>(InEditorNode));
    }
    if (static_cast<std::uint8_t>(ScopedNode.Evaluator.Kind) >
        static_cast<std::uint8_t>(StateTreeEvaluatorKind::StateTimeAtLeast))
    {
        return AICompileFail(InScratch, "AI node %u has an unknown evaluator", static_cast<unsigned>(InEditorNode));
    }
    if (static_cast<std::uint8_t>(ScopedNode.Task.Kind) > static_cast<std::uint8_t>(StateTreeTaskKind::ClearTag))
    {
        return AICompileFail(InScratch, "AI node %u has an unknown task", static_cast<unsigned>(InEditorNode));
    }
    if ((ScopedNode.Evaluator.Kind == StateTreeEvaluatorKind::FloatBelow ||
         ScopedNode.Evaluator.Kind == StateTreeEvaluatorKind::FloatAbove) &&
        ScopedNode.Evaluator.FloatIndex >= 4)
    {
        return AICompileFail(InScratch, "AI node %u reads a float index outside 0..3",
                             static_cast<unsigned>(InEditorNode));
    }
    if (ScopedNode.Parent != InEditorParent)
    {
        return AICompileFail(InScratch, "AI node %u has an inconsistent parent", static_cast<unsigned>(InEditorNode));
    }
    if (ScopedNode.Depth != InDepth)
    {
        return AICompileFail(InScratch, "AI node %u has depth %u, expected %u", static_cast<unsigned>(InEditorNode),
                             static_cast<unsigned>(ScopedNode.Depth), static_cast<unsigned>(InDepth));
    }
    if (InDepth == 1)
    {
        if (ScopedNode.Kind != StateTreeNodeKind::Root || InEditorNode != InDocument.Root)
        {
            return AICompileFail(InScratch, "the AI tree must start at its single root");
        }
    }
    else if (ScopedNode.Kind == StateTreeNodeKind::Root)
    {
        return AICompileFail(InScratch, "AI node %u is a second root", static_cast<unsigned>(InEditorNode));
    }

    if (ScopedNode.Kind == StateTreeNodeKind::Leaf && ScopedNode.FirstChild != kStateTreeNone)
    {
        return AICompileFail(InScratch, "AI leaf %u cannot own children", static_cast<unsigned>(InEditorNode));
    }
    if (ScopedNode.Kind != StateTreeNodeKind::Leaf && ScopedNode.FirstChild == kStateTreeNone)
    {
        return AICompileFail(InScratch, "AI selector %u has no children", static_cast<unsigned>(InEditorNode));
    }

    const std::uint16_t ScopedCanonical = StateTreeAddNode(InScratch.Out, ScopedNode.Kind, InCanonicalParent);
    if (ScopedCanonical == kStateTreeNone)
    {
        return AICompileFail(InScratch, "AI tree table is full");
    }
    InScratch.Map[InEditorNode] = ScopedCanonical;
    InScratch.Visit[InEditorNode] = 1;
    StateTreeNode& ScopedOut = InScratch.Out.Nodes[ScopedCanonical];
    ScopedOut.Enabled = ScopedNode.Enabled;
    ScopedOut.Evaluator = ScopedNode.Evaluator;
    ScopedOut.Task = ScopedNode.Task;
    if (ScopedNode.Kind == StateTreeNodeKind::Leaf)
    {
        ++InScratch.LeafCount;
    }
    InScratch.MaxDepth = std::max(InScratch.MaxDepth, static_cast<std::uint32_t>(InDepth));

    std::uint16_t ScopedChild = ScopedNode.FirstChild;
    std::uint16_t ScopedPrevious = kStateTreeNone;
    std::uint32_t ScopedChildCount = 0;
    while (ScopedChild != kStateTreeNone)
    {
        if (ScopedChild >= InDocument.NodeCount || ScopedChildCount >= kMaxStateTreeNodes)
        {
            return AICompileFail(InScratch, "AI node %u has a broken child list", static_cast<unsigned>(InEditorNode));
        }
        if (InDocument.Nodes[ScopedChild].Parent != InEditorNode)
        {
            return AICompileFail(InScratch, "AI node %u is linked under the wrong parent",
                                 static_cast<unsigned>(ScopedChild));
        }
        if (!AICompileEmit(InDocument, ScopedChild, InEditorNode, ScopedCanonical,
                           static_cast<std::uint8_t>(InDepth + 1), InScratch))
        {
            return false;
        }
        ScopedPrevious = ScopedChild;
        ScopedChild = InDocument.Nodes[ScopedChild].NextSibling;
        ++ScopedChildCount;
    }
    (void)ScopedPrevious;
    if (ScopedNode.ChildCount != ScopedChildCount)
    {
        return AICompileFail(InScratch, "AI node %u has a stale child count", static_cast<unsigned>(InEditorNode));
    }

    InScratch.Visit[InEditorNode] = 2;
    return true;
}

struct AICycleScratch
{
    std::array<std::uint8_t, kMaxStateTreeNodes> State{};
    std::array<std::uint16_t, kMaxStateTreeNodes> Path{};
    std::uint16_t PathLength = 0;
    std::uint16_t DeadNode = kStateTreeNone;
};

[[nodiscard]] bool AICycleIsClosedDeadLoop(const StateTreeDefinition& InTree, const std::uint16_t* InPath,
                                           std::uint16_t InLength) noexcept
{
    std::array<bool, kMaxStateTreeNodes> ScopedInCycle{};
    for (std::uint16_t ScopedI = 0; ScopedI < InLength; ++ScopedI)
    {
        const std::uint16_t ScopedNode = InPath[ScopedI];
        if (ScopedNode >= InTree.NodeCount)
        {
            return false;
        }
        ScopedInCycle[ScopedNode] = true;
    }

    // A cycle that emits at least one task is an AI behaviour loop (patrol /
    for (std::uint16_t ScopedI = 0; ScopedI < InLength; ++ScopedI)
    {
        if (InTree.Nodes[InPath[ScopedI]].Task.Kind != StateTreeTaskKind::None)
        {
            return false;
        }
    }

    // The loop is dead only when no enabled transition leaves it.
    for (std::uint16_t ScopedI = 0; ScopedI < InLength; ++ScopedI)
    {
        const std::uint16_t ScopedNode = InPath[ScopedI];
        for (std::uint16_t ScopedT = 0; ScopedT < InTree.TransitionCount; ++ScopedT)
        {
            const StateTreeTransition& ScopedTransition = InTree.Transitions[ScopedT];
            if (!ScopedTransition.Enabled || ScopedTransition.FromLeaf != ScopedNode)
            {
                continue;
            }
            if (ScopedTransition.ToNode >= InTree.NodeCount || !ScopedInCycle[ScopedTransition.ToNode])
            {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] bool AICycleVisit(const StateTreeDefinition& InTree, std::uint16_t InNode,
                                AICycleScratch& InScratch) noexcept
{
    InScratch.State[InNode] = 1;
    InScratch.Path[InScratch.PathLength++] = InNode;

    for (std::uint16_t ScopedT = 0; ScopedT < InTree.TransitionCount; ++ScopedT)
    {
        const StateTreeTransition& ScopedTransition = InTree.Transitions[ScopedT];
        if (!ScopedTransition.Enabled || ScopedTransition.FromLeaf != InNode ||
            ScopedTransition.ToNode >= InTree.NodeCount)
        {
            continue;
        }
        const std::uint16_t ScopedNext = ScopedTransition.ToNode;
        if (InScratch.State[ScopedNext] == 0)
        {
            if (AICycleVisit(InTree, ScopedNext, InScratch))
            {
                return true;
            }
        }
        else if (InScratch.State[ScopedNext] == 1)
        {
            std::uint16_t ScopedStart = 0;
            while (ScopedStart < InScratch.PathLength && InScratch.Path[ScopedStart] != ScopedNext)
            {
                ++ScopedStart;
            }
            if (ScopedStart < InScratch.PathLength &&
                AICycleIsClosedDeadLoop(InTree, InScratch.Path.data() + ScopedStart,
                                        static_cast<std::uint16_t>(InScratch.PathLength - ScopedStart)))
            {
                InScratch.DeadNode = ScopedNext;
                return true;
            }
        }
    }

    --InScratch.PathLength;
    InScratch.State[InNode] = 2;
    return false;
}

// TOML helpers. These are editor/IO paths and may allocate.
[[nodiscard]] std::int64_t AIReadInt(const toml::table& InTable, std::string_view InKey,
                                     std::int64_t InFallback) noexcept
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

[[nodiscard]] bool AIReadBool(const toml::table& InTable, std::string_view InKey, bool InFallback) noexcept
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

[[nodiscard]] float AIReadFloat(const toml::table& InTable, std::string_view InKey, float InFallback) noexcept
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

[[nodiscard]] std::uint64_t AIReadMask(const toml::table& InTable, std::string_view InKey,
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

void AIAppendU64(std::string& InDestination, std::uint64_t InValue)
{
    char ScopedBuffer[32]{};
    std::snprintf(ScopedBuffer, sizeof(ScopedBuffer), "0x%016llx", static_cast<unsigned long long>(InValue));
    InDestination += '"';
    InDestination += ScopedBuffer;
    InDestination += '"';
}

void AIAppendFloat(std::string& InDestination, float InValue)
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

void AIAppendInt(std::string& InDestination, std::int64_t InValue)
{
    char ScopedBuffer[32]{};
    std::snprintf(ScopedBuffer, sizeof(ScopedBuffer), "%lld", static_cast<long long>(InValue));
    InDestination += ScopedBuffer;
}

void AIAppendBool(std::string& InDestination, bool InValue)
{
    InDestination += InValue ? "true" : "false";
}

void AIAppendNode(std::string& InDestination, const StateTreeNode& InNode, std::uint16_t InIndex, const Vector2D& InPosition)
{
    InDestination += "\n[[ai_state_tree.node]]\n";
    InDestination += "index = ";
    AIAppendInt(InDestination, InIndex);
    InDestination += "\nkind = ";
    AIAppendInt(InDestination, static_cast<std::int64_t>(InNode.Kind));
    InDestination += "\nparent = ";
    AIAppendInt(InDestination, InNode.Parent);
    InDestination += "\nfirst_child = ";
    AIAppendInt(InDestination, InNode.FirstChild);
    InDestination += "\nnext_sibling = ";
    AIAppendInt(InDestination, InNode.NextSibling);
    InDestination += "\nchild_count = ";
    AIAppendInt(InDestination, InNode.ChildCount);
    InDestination += "\ndepth = ";
    AIAppendInt(InDestination, InNode.Depth);
    InDestination += "\nenabled = ";
    AIAppendBool(InDestination, InNode.Enabled);
    InDestination += "\nevaluator_kind = ";
    AIAppendInt(InDestination, static_cast<std::int64_t>(InNode.Evaluator.Kind));
    InDestination += "\nevaluator_tag_mask = ";
    AIAppendU64(InDestination, InNode.Evaluator.TagMask);
    InDestination += "\nevaluator_threshold = ";
    AIAppendFloat(InDestination, InNode.Evaluator.Threshold);
    InDestination += "\nevaluator_float_index = ";
    AIAppendInt(InDestination, InNode.Evaluator.FloatIndex);
    InDestination += "\ntask_kind = ";
    AIAppendInt(InDestination, static_cast<std::int64_t>(InNode.Task.Kind));
    InDestination += "\ntask_tag_mask = ";
    AIAppendU64(InDestination, InNode.Task.TagMask);
    InDestination += "\ntask_speed = ";
    AIAppendFloat(InDestination, InNode.Task.Speed);
    InDestination += "\ntask_range = ";
    AIAppendFloat(InDestination, InNode.Task.Range);
    InDestination += "\ntask_flags = ";
    AIAppendInt(InDestination, InNode.Task.Flags);
    InDestination += "\nposition = [";
    AIAppendFloat(InDestination, InPosition.X);
    InDestination += ", ";
    AIAppendFloat(InDestination, InPosition.Y);
    InDestination += "]\n";
}

void AIAppendTransition(std::string& InDestination, const StateTreeTransition& InTransition)
{
    InDestination += "\n[[ai_state_tree.transition]]\n";
    InDestination += "from = ";
    AIAppendInt(InDestination, InTransition.FromLeaf);
    InDestination += "\nto = ";
    AIAppendInt(InDestination, InTransition.ToNode);
    InDestination += "\ntriggers = ";
    AIAppendU64(InDestination, InTransition.TriggerMask);
    InDestination += "\nconsume = ";
    AIAppendU64(InDestination, InTransition.ConsumeMask);
    InDestination += "\nrequire_tags = ";
    AIAppendU64(InDestination, InTransition.RequireTagsAll);
    InDestination += "\nforbid_tags = ";
    AIAppendU64(InDestination, InTransition.ForbidTagsAny);
    InDestination += "\nenabled = ";
    AIAppendBool(InDestination, InTransition.Enabled);
    InDestination += "\nconsume_all = ";
    AIAppendBool(InDestination, InTransition.ConsumeAll);
    InDestination += '\n';
}

[[nodiscard]] bool AIRemoveExistingBlock(std::string& InDestination)
{
    std::size_t ScopedBegin = InDestination.find(kAIBlockBegin);
    while (ScopedBegin != std::string::npos)
    {
        const std::size_t ScopedEnd = InDestination.find(kAIBlockEnd, ScopedBegin);
        if (ScopedEnd == std::string::npos)
        {
            // A truncated block means the scene was edited by hand. Refuse to
            return false;
        }
        InDestination.erase(ScopedBegin, ScopedEnd + kAIBlockEnd.size() - ScopedBegin);
        ScopedBegin = InDestination.find(kAIBlockBegin);
    }
    return true;
}
}

// Field inspection API
bool AIStateTreeEvaluatorFieldsEqual(const StateTreeEvaluator& InA, const StateTreeEvaluator& InB) noexcept
{
    return AIFieldsEqual(kAIEvaluatorFields, &InA, &InB);
}

bool AIStateTreeTaskFieldsEqual(const StateTreeTask& InA, const StateTreeTask& InB) noexcept
{
    return AIFieldsEqual(kAITaskFields, &InA, &InB);
}

const char* AIStateTreeEvaluatorName(StateTreeEvaluatorKind InKind) noexcept
{
    switch (InKind)
    {
        case StateTreeEvaluatorKind::AlwaysTrue:
            return "AlwaysTrue";
        case StateTreeEvaluatorKind::AlwaysFalse:
            return "AlwaysFalse";
        case StateTreeEvaluatorKind::TagsAny:
            return "TagsAny";
        case StateTreeEvaluatorKind::TagsAll:
            return "TagsAll";
        case StateTreeEvaluatorKind::TagsNone:
            return "TagsNone";
        case StateTreeEvaluatorKind::TargetWithinRadius:
            return "TargetWithinRadius";
        case StateTreeEvaluatorKind::TargetBeyondRadius:
            return "TargetBeyondRadius";
        case StateTreeEvaluatorKind::FloatBelow:
            return "FloatBelow";
        case StateTreeEvaluatorKind::FloatAbove:
            return "FloatAbove";
        case StateTreeEvaluatorKind::StateTimeAtLeast:
            return "StateTimeAtLeast";
    }
    return "?";
}

const char* AIStateTreeTaskName(StateTreeTaskKind InKind) noexcept
{
    switch (InKind)
    {
        case StateTreeTaskKind::None:
            return "None";
        case StateTreeTaskKind::MoveToTarget:
            return "MoveToTarget";
        case StateTreeTaskKind::Stop:
            return "Stop";
        case StateTreeTaskKind::AttackTarget:
            return "AttackTarget";
        case StateTreeTaskKind::SetTag:
            return "SetTag";
        case StateTreeTaskKind::ClearTag:
            return "ClearTag";
    }
    return "?";
}

const char* AIStateTreeKindName(StateTreeNodeKind InKind) noexcept
{
    switch (InKind)
    {
        case StateTreeNodeKind::Root:
            return "AI Root";
        case StateTreeNodeKind::Selector:
            return "AI Selector";
        case StateTreeNodeKind::Leaf:
            return "AI Leaf";
    }
    return "AI ?";
}

AICommandComponent AIStateTreeCommandFor(const StateTreeTask& InTask, std::uint16_t InNode) noexcept
{
    AICommandComponent ScopedCommand;
    ScopedCommand.TaskIndex = InNode;
    ScopedCommand.Speed = InTask.Speed;
    ScopedCommand.Range = InTask.Range;
    ScopedCommand.Flags = InTask.Flags;
    switch (InTask.Kind)
    {
        case StateTreeTaskKind::MoveToTarget:
            ScopedCommand.Kind = static_cast<std::uint32_t>(AICommandKind::MoveTo);
            break;
        case StateTreeTaskKind::Stop:
            ScopedCommand.Kind = static_cast<std::uint32_t>(AICommandKind::Stop);
            break;
        case StateTreeTaskKind::AttackTarget:
            ScopedCommand.Kind = static_cast<std::uint32_t>(AICommandKind::Attack);
            break;
        case StateTreeTaskKind::None:
        case StateTreeTaskKind::SetTag:
        case StateTreeTaskKind::ClearTag:
            ScopedCommand.Kind = static_cast<std::uint32_t>(AICommandKind::None);
            break;
    }
    return ScopedCommand;
}

// Lifetime / document
AIStateTreeCanvasPanel::~AIStateTreeCanvasPanel()
{
    Shutdown();
}

bool AIStateTreeCanvasPanel::Initialize(CommandStack& InCommands, std::uint32_t InTreeId) noexcept
{
    if (MInitialized)
    {
        return true;
    }
    MCommands = &InCommands;
    MDocument.Tree.TreeId = InTreeId;
    MCodec = InCommands.RegisterExternalBuffer("AIStateTreeDocument", &MDocument,
                                               static_cast<std::uint32_t>(sizeof(AIStateTreeDocument)));
    if (MCodec == 0xFFFFu)
    {
        SetStatus("AI canvas: CommandStack external buffer rejected");
        MCommands = nullptr;
        return false;
    }
    MCodecSize = InCommands.CodecSize(MCodec);
    if (MCodecSize == 0)
    {
        SetStatus("AI canvas: document codec has no size");
        MCommands = nullptr;
        MCodec = 0xFFFFu;
        return false;
    }
    MInitialized = true;
    BuildDefaultDocument();
    return true;
}

void AIStateTreeCanvasPanel::Shutdown() noexcept
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
    MPieActiveCount = 0;
    MPieActiveLeaf = kStateTreeNone;
    MPieActiveSet.fill(false);
}

void AIStateTreeCanvasPanel::ResetDocument() noexcept
{
    const std::uint32_t ScopedTreeId = MDocument.Tree.TreeId;
    MDocument = AIStateTreeDocument{};
    MDocument.Tree.TreeId = ScopedTreeId;
    MCompiled = AIStateTreeCompiled{};
    MCompiled.EditorIndex.fill(kStateTreeNone);
    MCompiledValid = false;
    MCompiledDirty = true;
    MSelectedNode = kStateTreeNone;
    MSelectedTransition = 0xFFFFu;
    MPieActiveCount = 0;
    MPieActiveLeaf = kStateTreeNone;
    MPieActiveSet.fill(false);
}

void AIStateTreeCanvasPanel::BuildDefaultDocument() noexcept
{
    ResetDocument();
    const std::uint16_t ScopedRoot = AddAINode(StateTreeNodeKind::Root, kStateTreeNone, Vector2D{0.0f, 0.0f});
    if (ScopedRoot == kStateTreeNone)
    {
        SetStatus("AI canvas: could not create the demo root");
        return;
    }
    const std::uint16_t ScopedCombat = AddAINode(StateTreeNodeKind::Selector, ScopedRoot, Vector2D{-230.0f, 150.0f});
    const std::uint16_t ScopedIdle = AddAINode(StateTreeNodeKind::Selector, ScopedRoot, Vector2D{230.0f, 150.0f});
    if (ScopedCombat == kStateTreeNone || ScopedIdle == kStateTreeNone)
    {
        SetStatus("AI canvas: could not create the demo selectors");
        return;
    }
    const std::uint16_t ScopedAttack = AddAINode(StateTreeNodeKind::Leaf, ScopedCombat, Vector2D{-340.0f, 320.0f});
    const std::uint16_t ScopedChase = AddAINode(StateTreeNodeKind::Leaf, ScopedCombat, Vector2D{-120.0f, 320.0f});
    const std::uint16_t ScopedWander = AddAINode(StateTreeNodeKind::Leaf, ScopedIdle, Vector2D{230.0f, 320.0f});
    if (ScopedAttack == kStateTreeNone || ScopedChase == kStateTreeNone || ScopedWander == kStateTreeNone)
    {
        SetStatus("AI canvas: could not create the demo leaves");
        return;
    }

    StateTreeNode& ScopedAttackNode = MDocument.Tree.Nodes[ScopedAttack];
    ScopedAttackNode.Evaluator.Kind = StateTreeEvaluatorKind::TargetWithinRadius;
    ScopedAttackNode.Evaluator.Threshold = 160.0f;
    ScopedAttackNode.Task.Kind = StateTreeTaskKind::AttackTarget;
    ScopedAttackNode.Task.Speed = 0.0f;
    ScopedAttackNode.Task.Range = 48.0f;

    StateTreeNode& ScopedChaseNode = MDocument.Tree.Nodes[ScopedChase];
    ScopedChaseNode.Evaluator.Kind = StateTreeEvaluatorKind::AlwaysTrue;
    ScopedChaseNode.Task.Kind = StateTreeTaskKind::MoveToTarget;
    ScopedChaseNode.Task.Speed = 140.0f;
    ScopedChaseNode.Task.Range = 24.0f;

    StateTreeNode& ScopedWanderNode = MDocument.Tree.Nodes[ScopedWander];
    ScopedWanderNode.Evaluator.Kind = StateTreeEvaluatorKind::TagsAny;
    ScopedWanderNode.Evaluator.TagMask = 1ull << 0;
    ScopedWanderNode.Task.Kind = StateTreeTaskKind::MoveToTarget;
    ScopedWanderNode.Task.Speed = 64.0f;
    ScopedWanderNode.Task.Range = 16.0f;

    if (AddAITransition(ScopedAttack, ScopedCombat))
    {
        StateTreeTransition& ScopedTransition = MDocument.Tree.Transitions[MDocument.Tree.TransitionCount - 1];
        ScopedTransition.TriggerMask = kStateTreeTriggerLeafFailed;
        ScopedTransition.ConsumeMask = kStateTreeTriggerLeafFailed;
    }
    if (AddAITransition(ScopedChase, ScopedAttack))
    {
        StateTreeTransition& ScopedTransition = MDocument.Tree.Transitions[MDocument.Tree.TransitionCount - 1];
        ScopedTransition.TriggerMask = 1ull << 0;
        ScopedTransition.ConsumeMask = 1ull << 0;
    }
    if (AddAITransition(ScopedWander, ScopedIdle))
    {
        StateTreeTransition& ScopedTransition = MDocument.Tree.Transitions[MDocument.Tree.TransitionCount - 1];
        ScopedTransition.TriggerMask = kStateTreeTriggerLeafFailed;
        ScopedTransition.ConsumeMask = kStateTreeTriggerLeafFailed;
    }

    MSelectedNode = ScopedRoot;
    MSelectedTransition = 0xFFFFu;
    MCompiledValid = false;
    MCompiledDirty = true;
    SetStatus("AI: demo tree loaded - press Compile AI to flatten it");
}

void AIStateTreeCanvasPanel::NotifyHistoryChanged() noexcept
{
    if (MCompiledValid)
    {
        MCompiledDirty = true;
    }
    // Indices in the restored document may refer to different states; never let
    MSelectedNode = kStateTreeNone;
    MSelectedTransition = 0xFFFFu;
}

void AIStateTreeCanvasPanel::MarkDocumentDirty() noexcept
{
    MCompiledDirty = true;
}

std::uint16_t AIStateTreeCanvasPanel::AddAINode(StateTreeNodeKind InKind, std::uint16_t InParent,
                                                Vector2D InPosition) noexcept
{
    if (MDocument.Tree.NodeCount >= kMaxStateTreeNodes)
    {
        SetStatus("AI: node table is full");
        return kStateTreeNone;
    }

    std::uint16_t ScopedCreated = kStateTreeNone;
    if (MDocument.Tree.NodeCount == 0)
    {
        if (InKind != StateTreeNodeKind::Root || InParent != kStateTreeNone)
        {
            SetStatus("AI: the first state must be the single root");
            return kStateTreeNone;
        }
        ScopedCreated = StateTreeAddNode(MDocument.Tree, InKind, kStateTreeNone);
    }
    else
    {
        if (InKind == StateTreeNodeKind::Root || InParent >= MDocument.Tree.NodeCount)
        {
            SetStatus("AI: a child needs a valid non-root parent");
            return kStateTreeNone;
        }
        const StateTreeNode& ScopedParent = MDocument.Tree.Nodes[InParent];
        if (ScopedParent.Kind == StateTreeNodeKind::Leaf)
        {
            SetStatus("AI: an AI leaf cannot own children");
            return kStateTreeNone;
        }
        if (ScopedParent.Depth + 1 > kMaxStateTreeDepth)
        {
            SetStatus("AI: depth limit %u reached", static_cast<unsigned>(kMaxStateTreeDepth));
            return kStateTreeNone;
        }
        if (ScopedParent.Depth + 1 == kMaxStateTreeDepth && InKind != StateTreeNodeKind::Leaf)
        {
            SetStatus("AI: depth %u can only hold leaves", static_cast<unsigned>(kMaxStateTreeDepth));
            return kStateTreeNone;
        }
        ScopedCreated = StateTreeAddNode(MDocument.Tree, InKind, InParent);
    }

    if (ScopedCreated == kStateTreeNone)
    {
        SetStatus("AI: StateTreeAddNode rejected the new state");
        return kStateTreeNone;
    }
    MDocument.Positions[ScopedCreated] = InPosition;
    StateTreeNode& ScopedNode = MDocument.Tree.Nodes[ScopedCreated];
    if (InKind == StateTreeNodeKind::Leaf)
    {
        // The two inspector dropdowns expose exactly these flat functions.
        ScopedNode.Evaluator.Kind = StateTreeEvaluatorKind::TargetWithinRadius;
        ScopedNode.Evaluator.Threshold = 128.0f;
        ScopedNode.Task.Kind = StateTreeTaskKind::MoveToTarget;
        ScopedNode.Task.Speed = 128.0f;
        ScopedNode.Task.Range = 32.0f;
    }
    MSelectedNode = ScopedCreated;
    MSelectedTransition = 0xFFFFu;
    MarkDocumentDirty();
    return ScopedCreated;
}

bool AIStateTreeCanvasPanel::DeleteAINode(std::uint16_t InNode) noexcept
{
    if (InNode >= MDocument.Tree.NodeCount || InNode == MDocument.Tree.Root)
    {
        SetStatus("AI: the root or an invalid state cannot be deleted");
        return false;
    }

    std::array<StateTreeNode, kMaxStateTreeNodes> ScopedOriginal = MDocument.Tree.Nodes;
    std::array<bool, kMaxStateTreeNodes> ScopedRemoved{};
    std::array<std::uint16_t, kMaxStateTreeNodes> ScopedStack{};
    std::uint32_t ScopedStackCount = 0;
    ScopedStack[ScopedStackCount++] = InNode;
    while (ScopedStackCount > 0)
    {
        const std::uint16_t ScopedNode = ScopedStack[--ScopedStackCount];
        if (ScopedNode >= MDocument.Tree.NodeCount || ScopedRemoved[ScopedNode])
        {
            continue;
        }
        ScopedRemoved[ScopedNode] = true;
        std::uint16_t ScopedChild = ScopedOriginal[ScopedNode].FirstChild;
        std::uint32_t ScopedGuard = 0;
        while (ScopedChild != kStateTreeNone && ScopedChild < kMaxStateTreeNodes && ScopedGuard++ < kMaxStateTreeNodes)
        {
            if (!ScopedRemoved[ScopedChild])
            {
                ScopedStack[ScopedStackCount++] = ScopedChild;
            }
            ScopedChild = ScopedOriginal[ScopedChild].NextSibling;
        }
    }

    StateTreeDefinition ScopedRebuilt;
    ScopedRebuilt.TreeId = MDocument.Tree.TreeId;
    std::array<std::uint16_t, kMaxStateTreeNodes> ScopedMap{};
    ScopedMap.fill(kStateTreeNone);
    std::array<Vector2D, kMaxStateTreeNodes> ScopedPositions{};
    for (std::uint16_t ScopedOld = 0; ScopedOld < MDocument.Tree.NodeCount; ++ScopedOld)
    {
        if (ScopedRemoved[ScopedOld])
        {
            continue;
        }
        ScopedMap[ScopedOld] = ScopedRebuilt.NodeCount;
        ScopedRebuilt.Nodes[ScopedRebuilt.NodeCount] = ScopedOriginal[ScopedOld];
        ScopedPositions[ScopedRebuilt.NodeCount] = MDocument.Positions[ScopedOld];
        ++ScopedRebuilt.NodeCount;
    }

    ScopedRebuilt.Root = ScopedMap[MDocument.Tree.Root];
    if (ScopedRebuilt.Root == kStateTreeNone)
    {
        SetStatus("AI: delete would remove the root");
        return false;
    }
    for (std::uint16_t ScopedNew = 0; ScopedNew < ScopedRebuilt.NodeCount; ++ScopedNew)
    {
        StateTreeNode& ScopedNode = ScopedRebuilt.Nodes[ScopedNew];
        ScopedNode.Parent = ScopedNode.Parent == kStateTreeNone ? kStateTreeNone : ScopedMap[ScopedNode.Parent];
        ScopedNode.FirstChild =
            ScopedNode.FirstChild == kStateTreeNone ? kStateTreeNone : ScopedMap[ScopedNode.FirstChild];
        ScopedNode.NextSibling =
            ScopedNode.NextSibling == kStateTreeNone ? kStateTreeNone : ScopedMap[ScopedNode.NextSibling];
        ScopedNode.ChildCount = 0;
    }
    for (std::uint16_t ScopedNew = 0; ScopedNew < ScopedRebuilt.NodeCount; ++ScopedNew)
    {
        StateTreeNode& ScopedNode = ScopedRebuilt.Nodes[ScopedNew];
        std::uint16_t ScopedChild = ScopedNode.FirstChild;
        std::uint32_t ScopedCount = 0;
        std::uint32_t ScopedGuard = 0;
        while (ScopedChild != kStateTreeNone && ScopedChild < ScopedRebuilt.NodeCount &&
               ScopedGuard++ < kMaxStateTreeNodes)
        {
            ++ScopedCount;
            ScopedChild = ScopedRebuilt.Nodes[ScopedChild].NextSibling;
        }
        ScopedNode.ChildCount = static_cast<std::uint16_t>(ScopedCount);
    }

    ScopedRebuilt.TransitionCount = 0;
    for (std::uint16_t ScopedT = 0; ScopedT < MDocument.Tree.TransitionCount; ++ScopedT)
    {
        const StateTreeTransition& ScopedTransition = MDocument.Tree.Transitions[ScopedT];
        const bool ScopedFromRemoved =
            ScopedTransition.FromLeaf != kStateTreeNone &&
            (ScopedTransition.FromLeaf >= MDocument.Tree.NodeCount || ScopedRemoved[ScopedTransition.FromLeaf]);
        const bool ScopedToRemoved =
            ScopedTransition.ToNode >= MDocument.Tree.NodeCount || ScopedRemoved[ScopedTransition.ToNode];
        if (ScopedFromRemoved || ScopedToRemoved)
        {
            continue;
        }
        StateTreeTransition ScopedCopy = ScopedTransition;
        if (ScopedCopy.FromLeaf != kStateTreeNone)
        {
            ScopedCopy.FromLeaf = ScopedMap[ScopedCopy.FromLeaf];
        }
        ScopedCopy.ToNode = ScopedMap[ScopedCopy.ToNode];
        ScopedRebuilt.Transitions[ScopedRebuilt.TransitionCount++] = ScopedCopy;
    }

    MDocument.Tree = ScopedRebuilt;
    MDocument.Positions = ScopedPositions;
    MSelectedNode = kStateTreeNone;
    MSelectedTransition = 0xFFFFu;
    MarkDocumentDirty();
    SetStatus("AI: subtree deleted (%u states remain)", static_cast<unsigned>(MDocument.Tree.NodeCount));
    return true;
}

bool AIStateTreeCanvasPanel::AddAITransition(std::uint16_t InFromLeaf, std::uint16_t InToNode) noexcept
{
    if (InFromLeaf >= MDocument.Tree.NodeCount || InToNode >= MDocument.Tree.NodeCount)
    {
        SetStatus("AI: transition endpoint is invalid");
        return false;
    }
    if (MDocument.Tree.Nodes[InFromLeaf].Kind != StateTreeNodeKind::Leaf)
    {
        SetStatus("AI: transitions must start at a leaf");
        return false;
    }
    StateTreeTransition ScopedTransition;
    ScopedTransition.FromLeaf = InFromLeaf;
    ScopedTransition.ToNode = InToNode;
    ScopedTransition.TriggerMask = 1ull << 0;
    ScopedTransition.ConsumeMask = 1ull << 0;
    ScopedTransition.Enabled = true;
    if (!StateTreeAddTransition(MDocument.Tree, ScopedTransition))
    {
        SetStatus("AI: transition table is full");
        return false;
    }
    MSelectedTransition = static_cast<std::uint16_t>(MDocument.Tree.TransitionCount - 1);
    MSelectedNode = kStateTreeNone;
    MarkDocumentDirty();
    return true;
}

bool AIStateTreeCanvasPanel::RemoveAITransition(std::uint16_t InTransition) noexcept
{
    if (InTransition >= MDocument.Tree.TransitionCount)
    {
        return false;
    }
    for (std::uint16_t ScopedI = static_cast<std::uint16_t>(InTransition + 1); ScopedI < MDocument.Tree.TransitionCount;
         ++ScopedI)
    {
        MDocument.Tree.Transitions[ScopedI - 1] = MDocument.Tree.Transitions[ScopedI];
    }
    --MDocument.Tree.TransitionCount;
    MSelectedTransition = 0xFFFFu;
    MarkDocumentDirty();
    return true;
}

bool AIStateTreeCanvasPanel::SetAINodePosition(std::uint16_t InNode, Vector2D InPosition) noexcept
{
    if (InNode >= MDocument.Tree.NodeCount)
    {
        return false;
    }
    if (MDocument.Positions[InNode].X == InPosition.X && MDocument.Positions[InNode].Y == InPosition.Y)
    {
        return false;
    }
    MDocument.Positions[InNode] = InPosition;
    MarkDocumentDirty();
    return true;
}

// Transactions
bool AIStateTreeCanvasPanel::BeginEdit(const char* InLabel, EditSource InSource) noexcept
{
    if (!MInitialized || MCommands == nullptr || MCodec == 0xFFFFu)
    {
        SetStatus("AI: canvas is not initialized");
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
    const auto ScopedCaptured =
        MCommands->CaptureComponent(entt::null, MCodec, std::span<std::byte>(MBefore.data(), MCodecSize));
    if (ScopedCaptured != CommandStack::Result::Ok)
    {
        SetStatus("AI: cannot capture document for undo");
        return false;
    }
    const auto ScopedBegun = MCommands->BeginTransaction(InLabel);
    if (ScopedBegun != CommandStack::Result::Ok)
    {
        SetStatus("AI: transaction rejected: %s", CommandStack::ResultName(ScopedBegun));
        return false;
    }
    MEditing = true;
    MEditSource = InSource;
    MCanvasDrag = InSource == EditSource::CanvasDrag;
    return true;
}

void AIStateTreeCanvasPanel::RecordEdit() noexcept
{
    if (!MEditing || MCommands == nullptr)
    {
        return;
    }
    if (MCommands->CaptureComponent(entt::null, MCodec, std::span<std::byte>(MAfter.data(), MCodecSize)) !=
        CommandStack::Result::Ok)
    {
        SetStatus("AI: cannot capture edited document");
        return;
    }
    const auto ScopedRecorded =
        MCommands->Record(entt::null, MCodec, std::span<const std::byte>(MBefore.data(), MCodecSize),
                          std::span<const std::byte>(MAfter.data(), MCodecSize));
    if (ScopedRecorded != CommandStack::Result::Ok)
    {
        SetStatus("AI: document delta rejected: %s", CommandStack::ResultName(ScopedRecorded));
    }
}

void AIStateTreeCanvasPanel::CommitEdit() noexcept
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
        SetStatus("AI: transaction commit rejected: %s", CommandStack::ResultName(ScopedCommitted));
        return;
    }
    if (ScopedChanged)
    {
        MarkDocumentDirty();
    }
}

void AIStateTreeCanvasPanel::CancelEdit() noexcept
{
    if (MEditing && MCommands != nullptr)
    {
        MCommands->CancelTransaction();
    }
    MEditing = false;
    MCanvasDrag = false;
    MEditSource = EditSource::None;
}

bool AIStateTreeCanvasPanel::BeginInspectorEdit(const char* InLabel) noexcept
{
    return BeginEdit(InLabel, EditSource::Inspector);
}

void AIStateTreeCanvasPanel::FinishInspectorEdit(bool InCommitted) noexcept
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

// CompileTree: validate + flatten without allocation
bool AIStateTreeCanvasPanel::CompileTree() noexcept
{
    // Until this call succeeds, the previous compiled image no longer describes
    MCompiledDirty = true;
    const StateTreeDefinition& ScopedDocument = MDocument.Tree;
    if (ScopedDocument.NodeCount == 0)
    {
        SetStatus("AI compile: the tree is empty");
        return false;
    }
    if (ScopedDocument.NodeCount > kMaxStateTreeNodes)
    {
        SetStatus("AI compile: too many states (%u)", static_cast<unsigned>(ScopedDocument.NodeCount));
        return false;
    }
    if (ScopedDocument.Root >= ScopedDocument.NodeCount ||
        ScopedDocument.Nodes[ScopedDocument.Root].Kind != StateTreeNodeKind::Root)
    {
        SetStatus("AI compile: the root state is missing");
        return false;
    }
    if (ScopedDocument.Root != 0)
    {
        SetStatus("AI compile: the root must be node 0");
        return false;
    }

    AICompileScratch ScopedScratch;
    ScopedScratch.Out.TreeId = ScopedDocument.TreeId;
    ScopedScratch.Map.fill(kStateTreeNone);
    if (!AICompileEmit(ScopedDocument, ScopedDocument.Root, kStateTreeNone, kStateTreeNone, 1, ScopedScratch))
    {
        SetStatus("AI compile: %s", ScopedScratch.Error);
        return false;
    }
    if (ScopedScratch.Out.NodeCount != ScopedDocument.NodeCount)
    {
        SetStatus("AI compile: %u unreachable state(s)",
                  static_cast<unsigned>(ScopedDocument.NodeCount - ScopedScratch.Out.NodeCount));
        return false;
    }
    if (ScopedScratch.LeafCount == 0)
    {
        SetStatus("AI compile: the tree has no leaf state");
        return false;
    }
    if (ScopedDocument.TransitionCount > kMaxStateTreeTransitions)
    {
        SetStatus("AI compile: too many transitions");
        return false;
    }

    for (std::uint16_t ScopedT = 0; ScopedT < ScopedDocument.TransitionCount; ++ScopedT)
    {
        const StateTreeTransition& ScopedTransition = ScopedDocument.Transitions[ScopedT];
        StateTreeTransition ScopedCopy = ScopedTransition;
        if (ScopedCopy.TriggerMask == 0)
        {
            SetStatus("AI compile: transition %u has an empty trigger mask", static_cast<unsigned>(ScopedT));
            return false;
        }
        if (ScopedCopy.FromLeaf != kStateTreeNone)
        {
            if (ScopedCopy.FromLeaf >= ScopedDocument.NodeCount)
            {
                SetStatus("AI compile: transition %u has an invalid source", static_cast<unsigned>(ScopedT));
                return false;
            }
            if (ScopedDocument.Nodes[ScopedCopy.FromLeaf].Kind != StateTreeNodeKind::Leaf)
            {
                SetStatus("AI compile: transition %u starts outside a leaf", static_cast<unsigned>(ScopedT));
                return false;
            }
            ScopedCopy.FromLeaf = ScopedScratch.Map[ScopedCopy.FromLeaf];
        }
        if (ScopedCopy.ToNode >= ScopedDocument.NodeCount)
        {
            SetStatus("AI compile: transition %u has an invalid target", static_cast<unsigned>(ScopedT));
            return false;
        }
        ScopedCopy.ToNode = ScopedScratch.Map[ScopedCopy.ToNode];
        if (ScopedCopy.ToNode == kStateTreeNone)
        {
            SetStatus("AI compile: transition %u references a removed state", static_cast<unsigned>(ScopedT));
            return false;
        }
        if (!StateTreeAddTransition(ScopedScratch.Out, ScopedCopy))
        {
            SetStatus("AI compile: transition %u was rejected", static_cast<unsigned>(ScopedT));
            return false;
        }
    }

    AICycleScratch ScopedCycle;
    for (std::uint16_t ScopedNode = 0; ScopedNode < ScopedScratch.Out.NodeCount; ++ScopedNode)
    {
        if (ScopedCycle.State[ScopedNode] != 0)
        {
            continue;
        }
        if (AICycleVisit(ScopedScratch.Out, ScopedNode, ScopedCycle))
        {
            SetStatus("AI compile: closed transition dead loop at state %u",
                      static_cast<unsigned>(ScopedCycle.DeadNode));
            return false;
        }
    }

    MCompiled.Tree = ScopedScratch.Out;
    MCompiled.EditorIndex.fill(kStateTreeNone);
    for (std::uint16_t ScopedEditor = 0; ScopedEditor < ScopedDocument.NodeCount; ++ScopedEditor)
    {
        const std::uint16_t ScopedCanonical = ScopedScratch.Map[ScopedEditor];
        if (ScopedCanonical != kStateTreeNone && ScopedCanonical < kMaxStateTreeNodes)
        {
            MCompiled.EditorIndex[ScopedCanonical] = ScopedEditor;
        }
    }
    MCompiled.LeafCount = ScopedScratch.LeafCount;
    MCompiled.MaxDepth = ScopedScratch.MaxDepth;
    MCompiledValid = true;
    MCompiledDirty = false;
    SetStatus("AI compile: %u states, %u transitions, depth %u, %u leaves",
              static_cast<unsigned>(MCompiled.Tree.NodeCount), static_cast<unsigned>(MCompiled.Tree.TransitionCount),
              static_cast<unsigned>(MCompiled.MaxDepth), static_cast<unsigned>(MCompiled.LeafCount));
    return true;
}

// TOML persistence. The compiled tree is appended to the scene as a marked
bool AIStateTreeCanvasPanel::SaveCompiledToSceneToml()
{
    if (!MCompiledValid)
    {
        SetStatus("AI save: compile the tree first");
        return false;
    }
    if (MScenePath.empty())
    {
        SetStatus("AI save: no scene path is bound");
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
        SetStatus("AI save: scene TOML is missing or empty");
        return false;
    }
    if (!AIRemoveExistingBlock(ScopedText))
    {
        SetStatus("AI save: scene has a truncated AI state tree block");
        return false;
    }

    ScopedText += "\n\n";
    ScopedText += kAIBlockBegin;
    ScopedText += "\n# Generated by AIStateTreeCanvasPanel::CompileTree(). Do not edit by hand.\n";
    ScopedText += "[ai_state_tree]\nversion = 1\ntree_id = ";
    AIAppendInt(ScopedText, MCompiled.Tree.TreeId);
    ScopedText += "\nroot = ";
    AIAppendInt(ScopedText, MCompiled.Tree.Root);
    ScopedText += "\nnode_count = ";
    AIAppendInt(ScopedText, MCompiled.Tree.NodeCount);
    ScopedText += "\ntransition_count = ";
    AIAppendInt(ScopedText, MCompiled.Tree.TransitionCount);
    ScopedText += '\n';

    for (std::uint16_t ScopedNode = 0; ScopedNode < MCompiled.Tree.NodeCount; ++ScopedNode)
    {
        const std::uint16_t ScopedEditor = MCompiled.EditorIndex[ScopedNode];
        const Vector2D ScopedPosition =
            ScopedEditor < kMaxStateTreeNodes ? MDocument.Positions[ScopedEditor] : Vector2D{};
        AIAppendNode(ScopedText, MCompiled.Tree.Nodes[ScopedNode], ScopedNode, ScopedPosition);
    }
    for (std::uint16_t ScopedT = 0; ScopedT < MCompiled.Tree.TransitionCount; ++ScopedT)
    {
        AIAppendTransition(ScopedText, MCompiled.Tree.Transitions[ScopedT]);
    }
    ScopedText += kAIBlockEnd;
    ScopedText += '\n';

    std::string ScopedError;
    if (!WriteFileAtomically(MScenePath, ScopedText, &ScopedError))
    {
        SetStatus("AI save: %s", ScopedError.c_str());
        return false;
    }
    SetStatus("AI save: %u states written to the scene TOML", static_cast<unsigned>(MCompiled.Tree.NodeCount));
    return true;
}

bool AIStateTreeCanvasPanel::LoadCompiledFromSceneToml()
{
    if (MScenePath.empty())
    {
        SetStatus("AI load: no scene path is bound");
        return false;
    }
    std::ifstream ScopedFile(MScenePath, std::ios::binary);
    if (!ScopedFile)
    {
        BuildDefaultDocument();
        SetStatus("AI load: scene file not found; demo tree loaded");
        return false;
    }
    std::string ScopedText((std::istreambuf_iterator<char>(ScopedFile)), std::istreambuf_iterator<char>());
    if (ScopedText.find(kAIBlockBegin) == std::string::npos)
    {
        BuildDefaultDocument();
        SetStatus("AI load: no compiled AI tree in the scene; demo tree loaded");
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
        SetStatus("AI load: TOML error: %s", std::string(ScopedParseError.description()).c_str());
        return false;
    }
    const toml::table* ScopedTree = ScopedRoot["ai_state_tree"].as_table();
    if (ScopedTree == nullptr)
    {
        BuildDefaultDocument();
        SetStatus("AI load: ai_state_tree section is missing");
        return false;
    }
    const toml::array* ScopedNodes = (*ScopedTree)["node"].as_array();
    const toml::array* ScopedTransitions = (*ScopedTree)["transition"].as_array();
    if (ScopedNodes == nullptr || ScopedNodes->size() > kMaxStateTreeNodes)
    {
        BuildDefaultDocument();
        SetStatus("AI load: invalid ai_state_tree.node array");
        return false;
    }
    if (ScopedTransitions != nullptr && ScopedTransitions->size() > kMaxStateTreeTransitions)
    {
        BuildDefaultDocument();
        SetStatus("AI load: too many ai_state_tree.transition entries");
        return false;
    }

    StateTreeDefinition ScopedDefinition;
    ScopedDefinition.TreeId =
        static_cast<std::uint32_t>(AIReadInt(*ScopedTree, "tree_id", static_cast<std::int64_t>(MDocument.Tree.TreeId)));
    ScopedDefinition.Root = static_cast<std::uint16_t>(AIReadInt(*ScopedTree, "root", 0));
    ScopedDefinition.NodeCount = static_cast<std::uint16_t>(ScopedNodes->size());
    std::array<Vector2D, kMaxStateTreeNodes> ScopedPositions{};
    for (std::size_t ScopedI = 0; ScopedI < ScopedNodes->size(); ++ScopedI)
    {
        const toml::table* ScopedTable = ScopedNodes->get(ScopedI)->as_table();
        if (ScopedTable == nullptr)
        {
            BuildDefaultDocument();
            SetStatus("AI load: node %u is not a TOML table", static_cast<unsigned>(ScopedI));
            return false;
        }
        StateTreeNode& ScopedNode = ScopedDefinition.Nodes[ScopedI];
        ScopedNode.Kind = static_cast<StateTreeNodeKind>(AIReadInt(*ScopedTable, "kind", 0));
        ScopedNode.Parent = static_cast<std::uint16_t>(AIReadInt(*ScopedTable, "parent", kStateTreeNone));
        ScopedNode.FirstChild = static_cast<std::uint16_t>(AIReadInt(*ScopedTable, "first_child", kStateTreeNone));
        ScopedNode.NextSibling = static_cast<std::uint16_t>(AIReadInt(*ScopedTable, "next_sibling", kStateTreeNone));
        ScopedNode.ChildCount = static_cast<std::uint16_t>(AIReadInt(*ScopedTable, "child_count", 0));
        ScopedNode.Depth = static_cast<std::uint8_t>(AIReadInt(*ScopedTable, "depth", 1));
        ScopedNode.Enabled = AIReadBool(*ScopedTable, "enabled", true);
        ScopedNode.Evaluator.Kind = static_cast<StateTreeEvaluatorKind>(AIReadInt(*ScopedTable, "evaluator_kind", 0));
        ScopedNode.Evaluator.TagMask = AIReadMask(*ScopedTable, "evaluator_tag_mask", 0);
        ScopedNode.Evaluator.Threshold = AIReadFloat(*ScopedTable, "evaluator_threshold", 0.0f);
        ScopedNode.Evaluator.FloatIndex =
            static_cast<std::uint16_t>(AIReadInt(*ScopedTable, "evaluator_float_index", 0));
        ScopedNode.Task.Kind = static_cast<StateTreeTaskKind>(AIReadInt(*ScopedTable, "task_kind", 0));
        ScopedNode.Task.TagMask = AIReadMask(*ScopedTable, "task_tag_mask", 0);
        ScopedNode.Task.Speed = AIReadFloat(*ScopedTable, "task_speed", 0.0f);
        ScopedNode.Task.Range = AIReadFloat(*ScopedTable, "task_range", 0.0f);
        ScopedNode.Task.Flags = static_cast<std::uint32_t>(AIReadInt(*ScopedTable, "task_flags", 0));
        if (const toml::array* ScopedPosition = (*ScopedTable)["position"].as_array();
            ScopedPosition != nullptr && ScopedPosition->size() >= 2)
        {
            const auto ScopedX = ScopedPosition->get(0)->value<double>();
            const auto ScopedY = ScopedPosition->get(1)->value<double>();
            ScopedPositions[ScopedI] =
                Vector2D{static_cast<float>(ScopedX.value_or(0.0)), static_cast<float>(ScopedY.value_or(0.0))};
        }
    }
    if (ScopedTransitions != nullptr)
    {
        ScopedDefinition.TransitionCount = static_cast<std::uint16_t>(ScopedTransitions->size());
        for (std::size_t ScopedI = 0; ScopedI < ScopedTransitions->size(); ++ScopedI)
        {
            const toml::table* ScopedTable = ScopedTransitions->get(ScopedI)->as_table();
            if (ScopedTable == nullptr)
            {
                BuildDefaultDocument();
                SetStatus("AI load: transition %u is not a TOML table", static_cast<unsigned>(ScopedI));
                return false;
            }
            StateTreeTransition& ScopedTransition = ScopedDefinition.Transitions[ScopedI];
            ScopedTransition.FromLeaf = static_cast<std::uint16_t>(AIReadInt(*ScopedTable, "from", kStateTreeNone));
            ScopedTransition.ToNode = static_cast<std::uint16_t>(AIReadInt(*ScopedTable, "to", 0));
            ScopedTransition.TriggerMask = AIReadMask(*ScopedTable, "triggers", 0);
            ScopedTransition.ConsumeMask = AIReadMask(*ScopedTable, "consume", 0);
            ScopedTransition.RequireTagsAll = AIReadMask(*ScopedTable, "require_tags", 0);
            ScopedTransition.ForbidTagsAny = AIReadMask(*ScopedTable, "forbid_tags", 0);
            ScopedTransition.Enabled = AIReadBool(*ScopedTable, "enabled", true);
            ScopedTransition.ConsumeAll = AIReadBool(*ScopedTable, "consume_all", false);
        }
    }

    MDocument.Tree = ScopedDefinition;
    MDocument.Positions = ScopedPositions;
    MCompiledValid = false;
    MCompiledDirty = true;
    if (!CompileTree())
    {
        char ScopedCompileError[kAIStateTreeStatusBytes]{};
        std::snprintf(ScopedCompileError, sizeof(ScopedCompileError), "%s", MStatus);
        BuildDefaultDocument();
        SetStatus("AI load rejected: %s", ScopedCompileError);
        return false;
    }
    SetStatus("AI load: %u states restored from the scene TOML", static_cast<unsigned>(MCompiled.Tree.NodeCount));
    return true;
}

// PIE active path
void AIStateTreeCanvasPanel::BindRuntimeWorld(const Ecs::Registry* InRuntimeWorld,
                                              const Ecs::Registry* InSelectionWorld, bool InPieActive) noexcept
{
    MRuntimeWorld = InRuntimeWorld;
    MSelectionWorld = InSelectionWorld;
    MPieActive = InPieActive;
    if (!MPieActive)
    {
        MPieActiveCount = 0;
        MPieActiveLeaf = kStateTreeNone;
        MPieActiveSet.fill(false);
    }
}

std::uint32_t AIStateTreeCanvasPanel::RefreshPIEActivePath() noexcept
{
    MPieActiveSet.fill(false);
    MPieActiveCount = 0;
    MPieActiveLeaf = kStateTreeNone;
    if (!MPieActive || MRuntimeWorld == nullptr || !MCompiledValid || MCompiledDirty)
    {
        return 0;
    }

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
        return 0;
    }
    const StateTreeComponent* ScopedState = MRuntimeWorld->try_get<StateTreeComponent>(ScopedSelected);
    if (ScopedState == nullptr || ScopedState->TreeId != MCompiled.Tree.TreeId)
    {
        return 0;
    }

    const std::uint32_t ScopedDepth = std::min<std::uint32_t>(ScopedState->ActiveDepth, kMaxStateTreeDepth);
    for (std::uint32_t ScopedI = 0; ScopedI < ScopedDepth; ++ScopedI)
    {
        const std::uint16_t ScopedCompiled = ScopedState->ActiveStates[ScopedI];
        if (ScopedCompiled == kStateTreeNone || ScopedCompiled >= MCompiled.Tree.NodeCount)
        {
            continue;
        }
        const std::uint16_t ScopedEditor = MCompiled.EditorIndex[ScopedCompiled];
        if (ScopedEditor < MDocument.Tree.NodeCount)
        {
            if (!MPieActiveSet[ScopedEditor])
            {
                MPieActiveSet[ScopedEditor] = true;
                ++MPieActiveCount;
            }
            MPieActiveLeaf = ScopedEditor;
        }
    }
    if (ScopedState->CurrentLeaf != kStateTreeNone && ScopedState->CurrentLeaf < MCompiled.Tree.NodeCount)
    {
        const std::uint16_t ScopedEditor = MCompiled.EditorIndex[ScopedState->CurrentLeaf];
        if (ScopedEditor < MDocument.Tree.NodeCount)
        {
            MPieActiveLeaf = ScopedEditor;
        }
    }
    return MPieActiveCount;
}

bool AIStateTreeCanvasPanel::IsAINodePIEActive(std::uint16_t InNode) const noexcept
{
    return InNode < kMaxStateTreeNodes && MPieActiveSet[InNode];
}

// Canvas
void AIStateTreeCanvasPanel::Draw(const char* InWindowTitle, bool* InOpen)
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
        (void)RefreshPIEActivePath();

        const ImVec2 ScopedAvailable = ImGui::GetContentRegionAvail();
        const bool ScopedSplit = ScopedAvailable.x >= 760.0f;
        if (ScopedSplit)
        {
            const float ScopedCanvasWidth = std::max(360.0f, ScopedAvailable.x - 390.0f);
            if (ImGui::BeginChild("##ai_state_tree_canvas", ImVec2(ScopedCanvasWidth, 0.0f), ImGuiChildFlags_Borders,
                                  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
            {
                DrawCanvas();
            }
            ImGui::EndChild();
            ImGui::SameLine();
            if (ImGui::BeginChild("##ai_state_tree_inspector", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders))
            {
                DrawInspector();
            }
            ImGui::EndChild();
        }
        else
        {
            const float ScopedCanvasHeight = std::max(220.0f, ScopedAvailable.y * 0.56f);
            if (ImGui::BeginChild("##ai_state_tree_canvas", ImVec2(0.0f, ScopedCanvasHeight), ImGuiChildFlags_Borders,
                                  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
            {
                DrawCanvas();
            }
            ImGui::EndChild();
            if (ImGui::BeginChild("##ai_state_tree_inspector", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders))
            {
                DrawInspector();
            }
            ImGui::EndChild();
        }
    }
    EndEditorPanel();
    ImNodes::SetCurrentContext(ScopedPrevious);
}

void AIStateTreeCanvasPanel::DrawToolbar()
{
    ImGui::TextUnformatted("AI State Tree");
    ImGui::SameLine();
    ImGui::TextDisabled("%u / %u states, %u transitions", static_cast<unsigned>(MDocument.Tree.NodeCount),
                        static_cast<unsigned>(kMaxStateTreeNodes),
                        static_cast<unsigned>(MDocument.Tree.TransitionCount));
    ImGui::SameLine();
    if (ImGui::Button("Compile AI"))
    {
        if (CompileTree())
        {
            (void)SaveCompiledToSceneToml();
        }
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!MCompiledValid);
    if (ImGui::Button("Save AI TOML"))
    {
        (void)SaveCompiledToSceneToml();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("New AI Tree"))
    {
        if (BeginEdit("Reset AI tree", EditSource::CanvasCommand))
        {
            BuildDefaultDocument();
            RecordEdit();
            CommitEdit();
        }
    }
    ImGui::SameLine();
    ImGui::TextDisabled("depth <= %u", static_cast<unsigned>(kMaxStateTreeDepth));
    if (MCompiledDirty)
    {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.30f, 1.0f), "compiled image stale");
    }
    ImGui::TextDisabled("Drag a leaf's 'On trigger' pin to another state's 'In' pin; Delete removes the selection.");
    if (MStatus[0] != '\0')
    {
        ImGui::TextWrapped("%s", MStatus);
    }
    ImGui::Separator();
}

void AIStateTreeCanvasPanel::DrawCanvas()
{
    ImNodes::BeginNodeEditor();
    for (std::uint16_t ScopedNode = 0; ScopedNode < MDocument.Tree.NodeCount; ++ScopedNode)
    {
        const int ScopedId = NodeId(ScopedNode);
        const Vector2D ScopedStored = MDocument.Positions[ScopedNode];
        const ImVec2 ScopedStoredIm{ScopedStored.X, ScopedStored.Y};
        if (!MCanvasDrag || !MEditing)
        {
            ImNodes::SetNodeGridSpacePos(ScopedId, ScopedStoredIm);
        }

        const bool ScopedActive = MPieActiveSet[ScopedNode];
        if (ScopedActive)
        {
            ImNodes::PushColorStyle(ImNodesCol_NodeOutline, kAINeon);
            ImNodes::PushColorStyle(ImNodesCol_TitleBar, kAINeonTitle);
            ImNodes::PushColorStyle(ImNodesCol_NodeBackground, kAINeonBackground);
        }
        ImNodes::BeginNode(ScopedId);
        ImNodes::BeginNodeTitleBar();
        ImGui::TextUnformatted(AIStateTreeKindName(MDocument.Tree.Nodes[ScopedNode].Kind));
        ImGui::SameLine();
        ImGui::TextDisabled("#%u", static_cast<unsigned>(ScopedNode));
        ImNodes::EndNodeTitleBar();

        ImNodes::BeginInputAttribute(InputId(ScopedNode), ImNodesPinShape_CircleFilled);
        ImGui::TextUnformatted("In");
        ImNodes::EndInputAttribute();

        const StateTreeNode& ScopedState = MDocument.Tree.Nodes[ScopedNode];
        ImGui::TextDisabled("depth %u / %u", static_cast<unsigned>(ScopedState.Depth),
                            static_cast<unsigned>(kMaxStateTreeDepth));
        if (ScopedState.Kind == StateTreeNodeKind::Leaf)
        {
            ImGui::TextDisabled("eval: %s", AIStateTreeEvaluatorName(ScopedState.Evaluator.Kind));
            ImGui::TextDisabled("task: %s", AIStateTreeTaskName(ScopedState.Task.Kind));
            ImNodes::BeginOutputAttribute(OutputId(ScopedNode), ImNodesPinShape_TriangleFilled);
            ImGui::TextUnformatted("On trigger");
            ImNodes::EndOutputAttribute();
        }
        else
        {
            ImGui::TextDisabled("children: %u", static_cast<unsigned>(ScopedState.ChildCount));
        }
        if (MCompiledDirty && ScopedActive)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.30f, 1.0f), "stale PIE path");
        }
        ImNodes::EndNode();
        if (ScopedActive)
        {
            ImNodes::PopColorStyle();
            ImNodes::PopColorStyle();
            ImNodes::PopColorStyle();
        }
    }

    for (std::uint16_t ScopedT = 0; ScopedT < MDocument.Tree.TransitionCount; ++ScopedT)
    {
        const StateTreeTransition& ScopedTransition = MDocument.Tree.Transitions[ScopedT];
        if (ScopedTransition.FromLeaf >= MDocument.Tree.NodeCount ||
            ScopedTransition.ToNode >= MDocument.Tree.NodeCount ||
            MDocument.Tree.Nodes[ScopedTransition.FromLeaf].Kind != StateTreeNodeKind::Leaf)
        {
            continue;
        }
        const bool ScopedActiveSource =
            MPieActiveLeaf == ScopedTransition.FromLeaf || MPieActiveSet[ScopedTransition.FromLeaf];
        if (ScopedActiveSource)
        {
            ImNodes::PushColorStyle(ImNodesCol_Link, kAINeon);
        }
        else
        {
            ImNodes::PushColorStyle(ImNodesCol_Link,
                                    ScopedTransition.Enabled ? kAIMutedLink : IM_COL32(90, 70, 70, 160));
        }
        ImNodes::Link(TransitionLinkId(ScopedT), OutputId(ScopedTransition.FromLeaf), InputId(ScopedTransition.ToNode));
        ImNodes::PopColorStyle();
    }

    ImNodes::MiniMap(0.18f, ImNodesMiniMapLocation_BottomRight);
    ImNodes::EndNodeEditor();
    ProcessCanvasEvents();
}

void AIStateTreeCanvasPanel::ProcessCanvasEvents()
{
    // A node drag is one CommandStack transaction even though ImNodes writes a
    bool ScopedMoved = false;
    for (std::uint16_t ScopedNode = 0; ScopedNode < MDocument.Tree.NodeCount; ++ScopedNode)
    {
        const ImVec2 ScopedCurrent = ImNodes::GetNodeGridSpacePos(NodeId(ScopedNode));
        const ImVec2 ScopedStored{MDocument.Positions[ScopedNode].X, MDocument.Positions[ScopedNode].Y};
        if (Near(ScopedCurrent, ScopedStored))
        {
            continue;
        }
        if (!MEditing && !BeginEdit("Move AI state node", EditSource::CanvasDrag))
        {
            break;
        }
        if (MEditSource == EditSource::CanvasDrag)
        {
            (void)SetAINodePosition(ScopedNode, Vector2D{ScopedCurrent.x, ScopedCurrent.y});
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
        SetStatus("AI: node moved");
    }

    int ScopedDestroyed = 0;
    if (ImNodes::IsLinkDestroyed(&ScopedDestroyed))
    {
        std::uint16_t ScopedTransition = 0xFFFFu;
        if (DecodeTransitionLink(ScopedDestroyed, &ScopedTransition) &&
            BeginEdit("Delete AI transition", EditSource::CanvasCommand))
        {
            if (RemoveAITransition(ScopedTransition))
            {
                RecordEdit();
                CommitEdit();
                SetStatus("AI: transition deleted");
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
        std::uint16_t ScopedFromNode = kStateTreeNone;
        std::uint16_t ScopedToNode = kStateTreeNone;
        bool ScopedFromIsOutput = DecodeOutput(ScopedFromAttribute, &ScopedFromNode);
        bool ScopedToIsInput = DecodeInput(ScopedToAttribute, &ScopedToNode);
        if (!ScopedFromIsOutput || !ScopedToIsInput)
        {
            // ImNodes accepts a drag in either direction; normalize input ->
            std::uint16_t ScopedInputNode = kStateTreeNone;
            std::uint16_t ScopedOutputNode = kStateTreeNone;
            if (DecodeInput(ScopedFromAttribute, &ScopedInputNode) &&
                DecodeOutput(ScopedToAttribute, &ScopedOutputNode))
            {
                ScopedFromNode = ScopedOutputNode;
                ScopedToNode = ScopedInputNode;
                ScopedFromIsOutput = true;
                ScopedToIsInput = true;
            }
        }
        if (ScopedFromIsOutput && ScopedToIsInput)
        {
            if (BeginEdit("Create AI transition", EditSource::CanvasCommand))
            {
                if (AddAITransition(ScopedFromNode, ScopedToNode))
                {
                    RecordEdit();
                    CommitEdit();
                    SetStatus("AI: transition created - edit its trigger mask in the inspector");
                }
                else
                {
                    CancelEdit();
                }
            }
        }
        else
        {
            SetStatus("AI: a transition needs a leaf output and a state input");
        }
    }

    const bool ScopedDeletePressed =
        ImGui::IsKeyPressed(ImGuiKey_Delete, false) && !ImGui::GetIO().WantTextInput &&
        (ImNodes::IsEditorHovered() || ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows));
    if (ScopedDeletePressed)
    {
        std::array<int, kMaxStateTreeTransitions> ScopedLinks{};
        std::array<int, kMaxStateTreeNodes> ScopedNodes{};
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
            if (BeginEdit("Delete AI transitions", EditSource::CanvasCommand))
            {
                for (int ScopedI = ScopedLinkCount - 1; ScopedI >= 0; --ScopedI)
                {
                    std::uint16_t ScopedTransition = 0xFFFFu;
                    if (DecodeTransitionLink(ScopedLinks[static_cast<std::size_t>(ScopedI)], &ScopedTransition))
                    {
                        (void)RemoveAITransition(ScopedTransition);
                    }
                }
                RecordEdit();
                CommitEdit();
                ImNodes::ClearLinkSelection();
                SetStatus("AI: selected transitions deleted");
            }
        }
        else if (ScopedNodeCount > 0)
        {
            if (ScopedNodes[0] >= kAINodeIdBase && ScopedNodes[0] < kAIInputIdBase)
            {
                const auto ScopedNode = static_cast<std::uint16_t>(ScopedNodes[0] - kAINodeIdBase);
                if (BeginEdit("Delete AI state subtree", EditSource::CanvasCommand))
                {
                    if (DeleteAINode(ScopedNode))
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
        if (ScopedSelected >= kAINodeIdBase && ScopedSelected < kAIInputIdBase)
        {
            MSelectedNode = static_cast<std::uint16_t>(ScopedSelected - kAINodeIdBase);
            MSelectedTransition = 0xFFFFu;
        }
    }
    else if (ScopedSelectedLinkCount > 0)
    {
        int ScopedSelected = 0;
        ImNodes::GetSelectedLinks(&ScopedSelected);
        std::uint16_t ScopedTransition = 0xFFFFu;
        if (DecodeTransitionLink(ScopedSelected, &ScopedTransition) &&
            ScopedTransition < MDocument.Tree.TransitionCount)
        {
            MSelectedTransition = ScopedTransition;
            MSelectedNode = kStateTreeNone;
        }
    }
    else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImNodes::IsEditorHovered())
    {
        // Preserve a selection made by the inspector or by a just-created node
        int ScopedHovered = 0;
        if (!ImNodes::IsNodeHovered(&ScopedHovered) && !ImNodes::IsLinkHovered(&ScopedHovered))
        {
            MSelectedNode = kStateTreeNone;
            MSelectedTransition = 0xFFFFu;
        }
    }
}

void AIStateTreeCanvasPanel::DrawInspector()
{
    ImGui::TextUnformatted("AI Inspector");
    if (MCompiledDirty)
    {
        ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.30f, 1.0f), "Compile AI to refresh the runtime image.");
    }
    ImGui::Separator();

    if (MSelectedTransition < MDocument.Tree.TransitionCount)
    {
        DrawTransitionInspector(MSelectedTransition);
    }
    else if (MSelectedNode < MDocument.Tree.NodeCount)
    {
        DrawNodeInspector(MSelectedNode);
    }
    else
    {
        ImGui::TextDisabled("Select an AI state or a transition link.");
    }

    if (MPieActive)
    {
        ImGui::Separator();
        DrawPIEInspector();
    }
}

void AIStateTreeCanvasPanel::DrawNodeInspector(std::uint16_t InNode)
{
    if (InNode >= MDocument.Tree.NodeCount)
    {
        return;
    }
    StateTreeNode& ScopedNode = MDocument.Tree.Nodes[InNode];
    ImGui::Text("%s #%u", AIStateTreeKindName(ScopedNode.Kind), static_cast<unsigned>(InNode));
    ImGui::TextDisabled("depth %u / %u | parent %u | children %u", static_cast<unsigned>(ScopedNode.Depth),
                        static_cast<unsigned>(kMaxStateTreeDepth), static_cast<unsigned>(ScopedNode.Parent),
                        static_cast<unsigned>(ScopedNode.ChildCount));

    bool ScopedEnabled = ScopedNode.Enabled;
    if (ImGui::Checkbox("Enabled", &ScopedEnabled))
    {
        if (BeginInspectorEdit("Toggle AI state"))
        {
            ScopedNode.Enabled = ScopedEnabled;
            RecordEdit();
            FinishInspectorEdit(true);
        }
    }

    if (ScopedNode.Kind == StateTreeNodeKind::Leaf)
    {
        StateTreeEvaluatorKind ScopedEvaluatorKinds[] = {
            StateTreeEvaluatorKind::TargetWithinRadius, StateTreeEvaluatorKind::TagsAny,
            StateTreeEvaluatorKind::TagsAll, StateTreeEvaluatorKind::FloatBelow};
        if (ImGui::BeginCombo("Evaluator", AIStateTreeEvaluatorName(ScopedNode.Evaluator.Kind)))
        {
            for (const StateTreeEvaluatorKind ScopedKind : ScopedEvaluatorKinds)
            {
                const bool ScopedSelected = ScopedNode.Evaluator.Kind == ScopedKind;
                if (ImGui::Selectable(AIStateTreeEvaluatorName(ScopedKind), ScopedSelected))
                {
                    const StateTreeEvaluator ScopedBefore = ScopedNode.Evaluator;
                    if (BeginInspectorEdit("Change AI evaluator"))
                    {
                        ScopedNode.Evaluator.Kind = ScopedKind;
                        if (!AIStateTreeEvaluatorFieldsEqual(ScopedBefore, ScopedNode.Evaluator))
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
                if (ScopedSelected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        if (ScopedNode.Evaluator.Kind == StateTreeEvaluatorKind::TargetWithinRadius)
        {
            float ScopedThreshold = ScopedNode.Evaluator.Threshold;
            const bool ScopedChanged = ImGui::DragFloat("Radius", &ScopedThreshold, 1.0f, 0.0f, 100000.0f, "%.1f");
            if (ImGui::IsItemActivated())
            {
                (void)BeginInspectorEdit("Edit AI evaluator radius");
            }
            if (ScopedChanged && MEditing && MEditSource == EditSource::Inspector)
            {
                ScopedNode.Evaluator.Threshold = ScopedThreshold;
                RecordEdit();
            }
            if (ImGui::IsItemDeactivated())
            {
                FinishInspectorEdit(true);
            }
        }
        else if (ScopedNode.Evaluator.Kind == StateTreeEvaluatorKind::FloatBelow)
        {
            int ScopedFloatIndex = static_cast<int>(ScopedNode.Evaluator.FloatIndex);
            const bool ScopedIndexChanged = ImGui::DragInt("Float index", &ScopedFloatIndex, 0.1f, 0, 3);
            if (ImGui::IsItemActivated())
            {
                (void)BeginInspectorEdit("Edit AI evaluator float index");
            }
            if (ScopedIndexChanged && MEditing && MEditSource == EditSource::Inspector)
            {
                ScopedNode.Evaluator.FloatIndex = static_cast<std::uint16_t>(ScopedFloatIndex);
                RecordEdit();
            }
            if (ImGui::IsItemDeactivated())
            {
                FinishInspectorEdit(true);
            }
            float ScopedThreshold = ScopedNode.Evaluator.Threshold;
            const bool ScopedThresholdChanged =
                ImGui::DragFloat("Threshold", &ScopedThreshold, 1.0f, -100000.0f, 100000.0f, "%.3f");
            if (ImGui::IsItemActivated())
            {
                (void)BeginInspectorEdit("Edit AI evaluator threshold");
            }
            if (ScopedThresholdChanged && MEditing && MEditSource == EditSource::Inspector)
            {
                ScopedNode.Evaluator.Threshold = ScopedThreshold;
                RecordEdit();
            }
            if (ImGui::IsItemDeactivated())
            {
                FinishInspectorEdit(true);
            }
        }
        if (ScopedNode.Evaluator.Kind == StateTreeEvaluatorKind::TagsAny ||
            ScopedNode.Evaluator.Kind == StateTreeEvaluatorKind::TagsAll)
        {
            ImU64 ScopedTagMask = static_cast<ImU64>(ScopedNode.Evaluator.TagMask);
            const bool ScopedMaskChanged = ImGui::InputScalar("Tag mask", ImGuiDataType_U64, &ScopedTagMask, nullptr,
                                                              nullptr, "%016llx", ImGuiInputTextFlags_CharsHexadecimal);
            if (ImGui::IsItemActivated())
            {
                (void)BeginInspectorEdit("Edit AI evaluator tag mask");
            }
            if (ScopedMaskChanged && MEditing && MEditSource == EditSource::Inspector)
            {
                ScopedNode.Evaluator.TagMask = static_cast<std::uint64_t>(ScopedTagMask);
                RecordEdit();
            }
            if (ImGui::IsItemDeactivated())
            {
                FinishInspectorEdit(true);
            }
        }

        StateTreeTaskKind ScopedTaskKinds[] = {StateTreeTaskKind::MoveToTarget, StateTreeTaskKind::AttackTarget};
        if (ImGui::BeginCombo("Task", AIStateTreeTaskName(ScopedNode.Task.Kind)))
        {
            for (const StateTreeTaskKind ScopedKind : ScopedTaskKinds)
            {
                const bool ScopedSelected = ScopedNode.Task.Kind == ScopedKind;
                if (ImGui::Selectable(AIStateTreeTaskName(ScopedKind), ScopedSelected))
                {
                    const StateTreeTask ScopedBefore = ScopedNode.Task;
                    if (BeginInspectorEdit("Change AI task"))
                    {
                        ScopedNode.Task.Kind = ScopedKind;
                        if (!AIStateTreeTaskFieldsEqual(ScopedBefore, ScopedNode.Task))
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
                if (ScopedSelected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        if (ScopedNode.Task.Kind == StateTreeTaskKind::MoveToTarget ||
            ScopedNode.Task.Kind == StateTreeTaskKind::AttackTarget)
        {
            float ScopedSpeed = ScopedNode.Task.Speed;
            const bool ScopedSpeedChanged = ImGui::DragFloat("Speed", &ScopedSpeed, 1.0f, 0.0f, 10000.0f, "%.1f");
            if (ImGui::IsItemActivated())
            {
                (void)BeginInspectorEdit("Edit AI task speed");
            }
            if (ScopedSpeedChanged && MEditing && MEditSource == EditSource::Inspector)
            {
                ScopedNode.Task.Speed = ScopedSpeed;
                RecordEdit();
            }
            if (ImGui::IsItemDeactivated())
            {
                FinishInspectorEdit(true);
            }
            float ScopedRange = ScopedNode.Task.Range;
            const bool ScopedRangeChanged = ImGui::DragFloat("Range", &ScopedRange, 1.0f, 0.0f, 100000.0f, "%.1f");
            if (ImGui::IsItemActivated())
            {
                (void)BeginInspectorEdit("Edit AI task range");
            }
            if (ScopedRangeChanged && MEditing && MEditSource == EditSource::Inspector)
            {
                ScopedNode.Task.Range = ScopedRange;
                RecordEdit();
            }
            if (ImGui::IsItemDeactivated())
            {
                FinishInspectorEdit(true);
            }
        }

        const AICommandComponent ScopedCommand = AIStateTreeCommandFor(ScopedNode.Task, InNode);
        ImGui::SeparatorText("AICommandComponent");
        ImGui::TextDisabled("kind %u | speed %.1f | range %.1f | task %u", ScopedCommand.Kind,
                            static_cast<double>(ScopedCommand.Speed), static_cast<double>(ScopedCommand.Range),
                            ScopedCommand.TaskIndex);
    }
    else
    {
        if (ImGui::Button("Add AI Leaf child"))
        {
            const StateTreeNode& ScopedParent = MDocument.Tree.Nodes[InNode];
            const Vector2D ScopedPosition{MDocument.Positions[InNode].X +
                                              static_cast<float>(ScopedParent.ChildCount) * 180.0f,
                                          MDocument.Positions[InNode].Y + 150.0f};
            if (BeginEdit("Add AI leaf", EditSource::CanvasCommand))
            {
                if (AddAINode(StateTreeNodeKind::Leaf, InNode, ScopedPosition) != kStateTreeNone)
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
        if (ImGui::Button("Add AI Selector child"))
        {
            const StateTreeNode& ScopedParent = MDocument.Tree.Nodes[InNode];
            const Vector2D ScopedPosition{MDocument.Positions[InNode].X +
                                              static_cast<float>(ScopedParent.ChildCount) * 180.0f,
                                          MDocument.Positions[InNode].Y + 150.0f};
            if (BeginEdit("Add AI selector", EditSource::CanvasCommand))
            {
                if (AddAINode(StateTreeNodeKind::Selector, InNode, ScopedPosition) != kStateTreeNone)
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
    }

    ImGui::Separator();
    ImGui::BeginDisabled(InNode == MDocument.Tree.Root);
    if (ImGui::Button("Delete AI subtree"))
    {
        if (BeginEdit("Delete AI subtree", EditSource::CanvasCommand))
        {
            if (DeleteAINode(InNode))
            {
                RecordEdit();
                CommitEdit();
                return;
            }
            CancelEdit();
        }
    }
    ImGui::EndDisabled();
}

void AIStateTreeCanvasPanel::DrawTransitionInspector(std::uint16_t InTransition)
{
    if (InTransition >= MDocument.Tree.TransitionCount)
    {
        return;
    }
    StateTreeTransition& ScopedTransition = MDocument.Tree.Transitions[InTransition];
    ImGui::Text("AI Transition #%u", static_cast<unsigned>(InTransition));
    ImGui::TextDisabled("from leaf %u -> state %u", static_cast<unsigned>(ScopedTransition.FromLeaf),
                        static_cast<unsigned>(ScopedTransition.ToNode));

    bool ScopedEnabled = ScopedTransition.Enabled;
    if (ImGui::Checkbox("Transition enabled", &ScopedEnabled))
    {
        if (BeginInspectorEdit("Toggle AI transition"))
        {
            ScopedTransition.Enabled = ScopedEnabled;
            RecordEdit();
            FinishInspectorEdit(true);
        }
    }

    ImU64 ScopedTriggers = static_cast<ImU64>(ScopedTransition.TriggerMask);
    const bool ScopedTriggersChanged = ImGui::InputScalar("Triggers", ImGuiDataType_U64, &ScopedTriggers, nullptr,
                                                          nullptr, "%016llx", ImGuiInputTextFlags_CharsHexadecimal);
    if (ImGui::IsItemActivated())
    {
        (void)BeginInspectorEdit("Edit AI transition triggers");
    }
    if (ScopedTriggersChanged && MEditing && MEditSource == EditSource::Inspector)
    {
        ScopedTransition.TriggerMask = static_cast<std::uint64_t>(ScopedTriggers);
        RecordEdit();
    }
    if (ImGui::IsItemDeactivated())
    {
        FinishInspectorEdit(true);
    }

    ImU64 ScopedConsume = static_cast<ImU64>(ScopedTransition.ConsumeMask);
    const bool ScopedConsumeChanged = ImGui::InputScalar("Consume", ImGuiDataType_U64, &ScopedConsume, nullptr, nullptr,
                                                         "%016llx", ImGuiInputTextFlags_CharsHexadecimal);
    if (ImGui::IsItemActivated())
    {
        (void)BeginInspectorEdit("Edit AI transition consume");
    }
    if (ScopedConsumeChanged && MEditing && MEditSource == EditSource::Inspector)
    {
        ScopedTransition.ConsumeMask = static_cast<std::uint64_t>(ScopedConsume);
        RecordEdit();
    }
    if (ImGui::IsItemDeactivated())
    {
        FinishInspectorEdit(true);
    }

    bool ScopedConsumeAll = ScopedTransition.ConsumeAll;
    if (ImGui::Checkbox("Consume all triggers", &ScopedConsumeAll))
    {
        if (BeginInspectorEdit("Toggle AI transition consume-all"))
        {
            ScopedTransition.ConsumeAll = ScopedConsumeAll;
            RecordEdit();
            FinishInspectorEdit(true);
        }
    }

    ImU64 ScopedRequireTags = static_cast<ImU64>(ScopedTransition.RequireTagsAll);
    const bool ScopedRequireChanged = ImGui::InputScalar("Require tags", ImGuiDataType_U64, &ScopedRequireTags, nullptr,
                                                         nullptr, "%016llx", ImGuiInputTextFlags_CharsHexadecimal);
    if (ImGui::IsItemActivated())
    {
        (void)BeginInspectorEdit("Edit AI transition required tags");
    }
    if (ScopedRequireChanged && MEditing && MEditSource == EditSource::Inspector)
    {
        ScopedTransition.RequireTagsAll = static_cast<std::uint64_t>(ScopedRequireTags);
        RecordEdit();
    }
    if (ImGui::IsItemDeactivated())
    {
        FinishInspectorEdit(true);
    }

    ImU64 ScopedForbidTags = static_cast<ImU64>(ScopedTransition.ForbidTagsAny);
    const bool ScopedForbidChanged = ImGui::InputScalar("Forbid tags", ImGuiDataType_U64, &ScopedForbidTags, nullptr,
                                                        nullptr, "%016llx", ImGuiInputTextFlags_CharsHexadecimal);
    if (ImGui::IsItemActivated())
    {
        (void)BeginInspectorEdit("Edit AI transition forbidden tags");
    }
    if (ScopedForbidChanged && MEditing && MEditSource == EditSource::Inspector)
    {
        ScopedTransition.ForbidTagsAny = static_cast<std::uint64_t>(ScopedForbidTags);
        RecordEdit();
    }
    if (ImGui::IsItemDeactivated())
    {
        FinishInspectorEdit(true);
    }

    if (ImGui::Button("Delete AI transition"))
    {
        if (BeginEdit("Delete AI transition", EditSource::CanvasCommand))
        {
            (void)RemoveAITransition(InTransition);
            RecordEdit();
            CommitEdit();
            ImNodes::ClearLinkSelection();
            return;
        }
    }
}

void AIStateTreeCanvasPanel::DrawPIEInspector()
{
    ImGui::TextColored(ImVec4(0.25f, 1.0f, 0.63f, 1.0f), "PIE AI State");
    const Ecs::Registry* ScopedWorld = MRuntimeWorld;
    if (ScopedWorld == nullptr)
    {
        ImGui::TextDisabled("no runtime world bound");
        return;
    }
    const Ecs::Registry& ScopedSelection = MSelectionWorld != nullptr ? *MSelectionWorld : *ScopedWorld;
    Entity ScopedSelected = entt::null;
    const auto ScopedView = ScopedSelection.view<EditorSelected>();
    const auto ScopedIt = ScopedView.begin();
    if (ScopedIt != ScopedView.end())
    {
        ScopedSelected = *ScopedIt;
    }
    if (ScopedSelected == entt::null)
    {
        ImGui::TextDisabled("select an entity with StateTreeComponent");
        return;
    }
    const StateTreeComponent* ScopedState = ScopedWorld->try_get<StateTreeComponent>(ScopedSelected);
    if (ScopedState == nullptr)
    {
        ImGui::TextDisabled("selected entity has no StateTreeComponent");
        return;
    }

    ImGui::Text("tree %u | depth %u | leaf %u", static_cast<unsigned>(ScopedState->TreeId),
                static_cast<unsigned>(ScopedState->ActiveDepth), static_cast<unsigned>(ScopedState->CurrentLeaf));
    ImGui::Text("active path:");
    for (std::uint32_t ScopedI = 0; ScopedI < kMaxStateTreeDepth; ++ScopedI)
    {
        ImGui::SameLine();
        ImGui::TextDisabled("%u", static_cast<unsigned>(ScopedState->ActiveStates[ScopedI]));
    }

    // Mirror-driven blackboard readout: FieldDesc owns offset/kind/size, so the
    const auto ScopedFields = Reflect::Fields<StateTreeComponent>();
    const auto* ScopedBytes = reinterpret_cast<const std::byte*>(ScopedState);
    ImGui::SeparatorText("StateTreeComponent (Mirror)");
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
            case Reflect::FieldKind::Int128:
            case Reflect::FieldKind::Decimal:
                break;
            case Reflect::FieldKind::Vector2D:
            {
                Vector2D ScopedValue{};
                std::memcpy(&ScopedValue, ScopedBytes + ScopedField.offset, sizeof(ScopedValue));
                ImGui::Text("%.*s: (%.2f, %.2f)", static_cast<int>(ScopedField.name.size()), ScopedField.name.data(),
                            static_cast<double>(ScopedValue.X), static_cast<double>(ScopedValue.Y));
                break;
            }
            case Reflect::FieldKind::Int32:
            case Reflect::FieldKind::UInt32:
            case Reflect::FieldKind::Entity:
            case Reflect::FieldKind::Color32:
            case Reflect::FieldKind::String:
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

    if (const AICommandComponent* ScopedCommand = ScopedWorld->try_get<AICommandComponent>(ScopedSelected);
        ScopedCommand != nullptr)
    {
        ImGui::SeparatorText("AICommandComponent");
        ImGui::Text("kind %u | speed %.1f | range %.1f | sequence %u", ScopedCommand->Kind,
                    static_cast<double>(ScopedCommand->Speed), static_cast<double>(ScopedCommand->Range),
                    ScopedCommand->Sequence);
    }
}

void AIStateTreeCanvasPanel::SetStatus(const char* InFormat, ...) noexcept
{
    // NOLINT(cppcoreguidelines-init-variables)
    va_list ScopedArguments;
    va_start(ScopedArguments, InFormat);
    std::vsnprintf(MStatus, sizeof(MStatus), InFormat, ScopedArguments);
    va_end(ScopedArguments);
}

void AIStateTreeCanvasPanel::SetScenePath(const std::filesystem::path& InPath)
{
    MScenePath = InPath;
}
}
