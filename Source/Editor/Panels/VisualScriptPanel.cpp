// FemboyEngine2D - VisualScript panel implementation.
#include "Editor/Panels/VisualScriptPanel.hpp"

#include "Editor/Infrastructure/EditorFonts.hpp"

#include "Engine/ECS/Components.hpp"
#include "Engine/Script/AotHost.hpp"
#include "Engine/Script/Bytecode.hpp"

#include "imgui.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string_view>

namespace Fby::Editor
{
namespace
{

constexpr Script::LogicNodeKind kAddableKinds[] = {
    Script::LogicNodeKind::GetField,     Script::LogicNodeKind::SetField,     Script::LogicNodeKind::ConstFloat,
    Script::LogicNodeKind::ConstVec2,    Script::LogicNodeKind::SelfPosition, Script::LogicNodeKind::SelfVelocity,
    Script::LogicNodeKind::SelfRotation, Script::LogicNodeKind::DeltaTime,    Script::LogicNodeKind::Random01,
    Script::LogicNodeKind::Add,          Script::LogicNodeKind::Sub,          Script::LogicNodeKind::Mul,
    Script::LogicNodeKind::Div,          Script::LogicNodeKind::Min,          Script::LogicNodeKind::Max,
    Script::LogicNodeKind::Vec2Scale,    Script::LogicNodeKind::LogValue,     Script::LogicNodeKind::SetPosition,
    Script::LogicNodeKind::SetVelocity,  Script::LogicNodeKind::DespawnSelf,  Script::LogicNodeKind::Branch,
    Script::LogicNodeKind::Condition,    Script::LogicNodeKind::Math,         Script::LogicNodeKind::SpawnEntity,
    Script::LogicNodeKind::BigIntFromInt, Script::LogicNodeKind::BigIntAdd,   Script::LogicNodeKind::BigIntSub,
    Script::LogicNodeKind::BigIntMul,     Script::LogicNodeKind::BigIntDiv,   Script::LogicNodeKind::BigIntMod,
    Script::LogicNodeKind::BigIntCmp,     Script::LogicNodeKind::BigIntToInt, Script::LogicNodeKind::BigIntLog,
    Script::LogicNodeKind::DecimalFromInt, Script::LogicNodeKind::DecimalAdd, Script::LogicNodeKind::DecimalSub,
    Script::LogicNodeKind::DecimalMul,    Script::LogicNodeKind::DecimalDiv,
    Script::LogicNodeKind::DecimalDivRounded, Script::LogicNodeKind::DecimalCmp,
    Script::LogicNodeKind::DecimalToInt, Script::LogicNodeKind::DecimalLog,
};

constexpr std::uint32_t kAddableCount = sizeof(kAddableKinds) / sizeof(kAddableKinds[0]);

[[nodiscard]] const char* FieldName(const Script::LogicGraph& InGraph, std::uint16_t InFieldIndex) noexcept
{
    return InFieldIndex < InGraph.fieldCount ? InGraph.fields[InFieldIndex].fieldName : "?";
}

[[nodiscard]] const char* ComponentName(const Script::LogicGraph& InGraph, std::uint16_t InFieldIndex) noexcept
{
    return InFieldIndex < InGraph.fieldCount ? InGraph.fields[InFieldIndex].componentName : "?";
}

[[nodiscard]] const char* InputLabel(const Script::LogicGraph& InGraph, std::uint16_t InNode,
                                     std::uint8_t InPin) noexcept
{
    if (InNode >= InGraph.nodeCount || InPin >= InGraph.nodes[InNode].inputCount)
    {
        return "?";
    }
    switch (InGraph.nodes[InNode].kind)
    {
        case Script::LogicNodeKind::GetField:
            return "";
        case Script::LogicNodeKind::SetField:
            return InPin == 0 ? "Exec" : FieldName(InGraph, static_cast<std::uint16_t>(InGraph.nodes[InNode].param));
        case Script::LogicNodeKind::Condition:
        case Script::LogicNodeKind::Math:
            return InPin == 0 ? "A" : "B";
        case Script::LogicNodeKind::SpawnEntity:
            return "Exec";
        case Script::LogicNodeKind::Vec2Scale:
            return InPin == 0 ? "Vec2" : "Scalar";
        case Script::LogicNodeKind::Branch:
            return InPin == 0 ? "Exec" : "Condition";
        case Script::LogicNodeKind::LogValue:
            return InPin == 0 ? "Exec" : "Value";
        case Script::LogicNodeKind::SetPosition:
        case Script::LogicNodeKind::SetVelocity:
            return InPin == 0 ? "Exec" : "Value";
        case Script::LogicNodeKind::DespawnSelf:
            return "Exec";
        default:
            return "Value";
    }
}

[[nodiscard]] const char* OutputLabel(const Script::LogicGraph& InGraph, std::uint16_t InNode,
                                      std::uint8_t InPin) noexcept
{
    if (InNode >= InGraph.nodeCount)
    {
        return "?";
    }
    switch (InGraph.nodes[InNode].kind)
    {
        case Script::LogicNodeKind::Branch:
            return InPin == 0 ? "True" : "False";
        case Script::LogicNodeKind::GetField:
            return FieldName(InGraph, static_cast<std::uint16_t>(InGraph.nodes[InNode].param));
        case Script::LogicNodeKind::Condition:
        case Script::LogicNodeKind::Math:
            return "Result";
        case Script::LogicNodeKind::ConstFloat:
        case Script::LogicNodeKind::ConstVec2:
        case Script::LogicNodeKind::SelfPosition:
        case Script::LogicNodeKind::SelfVelocity:
        case Script::LogicNodeKind::SelfRotation:
        case Script::LogicNodeKind::DeltaTime:
        case Script::LogicNodeKind::Random01:
            return "Value";
        default:
            return "Then";
    }
}

[[nodiscard]] std::uint32_t OutputCount(const Script::LogicGraph& InGraph, std::uint16_t InNode) noexcept
{
    if (InNode >= InGraph.nodeCount)
    {
        return 0;
    }
    return InGraph.nodes[InNode].kind == Script::LogicNodeKind::Branch ? 2u : 1u;
}
}

bool VisualScriptPanel::Initialize(CommandStack& InCommands, const Scene::ComponentRegistry* InComponents,
                                   const Script::AotBuildOptions& InAotOptions)
{
    if (MInitialized)
    {
        return true;
    }
    MComponents = InComponents;
    MAotOptions = InAotOptions;
    if (MAotOptions.outputDirectory.empty())
    {
        std::error_code ScopedCode;
        MAotOptions.outputDirectory = std::filesystem::current_path(ScopedCode) / "build" / "aot";
    }
    if (MAotOptions.includeDirectory.empty())
    {
#if defined(FBY_ENGINE_INCLUDE_DIR)
        MAotOptions.includeDirectory = FBY_ENGINE_INCLUDE_DIR;
#else
        MAotOptions.includeDirectory = std::filesystem::path("engine/include");
#endif
    }
    if (MAotOptions.moduleName.empty())
    {
        MAotOptions.moduleName = "logic_graph";
    }

    MCodec = InCommands.RegisterExternalBuffer("LogicGraphDocument", &MDocument,
                                               static_cast<std::uint32_t>(sizeof(LogicGraphDocument)));
    MCanvas.Initialize(InCommands, MCodec);
    std::snprintf(MTextSource.data(), MTextSource.size(), "%s",
                  "event update {\n    let half = 0.5;\n    self.Transform2D.Rotation = self.Transform2D.Rotation + half;\n}\n");

    CanvasModelOps ScopedOps;
    ScopedOps.user = this;
    ScopedOps.nodeCount = &VisualScriptPanel::ModelNodeCount;
    ScopedOps.nodeTitle = &VisualScriptPanel::ModelNodeTitle;
    ScopedOps.nodePosition = &VisualScriptPanel::ModelNodePosition;
    ScopedOps.setNodePosition = &VisualScriptPanel::ModelSetNodePosition;
    ScopedOps.inputCount = &VisualScriptPanel::ModelInputCount;
    ScopedOps.inputName = &VisualScriptPanel::ModelInputName;
    ScopedOps.outputCount = &VisualScriptPanel::ModelOutputCount;
    ScopedOps.outputName = &VisualScriptPanel::ModelOutputName;
    ScopedOps.linkCount = &VisualScriptPanel::ModelLinkCount;
    ScopedOps.linkEndpoints = &VisualScriptPanel::ModelLinkEndpoints;
    ScopedOps.connect = &VisualScriptPanel::ModelConnect;
    ScopedOps.disconnect = &VisualScriptPanel::ModelDisconnect;
    ScopedOps.deleteNodes = &VisualScriptPanel::ModelDeleteNodes;
    ScopedOps.addableCount = &VisualScriptPanel::ModelAddableCount;
    ScopedOps.addableName = &VisualScriptPanel::ModelAddableName;
    ScopedOps.addNode = &VisualScriptPanel::ModelAddNode;
    ScopedOps.drawNodeBody = &VisualScriptPanel::ModelDrawNodeBody;
    ScopedOps.drawPanelTools = &VisualScriptPanel::ModelDrawPanelTools;
    MCanvas.SetModel(ScopedOps);

    BuildDefaultGraph();
    MInitialized = true;
    (void)Compile();
    return true;
}

void VisualScriptPanel::Shutdown()
{
    MCanvas.CommitDocumentEdit();
    MAot.Cancel();
    MCanvas.Shutdown();
    MInitialized = false;
}

void VisualScriptPanel::SetFonts(const EditorFonts* InFonts) noexcept
{
    MFonts = InFonts;
    MCanvas.SetModelFonts(MFonts != nullptr ? MFonts->ScriptNodeTitle() : nullptr,
                          MFonts != nullptr ? MFonts->ScriptPinInput() : nullptr,
                          MFonts != nullptr ? MFonts->ScriptPinOutput() : nullptr,
                          MFonts != nullptr ? MFonts->PanelTitle() : nullptr);
}

void VisualScriptPanel::Draw(const char* InWindowTitle, bool* InOpen)
{
    if (!MInitialized || (InOpen != nullptr && !*InOpen))
    {
        return;
    }
    PollAot();
    if (MAutoCompile && Script::HashLogicGraph(MDocument.graph) != MLastCompiledHash)
    {
        (void)Compile();
    }
    MCanvas.SetHighlightNode(MDebugNode);
    MCanvas.Draw(InWindowTitle, InOpen);
}

void VisualScriptPanel::PollAot()
{
    const std::uint64_t ScopedRevision = MAot.Revision();
    if (ScopedRevision != MLastAotRevision)
    {
        MLastAotRevision = ScopedRevision;
        MAotResult = MAot.Snapshot();
        if (MAotResult.state == Script::AotBuildState::Succeeded)
        {
            WriteAotManifest();
            SetStatusText(MStatus.data(), MStatus.size(), "AOT: " + MAotResult.libraryPath.string());
        }
        else if (MAotResult.state == Script::AotBuildState::Failed)
        {
            SetStatusText(MStatus.data(), MStatus.size(), "AOT failed: " + MAotResult.error);
        }
    }
}

void VisualScriptPanel::WriteAotManifest()
{
    if (MAotProgramHash == 0 || MAotResult.libraryPath.empty())
    {
        return;
    }
    Script::AotProgramRef ScopedRef;
    ScopedRef.Hash = MAotProgramHash;
    ScopedRef.LibraryPath = MAotResult.libraryPath.filename().string();
    ScopedRef.Name = MAotOptions.moduleName;
    const std::array<Script::AotProgramRef, 1> ScopedRefs{ScopedRef};
    const std::string ScopedManifest = Script::AotHost::BuildManifest(ScopedRefs);
    const std::filesystem::path ScopedPath = MAotResult.libraryPath.parent_path() / (MAotOptions.moduleName + ".fbyaot");
    std::ofstream ScopedFile(ScopedPath, std::ios::binary | std::ios::trunc);
    if (ScopedFile)
    {
        ScopedFile << ScopedManifest;
    }
}

bool VisualScriptPanel::CompileAndNativize()
{
    if (!Compile())
    {
        return false;
    }
    MAotProgramHash = Script::HashProgramBytes(MCompiled.program);
    if (!StartAotBuild())
    {
        return false;
    }
    SetStatusText(MStatus.data(), MStatus.size(), "compiled + nativizing");
    return true;
}

bool VisualScriptPanel::Compile()
{
    Script::LogicCompileOptions ScopedOptions;
    ScopedOptions.graphHash = Script::HashLogicGraph(MDocument.graph);
    MCompiled = {};
    MLastCompiledHash = ScopedOptions.graphHash;
    if (!Script::CompileLogicGraph(MDocument.graph, ScopedOptions, &MCompiled))
    {
        SetStatusText(MStatus.data(), MStatus.size(), "compile: " + MCompiled.error);
        return false;
    }
    char ScopedBuffer[kCanvasStatusBytes] = {};
    std::snprintf(ScopedBuffer, sizeof(ScopedBuffer), "PIE: %u instructions, %u registers, %u fields",
                  MCompiled.instructionCount, MCompiled.registerHighWater, MCompiled.fieldBindings);
    SetStatusText(MStatus.data(), MStatus.size(), ScopedBuffer);
    return true;
}

bool VisualScriptPanel::CompileText()
{
    if (MComponents == nullptr)
    {
        SetStatusText(MStatus.data(), MStatus.size(), "text compile: no component registry");
        return false;
    }
    Script::TextCompileOptions ScopedOptions;
    ScopedOptions.sourceHash = Reflect::Fnv1a(MTextSource.data());
    std::string ScopedError;
    if (!Script::CompileTextScript(MTextSource.data(), *MComponents, ScopedOptions, &MTextProgram, &ScopedError))
    {
        SetStatusText(MStatus.data(), MStatus.size(), "text: " + ScopedError);
        return false;
    }
    char ScopedBuffer[kCanvasStatusBytes] = {};
    std::snprintf(ScopedBuffer, sizeof(ScopedBuffer), "text: %u bytes of bytecode",
                  static_cast<unsigned>(MTextProgram.size()));
    SetStatusText(MStatus.data(), MStatus.size(), ScopedBuffer);
    return true;
}

bool VisualScriptPanel::StartAotBuild()
{
    std::string ScopedError;
    // Capture the revision before Start(): a very fast worker can publish twice
    MLastAotRevision = MAot.Revision();
    if (!MAot.Start(MDocument.graph, MAotOptions, &ScopedError))
    {
        SetStatusText(MStatus.data(), MStatus.size(), "AOT: " + ScopedError);
        return false;
    }
    SetStatusText(MStatus.data(), MStatus.size(), "AOT build started");
    return true;
}

void VisualScriptPanel::SetStatusText(char* InDestination, std::size_t InCapacity, const std::string& InText) noexcept
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

void VisualScriptPanel::BuildDefaultGraph()
{
    MDocument.graph.Clear();
    MDocument.nodePositions.fill(Fby::Vector2D{});
    MDefaultField = FindOrAddField("Transform2D", "Position");
    const std::uint16_t ScopedEvent = MDocument.graph.AddNode(Script::LogicNodeKind::EventUpdate);
    if (ScopedEvent == 0xFFFFu || MDefaultField == 0xFFFFu)
    {
        return;
    }
    const std::uint16_t ScopedGet = MDocument.graph.AddFieldNode(Script::LogicNodeKind::GetField, MDefaultField);
    const std::uint16_t ScopedSet = MDocument.graph.AddFieldNode(Script::LogicNodeKind::SetField, MDefaultField);
    if (ScopedGet == 0xFFFFu || ScopedSet == 0xFFFFu)
    {
        return;
    }
    MDocument.nodePositions[ScopedEvent] = Fby::Vector2D{40.0f, 160.0f};
    MDocument.nodePositions[ScopedGet] = Fby::Vector2D{40.0f, 40.0f};
    MDocument.nodePositions[ScopedSet] = Fby::Vector2D{380.0f, 120.0f};
    (void)MDocument.graph.AddLink(ScopedGet, 0, ScopedSet, 1);
    (void)MDocument.graph.AddLink(ScopedEvent, 0, ScopedSet, 0);
}

std::uint16_t VisualScriptPanel::FindOrAddField(std::string_view InComponentName, std::string_view InFieldName)
{
    Script::LogicFieldBinding ScopedBinding{};
    if (MComponents != nullptr)
    {
        const Scene::ComponentOps* ScopedOps = MComponents->Find(InComponentName);
        if (ScopedOps != nullptr)
        {
            const Reflect::FieldDesc* ScopedField = nullptr;
            for (const Reflect::FieldDesc& ScopedCandidate : ScopedOps->fields)
            {
                if (ScopedCandidate.name == InFieldName)
                {
                    ScopedField = &ScopedCandidate;
                    break;
                }
            }
            if (ScopedField != nullptr && Script::LogicFieldIsScriptable(static_cast<std::uint8_t>(ScopedField->kind)))
            {
                ScopedBinding.layoutHash = ScopedOps->layoutHash;
                ScopedBinding.componentSize = ScopedOps->size;
                ScopedBinding.offset = ScopedField->offset;
                ScopedBinding.fieldSize = ScopedField->size;
                ScopedBinding.kind = static_cast<std::uint8_t>(ScopedField->kind);
                const auto ScopedCopy =
                    [](char* InDestination, std::size_t InCapacity, std::string_view InText) noexcept
                {
                    const std::size_t ScopedLength = InText.size() < InCapacity - 1 ? InText.size() : InCapacity - 1;
                    for (std::size_t ScopedI = 0; ScopedI < ScopedLength; ++ScopedI)
                    {
                        InDestination[ScopedI] = InText[ScopedI];
                    }
                    InDestination[ScopedLength] = '\0';
                };
                ScopedCopy(ScopedBinding.componentName, sizeof(ScopedBinding.componentName), ScopedOps->name);
                ScopedCopy(ScopedBinding.fieldName, sizeof(ScopedBinding.fieldName), ScopedField->name);
            }
        }
    }
    if (ScopedBinding.layoutHash == 0 && InComponentName == "Transform2D" && InFieldName == "Position")
    {
        ScopedBinding = Script::MakeLogicFieldBinding<Transform2D>("Position");
    }
    if (ScopedBinding.layoutHash == 0)
    {
        return 0xFFFFu;
    }
    for (std::uint32_t ScopedI = 0; ScopedI < MDocument.graph.fieldCount; ++ScopedI)
    {
        if (MDocument.graph.fields[ScopedI].layoutHash == ScopedBinding.layoutHash &&
            MDocument.graph.fields[ScopedI].offset == ScopedBinding.offset)
        {
            return static_cast<std::uint16_t>(ScopedI);
        }
    }
    return MDocument.graph.AddFieldBinding(ScopedBinding);
}

std::uint16_t VisualScriptPanel::DefaultField()
{
    if (MDefaultField == 0xFFFFu)
    {
        MDefaultField = FindOrAddField("Transform2D", "Position");
    }
    return MDefaultField;
}

bool VisualScriptPanel::RebuildWithoutNodes(LogicGraphDocument& InDocument, std::span<const std::uint16_t> InNodes,
                                            std::string* InError)
{
    if (InNodes.empty())
    {
        return true;
    }
    const Script::LogicGraph& ScopedGraph = InDocument.graph;
    std::array<bool, Script::kMaxLogicNodes> ScopedRemoved{};
    for (const std::uint16_t ScopedNode : InNodes)
    {
        if (ScopedNode < ScopedGraph.nodeCount)
        {
            ScopedRemoved[ScopedNode] = true;
        }
    }
    Script::LogicGraph ScopedRebuilt;
    ScopedRebuilt.fields = ScopedGraph.fields;
    ScopedRebuilt.fieldCount = ScopedGraph.fieldCount;
    std::array<Fby::Vector2D, Script::kMaxLogicNodes> ScopedRebuiltPositions{};
    std::array<std::uint16_t, Script::kMaxLogicNodes> ScopedMap{};
    ScopedMap.fill(0xFFFFu);
    for (std::uint32_t ScopedI = 0; ScopedI < ScopedGraph.nodeCount; ++ScopedI)
    {
        if (ScopedRemoved[ScopedI])
        {
            continue;
        }
        if (ScopedRebuilt.nodeCount >= Script::kMaxLogicNodes)
        {
            if (InError != nullptr)
            {
                *InError = "LogicGraph node array is full";
            }
            return false;
        }
        ScopedMap[ScopedI] = static_cast<std::uint16_t>(ScopedRebuilt.nodeCount);
        ScopedRebuilt.nodes[ScopedRebuilt.nodeCount] = ScopedGraph.nodes[ScopedI];
        ScopedRebuiltPositions[ScopedRebuilt.nodeCount] = InDocument.nodePositions[ScopedI];
        ++ScopedRebuilt.nodeCount;
    }
    for (std::uint32_t ScopedI = 0; ScopedI < ScopedGraph.linkCount; ++ScopedI)
    {
        const Script::LogicLink& ScopedLink = ScopedGraph.links[ScopedI];
        if (ScopedLink.fromNode >= ScopedGraph.nodeCount || ScopedLink.toNode >= ScopedGraph.nodeCount ||
            ScopedMap[ScopedLink.fromNode] == 0xFFFFu || ScopedMap[ScopedLink.toNode] == 0xFFFFu)
        {
            continue;
        }
        if (ScopedRebuilt.linkCount >= Script::kMaxLogicLinks)
        {
            if (InError != nullptr)
            {
                *InError = "LogicGraph link array is full";
            }
            return false;
        }
        Script::LogicLink ScopedCopy = ScopedLink;
        ScopedCopy.fromNode = ScopedMap[ScopedLink.fromNode];
        ScopedCopy.toNode = ScopedMap[ScopedLink.toNode];
        ScopedRebuilt.links[ScopedRebuilt.linkCount++] = ScopedCopy;
    }
    InDocument.graph = ScopedRebuilt;
    InDocument.nodePositions = ScopedRebuiltPositions;
    return true;
}

// Canvas callbacks
std::uint32_t VisualScriptPanel::ModelNodeCount(void* InUser) noexcept
{
    return static_cast<VisualScriptPanel*>(InUser)->MDocument.graph.nodeCount;
}

const char* VisualScriptPanel::ModelNodeTitle(void* InUser, std::uint16_t InNode) noexcept
{
    const Script::LogicGraph& ScopedGraph = static_cast<VisualScriptPanel*>(InUser)->MDocument.graph;
    return InNode < ScopedGraph.nodeCount ? Script::LogicNodeKindName(ScopedGraph.nodes[InNode].kind) : "?";
}

ImVec2 VisualScriptPanel::ModelNodePosition(void* InUser, std::uint16_t InNode) noexcept
{
    const LogicGraphDocument& ScopedDocument = static_cast<VisualScriptPanel*>(InUser)->MDocument;
    if (InNode >= ScopedDocument.graph.nodeCount)
    {
        return ImVec2{0.0f, 0.0f};
    }
    const Fby::Vector2D ScopedPosition = ScopedDocument.nodePositions[InNode];
    return ImVec2{ScopedPosition.X, ScopedPosition.Y};
}

void VisualScriptPanel::ModelSetNodePosition(void* InUser, std::uint16_t InNode, ImVec2 InPosition) noexcept
{
    LogicGraphDocument& ScopedDocument = static_cast<VisualScriptPanel*>(InUser)->MDocument;
    if (InNode < ScopedDocument.graph.nodeCount)
    {
        ScopedDocument.nodePositions[InNode] = Fby::Vector2D{InPosition.x, InPosition.y};
    }
}

std::uint8_t VisualScriptPanel::ModelInputCount(void* InUser, std::uint16_t InNode) noexcept
{
    const Script::LogicGraph& ScopedGraph = static_cast<VisualScriptPanel*>(InUser)->MDocument.graph;
    return InNode < ScopedGraph.nodeCount ? ScopedGraph.nodes[InNode].inputCount : 0;
}

const char* VisualScriptPanel::ModelInputName(void* InUser, std::uint16_t InNode, std::uint8_t InPin) noexcept
{
    const Script::LogicGraph& ScopedGraph = static_cast<VisualScriptPanel*>(InUser)->MDocument.graph;
    return InputLabel(ScopedGraph, InNode, InPin);
}

std::uint8_t VisualScriptPanel::ModelOutputCount(void* InUser, std::uint16_t InNode) noexcept
{
    const Script::LogicGraph& ScopedGraph = static_cast<VisualScriptPanel*>(InUser)->MDocument.graph;
    return static_cast<std::uint8_t>(OutputCount(ScopedGraph, InNode));
}

const char* VisualScriptPanel::ModelOutputName(void* InUser, std::uint16_t InNode, std::uint8_t InPin) noexcept
{
    const Script::LogicGraph& ScopedGraph = static_cast<VisualScriptPanel*>(InUser)->MDocument.graph;
    return OutputLabel(ScopedGraph, InNode, InPin);
}

std::uint32_t VisualScriptPanel::ModelLinkCount(void* InUser) noexcept
{
    return static_cast<VisualScriptPanel*>(InUser)->MDocument.graph.linkCount;
}

void VisualScriptPanel::ModelLinkEndpoints(void* InUser, std::uint32_t InLink, std::uint16_t* InFromNode,
                                           std::uint8_t* InFromPin, std::uint16_t* InToNode,
                                           std::uint8_t* InToPin) noexcept
{
    const Script::LogicGraph& ScopedGraph = static_cast<VisualScriptPanel*>(InUser)->MDocument.graph;
    if (InLink >= ScopedGraph.linkCount)
    {
        return;
    }
    *InFromNode = ScopedGraph.links[InLink].fromNode;
    *InFromPin = ScopedGraph.links[InLink].fromPin;
    *InToNode = ScopedGraph.links[InLink].toNode;
    *InToPin = ScopedGraph.links[InLink].toPin;
}

bool VisualScriptPanel::ModelConnect(void* InUser, std::uint16_t InToNode, std::uint8_t InToPin,
                                     std::uint16_t InFromNode, std::uint8_t InFromPin, std::string* InError)
{
    auto* ScopedSelf = static_cast<VisualScriptPanel*>(InUser);
    Script::LogicGraph& ScopedGraph = ScopedSelf->MDocument.graph;
    if (InToNode >= ScopedGraph.nodeCount || InFromNode >= ScopedGraph.nodeCount || InToNode == InFromNode)
    {
        if (InError != nullptr)
        {
            *InError = "invalid or self-referencing link";
        }
        return false;
    }
    const Script::LogicNode& ScopedTarget = ScopedGraph.nodes[InToNode];
    const Script::LogicNode& ScopedSource = ScopedGraph.nodes[InFromNode];
    if (InToPin >= ScopedTarget.inputCount)
    {
        if (InError != nullptr)
        {
            *InError = "link targets a pin the node does not have";
        }
        return false;
    }
    const std::uint8_t ScopedMaxOut = ScopedSource.kind == Script::LogicNodeKind::Branch ? 2u : 1u;
    if (InFromPin >= ScopedMaxOut)
    {
        if (InError != nullptr)
        {
            *InError = "link reads a pin the node does not have";
        }
        return false;
    }
    const Script::PinType ScopedExpected = Script::LogicGraph::InputType(ScopedTarget.kind, InToPin);
    const Script::PinType ScopedProduced =
        ScopedSource.kind == Script::LogicNodeKind::GetField && ScopedSource.param < ScopedGraph.fieldCount
            ? Script::LogicGraph::FieldPinType(ScopedGraph.fields[ScopedSource.param].kind)
            : static_cast<Script::PinType>(ScopedSource.outputType);
    if (ScopedExpected != Script::PinType::Any && ScopedProduced != ScopedExpected)
    {
        if (InError != nullptr)
        {
            *InError = "link connects incompatible pin types";
        }
        return false;
    }
    (void)ScopedGraph.RemoveLinkToInput(InToNode, InToPin);
    if (!ScopedGraph.AddLink(InFromNode, InFromPin, InToNode, InToPin))
    {
        if (InError != nullptr)
        {
            *InError = "LogicGraph link table is full";
        }
        return false;
    }
    return true;
}

void VisualScriptPanel::ModelDisconnect(void* InUser, std::uint16_t InToNode, std::uint8_t InToPin) noexcept
{
    (void)static_cast<VisualScriptPanel*>(InUser)->MDocument.graph.RemoveLinkToInput(InToNode, InToPin);
}

bool VisualScriptPanel::ModelDeleteNodes(void* InUser, std::span<const std::uint16_t> InNodes, std::string* InError)
{
    return RebuildWithoutNodes(static_cast<VisualScriptPanel*>(InUser)->MDocument, InNodes, InError);
}

std::uint32_t VisualScriptPanel::ModelAddableCount(void*) noexcept
{
    return kAddableCount;
}

const char* VisualScriptPanel::ModelAddableName(void*, std::uint32_t InAddable) noexcept
{
    return InAddable < kAddableCount ? Script::LogicNodeKindName(kAddableKinds[InAddable]) : "?";
}

bool VisualScriptPanel::ModelAddNode(void* InUser, std::uint32_t InAddable, ImVec2 InPosition, std::uint16_t* OutNode,
                                     std::string* InError)
{
    auto* ScopedSelf = static_cast<VisualScriptPanel*>(InUser);
    if (InAddable >= kAddableCount)
    {
        if (InError != nullptr)
        {
            *InError = "unknown LogicGraph node kind";
        }
        return false;
    }
    const Script::LogicNodeKind ScopedKind = kAddableKinds[InAddable];
    std::uint16_t ScopedCreated = 0xFFFFu;
    switch (ScopedKind)
    {
        case Script::LogicNodeKind::GetField:
        case Script::LogicNodeKind::SetField:
        {
            const std::uint16_t ScopedField = ScopedSelf->DefaultField();
            if (ScopedField == 0xFFFFu)
            {
                if (InError != nullptr)
                {
                    *InError = "no scriptable Transform2D.position field is registered";
                }
                return false;
            }
            ScopedCreated = ScopedSelf->MDocument.graph.AddFieldNode(ScopedKind, ScopedField);
            break;
        }
        case Script::LogicNodeKind::ConstFloat:
            ScopedCreated = ScopedSelf->MDocument.graph.AddNode(ScopedKind, std::bit_cast<std::uint32_t>(0.5f));
            break;
        case Script::LogicNodeKind::ConstVec2:
            ScopedCreated = ScopedSelf->MDocument.graph.AddNode(ScopedKind, std::bit_cast<std::uint32_t>(0.0f));
            if (ScopedCreated != 0xFFFFu)
            {
                ScopedSelf->MDocument.graph.nodes[ScopedCreated].param2 = std::bit_cast<std::uint32_t>(0.0f);
            }
            break;
        case Script::LogicNodeKind::Condition:
            ScopedCreated = ScopedSelf->MDocument.graph.AddNode(
                ScopedKind, static_cast<std::uint32_t>(Script::LogicCompareOp::Equal));
            break;
        case Script::LogicNodeKind::Math:
            ScopedCreated =
                ScopedSelf->MDocument.graph.AddNode(ScopedKind, static_cast<std::uint32_t>(Script::LogicMathOp::Add));
            break;
        case Script::LogicNodeKind::SpawnEntity:
            ScopedCreated = ScopedSelf->MDocument.graph.AddNode(ScopedKind, 0);
            break;
        default:
            ScopedCreated = ScopedSelf->MDocument.graph.AddNode(ScopedKind);
            break;
    }
    if (ScopedCreated == 0xFFFFu)
    {
        if (InError != nullptr)
        {
            *InError = "LogicGraph is full";
        }
        return false;
    }
    if (ScopedCreated < Script::kMaxLogicNodes)
    {
        ScopedSelf->MDocument.nodePositions[ScopedCreated] = Fby::Vector2D{InPosition.x, InPosition.y};
    }
    *OutNode = ScopedCreated;
    return true;
}

void VisualScriptPanel::ModelDrawNodeBody(void* InUser, std::uint16_t InNode) noexcept
{
    const Script::LogicGraph& ScopedGraph = static_cast<VisualScriptPanel*>(InUser)->MDocument.graph;
    if (InNode >= ScopedGraph.nodeCount)
    {
        return;
    }
    const Script::LogicNode& ScopedRecord = ScopedGraph.nodes[InNode];
    switch (ScopedRecord.kind)
    {
        case Script::LogicNodeKind::GetField:
        case Script::LogicNodeKind::SetField:
            ImGui::TextDisabled("%s.%s", ComponentName(ScopedGraph, static_cast<std::uint16_t>(ScopedRecord.param)),
                                FieldName(ScopedGraph, static_cast<std::uint16_t>(ScopedRecord.param)));
            break;
        case Script::LogicNodeKind::ConstFloat:
            ImGui::TextDisabled("%.3f", static_cast<double>(std::bit_cast<float>(ScopedRecord.param)));
            break;
        case Script::LogicNodeKind::ConstVec2:
            ImGui::TextDisabled("(%.2f, %.2f)", static_cast<double>(std::bit_cast<float>(ScopedRecord.param)),
                                static_cast<double>(std::bit_cast<float>(ScopedRecord.param2)));
            break;
        case Script::LogicNodeKind::Condition:
            ImGui::TextDisabled("%s",
                                Script::LogicCompareOpName(static_cast<Script::LogicCompareOp>(ScopedRecord.param)));
            break;
        case Script::LogicNodeKind::Math:
            ImGui::TextDisabled("%s", Script::LogicMathOpName(static_cast<Script::LogicMathOp>(ScopedRecord.param)));
            break;
        case Script::LogicNodeKind::SpawnEntity:
            ImGui::TextDisabled("prefab %u", ScopedRecord.param);
            break;
        default:
            break;
    }
}

void VisualScriptPanel::ModelDrawPanelTools(void* InUser) noexcept
{
    static_cast<VisualScriptPanel*>(InUser)->DrawNodeInspector();
}

void VisualScriptPanel::DrawNodeInspector()
{
    if (ImGui::Button("Compile (PIE)"))
    {
        (void)Compile();
    }
    ImGui::SameLine();
    if (ImGui::Button("Compile + Nativize"))
    {
        (void)CompileAndNativize();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cook AOT"))
    {
        (void)StartAotBuild();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel AOT"))
    {
        MAot.Cancel();
    }
    ImGui::SameLine();
    ImGui::Checkbox("Auto compile", &MAutoCompile);
    ImGui::SameLine();
    ImGui::TextDisabled("%s", Script::AotBuildStateName(MAot.State()));

    if (MStatus[0] != '\0')
    {
        ImGui::TextWrapped("%s", MStatus.data());
    }
    DrawAotStatus();

    ImGui::Checkbox("Text mode", &MTextMode);
    if (MTextMode)
    {
        ImGui::InputTextMultiline("##femboyscript_text", MTextSource.data(), MTextSource.size(), ImVec2(-1.0f, 220.0f));
        if (ImGui::Button("Compile text"))
        {
            (void)CompileText();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%u bytes", static_cast<unsigned>(MTextProgram.size()));
        return;
    }

    const std::uint16_t ScopedSelected = MCanvas.SelectedNode();
    if (!ImGui::CollapsingHeader("Node", ImGuiTreeNodeFlags_DefaultOpen))
    {
        return;
    }
    if (ScopedSelected >= MDocument.graph.nodeCount)
    {
        ImGui::TextDisabled("select a node");
        return;
    }
    Script::LogicNode& ScopedNode = MDocument.graph.nodes[ScopedSelected];
    ImGui::Text("%u: %s", static_cast<unsigned>(ScopedSelected), Script::LogicNodeKindName(ScopedNode.kind));

    const auto ScopedFinishWidget = [this](bool InChanged, auto&& InApply)
    {
        if (ImGui::IsItemActivated())
        {
            (void)MCanvas.BeginDocumentEdit("Edit LogicGraph node");
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

    switch (ScopedNode.kind)
    {
        case Script::LogicNodeKind::ConstFloat:
        {
            float ScopedValue = std::bit_cast<float>(ScopedNode.param);
            const bool ScopedChanged = ImGui::DragFloat("##value", &ScopedValue, 0.01f);
            ScopedFinishWidget(ScopedChanged, [&] { ScopedNode.param = std::bit_cast<std::uint32_t>(ScopedValue); });
            break;
        }
        case Script::LogicNodeKind::ConstVec2:
        {
            float ScopedValues[2] = {std::bit_cast<float>(ScopedNode.param), std::bit_cast<float>(ScopedNode.param2)};
            const bool ScopedChanged = ImGui::DragFloat2("##value", ScopedValues, 0.01f);
            ScopedFinishWidget(ScopedChanged,
                               [&]
                               {
                                   ScopedNode.param = std::bit_cast<std::uint32_t>(ScopedValues[0]);
                                   ScopedNode.param2 = std::bit_cast<std::uint32_t>(ScopedValues[1]);
                               });
            break;
        }
        case Script::LogicNodeKind::Condition:
        {
            int ScopedOperation = static_cast<int>(ScopedNode.param);
            const char* ScopedCurrent =
                Script::LogicCompareOpName(static_cast<Script::LogicCompareOp>(ScopedOperation));
            if (ImGui::BeginCombo("Comparison", ScopedCurrent))
            {
                for (int ScopedIndex = 0; ScopedIndex < static_cast<int>(Script::LogicCompareOp::Count); ++ScopedIndex)
                {
                    const bool ScopedSelectedOp = ScopedIndex == ScopedOperation;
                    if (ImGui::Selectable(Script::LogicCompareOpName(static_cast<Script::LogicCompareOp>(ScopedIndex)),
                                          ScopedSelectedOp))
                    {
                        if (MCanvas.BeginDocumentEdit("Change condition"))
                        {
                            ScopedNode.param = static_cast<std::uint32_t>(ScopedIndex);
                            MCanvas.RecordDocumentEdit();
                            MCanvas.CommitDocumentEdit();
                        }
                    }
                    if (ScopedSelectedOp)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
            break;
        }
        case Script::LogicNodeKind::Math:
        {
            int ScopedOperation = static_cast<int>(ScopedNode.param);
            const char* ScopedCurrent = Script::LogicMathOpName(static_cast<Script::LogicMathOp>(ScopedOperation));
            if (ImGui::BeginCombo("Operation", ScopedCurrent))
            {
                for (int ScopedIndex = 0; ScopedIndex < static_cast<int>(Script::LogicMathOp::Count); ++ScopedIndex)
                {
                    const bool ScopedSelectedOp = ScopedIndex == ScopedOperation;
                    if (ImGui::Selectable(Script::LogicMathOpName(static_cast<Script::LogicMathOp>(ScopedIndex)),
                                          ScopedSelectedOp))
                    {
                        if (MCanvas.BeginDocumentEdit("Change math operation"))
                        {
                            ScopedNode.param = static_cast<std::uint32_t>(ScopedIndex);
                            MCanvas.RecordDocumentEdit();
                            MCanvas.CommitDocumentEdit();
                        }
                    }
                    if (ScopedSelectedOp)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
            break;
        }
        case Script::LogicNodeKind::SpawnEntity:
        {
            int ScopedPrefab = static_cast<int>(ScopedNode.param);
            const bool ScopedChanged = ImGui::DragInt("Prefab id", &ScopedPrefab, 1.0f, 0, 65535);
            ScopedFinishWidget(ScopedChanged, [&] { ScopedNode.param = static_cast<std::uint32_t>(ScopedPrefab); });
            break;
        }
        case Script::LogicNodeKind::GetField:
        case Script::LogicNodeKind::SetField:
        {
            ImGui::TextDisabled("Current: %s.%s",
                                ComponentName(MDocument.graph, static_cast<std::uint16_t>(ScopedNode.param)),
                                FieldName(MDocument.graph, static_cast<std::uint16_t>(ScopedNode.param)));
            if (MComponents != nullptr && ImGui::BeginCombo("Field", "Choose component field..."))
            {
                std::uint32_t ScopedComponentIndex = 0;
                for (const Scene::ComponentOps& ScopedOps : MComponents->All())
                {
                    for (const Reflect::FieldDesc& ScopedField : ScopedOps.fields)
                    {
                        if (!Script::LogicFieldIsScriptable(static_cast<std::uint8_t>(ScopedField.kind)))
                        {
                            continue;
                        }
                        ImGui::PushID(static_cast<int>(ScopedComponentIndex++));
                        char ScopedLabel[96] = {};
                        std::snprintf(ScopedLabel, sizeof(ScopedLabel), "%.*s.%.*s",
                                      static_cast<int>(ScopedOps.name.size()), ScopedOps.name.data(),
                                      static_cast<int>(ScopedField.name.size()), ScopedField.name.data());
                        if (ImGui::Selectable(ScopedLabel))
                        {
                            const std::uint16_t ScopedFieldIndex = FindOrAddField(ScopedOps.name, ScopedField.name);
                            if (ScopedFieldIndex != 0xFFFFu && MCanvas.BeginDocumentEdit("Rebind LogicGraph field"))
                            {
                                ScopedNode.param = ScopedFieldIndex;
                                if (ScopedNode.kind == Script::LogicNodeKind::GetField)
                                {
                                    ScopedNode.outputType = static_cast<std::uint8_t>(Script::LogicGraph::FieldPinType(
                                        MDocument.graph.fields[ScopedFieldIndex].kind));
                                }
                                MCanvas.RecordDocumentEdit();
                                MCanvas.CommitDocumentEdit();
                            }
                        }
                        ImGui::PopID();
                    }
                }
                ImGui::EndCombo();
            }
            break;
        }
        default:
            ImGui::TextDisabled("no editable parameter");
            break;
    }
}

void VisualScriptPanel::DrawAotStatus()
{
    const Script::AotBuildResult& ScopedResult = MAotResult;
    if (ScopedResult.state == Script::AotBuildState::Idle && ScopedResult.graphHash == 0)
    {
        return;
    }
    if (ImGui::CollapsingHeader("AOT module"))
    {
        ImGui::Text("state: %s", Script::AotBuildStateName(ScopedResult.state));
        ImGui::Text("instructions: %u", ScopedResult.instructions);
        if (!ScopedResult.sourcePath.empty())
        {
            ImGui::TextWrapped("source: %s", ScopedResult.sourcePath.string().c_str());
        }
        if (!ScopedResult.libraryPath.empty())
        {
            ImGui::TextWrapped("library: %s", ScopedResult.libraryPath.string().c_str());
        }
        if (!ScopedResult.logPath.empty())
        {
            ImGui::TextWrapped("log: %s", ScopedResult.logPath.string().c_str());
        }
        if (!ScopedResult.error.empty())
        {
            ImGui::TextWrapped("error: %s", ScopedResult.error.c_str());
        }
    }
}
}
