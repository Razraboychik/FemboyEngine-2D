// FemboyEngine2D - FemboyScript 2.0 bytecode validation and building.
#include "Engine/Script/Bytecode.hpp"

#include <algorithm>
#include <cstring>
#include <limits>

namespace Fby::Script
{
namespace
{
constexpr std::size_t AlignUp(std::size_t InValue, std::size_t InAlignment) noexcept
{
    return (InValue + InAlignment - 1) & ~(InAlignment - 1);
}

void SetError(std::string* InError, const char* InMessage)
{
    if (InError != nullptr)
    {
        *InError = InMessage;
    }
}

void SetError(std::string* InError, const std::string& InMessage)
{
    if (InError != nullptr)
    {
        *InError = InMessage;
    }
}

[[nodiscard]] bool IsRegister(std::uint32_t InIndex, std::uint32_t InLimit) noexcept
{
    return InIndex < InLimit;
}

[[nodiscard]] bool HasVec2Destination(OpCode InOp) noexcept
{
    return InOp == OpCode::LoadFieldVec2;
}

[[nodiscard]] bool HasVec2Source(OpCode InOp) noexcept
{
    return InOp == OpCode::StoreFieldVec2;
}

// The instruction's register footprint, so the builder can compute the high
void TouchRegisters(const Instruction& InInstruction, std::uint32_t* InHighWater)
{
    const OpCode ScopedOp = InInstruction.Code();
    const auto ScopedTouch = [InHighWater](std::uint32_t InIndex)
    {
        if (InIndex + 1 > *InHighWater)
        {
            *InHighWater = InIndex + 1;
        }
    };

    ScopedTouch(InInstruction.dst);
    ScopedTouch(InInstruction.src);
    if (HasVec2Destination(ScopedOp))
    {
        ScopedTouch(static_cast<std::uint32_t>(InInstruction.dst) + 1u);
    }
    if (HasVec2Source(ScopedOp))
    {
        ScopedTouch(static_cast<std::uint32_t>(InInstruction.src) + 1u);
    }

    switch (ScopedOp)
    {
        case OpCode::AddF:
        case OpCode::SubF:
        case OpCode::MulF:
        case OpCode::DivF:
        case OpCode::AddI:
        case OpCode::SubI:
        case OpCode::MulI:
        case OpCode::DivI:
        case OpCode::ModI:
        case OpCode::LtF:
        case OpCode::LeF:
        case OpCode::GtF:
        case OpCode::GeF:
        case OpCode::EqF:
        case OpCode::NeF:
        case OpCode::LtI:
        case OpCode::LeI:
        case OpCode::GtI:
        case OpCode::GeI:
        case OpCode::EqI:
        case OpCode::NeI:
        case OpCode::RandomRangeF:
            // The second arithmetic source is encoded in `operand` as a register.
            ScopedTouch(InInstruction.operand);
            break;
        default:
            break;
    }
}

struct Layout
{
    std::size_t codeOffset = 0;
    std::size_t constantOffset = 0;
    std::size_t fieldOffset = 0;
    std::size_t componentOffset = 0;
    std::size_t functionOffset = 0;
    std::size_t nativeOffset = 0;
    std::size_t totalSize = 0;
};

[[nodiscard]] Layout ComputeLayout(const ProgramHeader& InHeader) noexcept
{
    Layout ScopedLayout;
    std::size_t ScopedCursor = sizeof(ProgramHeader);
    ScopedLayout.codeOffset = ScopedCursor;
    ScopedCursor += static_cast<std::size_t>(InHeader.codeCount) * sizeof(Instruction);
    ScopedLayout.constantOffset = ScopedCursor;
    ScopedCursor += static_cast<std::size_t>(InHeader.constantCount) * sizeof(float);
    ScopedLayout.fieldOffset = AlignUp(ScopedCursor, alignof(FieldBinding));
    ScopedCursor = ScopedLayout.fieldOffset + static_cast<std::size_t>(InHeader.fieldCount) * sizeof(FieldBinding);
    ScopedLayout.componentOffset = AlignUp(ScopedCursor, alignof(ComponentBinding));
    ScopedCursor =
        ScopedLayout.componentOffset + static_cast<std::size_t>(InHeader.componentCount) * sizeof(ComponentBinding);
    ScopedLayout.functionOffset = AlignUp(ScopedCursor, alignof(FunctionEntry));
    ScopedCursor =
        ScopedLayout.functionOffset + static_cast<std::size_t>(InHeader.functionCount) * sizeof(FunctionEntry);
    ScopedLayout.nativeOffset = AlignUp(ScopedCursor, alignof(std::uint16_t));
    ScopedCursor = ScopedLayout.nativeOffset + static_cast<std::size_t>(InHeader.nativeCount) * sizeof(std::uint16_t);
    ScopedLayout.totalSize = AlignUp(ScopedCursor, alignof(std::max_align_t));
    return ScopedLayout;
}
}

bool ParseProgram(std::span<const std::byte> InStorage, CompiledProgram* Out, std::string* InError)
{
    if (Out == nullptr)
    {
        SetError(InError, "ParseProgram: null output");
        return false;
    }
    *Out = {};
    if (InStorage.size() < sizeof(ProgramHeader))
    {
        SetError(InError, "program is truncated (no complete header)");
        return false;
    }

    ProgramHeader ScopedHeader{};
    std::memcpy(&ScopedHeader, InStorage.data(), sizeof(ScopedHeader));
    if (ScopedHeader.magic != kProgramMagic)
    {
        SetError(InError, "program has a bad magic (not a cooked FemboyScript program)");
        return false;
    }
    if (ScopedHeader.version != kProgramVersion)
    {
        SetError(InError, "program uses an unsupported bytecode version");
        return false;
    }
    if (ScopedHeader.codeCount == 0 || ScopedHeader.codeCount > kMaxInstructions ||
        ScopedHeader.constantCount > kMaxConstants || ScopedHeader.fieldCount > kMaxFieldBindings ||
        ScopedHeader.componentCount > kMaxComponentBindings || ScopedHeader.functionCount == 0 ||
        ScopedHeader.functionCount > kMaxFunctions || ScopedHeader.nativeCount > kMaxProgramNatives)
    {
        SetError(InError, "program header table sizes are out of range");
        return false;
    }
    if (ScopedHeader.registerHighWater == 0 || ScopedHeader.registerHighWater > kMaxVmRegisters)
    {
        SetError(InError, "program register high-water mark is out of range");
        return false;
    }

    const Layout ScopedLayout = ComputeLayout(ScopedHeader);
    if (InStorage.size() < ScopedLayout.totalSize)
    {
        SetError(InError, "program payload is shorter than its declared tables");
        return false;
    }

    CompiledProgram ScopedProgram;
    ScopedProgram.header = ScopedHeader;
    ScopedProgram.storage = InStorage.first(ScopedLayout.totalSize);
    ScopedProgram.code = {reinterpret_cast<const Instruction*>(InStorage.data() + ScopedLayout.codeOffset),
                          ScopedHeader.codeCount};
    ScopedProgram.constants = {reinterpret_cast<const float*>(InStorage.data() + ScopedLayout.constantOffset),
                               ScopedHeader.constantCount};
    ScopedProgram.fields = {reinterpret_cast<const FieldBinding*>(InStorage.data() + ScopedLayout.fieldOffset),
                            ScopedHeader.fieldCount};
    ScopedProgram.components = {
        reinterpret_cast<const ComponentBinding*>(InStorage.data() + ScopedLayout.componentOffset),
        ScopedHeader.componentCount};
    ScopedProgram.functions = {reinterpret_cast<const FunctionEntry*>(InStorage.data() + ScopedLayout.functionOffset),
                               ScopedHeader.functionCount};
    ScopedProgram.natives = {reinterpret_cast<const std::uint16_t*>(InStorage.data() + ScopedLayout.nativeOffset),
                             ScopedHeader.nativeCount};

    const auto ScopedRegisters = ScopedHeader.registerHighWater;
    for (std::uint32_t ScopedPc = 0; ScopedPc < ScopedHeader.codeCount; ++ScopedPc)
    {
        const Instruction& ScopedInstruction = ScopedProgram.code[ScopedPc];
        const OpCode ScopedOp = ScopedInstruction.Code();
        if (static_cast<std::uint32_t>(ScopedOp) >= static_cast<std::uint32_t>(OpCode::Count))
        {
            SetError(InError, "program contains an unknown opcode at instruction " + std::to_string(ScopedPc));
            return false;
        }
        if (!IsRegister(ScopedInstruction.dst, ScopedRegisters) || !IsRegister(ScopedInstruction.src, ScopedRegisters))
        {
            SetError(InError, "program touches a register outside its high-water mark at instruction " +
                                  std::to_string(ScopedPc));
            return false;
        }
        if (HasVec2Destination(ScopedOp) &&
            !IsRegister(static_cast<std::uint32_t>(ScopedInstruction.dst) + 1u, ScopedRegisters))
        {
            SetError(InError,
                     "program writes a Vec2 past the register file at instruction " + std::to_string(ScopedPc));
            return false;
        }
        if (HasVec2Source(ScopedOp) &&
            !IsRegister(static_cast<std::uint32_t>(ScopedInstruction.src) + 1u, ScopedRegisters))
        {
            SetError(InError, "program reads a Vec2 past the register file at instruction " + std::to_string(ScopedPc));
            return false;
        }

        switch (ScopedOp)
        {
            case OpCode::LoadF32:
            case OpCode::RandomRangeF:
                if (ScopedInstruction.operand >= ScopedHeader.constantCount)
                {
                    SetError(InError,
                             "program references a constant out of range at instruction " + std::to_string(ScopedPc));
                    return false;
                }
                break;
            case OpCode::LoadFieldF:
            case OpCode::StoreFieldF:
                if (ScopedInstruction.operand >= ScopedHeader.fieldCount)
                {
                    SetError(InError, "program references a field binding out of range at instruction " +
                                          std::to_string(ScopedPc));
                    return false;
                }
                if (ScopedProgram.fields[ScopedInstruction.operand].kind !=
                        static_cast<std::uint8_t>(Reflect::FieldKind::Float) ||
                    ScopedProgram.fields[ScopedInstruction.operand].size != sizeof(float))
                {
                    SetError(InError, "float field opcode does not match its field binding at instruction " +
                                          std::to_string(ScopedPc));
                    return false;
                }
                break;
            case OpCode::LoadFieldI:
            case OpCode::StoreFieldI:
                if (ScopedInstruction.operand >= ScopedHeader.fieldCount)
                {
                    SetError(InError, "program references a field binding out of range at instruction " +
                                          std::to_string(ScopedPc));
                    return false;
                }
                if (ScopedProgram.fields[ScopedInstruction.operand].size == 0 ||
                    ScopedProgram.fields[ScopedInstruction.operand].size > sizeof(std::uint32_t))
                {
                    SetError(InError, "integer field opcode requires a 1..4 byte field at instruction " +
                                          std::to_string(ScopedPc));
                    return false;
                }
                break;
            case OpCode::LoadFieldVec2:
            case OpCode::StoreFieldVec2:
                if (ScopedInstruction.operand >= ScopedHeader.fieldCount)
                {
                    SetError(InError, "program references a field binding out of range at instruction " +
                                          std::to_string(ScopedPc));
                    return false;
                }
                if (ScopedProgram.fields[ScopedInstruction.operand].kind !=
                        static_cast<std::uint8_t>(Reflect::FieldKind::Vector2D) ||
                    ScopedProgram.fields[ScopedInstruction.operand].size != 2 * sizeof(float))
                {
                    SetError(InError, "Vec2 field opcode does not match its field binding at instruction " +
                                          std::to_string(ScopedPc));
                    return false;
                }
                break;
            case OpCode::CallNative:
                if (ScopedInstruction.operand >= ScopedHeader.nativeCount)
                {
                    SetError(InError,
                             "program references a native out of range at instruction " + std::to_string(ScopedPc));
                    return false;
                }
                if (ScopedProgram.natives[ScopedInstruction.operand] >= kNativeFunctionCount)
                {
                    SetError(InError, "program references an unknown native id at instruction " +
                                          std::to_string(ScopedPc));
                    return false;
                }
                if (static_cast<std::uint32_t>(ScopedInstruction.dst) +
                        NativeArgumentCount(static_cast<NativeId>(ScopedProgram.natives[ScopedInstruction.operand])) >
                    ScopedRegisters)
                {
                    SetError(InError, "native call arguments exceed the register file at instruction " +
                                          std::to_string(ScopedPc));
                    return false;
                }
                break;
            case OpCode::Jump:
            case OpCode::JumpIfZero:
            case OpCode::JumpIfNonZero:
                if (ScopedInstruction.operand >= ScopedHeader.codeCount)
                {
                    SetError(InError, "program jumps outside its code at instruction " + std::to_string(ScopedPc));
                    return false;
                }
                break;
            case OpCode::AddF:
            case OpCode::SubF:
            case OpCode::MulF:
            case OpCode::DivF:
            case OpCode::AddI:
            case OpCode::SubI:
            case OpCode::MulI:
            case OpCode::DivI:
            case OpCode::ModI:
            case OpCode::LtF:
            case OpCode::LeF:
            case OpCode::GtF:
            case OpCode::GeF:
            case OpCode::EqF:
            case OpCode::NeF:
            case OpCode::LtI:
            case OpCode::LeI:
            case OpCode::GtI:
            case OpCode::GeI:
            case OpCode::EqI:
            case OpCode::NeI:
                if (!IsRegister(ScopedInstruction.operand, ScopedRegisters))
                {
                    SetError(InError,
                             "program reads a register out of range at instruction " + std::to_string(ScopedPc));
                    return false;
                }
                break;
            default:
                break;
        }
    }

    for (const FieldBinding& ScopedField : ScopedProgram.fields)
    {
        if (ScopedField.component >= ScopedHeader.componentCount)
        {
            SetError(InError, "program field binding references a missing component");
            return false;
        }
        const ComponentBinding& ScopedComponent = ScopedProgram.components[ScopedField.component];
        if (static_cast<std::uint32_t>(ScopedField.kind) > static_cast<std::uint32_t>(Reflect::FieldKind::UInt64))
        {
            SetError(InError, "program field binding uses an unknown FieldKind");
            return false;
        }
        if (ScopedField.size != 0 &&
            (static_cast<std::uint64_t>(ScopedField.offset) + ScopedField.size > ScopedComponent.size))
        {
            SetError(InError, "program field binding lies outside its component");
            return false;
        }
        if (ScopedField.kind == static_cast<std::uint8_t>(Reflect::FieldKind::String) ||
            ScopedField.kind == static_cast<std::uint8_t>(Reflect::FieldKind::UInt64))
        {
            SetError(InError, "program field binding uses a kind the VM cannot touch (string/uint64)");
            return false;
        }
    }

    for (const FunctionEntry& ScopedFunction : ScopedProgram.functions)
    {
        if (ScopedFunction.codeOffset >= ScopedHeader.codeCount ||
            ScopedFunction.event >= static_cast<std::uint16_t>(ScriptEvent::Count))
        {
            SetError(InError, "program function table entry is invalid");
            return false;
        }
    }

    for (const std::uint16_t ScopedNative : ScopedProgram.natives)
    {
        if (ScopedNative >= kNativeFunctionCount)
        {
            SetError(InError, "program references an unknown native function id");
            return false;
        }
    }

    // Optional debug section, appended right after the tables.
    if ((ScopedHeader.flags & kProgramHeaderFlagDebug) != 0)
    {
        const std::size_t ScopedDebugOffset = ScopedLayout.totalSize;
        if (InStorage.size() < ScopedDebugOffset + sizeof(DebugSectionHeader))
        {
            SetError(InError, "program is flagged as debuggable but its debug header is truncated");
            return false;
        }
        DebugSectionHeader ScopedDebug{};
        std::memcpy(&ScopedDebug, InStorage.data() + ScopedDebugOffset, sizeof(ScopedDebug));
        if (ScopedDebug.Magic != kDebugSectionMagic || ScopedDebug.Version != kDebugSectionVersion)
        {
            SetError(InError, "program debug section has an unsupported magic or version");
            return false;
        }
        if (ScopedDebug.SpanCount > kMaxDebugSpans || ScopedDebug.VariableCount > kMaxDebugVariables ||
            ScopedDebug.NameBytes > kMaxDebugNameBytes)
        {
            SetError(InError, "program debug section exceeds a table capacity");
            return false;
        }
        const std::size_t ScopedSpanOffset = ScopedDebugOffset + sizeof(DebugSectionHeader);
        const std::size_t ScopedVariableOffset = ScopedSpanOffset + ScopedDebug.SpanCount * sizeof(DebugSpan);
        const std::size_t ScopedNameOffset = ScopedVariableOffset + ScopedDebug.VariableCount * sizeof(DebugVariable);
        const std::size_t ScopedDebugEnd = ScopedNameOffset + ScopedDebug.NameBytes;
        if (InStorage.size() < ScopedDebugEnd)
        {
            SetError(InError, "program debug section is shorter than its declared tables");
            return false;
        }
        if (ScopedDebug.SourceNameOffset >= (ScopedDebug.NameBytes == 0 ? 1u : ScopedDebug.NameBytes))
        {
            SetError(InError, "program debug source name offset is out of range");
            return false;
        }
        CompiledDebug ScopedInfo;
        ScopedInfo.spans = {reinterpret_cast<const DebugSpan*>(InStorage.data() + ScopedSpanOffset),
                            ScopedDebug.SpanCount};
        ScopedInfo.variables = {reinterpret_cast<const DebugVariable*>(InStorage.data() + ScopedVariableOffset),
                                ScopedDebug.VariableCount};
        ScopedInfo.names = {reinterpret_cast<const char*>(InStorage.data() + ScopedNameOffset), ScopedDebug.NameBytes};
        std::uint32_t ScopedPcPrevious = 0;
        for (std::size_t ScopedI = 0; ScopedI < ScopedInfo.spans.size(); ++ScopedI)
        {
            if (ScopedInfo.spans[ScopedI].Pc >= ScopedHeader.codeCount)
            {
                SetError(InError, "program debug span points past the code table");
                return false;
            }
            if (ScopedI > 0 && ScopedInfo.spans[ScopedI].Pc <= ScopedPcPrevious)
            {
                SetError(InError, "program debug spans are not strictly ordered by pc");
                return false;
            }
            ScopedPcPrevious = ScopedInfo.spans[ScopedI].Pc;
        }
        for (const DebugVariable& ScopedVariable : ScopedInfo.variables)
        {
            if (ScopedVariable.Register >= ScopedHeader.registerHighWater)
            {
                SetError(InError, "program debug variable binds a register past the high-water mark");
                return false;
            }
            if (ScopedVariable.NameOffset >= ScopedDebug.NameBytes)
            {
                SetError(InError, "program debug variable name offset is out of range");
                return false;
            }
        }
        ScopedInfo.sourceName = ScopedInfo.names.substr(ScopedDebug.SourceNameOffset);
        const std::size_t ScopedTerminator = ScopedInfo.sourceName.find('\0');
        if (ScopedTerminator != std::string_view::npos)
        {
            ScopedInfo.sourceName = ScopedInfo.sourceName.substr(0, ScopedTerminator);
        }
        ScopedInfo.present = true;
        ScopedProgram.debug = ScopedInfo;
        ScopedProgram.storage = InStorage;
    }

    ScopedProgram.valid = true;
    *Out = ScopedProgram;
    return true;
}

// ProgramBuilder
std::uint16_t ProgramBuilder::AddComponent(std::uint64_t InLayoutHash, std::uint32_t InComponentSize)
{
    const auto ScopedIndex = static_cast<std::uint16_t>(MComponents.size());
    ComponentBinding ScopedBinding;
    ScopedBinding.layoutHash = InLayoutHash;
    ScopedBinding.size = InComponentSize;
    ScopedBinding.firstField = static_cast<std::uint16_t>(MFields.size());
    MComponents.push_back(ScopedBinding);
    return ScopedIndex;
}

std::uint16_t ProgramBuilder::AddField(std::uint16_t InComponent, Reflect::FieldKind InKind, std::uint32_t InOffset,
                                       std::uint32_t InFieldSize)
{
    const auto ScopedIndex = static_cast<std::uint16_t>(MFields.size());
    PendingField ScopedField;
    ScopedField.component = InComponent;
    ScopedField.kind = InKind;
    ScopedField.offset = InOffset;
    ScopedField.size = InFieldSize;
    MFields.push_back(ScopedField);
    MComponents[InComponent].fieldCount = static_cast<std::uint16_t>(MComponents[InComponent].fieldCount + 1);
    return ScopedIndex;
}

std::uint16_t ProgramBuilder::AddConstant(float InValue)
{
    const auto ScopedIndex = static_cast<std::uint16_t>(MConstants.size());
    MConstants.push_back(InValue);
    return ScopedIndex;
}

std::uint16_t ProgramBuilder::AddNative(NativeId InNative)
{
    const std::uint16_t ScopedId = static_cast<std::uint16_t>(InNative);
    for (std::uint16_t ScopedI = 0; ScopedI < MNatives.size(); ++ScopedI)
    {
        if (MNatives[ScopedI] == ScopedId)
        {
            return ScopedI;
        }
    }
    MNatives.push_back(ScopedId);
    return static_cast<std::uint16_t>(MNatives.size() - 1);
}

std::uint32_t ProgramBuilder::Emit(OpCode InOp, std::uint8_t InDst, std::uint8_t InSrc, std::uint16_t InOperand,
                                   std::uint8_t InFlags)
{
    Instruction ScopedInstruction;
    ScopedInstruction.op = static_cast<std::uint8_t>(InOp);
    ScopedInstruction.dst = InDst;
    ScopedInstruction.src = InSrc;
    ScopedInstruction.flags = InFlags;
    ScopedInstruction.operand = InOperand;
    TouchRegisters(ScopedInstruction, &MRegisterHighWater);
    MCode.push_back(ScopedInstruction);
    return static_cast<std::uint32_t>(MCode.size() - 1);
}

void ProgramBuilder::PatchJump(std::uint32_t InInstructionIndex, std::uint32_t InTarget)
{
    if (InInstructionIndex < MCode.size())
    {
        MCode[InInstructionIndex].operand = static_cast<std::uint16_t>(InTarget);
    }
}

std::uint16_t ProgramBuilder::AddFunction(ScriptEvent InEvent, std::uint32_t InCodeOffset)
{
    FunctionEntry ScopedEntry;
    ScopedEntry.codeOffset = InCodeOffset;
    ScopedEntry.event = static_cast<std::uint16_t>(InEvent);
    ScopedEntry.registerHighWater = static_cast<std::uint16_t>(MRegisterHighWater);
    MFunctions.push_back(ScopedEntry);
    return static_cast<std::uint16_t>(MFunctions.size() - 1);
}

void ProgramBuilder::SetSourceName(std::string_view InName)
{
    MSourceName.assign(InName);
}

void ProgramBuilder::AddSpan(std::uint32_t InPc, std::uint32_t InLine, std::uint32_t InColumn, std::uint16_t InFlags)
{
    // A node can be marked before its first instruction is emitted, so a span at
    // the current code size is valid: Build drops spans that end up past the
    // last instruction.
    if (InPc > MCode.size())
    {
        return;
    }
    DebugSpan ScopedSpan;
    ScopedSpan.Pc = InPc;
    ScopedSpan.Line = InLine;
    ScopedSpan.Column = InColumn;
    ScopedSpan.Flags = InFlags;
    for (DebugSpan& ScopedExisting : MSpans)
    {
        if (ScopedExisting.Pc == InPc)
        {
            ScopedExisting = ScopedSpan;
            return;
        }
    }
    MSpans.push_back(ScopedSpan);
}

void ProgramBuilder::AddVariable(std::uint8_t InRegister, std::string_view InName, std::uint8_t InKind)
{
    DebugVariable ScopedVariable;
    ScopedVariable.Register = InRegister;
    ScopedVariable.Kind = InKind;
    ScopedVariable.NameOffset = 0;
    MVariables.push_back(ScopedVariable);
    MVariableNames.emplace_back(InName);
}

std::vector<std::byte> ProgramBuilder::Build(std::uint64_t InSourceHash, std::uint64_t InGraphHash,
                                             std::string* InError) const
{
    if (MCode.empty())
    {
        SetError(InError, "program has no code");
        return {};
    }
    if (MCode.size() > kMaxInstructions || MConstants.size() > kMaxConstants || MFields.size() > kMaxFieldBindings ||
        MComponents.size() > kMaxComponentBindings || MFunctions.size() > kMaxFunctions ||
        MNatives.size() > kMaxProgramNatives)
    {
        SetError(InError, "program exceeds a table capacity");
        return {};
    }
    if (MRegisterHighWater > kMaxVmRegisters)
    {
        SetError(InError, "program needs more registers than the VM has");
        return {};
    }
    for (const PendingField& ScopedField : MFields)
    {
        const ComponentBinding& ScopedComponent = MComponents[ScopedField.component];
        if (ScopedField.component >= MComponents.size() ||
            static_cast<std::uint64_t>(ScopedField.offset) + ScopedField.size > ScopedComponent.size)
        {
            SetError(InError, "program field binding lies outside its component");
            return {};
        }
    }

    ProgramHeader ScopedHeader;
    ScopedHeader.magic = kProgramMagic;
    ScopedHeader.version = kProgramVersion;
    ScopedHeader.flags = HasDebugInfo() ? kProgramHeaderFlagDebug : 0;
    ScopedHeader.codeCount = static_cast<std::uint32_t>(MCode.size());
    ScopedHeader.constantCount = static_cast<std::uint32_t>(MConstants.size());
    ScopedHeader.fieldCount = static_cast<std::uint32_t>(MFields.size());
    ScopedHeader.componentCount = static_cast<std::uint32_t>(MComponents.size());
    ScopedHeader.functionCount = static_cast<std::uint32_t>(MFunctions.size());
    ScopedHeader.nativeCount = static_cast<std::uint32_t>(MNatives.size());
    ScopedHeader.registerHighWater = MRegisterHighWater;
    ScopedHeader.sourceHash = InSourceHash;
    ScopedHeader.graphHash = InGraphHash;

    const Layout ScopedLayout = ComputeLayout(ScopedHeader);
    std::vector<std::byte> ScopedBytes(ScopedLayout.totalSize, std::byte{0});
    std::memcpy(ScopedBytes.data(), &ScopedHeader, sizeof(ScopedHeader));
    std::memcpy(ScopedBytes.data() + ScopedLayout.codeOffset, MCode.data(), MCode.size() * sizeof(Instruction));
    if (!MConstants.empty())
    {
        std::memcpy(ScopedBytes.data() + ScopedLayout.constantOffset, MConstants.data(),
                    MConstants.size() * sizeof(float));
    }
    for (std::size_t ScopedI = 0; ScopedI < MFields.size(); ++ScopedI)
    {
        FieldBinding ScopedBinding;
        ScopedBinding.component = MFields[ScopedI].component;
        ScopedBinding.kind = static_cast<std::uint8_t>(MFields[ScopedI].kind);
        ScopedBinding.size = static_cast<std::uint8_t>(std::min<std::uint32_t>(MFields[ScopedI].size, 255u));
        ScopedBinding.offset = MFields[ScopedI].offset;
        std::memcpy(ScopedBytes.data() + ScopedLayout.fieldOffset + ScopedI * sizeof(FieldBinding), &ScopedBinding,
                    sizeof(ScopedBinding));
    }
    if (!MComponents.empty())
    {
        std::memcpy(ScopedBytes.data() + ScopedLayout.componentOffset, MComponents.data(),
                    MComponents.size() * sizeof(ComponentBinding));
    }
    if (!MFunctions.empty())
    {
        std::memcpy(ScopedBytes.data() + ScopedLayout.functionOffset, MFunctions.data(),
                    MFunctions.size() * sizeof(FunctionEntry));
    }
    if (!MNatives.empty())
    {
        std::memcpy(ScopedBytes.data() + ScopedLayout.nativeOffset, MNatives.data(),
                    MNatives.size() * sizeof(std::uint16_t));
    }

    if ((ScopedHeader.flags & kProgramHeaderFlagDebug) != 0)
    {
        std::vector<DebugSpan> ScopedSpans = MSpans;
        std::sort(ScopedSpans.begin(), ScopedSpans.end(),
                  [](const DebugSpan& InLeft, const DebugSpan& InRight)
                  {
                      return InLeft.Pc < InRight.Pc;
                  });
        ScopedSpans.erase(std::remove_if(ScopedSpans.begin(), ScopedSpans.end(),
                                         [&ScopedHeader](const DebugSpan& InSpan)
                                         {
                                             return InSpan.Pc >= ScopedHeader.codeCount;
                                         }),
                          ScopedSpans.end());
        if (ScopedSpans.size() > kMaxDebugSpans || MVariables.size() > kMaxDebugVariables)
        {
            SetError(InError, "program debug info exceeds a table capacity");
            return {};
        }
        std::string ScopedNames;
        if (MSourceName.empty())
        {
            ScopedNames = "<script>";
        }
        else
        {
            ScopedNames = MSourceName;
        }
        ScopedNames.push_back('\0');
        std::vector<DebugVariable> ScopedVariables = MVariables;
        for (std::size_t ScopedI = 0; ScopedI < ScopedVariables.size(); ++ScopedI)
        {
            ScopedVariables[ScopedI].NameOffset = static_cast<std::uint16_t>(ScopedNames.size());
            ScopedNames += MVariableNames[ScopedI];
            ScopedNames.push_back('\0');
        }
        if (ScopedNames.size() > kMaxDebugNameBytes)
        {
            SetError(InError, "program debug names exceed the name blob");
            return {};
        }
        std::size_t ScopedDebugSize = sizeof(DebugSectionHeader) + ScopedSpans.size() * sizeof(DebugSpan) +
                                      ScopedVariables.size() * sizeof(DebugVariable) + ScopedNames.size();
        const std::size_t ScopedDebugStart = ScopedLayout.totalSize;
        ScopedBytes.resize(ScopedDebugStart + ScopedDebugSize, std::byte{0});
        DebugSectionHeader ScopedSection;
        ScopedSection.SpanCount = static_cast<std::uint32_t>(ScopedSpans.size());
        ScopedSection.VariableCount = static_cast<std::uint32_t>(ScopedVariables.size());
        ScopedSection.NameBytes = static_cast<std::uint32_t>(ScopedNames.size());
        ScopedSection.SourceNameOffset = 0;
        std::memcpy(ScopedBytes.data() + ScopedDebugStart, &ScopedSection, sizeof(ScopedSection));
        if (!ScopedSpans.empty())
        {
            std::memcpy(ScopedBytes.data() + ScopedDebugStart + sizeof(DebugSectionHeader), ScopedSpans.data(),
                        ScopedSpans.size() * sizeof(DebugSpan));
        }
        const std::size_t ScopedVariableStart =
            ScopedDebugStart + sizeof(DebugSectionHeader) + ScopedSpans.size() * sizeof(DebugSpan);
        if (!ScopedVariables.empty())
        {
            std::memcpy(ScopedBytes.data() + ScopedVariableStart, ScopedVariables.data(),
                        ScopedVariables.size() * sizeof(DebugVariable));
        }
        const std::size_t ScopedNameStart = ScopedVariableStart + ScopedVariables.size() * sizeof(DebugVariable);
        std::memcpy(ScopedBytes.data() + ScopedNameStart, ScopedNames.data(), ScopedNames.size());
    }

    // A builder bug must surface here, not as a VM fault in a shipped build.
    CompiledProgram ScopedValidated;
    if (!ParseProgram(ScopedBytes, &ScopedValidated, InError))
    {
        return {};
    }
    return ScopedBytes;
}

bool CompiledDebug::Locate(std::uint32_t InPc, DebugSpan* OutSpan) const noexcept
{
    if (spans.empty() || OutSpan == nullptr)
    {
        return false;
    }
    std::size_t ScopedLow = 0;
    std::size_t ScopedHigh = spans.size();
    while (ScopedLow < ScopedHigh)
    {
        const std::size_t ScopedMid = (ScopedLow + ScopedHigh) / 2;
        if (spans[ScopedMid].Pc <= InPc)
        {
            ScopedLow = ScopedMid + 1;
        }
        else
        {
            ScopedHigh = ScopedMid;
        }
    }
    if (ScopedLow == 0)
    {
        return false;
    }
    *OutSpan = spans[ScopedLow - 1];
    return true;
}

std::uint64_t HashProgramBytes(std::span<const std::byte> InCooked) noexcept
{
    constexpr std::uint64_t kOffsetBasis = 0xCBF29CE484222325ull;
    constexpr std::uint64_t kPrime = 0x100000001B3ull;
    std::uint64_t ScopedHash = kOffsetBasis;
    for (const std::byte ScopedByte : InCooked)
    {
        ScopedHash ^= static_cast<std::uint64_t>(static_cast<unsigned>(ScopedByte));
        ScopedHash *= kPrime;
    }
    return ScopedHash;
}
}
