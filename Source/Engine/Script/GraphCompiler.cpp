// FemboyEngine2D - VisualScript graph compiler.
#include "Engine/Script/GraphCompiler.hpp"

#include "Engine/ECS/Components.hpp"

#include <bit>
#include <cstdio>
#include <cstring>
#include <string_view>

namespace Fby::Script
{
namespace
{
constexpr std::uint8_t kNoRegister = 0xFFu;
constexpr std::uint16_t kNoNode = 0xFFFFu;
constexpr std::uint16_t kNoBinding = 0xFFFFu;

struct ComponentMapEntry
{
    std::uint64_t layoutHash = 0;
    std::uint32_t size = 0;
    std::uint16_t binding = kNoBinding;
};

[[nodiscard]] constexpr float BitsFloat(std::uint32_t InBits) noexcept
{
    return std::bit_cast<float>(InBits);
}

[[nodiscard]] std::string NodeMessage(std::uint32_t InNode, const char* InText)
{
    char ScopedBuffer[256] = {};
    std::snprintf(ScopedBuffer, sizeof(ScopedBuffer), "node %u (%s): %s", InNode, "?", InText);
    return ScopedBuffer;
}

template <typename T> [[nodiscard]] constexpr const Reflect::FieldDesc* FindField(std::string_view InName) noexcept
{
    for (const Reflect::FieldDesc& ScopedField : Reflect::Fields<T>())
    {
        if (ScopedField.name == InName)
        {
            return &ScopedField;
        }
    }
    return nullptr;
}

class GraphCompiler
{
public:
    GraphCompiler(const LogicGraph& InGraph, const LogicCompileOptions& InOptions, LogicCompileResult* Out)
        : MGraph(InGraph), MOptions(InOptions), MOut(Out)
    {
        MFieldBindingMap.fill(kNoBinding);
        MComponentMap.fill(ComponentMapEntry{});
    }

    [[nodiscard]] bool Compile()
    {
        if (MOut == nullptr)
        {
            return false;
        }
        *MOut = {};
        if (MGraph.nodeCount == 0 || MGraph.nodeCount > kMaxLogicNodes || MGraph.linkCount > kMaxLogicLinks)
        {
            return Fail("graph is empty or exceeds its capacity");
        }
        if (!CheckStructure() || !CheckAcyclic() || !CheckLinks())
        {
            return false;
        }

        // Exactly one event per program: the cooker compiles Start and Update as
        std::uint16_t ScopedEventNode = kNoNode;
        ScriptEvent ScopedEvent = ScriptEvent::Update;
        for (std::uint32_t ScopedI = 0; ScopedI < MGraph.nodeCount; ++ScopedI)
        {
            const LogicNode& ScopedNode = MGraph.nodes[ScopedI];
            if (ScopedNode.kind == LogicNodeKind::EventStart || ScopedNode.kind == LogicNodeKind::EventUpdate)
            {
                if (ScopedEventNode != kNoNode)
                {
                    return Fail("graph contains more than one event node; compile one event per program");
                }
                ScopedEventNode = static_cast<std::uint16_t>(ScopedI);
                ScopedEvent = ScopedNode.kind == LogicNodeKind::EventStart ? ScriptEvent::Start : ScriptEvent::Update;
            }
        }
        if (ScopedEventNode == kNoNode)
        {
            return Fail("graph has no EventStart/EventUpdate node");
        }

        // Self entity lives in r0 for the whole invocation.
        MBuilder.Emit(OpCode::LoadSelf, 0);

        const LogicLink* ScopedFirst = FindExecFrom(ScopedEventNode, 0);
        if (ScopedFirst != nullptr && !CompileChain(ScopedFirst->toNode, 0))
        {
            return false;
        }
        MBuilder.Emit(OpCode::Halt);
        (void)MBuilder.AddFunction(ScopedEvent, 0);

        std::string ScopedBuildError;
        const std::vector<std::byte> ScopedBytes =
            MBuilder.Build(MOptions.sourceHash, MOptions.graphHash, &ScopedBuildError);
        if (ScopedBytes.empty())
        {
            return Fail(ScopedBuildError.empty() ? "program build failed" : ScopedBuildError.c_str());
        }
        CompiledProgram ScopedParsed;
        if (!ParseProgram(ScopedBytes, &ScopedParsed, &ScopedBuildError))
        {
            return Fail(ScopedBuildError.empty() ? "generated program failed validation" : ScopedBuildError.c_str());
        }

        MOut->program = ScopedBytes;
        MOut->instructionCount = ScopedParsed.header.codeCount;
        MOut->registerHighWater = ScopedParsed.header.registerHighWater;
        MOut->componentBindings = ScopedParsed.header.componentCount;
        MOut->fieldBindings = ScopedParsed.header.fieldCount;
        return true;
    }

private:
    [[nodiscard]] bool Fail(const char* InMessage) const
    {
        if (MOut != nullptr)
        {
            MOut->error = InMessage;
        }
        return false;
    }

    [[nodiscard]] bool Fail(const std::string& InMessage) const
    {
        if (MOut != nullptr)
        {
            MOut->error = InMessage;
        }
        return false;
    }

    [[nodiscard]] bool CheckStructure() const
    {
        for (std::uint32_t ScopedI = 0; ScopedI < MGraph.linkCount; ++ScopedI)
        {
            const LogicLink& ScopedLink = MGraph.links[ScopedI];
            if (ScopedLink.fromNode >= MGraph.nodeCount || ScopedLink.toNode >= MGraph.nodeCount)
            {
                return false;
            }
            const LogicNode& ScopedFrom = MGraph.nodes[ScopedLink.fromNode];
            const LogicNode& ScopedTo = MGraph.nodes[ScopedLink.toNode];
            (void)ScopedFrom;
            if (ScopedLink.toPin >= LogicGraph::InputCount(ScopedTo.kind))
            {
                return false;
            }
            const std::uint8_t ScopedMaxOut = ScopedFrom.kind == LogicNodeKind::Branch ? 2u : 1u;
            if (ScopedLink.fromPin >= ScopedMaxOut)
            {
                return false;
            }
        }
        return true;
    }

    // Kahn's algorithm over every link (data and exec). A VisualScript graph is a
    [[nodiscard]] bool CheckAcyclic() const
    {
        std::array<std::uint16_t, kMaxLogicNodes> ScopedIndegree{};
        for (std::uint32_t ScopedI = 0; ScopedI < MGraph.linkCount; ++ScopedI)
        {
            ScopedIndegree[MGraph.links[ScopedI].toNode] =
                static_cast<std::uint16_t>(ScopedIndegree[MGraph.links[ScopedI].toNode] + 1);
        }
        std::array<std::uint16_t, kMaxLogicNodes> ScopedQueue{};
        std::uint32_t ScopedHead = 0;
        std::uint32_t ScopedTail = 0;
        for (std::uint32_t ScopedI = 0; ScopedI < MGraph.nodeCount; ++ScopedI)
        {
            if (ScopedIndegree[ScopedI] == 0)
            {
                ScopedQueue[ScopedTail++] = static_cast<std::uint16_t>(ScopedI);
            }
        }
        std::uint32_t ScopedVisited = 0;
        while (ScopedHead < ScopedTail)
        {
            const std::uint16_t ScopedNode = ScopedQueue[ScopedHead++];
            ++ScopedVisited;
            for (std::uint32_t ScopedI = 0; ScopedI < MGraph.linkCount; ++ScopedI)
            {
                if (MGraph.links[ScopedI].fromNode == ScopedNode)
                {
                    const std::uint16_t ScopedTarget = MGraph.links[ScopedI].toNode;
                    if (--ScopedIndegree[ScopedTarget] == 0)
                    {
                        ScopedQueue[ScopedTail++] = ScopedTarget;
                    }
                }
            }
        }
        if (ScopedVisited != MGraph.nodeCount)
        {
            return Fail("graph contains a cycle; VisualScript graphs must be acyclic");
        }
        return true;
    }

    [[nodiscard]] bool CheckLinks() const
    {
        std::array<std::int16_t, kMaxLogicNodes * kMaxLogicInputs> ScopedUsedInputs{};
        ScopedUsedInputs.fill(-1);
        std::array<std::int16_t, kMaxLogicNodes * 2> ScopedUsedExecOutputs{};
        ScopedUsedExecOutputs.fill(-1);

        for (std::uint32_t ScopedI = 0; ScopedI < MGraph.linkCount; ++ScopedI)
        {
            const LogicLink& ScopedLink = MGraph.links[ScopedI];
            const LogicNode& ScopedFrom = MGraph.nodes[ScopedLink.fromNode];
            const LogicNode& ScopedTo = MGraph.nodes[ScopedLink.toNode];

            if (ScopedLink.toPin >= LogicGraph::InputCount(ScopedTo.kind))
            {
                return Fail(NodeMessage(ScopedLink.toNode, "link targets a pin the node does not have"));
            }
            const PinType ScopedExpected = LogicGraph::InputType(ScopedTo.kind, ScopedLink.toPin);
            const std::uint8_t ScopedMaxOut = ScopedFrom.kind == LogicNodeKind::Branch ? 2u : 1u;
            if (ScopedLink.fromPin >= ScopedMaxOut)
            {
                return Fail(NodeMessage(ScopedLink.fromNode, "link reads a pin the node does not have"));
            }
            // GetField's output type is field-dependent; use the node's cached
            PinType ScopedProduced = static_cast<PinType>(ScopedFrom.outputType);
            if (ScopedFrom.kind == LogicNodeKind::GetField && ScopedFrom.param < MGraph.fieldCount)
            {
                ScopedProduced = LogicGraph::FieldPinType(MGraph.fields[ScopedFrom.param].kind);
            }
            if (ScopedFrom.kind == LogicNodeKind::Branch || ScopedProduced >= PinType::Count)
            {
                ScopedProduced = LogicGraph::OutputType(ScopedFrom.kind, ScopedLink.fromPin);
            }
            if (ScopedExpected != PinType::Any && ScopedProduced != ScopedExpected)
            {
                return Fail(NodeMessage(ScopedLink.toNode, "link connects incompatible pin types"));
            }

            const std::uint32_t ScopedInputIndex =
                static_cast<std::uint32_t>(ScopedLink.toNode) * kMaxLogicInputs + ScopedLink.toPin;
            if (ScopedUsedInputs[ScopedInputIndex] >= 0)
            {
                return Fail(NodeMessage(ScopedLink.toNode, "input pin has more than one connection"));
            }
            ScopedUsedInputs[ScopedInputIndex] = static_cast<std::int16_t>(ScopedI);

            if (ScopedProduced == PinType::Exec)
            {
                const std::uint32_t ScopedOutputIndex =
                    static_cast<std::uint32_t>(ScopedLink.fromNode) * 2 + ScopedLink.fromPin;
                if (ScopedUsedExecOutputs[ScopedOutputIndex] >= 0)
                {
                    return Fail(NodeMessage(ScopedLink.fromNode, "exec output has more than one connection"));
                }
                ScopedUsedExecOutputs[ScopedOutputIndex] = static_cast<std::int16_t>(ScopedI);
            }
        }
        return true;
    }

    [[nodiscard]] const LogicLink* FindExecFrom(std::uint16_t InNode, std::uint8_t InPin) const
    {
        for (std::uint32_t ScopedI = 0; ScopedI < MGraph.linkCount; ++ScopedI)
        {
            const LogicLink& ScopedLink = MGraph.links[ScopedI];
            if (ScopedLink.fromNode == InNode && ScopedLink.fromPin == InPin &&
                LogicGraph::OutputType(MGraph.nodes[InNode].kind, InPin) == PinType::Exec)
            {
                return &ScopedLink;
            }
        }
        return nullptr;
    }

    [[nodiscard]] const LogicLink* FindInputLink(std::uint16_t InNode, std::uint8_t InPin) const
    {
        for (std::uint32_t ScopedI = 0; ScopedI < MGraph.linkCount; ++ScopedI)
        {
            const LogicLink& ScopedLink = MGraph.links[ScopedI];
            if (ScopedLink.toNode == InNode && ScopedLink.toPin == InPin)
            {
                return &ScopedLink;
            }
        }
        return nullptr;
    }

    [[nodiscard]] std::uint8_t Alloc()
    {
        if (MNextRegister >= kMaxVmRegisters)
        {
            MFailedRegisters = true;
            return kNoRegister;
        }
        return MNextRegister++;
    }

    [[nodiscard]] std::uint8_t Alloc2()
    {
        const std::uint8_t ScopedFirst = Alloc();
        if (ScopedFirst == kNoRegister)
        {
            return kNoRegister;
        }
        (void)Alloc();
        return ScopedFirst;
    }

    [[nodiscard]] bool CompileExpr(std::uint16_t InNodeIndex, std::uint8_t InDst)
    {
        const LogicNode& ScopedNode = MGraph.nodes[InNodeIndex];
        switch (ScopedNode.kind)
        {
            case LogicNodeKind::ConstFloat:
            {
                const std::uint16_t ScopedConstant = MBuilder.AddConstant(BitsFloat(ScopedNode.param));
                MBuilder.Emit(OpCode::LoadF32, InDst, 0, ScopedConstant);
                return true;
            }
            case LogicNodeKind::ConstVec2:
            {
                const std::uint16_t ScopedFirst = MBuilder.AddConstant(BitsFloat(ScopedNode.param));
                const std::uint16_t ScopedSecond = MBuilder.AddConstant(BitsFloat(ScopedNode.param2));
                MBuilder.Emit(OpCode::LoadF32, InDst, 0, ScopedFirst);
                MBuilder.Emit(OpCode::LoadF32, static_cast<std::uint8_t>(InDst + 1), 0, ScopedSecond);
                return true;
            }
            case LogicNodeKind::SelfPosition:
            {
                if (!EnsureTransform())
                {
                    return false;
                }
                MBuilder.Emit(OpCode::LoadFieldVec2, InDst, 0, MTransformPosition);
                return true;
            }
            case LogicNodeKind::SelfVelocity:
            {
                if (!EnsureVelocity())
                {
                    return false;
                }
                MBuilder.Emit(OpCode::LoadFieldVec2, InDst, 0, MVelocityValue);
                return true;
            }
            case LogicNodeKind::SelfRotation:
            {
                if (!EnsureTransform())
                {
                    return false;
                }
                MBuilder.Emit(OpCode::LoadFieldF, InDst, 0, MTransformRotation);
                return true;
            }
            case LogicNodeKind::DeltaTime:
            {
                const std::uint16_t ScopedNative = MBuilder.AddNative(NativeId::GetDeltaTime);
                MBuilder.Emit(OpCode::CallNative, InDst, 0, ScopedNative);
                return true;
            }
            case LogicNodeKind::Random01:
            {
                const std::uint16_t ScopedNative = MBuilder.AddNative(NativeId::RandomFloat);
                MBuilder.Emit(OpCode::CallNative, InDst, 0, ScopedNative);
                return true;
            }
            case LogicNodeKind::Add:
            case LogicNodeKind::Sub:
            case LogicNodeKind::Mul:
            case LogicNodeKind::Div:
            {
                const std::uint8_t ScopedLeft = Alloc();
                const std::uint8_t ScopedRight = Alloc();
                if (ScopedLeft == kNoRegister || ScopedRight == kNoRegister)
                {
                    return Fail("expression needs more registers than the VM has");
                }
                if (!CompileInput(InNodeIndex, 0, ScopedLeft) || !CompileInput(InNodeIndex, 1, ScopedRight))
                {
                    return false;
                }
                OpCode ScopedOp = OpCode::AddF;
                if (ScopedNode.kind == LogicNodeKind::Sub)
                {
                    ScopedOp = OpCode::SubF;
                }
                else if (ScopedNode.kind == LogicNodeKind::Mul)
                {
                    ScopedOp = OpCode::MulF;
                }
                else if (ScopedNode.kind == LogicNodeKind::Div)
                {
                    ScopedOp = OpCode::DivF;
                }
                MBuilder.Emit(ScopedOp, InDst, ScopedLeft, ScopedRight);
                return true;
            }
            case LogicNodeKind::Min:
            case LogicNodeKind::Max:
            {
                const std::uint8_t ScopedLeft = Alloc();
                const std::uint8_t ScopedRight = Alloc();
                const std::uint8_t ScopedCompare = Alloc();
                if (ScopedLeft == kNoRegister || ScopedRight == kNoRegister || ScopedCompare == kNoRegister)
                {
                    return Fail("expression needs more registers than the VM has");
                }
                if (!CompileInput(InNodeIndex, 0, ScopedLeft) || !CompileInput(InNodeIndex, 1, ScopedRight))
                {
                    return false;
                }
                MBuilder.Emit(ScopedNode.kind == LogicNodeKind::Min ? OpCode::LtF : OpCode::GtF, ScopedCompare,
                              ScopedLeft, ScopedRight);
                const std::uint32_t ScopedZeroBranch = MBuilder.Emit(OpCode::JumpIfZero, ScopedCompare, 0, 0);
                MBuilder.Emit(OpCode::MoveF, InDst, ScopedLeft);
                const std::uint32_t ScopedEnd = MBuilder.Emit(OpCode::Jump, 0, 0, 0);
                MBuilder.PatchJump(ScopedZeroBranch, MBuilder.CurrentOffset());
                MBuilder.Emit(OpCode::MoveF, InDst, ScopedRight);
                MBuilder.PatchJump(ScopedEnd, MBuilder.CurrentOffset());
                return true;
            }
            case LogicNodeKind::Vec2Scale:
            {
                const std::uint8_t ScopedVector = Alloc2();
                const std::uint8_t ScopedScalar = Alloc();
                if (ScopedVector == kNoRegister || ScopedScalar == kNoRegister)
                {
                    return Fail("expression needs more registers than the VM has");
                }
                if (!CompileInput(InNodeIndex, 0, ScopedVector) || !CompileInput(InNodeIndex, 1, ScopedScalar))
                {
                    return false;
                }
                MBuilder.Emit(OpCode::MulF, InDst, ScopedVector, ScopedScalar);
                MBuilder.Emit(OpCode::MulF, static_cast<std::uint8_t>(InDst + 1),
                              static_cast<std::uint8_t>(ScopedVector + 1), ScopedScalar);
                return true;
            }
            case LogicNodeKind::GetField:
            {
                std::uint16_t ScopedBinding = kNoBinding;
                if (!EnsureFieldBinding(static_cast<std::uint16_t>(ScopedNode.param), &ScopedBinding))
                {
                    return false;
                }
                const LogicFieldBinding* ScopedField = FieldBinding(static_cast<std::uint16_t>(ScopedNode.param));
                if (ScopedField == nullptr)
                {
                    return Fail(NodeMessage(InNodeIndex, "field binding is missing"));
                }
                if (ScopedField->kind == static_cast<std::uint8_t>(Reflect::FieldKind::Vector2D))
                {
                    MBuilder.Emit(OpCode::LoadFieldVec2, InDst, 0, ScopedBinding);
                }
                else
                {
                    MBuilder.Emit(OpCode::LoadFieldF, InDst, 0, ScopedBinding);
                }
                return true;
            }
            case LogicNodeKind::Condition:
            {
                const std::uint8_t ScopedLeft = Alloc();
                const std::uint8_t ScopedRight = Alloc();
                const std::uint8_t ScopedCompare = Alloc();
                if (ScopedLeft == kNoRegister || ScopedRight == kNoRegister || ScopedCompare == kNoRegister)
                {
                    return Fail("expression needs more registers than the VM has");
                }
                if (!CompileInput(InNodeIndex, 0, ScopedLeft) || !CompileInput(InNodeIndex, 1, ScopedRight))
                {
                    return false;
                }
                OpCode ScopedOp = OpCode::EqF;
                switch (static_cast<LogicCompareOp>(ScopedNode.param))
                {
                    case LogicCompareOp::Equal:
                        ScopedOp = OpCode::EqF;
                        break;
                    case LogicCompareOp::NotEqual:
                        ScopedOp = OpCode::NeF;
                        break;
                    case LogicCompareOp::Less:
                        ScopedOp = OpCode::LtF;
                        break;
                    case LogicCompareOp::LessEqual:
                        ScopedOp = OpCode::LeF;
                        break;
                    case LogicCompareOp::Greater:
                        ScopedOp = OpCode::GtF;
                        break;
                    case LogicCompareOp::GreaterEqual:
                        ScopedOp = OpCode::GeF;
                        break;
                    case LogicCompareOp::Count:
                        return Fail(NodeMessage(InNodeIndex, "Condition has an invalid comparison selector"));
                }
                MBuilder.Emit(ScopedOp, ScopedCompare, ScopedLeft, ScopedRight);
                // The compare opcodes write an int 1/0 into the register union, but
                const std::uint16_t ScopedOne = MBuilder.AddConstant(1.0f);
                const std::uint16_t ScopedZero = MBuilder.AddConstant(0.0f);
                const std::uint32_t ScopedFalseJump = MBuilder.Emit(OpCode::JumpIfZero, ScopedCompare, 0, 0);
                MBuilder.Emit(OpCode::LoadF32, InDst, 0, ScopedOne);
                const std::uint32_t ScopedEndJump = MBuilder.Emit(OpCode::Jump, 0, 0, 0);
                MBuilder.PatchJump(ScopedFalseJump, MBuilder.CurrentOffset());
                MBuilder.Emit(OpCode::LoadF32, InDst, 0, ScopedZero);
                MBuilder.PatchJump(ScopedEndJump, MBuilder.CurrentOffset());
                return true;
            }
            case LogicNodeKind::Math:
            {
                const LogicMathOp ScopedMath = static_cast<LogicMathOp>(ScopedNode.param);
                const std::uint8_t ScopedLeft = Alloc();
                const std::uint8_t ScopedRight = Alloc();
                if (ScopedLeft == kNoRegister || ScopedRight == kNoRegister)
                {
                    return Fail("expression needs more registers than the VM has");
                }
                if (!CompileInput(InNodeIndex, 0, ScopedLeft) || !CompileInput(InNodeIndex, 1, ScopedRight))
                {
                    return false;
                }
                switch (ScopedMath)
                {
                    case LogicMathOp::Add:
                        MBuilder.Emit(OpCode::AddF, InDst, ScopedLeft, ScopedRight);
                        return true;
                    case LogicMathOp::Sub:
                        MBuilder.Emit(OpCode::SubF, InDst, ScopedLeft, ScopedRight);
                        return true;
                    case LogicMathOp::Mul:
                        MBuilder.Emit(OpCode::MulF, InDst, ScopedLeft, ScopedRight);
                        return true;
                    case LogicMathOp::Div:
                        MBuilder.Emit(OpCode::DivF, InDst, ScopedLeft, ScopedRight);
                        return true;
                    case LogicMathOp::Min:
                    case LogicMathOp::Max:
                    {
                        const std::uint8_t ScopedCompare = Alloc();
                        if (ScopedCompare == kNoRegister)
                        {
                            return Fail("expression needs more registers than the VM has");
                        }
                        MBuilder.Emit(ScopedMath == LogicMathOp::Min ? OpCode::LtF : OpCode::GtF, ScopedCompare,
                                      ScopedLeft, ScopedRight);
                        const std::uint32_t ScopedRightBranch = MBuilder.Emit(OpCode::JumpIfZero, ScopedCompare, 0, 0);
                        MBuilder.Emit(OpCode::MoveF, InDst, ScopedLeft);
                        const std::uint32_t ScopedEnd = MBuilder.Emit(OpCode::Jump, 0, 0, 0);
                        MBuilder.PatchJump(ScopedRightBranch, MBuilder.CurrentOffset());
                        MBuilder.Emit(OpCode::MoveF, InDst, ScopedRight);
                        MBuilder.PatchJump(ScopedEnd, MBuilder.CurrentOffset());
                        return true;
                    }
                    case LogicMathOp::Count:
                        return Fail(NodeMessage(InNodeIndex, "Math has an invalid operation selector"));
                }
                return false;
            }
            case LogicNodeKind::BigIntFromInt:
            {
                const std::uint8_t ScopedValue = Alloc();
                if (ScopedValue == kNoRegister || !CompileInput(InNodeIndex, 0, ScopedValue))
                {
                    return false;
                }
                const std::uint16_t ScopedNative = MBuilder.AddNative(NativeId::BigIntFromInt);
                MBuilder.Emit(OpCode::CallNative, ScopedValue, 0, ScopedNative);
                MBuilder.Emit(OpCode::MoveF, InDst, ScopedValue);
                return true;
            }
            case LogicNodeKind::BigIntAdd:
            case LogicNodeKind::BigIntSub:
            case LogicNodeKind::BigIntMul:
            case LogicNodeKind::BigIntDiv:
            case LogicNodeKind::BigIntMod:
            case LogicNodeKind::BigIntCmp:
            {
                const std::uint8_t ScopedLeft = Alloc();
                const std::uint8_t ScopedRight = Alloc();
                if (ScopedLeft == kNoRegister || ScopedRight == kNoRegister)
                {
                    return Fail("expression needs more registers than the VM has");
                }
                if (!CompileInput(InNodeIndex, 0, ScopedLeft) || !CompileInput(InNodeIndex, 1, ScopedRight))
                {
                    return false;
                }
                NativeId ScopedNative = NativeId::BigIntAdd;
                if (ScopedNode.kind == LogicNodeKind::BigIntSub)
                {
                    ScopedNative = NativeId::BigIntSub;
                }
                else if (ScopedNode.kind == LogicNodeKind::BigIntMul)
                {
                    ScopedNative = NativeId::BigIntMul;
                }
                else if (ScopedNode.kind == LogicNodeKind::BigIntDiv)
                {
                    ScopedNative = NativeId::BigIntDiv;
                }
                else if (ScopedNode.kind == LogicNodeKind::BigIntMod)
                {
                    ScopedNative = NativeId::BigIntMod;
                }
                else if (ScopedNode.kind == LogicNodeKind::BigIntCmp)
                {
                    ScopedNative = NativeId::BigIntCmp;
                }
                const std::uint16_t ScopedIndex = MBuilder.AddNative(ScopedNative);
                MBuilder.Emit(OpCode::CallNative, ScopedLeft, 0, ScopedIndex);
                MBuilder.Emit(OpCode::MoveF, InDst, ScopedLeft);
                return true;
            }
            case LogicNodeKind::BigIntToInt:
            {
                const std::uint8_t ScopedValue = Alloc();
                if (ScopedValue == kNoRegister || !CompileInput(InNodeIndex, 0, ScopedValue))
                {
                    return false;
                }
                const std::uint16_t ScopedNative = MBuilder.AddNative(NativeId::BigIntToInt);
                MBuilder.Emit(OpCode::CallNative, ScopedValue, 0, ScopedNative);
                MBuilder.Emit(OpCode::MoveF, InDst, ScopedValue);
                return true;
            }
            case LogicNodeKind::DecimalFromInt:
            {
                const std::uint8_t ScopedValue = Alloc();
                if (ScopedValue == kNoRegister || !CompileInput(InNodeIndex, 0, ScopedValue))
                {
                    return false;
                }
                const std::uint16_t ScopedNative = MBuilder.AddNative(NativeId::DecimalFromInt);
                MBuilder.Emit(OpCode::CallNative, ScopedValue, 0, ScopedNative);
                MBuilder.Emit(OpCode::MoveF, InDst, ScopedValue);
                return true;
            }
            case LogicNodeKind::DecimalAdd:
            case LogicNodeKind::DecimalSub:
            case LogicNodeKind::DecimalMul:
            case LogicNodeKind::DecimalDiv:
            case LogicNodeKind::DecimalCmp:
            {
                const std::uint8_t ScopedLeft = Alloc();
                const std::uint8_t ScopedRight = Alloc();
                if (ScopedLeft == kNoRegister || ScopedRight == kNoRegister)
                {
                    return Fail("expression needs more registers than the VM has");
                }
                if (!CompileInput(InNodeIndex, 0, ScopedLeft) || !CompileInput(InNodeIndex, 1, ScopedRight))
                {
                    return false;
                }
                NativeId ScopedNative = NativeId::DecimalAdd;
                if (ScopedNode.kind == LogicNodeKind::DecimalSub)
                {
                    ScopedNative = NativeId::DecimalSub;
                }
                else if (ScopedNode.kind == LogicNodeKind::DecimalMul)
                {
                    ScopedNative = NativeId::DecimalMul;
                }
                else if (ScopedNode.kind == LogicNodeKind::DecimalDiv)
                {
                    ScopedNative = NativeId::DecimalDiv;
                }
                else if (ScopedNode.kind == LogicNodeKind::DecimalCmp)
                {
                    ScopedNative = NativeId::DecimalCmp;
                }
                const std::uint16_t ScopedIndex = MBuilder.AddNative(ScopedNative);
                MBuilder.Emit(OpCode::CallNative, ScopedLeft, 0, ScopedIndex);
                MBuilder.Emit(OpCode::MoveF, InDst, ScopedLeft);
                return true;
            }
            case LogicNodeKind::DecimalDivRounded:
            {
                const std::uint8_t ScopedLeft = Alloc();
                const std::uint8_t ScopedRight = Alloc();
                const std::uint8_t ScopedMode = Alloc();
                if (ScopedLeft == kNoRegister || ScopedRight == kNoRegister || ScopedMode == kNoRegister)
                {
                    return Fail("expression needs more registers than the VM has");
                }
                if (!CompileInput(InNodeIndex, 0, ScopedLeft) || !CompileInput(InNodeIndex, 1, ScopedRight) ||
                    !CompileInput(InNodeIndex, 2, ScopedMode))
                {
                    return false;
                }
                const std::uint16_t ScopedNative = MBuilder.AddNative(NativeId::DecimalDivRounded);
                MBuilder.Emit(OpCode::CallNative, ScopedLeft, 0, ScopedNative);
                MBuilder.Emit(OpCode::MoveF, InDst, ScopedLeft);
                return true;
            }
            case LogicNodeKind::DecimalToInt:
            {
                const std::uint8_t ScopedValue = Alloc();
                if (ScopedValue == kNoRegister || !CompileInput(InNodeIndex, 0, ScopedValue))
                {
                    return false;
                }
                const std::uint16_t ScopedNative = MBuilder.AddNative(NativeId::DecimalToInt);
                MBuilder.Emit(OpCode::CallNative, ScopedValue, 0, ScopedNative);
                MBuilder.Emit(OpCode::MoveF, InDst, ScopedValue);
                return true;
            }
            default:
                return Fail(NodeMessage(InNodeIndex, "node is not an expression and cannot be used as a value"));
        }
    }

    [[nodiscard]] bool CompileInput(std::uint16_t InNodeIndex, std::uint8_t InPin, std::uint8_t InDst)
    {
        const LogicLink* ScopedLink = FindInputLink(InNodeIndex, InPin);
        if (ScopedLink == nullptr)
        {
            return Fail(NodeMessage(InNodeIndex, "required input pin is not connected"));
        }
        return CompileExpr(ScopedLink->fromNode, InDst);
    }

    [[nodiscard]] bool CompileChain(std::uint16_t InNodeIndex, std::uint32_t InDepth)
    {
        if (InDepth > kMaxLogicNodes)
        {
            return Fail("exec chain is deeper than the node capacity");
        }
        // A node span covers every instruction until the next node starts, so a
        // paused pc maps back to the node that emitted it.
        MBuilder.AddSpan(MBuilder.CodeSize(), InNodeIndex, 0, DebugSpanGraphNode);
        // R0 is the self entity and stays live across statements
        MNextRegister = 1;
        const LogicNode& ScopedNode = MGraph.nodes[InNodeIndex];

        switch (ScopedNode.kind)
        {
            case LogicNodeKind::DespawnSelf:
            {
                const std::uint16_t ScopedNative = MBuilder.AddNative(NativeId::DespawnSelf);
                MBuilder.Emit(OpCode::CallNative, 0, 0, ScopedNative);
                break;
            }
            case LogicNodeKind::LogValue:
            {
                const std::uint8_t ScopedValue = Alloc();
                if (ScopedValue == kNoRegister || !CompileInput(InNodeIndex, 1, ScopedValue))
                {
                    return false;
                }
                const std::uint16_t ScopedNative = MBuilder.AddNative(NativeId::LogFloat);
                MBuilder.Emit(OpCode::CallNative, ScopedValue, 0, ScopedNative);
                break;
            }
            case LogicNodeKind::BigIntLog:
            {
                const std::uint8_t ScopedValue = Alloc();
                if (ScopedValue == kNoRegister || !CompileInput(InNodeIndex, 1, ScopedValue))
                {
                    return false;
                }
                const std::uint16_t ScopedNative = MBuilder.AddNative(NativeId::BigIntLog);
                MBuilder.Emit(OpCode::CallNative, ScopedValue, 0, ScopedNative);
                break;
            }
            case LogicNodeKind::DecimalLog:
            {
                const std::uint8_t ScopedValue = Alloc();
                if (ScopedValue == kNoRegister || !CompileInput(InNodeIndex, 1, ScopedValue))
                {
                    return false;
                }
                const std::uint16_t ScopedNative = MBuilder.AddNative(NativeId::DecimalLog);
                MBuilder.Emit(OpCode::CallNative, ScopedValue, 0, ScopedNative);
                break;
            }
            case LogicNodeKind::SetPosition:
            {
                const std::uint8_t ScopedValue = Alloc2();
                if (ScopedValue == kNoRegister || !EnsureTransform() || !CompileInput(InNodeIndex, 1, ScopedValue))
                {
                    return false;
                }
                MBuilder.Emit(OpCode::StoreFieldVec2, 0, ScopedValue, MTransformPosition);
                break;
            }
            case LogicNodeKind::SetVelocity:
            {
                const std::uint8_t ScopedValue = Alloc2();
                if (ScopedValue == kNoRegister || !EnsureVelocity() || !CompileInput(InNodeIndex, 1, ScopedValue))
                {
                    return false;
                }
                MBuilder.Emit(OpCode::StoreFieldVec2, 0, ScopedValue, MVelocityValue);
                break;
            }
            case LogicNodeKind::SetField:
            {
                const std::uint16_t ScopedFieldIndex = static_cast<std::uint16_t>(ScopedNode.param);
                const LogicFieldBinding* ScopedField = FieldBinding(ScopedFieldIndex);
                if (ScopedField == nullptr)
                {
                    return Fail(NodeMessage(InNodeIndex, "Set Field has no field binding"));
                }
                if (!LogicFieldIsScriptable(ScopedField->kind))
                {
                    return Fail(NodeMessage(InNodeIndex, "Set Field targets a non-scriptable field"));
                }
                const PinType ScopedSourceType = InputSourceType(InNodeIndex, 1);
                const PinType ScopedExpected = LogicGraph::FieldPinType(ScopedField->kind);
                if (ScopedSourceType != ScopedExpected)
                {
                    return Fail(NodeMessage(InNodeIndex, "Set Field value type does not match the bound member"));
                }
                const std::uint8_t ScopedValue =
                    ScopedField->kind == static_cast<std::uint8_t>(Reflect::FieldKind::Vector2D) ? Alloc2() : Alloc();
                if (ScopedValue == kNoRegister || !CompileInput(InNodeIndex, 1, ScopedValue))
                {
                    return false;
                }
                std::uint16_t ScopedBinding = kNoBinding;
                if (!EnsureFieldBinding(ScopedFieldIndex, &ScopedBinding))
                {
                    return false;
                }
                if (ScopedField->kind == static_cast<std::uint8_t>(Reflect::FieldKind::Vector2D))
                {
                    MBuilder.Emit(OpCode::StoreFieldVec2, 0, ScopedValue, ScopedBinding);
                }
                else
                {
                    MBuilder.Emit(OpCode::StoreFieldF, 0, ScopedValue, ScopedBinding);
                }
                break;
            }
            case LogicNodeKind::SpawnEntity:
            {
                // SpawnPrefab writes the new entity handle into a scratch register;
                const std::uint8_t ScopedSpawned = Alloc();
                if (ScopedSpawned == kNoRegister)
                {
                    return Fail("Spawn Entity needs a free VM register");
                }
                MBuilder.Emit(OpCode::SpawnPrefab, ScopedSpawned, 0, static_cast<std::uint16_t>(ScopedNode.param));
                break;
            }
            case LogicNodeKind::Branch:
            {
                const std::uint8_t ScopedCondition = Alloc();
                const std::uint8_t ScopedZero = Alloc();
                if (ScopedCondition == kNoRegister || ScopedZero == kNoRegister ||
                    !CompileInput(InNodeIndex, 1, ScopedCondition))
                {
                    return false;
                }
                const std::uint16_t ScopedZeroConstant = MBuilder.AddConstant(0.0f);
                MBuilder.Emit(OpCode::LoadF32, ScopedZero, 0, ScopedZeroConstant);
                // Normalize the float to a 0/1 integer: +0.0f and -0.0f both become
                MBuilder.Emit(OpCode::NeF, ScopedCondition, ScopedCondition, ScopedZero);
                const std::uint32_t ScopedFalseJump = MBuilder.Emit(OpCode::JumpIfZero, ScopedCondition, 0, 0);

                const LogicLink* ScopedWhenTrue = FindExecFrom(InNodeIndex, 0);
                if (ScopedWhenTrue != nullptr && !CompileChain(ScopedWhenTrue->toNode, InDepth + 1))
                {
                    return false;
                }
                const std::uint32_t ScopedEndJump = MBuilder.Emit(OpCode::Jump, 0, 0, 0);
                MBuilder.PatchJump(ScopedFalseJump, MBuilder.CurrentOffset());
                const LogicLink* ScopedWhenFalse = FindExecFrom(InNodeIndex, 1);
                if (ScopedWhenFalse != nullptr && !CompileChain(ScopedWhenFalse->toNode, InDepth + 1))
                {
                    return false;
                }
                MBuilder.PatchJump(ScopedEndJump, MBuilder.CurrentOffset());
                return true;
            }
            default:
                return Fail(NodeMessage(InNodeIndex, "node is not an executable statement"));
        }

        const LogicLink* ScopedNext = FindExecFrom(InNodeIndex, 0);
        if (ScopedNext != nullptr)
        {
            return CompileChain(ScopedNext->toNode, InDepth + 1);
        }
        return true;
    }

    // Resolves one graph field binding to a ProgramBuilder component/field pair.
    [[nodiscard]] bool EnsureFieldBinding(std::uint16_t InFieldIndex, std::uint16_t* OutBinding)
    {
        if (InFieldIndex >= MGraph.fieldCount)
        {
            return Fail(NodeMessage(InFieldIndex, "field binding index is out of range"));
        }
        if (MFieldBindingMap[InFieldIndex] != kNoBinding)
        {
            *OutBinding = MFieldBindingMap[InFieldIndex];
            return true;
        }
        const LogicFieldBinding& ScopedField = MGraph.fields[InFieldIndex];
        if (!LogicFieldIsScriptable(ScopedField.kind))
        {
            return Fail("LogicGraph field is not a Float or Vec2 member");
        }

        std::uint16_t ScopedComponent = kNoBinding;
        for (std::uint32_t ScopedI = 0; ScopedI < MComponentMapCount; ++ScopedI)
        {
            if (MComponentMap[ScopedI].layoutHash == ScopedField.layoutHash &&
                MComponentMap[ScopedI].size == ScopedField.componentSize)
            {
                ScopedComponent = MComponentMap[ScopedI].binding;
                break;
            }
        }
        if (ScopedComponent == kNoBinding)
        {
            ScopedComponent = MBuilder.AddComponent(ScopedField.layoutHash, ScopedField.componentSize);
            if (MComponentMapCount >= MComponentMap.size())
            {
                return Fail("LogicGraph touches more component types than a program can bind");
            }
            MComponentMap[MComponentMapCount++] =
                ComponentMapEntry{ScopedField.layoutHash, ScopedField.componentSize, ScopedComponent};
        }
        const std::uint16_t ScopedBinding =
            MBuilder.AddField(ScopedComponent, static_cast<Reflect::FieldKind>(ScopedField.kind), ScopedField.offset,
                              ScopedField.fieldSize);
        MFieldBindingMap[InFieldIndex] = ScopedBinding;
        *OutBinding = ScopedBinding;
        return true;
    }

    [[nodiscard]] const LogicFieldBinding* FieldBinding(std::uint16_t InFieldIndex) const noexcept
    {
        return InFieldIndex < MGraph.fieldCount ? &MGraph.fields[InFieldIndex] : nullptr;
    }

    // The value type of the node feeding one input pin, or Exec when missing.
    [[nodiscard]] PinType InputSourceType(std::uint16_t InNodeIndex, std::uint8_t InPin) const noexcept
    {
        const LogicLink* ScopedLink = FindInputLink(InNodeIndex, InPin);
        if (ScopedLink == nullptr)
        {
            return PinType::Exec;
        }
        const LogicNode& ScopedSource = MGraph.nodes[ScopedLink->fromNode];
        if (ScopedSource.kind == LogicNodeKind::GetField && ScopedSource.param < MGraph.fieldCount)
        {
            return LogicGraph::FieldPinType(MGraph.fields[ScopedSource.param].kind);
        }
        const PinType ScopedType = static_cast<PinType>(ScopedSource.outputType);
        return ScopedType < PinType::Count ? ScopedType
                                           : LogicGraph::OutputType(ScopedSource.kind, ScopedLink->fromPin);
    }

    [[nodiscard]] bool EnsureTransform()
    {
        if (MTransformComponent != kNoNode)
        {
            return true;
        }
        const auto* ScopedPosition = FindField<Transform2D>("Position");
        const auto* ScopedRotation = FindField<Transform2D>("Rotation");
        if (ScopedPosition == nullptr || ScopedRotation == nullptr)
        {
            return Fail("Transform2D is not reflected with position/rotation fields");
        }
        MTransformComponent = MBuilder.AddComponent(Reflect::LayoutHash<Transform2D>(), sizeof(Transform2D));
        MTransformPosition =
            MBuilder.AddField(MTransformComponent, ScopedPosition->kind, ScopedPosition->offset, ScopedPosition->size);
        MTransformRotation =
            MBuilder.AddField(MTransformComponent, ScopedRotation->kind, ScopedRotation->offset, ScopedRotation->size);
        return true;
    }

    [[nodiscard]] bool EnsureVelocity()
    {
        if (MVelocityComponent != kNoNode)
        {
            return true;
        }
        const auto* ScopedValue = FindField<Velocity>("Value");
        if (ScopedValue == nullptr)
        {
            return Fail("Velocity is not reflected with a value field");
        }
        MVelocityComponent = MBuilder.AddComponent(Reflect::LayoutHash<Velocity>(), sizeof(Velocity));
        MVelocityValue =
            MBuilder.AddField(MVelocityComponent, ScopedValue->kind, ScopedValue->offset, ScopedValue->size);
        return true;
    }

    const LogicGraph& MGraph;
    const LogicCompileOptions& MOptions;
    LogicCompileResult* MOut = nullptr;
    ProgramBuilder MBuilder{};
    std::uint8_t MNextRegister = 1;
    bool MFailedRegisters = false;
    std::uint16_t MTransformComponent = kNoNode;
    std::uint16_t MVelocityComponent = kNoNode;
    std::uint16_t MTransformPosition = 0;
    std::uint16_t MTransformRotation = 0;
    std::uint16_t MVelocityValue = 0;

    std::array<std::uint16_t, kMaxLogicFields> MFieldBindingMap{};
    std::array<ComponentMapEntry, kMaxComponentBindings> MComponentMap{};
    std::uint32_t MComponentMapCount = 0;
};
}

std::uint64_t HashLogicGraph(const LogicGraph& InGraph) noexcept
{
    std::uint64_t ScopedHash = Reflect::Fnv1a("LogicGraph");
    ScopedHash = Reflect::MixHash(ScopedHash, InGraph.nodeCount);
    ScopedHash = Reflect::MixHash(ScopedHash, InGraph.linkCount);
    ScopedHash = Reflect::MixHash(ScopedHash, InGraph.fieldCount);
    for (std::uint32_t ScopedI = 0; ScopedI < InGraph.nodeCount; ++ScopedI)
    {
        const LogicNode& ScopedNode = InGraph.nodes[ScopedI];
        ScopedHash = Reflect::MixHash(ScopedHash, static_cast<std::uint64_t>(ScopedNode.kind));
        ScopedHash = Reflect::MixHash(ScopedHash, ScopedNode.inputCount);
        ScopedHash = Reflect::MixHash(ScopedHash, ScopedNode.outputType);
        ScopedHash = Reflect::MixHash(ScopedHash, ScopedNode.param);
        ScopedHash = Reflect::MixHash(ScopedHash, ScopedNode.param2);
        ScopedHash = Reflect::MixHash(ScopedHash, ScopedNode.flags);
    }
    for (std::uint32_t ScopedI = 0; ScopedI < InGraph.linkCount; ++ScopedI)
    {
        const LogicLink& ScopedLink = InGraph.links[ScopedI];
        ScopedHash = Reflect::MixHash(ScopedHash, ScopedLink.fromNode);
        ScopedHash = Reflect::MixHash(ScopedHash, ScopedLink.fromPin);
        ScopedHash = Reflect::MixHash(ScopedHash, ScopedLink.toNode);
        ScopedHash = Reflect::MixHash(ScopedHash, ScopedLink.toPin);
    }
    for (std::uint32_t ScopedI = 0; ScopedI < InGraph.fieldCount; ++ScopedI)
    {
        const LogicFieldBinding& ScopedField = InGraph.fields[ScopedI];
        ScopedHash = Reflect::MixHash(ScopedHash, ScopedField.layoutHash);
        ScopedHash = Reflect::MixHash(ScopedHash, ScopedField.offset);
        ScopedHash = Reflect::MixHash(ScopedHash, ScopedField.fieldSize);
        ScopedHash = Reflect::MixHash(ScopedHash, ScopedField.kind);
    }
    return ScopedHash;
}

bool CompileLogicGraph(const LogicGraph& InGraph, const LogicCompileOptions& InOptions, LogicCompileResult* Out)
{
    GraphCompiler ScopedCompiler(InGraph, InOptions, Out);
    return ScopedCompiler.Compile();
}
}
