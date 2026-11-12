// FemboyEngine2D - FemboyScript bytecode -> C++26 transpiler.
#include "Engine/Script/AotTranspiler.hpp"

#include "Engine/Script/AotAbi.hpp"

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace Fby::Script
{
namespace
{
// Sizes the destination from the actual formatted length, so a long symbol or
// program name cannot silently truncate generated C++.
void AppendFormat(std::string& Out, const char* InFormat, ...)
{
    va_list ScopedArguments;
    va_start(ScopedArguments, InFormat);
    va_list ScopedCopy;
    va_copy(ScopedCopy, ScopedArguments);
    const int ScopedNeeded = std::vsnprintf(nullptr, 0, InFormat, ScopedCopy);
    va_end(ScopedCopy);
    if (ScopedNeeded <= 0)
    {
        va_end(ScopedArguments);
        return;
    }
    const std::size_t ScopedOldSize = Out.size();
    Out.resize(ScopedOldSize + static_cast<std::size_t>(ScopedNeeded) + 1);
    const int ScopedWritten = std::vsnprintf(Out.data() + ScopedOldSize, static_cast<std::size_t>(ScopedNeeded) + 1,
                                             InFormat, ScopedArguments);
    va_end(ScopedArguments);
    if (ScopedWritten < 0)
    {
        Out.resize(ScopedOldSize);
        return;
    }
    Out.resize(ScopedOldSize + static_cast<std::size_t>(ScopedWritten));
}

[[nodiscard]] std::string Hex64(std::uint64_t InValue)
{
    char ScopedBuffer[32] = {};
    std::snprintf(ScopedBuffer, sizeof(ScopedBuffer), "0x%016llXull", static_cast<unsigned long long>(InValue));
    return ScopedBuffer;
}

[[nodiscard]] std::string FloatLiteral(float InValue)
{
    if (std::isnan(InValue))
    {
        return "std::numeric_limits<float>::quiet_NaN()";
    }
    if (std::isinf(InValue))
    {
        return InValue < 0.0f ? "-std::numeric_limits<float>::infinity()"
                              : "std::numeric_limits<float>::infinity()";
    }
    char ScopedBuffer[64] = {};
    std::snprintf(ScopedBuffer, sizeof(ScopedBuffer), "%.9g", static_cast<double>(InValue));
    std::string ScopedText = ScopedBuffer;
    // "2" alone is an integer literal; a float literal needs a decimal point or
    // an exponent before the suffix.
    if (ScopedText.find_first_of(".eE") == std::string::npos)
    {
        ScopedText += ".0";
    }
    ScopedText += "f";
    return ScopedText;
}

[[nodiscard]] std::string U32Literal(std::uint32_t InValue)
{
    char ScopedBuffer[32] = {};
    std::snprintf(ScopedBuffer, sizeof(ScopedBuffer), "%uull", InValue);
    return ScopedBuffer;
}

// Opcodes that always write a numeric payload, so their result tag is reset.
[[nodiscard]] constexpr bool WritesNumericResult(OpCode InOp) noexcept
{
    switch (InOp)
    {
        case OpCode::LoadF32:
        case OpCode::LoadI32:
        case OpCode::AddF:
        case OpCode::SubF:
        case OpCode::MulF:
        case OpCode::DivF:
        case OpCode::AddI:
        case OpCode::SubI:
        case OpCode::MulI:
        case OpCode::DivI:
        case OpCode::ModI:
        case OpCode::NegF:
        case OpCode::NegI:
        case OpCode::AndI:
        case OpCode::OrI:
        case OpCode::NotI:
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
        case OpCode::LoadSelf:
        case OpCode::LoadFieldF:
        case OpCode::LoadFieldI:
        case OpCode::LoadFieldVec2:
        case OpCode::SpawnPrefab:
        case OpCode::DespawnEntity:
        case OpCode::RandomF:
        case OpCode::RandomRangeF:
            return true;
        default:
            return false;
    }
}

[[nodiscard]] constexpr bool IsHeapNative(NativeId InNative) noexcept
{
    switch (InNative)
    {
        case NativeId::ArrayNew:
        case NativeId::ArrayPush:
        case NativeId::ArraySet:
        case NativeId::ArrayGet:
        case NativeId::ArrayLength:
        case NativeId::MapNew:
        case NativeId::MapSet:
        case NativeId::MapGet:
        case NativeId::MapHas:
        case NativeId::MapLength:
        case NativeId::StringConcat:
        case NativeId::StringLength:
        case NativeId::StringEquals:
            return true;
        default:
            return false;
    }
}

// Every byte is emitted as a hex escape; the next escape always starts with a
// backslash, so adjacent escapes cannot merge into one longer hex sequence.
[[nodiscard]] std::string EscapeStringLiteral(std::string_view InText)
{
    std::string ScopedLiteral = "\"";
    for (const unsigned char ScopedCharacter : InText)
    {
        char ScopedBuffer[8] = {};
        std::snprintf(ScopedBuffer, sizeof(ScopedBuffer), "\\x%02X", ScopedCharacter);
        ScopedLiteral += ScopedBuffer;
    }
    ScopedLiteral += "\"";
    return ScopedLiteral;
}

[[nodiscard]] const char* EntryName(ScriptEvent InEvent) noexcept
{
    switch (InEvent)
    {
        case ScriptEvent::Start:
            return "start";
        case ScriptEvent::Update:
            return "update";
        case ScriptEvent::FixedUpdate:
            return "fixedUpdate";
        default:
            return nullptr;
    }
}

// Emits one event function. Returns false when the program contains something
class FunctionEmitter
{
public:
    FunctionEmitter(const CompiledProgram& InProgram, std::string& Out) : MProgram(InProgram), MOut(Out)
    {
    }

    [[nodiscard]] bool Emit(const FunctionEntry& InFunction, std::uint32_t InIndex, std::string* InError)
    {
        const auto& ScopedCode = MProgram.code;
        if (InFunction.codeOffset >= ScopedCode.size())
        {
            *InError = "function entry points past the end of the code";
            return false;
        }
        const bool ScopedUserFunction = (InFunction.event & kUserFunctionFlag) != 0;

        // Events end at Halt; a user function ends at Return.
        std::uint32_t ScopedEnd = InFunction.codeOffset;
        for (; ScopedEnd < ScopedCode.size(); ++ScopedEnd)
        {
            const OpCode ScopedOp = ScopedCode[ScopedEnd].Code();
            if ((ScopedUserFunction && ScopedOp == OpCode::Return) || (!ScopedUserFunction && ScopedOp == OpCode::Halt))
            {
                break;
            }
        }
        if (ScopedEnd >= ScopedCode.size())
        {
            *InError = ScopedUserFunction ? "user function has no Return" : "event function has no Halt";
            return false;
        }

        MCurrentBase = InFunction.registerHighWater;

        MUsedComponents.assign(MProgram.components.size(), false);
        for (std::uint32_t ScopedPc = InFunction.codeOffset; ScopedPc <= ScopedEnd; ++ScopedPc)
        {
            const Instruction& ScopedInstruction = ScopedCode[ScopedPc];
            if (ScopedInstruction.Code() == OpCode::LoadFieldF || ScopedInstruction.Code() == OpCode::LoadFieldI ||
                ScopedInstruction.Code() == OpCode::LoadFieldVec2 || ScopedInstruction.Code() == OpCode::StoreFieldF ||
                ScopedInstruction.Code() == OpCode::StoreFieldI || ScopedInstruction.Code() == OpCode::StoreFieldVec2)
            {
                const std::uint16_t ScopedComponent = MProgram.fields[ScopedInstruction.operand].component;
                if (ScopedComponent >= MUsedComponents.size())
                {
                    *InError = "field references a component that is not declared";
                    return false;
                }
                MUsedComponents[ScopedComponent] = true;
            }
        }

        if (ScopedUserFunction)
        {
            AppendFormat(MOut, "void fby_fn_%u(::Fby::Script::Aot::Context& ctx, Registers& r) noexcept\n{\n", InIndex);
        }
        else
        {
            const char* ScopedName = EntryName(static_cast<ScriptEvent>(InFunction.event));
            if (ScopedName == nullptr)
            {
                *InError = "unknown script event id";
                return false;
            }
            AppendFormat(MOut, "void fby_entry_%s(::Fby::Script::Aot::Context& ctx) noexcept\n{\n", ScopedName);
            MOut += "    Registers r{};\n";
            MOut += "    (void)r;\n";
        }
        for (std::uint32_t ScopedComponent = 0; ScopedComponent < MUsedComponents.size(); ++ScopedComponent)
        {
            if (!MUsedComponents[ScopedComponent])
            {
                continue;
            }
            AppendFormat(MOut, "    std::byte* c%u = ctx.Component(%s);\n", ScopedComponent,
                         Hex64(MProgram.components[ScopedComponent].layoutHash).c_str());
            AppendFormat(MOut, "    if (c%u == nullptr) { ctx.Fault(%u, %u); return; }\n", ScopedComponent,
                         Aot::FaultCode::MissingComponent, InFunction.codeOffset);
        }
        MOut += "\n";

        std::vector<bool> ScopedLabels(ScopedCode.size(), false);
        for (std::uint32_t ScopedPc = InFunction.codeOffset; ScopedPc <= ScopedEnd; ++ScopedPc)
        {
            const OpCode ScopedOp = ScopedCode[ScopedPc].Code();
            if (ScopedOp == OpCode::Jump || ScopedOp == OpCode::JumpIfZero || ScopedOp == OpCode::JumpIfNonZero)
            {
                const std::uint32_t ScopedTarget = ScopedCode[ScopedPc].operand;
                if (ScopedTarget < InFunction.codeOffset || ScopedTarget > ScopedEnd)
                {
                    *InError = "jump crosses an event-function boundary";
                    return false;
                }
                ScopedLabels[ScopedTarget] = true;
            }
        }

        for (std::uint32_t ScopedPc = InFunction.codeOffset; ScopedPc <= ScopedEnd; ++ScopedPc)
        {
            if (ScopedLabels[ScopedPc])
            {
                AppendFormat(MOut, "L%u:;\n", ScopedPc);
            }
            const Instruction& ScopedInstruction = ScopedCode[ScopedPc];
            if (!EmitInstruction(ScopedInstruction, ScopedPc, InError))
            {
                return false;
            }
        }
        MOut += "}\n\n";
        return true;
    }

private:
    [[nodiscard]] bool EmitInstruction(const Instruction& InInstruction, std::uint32_t InPc, std::string* InError)
    {
        const OpCode ScopedOp = InInstruction.Code();
        const auto ScopedD = static_cast<std::uint32_t>(InInstruction.dst);
        const auto ScopedS = static_cast<std::uint32_t>(InInstruction.src);
        const auto ScopedO = static_cast<std::uint32_t>(InInstruction.operand);

        if (WritesNumericResult(ScopedOp))
        {
            AppendFormat(MOut, "    r.tag[%u] = ::Fby::Script::Aot::Tag::Float;\n", ScopedD);
            if (ScopedOp == OpCode::LoadFieldVec2 && ScopedD + 1 < 128)
            {
                AppendFormat(MOut, "    r.tag[%u] = ::Fby::Script::Aot::Tag::Float;\n", ScopedD + 1);
            }
        }

        switch (ScopedOp)
        {
            case OpCode::Nop:
                MOut += "    ;\n";
                return true;
            case OpCode::Halt:
                MOut += "    return;\n";
                return true;
            case OpCode::LoadF32:
                AppendFormat(MOut, "    r.f[%u] = %s;\n", ScopedD,
                             FloatLiteral(MProgram.constants[InInstruction.operand]).c_str());
                return true;
            case OpCode::LoadI32:
                // The operand is a signed 16-bit immediate, exactly like the VM.
                AppendFormat(MOut, "    r.i[%u] = %d;\n", ScopedD,
                             static_cast<int>(static_cast<std::int16_t>(InInstruction.operand)));
                return true;
            case OpCode::LoadString:
            {
                std::string_view ScopedText;
                if (!MProgram.StringAt(InInstruction.operand, &ScopedText))
                {
                    *InError = "LoadString references a missing string";
                    return false;
                }
                AppendFormat(MOut, "    r.u[%u] = ctx.InternString(%s, %u);\n", ScopedD,
                             EscapeStringLiteral(ScopedText).c_str(), static_cast<std::uint32_t>(ScopedText.size()));
                AppendFormat(MOut,
                             "    if (r.u[%u] == 0xFFFFFFFFu) { ctx.Fault(%u, %u); return; }\n", ScopedD,
                             Aot::FaultCode::HeapFull, InPc);
                AppendFormat(MOut, "    r.tag[%u] = ::Fby::Script::Aot::Tag::String;\n", ScopedD);
                return true;
            }
            case OpCode::MoveF:
                AppendFormat(MOut, "    r.f[%u] = r.f[%u];\n", ScopedD, ScopedS);
                AppendFormat(MOut, "    r.tag[%u] = r.tag[%u];\n", ScopedD, ScopedS);
                return true;
            case OpCode::MoveI:
                AppendFormat(MOut, "    r.u[%u] = r.u[%u];\n", ScopedD, ScopedS);
                AppendFormat(MOut, "    r.tag[%u] = r.tag[%u];\n", ScopedD, ScopedS);
                return true;
            case OpCode::AddF:
                AppendFormat(MOut, "    r.f[%u] = r.f[%u] + r.f[%u];\n", ScopedD, ScopedS, ScopedO);
                return true;
            case OpCode::SubF:
                AppendFormat(MOut, "    r.f[%u] = r.f[%u] - r.f[%u];\n", ScopedD, ScopedS, ScopedO);
                return true;
            case OpCode::MulF:
                AppendFormat(MOut, "    r.f[%u] = r.f[%u] * r.f[%u];\n", ScopedD, ScopedS, ScopedO);
                return true;
            case OpCode::DivF:
                AppendFormat(MOut, "    r.f[%u] = r.f[%u] / r.f[%u];\n", ScopedD, ScopedS, ScopedO);
                return true;
            case OpCode::AddI:
                AppendFormat(MOut, "    r.u[%u] = r.u[%u] + r.u[%u];\n", ScopedD, ScopedS, ScopedO);
                return true;
            case OpCode::SubI:
                AppendFormat(MOut, "    r.u[%u] = r.u[%u] - r.u[%u];\n", ScopedD, ScopedS, ScopedO);
                return true;
            case OpCode::MulI:
                AppendFormat(MOut, "    r.u[%u] = r.u[%u] * r.u[%u];\n", ScopedD, ScopedS, ScopedO);
                return true;
            case OpCode::DivI:
                AppendFormat(MOut, "    if (r.u[%u] == 0u) { ctx.Fault(%u, %u); return; }\n", ScopedO,
                             Aot::FaultCode::DivisionByZero, InPc);
                AppendFormat(MOut,
                             "    r.u[%u] = (r.u[%u] == 0x80000000u && r.u[%u] == 0xFFFFFFFFu) ? 0x80000000u : "
                             "static_cast<std::uint32_t>(static_cast<std::int32_t>(r.u[%u]) / "
                             "static_cast<std::int32_t>(r.u[%u]));\n",
                             ScopedD, ScopedS, ScopedO, ScopedS, ScopedO);
                return true;
            case OpCode::ModI:
                AppendFormat(MOut, "    if (r.u[%u] == 0u) { ctx.Fault(%u, %u); return; }\n", ScopedO,
                             Aot::FaultCode::DivisionByZero, InPc);
                AppendFormat(MOut,
                             "    r.u[%u] = (r.u[%u] == 0x80000000u && r.u[%u] == 0xFFFFFFFFu) ? 0u : "
                             "static_cast<std::uint32_t>(static_cast<std::int32_t>(r.u[%u]) %% "
                             "static_cast<std::int32_t>(r.u[%u]));\n",
                             ScopedD, ScopedS, ScopedO, ScopedS, ScopedO);
                return true;
            case OpCode::NegF:
                AppendFormat(MOut, "    r.f[%u] = -r.f[%u];\n", ScopedD, ScopedS);
                return true;
            case OpCode::NegI:
                AppendFormat(MOut, "    r.u[%u] = 0u - r.u[%u];\n", ScopedD, ScopedS);
                return true;
            case OpCode::AndI:
                AppendFormat(MOut, "    r.u[%u] = r.u[%u] & r.u[%u];\n", ScopedD, ScopedS, ScopedO);
                return true;
            case OpCode::OrI:
                AppendFormat(MOut, "    r.u[%u] = r.u[%u] | r.u[%u];\n", ScopedD, ScopedS, ScopedO);
                return true;
            case OpCode::NotI:
                AppendFormat(MOut, "    r.u[%u] = ~r.u[%u];\n", ScopedD, ScopedS);
                return true;

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
            {
                const char* ScopedOperatorText = "<";
                bool ScopedIsFloat = true;
                switch (ScopedOp)
                {
                    case OpCode::LtF:
                        ScopedOperatorText = "<";
                        break;
                    case OpCode::LeF:
                        ScopedOperatorText = "<=";
                        break;
                    case OpCode::GtF:
                        ScopedOperatorText = ">";
                        break;
                    case OpCode::GeF:
                        ScopedOperatorText = ">=";
                        break;
                    case OpCode::EqF:
                        ScopedOperatorText = "==";
                        break;
                    case OpCode::NeF:
                        ScopedOperatorText = "!=";
                        break;
                    case OpCode::LtI:
                        ScopedOperatorText = "<";
                        ScopedIsFloat = false;
                        break;
                    case OpCode::LeI:
                        ScopedOperatorText = "<=";
                        ScopedIsFloat = false;
                        break;
                    case OpCode::GtI:
                        ScopedOperatorText = ">";
                        ScopedIsFloat = false;
                        break;
                    case OpCode::GeI:
                        ScopedOperatorText = ">=";
                        ScopedIsFloat = false;
                        break;
                    case OpCode::EqI:
                        ScopedOperatorText = "==";
                        ScopedIsFloat = false;
                        break;
                    default:
                        ScopedOperatorText = "!=";
                        ScopedIsFloat = false;
                        break;
                }
                if (ScopedIsFloat)
                {
                    AppendFormat(MOut, "    r.u[%u] = (r.f[%u] %s r.f[%u]) ? 1u : 0u;\n", ScopedD, ScopedS,
                                 ScopedOperatorText, ScopedO);
                }
                else if (ScopedOp == OpCode::EqI || ScopedOp == OpCode::NeI)
                {
                    AppendFormat(MOut, "    r.u[%u] = (r.u[%u] %s r.u[%u]) ? 1u : 0u;\n", ScopedD, ScopedS,
                                 ScopedOperatorText, ScopedO);
                }
                else
                {
                    AppendFormat(MOut,
                                 "    r.u[%u] = (static_cast<std::int32_t>(r.u[%u]) %s "
                                 "static_cast<std::int32_t>(r.u[%u])) ? 1u : 0u;\n",
                                 ScopedD, ScopedS, ScopedOperatorText, ScopedO);
                }
                return true;
            }

            case OpCode::Jump:
                AppendFormat(MOut, "    goto L%u;\n", ScopedO);
                return true;
            case OpCode::JumpIfZero:
                AppendFormat(MOut, "    if (r.u[%u] == 0u) goto L%u;\n", ScopedD, ScopedO);
                return true;
            case OpCode::JumpIfNonZero:
                AppendFormat(MOut, "    if (r.u[%u] != 0u) goto L%u;\n", ScopedD, ScopedO);
                return true;

            case OpCode::LoadSelf:
                AppendFormat(MOut, "    r.u[%u] = ctx.self;\n", ScopedD);
                return true;

            case OpCode::LoadFieldF:
            {
                const FieldBinding& ScopedField = MProgram.fields[InInstruction.operand];
                AppendFormat(MOut, "    r.f[%u] = ::Fby::Script::Aot::Load<float>(c%u, %u);\n", ScopedD,
                             ScopedField.component, ScopedField.offset);
                return true;
            }
            case OpCode::LoadFieldI:
            {
                const FieldBinding& ScopedField = MProgram.fields[InInstruction.operand];
                const char* ScopedType = ScopedField.size == 1
                                             ? "std::uint8_t"
                                             : (ScopedField.size == 2 ? "std::uint16_t" : "std::uint32_t");
                AppendFormat(MOut, "    r.u[%u] = static_cast<std::uint32_t>(::Fby::Script::Aot::Load<%s>(c%u, %u));\n",
                             ScopedD, ScopedType, ScopedField.component, ScopedField.offset);
                return true;
            }
            case OpCode::LoadFieldVec2:
            {
                const FieldBinding& ScopedField = MProgram.fields[InInstruction.operand];
                AppendFormat(MOut, "    r.f[%u] = ::Fby::Script::Aot::Load<float>(c%u, %u);\n", ScopedD,
                             ScopedField.component, ScopedField.offset);
                AppendFormat(MOut, "    r.f[%u] = ::Fby::Script::Aot::Load<float>(c%u, %u);\n", ScopedD + 1,
                             ScopedField.component, ScopedField.offset + 4);
                return true;
            }
            case OpCode::StoreFieldF:
            {
                const FieldBinding& ScopedField = MProgram.fields[InInstruction.operand];
                AppendFormat(MOut, "    ::Fby::Script::Aot::Store<float>(c%u, %u, r.f[%u]);\n", ScopedField.component,
                             ScopedField.offset, ScopedS);
                return true;
            }
            case OpCode::StoreFieldI:
            {
                const FieldBinding& ScopedField = MProgram.fields[InInstruction.operand];
                const char* ScopedType = ScopedField.size == 1
                                             ? "std::uint8_t"
                                             : (ScopedField.size == 2 ? "std::uint16_t" : "std::uint32_t");
                AppendFormat(MOut, "    ::Fby::Script::Aot::Store<%s>(c%u, %u, static_cast<%s>(r.u[%u]));\n",
                             ScopedType, ScopedField.component, ScopedField.offset, ScopedType, ScopedS);
                return true;
            }
            case OpCode::StoreFieldVec2:
            {
                const FieldBinding& ScopedField = MProgram.fields[InInstruction.operand];
                AppendFormat(MOut, "    ::Fby::Script::Aot::Store<float>(c%u, %u, r.f[%u]);\n", ScopedField.component,
                             ScopedField.offset, ScopedS);
                AppendFormat(MOut, "    ::Fby::Script::Aot::Store<float>(c%u, %u, r.f[%u]);\n", ScopedField.component,
                             ScopedField.offset + 4, ScopedS + 1);
                return true;
            }

            case OpCode::CallNative:
            {
                if (InInstruction.operand >= MProgram.natives.size())
                {
                    *InError = "native call references an out-of-range id";
                    return false;
                }
                const auto ScopedNative = static_cast<NativeId>(MProgram.natives[InInstruction.operand]);
                if (!IsHeapNative(ScopedNative))
                {
                    AppendFormat(MOut, "    r.tag[%u] = ::Fby::Script::Aot::Tag::Float;\n", ScopedD);
                }
                switch (ScopedNative)
                {
                    case NativeId::LogFloat:
                        AppendFormat(MOut, "    ctx.LogFloat(r.f[%u]);\n", ScopedD);
                        break;
                    case NativeId::LogInt:
                        // The VM logs a signed int32; a plugin must print the same.
                        AppendFormat(MOut, "    ctx.LogI32(r.i[%u]);\n", ScopedD);
                        break;
                    case NativeId::LogMessage:
                        AppendFormat(MOut, "    ctx.LogU32(r.u[%u]);\n", ScopedD);
                        break;
                    case NativeId::LogVec2:
                        AppendFormat(MOut, "    ctx.LogVec2(r.f[%u], r.f[%u]);\n", ScopedD, ScopedD + 1);
                        break;
                    case NativeId::DespawnSelf:
                        // The VM writes 1/0 into the native result register, so a
                        // graph that branches on it behaves the same in a plugin.
                        AppendFormat(MOut, "    r.u[%u] = ctx.Despawn(r.u[%u]);\n", ScopedD, ScopedD);
                        break;
                    case NativeId::SpawnPrefab:
                        AppendFormat(MOut, "    r.u[%u] = ctx.Spawn(r.u[%u]);\n", ScopedD, ScopedD);
                        break;
                    case NativeId::RandomFloat:
                        AppendFormat(MOut, "    r.f[%u] = ctx.RandomFloat();\n", ScopedD);
                        break;
                    case NativeId::RandomRange:
                        AppendFormat(MOut, "    r.f[%u] = ctx.RandomFloat() * r.f[%u];\n", ScopedD, ScopedD + 1);
                        break;
                    case NativeId::PlaySound:
                        MOut += "    ;\n";
                        break;
                    case NativeId::GetDeltaTime:
                        AppendFormat(MOut, "    r.f[%u] = ctx.dt;\n", ScopedD);
                        break;
                    case NativeId::GetFrameIndex:
                        AppendFormat(MOut, "    r.u[%u] = static_cast<std::uint32_t>(ctx.frameIndex);\n", ScopedD);
                        break;
                    case NativeId::BigIntFromInt:
                        AppendFormat(MOut,
                                     "    r.f[%u] = static_cast<float>(ctx.BigIntFromInt(static_cast<std::int32_t>(r.f[%u])));\n",
                                     ScopedD, ScopedD);
                        break;
                    case NativeId::BigIntAdd:
                        AppendFormat(MOut,
                                     "    r.f[%u] = static_cast<float>(ctx.BigIntAdd(static_cast<std::uint32_t>(r.f[%u]), "
                                     "static_cast<std::uint32_t>(r.f[%u])));\n",
                                     ScopedD, ScopedD, ScopedD + 1);
                        break;
                    case NativeId::BigIntSub:
                        AppendFormat(MOut,
                                     "    r.f[%u] = static_cast<float>(ctx.BigIntSub(static_cast<std::uint32_t>(r.f[%u]), "
                                     "static_cast<std::uint32_t>(r.f[%u])));\n",
                                     ScopedD, ScopedD, ScopedD + 1);
                        break;
                    case NativeId::BigIntMul:
                        AppendFormat(MOut,
                                     "    r.f[%u] = static_cast<float>(ctx.BigIntMul(static_cast<std::uint32_t>(r.f[%u]), "
                                     "static_cast<std::uint32_t>(r.f[%u])));\n",
                                     ScopedD, ScopedD, ScopedD + 1);
                        break;
                    case NativeId::BigIntDiv:
                        AppendFormat(MOut,
                                     "    r.f[%u] = static_cast<float>(ctx.BigIntDiv(static_cast<std::uint32_t>(r.f[%u]), "
                                     "static_cast<std::uint32_t>(r.f[%u])));\n",
                                     ScopedD, ScopedD, ScopedD + 1);
                        break;
                    case NativeId::BigIntMod:
                        AppendFormat(MOut,
                                     "    r.f[%u] = static_cast<float>(ctx.BigIntMod(static_cast<std::uint32_t>(r.f[%u]), "
                                     "static_cast<std::uint32_t>(r.f[%u])));\n",
                                     ScopedD, ScopedD, ScopedD + 1);
                        break;
                    case NativeId::BigIntCmp:
                        AppendFormat(MOut,
                                     "    r.f[%u] = static_cast<float>(ctx.BigIntCmp(static_cast<std::uint32_t>(r.f[%u]), "
                                     "static_cast<std::uint32_t>(r.f[%u])));\n",
                                     ScopedD, ScopedD, ScopedD + 1);
                        break;
                    case NativeId::BigIntToInt:
                        AppendFormat(MOut,
                                     "    r.f[%u] = static_cast<float>(ctx.BigIntToInt(static_cast<std::uint32_t>(r.f[%u])));\n",
                                     ScopedD, ScopedD);
                        break;
                    case NativeId::BigIntLog:
                        AppendFormat(MOut, "    ctx.BigIntLog(static_cast<std::uint32_t>(r.f[%u]));\n", ScopedD);
                        break;
                    case NativeId::BigIntRelease:
                        AppendFormat(MOut, "    ctx.BigIntRelease(static_cast<std::uint32_t>(r.f[%u]));\n",
                                     ScopedD);
                        break;
                    case NativeId::DecimalFromInt:
                        AppendFormat(MOut,
                                     "    r.f[%u] = static_cast<float>(ctx.DecimalFromInt(static_cast<std::int32_t>(r.f[%u])));\n",
                                     ScopedD, ScopedD);
                        break;
                    case NativeId::DecimalAdd:
                        AppendFormat(MOut,
                                     "    r.f[%u] = static_cast<float>(ctx.DecimalAdd(static_cast<std::uint32_t>(r.f[%u]), "
                                     "static_cast<std::uint32_t>(r.f[%u])));\n",
                                     ScopedD, ScopedD, ScopedD + 1);
                        break;
                    case NativeId::DecimalSub:
                        AppendFormat(MOut,
                                     "    r.f[%u] = static_cast<float>(ctx.DecimalSub(static_cast<std::uint32_t>(r.f[%u]), "
                                     "static_cast<std::uint32_t>(r.f[%u])));\n",
                                     ScopedD, ScopedD, ScopedD + 1);
                        break;
                    case NativeId::DecimalMul:
                        AppendFormat(MOut,
                                     "    r.f[%u] = static_cast<float>(ctx.DecimalMul(static_cast<std::uint32_t>(r.f[%u]), "
                                     "static_cast<std::uint32_t>(r.f[%u])));\n",
                                     ScopedD, ScopedD, ScopedD + 1);
                        break;
                    case NativeId::DecimalDiv:
                        AppendFormat(MOut,
                                     "    r.f[%u] = static_cast<float>(ctx.DecimalDiv(static_cast<std::uint32_t>(r.f[%u]), "
                                     "static_cast<std::uint32_t>(r.f[%u])));\n",
                                     ScopedD, ScopedD, ScopedD + 1);
                        break;
                    case NativeId::DecimalDivRounded:
                        AppendFormat(MOut,
                                     "    r.f[%u] = static_cast<float>(ctx.DecimalDivRounded("
                                     "static_cast<std::uint32_t>(r.f[%u]), static_cast<std::uint32_t>(r.f[%u]), "
                                     "static_cast<std::int32_t>(r.f[%u])));\n",
                                     ScopedD, ScopedD, ScopedD + 1, ScopedD + 2);
                        break;
                    case NativeId::DecimalCmp:
                        AppendFormat(MOut,
                                     "    r.f[%u] = static_cast<float>(ctx.DecimalCmp(static_cast<std::uint32_t>(r.f[%u]), "
                                     "static_cast<std::uint32_t>(r.f[%u])));\n",
                                     ScopedD, ScopedD, ScopedD + 1);
                        break;
                    case NativeId::DecimalToInt:
                        AppendFormat(MOut,
                                     "    r.f[%u] = static_cast<float>(ctx.DecimalToInt(static_cast<std::uint32_t>(r.f[%u])));\n",
                                     ScopedD, ScopedD);
                        break;
                    case NativeId::DecimalLog:
                        AppendFormat(MOut, "    ctx.DecimalLog(static_cast<std::uint32_t>(r.f[%u]));\n", ScopedD);
                        break;
                    case NativeId::DecimalRelease:
                        AppendFormat(MOut, "    ctx.DecimalRelease(static_cast<std::uint32_t>(r.f[%u]));\n",
                                     ScopedD);
                        break;
                    case NativeId::PhysicsGetBackend:
                        AppendFormat(MOut, "    r.f[%u] = static_cast<float>(ctx.PhysicsGetBackend());\n", ScopedD);
                        break;
                    case NativeId::PhysicsSetBackend:
                        AppendFormat(MOut, "    r.f[%u] = static_cast<float>(ctx.PhysicsSetBackend(static_cast<std::int32_t>(r.f[%u])));\n",
                                     ScopedD, ScopedD + 1);
                        break;
                    case NativeId::PhysicsGetCapabilities:
                        AppendFormat(MOut, "    r.f[%u] = static_cast<float>(ctx.PhysicsGetCapabilities());\n", ScopedD);
                        break;
                    case NativeId::CameraAddTrauma:
                        AppendFormat(MOut, "    ctx.CameraAddTrauma(r.f[%u]);\n", ScopedD);
                        AppendFormat(MOut, "    r.f[%u] = 1.0f;\n", ScopedD);
                        break;
                    case NativeId::CameraDistort:
                        AppendFormat(MOut, "    ctx.CameraDistort(static_cast<std::uint32_t>(r.f[%u]), r.f[%u], r.f[%u], r.f[%u], r.f[%u]);\n",
                                     ScopedD, ScopedD + 1, ScopedD + 2, ScopedD + 3, ScopedD + 4);
                        AppendFormat(MOut, "    r.f[%u] = 1.0f;\n", ScopedD);
                        break;
                    case NativeId::CameraOffset:
                        AppendFormat(MOut, "    ctx.CameraOffset(r.f[%u], r.f[%u], r.f[%u]);\n", ScopedD, ScopedD + 1, ScopedD + 2);
                        AppendFormat(MOut, "    r.f[%u] = 1.0f;\n", ScopedD);
                        break;
                    case NativeId::CameraZoom:
                        AppendFormat(MOut, "    ctx.CameraZoom(r.f[%u], r.f[%u]);\n", ScopedD, ScopedD + 1);
                        AppendFormat(MOut, "    r.f[%u] = 1.0f;\n", ScopedD);
                        break;
                    case NativeId::CameraRoll:
                        AppendFormat(MOut, "    ctx.CameraRoll(r.f[%u], r.f[%u]);\n", ScopedD, ScopedD + 1);
                        AppendFormat(MOut, "    r.f[%u] = 1.0f;\n", ScopedD);
                        break;
                    case NativeId::CameraClearEffects:
                        MOut += "    ctx.CameraClear();\n";
                        AppendFormat(MOut, "    r.f[%u] = 1.0f;\n", ScopedD);
                        break;
                    case NativeId::CameraSetMotionScale:
                        AppendFormat(MOut, "    ctx.CameraSetMotion(r.f[%u]);\n", ScopedD);
                        AppendFormat(MOut, "    r.f[%u] = 1.0f;\n", ScopedD);
                        break;
                    case NativeId::ArrayNew:
                        AppendFormat(MOut, "    r.u[%u] = ctx.ArrayNew();\n", ScopedD);
                        AppendFormat(MOut,
                                     "    if (r.u[%u] == 0xFFFFFFFFu) { ctx.Fault(%u, %u); return; }\n", ScopedD,
                                     Aot::FaultCode::HeapFull, InPc);
                        AppendFormat(MOut, "    r.tag[%u] = ::Fby::Script::Aot::Tag::Array;\n", ScopedD);
                        break;
                    case NativeId::ArrayPush:
                        AppendFormat(MOut,
                                     "    if (!ctx.ArrayPush(r.u[%u], r.u[%u], r.tag[%u])) { ctx.Fault(%u, %u); "
                                     "return; }\n",
                                     ScopedD, ScopedD + 1, ScopedD + 1, Aot::FaultCode::BadCollection, InPc);
                        AppendFormat(MOut, "    r.u[%u] = 1; r.tag[%u] = ::Fby::Script::Aot::Tag::Int;\n", ScopedD,
                                     ScopedD);
                        break;
                    case NativeId::ArraySet:
                        AppendFormat(MOut,
                                     "    if (!ctx.ArraySet(r.u[%u], r.u[%u], r.u[%u], r.tag[%u])) { ctx.Fault(%u, "
                                     "%u); return; }\n",
                                     ScopedD, ScopedD + 1, ScopedD + 2, ScopedD + 2, Aot::FaultCode::OutOfRange,
                                     InPc);
                        AppendFormat(MOut, "    r.u[%u] = 1; r.tag[%u] = ::Fby::Script::Aot::Tag::Int;\n", ScopedD,
                                     ScopedD);
                        break;
                    case NativeId::ArrayGet:
                        AppendFormat(MOut,
                                     "    { std::uint32_t b%u = 0; std::uint32_t t%u = 0; if "
                                     "(!ctx.ArrayGet(r.u[%u], r.u[%u], &b%u, &t%u)) { ctx.Fault(%u, %u); "
                                     "return; } r.u[%u] = b%u; r.tag[%u] = t%u; }\n",
                                     InPc, InPc, ScopedD, ScopedD + 1, InPc, InPc, Aot::FaultCode::OutOfRange, InPc,
                                     ScopedD, InPc, ScopedD, InPc);
                        break;
                    case NativeId::ArrayLength:
                        AppendFormat(MOut,
                                     "    { const std::uint32_t l%u = ctx.ArrayLength(r.u[%u]); if (l%u == "
                                     "0xFFFFFFFFu) { ctx.Fault(%u, %u); return; } r.u[%u] = l%u; r.tag[%u] = "
                                     "::Fby::Script::Aot::Tag::Int; }\n",
                                     InPc, ScopedD, InPc, Aot::FaultCode::BadCollection, InPc, ScopedD, InPc,
                                     ScopedD);
                        break;
                    case NativeId::MapNew:
                        AppendFormat(MOut, "    r.u[%u] = ctx.MapNew();\n", ScopedD);
                        AppendFormat(MOut,
                                     "    if (r.u[%u] == 0xFFFFFFFFu) { ctx.Fault(%u, %u); return; }\n", ScopedD,
                                     Aot::FaultCode::HeapFull, InPc);
                        AppendFormat(MOut, "    r.tag[%u] = ::Fby::Script::Aot::Tag::Map;\n", ScopedD);
                        break;
                    case NativeId::MapSet:
                        AppendFormat(MOut,
                                     "    if (!ctx.MapSet(r.u[%u], r.u[%u], r.tag[%u], r.u[%u], r.tag[%u])) { "
                                     "ctx.Fault(%u, %u); return; }\n",
                                     ScopedD, ScopedD + 1, ScopedD + 1, ScopedD + 2, ScopedD + 2,
                                     Aot::FaultCode::BadCollection, InPc);
                        AppendFormat(MOut, "    r.u[%u] = 1; r.tag[%u] = ::Fby::Script::Aot::Tag::Int;\n", ScopedD,
                                     ScopedD);
                        break;
                    case NativeId::MapGet:
                        AppendFormat(MOut,
                                     "    { std::uint32_t b%u = 0; std::uint32_t t%u = 0; if "
                                     "(!ctx.MapGet(r.u[%u], r.u[%u], r.tag[%u], &b%u, &t%u)) { ctx.Fault(%u, "
                                     "%u); return; } r.u[%u] = b%u; r.tag[%u] = t%u; }\n",
                                     InPc, InPc, ScopedD, ScopedD + 1, ScopedD + 1, InPc, InPc,
                                     Aot::FaultCode::MissingKey, InPc, ScopedD, InPc, ScopedD, InPc);
                        break;
                    case NativeId::MapHas:
                        AppendFormat(MOut,
                                     "    r.u[%u] = ctx.MapHas(r.u[%u], r.u[%u], r.tag[%u]) ? 1u : 0u; "
                                     "r.tag[%u] = ::Fby::Script::Aot::Tag::Int;\n",
                                     ScopedD, ScopedD, ScopedD + 1, ScopedD + 1, ScopedD);
                        break;
                    case NativeId::MapLength:
                        AppendFormat(MOut,
                                     "    { const std::uint32_t l%u = ctx.MapLength(r.u[%u]); if (l%u == "
                                     "0xFFFFFFFFu) { ctx.Fault(%u, %u); return; } r.u[%u] = l%u; r.tag[%u] = "
                                     "::Fby::Script::Aot::Tag::Int; }\n",
                                     InPc, ScopedD, InPc, Aot::FaultCode::BadCollection, InPc, ScopedD, InPc,
                                     ScopedD);
                        break;
                    case NativeId::StringConcat:
                        AppendFormat(MOut,
                                     "    r.u[%u] = ctx.StringConcat(r.u[%u], r.u[%u]);\n", ScopedD, ScopedD,
                                     ScopedD + 1);
                        AppendFormat(MOut,
                                     "    if (r.u[%u] == 0xFFFFFFFFu) { ctx.Fault(%u, %u); return; }\n", ScopedD,
                                     Aot::FaultCode::BadString, InPc);
                        AppendFormat(MOut, "    r.tag[%u] = ::Fby::Script::Aot::Tag::String;\n", ScopedD);
                        break;
                    case NativeId::StringLength:
                        AppendFormat(MOut,
                                     "    { const std::uint32_t l%u = ctx.StringLength(r.u[%u]); if (l%u == "
                                     "0xFFFFFFFFu) { ctx.Fault(%u, %u); return; } r.u[%u] = l%u; r.tag[%u] = "
                                     "::Fby::Script::Aot::Tag::Int; }\n",
                                     InPc, ScopedD, InPc, Aot::FaultCode::BadString, InPc, ScopedD, InPc, ScopedD);
                        break;
                    case NativeId::StringEquals:
                        AppendFormat(MOut,
                                     "    r.u[%u] = ctx.StringEquals(r.u[%u], r.u[%u]) ? 1u : 0u; r.tag[%u] = "
                                     "::Fby::Script::Aot::Tag::Int;\n",
                                     ScopedD, ScopedD, ScopedD + 1, ScopedD);
                        break;
                    case NativeId::Count:
                    default:
                        *InError = "native call uses an unknown NativeId";
                        return false;
                }
                return true;
            }

            case OpCode::SpawnPrefab:
                AppendFormat(MOut, "    r.u[%u] = ctx.Spawn(%s);\n", ScopedD,
                             U32Literal(InInstruction.operand).c_str());
                return true;
            case OpCode::DespawnEntity:
                AppendFormat(MOut, "    ctx.Despawn(r.u[%u]);\n", ScopedS);
                return true;
            case OpCode::RandomF:
                AppendFormat(MOut, "    r.f[%u] = ctx.RandomFloat();\n", ScopedD);
                return true;
            case OpCode::RandomRangeF:
                AppendFormat(MOut,
                             "    { float a = r.f[%u]; float b = %s; if (b < a) { const float t = a; a = b; b = t; } "
                             "r.f[%u] = a + (b - a) * ctx.RandomFloat(); }\n",
                             ScopedS, FloatLiteral(MProgram.constants[InInstruction.operand]).c_str(), ScopedD);
                return true;

            case OpCode::CallFunction:
            {
                if (ScopedO >= MProgram.functions.size() ||
                    (MProgram.functions[ScopedO].event & kUserFunctionFlag) == 0)
                {
                    *InError = "CallFunction references a non-user entry";
                    return false;
                }
                const std::uint32_t ScopedBase = MProgram.functions[ScopedO].registerHighWater;
                AppendFormat(MOut,
                             "    if (ctx.callDepth >= %u) { ctx.Fault(%u, %u); return; }\n", kMaxCallDepth,
                             Aot::FaultCode::Recursion, InPc);
                MOut += "    ++ctx.callDepth;\n";
                AppendFormat(MOut, "    fby_fn_%u(ctx, r);\n", ScopedO);
                MOut += "    --ctx.callDepth;\n";
                AppendFormat(MOut, "    r.u[%u] = r.u[%u]; r.tag[%u] = r.tag[%u];\n", ScopedD, ScopedBase, ScopedD,
                             ScopedBase);
                return true;
            }
            case OpCode::Return:
                AppendFormat(MOut, "    r.u[%u] = r.u[%u]; r.tag[%u] = r.tag[%u];\n", MCurrentBase, ScopedS,
                             MCurrentBase, ScopedS);
                MOut += "    return;\n";
                return true;

            case OpCode::Count:
            default:
                *InError = "unsupported opcode at instruction " + std::to_string(InPc);
                return false;
        }
    }

    const CompiledProgram& MProgram;
    std::string& MOut;
    std::vector<bool> MUsedComponents{};
    std::uint32_t MCurrentBase = 0;
};
}

bool TranspileProgramToCpp(const CompiledProgram& InProgram, const AotTranspileOptions& InOptions,
                           std::string* InSource, std::string* InError)
{
    if (InSource == nullptr)
    {
        return false;
    }
    *InSource = {};
    if (!InProgram.valid)
    {
        if (InError != nullptr)
        {
            *InError = "TranspileProgramToCpp: invalid program";
        }
        return false;
    }
    if (InError != nullptr)
    {
        InError->clear();
    }

    std::string& ScopedOut = *InSource;
    ScopedOut += "// Generated by FemboyEngine2D (fby_aotc) from cooked FemboyScript bytecode.\n";
    ScopedOut += "// Do not edit: re-cook the VisualScript graph instead.\n";
    ScopedOut += "//\n";
    AppendFormat(ScopedOut, "// program: %.*s\n", static_cast<int>(InOptions.programName.size()),
                 InOptions.programName.data());
    AppendFormat(ScopedOut, "// graphHash: %s\n", Hex64(InProgram.header.graphHash).c_str());
    AppendFormat(ScopedOut, "// sourceHash: %s\n", Hex64(InProgram.header.sourceHash).c_str());
    AppendFormat(ScopedOut, "// instructions: %u\n\n", InProgram.header.codeCount);
    ScopedOut += "#include \"Engine/Script/AotAbi.hpp\"\n\n";
    ScopedOut += "#include <cstdint>\n";
    ScopedOut += "#include <limits>\n\n";
    ScopedOut += "namespace\n{\n";
    ScopedOut += "struct Registers\n{\n";
    ScopedOut += "    union\n    {\n";
    ScopedOut += "        float f[128];\n";
    ScopedOut += "        std::uint32_t u[128];\n";
    ScopedOut += "        std::int32_t i[128];\n";
    ScopedOut += "    };\n";
    ScopedOut += "    std::uint32_t tag[128]{};\n";
    ScopedOut += "};\n\n";

    bool ScopedAnyFunction = false;
    FunctionEmitter ScopedEmitter(InProgram, ScopedOut);
    // User functions can call each other in any order once every prototype is
    // visible; the text compiler still rejects forward calls.
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < InProgram.functions.size(); ++ScopedIndex)
    {
        if ((InProgram.functions[ScopedIndex].event & kUserFunctionFlag) != 0)
        {
            AppendFormat(ScopedOut, "void fby_fn_%u(::Fby::Script::Aot::Context& ctx, Registers& r) noexcept;\n",
                         ScopedIndex);
        }
    }
    if (!InProgram.functions.empty())
    {
        ScopedOut += "\n";
    }
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < InProgram.functions.size(); ++ScopedIndex)
    {
        const FunctionEntry& ScopedFunction = InProgram.functions[ScopedIndex];
        std::string ScopedFunctionError;
        if (!ScopedEmitter.Emit(ScopedFunction, ScopedIndex, &ScopedFunctionError))
        {
            if (InError != nullptr)
            {
                *InError = ScopedFunctionError;
            }
            InSource->clear();
            return false;
        }
        ScopedAnyFunction = true;
    }
    if (!ScopedAnyFunction)
    {
        ScopedOut += "// program declares no event functions\n";
    }

    // Emit the module record. Entry order: start, update, fixedUpdate.
    const auto ScopedEntryFor = [&InProgram](ScriptEvent InEvent) -> std::string
    {
        const FunctionEntry* ScopedEntry = InProgram.FindFunction(InEvent);
        return ScopedEntry != nullptr ? std::string("fby_entry_") + EntryName(InEvent) : std::string("nullptr");
    };
    const std::string ScopedStartEntry = ScopedEntryFor(ScriptEvent::Start);
    const std::string ScopedUpdateEntry = ScopedEntryFor(ScriptEvent::Update);
    const std::string ScopedFixedEntry = ScopedEntryFor(ScriptEvent::FixedUpdate);

    ScopedOut += "}\n\n";
    ScopedOut += "FBY_AOT_EXPORT const ::Fby::Script::Aot::Module* fby_aot_module() noexcept\n{\n";
    if (InProgram.components.empty())
    {
        ScopedOut += "    static const std::uint64_t kComponentHashes[1] = {0ull};\n";
    }
    else
    {
        ScopedOut += "    static const std::uint64_t kComponentHashes[] = {\n";
        for (const ComponentBinding& ScopedComponent : InProgram.components)
        {
            AppendFormat(ScopedOut, "        %s,\n", Hex64(ScopedComponent.layoutHash).c_str());
        }
        ScopedOut += "    };\n";
    }
    ScopedOut += "    static const ::Fby::Script::Aot::Module module{\n";
    ScopedOut += "        ::Fby::Script::Aot::kMagic,\n";
    ScopedOut += "        ::Fby::Script::Aot::kAbiVersion,\n";
    ScopedOut += "        0,\n";
    AppendFormat(ScopedOut, "        %s,\n", Hex64(InProgram.header.graphHash).c_str());
    AppendFormat(ScopedOut, "        %s,\n", Hex64(InProgram.header.sourceHash).c_str());
    AppendFormat(ScopedOut, "        %s,\n",
                 U32Literal(static_cast<std::uint32_t>(InProgram.components.size())).c_str());
    ScopedOut += "        0,\n";
    ScopedOut += InProgram.components.empty() ? "        nullptr,\n" : "        kComponentHashes,\n";
    AppendFormat(ScopedOut, "        %s,\n", ScopedStartEntry.c_str());
    AppendFormat(ScopedOut, "        %s,\n", ScopedUpdateEntry.c_str());
    AppendFormat(ScopedOut, "        %s,\n", ScopedFixedEntry.c_str());
    ScopedOut += "    };\n";
    ScopedOut += "    return &module;\n";
    ScopedOut += "}\n";
    return true;
}
}
