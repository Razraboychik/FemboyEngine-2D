// FemboyEngine2D - Material Graph panel implementation.
#include "Editor/Panels/MaterialGraphPanel.hpp"

#include <cstdio>
#include <fstream>
#include <vector>

#include "imgui.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string_view>

namespace Fby::Editor
{
namespace
{

constexpr Render::MaterialNodeKind kAddableKinds[] = {
    Render::MaterialNodeKind::Uv,
    Render::MaterialNodeKind::VertexColor,
    Render::MaterialNodeKind::Time,
    Render::MaterialNodeKind::WorldPosition,
    Render::MaterialNodeKind::VertexPosition,
    Render::MaterialNodeKind::Scalar,
    Render::MaterialNodeKind::Vector2D,
    Render::MaterialNodeKind::Vector3D,
    Render::MaterialNodeKind::Color,
    Render::MaterialNodeKind::TextureSample,
    Render::MaterialNodeKind::Add,
    Render::MaterialNodeKind::Sub,
    Render::MaterialNodeKind::Mul,
    Render::MaterialNodeKind::Div,
    Render::MaterialNodeKind::Lerp,
    Render::MaterialNodeKind::Saturate,
    Render::MaterialNodeKind::Clamp,
    Render::MaterialNodeKind::Power,
    Render::MaterialNodeKind::Sin,
    Render::MaterialNodeKind::Cos,
    Render::MaterialNodeKind::OneMinus,
    Render::MaterialNodeKind::Dot,
    Render::MaterialNodeKind::Fresnel,
    Render::MaterialNodeKind::Tilt25D,
    Render::MaterialNodeKind::Distortion,
};

constexpr std::uint32_t kAddableCount = sizeof(kAddableKinds) / sizeof(kAddableKinds[0]);
}

bool MaterialGraphPanel::Initialize(CommandStack& InCommands, Rhi::RHI* InGpu, std::uint32_t InMaterialId)
{
    if (MInitialized)
    {
        return true;
    }
    MGpu = InGpu;
    MMaterialId = InMaterialId;
    BuildDefaultGraph(MGraph);
    std::snprintf(MGraphPath.data(), MGraphPath.size(), "Saved/materials/material-%u.fgraph",
                  static_cast<unsigned>(InMaterialId));

    MCodec = InCommands.RegisterExternalBuffer("MaterialGraph", &MGraph,
                                               static_cast<std::uint32_t>(sizeof(Render::MaterialGraph)));
    MCanvas.Initialize(InCommands, MCodec);

    CanvasModelOps ScopedOps;
    ScopedOps.user = this;
    ScopedOps.nodeCount = &MaterialGraphPanel::ModelNodeCount;
    ScopedOps.nodeTitle = &MaterialGraphPanel::ModelNodeTitle;
    ScopedOps.nodePosition = &MaterialGraphPanel::ModelNodePosition;
    ScopedOps.setNodePosition = &MaterialGraphPanel::ModelSetNodePosition;
    ScopedOps.inputCount = &MaterialGraphPanel::ModelInputCount;
    ScopedOps.inputName = &MaterialGraphPanel::ModelInputName;
    ScopedOps.outputCount = &MaterialGraphPanel::ModelOutputCount;
    ScopedOps.outputName = &MaterialGraphPanel::ModelOutputName;
    ScopedOps.linkCount = &MaterialGraphPanel::ModelLinkCount;
    ScopedOps.linkEndpoints = &MaterialGraphPanel::ModelLinkEndpoints;
    ScopedOps.connect = &MaterialGraphPanel::ModelConnect;
    ScopedOps.disconnect = &MaterialGraphPanel::ModelDisconnect;
    ScopedOps.deleteNodes = &MaterialGraphPanel::ModelDeleteNodes;
    ScopedOps.addableCount = &MaterialGraphPanel::ModelAddableCount;
    ScopedOps.addableName = &MaterialGraphPanel::ModelAddableName;
    ScopedOps.addNode = &MaterialGraphPanel::ModelAddNode;
    ScopedOps.drawNodeBody = &MaterialGraphPanel::ModelDrawNodeBody;
    ScopedOps.drawPanelTools = &MaterialGraphPanel::ModelDrawPanelTools;
    MCanvas.SetModel(ScopedOps);

    if (MGpu != nullptr)
    {
        MRegistered = MRuntime.RegisterMaterial(MMaterialId, &MGraph);
    }
    MInitialized = true;

    // Compile the starter graph once so the panel opens with a valid pipeline.
    (void)ApplyShader();
    return true;
}

void MaterialGraphPanel::Shutdown(Rhi::RHI& InGpu)
{
    if (MGpu != nullptr)
    {
        MRuntime.Shutdown(InGpu);
    }
    MRegistered = false;
    MGpu = nullptr;
    MBoundItem = nullptr;
    MCanvas.Shutdown();
    MInitialized = false;
}

void MaterialGraphPanel::Draw(const char* InWindowTitle, bool* InOpen)
{
    if (!MInitialized || (InOpen != nullptr && !*InOpen))
    {
        return;
    }
    if (MAutoApply && MGraph.Hash() != MLastAppliedHash)
    {
        (void)ApplyShader();
    }
    MCanvas.Draw(InWindowTitle, InOpen);
}

bool MaterialGraphPanel::ApplyShader()
{
    const std::uint64_t ScopedHash = MGraph.Hash();
    MLastAppliedHash = ScopedHash;
    MCompileError[0] = '\0';
    MGlslValid = false;
    MFragmentLength = 0;
    MVertexLength = 0;
    MProgram = {};

    const Render::MaterialGraphRequirements ScopedRequirements = MGraph.Requirements();
    Render::MaterialCompileOptions ScopedOptions;
    ScopedOptions.domain = MRuntime.Domain();
    // The sprite domain always keeps the engine quad vertex stage.
    ScopedOptions.generateVertexStage =
        ScopedOptions.domain == Render::MaterialDomain::Mesh && ScopedRequirements.needsVertexStage;
    ScopedOptions.debugName = "MaterialGraph";

    Render::MaterialGraphCompiler ScopedCompiler;
    std::string ScopedError;
    const std::span<char> ScopedFragmentBuffer(MFragmentGlsl.data(), MFragmentGlsl.size());
    const std::span<char> ScopedVertexBuffer(MVertexGlsl.data(), MVertexGlsl.size());
    if (!ScopedCompiler.Compile(MGraph, ScopedOptions,
                                ScopedOptions.generateVertexStage ? ScopedVertexBuffer : std::span<char>{},
                                ScopedFragmentBuffer, &MProgram, &ScopedError))
    {
        SetError(MCompileError.data(), MCompileError.size(), ScopedError);
        return false;
    }
    MFragmentLength = MProgram.fragmentLength;
    MVertexLength = MProgram.vertexLength;
    MGlslValid = true;

    if (MGpu == nullptr)
    {
        return true;
    }
    if (!MRegistered)
    {
        SetError(MCompileError.data(), MCompileError.size(), "material is not registered with MaterialGraphRuntime");
        return false;
    }
    if (!MRuntime.Update(*MGpu, MGraph))
    {
        const char* ScopedRuntimeError = MRuntime.LastError(MMaterialId);
        SetError(MCompileError.data(), MCompileError.size(),
                 ScopedRuntimeError != nullptr && ScopedRuntimeError[0] != '\0' ? ScopedRuntimeError
                                                                                : "SPIR-V/pipeline creation failed");
        return false;
    }
    // Only a mesh item takes a mesh material pipeline; the sprite variant is
    // consumed through a DrawBatch, not through MeshRenderItem.
    if (MBoundItem != nullptr && MRuntime.Domain() == Render::MaterialDomain::Mesh)
    {
        MBoundItem->materialPipeline = PipelineId();
    }
    return true;
}

void MaterialGraphPanel::SetError(char* InDestination, std::size_t InCapacity, const std::string& InText) noexcept
{
    if (InDestination == nullptr || InCapacity == 0)
    {
        return;
    }
    const std::size_t ScopedLength = std::min(InText.size(), InCapacity - 1);
    if (ScopedLength > 0)
    {
        std::memcpy(InDestination, InText.data(), ScopedLength);
    }
    InDestination[ScopedLength] = '\0';
}

void MaterialGraphPanel::BuildDefaultGraph(Render::MaterialGraph& InGraph)
{
    InGraph.Clear();
    const std::uint16_t ScopedUv = InGraph.AddInputNode(Render::MaterialNodeKind::Uv, Fby::Vector2D{40.0f, 40.0f});
    const std::uint16_t ScopedSample = InGraph.AddTextureSampleNode(0, Fby::Vector2D{300.0f, 40.0f});
    const std::uint16_t ScopedTint = InGraph.AddColorNode(1.0f, 1.0f, 1.0f, 1.0f, Fby::Vector2D{300.0f, 160.0f});
    const std::uint16_t ScopedMultiply =
        InGraph.AddBinaryNode(Render::MaterialNodeKind::Mul, Fby::Vector2D{560.0f, 100.0f});
    InGraph.Node(InGraph.OutputNode()).uiPosition = Fby::Vector2D{820.0f, 100.0f};
    if (ScopedUv != Render::kInvalidMaterialNode && ScopedSample != Render::kInvalidMaterialNode &&
        ScopedTint != Render::kInvalidMaterialNode && ScopedMultiply != Render::kInvalidMaterialNode)
    {
        (void)InGraph.Connect(ScopedSample, 0, ScopedUv);
        (void)InGraph.Connect(ScopedMultiply, 0, ScopedSample);
        (void)InGraph.Connect(ScopedMultiply, 1, ScopedTint);
        (void)InGraph.Connect(InGraph.OutputNode(), 0, ScopedMultiply);
    }
}

Render::MaterialNodeKind MaterialGraphPanel::AddableKind(std::uint32_t InIndex) noexcept
{
    return InIndex < kAddableCount ? kAddableKinds[InIndex] : Render::MaterialNodeKind::Count;
}

bool MaterialGraphPanel::CopyNode(const Render::MaterialGraph& InSource, std::uint16_t InNode,
                                  Render::MaterialGraph& InTarget, std::uint16_t* OutNode) noexcept
{
    if (InNode >= InSource.NodeCount())
    {
        return false;
    }
    const Render::MaterialNode& ScopedRecord = InSource.Node(InNode);
    if (ScopedRecord.kind == Render::MaterialNodeKind::Output)
    {
        *OutNode = InTarget.OutputNode();
        return true;
    }
    const Render::MaterialParam* ScopedParam =
        ScopedRecord.paramIndex != Render::kInvalidMaterialParam && ScopedRecord.paramIndex < InSource.ParamCount()
            ? &InSource.Param(ScopedRecord.paramIndex)
            : nullptr;
    std::uint16_t ScopedCreated = Render::kInvalidMaterialNode;
    switch (ScopedRecord.kind)
    {
        case Render::MaterialNodeKind::Scalar:
            ScopedCreated =
                InTarget.AddScalarNode(ScopedParam != nullptr ? ScopedParam->X : 0.0f, ScopedRecord.uiPosition);
            break;
        case Render::MaterialNodeKind::Vector2D:
            ScopedCreated = InTarget.AddVec2Node(ScopedParam != nullptr ? Fby::Vector2D{ScopedParam->X, ScopedParam->Y}
                                                                        : Fby::Vector2D{},
                                                 ScopedRecord.uiPosition);
            break;
        case Render::MaterialNodeKind::Vector3D:
            ScopedCreated = InTarget.AddVec3Node(
                ScopedParam != nullptr ? ScopedParam->X : 0.0f, ScopedParam != nullptr ? ScopedParam->Y : 0.0f,
                ScopedParam != nullptr ? ScopedParam->Z : 0.0f, ScopedRecord.uiPosition);
            break;
        case Render::MaterialNodeKind::Color:
            ScopedCreated = InTarget.AddColorNode(
                ScopedParam != nullptr ? ScopedParam->X : 1.0f, ScopedParam != nullptr ? ScopedParam->Y : 1.0f,
                ScopedParam != nullptr ? ScopedParam->Z : 1.0f, ScopedParam != nullptr ? ScopedParam->W : 1.0f,
                ScopedRecord.uiPosition);
            break;
        case Render::MaterialNodeKind::TextureSample:
            ScopedCreated = InTarget.AddTextureSampleNode(ScopedParam != nullptr ? ScopedParam->textureLayer : 0,
                                                          ScopedRecord.uiPosition);
            break;
        case Render::MaterialNodeKind::Tilt25D:
            ScopedCreated = InTarget.AddTilt25DNode(
                ScopedParam != nullptr ? Fby::Vector2D{ScopedParam->X, ScopedParam->Y} : Fby::Vector2D{},
                ScopedParam != nullptr ? ScopedParam->Z : 1.0f, ScopedRecord.uiPosition);
            break;
        case Render::MaterialNodeKind::Distortion:
            ScopedCreated =
                InTarget.AddDistortionNode(ScopedParam != nullptr ? ScopedParam->X : 0.01f,
                                           ScopedParam != nullptr ? ScopedParam->Y : 4.0f, ScopedRecord.uiPosition);
            break;
        case Render::MaterialNodeKind::Output:
        case Render::MaterialNodeKind::Count:
            return false;
        default:
            ScopedCreated = InTarget.AddNode(ScopedRecord.kind, ScopedRecord.uiPosition);
            break;
    }
    if (ScopedCreated == Render::kInvalidMaterialNode)
    {
        return false;
    }
    *OutNode = ScopedCreated;
    return true;
}

bool MaterialGraphPanel::RebuildWithoutNodes(Render::MaterialGraph& InGraph, std::span<const std::uint16_t> InNodes,
                                             std::string* InError)
{
    if (InNodes.empty())
    {
        return true;
    }
    std::array<bool, Render::kMaxMaterialNodes> ScopedRemoved{};
    for (const std::uint16_t ScopedNode : InNodes)
    {
        if (ScopedNode < InGraph.NodeCount() && ScopedNode != InGraph.OutputNode())
        {
            ScopedRemoved[ScopedNode] = true;
        }
    }

    Render::MaterialGraph ScopedRebuilt;
    ScopedRebuilt.Node(ScopedRebuilt.OutputNode()).uiPosition = InGraph.Node(InGraph.OutputNode()).uiPosition;
    std::array<std::uint16_t, Render::kMaxMaterialNodes> ScopedMap{};
    ScopedMap.fill(Render::kInvalidMaterialNode);
    ScopedMap[InGraph.OutputNode()] = ScopedRebuilt.OutputNode();

    for (std::uint32_t ScopedIndex = 0; ScopedIndex < InGraph.NodeCount(); ++ScopedIndex)
    {
        const auto ScopedNode = static_cast<std::uint16_t>(ScopedIndex);
        if (ScopedNode == InGraph.OutputNode() || ScopedRemoved[ScopedNode])
        {
            continue;
        }
        std::uint16_t ScopedCopy = Render::kInvalidMaterialNode;
        if (!CopyNode(InGraph, ScopedNode, ScopedRebuilt, &ScopedCopy))
        {
            if (InError != nullptr)
            {
                *InError = "cannot copy a material node while deleting";
            }
            return false;
        }
        ScopedMap[ScopedNode] = ScopedCopy;
    }

    // Reconnect in source order. Output is index 0 and is always kept.
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < InGraph.NodeCount(); ++ScopedIndex)
    {
        const auto ScopedNode = static_cast<std::uint16_t>(ScopedIndex);
        if (ScopedRemoved[ScopedNode] || ScopedMap[ScopedNode] == Render::kInvalidMaterialNode)
        {
            continue;
        }
        const Render::MaterialNode& ScopedRecord = InGraph.Node(ScopedNode);
        for (std::uint8_t ScopedSlot = 0; ScopedSlot < ScopedRecord.inputCount; ++ScopedSlot)
        {
            const Render::MaterialPin& ScopedPin =
                InGraph.Pin(static_cast<std::uint16_t>(ScopedRecord.firstInput + ScopedSlot));
            if (ScopedPin.sourceNode == Render::kInvalidMaterialNode || ScopedPin.sourceNode >= InGraph.NodeCount() ||
                ScopedMap[ScopedPin.sourceNode] == Render::kInvalidMaterialNode)
            {
                continue;
            }
            if (!ScopedRebuilt.Connect(ScopedMap[ScopedNode], ScopedSlot, ScopedMap[ScopedPin.sourceNode], InError))
            {
                return false;
            }
        }
    }
    InGraph = ScopedRebuilt;
    return true;
}

// Canvas model callbacks
std::uint32_t MaterialGraphPanel::ModelNodeCount(void* InUser) noexcept
{
    return static_cast<MaterialGraphPanel*>(InUser)->MGraph.NodeCount();
}

const char* MaterialGraphPanel::ModelNodeTitle(void* InUser, std::uint16_t InNode) noexcept
{
    const Render::MaterialGraph& ScopedGraph = static_cast<MaterialGraphPanel*>(InUser)->MGraph;
    return InNode < ScopedGraph.NodeCount() ? Render::MaterialNodeKindName(ScopedGraph.Node(InNode).kind) : "?";
}

ImVec2 MaterialGraphPanel::ModelNodePosition(void* InUser, std::uint16_t InNode) noexcept
{
    const Render::MaterialGraph& ScopedGraph = static_cast<MaterialGraphPanel*>(InUser)->MGraph;
    const Fby::Vector2D ScopedPosition =
        InNode < ScopedGraph.NodeCount() ? ScopedGraph.Node(InNode).uiPosition : Fby::Vector2D{};
    return ImVec2{ScopedPosition.X, ScopedPosition.Y};
}

void MaterialGraphPanel::ModelSetNodePosition(void* InUser, std::uint16_t InNode, ImVec2 InPosition) noexcept
{
    Render::MaterialGraph& ScopedGraph = static_cast<MaterialGraphPanel*>(InUser)->MGraph;
    if (InNode < ScopedGraph.NodeCount())
    {
        ScopedGraph.Node(InNode).uiPosition = Fby::Vector2D{InPosition.x, InPosition.y};
    }
}

std::uint8_t MaterialGraphPanel::ModelInputCount(void* InUser, std::uint16_t InNode) noexcept
{
    const Render::MaterialGraph& ScopedGraph = static_cast<MaterialGraphPanel*>(InUser)->MGraph;
    return InNode < ScopedGraph.NodeCount() ? ScopedGraph.Node(InNode).inputCount : 0;
}

const char* MaterialGraphPanel::ModelInputName(void* InUser, std::uint16_t InNode, std::uint8_t InPin) noexcept
{
    const Render::MaterialGraph& ScopedGraph = static_cast<MaterialGraphPanel*>(InUser)->MGraph;
    return InNode < ScopedGraph.NodeCount() ? Render::MaterialNodeInputName(ScopedGraph.Node(InNode).kind, InPin) : "?";
}

std::uint8_t MaterialGraphPanel::ModelOutputCount(void* InUser, std::uint16_t InNode) noexcept
{
    const Render::MaterialGraph& ScopedGraph = static_cast<MaterialGraphPanel*>(InUser)->MGraph;
    if (InNode >= ScopedGraph.NodeCount() || ScopedGraph.Node(InNode).kind == Render::MaterialNodeKind::Output)
    {
        return 0;
    }
    return 1;
}

const char* MaterialGraphPanel::ModelOutputName(void*, std::uint16_t, std::uint8_t) noexcept
{
    return "Out";
}

std::uint32_t MaterialGraphPanel::ModelLinkCount(void* InUser) noexcept
{
    const Render::MaterialGraph& ScopedGraph = static_cast<MaterialGraphPanel*>(InUser)->MGraph;
    std::uint32_t ScopedCount = 0;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedGraph.NodeCount(); ++ScopedIndex)
    {
        const Render::MaterialNode& ScopedNode = ScopedGraph.Node(static_cast<std::uint16_t>(ScopedIndex));
        for (std::uint8_t ScopedSlot = 0; ScopedSlot < ScopedNode.inputCount; ++ScopedSlot)
        {
            if (ScopedGraph.Pin(static_cast<std::uint16_t>(ScopedNode.firstInput + ScopedSlot)).sourceNode !=
                Render::kInvalidMaterialNode)
            {
                ++ScopedCount;
            }
        }
    }
    return ScopedCount;
}

void MaterialGraphPanel::ModelLinkEndpoints(void* InUser, std::uint32_t InLink, std::uint16_t* InFromNode,
                                            std::uint8_t* InFromPin, std::uint16_t* InToNode,
                                            std::uint8_t* InToPin) noexcept
{
    const Render::MaterialGraph& ScopedGraph = static_cast<MaterialGraphPanel*>(InUser)->MGraph;
    std::uint32_t ScopedOrdinal = 0;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedGraph.NodeCount(); ++ScopedIndex)
    {
        const Render::MaterialNode& ScopedNode = ScopedGraph.Node(static_cast<std::uint16_t>(ScopedIndex));
        for (std::uint8_t ScopedSlot = 0; ScopedSlot < ScopedNode.inputCount; ++ScopedSlot)
        {
            const Render::MaterialPin& ScopedPin =
                ScopedGraph.Pin(static_cast<std::uint16_t>(ScopedNode.firstInput + ScopedSlot));
            if (ScopedPin.sourceNode == Render::kInvalidMaterialNode)
            {
                continue;
            }
            if (ScopedOrdinal == InLink)
            {
                *InFromNode = ScopedPin.sourceNode;
                *InFromPin = ScopedPin.sourceSlot;
                *InToNode = static_cast<std::uint16_t>(ScopedIndex);
                *InToPin = ScopedSlot;
                return;
            }
            ++ScopedOrdinal;
        }
    }
}

bool MaterialGraphPanel::ModelConnect(void* InUser, std::uint16_t InToNode, std::uint8_t InToPin,
                                      std::uint16_t InFromNode, std::uint8_t InFromPin, std::string* InError)
{
    Render::MaterialGraph& ScopedGraph = static_cast<MaterialGraphPanel*>(InUser)->MGraph;
    if (InFromPin != 0)
    {
        if (InError != nullptr)
        {
            *InError = "material nodes have a single output pin";
        }
        return false;
    }
    return ScopedGraph.Connect(InToNode, InToPin, InFromNode, InError);
}

void MaterialGraphPanel::ModelDisconnect(void* InUser, std::uint16_t InToNode, std::uint8_t InToPin) noexcept
{
    static_cast<MaterialGraphPanel*>(InUser)->MGraph.Disconnect(InToNode, InToPin);
}

bool MaterialGraphPanel::ModelDeleteNodes(void* InUser, std::span<const std::uint16_t> InNodes, std::string* InError)
{
    return RebuildWithoutNodes(static_cast<MaterialGraphPanel*>(InUser)->MGraph, InNodes, InError);
}

std::uint32_t MaterialGraphPanel::ModelAddableCount(void*) noexcept
{
    return kAddableCount;
}

const char* MaterialGraphPanel::ModelAddableName(void*, std::uint32_t InAddable) noexcept
{
    return Render::MaterialNodeKindName(AddableKind(InAddable));
}

bool MaterialGraphPanel::ModelAddNode(void* InUser, std::uint32_t InAddable, ImVec2 InPosition, std::uint16_t* OutNode,
                                      std::string* InError)
{
    auto* ScopedSelf = static_cast<MaterialGraphPanel*>(InUser);
    const Render::MaterialNodeKind ScopedKind = AddableKind(InAddable);
    if (ScopedKind == Render::MaterialNodeKind::Count)
    {
        if (InError != nullptr)
        {
            *InError = "unknown material node kind";
        }
        return false;
    }
    const Fby::Vector2D ScopedUi{InPosition.x, InPosition.y};
    std::uint16_t ScopedCreated = Render::kInvalidMaterialNode;
    switch (ScopedKind)
    {
        case Render::MaterialNodeKind::Scalar:
            ScopedCreated = ScopedSelf->MGraph.AddScalarNode(0.5f, ScopedUi);
            break;
        case Render::MaterialNodeKind::Vector2D:
            ScopedCreated = ScopedSelf->MGraph.AddVec2Node(Fby::Vector2D{0.5f, 0.5f}, ScopedUi);
            break;
        case Render::MaterialNodeKind::Vector3D:
            ScopedCreated = ScopedSelf->MGraph.AddVec3Node(0.5f, 0.5f, 0.5f, ScopedUi);
            break;
        case Render::MaterialNodeKind::Color:
            ScopedCreated = ScopedSelf->MGraph.AddColorNode(1.0f, 1.0f, 1.0f, 1.0f, ScopedUi);
            break;
        case Render::MaterialNodeKind::TextureSample:
            ScopedCreated = ScopedSelf->MGraph.AddTextureSampleNode(0, ScopedUi);
            break;
        case Render::MaterialNodeKind::Tilt25D:
            ScopedCreated = ScopedSelf->MGraph.AddTilt25DNode(Fby::Vector2D{0.05f, 0.05f}, 1.0f, ScopedUi);
            break;
        case Render::MaterialNodeKind::Distortion:
            ScopedCreated = ScopedSelf->MGraph.AddDistortionNode(0.01f, 4.0f, ScopedUi);
            break;
        default:
            ScopedCreated = ScopedSelf->MGraph.AddNode(ScopedKind, ScopedUi);
            break;
    }
    if (ScopedCreated == Render::kInvalidMaterialNode)
    {
        if (InError != nullptr)
        {
            *InError = "material graph is full or the node kind needs a payload";
        }
        return false;
    }
    *OutNode = ScopedCreated;
    return true;
}

void MaterialGraphPanel::ModelDrawNodeBody(void* InUser, std::uint16_t InNode) noexcept
{
    const Render::MaterialGraph& ScopedGraph = static_cast<MaterialGraphPanel*>(InUser)->MGraph;
    if (InNode >= ScopedGraph.NodeCount())
    {
        return;
    }
    const Render::MaterialNode& ScopedRecord = ScopedGraph.Node(InNode);
    if (!Render::MaterialNodeUsesParam(ScopedRecord.kind) || ScopedRecord.paramIndex >= ScopedGraph.ParamCount())
    {
        return;
    }
    const Render::MaterialParam& ScopedParam = ScopedGraph.Param(ScopedRecord.paramIndex);
    switch (ScopedRecord.kind)
    {
        case Render::MaterialNodeKind::Scalar:
            ImGui::TextDisabled("%.3f", static_cast<double>(ScopedParam.X));
            break;
        case Render::MaterialNodeKind::Vector2D:
            ImGui::TextDisabled("(%.2f, %.2f)", static_cast<double>(ScopedParam.X), static_cast<double>(ScopedParam.Y));
            break;
        case Render::MaterialNodeKind::Vector3D:
            ImGui::TextDisabled("(%.2f, %.2f, %.2f)", static_cast<double>(ScopedParam.X),
                                static_cast<double>(ScopedParam.Y), static_cast<double>(ScopedParam.Z));
            break;
        case Render::MaterialNodeKind::Color:
            ImGui::TextDisabled("(%.2f, %.2f, %.2f, %.2f)", static_cast<double>(ScopedParam.X),
                                static_cast<double>(ScopedParam.Y), static_cast<double>(ScopedParam.Z),
                                static_cast<double>(ScopedParam.W));
            break;
        case Render::MaterialNodeKind::TextureSample:
            ImGui::TextDisabled("layer %u", ScopedParam.textureLayer);
            break;
        case Render::MaterialNodeKind::Tilt25D:
            ImGui::TextDisabled("slope (%.2f, %.2f) h %.2f", static_cast<double>(ScopedParam.X),
                                static_cast<double>(ScopedParam.Y), static_cast<double>(ScopedParam.Z));
            break;
        case Render::MaterialNodeKind::Distortion:
            ImGui::TextDisabled("strength %.3f / freq %.2f", static_cast<double>(ScopedParam.X),
                                static_cast<double>(ScopedParam.Y));
            break;
        default:
            break;
    }
}

void MaterialGraphPanel::ModelDrawPanelTools(void* InUser) noexcept
{
    static_cast<MaterialGraphPanel*>(InUser)->DrawNodeInspector();
    static_cast<MaterialGraphPanel*>(InUser)->DrawShaderPreview();
}

bool MaterialGraphPanel::SaveGraph(const char* InPath)
{
    if (InPath == nullptr || InPath[0] == ' ')
    {
        return false;
    }
    std::vector<std::byte> ScopedBytes;
    if (!Render::EncodeMaterialGraph(MGraph, &ScopedBytes))
    {
        return false;
    }
    std::ofstream ScopedStream(InPath, std::ios::binary);
    if (!ScopedStream)
    {
        return false;
    }
    ScopedStream.write(reinterpret_cast<const char*>(ScopedBytes.data()),
                       static_cast<std::streamsize>(ScopedBytes.size()));
    return static_cast<bool>(ScopedStream);
}

bool MaterialGraphPanel::LoadGraph(const char* InPath)
{
    if (InPath == nullptr || InPath[0] == ' ')
    {
        return false;
    }
    std::ifstream ScopedStream(InPath, std::ios::binary | std::ios::ate);
    if (!ScopedStream)
    {
        return false;
    }
    const std::streamsize ScopedSize = ScopedStream.tellg();
    if (ScopedSize <= 0)
    {
        return false;
    }
    std::vector<std::byte> ScopedBytes(static_cast<std::size_t>(ScopedSize));
    ScopedStream.seekg(0, std::ios::beg);
    ScopedStream.read(reinterpret_cast<char*>(ScopedBytes.data()), ScopedSize);
    if (!ScopedStream)
    {
        return false;
    }
    Render::MaterialAssetError ScopedError = Render::MaterialAssetError::None;
    if (!Render::DecodeMaterialGraph(ScopedBytes, &MGraph, &ScopedError))
    {
        SetError(MCompileError.data(), MCompileError.size(),
                 std::string("cannot load the .fgraph file: ") + Render::MaterialAssetErrorName(ScopedError));
        return false;
    }
    // The graph object stays the CommandStack external buffer, so the load is a
    // content replacement and the undo history continues to work.
    MLastAppliedHash = 0;
    return true;
}

void MaterialGraphPanel::DrawNodeInspector()
{
    if (ImGui::Button("Apply Shader"))
    {
        (void)ApplyShader();
    }
    ImGui::SameLine();
    ImGui::Checkbox("Auto apply", &MAutoApply);
    ImGui::SameLine();
    ImGui::TextDisabled("pipeline %u", PipelineId());

    // The domain picks the shader contract: mesh replaces mesh.frag, sprite
    // replaces the sampling block of quad.frag and keeps the quad vertex stage.
    int ScopedDomain = MRuntime.Domain() == Render::MaterialDomain::Sprite ? 1 : 0;
    ImGui::SetNextItemWidth(110.0f);
    if (ImGui::Combo("Domain", &ScopedDomain, "Mesh Sprite "))
    {
        SetDomain(ScopedDomain == 1 ? Render::MaterialDomain::Sprite : Render::MaterialDomain::Mesh);
        (void)ApplyShader();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("cook the same file with --sprite for the sprite variant");

    // The .fgraph file is the cooker input, so the editor writes the format the
    // build step reads instead of keeping the graph in memory only.
    ImGui::SetNextItemWidth(260.0f);
    ImGui::InputText("##graphpath", MGraphPath.data(), MGraphPath.size());
    ImGui::SameLine();
    if (ImGui::Button("Save .fgraph"))
    {
        if (!SaveGraph(MGraphPath.data()))
        {
            SetError(MCompileError.data(), MCompileError.size(), std::string("cannot write the .fgraph file"));
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Load .fgraph"))
    {
        if (LoadGraph(MGraphPath.data()))
        {
            (void)ApplyShader();
        }
    }

    if (MCompileError[0] != '\0')
    {
        ImGui::TextColored(ImVec4{0.95f, 0.35f, 0.45f, 1.0f}, "%s", MCompileError.data());
    }
    else if (MGlslValid)
    {
        ImGui::SameLine();
        ImGui::TextDisabled("GLSL %u B / vertex %u B", MFragmentLength, MVertexLength);
    }

    const std::uint16_t ScopedSelected = MCanvas.SelectedNode();
    if (!ImGui::CollapsingHeader("Node", ImGuiTreeNodeFlags_DefaultOpen))
    {
        return;
    }
    if (ScopedSelected >= MGraph.NodeCount())
    {
        ImGui::TextDisabled("select a node");
        return;
    }
    Render::MaterialNode& ScopedNode = MGraph.Node(ScopedSelected);
    ImGui::Text("%u: %s", static_cast<unsigned>(ScopedSelected), Render::MaterialNodeKindName(ScopedNode.kind));
    if (!Render::MaterialNodeUsesParam(ScopedNode.kind) || ScopedNode.paramIndex >= MGraph.ParamCount())
    {
        ImGui::TextDisabled("no editable constant");
        return;
    }

    // A widget edit is one transaction. BeginDocumentEdit snapshots the graph
    const auto ScopedFinishWidget = [this](bool InChanged, auto&& InApply)
    {
        if (ImGui::IsItemActivated())
        {
            (void)MCanvas.BeginDocumentEdit("Edit material constant");
        }
        if (InChanged)
        {
            InApply();
            MCanvas.RecordDocumentEdit();
        }
        if (ImGui::IsItemDeactivated())
        {
            MCanvas.CommitDocumentEdit();
        }
    };

    Render::MaterialParam& ScopedParam = MGraph.Param(ScopedNode.paramIndex);
    switch (ScopedNode.kind)
    {
        case Render::MaterialNodeKind::Scalar:
        {
            float ScopedValue = ScopedParam.X;
            const bool ScopedChanged = ImGui::DragFloat("##scalar", &ScopedValue, 0.01f);
            ScopedFinishWidget(ScopedChanged, [&] { ScopedParam.X = ScopedValue; });
            break;
        }
        case Render::MaterialNodeKind::Vector2D:
        {
            float ScopedValues[2] = {ScopedParam.X, ScopedParam.Y};
            const bool ScopedChanged = ImGui::DragFloat2("##vec2", ScopedValues, 0.01f);
            ScopedFinishWidget(ScopedChanged,
                               [&]
                               {
                                   ScopedParam.X = ScopedValues[0];
                                   ScopedParam.Y = ScopedValues[1];
                               });
            break;
        }
        case Render::MaterialNodeKind::Vector3D:
        {
            float ScopedValues[3] = {ScopedParam.X, ScopedParam.Y, ScopedParam.Z};
            const bool ScopedChanged = ImGui::DragFloat3("##vec3", ScopedValues, 0.01f);
            ScopedFinishWidget(ScopedChanged,
                               [&]
                               {
                                   ScopedParam.X = ScopedValues[0];
                                   ScopedParam.Y = ScopedValues[1];
                                   ScopedParam.Z = ScopedValues[2];
                               });
            break;
        }
        case Render::MaterialNodeKind::Color:
        {
            float ScopedValues[4] = {ScopedParam.X, ScopedParam.Y, ScopedParam.Z, ScopedParam.W};
            const bool ScopedChanged = ImGui::ColorEdit4("##color", ScopedValues);
            ScopedFinishWidget(ScopedChanged,
                               [&]
                               {
                                   ScopedParam.X = ScopedValues[0];
                                   ScopedParam.Y = ScopedValues[1];
                                   ScopedParam.Z = ScopedValues[2];
                                   ScopedParam.W = ScopedValues[3];
                               });
            break;
        }
        case Render::MaterialNodeKind::TextureSample:
        {
            int ScopedLayer = static_cast<int>(ScopedParam.textureLayer);
            const bool ScopedChanged = ImGui::DragInt("##layer", &ScopedLayer, 0.05f, 0, 255);
            ScopedFinishWidget(ScopedChanged,
                               [&] { ScopedParam.textureLayer = static_cast<std::uint32_t>(ScopedLayer); });
            break;
        }
        case Render::MaterialNodeKind::Tilt25D:
        {
            float ScopedSlope[2] = {ScopedParam.X, ScopedParam.Y};
            float ScopedHeight = ScopedParam.Z;
            const bool ScopedSlopeChanged = ImGui::DragFloat2("##slope", ScopedSlope, 0.005f);
            ScopedFinishWidget(ScopedSlopeChanged,
                               [&]
                               {
                                   ScopedParam.X = ScopedSlope[0];
                                   ScopedParam.Y = ScopedSlope[1];
                               });
            const bool ScopedHeightChanged = ImGui::DragFloat("##height", &ScopedHeight, 0.01f);
            ScopedFinishWidget(ScopedHeightChanged, [&] { ScopedParam.Z = ScopedHeight; });
            break;
        }
        case Render::MaterialNodeKind::Distortion:
        {
            float ScopedStrength = ScopedParam.X;
            const bool ScopedStrengthChanged = ImGui::DragFloat("##strength", &ScopedStrength, 0.001f, 0.0f, 1.0f);
            ScopedFinishWidget(ScopedStrengthChanged, [&] { ScopedParam.X = ScopedStrength; });
            float ScopedFrequency = ScopedParam.Y;
            const bool ScopedFrequencyChanged = ImGui::DragFloat("##frequency", &ScopedFrequency, 0.05f, 0.0f, 64.0f);
            ScopedFinishWidget(ScopedFrequencyChanged, [&] { ScopedParam.Y = ScopedFrequency; });
            break;
        }
        default:
            ImGui::TextDisabled("no editor for this constant");
            break;
    }
}

void MaterialGraphPanel::DrawShaderPreview()
{
    if (!ImGui::CollapsingHeader("Generated GLSL"))
    {
        return;
    }
    if (!MGlslValid || MFragmentLength == 0)
    {
        ImGui::TextDisabled("apply the shader to generate GLSL");
        return;
    }
    ImGui::InputTextMultiline("##fragment_glsl", MFragmentGlsl.data(), static_cast<std::size_t>(MFragmentLength) + 1u,
                              ImVec2(0.0f, 240.0f), ImGuiInputTextFlags_ReadOnly);
    if (MVertexLength > 0)
    {
        ImGui::Separator();
        ImGui::TextDisabled("vertex stage");
        ImGui::InputTextMultiline("##vertex_glsl", MVertexGlsl.data(), static_cast<std::size_t>(MVertexLength) + 1u,
                                  ImVec2(0.0f, 180.0f), ImGuiInputTextFlags_ReadOnly);
    }
}
}
