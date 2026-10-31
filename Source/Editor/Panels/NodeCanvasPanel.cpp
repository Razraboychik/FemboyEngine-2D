// FemboyEngine2D - transactional ImNodes canvas implementation.
#include "Editor/Panels/NodeCanvasPanel.hpp"

#include "imnodes.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>

namespace Fby::Editor
{
namespace
{

constexpr int kNodeIdBase = 1;
constexpr int kInputIdBase = 1'000'000;
constexpr int kOutputIdBase = 2'000'000;
constexpr int kLinkIdBase = 3'000'000;
constexpr int kPinsPerNode = 8;
constexpr float kPositionEpsilon = 0.01f;

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

[[nodiscard]] int LinkId(std::uint16_t InNode, std::uint8_t InPin) noexcept
{
    return kLinkIdBase + static_cast<int>(InNode) * kPinsPerNode + static_cast<int>(InPin);
}

[[nodiscard]] bool DecodeAttribute(int InId, bool* InIsOutput, std::uint16_t* InNode, std::uint8_t* InPin) noexcept
{
    const bool ScopedOutput = InId >= kOutputIdBase && InId < kLinkIdBase;
    const bool ScopedInput = InId >= kInputIdBase && InId < kOutputIdBase;
    if (!ScopedOutput && !ScopedInput)
    {
        return false;
    }
    const int ScopedLocal = InId - (ScopedOutput ? kOutputIdBase : kInputIdBase);
    *InIsOutput = ScopedOutput;
    *InNode = static_cast<std::uint16_t>(ScopedLocal / kPinsPerNode);
    *InPin = static_cast<std::uint8_t>(ScopedLocal % kPinsPerNode);
    return true;
}

[[nodiscard]] bool DecodeLink(int InId, std::uint16_t* InNode, std::uint8_t* InPin) noexcept
{
    if (InId < kLinkIdBase)
    {
        return false;
    }
    const int ScopedLocal = InId - kLinkIdBase;
    *InNode = static_cast<std::uint16_t>(ScopedLocal / kPinsPerNode);
    *InPin = static_cast<std::uint8_t>(ScopedLocal % kPinsPerNode);
    return true;
}

[[nodiscard]] bool Near(ImVec2 InA, ImVec2 InB) noexcept
{
    return (InA.x - InB.x) * (InA.x - InB.x) + (InA.y - InB.y) * (InA.y - InB.y) <= kPositionEpsilon * kPositionEpsilon;
}
}

NodeCanvasPanel::~NodeCanvasPanel()
{
    Shutdown();
}

void NodeCanvasPanel::Initialize(CommandStack& InCommands, std::uint16_t InCodec) noexcept
{
    MCommands = &InCommands;
    MCodec = InCodec;
    MCodecSize = InCommands.CodecSize(InCodec);
    MInitialized = MCodecSize > 0 && MCodecSize <= MBefore.size();
    if (!MInitialized)
    {
        SetStatus("canvas disabled: codec %u is not a registered external buffer", static_cast<unsigned>(InCodec));
    }
}

void NodeCanvasPanel::Shutdown() noexcept
{
    if (MEditing)
    {
        CommitDocumentEdit();
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
    MInitialized = false;
    MModel = {};
    MCodec = 0xFFFFu;
    MCodecSize = 0;
}

void NodeCanvasPanel::SetModel(const CanvasModelOps& InModel) noexcept
{
    MModel = InModel;
}

void NodeCanvasPanel::SetModelFonts(ImFont* InTitleFont, ImFont* InPinFontInput, ImFont* InPinFontOutput,
                                    ImFont* InPanelTitleFont) noexcept
{
    MNodeTitleFont = InTitleFont;
    MInputPinFont = InPinFontInput;
    MOutputPinFont = InPinFontOutput;
    MPanelTitleFont = InPanelTitleFont;
}

bool NodeCanvasPanel::Ready() const noexcept
{
    return MInitialized && MCommands != nullptr && MModel.nodeCount != nullptr;
}

bool NodeCanvasPanel::BeginDocumentEdit(const char* InLabel) noexcept
{
    if (!Ready())
    {
        return false;
    }
    if (MEditing)
    {
        return true;
    }
    if (MCommands->CaptureComponent(entt::null, MCodec, std::span<std::byte>(MBefore.data(), MCodecSize)) !=
        CommandStack::Result::Ok)
    {
        SetStatus("cannot capture graph for undo");
        return false;
    }
    if (MCommands->BeginTransaction(InLabel) != CommandStack::Result::Ok)
    {
        SetStatus("cannot open graph transaction");
        return false;
    }
    MEditing = true;
    MCanvasDrag = false;
    return true;
}

void NodeCanvasPanel::RecordDocumentEdit() noexcept
{
    if (!MEditing)
    {
        return;
    }
    if (MCommands->CaptureComponent(entt::null, MCodec, std::span<std::byte>(MAfter.data(), MCodecSize)) !=
        CommandStack::Result::Ok)
    {
        SetStatus("cannot capture edited graph");
        return;
    }
    const auto ScopedResult =
        MCommands->Record(entt::null, MCodec, std::span<const std::byte>(MBefore.data(), MCodecSize),
                          std::span<const std::byte>(MAfter.data(), MCodecSize));
    if (ScopedResult != CommandStack::Result::Ok)
    {
        SetStatus("graph delta rejected: %s", CommandStack::ResultName(ScopedResult));
    }
}

void NodeCanvasPanel::CommitDocumentEdit() noexcept
{
    if (!MEditing)
    {
        return;
    }
    const auto ScopedResult = MCommands->CommitTransaction();
    MEditing = false;
    MCanvasDrag = false;
    if (ScopedResult != CommandStack::Result::Ok)
    {
        SetStatus("graph commit rejected: %s", CommandStack::ResultName(ScopedResult));
    }
}

void NodeCanvasPanel::Draw(const char* InWindowTitle, bool* InOpen)
{
    if (!Ready() || InWindowTitle == nullptr)
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
    auto* ScopedPreviousContext = ImNodes::GetCurrentContext();
    auto* ScopedContext = static_cast<ImNodesContext*>(MImNodesContext);
    ImNodes::SetCurrentContext(ScopedContext);

    if (MPanelTitleFont != nullptr)
    {
        ImGui::PushFont(MPanelTitleFont);
    }
    const bool ScopedOpen = ImGui::Begin(InWindowTitle, InOpen, ImGuiWindowFlags_NoScrollbar);
    if (MPanelTitleFont != nullptr)
    {
        ImGui::PopFont();
    }
    if (ScopedOpen)
    {
        DrawToolbar();
        ImNodes::BeginNodeEditor();

        const std::uint32_t ScopedNodeCount = MModel.nodeCount(MModel.user);
        const bool ScopedLeftDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedNodeCount; ++ScopedIndex)
        {
            const auto ScopedNode = static_cast<std::uint16_t>(ScopedIndex);
            const int ScopedId = NodeId(ScopedNode);
            if (!ScopedLeftDown || !MEditing)
            {
                ImNodes::SetNodeGridSpacePos(ScopedId, MModel.nodePosition(MModel.user, ScopedNode));
            }

            ImNodes::BeginNode(ScopedId);
            if (MNodeTitleFont != nullptr)
            {
                ImGui::PushFont(MNodeTitleFont);
            }
            ImNodes::BeginNodeTitleBar();
            ImGui::TextUnformatted(MModel.nodeTitle(MModel.user, ScopedNode));
            ImNodes::EndNodeTitleBar();
            if (MNodeTitleFont != nullptr)
            {
                ImGui::PopFont();
            }

            if (MInputPinFont != nullptr)
            {
                ImGui::PushFont(MInputPinFont);
            }
            const std::uint8_t ScopedInputs = MModel.inputCount(MModel.user, ScopedNode);
            for (std::uint8_t ScopedPin = 0; ScopedPin < ScopedInputs; ++ScopedPin)
            {
                ImNodes::BeginInputAttribute(InputId(ScopedNode, ScopedPin), ImNodesPinShape_CircleFilled);
                ImGui::TextUnformatted(MModel.inputName(MModel.user, ScopedNode, ScopedPin));
                ImNodes::EndInputAttribute();
            }
            if (MInputPinFont != nullptr)
            {
                ImGui::PopFont();
            }

            if (MOutputPinFont != nullptr)
            {
                ImGui::PushFont(MOutputPinFont);
            }
            const std::uint8_t ScopedOutputs = MModel.outputCount(MModel.user, ScopedNode);
            for (std::uint8_t ScopedPin = 0; ScopedPin < ScopedOutputs; ++ScopedPin)
            {
                ImNodes::BeginOutputAttribute(OutputId(ScopedNode, ScopedPin), ImNodesPinShape_TriangleFilled);
                ImGui::TextUnformatted(MModel.outputName(MModel.user, ScopedNode, ScopedPin));
                ImNodes::EndOutputAttribute();
            }
            if (MOutputPinFont != nullptr)
            {
                ImGui::PopFont();
            }

            if (MModel.drawNodeBody != nullptr)
            {
                MModel.drawNodeBody(MModel.user, ScopedNode);
            }
            ImNodes::EndNode();
        }

        const std::uint32_t ScopedLinkCount = MModel.linkCount(MModel.user);
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedLinkCount; ++ScopedIndex)
        {
            std::uint16_t ScopedFromNode = 0;
            std::uint8_t ScopedFromPin = 0;
            std::uint16_t ScopedToNode = 0;
            std::uint8_t ScopedToPin = 0;
            MModel.linkEndpoints(MModel.user, ScopedIndex, &ScopedFromNode, &ScopedFromPin, &ScopedToNode,
                                 &ScopedToPin);
            ImNodes::Link(LinkId(ScopedToNode, ScopedToPin), OutputId(ScopedFromNode, ScopedFromPin),
                          InputId(ScopedToNode, ScopedToPin));
        }

        ImNodes::MiniMap(0.16f, ImNodesMiniMapLocation_BottomRight);

        // Debugger highlight: drawn inside the editor context because ImNodes
        // only resolves node screen positions while the editor frame is open.
        if (MHighlightNode < MModel.nodeCount(MModel.user))
        {
            const int ScopedNodeId = NodeId(static_cast<std::uint16_t>(MHighlightNode));
            const ImVec2 ScopedMin = ImNodes::GetNodeScreenSpacePos(ScopedNodeId);
            const ImVec2 ScopedSize = ImNodes::GetNodeDimensions(ScopedNodeId);
            ImDrawList* ScopedDraw = ImGui::GetForegroundDrawList();
            ScopedDraw->AddRect(ImVec2(ScopedMin.x - 2.0f, ScopedMin.y - 2.0f),
                                ImVec2(ScopedMin.x + ScopedSize.x + 2.0f, ScopedMin.y + ScopedSize.y + 2.0f),
                                IM_COL32(255, 220, 120, 255), 4.0f, 0, 2.5f);
        }
        ImNodes::EndNodeEditor();

        ProcessEvents();
    }
    ImGui::End();

    ImNodes::SetCurrentContext(ScopedPreviousContext);
}

void NodeCanvasPanel::DrawToolbar()
{
    const std::uint32_t ScopedNodes = MModel.nodeCount(MModel.user);
    const std::uint32_t ScopedLinks = MModel.linkCount(MModel.user);
    ImGui::TextDisabled("%u nodes / %u links", ScopedNodes, ScopedLinks);
    if (MStatus[0] != '\0')
    {
        ImGui::SameLine();
        ImGui::TextUnformatted(MStatus);
    }
    ImGui::Separator();
    if (MModel.drawPanelTools != nullptr)
    {
        MModel.drawPanelTools(MModel.user);
        ImGui::Separator();
    }

    if (ImGui::BeginPopupContextWindow("##node_canvas_context",
                                       ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
    {
        const std::uint32_t ScopedAddable = MModel.addableCount != nullptr ? MModel.addableCount(MModel.user) : 0;
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedAddable; ++ScopedIndex)
        {
            if (ImGui::MenuItem(MModel.addableName(MModel.user, ScopedIndex)))
            {
                const ImVec2 ScopedPosition{40.0f + static_cast<float>((ScopedNodes % 6) * 230),
                                            40.0f + static_cast<float>((ScopedNodes / 6) * 150)};
                if (BeginDocumentEdit("Add graph node"))
                {
                    std::uint16_t ScopedCreated = 0xFFFFu;
                    std::string ScopedError;
                    if (MModel.addNode(MModel.user, ScopedIndex, ScopedPosition, &ScopedCreated, &ScopedError))
                    {
                        RecordDocumentEdit();
                        CommitDocumentEdit();
                        MSelectedNode = ScopedCreated;
                        SetStatus("added node %u", static_cast<unsigned>(ScopedCreated));
                    }
                    else
                    {
                        CommitDocumentEdit();
                        SetStatus("add node failed: %s", ScopedError.c_str());
                    }
                }
            }
        }
        if (ScopedAddable == 0)
        {
            ImGui::TextDisabled("no node palette");
        }
        ImGui::EndPopup();
    }
}

void NodeCanvasPanel::ProcessEvents()
{
    // Node drags: ImNodes updates its internal grid positions; copy them back
    const std::uint32_t ScopedNodeCount = MModel.nodeCount(MModel.user);
    bool ScopedMoved = false;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedNodeCount; ++ScopedIndex)
    {
        const auto ScopedNode = static_cast<std::uint16_t>(ScopedIndex);
        const ImVec2 ScopedCurrent = ImNodes::GetNodeGridSpacePos(NodeId(ScopedNode));
        const ImVec2 ScopedStored = MModel.nodePosition(MModel.user, ScopedNode);
        if (!Near(ScopedCurrent, ScopedStored))
        {
            if (!MEditing && !BeginDocumentEdit("Move graph node"))
            {
                break;
            }
            MCanvasDrag = true;
            MModel.setNodePosition(MModel.user, ScopedNode, ScopedCurrent);
            RecordDocumentEdit();
            ScopedMoved = true;
        }
    }
    if (MCanvasDrag && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        CommitDocumentEdit();
    }
    if (ScopedMoved)
    {
        SetStatus("node moved");
    }

    // A link dragged off a pin: the target pin is encoded in the link id.
    int ScopedDestroyed = 0;
    if (ImNodes::IsLinkDestroyed(&ScopedDestroyed))
    {
        std::uint16_t ScopedNode = 0;
        std::uint8_t ScopedPin = 0;
        if (DecodeLink(ScopedDestroyed, &ScopedNode, &ScopedPin) && BeginDocumentEdit("Delete graph link"))
        {
            MModel.disconnect(MModel.user, ScopedNode, ScopedPin);
            RecordDocumentEdit();
            CommitDocumentEdit();
            SetStatus("link removed");
        }
    }

    int ScopedFromAttribute = 0;
    int ScopedToAttribute = 0;
    if (ImNodes::IsLinkCreated(&ScopedFromAttribute, &ScopedToAttribute))
    {
        bool ScopedFromIsOutput = false;
        bool ScopedToIsOutput = false;
        std::uint16_t ScopedFromNode = 0;
        std::uint16_t ScopedToNode = 0;
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
                if (BeginDocumentEdit("Create graph link"))
                {
                    std::string ScopedError;
                    if (MModel.connect(MModel.user, ScopedToNode, ScopedToPin, ScopedFromNode, ScopedFromPin,
                                       &ScopedError))
                    {
                        RecordDocumentEdit();
                        CommitDocumentEdit();
                        SetStatus("link created");
                    }
                    else
                    {
                        CommitDocumentEdit();
                        SetStatus("link rejected: %s", ScopedError.c_str());
                    }
                }
            }
            else
            {
                SetStatus("a link needs one output and one input");
            }
        }
    }

    // Delete selected links first, then nodes: deleting a node rebuilds the
    const bool ScopedDeletePressed =
        ImGui::IsKeyPressed(ImGuiKey_Delete, false) && !ImGui::GetIO().WantTextInput &&
        (ImNodes::IsEditorHovered() || ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows));
    if (ScopedDeletePressed)
    {
        std::array<int, kMaxCanvasNodes> ScopedNodeIds{};
        std::array<int, kMaxCanvasLinks> ScopedLinkIds{};
        const int ScopedSelectedNodeCount =
            std::min(ImNodes::NumSelectedNodes(), static_cast<int>(ScopedNodeIds.size()));
        const int ScopedSelectedLinkCount =
            std::min(ImNodes::NumSelectedLinks(), static_cast<int>(ScopedLinkIds.size()));
        if (ScopedSelectedNodeCount > 0)
        {
            ImNodes::GetSelectedNodes(ScopedNodeIds.data());
        }
        if (ScopedSelectedLinkCount > 0)
        {
            ImNodes::GetSelectedLinks(ScopedLinkIds.data());
        }

        std::array<std::uint16_t, kMaxCanvasNodes> ScopedNodesToDelete{};
        std::uint32_t ScopedNodeDeleteCount = 0;
        for (int ScopedI = 0; ScopedI < ScopedSelectedNodeCount; ++ScopedI)
        {
            if (ScopedNodeIds[ScopedI] >= kNodeIdBase && ScopedNodeIds[ScopedI] < kInputIdBase)
            {
                ScopedNodesToDelete[ScopedNodeDeleteCount++] =
                    static_cast<std::uint16_t>(ScopedNodeIds[ScopedI] - kNodeIdBase);
            }
        }

        if (ScopedSelectedLinkCount > 0 || ScopedNodeDeleteCount > 0)
        {
            if (BeginDocumentEdit("Delete graph selection"))
            {
                for (int ScopedI = 0; ScopedI < ScopedSelectedLinkCount; ++ScopedI)
                {
                    std::uint16_t ScopedNode = 0;
                    std::uint8_t ScopedPin = 0;
                    if (DecodeLink(ScopedLinkIds[ScopedI], &ScopedNode, &ScopedPin))
                    {
                        MModel.disconnect(MModel.user, ScopedNode, ScopedPin);
                    }
                }
                if (ScopedNodeDeleteCount > 0)
                {
                    std::string ScopedError;
                    if (!MModel.deleteNodes(
                            MModel.user,
                            std::span<const std::uint16_t>(ScopedNodesToDelete.data(), ScopedNodeDeleteCount),
                            &ScopedError))
                    {
                        SetStatus("delete rejected: %s", ScopedError.c_str());
                    }
                }
                RecordDocumentEdit();
                CommitDocumentEdit();
                ImNodes::ClearNodeSelection();
                ImNodes::ClearLinkSelection();
                MSelectedNode = 0xFFFFu;
                SetStatus("selection deleted");
            }
        }
    }

    MSelectedNode = 0xFFFFu;
    if (ImNodes::NumSelectedNodes() > 0)
    {
        int ScopedSelected = 0;
        ImNodes::GetSelectedNodes(&ScopedSelected);
        if (ScopedSelected >= kNodeIdBase && ScopedSelected < kInputIdBase)
        {
            MSelectedNode = static_cast<std::uint16_t>(ScopedSelected - kNodeIdBase);
        }
    }
}

void NodeCanvasPanel::SetStatus(const char* InFormat, ...) noexcept
{
    // NOLINT(cppcoreguidelines-init-variables)
    va_list ScopedArguments;
    va_start(ScopedArguments, InFormat);
    std::vsnprintf(MStatus, sizeof(MStatus), InFormat, ScopedArguments);
    va_end(ScopedArguments);
}
}
