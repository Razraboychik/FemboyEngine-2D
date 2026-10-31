// FemboyEngine2D - MaterialGraph implementation: DAG construction, validation,
#include "Engine/RHI/MaterialGraph.hpp"
#include "Engine/RHI/MaterialGraphCompiler.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <string>
#include <string_view>

namespace Fby::Render
{

namespace
{

// Stable hashing
constexpr std::uint64_t kHashOffsetBasis = 0xCBF29CE484222325ull;
constexpr std::uint64_t kHashPrime = 0x00000100000001B3ull;

void HashByte(std::uint64_t& InHash, std::uint8_t InByte) noexcept
{
    InHash ^= static_cast<std::uint64_t>(InByte);
    InHash *= kHashPrime;
}

void HashU16(std::uint64_t& InHash, std::uint16_t InValue) noexcept
{
    HashByte(InHash, static_cast<std::uint8_t>(InValue & 0xFFu));
    HashByte(InHash, static_cast<std::uint8_t>((InValue >> 8) & 0xFFu));
}

void HashU32(std::uint64_t& InHash, std::uint32_t InValue) noexcept
{
    for (std::uint32_t ScopedShift = 0; ScopedShift < 32; ScopedShift += 8)
    {
        HashByte(InHash, static_cast<std::uint8_t>((InValue >> ScopedShift) & 0xFFu));
    }
}

void HashFloat(std::uint64_t& InHash, float InValue) noexcept
{
    HashU32(InHash, BitCast<std::uint32_t>(InValue));
}

// Error helpers. Errors are only built on the failure path, which is cold.
void SetError(std::string* InError, std::string_view InText)
{
    if (InError != nullptr)
    {
        InError->assign(InText);
    }
}

std::string NodeLabel(std::uint32_t InNodeIndex, MaterialNodeKind InKind)
{
    return "node " + std::to_string(InNodeIndex) + " (" + MaterialNodeKindName(InKind) + ")";
}

std::string InputLabel(const MaterialNode& InNode, std::uint32_t InSlot)
{
    return "input " + std::to_string(InSlot) + " (" +
           MaterialNodeInputName(InNode.kind, static_cast<std::uint8_t>(InSlot)) + ")";
}

// Minimal structural access that never asserts: a corrupt or half-deserialized
[[nodiscard]] bool PinIndexValid(const MaterialGraph& InGraph, const MaterialNode& InNode,
                                 std::uint32_t InSlot) noexcept
{
    if (InSlot >= InNode.inputCount)
    {
        return false;
    }
    return static_cast<std::uint32_t>(InNode.firstInput) + InSlot < InGraph.PinCount();
}

[[nodiscard]] const MaterialPin* FindPin(const MaterialGraph& InGraph, const MaterialNode& InNode,
                                         std::uint32_t InSlot) noexcept
{
    if (!PinIndexValid(InGraph, InNode, InSlot))
    {
        return nullptr;
    }
    return &InGraph.Pin(static_cast<std::uint16_t>(static_cast<std::uint32_t>(InNode.firstInput) + InSlot));
}

// An edge exists only when the source is a real node other than the consumer
[[nodiscard]] bool IsGraphEdge(std::uint32_t InConsumer, std::uint16_t InSource, std::uint32_t InNodeCount) noexcept
{
    return InSource != kInvalidMaterialNode && InSource < InNodeCount && InSource != InConsumer;
}

// Kahn's algorithm over every node, deterministic: the lowest ready index is
[[nodiscard]] bool TopologicalSortAll(const MaterialGraph& InGraph, std::uint16_t* InOrder, std::uint32_t* OutCount,
                                      std::string* InError)
{
    const std::uint32_t ScopedNodeCount = InGraph.NodeCount();
    std::uint16_t ScopedIndegree[kMaxMaterialNodes] = {};
    for (std::uint32_t ScopedConsumer = 0; ScopedConsumer < ScopedNodeCount; ++ScopedConsumer)
    {
        const MaterialNode& ScopedNode = InGraph.Node(static_cast<std::uint16_t>(ScopedConsumer));
        for (std::uint32_t ScopedSlot = 0; ScopedSlot < ScopedNode.inputCount; ++ScopedSlot)
        {
            const MaterialPin* ScopedPin = FindPin(InGraph, ScopedNode, ScopedSlot);
            if (ScopedPin != nullptr && IsGraphEdge(ScopedConsumer, ScopedPin->sourceNode, ScopedNodeCount))
            {
                ++ScopedIndegree[ScopedConsumer];
            }
        }
    }

    bool ScopedDone[kMaxMaterialNodes] = {};
    std::uint32_t ScopedEmitted = 0;
    while (ScopedEmitted < ScopedNodeCount)
    {
        std::uint32_t ScopedReady = ScopedNodeCount;
        for (std::uint32_t ScopedCandidate = 0; ScopedCandidate < ScopedNodeCount; ++ScopedCandidate)
        {
            if (!ScopedDone[ScopedCandidate] && ScopedIndegree[ScopedCandidate] == 0)
            {
                ScopedReady = ScopedCandidate;
                break;
            }
        }
        if (ScopedReady == ScopedNodeCount)
        {
            break;
        }
        InOrder[ScopedEmitted] = static_cast<std::uint16_t>(ScopedReady);
        ++ScopedEmitted;
        ScopedDone[ScopedReady] = true;

        // Release every consumer of the node just emitted.
        for (std::uint32_t ScopedConsumer = 0; ScopedConsumer < ScopedNodeCount; ++ScopedConsumer)
        {
            if (ScopedDone[ScopedConsumer])
            {
                continue;
            }
            const MaterialNode& ScopedNode = InGraph.Node(static_cast<std::uint16_t>(ScopedConsumer));
            for (std::uint32_t ScopedSlot = 0; ScopedSlot < ScopedNode.inputCount; ++ScopedSlot)
            {
                const MaterialPin* ScopedPin = FindPin(InGraph, ScopedNode, ScopedSlot);
                if (ScopedPin != nullptr && ScopedPin->sourceNode == static_cast<std::uint16_t>(ScopedReady))
                {
                    --ScopedIndegree[ScopedConsumer];
                }
            }
        }
    }

    if (ScopedEmitted != ScopedNodeCount)
    {
        for (std::uint32_t ScopedBlocked = 0; ScopedBlocked < ScopedNodeCount; ++ScopedBlocked)
        {
            if (!ScopedDone[ScopedBlocked])
            {
                SetError(InError,
                         "material graph contains a cycle (" +
                             NodeLabel(ScopedBlocked, InGraph.Node(static_cast<std::uint16_t>(ScopedBlocked)).kind) +
                             " cannot be scheduled)");
                return false;
            }
        }
        SetError(InError, "material graph contains a cycle");
        return false;
    }

    *OutCount = ScopedEmitted;
    return true;
}

// Resolves the output type of every node. Unconnected optional inputs fall back
[[nodiscard]] MaterialValueType ResolvedInputType(const MaterialGraph& InGraph, const MaterialValueType* InNodeTypes,
                                                  std::uint32_t InNodeIndex, std::uint32_t InSlot) noexcept
{
    const MaterialNode& ScopedNode = InGraph.Node(static_cast<std::uint16_t>(InNodeIndex));
    const MaterialPin* ScopedPin = FindPin(InGraph, ScopedNode, InSlot);
    if (ScopedPin == nullptr)
    {
        return MaterialValueType::Float;
    }
    if (IsGraphEdge(InNodeIndex, ScopedPin->sourceNode, InGraph.NodeCount()))
    {
        const MaterialValueType ScopedSourceType = InNodeTypes[ScopedPin->sourceNode];
        if (ScopedSourceType < MaterialValueType::Count)
        {
            return ScopedSourceType;
        }
    }
    return ScopedPin->type < MaterialValueType::Count ? ScopedPin->type : MaterialValueType::Float;
}

[[nodiscard]] bool ResolveTypesImpl(const MaterialGraph& InGraph, MaterialValueType* InNodeTypes, std::string* InError)
{
    std::uint16_t ScopedOrder[kMaxMaterialNodes] = {};
    std::uint32_t ScopedOrderCount = 0;
    if (!TopologicalSortAll(InGraph, ScopedOrder, &ScopedOrderCount, InError))
    {
        return false;
    }

    for (std::uint32_t ScopedScheduled = 0; ScopedScheduled < ScopedOrderCount; ++ScopedScheduled)
    {
        const std::uint16_t ScopedNodeIndex = ScopedOrder[ScopedScheduled];
        const MaterialNode& ScopedNode = InGraph.Node(ScopedNodeIndex);
        MaterialValueType ScopedOutputType = MaterialNodeStaticOutputType(ScopedNode.kind);
        if (ScopedNode.kind == MaterialNodeKind::Output)
        {
            // The Output node has no output
            ScopedOutputType = MaterialValueType::Count;
        }
        else if (ScopedOutputType >= MaterialValueType::Count)
        {
            const MaterialValueType ScopedLeft = ResolvedInputType(InGraph, InNodeTypes, ScopedNodeIndex, 0);
            const MaterialValueType ScopedRight = ResolvedInputType(InGraph, InNodeTypes, ScopedNodeIndex, 1);
            ScopedOutputType = MaterialNodeMathOutputType(ScopedNode.kind, ScopedLeft, ScopedRight);
        }
        InNodeTypes[ScopedNodeIndex] = ScopedOutputType;
    }
    return true;
}

// Helpers for the builder's math node factories.
[[nodiscard]] bool IsUnaryMathKind(MaterialNodeKind InKind) noexcept
{
    switch (InKind)
    {
        case MaterialNodeKind::Saturate:
        case MaterialNodeKind::Sin:
        case MaterialNodeKind::Cos:
        case MaterialNodeKind::OneMinus:
            return true;
        default:
            return false;
    }
}

[[nodiscard]] bool IsBinaryMathKind(MaterialNodeKind InKind) noexcept
{
    switch (InKind)
    {
        case MaterialNodeKind::Add:
        case MaterialNodeKind::Sub:
        case MaterialNodeKind::Mul:
        case MaterialNodeKind::Div:
        case MaterialNodeKind::Power:
        case MaterialNodeKind::Dot:
            return true;
        default:
            return false;
    }
}
}

// Node signature compatibility
bool MaterialNodeInputsCompatible(MaterialNodeKind InKind, std::span<const MaterialValueType> InInputTypes) noexcept
{
    const auto ScopedInput = [&InInputTypes](std::uint32_t InSlot) -> MaterialValueType
    { return InSlot < InInputTypes.size() ? InInputTypes[InSlot] : MaterialValueType::Float; };
    const auto ScopedScalar = [](MaterialValueType InType) { return InType == MaterialValueType::Float; };
    const auto ScopedVector = [](MaterialValueType InType)
    {
        return InType == MaterialValueType::Vector2D || InType == MaterialValueType::Vector3D ||
               InType == MaterialValueType::Vector4D;
    };

    if (InInputTypes.size() != MaterialNodeInputCount(InKind))
    {
        return false;
    }

    switch (InKind)
    {
        case MaterialNodeKind::Add:
        case MaterialNodeKind::Sub:
        case MaterialNodeKind::Mul:
        case MaterialNodeKind::Div:
        {
            // Vector widths must match; a scalar operand broadcasts.
            const MaterialValueType ScopedLeft = ScopedInput(0);
            const MaterialValueType ScopedRight = ScopedInput(1);
            if (!ScopedVector(ScopedLeft) && !ScopedVector(ScopedRight))
            {
                return true;
            }
            if (ScopedScalar(ScopedLeft) || ScopedScalar(ScopedRight))
            {
                return true;
            }
            return ScopedLeft == ScopedRight;
        }
        case MaterialNodeKind::Lerp:
        {
            const MaterialValueType ScopedFrom = ScopedInput(0);
            const MaterialValueType ScopedTo = ScopedInput(1);
            const MaterialValueType ScopedBlend = ScopedInput(2);
            if (ScopedFrom != ScopedTo)
            {
                return false;
            }
            return ScopedScalar(ScopedBlend) || ScopedBlend == ScopedFrom;
        }
        case MaterialNodeKind::Clamp:
        {
            const MaterialValueType ScopedValue = ScopedInput(0);
            for (std::uint32_t ScopedSlot = 1; ScopedSlot < 3; ++ScopedSlot)
            {
                if (!ScopedScalar(ScopedInput(ScopedSlot)) && ScopedInput(ScopedSlot) != ScopedValue)
                {
                    return false;
                }
            }
            return true;
        }
        case MaterialNodeKind::Power:
        {
            const MaterialValueType ScopedBase = ScopedInput(0);
            const MaterialValueType ScopedExponent = ScopedInput(1);
            return ScopedScalar(ScopedExponent) || ScopedExponent == ScopedBase;
        }
        case MaterialNodeKind::Dot:
        {
            const MaterialValueType ScopedLeft = ScopedInput(0);
            const MaterialValueType ScopedRight = ScopedInput(1);
            return ScopedLeft == ScopedRight && ScopedVector(ScopedLeft);
        }
        case MaterialNodeKind::Saturate:
        case MaterialNodeKind::Sin:
        case MaterialNodeKind::Cos:
        case MaterialNodeKind::OneMinus:
            return true;
        case MaterialNodeKind::Output:
        case MaterialNodeKind::TextureSample:
        case MaterialNodeKind::Fresnel:
        case MaterialNodeKind::Tilt25D:
        case MaterialNodeKind::Distortion:
        case MaterialNodeKind::Uv:
        case MaterialNodeKind::VertexColor:
        case MaterialNodeKind::Time:
        case MaterialNodeKind::WorldPosition:
        case MaterialNodeKind::VertexPosition:
        case MaterialNodeKind::Scalar:
        case MaterialNodeKind::Vector2D:
        case MaterialNodeKind::Vector3D:
        case MaterialNodeKind::Color:
        case MaterialNodeKind::Count:
            return true;
    }
    return true;
}

// MaterialGraph - construction
MaterialGraph::MaterialGraph()
{
    Clear();
}

void MaterialGraph::Clear() noexcept
{
    MNodes.fill(MaterialNode{});
    MPins.fill(MaterialPin{});
    MParams.fill(MaterialParam{});
    MNodeCount = 0;
    MPinCount = 0;
    MParamCount = 0;

    // The single Output node always exists at index 0; AddNode() refuses to
    MaterialNode& ScopedOutput = MNodes[0];
    ScopedOutput.kind = MaterialNodeKind::Output;
    ScopedOutput.firstInput = 0;
    ScopedOutput.inputCount = MaterialNodeInputCount(MaterialNodeKind::Output);
    ScopedOutput.paramIndex = kInvalidMaterialParam;
    ScopedOutput.flags = MaterialNodeFlags::MaterialNodeFlagNone;
    ScopedOutput.uiPosition = Vector2D{};
    MNodeCount = 1;

    for (std::uint8_t ScopedSlot = 0; ScopedSlot < ScopedOutput.inputCount; ++ScopedSlot)
    {
        MaterialPin& ScopedPin = MPins[MPinCount];
        ScopedPin = MaterialPin{};
        ScopedPin.node = 0;
        ScopedPin.slot = ScopedSlot;
        ScopedPin.type = MaterialNodeSlotDefaultType(MaterialNodeKind::Output, ScopedSlot);
        ScopedPin.sourceNode = kInvalidMaterialNode;
        ScopedPin.sourceSlot = 0;
        ScopedPin.flags = 0;
        ++MPinCount;
    }
}

bool MaterialGraph::AdoptCounts(std::uint32_t InNodeCount, std::uint32_t InPinCount,
                                std::uint32_t InParamCount) noexcept
{
    if (InNodeCount == 0 || InNodeCount > kMaxMaterialNodes || InPinCount > kMaxMaterialPins ||
        InParamCount > kMaxMaterialParams)
    {
        return false;
    }
    MNodes.fill(MaterialNode{});
    MPins.fill(MaterialPin{});
    MParams.fill(MaterialParam{});
    MNodeCount = InNodeCount;
    MPinCount = InPinCount;
    MParamCount = InParamCount;
    return true;
}

std::uint16_t MaterialGraph::AddNode(MaterialNodeKind InKind, Vector2D InUiPosition)
{
    if (InKind == MaterialNodeKind::Output || InKind >= MaterialNodeKind::Count)
    {
        return kInvalidMaterialNode;
    }
    const std::uint8_t ScopedInputs = MaterialNodeInputCount(InKind);
    if (MNodeCount >= kMaxMaterialNodes || MPinCount + ScopedInputs > kMaxMaterialPins)
    {
        return kInvalidMaterialNode;
    }

    const auto ScopedIndex = static_cast<std::uint16_t>(MNodeCount);
    MaterialNode& ScopedNode = MNodes[MNodeCount];
    ScopedNode = MaterialNode{};
    ScopedNode.kind = InKind;
    ScopedNode.firstInput = static_cast<std::uint16_t>(MPinCount);
    ScopedNode.inputCount = ScopedInputs;
    ScopedNode.paramIndex = kInvalidMaterialParam;
    ScopedNode.flags = MaterialNodeFlags::MaterialNodeFlagNone;
    ScopedNode.uiPosition = InUiPosition;
    ++MNodeCount;

    for (std::uint8_t ScopedSlot = 0; ScopedSlot < ScopedInputs; ++ScopedSlot)
    {
        MaterialPin& ScopedPin = MPins[MPinCount];
        ScopedPin = MaterialPin{};
        ScopedPin.node = ScopedIndex;
        ScopedPin.slot = ScopedSlot;
        ScopedPin.type = MaterialNodeSlotDefaultType(InKind, ScopedSlot);
        ScopedPin.sourceNode = kInvalidMaterialNode;
        ScopedPin.sourceSlot = 0;
        ScopedPin.flags = 0;
        ++MPinCount;
    }
    return ScopedIndex;
}

std::uint16_t MaterialGraph::AddInputNode(MaterialNodeKind InKind, Vector2D InUiPosition)
{
    switch (InKind)
    {
        case MaterialNodeKind::Uv:
        case MaterialNodeKind::VertexColor:
        case MaterialNodeKind::Time:
        case MaterialNodeKind::WorldPosition:
        case MaterialNodeKind::VertexPosition:
            return AddNode(InKind, InUiPosition);
        default:
            return kInvalidMaterialNode;
    }
}

std::uint16_t MaterialGraph::AddScalarNode(float InValue, Vector2D InUiPosition)
{
    if (MParamCount >= kMaxMaterialParams)
    {
        return kInvalidMaterialNode;
    }
    const std::uint16_t ScopedNode = AddNode(MaterialNodeKind::Scalar, InUiPosition);
    if (ScopedNode == kInvalidMaterialNode)
    {
        return kInvalidMaterialNode;
    }
    MaterialParam& ScopedParam = MParams[MParamCount];
    ScopedParam = MaterialParam{};
    ScopedParam.kind = MaterialParamKind::Scalar;
    ScopedParam.X = InValue;
    MNodes[ScopedNode].paramIndex = static_cast<std::uint16_t>(MParamCount);
    ++MParamCount;
    return ScopedNode;
}

std::uint16_t MaterialGraph::AddVec2Node(Vector2D InValue, Vector2D InUiPosition)
{
    if (MParamCount >= kMaxMaterialParams)
    {
        return kInvalidMaterialNode;
    }
    const std::uint16_t ScopedNode = AddNode(MaterialNodeKind::Vector2D, InUiPosition);
    if (ScopedNode == kInvalidMaterialNode)
    {
        return kInvalidMaterialNode;
    }
    MaterialParam& ScopedParam = MParams[MParamCount];
    ScopedParam = MaterialParam{};
    ScopedParam.kind = MaterialParamKind::Vector2D;
    ScopedParam.X = InValue.X;
    ScopedParam.Y = InValue.Y;
    MNodes[ScopedNode].paramIndex = static_cast<std::uint16_t>(MParamCount);
    ++MParamCount;
    return ScopedNode;
}

std::uint16_t MaterialGraph::AddVec3Node(float InX, float InY, float InZ, Vector2D InUiPosition)
{
    if (MParamCount >= kMaxMaterialParams)
    {
        return kInvalidMaterialNode;
    }
    const std::uint16_t ScopedNode = AddNode(MaterialNodeKind::Vector3D, InUiPosition);
    if (ScopedNode == kInvalidMaterialNode)
    {
        return kInvalidMaterialNode;
    }
    MaterialParam& ScopedParam = MParams[MParamCount];
    ScopedParam = MaterialParam{};
    ScopedParam.kind = MaterialParamKind::Vector3D;
    ScopedParam.X = InX;
    ScopedParam.Y = InY;
    ScopedParam.Z = InZ;
    MNodes[ScopedNode].paramIndex = static_cast<std::uint16_t>(MParamCount);
    ++MParamCount;
    return ScopedNode;
}

std::uint16_t MaterialGraph::AddColorNode(float InRed, float InGreen, float InBlue, float InAlpha,
                                          Vector2D InUiPosition)
{
    if (MParamCount >= kMaxMaterialParams)
    {
        return kInvalidMaterialNode;
    }
    const std::uint16_t ScopedNode = AddNode(MaterialNodeKind::Color, InUiPosition);
    if (ScopedNode == kInvalidMaterialNode)
    {
        return kInvalidMaterialNode;
    }
    MaterialParam& ScopedParam = MParams[MParamCount];
    ScopedParam = MaterialParam{};
    ScopedParam.kind = MaterialParamKind::Color;
    ScopedParam.X = InRed;
    ScopedParam.Y = InGreen;
    ScopedParam.Z = InBlue;
    ScopedParam.W = InAlpha;
    MNodes[ScopedNode].paramIndex = static_cast<std::uint16_t>(MParamCount);
    ++MParamCount;
    return ScopedNode;
}

std::uint16_t MaterialGraph::AddTextureSampleNode(std::uint32_t InTextureLayer, Vector2D InUiPosition)
{
    if (MParamCount >= kMaxMaterialParams)
    {
        return kInvalidMaterialNode;
    }
    const std::uint16_t ScopedNode = AddNode(MaterialNodeKind::TextureSample, InUiPosition);
    if (ScopedNode == kInvalidMaterialNode)
    {
        return kInvalidMaterialNode;
    }
    MaterialParam& ScopedParam = MParams[MParamCount];
    ScopedParam = MaterialParam{};
    ScopedParam.kind = MaterialParamKind::TextureLayer;
    ScopedParam.textureLayer = InTextureLayer;
    MNodes[ScopedNode].paramIndex = static_cast<std::uint16_t>(MParamCount);
    ++MParamCount;
    return ScopedNode;
}

std::uint16_t MaterialGraph::AddUnaryNode(MaterialNodeKind InKind, Vector2D InUiPosition)
{
    return IsUnaryMathKind(InKind) ? AddNode(InKind, InUiPosition) : kInvalidMaterialNode;
}

std::uint16_t MaterialGraph::AddBinaryNode(MaterialNodeKind InKind, Vector2D InUiPosition)
{
    return IsBinaryMathKind(InKind) ? AddNode(InKind, InUiPosition) : kInvalidMaterialNode;
}

std::uint16_t MaterialGraph::AddLerpNode(Vector2D InUiPosition)
{
    return AddNode(MaterialNodeKind::Lerp, InUiPosition);
}

std::uint16_t MaterialGraph::AddClampNode(Vector2D InUiPosition)
{
    return AddNode(MaterialNodeKind::Clamp, InUiPosition);
}

std::uint16_t MaterialGraph::AddFresnelNode(Vector2D InUiPosition)
{
    return AddNode(MaterialNodeKind::Fresnel, InUiPosition);
}

std::uint16_t MaterialGraph::AddTilt25DNode(Vector2D InSlope, float InHeight, Vector2D InUiPosition)
{
    if (MParamCount >= kMaxMaterialParams)
    {
        return kInvalidMaterialNode;
    }
    const std::uint16_t ScopedNode = AddNode(MaterialNodeKind::Tilt25D, InUiPosition);
    if (ScopedNode == kInvalidMaterialNode)
    {
        return kInvalidMaterialNode;
    }
    MaterialParam& ScopedParam = MParams[MParamCount];
    ScopedParam = MaterialParam{};
    ScopedParam.kind = MaterialParamKind::Vector2D;
    ScopedParam.X = InSlope.X;
    ScopedParam.Y = InSlope.Y;
    ScopedParam.Z = InHeight;
    MNodes[ScopedNode].paramIndex = static_cast<std::uint16_t>(MParamCount);
    ++MParamCount;
    return ScopedNode;
}

std::uint16_t MaterialGraph::AddDistortionNode(float InStrength, float InFrequency, Vector2D InUiPosition)
{
    if (MParamCount >= kMaxMaterialParams)
    {
        return kInvalidMaterialNode;
    }
    const std::uint16_t ScopedNode = AddNode(MaterialNodeKind::Distortion, InUiPosition);
    if (ScopedNode == kInvalidMaterialNode)
    {
        return kInvalidMaterialNode;
    }
    MaterialParam& ScopedParam = MParams[MParamCount];
    ScopedParam = MaterialParam{};
    ScopedParam.kind = MaterialParamKind::Scalar;
    ScopedParam.X = InStrength;
    ScopedParam.Y = InFrequency;
    MNodes[ScopedNode].paramIndex = static_cast<std::uint16_t>(MParamCount);
    ++MParamCount;
    return ScopedNode;
}

// MaterialGraph - connections
bool MaterialGraph::Connect(std::uint16_t InNode, std::uint8_t InSlot, std::uint16_t InSourceNode, std::string* InError)
{
    if (InNode >= MNodeCount)
    {
        SetError(InError, "material graph: connect target node " + std::to_string(InNode) + " is out of range");
        return false;
    }
    const MaterialNode& ScopedTarget = MNodes[InNode];
    if (InSlot >= ScopedTarget.inputCount)
    {
        SetError(InError, "material graph: " + InputLabel(ScopedTarget, InSlot) + " is out of range on " +
                              NodeLabel(InNode, ScopedTarget.kind));
        return false;
    }
    if (InSourceNode >= MNodeCount)
    {
        SetError(InError, "material graph: source node " + std::to_string(InSourceNode) + " is out of range");
        return false;
    }
    if (InSourceNode == InNode)
    {
        SetError(InError, "material graph: " + NodeLabel(InNode, ScopedTarget.kind) + " cannot feed itself");
        return false;
    }
    if (MNodes[InSourceNode].kind == MaterialNodeKind::Output)
    {
        SetError(InError, "material graph: the Output node has no output pin");
        return false;
    }

    MaterialValueType ScopedTypes[kMaxMaterialNodes] = {};
    if (!ResolveTypes({ScopedTypes, kMaxMaterialNodes}, InError))
    {
        return false;
    }
    const MaterialValueType ScopedSourceType = ScopedTypes[InSourceNode];
    if (ScopedSourceType >= MaterialValueType::Count)
    {
        SetError(InError,
                 "material graph: source node " + std::to_string(InSourceNode) + " has no resolvable output type");
        return false;
    }
    if (!MaterialSlotAccepts(ScopedTarget.kind, InSlot, ScopedSourceType))
    {
        SetError(InError, "material graph: " + InputLabel(ScopedTarget, InSlot) + " of " +
                              NodeLabel(InNode, ScopedTarget.kind) + " does not accept " +
                              MaterialValueTypeName(ScopedSourceType));
        return false;
    }

    // Cross-slot rules: pretend the connection is already made, then check the
    MaterialValueType ScopedInputs[kMaxMaterialInputs] = {};
    for (std::uint8_t ScopedInputSlot = 0; ScopedInputSlot < ScopedTarget.inputCount; ++ScopedInputSlot)
    {
        const MaterialPin* ScopedPin = FindPin(*this, ScopedTarget, ScopedInputSlot);
        const bool ScopedConnected = ScopedPin != nullptr && IsGraphEdge(InNode, ScopedPin->sourceNode, MNodeCount);
        if (ScopedInputSlot == InSlot || !ScopedConnected)
        {
            ScopedInputs[ScopedInputSlot] = ScopedSourceType;
        }
        else
        {
            ScopedInputs[ScopedInputSlot] =
                ScopedPin->type < MaterialValueType::Count ? ScopedPin->type : MaterialValueType::Float;
        }
    }
    if (!MaterialNodeInputsCompatible(ScopedTarget.kind,
                                      std::span<const MaterialValueType>(ScopedInputs, ScopedTarget.inputCount)))
    {
        SetError(InError, "material graph: " + NodeLabel(InNode, ScopedTarget.kind) + " input types are incompatible");
        return false;
    }

    MaterialPin& ScopedPin =
        MPins[static_cast<std::uint16_t>(static_cast<std::uint32_t>(ScopedTarget.firstInput) + InSlot)];
    ScopedPin.sourceNode = InSourceNode;
    ScopedPin.sourceSlot = 0;
    ScopedPin.type = ScopedSourceType;
    return true;
}

void MaterialGraph::Disconnect(std::uint16_t InNode, std::uint8_t InSlot) noexcept
{
    if (InNode >= MNodeCount)
    {
        return;
    }
    const MaterialNode& ScopedTarget = MNodes[InNode];
    if (InSlot >= ScopedTarget.inputCount)
    {
        return;
    }
    MaterialPin& ScopedPin =
        MPins[static_cast<std::uint16_t>(static_cast<std::uint32_t>(ScopedTarget.firstInput) + InSlot)];
    ScopedPin.sourceNode = kInvalidMaterialNode;
    ScopedPin.sourceSlot = 0;
    ScopedPin.type = MaterialNodeSlotDefaultType(ScopedTarget.kind, InSlot);
}

bool MaterialGraph::RefreshPinTypes(std::string* InError)
{
    MaterialValueType ScopedTypes[kMaxMaterialNodes] = {};
    if (!ResolveTypes({ScopedTypes, kMaxMaterialNodes}, InError))
    {
        return false;
    }
    for (std::uint32_t ScopedNodeIndex = 0; ScopedNodeIndex < MNodeCount; ++ScopedNodeIndex)
    {
        const MaterialNode& ScopedNode = MNodes[ScopedNodeIndex];
        for (std::uint32_t ScopedSlot = 0; ScopedSlot < ScopedNode.inputCount; ++ScopedSlot)
        {
            MaterialPin* ScopedPin = const_cast<MaterialPin*>(FindPin(*this, ScopedNode, ScopedSlot));
            if (ScopedPin == nullptr || !IsGraphEdge(ScopedNodeIndex, ScopedPin->sourceNode, MNodeCount))
            {
                continue;
            }
            const MaterialValueType ScopedSourceType = ScopedTypes[ScopedPin->sourceNode];
            if (ScopedSourceType < MaterialValueType::Count)
            {
                ScopedPin->type = ScopedSourceType;
            }
        }
    }
    return true;
}

// MaterialGraph - access
const MaterialNode& MaterialGraph::Node(std::uint16_t InIndex) const noexcept
{
    FBY_ASSERT(InIndex < MNodeCount);
    return MNodes[InIndex < kMaxMaterialNodes ? InIndex : 0];
}

MaterialNode& MaterialGraph::Node(std::uint16_t InIndex) noexcept
{
    FBY_ASSERT(InIndex < MNodeCount);
    return MNodes[InIndex < kMaxMaterialNodes ? InIndex : 0];
}

const MaterialPin& MaterialGraph::Pin(std::uint16_t InIndex) const noexcept
{
    FBY_ASSERT(InIndex < MPinCount);
    return MPins[InIndex < kMaxMaterialPins ? InIndex : 0];
}

MaterialPin& MaterialGraph::Pin(std::uint16_t InIndex) noexcept
{
    FBY_ASSERT(InIndex < MPinCount);
    return MPins[InIndex < kMaxMaterialPins ? InIndex : 0];
}

const MaterialParam& MaterialGraph::Param(std::uint16_t InIndex) const noexcept
{
    FBY_ASSERT(InIndex < MParamCount);
    return MParams[InIndex < kMaxMaterialParams ? InIndex : 0];
}

MaterialParam& MaterialGraph::Param(std::uint16_t InIndex) noexcept
{
    FBY_ASSERT(InIndex < MParamCount);
    return MParams[InIndex < kMaxMaterialParams ? InIndex : 0];
}

std::span<const MaterialPin> MaterialGraph::InputPins(std::uint16_t InNode) const noexcept
{
    if (InNode >= MNodeCount)
    {
        return {};
    }
    const MaterialNode& ScopedRecord = MNodes[InNode];
    if (static_cast<std::uint32_t>(ScopedRecord.firstInput) + ScopedRecord.inputCount > MPinCount)
    {
        return {};
    }
    return {MPins.data() + ScopedRecord.firstInput, ScopedRecord.inputCount};
}

std::uint16_t MaterialGraph::OutputNode() const noexcept
{
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < MNodeCount; ++ScopedIndex)
    {
        if (MNodes[ScopedIndex].kind == MaterialNodeKind::Output)
        {
            return static_cast<std::uint16_t>(ScopedIndex);
        }
    }
    return kInvalidMaterialNode;
}

bool MaterialGraph::UsesNodeKind(MaterialNodeKind InKind) const noexcept
{
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < MNodeCount; ++ScopedIndex)
    {
        if (MNodes[ScopedIndex].kind == InKind)
        {
            return true;
        }
    }
    return false;
}

MaterialGraphRequirements MaterialGraph::Requirements() const noexcept
{
    MaterialGraphRequirements ScopedRequirements;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < MNodeCount; ++ScopedIndex)
    {
        switch (MNodes[ScopedIndex].kind)
        {
            case MaterialNodeKind::Time:
                ScopedRequirements.needsTime = true;
                break;
            case MaterialNodeKind::Distortion:
                // The distortion phase is a function of the material clock even when
                ScopedRequirements.needsTime = true;
                break;
            case MaterialNodeKind::WorldPosition:
                ScopedRequirements.needsWorldPosition = true;
                break;
            case MaterialNodeKind::VertexPosition:
                ScopedRequirements.needsVertexStage = true;
                ScopedRequirements.needsWorldPosition = true;
                break;
            case MaterialNodeKind::Fresnel:
                ScopedRequirements.needsWorldPosition = true;
                ScopedRequirements.needsFragmentViewProjection = true;
                break;
            default:
                break;
        }
    }
    if (ScopedRequirements.needsWorldPosition)
    {
        ScopedRequirements.needsFragmentViewProjection = true;
    }
    return ScopedRequirements;
}

std::uint64_t MaterialGraph::Hash() const noexcept
{
    std::uint64_t ScopedHash = kHashOffsetBasis;
    HashU32(ScopedHash, MNodeCount);
    HashU32(ScopedHash, MPinCount);
    HashU32(ScopedHash, MParamCount);

    for (std::uint32_t ScopedIndex = 0; ScopedIndex < MNodeCount; ++ScopedIndex)
    {
        const MaterialNode& ScopedNode = MNodes[ScopedIndex];
        HashU16(ScopedHash, static_cast<std::uint16_t>(ScopedNode.kind));
        HashU16(ScopedHash, ScopedNode.firstInput);
        HashU16(ScopedHash, ScopedNode.inputCount);
        HashU16(ScopedHash, ScopedNode.paramIndex);
        HashU16(ScopedHash, ScopedNode.flags);
    // uiPosition is intentionally excluded: moving a node must not recompile.
    }
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < MPinCount; ++ScopedIndex)
    {
        const MaterialPin& ScopedPin = MPins[ScopedIndex];
        HashU16(ScopedHash, ScopedPin.node);
        HashByte(ScopedHash, ScopedPin.slot);
        HashByte(ScopedHash, static_cast<std::uint8_t>(ScopedPin.type));
        HashU16(ScopedHash, ScopedPin.sourceNode);
        HashByte(ScopedHash, ScopedPin.sourceSlot);
        HashByte(ScopedHash, ScopedPin.flags);
    }
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < MParamCount; ++ScopedIndex)
    {
        const MaterialParam& ScopedParam = MParams[ScopedIndex];
        HashByte(ScopedHash, static_cast<std::uint8_t>(ScopedParam.kind));
        HashFloat(ScopedHash, ScopedParam.X);
        HashFloat(ScopedHash, ScopedParam.Y);
        HashFloat(ScopedHash, ScopedParam.Z);
        HashFloat(ScopedHash, ScopedParam.W);
        HashU32(ScopedHash, ScopedParam.textureLayer);
    }
    return ScopedHash;
}

// MaterialGraph - validation
bool MaterialGraph::Validate(std::string* InError) const
{
    if (MNodeCount == 0)
    {
        SetError(InError, "material graph is empty");
        return false;
    }
    if (MNodeCount > kMaxMaterialNodes || MPinCount > kMaxMaterialPins || MParamCount > kMaxMaterialParams)
    {
        SetError(InError, "material graph exceeds a fixed capacity");
        return false;
    }

    // The Output count is checked before the per-node layout so a graph that was
    std::uint32_t ScopedOutputCount = 0;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < MNodeCount; ++ScopedIndex)
    {
        const MaterialNode& ScopedNode = MNodes[ScopedIndex];
        if (ScopedNode.kind >= MaterialNodeKind::Count)
        {
            SetError(InError, "material graph: node " + std::to_string(ScopedIndex) + " has an invalid kind");
            return false;
        }
        if (ScopedNode.kind == MaterialNodeKind::Output)
        {
            ++ScopedOutputCount;
        }
    }
    if (ScopedOutputCount != 1)
    {
        SetError(InError,
                 "material graph requires exactly one Output node (found " + std::to_string(ScopedOutputCount) + ")");
        return false;
    }

    for (std::uint32_t ScopedIndex = 0; ScopedIndex < MNodeCount; ++ScopedIndex)
    {
        const MaterialNode& ScopedNode = MNodes[ScopedIndex];
        const std::uint8_t ScopedExpectedInputs = MaterialNodeInputCount(ScopedNode.kind);
        if (ScopedNode.inputCount != ScopedExpectedInputs)
        {
            SetError(InError, "material graph: " + NodeLabel(ScopedIndex, ScopedNode.kind) + " has " +
                                  std::to_string(ScopedNode.inputCount) + " input slots, expected " +
                                  std::to_string(ScopedExpectedInputs));
            return false;
        }
        if (static_cast<std::uint32_t>(ScopedNode.firstInput) + ScopedNode.inputCount > MPinCount)
        {
            SetError(InError,
                     "material graph: " + NodeLabel(ScopedIndex, ScopedNode.kind) + " input pins are out of range");
            return false;
        }
        for (std::uint32_t ScopedSlot = 0; ScopedSlot < ScopedNode.inputCount; ++ScopedSlot)
        {
            const MaterialPin& ScopedPin = MPins[ScopedNode.firstInput + ScopedSlot];
            if (ScopedPin.node != ScopedIndex || ScopedPin.slot != ScopedSlot)
            {
                SetError(InError,
                         "material graph: pin ownership is corrupt around " + NodeLabel(ScopedIndex, ScopedNode.kind));
                return false;
            }
        }

        if (MaterialNodeUsesParam(ScopedNode.kind))
        {
            if (ScopedNode.paramIndex >= MParamCount)
            {
                SetError(InError,
                         "material graph: " + NodeLabel(ScopedIndex, ScopedNode.kind) + " has no constant payload");
                return false;
            }
            const MaterialParam& ScopedParam = MParams[ScopedNode.paramIndex];
            if (ScopedParam.kind != MaterialNodeParamKind(ScopedNode.kind))
            {
                SetError(InError, "material graph: " + NodeLabel(ScopedIndex, ScopedNode.kind) +
                                      " constant payload kind mismatch");
                return false;
            }
            if (ScopedParam.kind != MaterialParamKind::TextureLayer)
            {
                if (!std::isfinite(ScopedParam.X) || !std::isfinite(ScopedParam.Y) || !std::isfinite(ScopedParam.Z) ||
                    !std::isfinite(ScopedParam.W))
                {
                    SetError(InError, "material graph: " + NodeLabel(ScopedIndex, ScopedNode.kind) +
                                          " constant contains a non-finite value");
                    return false;
                }
            }
        }
        else if (ScopedNode.paramIndex != kInvalidMaterialParam)
        {
            SetError(InError,
                     "material graph: " + NodeLabel(ScopedIndex, ScopedNode.kind) + " owns a constant it cannot use");
            return false;
        }
    }

    // Resolving types also runs the topological sort, so a cycle is reported
    MaterialValueType ScopedTypes[kMaxMaterialNodes] = {};
    if (!ResolveTypes({ScopedTypes, kMaxMaterialNodes}, InError))
    {
        return false;
    }

    for (std::uint32_t ScopedIndex = 0; ScopedIndex < MNodeCount; ++ScopedIndex)
    {
        const MaterialNode& ScopedNode = MNodes[ScopedIndex];
        MaterialValueType ScopedInputTypes[kMaxMaterialInputs] = {};
        for (std::uint32_t ScopedSlot = 0; ScopedSlot < ScopedNode.inputCount; ++ScopedSlot)
        {
            const MaterialPin& ScopedPin = MPins[static_cast<std::uint32_t>(ScopedNode.firstInput) + ScopedSlot];
            ScopedInputTypes[ScopedSlot] = ScopedPin.type;
            if (ScopedPin.sourceNode == kInvalidMaterialNode)
            {
                if (!MaterialNodeSlotOptional(ScopedNode.kind, static_cast<std::uint8_t>(ScopedSlot)))
                {
                    SetError(InError, "material graph: " + NodeLabel(ScopedIndex, ScopedNode.kind) + " " +
                                          InputLabel(ScopedNode, ScopedSlot) + " is unconnected");
                    return false;
                }
                continue;
            }
            if (ScopedPin.sourceNode >= MNodeCount)
            {
                SetError(InError, "material graph: " + NodeLabel(ScopedIndex, ScopedNode.kind) + " " +
                                      InputLabel(ScopedNode, ScopedSlot) + " references node " +
                                      std::to_string(ScopedPin.sourceNode) + " which does not exist");
                return false;
            }
            if (ScopedPin.sourceNode == ScopedIndex)
            {
                SetError(InError, "material graph: " + NodeLabel(ScopedIndex, ScopedNode.kind) + " feeds itself");
                return false;
            }
            if (MNodes[ScopedPin.sourceNode].kind == MaterialNodeKind::Output)
            {
                SetError(InError, "material graph: the Output node is used as a source");
                return false;
            }
            const MaterialValueType ScopedSourceType = ScopedTypes[ScopedPin.sourceNode];
            if (ScopedSourceType >= MaterialValueType::Count)
            {
                SetError(InError, "material graph: " + NodeLabel(ScopedIndex, ScopedNode.kind) + " " +
                                      InputLabel(ScopedNode, ScopedSlot) + " is fed by a node with no output");
                return false;
            }
            if (!MaterialSlotAccepts(ScopedNode.kind, static_cast<std::uint8_t>(ScopedSlot), ScopedSourceType))
            {
                SetError(InError, "material graph: " + NodeLabel(ScopedIndex, ScopedNode.kind) + " " +
                                      InputLabel(ScopedNode, ScopedSlot) + " cannot accept " +
                                      MaterialValueTypeName(ScopedSourceType));
                return false;
            }
            ScopedInputTypes[ScopedSlot] = ScopedSourceType;
        }

        if (!MaterialNodeInputsCompatible(ScopedNode.kind,
                                          std::span<const MaterialValueType>(ScopedInputTypes, ScopedNode.inputCount)))
        {
            SetError(InError,
                     "material graph: " + NodeLabel(ScopedIndex, ScopedNode.kind) + " input types are incompatible");
            return false;
        }
    }

    // Required Output slots, reported last so structural problems win.
    const MaterialNode& ScopedOutput = Node(OutputNode());
    for (std::uint32_t ScopedSlot = 0; ScopedSlot < ScopedOutput.inputCount; ++ScopedSlot)
    {
        const MaterialPin& ScopedPin = MPins[static_cast<std::uint32_t>(ScopedOutput.firstInput) + ScopedSlot];
        if (ScopedPin.sourceNode == kInvalidMaterialNode &&
            !MaterialNodeSlotOptional(ScopedOutput.kind, static_cast<std::uint8_t>(ScopedSlot)))
        {
            SetError(InError, "material graph: the Output node is missing its BaseColor input");
            return false;
        }
    }
    return true;
}

// MaterialGraph - type resolution and evaluation order
bool MaterialGraph::ResolveTypes(std::span<MaterialValueType> InNodeTypes, std::string* InError) const
{
    if (InNodeTypes.size() < MNodeCount)
    {
        SetError(InError, "material graph: node type span is too small");
        return false;
    }
    return ResolveTypesImpl(*this, InNodeTypes.data(), InError);
}

std::uint32_t MaterialGraph::BuildEvaluationOrder(std::span<std::uint16_t> InOrder, std::string* InError) const
{
    const std::uint16_t ScopedOutput = OutputNode();
    if (ScopedOutput == kInvalidMaterialNode)
    {
        SetError(InError, "material graph has no Output node");
        return 0;
    }

    // Reachability from the Output: dead nodes never reach the shader.
    bool ScopedReachable[kMaxMaterialNodes] = {};
    std::uint16_t ScopedStack[kMaxMaterialNodes] = {};
    std::uint32_t ScopedStackSize = 0;
    ScopedReachable[ScopedOutput] = true;
    ScopedStack[ScopedStackSize] = ScopedOutput;
    ++ScopedStackSize;
    while (ScopedStackSize > 0)
    {
        --ScopedStackSize;
        const MaterialNode& ScopedNode = MNodes[ScopedStack[ScopedStackSize]];
        for (std::uint32_t ScopedSlot = 0; ScopedSlot < ScopedNode.inputCount; ++ScopedSlot)
        {
            const MaterialPin* ScopedPin = FindPin(*this, ScopedNode, ScopedSlot);
            if (ScopedPin == nullptr || !IsGraphEdge(ScopedStack[ScopedStackSize], ScopedPin->sourceNode, MNodeCount) ||
                ScopedReachable[ScopedPin->sourceNode])
            {
                continue;
            }
            ScopedReachable[ScopedPin->sourceNode] = true;
            ScopedStack[ScopedStackSize] = ScopedPin->sourceNode;
            ++ScopedStackSize;
        }
    }

    std::uint32_t ScopedReachableCount = 0;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < MNodeCount; ++ScopedIndex)
    {
        if (ScopedReachable[ScopedIndex])
        {
            ++ScopedReachableCount;
        }
    }
    if (InOrder.size() < ScopedReachableCount)
    {
        SetError(InError, "material graph: evaluation order span is too small");
        return 0;
    }

    std::uint16_t ScopedIndegree[kMaxMaterialNodes] = {};
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < MNodeCount; ++ScopedIndex)
    {
        if (!ScopedReachable[ScopedIndex])
        {
            continue;
        }
        const MaterialNode& ScopedNode = MNodes[ScopedIndex];
        for (std::uint32_t ScopedSlot = 0; ScopedSlot < ScopedNode.inputCount; ++ScopedSlot)
        {
            const MaterialPin* ScopedPin = FindPin(*this, ScopedNode, ScopedSlot);
            if (ScopedPin != nullptr && IsGraphEdge(ScopedIndex, ScopedPin->sourceNode, MNodeCount))
            {
                ++ScopedIndegree[ScopedIndex];
            }
        }
    }

    bool ScopedEmitted[kMaxMaterialNodes] = {};
    std::uint32_t ScopedCount = 0;
    while (ScopedCount < ScopedReachableCount)
    {
        std::uint32_t ScopedReady = MNodeCount;
        for (std::uint32_t ScopedCandidate = 0; ScopedCandidate < MNodeCount; ++ScopedCandidate)
        {
            if (ScopedReachable[ScopedCandidate] && !ScopedEmitted[ScopedCandidate] &&
                ScopedIndegree[ScopedCandidate] == 0)
            {
                ScopedReady = ScopedCandidate;
                break;
            }
        }
        if (ScopedReady == MNodeCount)
        {
            SetError(InError, "material graph contains a cycle");
            return 0;
        }
        InOrder[ScopedCount] = static_cast<std::uint16_t>(ScopedReady);
        ++ScopedCount;
        ScopedEmitted[ScopedReady] = true;
        for (std::uint32_t ScopedConsumer = 0; ScopedConsumer < MNodeCount; ++ScopedConsumer)
        {
            if (!ScopedReachable[ScopedConsumer] || ScopedEmitted[ScopedConsumer])
            {
                continue;
            }
            const MaterialNode& ScopedNode = MNodes[ScopedConsumer];
            for (std::uint32_t ScopedSlot = 0; ScopedSlot < ScopedNode.inputCount; ++ScopedSlot)
            {
                const MaterialPin* ScopedPin = FindPin(*this, ScopedNode, ScopedSlot);
                if (ScopedPin != nullptr && ScopedPin->sourceNode == static_cast<std::uint16_t>(ScopedReady))
                {
                    --ScopedIndegree[ScopedConsumer];
                }
            }
        }
    }
    return ScopedCount;
}

// MaterialGraphCompiler
namespace
{

// Bounded GLSL writer.
class GlslWriter
{
public:
    explicit GlslWriter(std::span<char> InBuffer) noexcept
        : MData(InBuffer.data()), MCapacity(InBuffer.empty() ? 0 : InBuffer.size() - 1), MRequired(0)
    {
    }

    void Append(std::string_view InText) noexcept
    {
        const std::size_t ScopedBegin = MRequired;
        MRequired += InText.size();
        if (ScopedBegin >= MCapacity || MData == nullptr)
        {
            return;
        }
        const std::size_t ScopedWritable = std::min(InText.size(), MCapacity - ScopedBegin);
        if (ScopedWritable > 0)
        {
            std::memcpy(MData + ScopedBegin, InText.data(), ScopedWritable);
        }
    }

    void AppendU32(std::uint32_t InValue) noexcept
    {
        char ScopedDigits[16] = {};
        const auto ScopedResult = std::to_chars(ScopedDigits, ScopedDigits + sizeof(ScopedDigits), InValue);
        Append(std::string_view(ScopedDigits, static_cast<std::size_t>(ScopedResult.ptr - ScopedDigits)));
    }

    void AppendFloat(float InValue) noexcept
    {
        char ScopedDigits[48] = {};
        const auto ScopedResult = std::to_chars(ScopedDigits, ScopedDigits + sizeof(ScopedDigits), InValue);
        std::size_t ScopedLength = static_cast<std::size_t>(ScopedResult.ptr - ScopedDigits);
        // GLSL needs a float literal; std::to_chars writes "1" for 1.0f.
        if (ScopedLength > 0 &&
            std::string_view(ScopedDigits, ScopedLength).find_first_of(".eE") == std::string_view::npos)
        {
            ScopedDigits[ScopedLength] = '.';
            ++ScopedLength;
            ScopedDigits[ScopedLength] = '0';
            ++ScopedLength;
        }
        Append(std::string_view(ScopedDigits, ScopedLength));
    }

    void AppendHexU64(std::uint64_t InValue) noexcept
    {
        char ScopedDigits[24] = {};
        const auto ScopedResult = std::to_chars(ScopedDigits, ScopedDigits + sizeof(ScopedDigits), InValue, 16);
        Append(std::string_view(ScopedDigits, static_cast<std::size_t>(ScopedResult.ptr - ScopedDigits)));
    }

    [[nodiscard]] bool Overflowed() const noexcept
    {
        return MRequired > MCapacity;
    }
    [[nodiscard]] std::size_t Required() const noexcept
    {
        return MRequired;
    }
    [[nodiscard]] std::size_t Capacity() const noexcept
    {
        return MCapacity;
    }

    // NUL-terminates when the text fit; returns the byte count without the NUL.
    std::size_t Finish() noexcept
    {
        if (MData != nullptr && MCapacity > 0)
        {
            MData[std::min(MRequired, MCapacity)] = '\0';
        }
        return MRequired;
    }

private:
    char* MData = nullptr;
    // Usable bytes, excluding the NUL slot
    std::size_t MCapacity = 0;
    std::size_t MRequired = 0;
};

// Everything the emitter needs to know about one compiled graph.
struct CompileContext
{
    const MaterialGraph& graph;
    MaterialCompileOptions options;
    MaterialValueType nodeTypes[kMaxMaterialNodes] = {};
    std::uint16_t order[kMaxMaterialNodes] = {};
    std::uint32_t orderCount = 0;

    bool usesTime = false;
    bool usesWorldPositionNode = false;
    bool usesVertexPositionNode = false;
    bool usesFresnel = false;

    bool generatedVertexStage = false;
    bool needsWorldPosition = false;
    bool worldViaVarying = false;
    bool needsInverse = false;
    bool fragmentNeedsMaterialParams = false;
    bool fragmentNeedsViewProjection = false;
    // True whenever the fragment declares the push-constant block: either it
    bool fragmentNeedsPushConstant = false;
    bool vertexNeedsMaterialParams = false;

    CompileContext(const MaterialGraph& InGraphRef, const MaterialCompileOptions& InCompileOptions)
        : graph(InGraphRef), options(InCompileOptions)
    {
    }
};

void AnalyzeGraph(CompileContext& InContext)
{
    for (std::uint32_t ScopedScheduled = 0; ScopedScheduled < InContext.orderCount; ++ScopedScheduled)
    {
        switch (InContext.graph.Node(InContext.order[ScopedScheduled]).kind)
        {
            case MaterialNodeKind::Time:
                InContext.usesTime = true;
                break;
            case MaterialNodeKind::Distortion:
                InContext.usesTime = true;
                break;
            case MaterialNodeKind::WorldPosition:
                InContext.usesWorldPositionNode = true;
                break;
            case MaterialNodeKind::VertexPosition:
                InContext.usesVertexPositionNode = true;
                break;
            case MaterialNodeKind::Fresnel:
                InContext.usesFresnel = true;
                break;
            default:
                break;
        }
    }

    InContext.generatedVertexStage = InContext.options.generateVertexStage;
    InContext.needsWorldPosition = InContext.usesWorldPositionNode || InContext.usesFresnel;
    // The sprite domain reads the engine quad varying at location 3 directly and
    // has no view-projection in its push block, so it needs no inverse matrix.
    const bool ScopedSpriteDomain = InContext.options.domain == MaterialDomain::Sprite;
    InContext.worldViaVarying = InContext.generatedVertexStage && InContext.needsWorldPosition;
    InContext.needsInverse =
        (InContext.needsWorldPosition && !InContext.worldViaVarying && !ScopedSpriteDomain) || InContext.usesFresnel;
    InContext.fragmentNeedsMaterialParams =
        InContext.usesTime || (InContext.needsWorldPosition && !InContext.worldViaVarying && !ScopedSpriteDomain);
    InContext.fragmentNeedsViewProjection = InContext.needsInverse;
    InContext.fragmentNeedsPushConstant = InContext.fragmentNeedsMaterialParams || InContext.needsInverse;
    InContext.vertexNeedsMaterialParams = InContext.generatedVertexStage && InContext.fragmentNeedsMaterialParams;
}

// Small emission helpers
[[nodiscard]] bool PinConnected(const CompileContext& InContext, std::uint16_t InNodeIndex,
                                std::uint8_t InSlot) noexcept
{
    const MaterialNode& ScopedNode = InContext.graph.Node(InNodeIndex);
    const MaterialPin* ScopedPin = FindPin(InContext.graph, ScopedNode, InSlot);
    return ScopedPin != nullptr && IsGraphEdge(InNodeIndex, ScopedPin->sourceNode, InContext.graph.NodeCount());
}

[[nodiscard]] MaterialPin const* ConnectedPin(const CompileContext& InContext, std::uint16_t InNodeIndex,
                                              std::uint8_t InSlot) noexcept
{
    const MaterialNode& ScopedNode = InContext.graph.Node(InNodeIndex);
    return FindPin(InContext.graph, ScopedNode, InSlot);
}

[[nodiscard]] MaterialValueType InputType(const CompileContext& InContext, std::uint16_t InNodeIndex,
                                          std::uint8_t InSlot) noexcept
{
    return ResolvedInputType(InContext.graph, InContext.nodeTypes, InNodeIndex, InSlot);
}

void AppendInputReference(GlslWriter& Out, const CompileContext& InContext, std::uint16_t InNodeIndex,
                          std::uint8_t InSlot)
{
    if (PinConnected(InContext, InNodeIndex, InSlot))
    {
        Out.Append("n");
        Out.AppendU32(ConnectedPin(InContext, InNodeIndex, InSlot)->sourceNode);
        return;
    }
    // Documented defaults of the optional slots (required slots are guaranteed
    const MaterialNodeKind ScopedKind = InContext.graph.Node(InNodeIndex).kind;
    switch (ScopedKind)
    {
        case MaterialNodeKind::TextureSample:
            Out.Append("vUv");
            return;
        case MaterialNodeKind::Fresnel:
            Out.Append("5.0");
            return;
        default:
            break;
    }
    Out.Append("0.0");
}

void AppendNodeExpression(GlslWriter& Out, const CompileContext& InContext, std::uint16_t InNodeIndex)
{
    const MaterialGraph& ScopedGraph = InContext.graph;
    const MaterialNode& ScopedNode = ScopedGraph.Node(InNodeIndex);
    const MaterialValueType ScopedOutputType = InContext.nodeTypes[InNodeIndex];

    switch (ScopedNode.kind)
    {
        case MaterialNodeKind::Uv:
            Out.Append("vUv");
            return;
        case MaterialNodeKind::VertexColor:
            Out.Append("vColor");
            return;
        case MaterialNodeKind::Time:
            Out.Append("fbyTime");
            return;
        case MaterialNodeKind::WorldPosition:
            Out.Append("fbyWorldPosition");
            return;
        case MaterialNodeKind::VertexPosition:
            Out.Append("vVertexPosition");
            return;
        case MaterialNodeKind::Scalar:
            Out.AppendFloat(ScopedGraph.Param(ScopedNode.paramIndex).X);
            return;
        case MaterialNodeKind::Vector2D:
        {
            const MaterialParam& ScopedParam = ScopedGraph.Param(ScopedNode.paramIndex);
            Out.Append("vec2(");
            Out.AppendFloat(ScopedParam.X);
            Out.Append(", ");
            Out.AppendFloat(ScopedParam.Y);
            Out.Append(")");
            return;
        }
        case MaterialNodeKind::Vector3D:
        {
            const MaterialParam& ScopedParam = ScopedGraph.Param(ScopedNode.paramIndex);
            Out.Append("vec3(");
            Out.AppendFloat(ScopedParam.X);
            Out.Append(", ");
            Out.AppendFloat(ScopedParam.Y);
            Out.Append(", ");
            Out.AppendFloat(ScopedParam.Z);
            Out.Append(")");
            return;
        }
        case MaterialNodeKind::Color:
        {
            const MaterialParam& ScopedParam = ScopedGraph.Param(ScopedNode.paramIndex);
            Out.Append("vec4(");
            Out.AppendFloat(ScopedParam.X);
            Out.Append(", ");
            Out.AppendFloat(ScopedParam.Y);
            Out.Append(", ");
            Out.AppendFloat(ScopedParam.Z);
            Out.Append(", ");
            Out.AppendFloat(ScopedParam.W);
            Out.Append(")");
            return;
        }
        case MaterialNodeKind::TextureSample:
        {
            const MaterialParam& ScopedParam = ScopedGraph.Param(ScopedNode.paramIndex);
            Out.Append("texture(uAtlas, vec3(");
            AppendInputReference(Out, InContext, InNodeIndex, 0);
            Out.Append(", ");
            if (InContext.options.domain == MaterialDomain::Sprite)
            {
                // A sprite material follows its item's atlas slice: the authored
                // layer parameter only applies to the mesh domain.
                Out.Append("float(vLayer)");
            }
            else
            {
                Out.AppendFloat(static_cast<float>(ScopedParam.textureLayer));
            }
            Out.Append("))");
            return;
        }
        case MaterialNodeKind::Add:
        case MaterialNodeKind::Sub:
        case MaterialNodeKind::Mul:
        case MaterialNodeKind::Div:
        {
            const char* ScopedSymbol = ScopedNode.kind == MaterialNodeKind::Add   ? " + "
                                       : ScopedNode.kind == MaterialNodeKind::Sub ? " - "
                                       : ScopedNode.kind == MaterialNodeKind::Mul ? " * "
                                                                                  : " / ";
            Out.Append("(");
            AppendInputReference(Out, InContext, InNodeIndex, 0);
            Out.Append(ScopedSymbol);
            AppendInputReference(Out, InContext, InNodeIndex, 1);
            Out.Append(")");
            return;
        }
        case MaterialNodeKind::Lerp:
            Out.Append("mix(");
            AppendInputReference(Out, InContext, InNodeIndex, 0);
            Out.Append(", ");
            AppendInputReference(Out, InContext, InNodeIndex, 1);
            Out.Append(", ");
            AppendInputReference(Out, InContext, InNodeIndex, 2);
            Out.Append(")");
            return;
        case MaterialNodeKind::Saturate:
            Out.Append("clamp(");
            AppendInputReference(Out, InContext, InNodeIndex, 0);
            Out.Append(", 0.0, 1.0)");
            return;
        case MaterialNodeKind::Clamp:
            Out.Append("clamp(");
            AppendInputReference(Out, InContext, InNodeIndex, 0);
            Out.Append(", ");
            AppendInputReference(Out, InContext, InNodeIndex, 1);
            Out.Append(", ");
            AppendInputReference(Out, InContext, InNodeIndex, 2);
            Out.Append(")");
            return;
        case MaterialNodeKind::Power:
        {
            // GLSL has no (vecN, float) pow overload; widen the scalar exponent.
            Out.Append("pow(");
            AppendInputReference(Out, InContext, InNodeIndex, 0);
            Out.Append(", ");
            const MaterialValueType ScopedExponentType = InputType(InContext, InNodeIndex, 1);
            if (ScopedOutputType != MaterialValueType::Float && ScopedExponentType == MaterialValueType::Float)
            {
                Out.Append(MaterialValueTypeGlsl(ScopedOutputType));
                Out.Append("(");
                AppendInputReference(Out, InContext, InNodeIndex, 1);
                Out.Append(")");
            }
            else
            {
                AppendInputReference(Out, InContext, InNodeIndex, 1);
            }
            Out.Append(")");
            return;
        }
        case MaterialNodeKind::Sin:
            Out.Append("sin(");
            AppendInputReference(Out, InContext, InNodeIndex, 0);
            Out.Append(")");
            return;
        case MaterialNodeKind::Cos:
            Out.Append("cos(");
            AppendInputReference(Out, InContext, InNodeIndex, 0);
            Out.Append(")");
            return;
        case MaterialNodeKind::OneMinus:
            Out.Append("(1.0 - ");
            AppendInputReference(Out, InContext, InNodeIndex, 0);
            Out.Append(")");
            return;
        case MaterialNodeKind::Dot:
            Out.Append("dot(");
            AppendInputReference(Out, InContext, InNodeIndex, 0);
            Out.Append(", ");
            AppendInputReference(Out, InContext, InNodeIndex, 1);
            Out.Append(")");
            return;
        case MaterialNodeKind::Fresnel:
            Out.Append("fbyFresnel(fbyWorldPosition, ");
            AppendInputReference(Out, InContext, InNodeIndex, 0);
            Out.Append(", fbyInverseViewProjection)");
            return;
        case MaterialNodeKind::Tilt25D:
        {
            // uv + slope * height: a slope in UV units per unit of authored height.
            Out.Append("(");
            if (PinConnected(InContext, InNodeIndex, 0))
            {
                AppendInputReference(Out, InContext, InNodeIndex, 0);
            }
            else
            {
                Out.Append("vUv");
            }
            Out.Append(" + ");
            if (PinConnected(InContext, InNodeIndex, 1))
            {
                AppendInputReference(Out, InContext, InNodeIndex, 1);
            }
            else
            {
                const MaterialParam& ScopedParam = ScopedGraph.Param(ScopedNode.paramIndex);
                Out.Append("vec2(");
                Out.AppendFloat(ScopedParam.X);
                Out.Append(", ");
                Out.AppendFloat(ScopedParam.Y);
                Out.Append(")");
            }
            Out.Append(" * ");
            if (PinConnected(InContext, InNodeIndex, 2))
            {
                AppendInputReference(Out, InContext, InNodeIndex, 2);
            }
            else
            {
                Out.AppendFloat(ScopedGraph.Param(ScopedNode.paramIndex).Z);
            }
            Out.Append(")");
            return;
        }
        case MaterialNodeKind::Distortion:
        {
            // Deterministic animated warp. The phase is a pure function of UV and
            const MaterialParam& ScopedParam = ScopedGraph.Param(ScopedNode.paramIndex);
            Out.Append("(");
            if (PinConnected(InContext, InNodeIndex, 0))
            {
                AppendInputReference(Out, InContext, InNodeIndex, 0);
            }
            else
            {
                Out.Append("vUv");
            }
            Out.Append(" + vec2(sin(");
            if (PinConnected(InContext, InNodeIndex, 0))
            {
                AppendInputReference(Out, InContext, InNodeIndex, 0);
            }
            else
            {
                Out.Append("vUv");
            }
            Out.Append(".y * ");
            Out.AppendFloat(ScopedParam.Y);
            Out.Append(" + fbyTime), cos(");
            if (PinConnected(InContext, InNodeIndex, 0))
            {
                AppendInputReference(Out, InContext, InNodeIndex, 0);
            }
            else
            {
                Out.Append("vUv");
            }
            Out.Append(".x * ");
            Out.AppendFloat(ScopedParam.Y);
            Out.Append(" + fbyTime)) * ");
            if (PinConnected(InContext, InNodeIndex, 1))
            {
                AppendInputReference(Out, InContext, InNodeIndex, 1);
            }
            else
            {
                Out.AppendFloat(ScopedParam.X);
            }
            Out.Append(")");
            return;
        }
        case MaterialNodeKind::Output:
            break;
        case MaterialNodeKind::Count:
            break;
    }
    Out.Append("0.0");
}

// Output node inputs: BaseColor (vec3/vec4/float), Opacity (float), Emissive
void AppendOutputBaseColor(GlslWriter& Out, const CompileContext& InContext, std::uint16_t OutputNode)
{
    if (!PinConnected(InContext, OutputNode, 0))
    {
        Out.Append("vec4(1.0)");
        return;
    }
    const MaterialValueType ScopedType = InputType(InContext, OutputNode, 0);
    switch (ScopedType)
    {
        case MaterialValueType::Vector4D:
            AppendInputReference(Out, InContext, OutputNode, 0);
            return;
        case MaterialValueType::Vector3D:
            Out.Append("vec4(");
            AppendInputReference(Out, InContext, OutputNode, 0);
            Out.Append(", 1.0)");
            return;
        default:
            Out.Append("vec4(vec3(");
            AppendInputReference(Out, InContext, OutputNode, 0);
            Out.Append("), 1.0)");
            return;
    }
}

void AppendOutputEmissive(GlslWriter& Out, const CompileContext& InContext, std::uint16_t OutputNode)
{
    if (!PinConnected(InContext, OutputNode, 2))
    {
        Out.Append("vec3(0.0)");
        return;
    }
    const MaterialValueType ScopedType = InputType(InContext, OutputNode, 2);
    switch (ScopedType)
    {
        case MaterialValueType::Vector4D:
            AppendInputReference(Out, InContext, OutputNode, 2);
            Out.Append(".rgb");
            return;
        case MaterialValueType::Vector3D:
            AppendInputReference(Out, InContext, OutputNode, 2);
            return;
        default:
            Out.Append("vec3(");
            AppendInputReference(Out, InContext, OutputNode, 2);
            Out.Append(")");
            return;
    }
}

void AppendOutputOpacity(GlslWriter& Out, const CompileContext& InContext, std::uint16_t OutputNode)
{
    if (!PinConnected(InContext, OutputNode, 1))
    {
        Out.Append("1.0");
        return;
    }
    AppendInputReference(Out, InContext, OutputNode, 1);
}

// Generated source headers
void EmitPreamble(GlslWriter& Out, const CompileContext& InContext, const char* InStage)
{
    Out.Append("#version 450\n");
    Out.Append("// FemboyEngine2D - generated material ");
    Out.Append(InStage);
    Out.Append(" shader.\n");
    Out.Append("// Graph hash: 0x");
    Out.AppendHexU64(InContext.graph.Hash());
    Out.Append("\n");
    if (InContext.options.debugName != nullptr)
    {
        Out.Append("// Debug name: ");
        std::size_t ScopedWritten = 0;
        for (const char* ScopedCharacter = InContext.options.debugName; *ScopedCharacter != '\0' && ScopedWritten < 96;
             ++ScopedCharacter, ++ScopedWritten)
        {
            // Keep the label on one comment line whatever the editor passed in.
            if (*ScopedCharacter == '\n' || *ScopedCharacter == '\r')
            {
                Out.Append(" ");
            }
            else
            {
                Out.Append(std::string_view(ScopedCharacter, 1));
            }
        }
        Out.Append("\n");
    }
    Out.Append("// Generated by Fby::Render::MaterialGraphCompiler. Do not edit.\n");
    Out.Append("//\n");
    Out.Append("// Engine contract: the built-in mesh pass provides the vertex stage unless\n");
    Out.Append("// this material also ships a generated one.\n");
    Out.Append("\n");
}

void EmitVertexShader(GlslWriter& Out, const CompileContext& InContext)
{
    EmitPreamble(Out, InContext, "vertex");
    Out.Append("// Engine mesh vertex contract (engine/shaders/mesh.vert). No vertex input\n");
    Out.Append("// binding: gl_InstanceIndex selects a full MeshDrawData record, so every mesh\n");
    Out.Append("// in one indirect multi-draw carries its own mat4 model.\n");
    Out.Append("struct SkinnedVertex\n");
    Out.Append("{\n");
    Out.Append("    vec4 position;\n");
    Out.Append("    vec2 uv;\n");
    Out.Append("    uint color;\n");
    Out.Append("    uint padding;\n");
    Out.Append("};\n");
    Out.Append("\n");
    Out.Append("struct MeshDrawData\n");
    Out.Append("{\n");
    Out.Append("    mat4 model;\n");
    Out.Append("    uint vertexBase;\n");
    Out.Append("    uint layer;\n");
    Out.Append("    uint flags;\n");
    Out.Append("    uint padding;\n");
    Out.Append("};\n");
    Out.Append("\n");
    Out.Append("layout(set = 1, binding = 0) readonly buffer SkinnedVertices\n");
    Out.Append("{\n");
    Out.Append("    SkinnedVertex vertices[];\n");
    Out.Append("} skinned;\n");
    Out.Append("\n");
    Out.Append("layout(set = 1, binding = 1) readonly buffer StaticVertices\n");
    Out.Append("{\n");
    Out.Append("    SkinnedVertex vertices[];\n");
    Out.Append("} staticVertices;\n");
    Out.Append("\n");
    Out.Append("layout(set = 1, binding = 2) readonly buffer DrawData\n");
    Out.Append("{\n");
    Out.Append("    MeshDrawData draws[];\n");
    Out.Append("} drawData;\n");
    Out.Append("\n");
    Out.Append("layout(push_constant) uniform Push\n");
    Out.Append("{\n");
    Out.Append("    mat4 viewProjection;\n");
    if (InContext.vertexNeedsMaterialParams)
    {
        Out.Append("    // Material extension: x = time seconds, yz = viewport size.\n");
        Out.Append("    vec4 materialParams;\n");
    }
    Out.Append("} pc;\n");
    Out.Append("\n");
    Out.Append("layout(location = 0) out vec2 vUv;\n");
    Out.Append("layout(location = 1) out vec4 vColor;\n");
    Out.Append("layout(location = 2) flat out uint vLayer;\n");
    if (InContext.needsWorldPosition)
    {
        Out.Append("// World position after the model and view-projection w divide.\n");
        Out.Append("layout(location = 3) out vec3 vWorldPosition;\n");
    }
    if (InContext.usesVertexPositionNode)
    {
        Out.Append("// Model-space (bind-pose) position of the shaded vertex.\n");
        Out.Append("layout(location = 4) out vec3 vVertexPosition;\n");
    }
    Out.Append("\n");
    Out.Append("void main()\n");
    Out.Append("{\n");
    Out.Append("    // Vulkan defines gl_InstanceIndex as firstInstance + instance. The RHI sets\n");
    Out.Append("    // firstInstance to the draw record index and instanceCount to 1.\n");
    Out.Append("    const uint drawIndex = uint(gl_InstanceIndex);\n");
    Out.Append("    const MeshDrawData draw = drawData.draws[drawIndex];\n");
    Out.Append("    const uint vertexIndex = draw.vertexBase + uint(gl_VertexIndex);\n");
    Out.Append("\n");
    Out.Append("    SkinnedVertex vertex;\n");
    Out.Append("    if ((draw.flags & 1u) != 0u)\n");
    Out.Append("    {\n");
    Out.Append("        vertex = staticVertices.vertices[vertexIndex];\n");
    Out.Append("    }\n");
    Out.Append("    else\n");
    Out.Append("    {\n");
    Out.Append("        vertex = skinned.vertices[vertexIndex];\n");
    Out.Append("    }\n");
    Out.Append("\n");
    Out.Append("    // Full 4x4 model, then full 4x4 view-projection. The homogeneous w is\n");
    Out.Append("    // preserved, so tilted 2.5D planes get a real perspective divide.\n");
    Out.Append("    const vec4 world = draw.model * vertex.position;\n");
    Out.Append("    gl_Position = pc.viewProjection * world;\n");
    Out.Append("\n");
    Out.Append("    vUv = vertex.uv;\n");
    Out.Append("    vColor = unpackUnorm4x8(vertex.color);\n");
    Out.Append("    vLayer = draw.layer;\n");
    if (InContext.needsWorldPosition)
    {
        Out.Append("    // Interpolated linearly in world space; for the 2.5D plane case the w\n");
        Out.Append("    // divide is exact and matches the fragment-side reconstruction.\n");
        Out.Append("    vWorldPosition = world.xyz / world.w;\n");
    }
    if (InContext.usesVertexPositionNode)
    {
        Out.Append("    vVertexPosition = vertex.position.xyz / vertex.position.w;\n");
    }
    Out.Append("}\n");
}

void EmitInverseHelper(GlslWriter& Out)
{
    Out.Append("// Inverse of a column-major mat4 by Gauss-Jordan elimination with partial\n");
    Out.Append("// pivoting. The engine uploads only viewProjection, so a fragment-only material\n");
    Out.Append("// inverts it here to recover world position (and a view reference point).\n");
    Out.Append("mat4 fbyInverseMat4(mat4 m)\n");
    Out.Append("{\n");
    Out.Append("    mat4 a = m;\n");
    Out.Append("    mat4 inverse = mat4(1.0);\n");
    Out.Append("    for (int column = 0; column < 4; ++column)\n");
    Out.Append("    {\n");
    Out.Append("        int pivot = column;\n");
    Out.Append("        float best = abs(a[column][column]);\n");
    Out.Append("        for (int row = column + 1; row < 4; ++row)\n");
    Out.Append("        {\n");
    Out.Append("            const float candidate = abs(a[column][row]);\n");
    Out.Append("            if (candidate > best)\n");
    Out.Append("            {\n");
    Out.Append("                best = candidate;\n");
    Out.Append("                pivot = row;\n");
    Out.Append("            }\n");
    Out.Append("        }\n");
    Out.Append("        if (best < 1.0e-8)\n");
    Out.Append("        {\n");
    Out.Append("            return mat4(1.0);\n");
    Out.Append("        }\n");
    Out.Append("        if (pivot != column)\n");
    Out.Append("        {\n");
    Out.Append("            for (int k = 0; k < 4; ++k)\n");
    Out.Append("            {\n");
    Out.Append("                const float swapLeft = a[k][column];\n");
    Out.Append("                a[k][column] = a[k][pivot];\n");
    Out.Append("                a[k][pivot] = swapLeft;\n");
    Out.Append("                const float swapRight = inverse[k][column];\n");
    Out.Append("                inverse[k][column] = inverse[k][pivot];\n");
    Out.Append("                inverse[k][pivot] = swapRight;\n");
    Out.Append("            }\n");
    Out.Append("        }\n");
    Out.Append("        const float scale = 1.0 / a[column][column];\n");
    Out.Append("        for (int k = 0; k < 4; ++k)\n");
    Out.Append("        {\n");
    Out.Append("            a[k][column] *= scale;\n");
    Out.Append("            inverse[k][column] *= scale;\n");
    Out.Append("        }\n");
    Out.Append("        for (int row = 0; row < 4; ++row)\n");
    Out.Append("        {\n");
    Out.Append("            if (row == column)\n");
    Out.Append("            {\n");
    Out.Append("                continue;\n");
    Out.Append("            }\n");
    Out.Append("            const float factor = a[column][row];\n");
    Out.Append("            if (factor == 0.0)\n");
    Out.Append("            {\n");
    Out.Append("                continue;\n");
    Out.Append("            }\n");
    Out.Append("            for (int k = 0; k < 4; ++k)\n");
    Out.Append("            {\n");
    Out.Append("                a[k][row] -= a[k][column] * factor;\n");
    Out.Append("                inverse[k][row] -= inverse[k][column] * factor;\n");
    Out.Append("            }\n");
    Out.Append("        }\n");
    Out.Append("    }\n");
    Out.Append("    return inverse;\n");
    Out.Append("}\n");
    Out.Append("\n");
}

void EmitWorldPositionHelper(GlslWriter& Out)
{
    Out.Append("// Reconstructs world position from the fragment's depth and the inverse\n");
    Out.Append("// view-projection. pc.materialParams.yz must carry the viewport extent in\n");
    Out.Append("// pixels (material push-constant extension, see MaterialGraphCompiler.hpp).\n");
    Out.Append("vec3 fbyReconstructWorldPosition(mat4 inverseViewProjection)\n");
    Out.Append("{\n");
    Out.Append("    const vec2 ndc = gl_FragCoord.xy / pc.materialParams.yz * 2.0 - 1.0;\n");
    Out.Append("    const vec4 clip = vec4(ndc, gl_FragCoord.z, 1.0);\n");
    Out.Append("    const vec4 world = inverseViewProjection * clip;\n");
    Out.Append("    return world.xyz / world.w;\n");
    Out.Append("}\n");
    Out.Append("\n");
}

void EmitFresnelHelper(GlslWriter& Out)
{
    Out.Append("// Stylised Fresnel rim term. The engine mesh contract carries no normal and\n");
    Out.Append("// no camera position, so the geometric normal comes from the screen-space\n");
    Out.Append("// derivatives of world position and the view direction from the near-plane\n");
    Out.Append("// centre recovered from the inverse view-projection. This is an art-directed\n");
    Out.Append("// rim, not a physical BRDF.\n");
    Out.Append("float fbyFresnel(vec3 worldPosition, float power, mat4 inverseViewProjection)\n");
    Out.Append("{\n");
    Out.Append("    const vec4 nearCentre = inverseViewProjection * vec4(0.0, 0.0, 0.0, 1.0);\n");
    Out.Append("    const vec3 viewDirection = normalize(worldPosition - nearCentre.xyz / nearCentre.w);\n");
    Out.Append("    const vec3 normal = normalize(cross(dFdx(worldPosition), dFdy(worldPosition)));\n");
    Out.Append("    return pow(clamp(1.0 - abs(dot(normal, viewDirection)), 0.0, 1.0), max(power, 0.001));\n");
    Out.Append("}\n");
    Out.Append("\n");
}

void EmitFragmentShader(GlslWriter& Out, const CompileContext& InContext)
{
    const bool ScopedSpriteDomain = InContext.options.domain == MaterialDomain::Sprite;
    EmitPreamble(Out, InContext, "fragment");
    if (ScopedSpriteDomain)
    {
        Out.Append("// Fragment interface, identical to the engine quad.vert varyings.\n");
    }
    else
    {
        Out.Append("// Fragment interface, identical to engine/shaders/mesh.frag.\n");
    }
    Out.Append("layout(location = 0) in vec2 vUv;\n");
    Out.Append("layout(location = 1) in vec4 vColor;\n");
    Out.Append("layout(location = 2) flat in uint vLayer;\n");
    Out.Append("\n");
    Out.Append("layout(set = 0, binding = 0) uniform sampler2DArray uAtlas;\n");
    Out.Append("\n");
    if (ScopedSpriteDomain)
    {
        if (InContext.needsWorldPosition)
        {
            Out.Append("// Engine quad varying (location 3): world-space position.\n");
            Out.Append("layout(location = 3) in vec2 vWorld;\n");
            Out.Append("\n");
        }
    }
    else
    {
        if (InContext.worldViaVarying)
        {
            Out.Append("// Generated vertex stage varying (location 3).\n");
            Out.Append("layout(location = 3) in vec3 vWorldPosition;\n");
            Out.Append("\n");
        }
        if (InContext.usesVertexPositionNode)
        {
            Out.Append("// Generated vertex stage varying (location 4).\n");
            Out.Append("layout(location = 4) in vec3 vVertexPosition;\n");
            Out.Append("\n");
        }
    }
    if (InContext.fragmentNeedsPushConstant)
    {
        if (ScopedSpriteDomain)
        {
            // The sprite layout is the quad.vert camera block: offsets 0..63 are
            // the camera, 64 is the feedback slot and 80 is the material clock.
            Out.Append("// Sprite camera block, declared in full so every member offset\n");
            Out.Append("// matches the sprite pipeline layout.\n");
            Out.Append("layout(push_constant) uniform Camera\n");
            Out.Append("{\n");
            Out.Append("    vec4 viewportAndZoom;\n");
            Out.Append("    vec4 centerAndPadding;\n");
            Out.Append("    vec4 vtWorld;\n");
            Out.Append("    vec4 vtLayout;\n");
            Out.Append("    vec4 frame;\n");
            Out.Append("    // x = time seconds, yz = viewport size in pixels.\n");
            Out.Append("    vec4 materialParams;\n");
            Out.Append("} pc;\n");
            Out.Append("\n");
        }
        else
        {
            Out.Append("// Full 2.5D view-projection from the mesh push constant. The material\n");
            Out.Append("// pipeline layout must expose the range to the fragment stage.\n");
            Out.Append("layout(push_constant) uniform Push\n");
            Out.Append("{\n");
            Out.Append("    mat4 viewProjection;\n");
            if (InContext.fragmentNeedsMaterialParams)
            {
                Out.Append("    // Material extension: x = time seconds, yz = viewport size in pixels.\n");
                Out.Append("    vec4 materialParams;\n");
            }
            Out.Append("} pc;\n");
            Out.Append("\n");
        }
    }
    Out.Append("layout(location = 0) out vec4 outColor;\n");
    Out.Append("\n");

    if (InContext.needsInverse)
    {
        EmitInverseHelper(Out);
    }
    if (InContext.needsWorldPosition && !InContext.worldViaVarying)
    {
        EmitWorldPositionHelper(Out);
    }
    if (InContext.usesFresnel)
    {
        EmitFresnelHelper(Out);
    }

    Out.Append("void main()\n");
    Out.Append("{\n");
    if (InContext.needsInverse)
    {
        Out.Append("    // One inverse per fragment; every world-space node shares it.\n");
        Out.Append("    const mat4 fbyInverseViewProjection = fbyInverseMat4(pc.viewProjection);\n");
    }
    if (InContext.needsWorldPosition)
    {
        if (ScopedSpriteDomain)
        {
            Out.Append("    // Sprite world position: the quad carries it in world units, z is flat.\n");
            Out.Append("    const vec3 fbyWorldPosition = vec3(vWorld, 0.0);\n");
        }
        else if (InContext.worldViaVarying)
        {
            Out.Append("    const vec3 fbyWorldPosition = vWorldPosition;\n");
        }
        else
        {
            Out.Append("    const vec3 fbyWorldPosition = fbyReconstructWorldPosition(fbyInverseViewProjection);\n");
        }
    }
    if (InContext.usesTime)
    {
        Out.Append("    // Material push-constant clock, in seconds.\n");
        Out.Append("    const float fbyTime = pc.materialParams.x;\n");
    }
    Out.Append("\n");

    // One temporary per reachable node, in dependency order, skipping Output.
    for (std::uint32_t ScopedScheduled = 0; ScopedScheduled < InContext.orderCount; ++ScopedScheduled)
    {
        const std::uint16_t ScopedNodeIndex = InContext.order[ScopedScheduled];
        if (InContext.graph.Node(ScopedNodeIndex).kind == MaterialNodeKind::Output)
        {
            continue;
        }
        Out.Append("    const ");
        Out.Append(MaterialValueTypeGlsl(InContext.nodeTypes[ScopedNodeIndex]));
        Out.Append(" n");
        Out.AppendU32(ScopedNodeIndex);
        Out.Append(" = ");
        AppendNodeExpression(Out, InContext, ScopedNodeIndex);
        Out.Append(";");
        // A short comment naming the node kind keeps the generated source
        Out.Append(" // ");
        Out.Append(MaterialNodeKindName(InContext.graph.Node(ScopedNodeIndex).kind));
        Out.Append("\n");
    }

    const std::uint16_t ScopedOutputNode = InContext.graph.OutputNode();
    Out.Append("\n");
    Out.Append("    // Output node: BaseColor * Opacity with the additive Emissive term on top.\n");
    Out.Append("    const vec4 fbyBaseColor = ");
    AppendOutputBaseColor(Out, InContext, ScopedOutputNode);
    Out.Append(";\n");
    Out.Append("    const vec3 fbyEmissive = ");
    AppendOutputEmissive(Out, InContext, ScopedOutputNode);
    Out.Append(";\n");
    Out.Append("    const float fbyOpacity = ");
    AppendOutputOpacity(Out, InContext, ScopedOutputNode);
    Out.Append(";\n");
    Out.Append("    const vec4 fbyColor = vec4(fbyBaseColor.rgb + fbyEmissive, fbyBaseColor.a * fbyOpacity);\n");
    Out.Append("    // Same alpha cut as the built-in mesh.frag, so cutout sprites stay crisp.\n");
    Out.Append("    if (fbyColor.a < 0.002)\n");
    Out.Append("    {\n");
    Out.Append("        discard;\n");
    Out.Append("    }\n");
    Out.Append("    outColor = fbyColor;\n");
    Out.Append("}\n");
}
}

// MaterialGraphCompiler
bool MaterialGraphCompiler::Compile(const MaterialGraph& InGraph, const MaterialCompileOptions& InOptions,
                                    std::span<char> InVertexBuffer, std::span<char> InFragmentBuffer,
                                    MaterialShaderProgram* InProgram, std::string* InError) const
{
    if (InProgram == nullptr)
    {
        SetError(InError, "material compiler: null program output");
        return false;
    }
    *InProgram = MaterialShaderProgram{};

    if (!InGraph.Validate(InError))
    {
        return false;
    }

    CompileContext ScopedContext(InGraph, InOptions);
    if (!InGraph.ResolveTypes({ScopedContext.nodeTypes, kMaxMaterialNodes}, InError))
    {
        return false;
    }
    ScopedContext.orderCount = InGraph.BuildEvaluationOrder({ScopedContext.order, kMaxMaterialNodes}, InError);
    if (ScopedContext.orderCount == 0)
    {
        return false;
    }
    AnalyzeGraph(ScopedContext);

    // The sprite domain replaces the sampling block of quad.frag only: the
    // vertex stage is the engine quad, and the push block carries the camera,
    // so the mesh-only nodes are refused with a readable reason.
    if (InOptions.domain == MaterialDomain::Sprite)
    {
        if (InOptions.generateVertexStage)
        {
            SetError(InError, "material compiler: the sprite domain always uses the engine quad vertex stage; "
                              "clear MaterialCompileOptions::generateVertexStage");
            return false;
        }
        if (ScopedContext.usesVertexPositionNode)
        {
            SetError(InError, "material compiler: VertexPosition is not available in the sprite domain");
            return false;
        }
        if (ScopedContext.usesFresnel)
        {
            SetError(InError, "material compiler: Fresnel is not available in the sprite domain; it needs the mesh "
                              "view-projection");
            return false;
        }
    }

    if (ScopedContext.usesVertexPositionNode && !InOptions.generateVertexStage)
    {
        SetError(InError, "material graph uses VertexPosition, which only the generated vertex stage can provide; "
                          "enable MaterialCompileOptions::generateVertexStage");
        return false;
    }
    if (InOptions.generateVertexStage && InVertexBuffer.size() < 2)
    {
        SetError(InError, "material compiler: no vertex GLSL buffer provided");
        return false;
    }

    GlslWriter ScopedFragmentWriter(InFragmentBuffer);
    GlslWriter ScopedVertexWriter(InOptions.generateVertexStage ? InVertexBuffer : std::span<char>{});
    if (InOptions.generateVertexStage)
    {
        EmitVertexShader(ScopedVertexWriter, ScopedContext);
    }
    EmitFragmentShader(ScopedFragmentWriter, ScopedContext);

    if (ScopedFragmentWriter.Overflowed())
    {
        SetError(InError, "material fragment GLSL buffer too small: need " +
                              std::to_string(ScopedFragmentWriter.Required()) + " bytes, have " +
                              std::to_string(ScopedFragmentWriter.Capacity() + 1));
        return false;
    }
    if (InOptions.generateVertexStage && ScopedVertexWriter.Overflowed())
    {
        SetError(InError, "material vertex GLSL buffer too small: need " +
                              std::to_string(ScopedVertexWriter.Required()) + " bytes, have " +
                              std::to_string(ScopedVertexWriter.Capacity() + 1));
        return false;
    }

    if (InOptions.generateVertexStage)
    {
        InProgram->vertexLength = static_cast<std::uint32_t>(ScopedVertexWriter.Finish());
    }
    InProgram->fragmentLength = static_cast<std::uint32_t>(ScopedFragmentWriter.Finish());
    InProgram->usesGeneratedVertexStage = InOptions.generateVertexStage;
    InProgram->fragmentReadsViewProjection = ScopedContext.fragmentNeedsViewProjection;
    InProgram->fragmentReadsMaterialParams = ScopedContext.fragmentNeedsMaterialParams;
    InProgram->usesWorldPositionVarying = ScopedContext.worldViaVarying;
    InProgram->usesVertexPositionVarying = ScopedContext.usesVertexPositionNode && InOptions.generateVertexStage;
    return true;
}

bool MaterialGraphCompiler::CompileFragment(const MaterialGraph& InGraph, std::span<char> InFragmentBuffer,
                                            MaterialShaderProgram* InProgram, std::string* InError) const
{
    MaterialCompileOptions ScopedOptions;
    ScopedOptions.generateVertexStage = false;
    return Compile(InGraph, ScopedOptions, std::span<char>{}, InFragmentBuffer, InProgram, InError);
}
}
