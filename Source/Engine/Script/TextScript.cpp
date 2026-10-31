// FemboyEngine2D - FemboyScript textual front-end implementation.
#include "Engine/Script/TextScript.hpp"

#include "Engine/Core/Mirror.hpp"
#include "Engine/Script/Bytecode.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <vector>

namespace Fby::Script
{
namespace
{
enum class TokenKind : std::uint8_t
{
    End,
    Identifier,
    Number,
    Punctuation,
};

struct Token
{
    TokenKind kind = TokenKind::End;
    std::string text;
    std::uint32_t line = 1;
    std::uint32_t column = 1;
};

class Lexer
{
public:
    explicit Lexer(std::string_view InSource) : MSource(InSource)
    {
    }

    [[nodiscard]] Token Next()
    {
        SkipSpace();
        if (MOffset >= MSource.size())
        {
            return {};
        }
        const char ScopedCharacter = MSource[MOffset];
        if (std::isalpha(static_cast<unsigned char>(ScopedCharacter)) || ScopedCharacter == '_')
        {
            const std::size_t ScopedStart = MOffset;
            while (MOffset < MSource.size())
            {
                const char ScopedNext = MSource[MOffset];
                if (!std::isalnum(static_cast<unsigned char>(ScopedNext)) && ScopedNext != '_')
                {
                    break;
                }
                ++MOffset;
            }
            return MakeToken(TokenKind::Identifier, MSource.substr(ScopedStart, MOffset - ScopedStart), ScopedStart);
        }
        if (std::isdigit(static_cast<unsigned char>(ScopedCharacter)) || ScopedCharacter == '.')
        {
            const std::size_t ScopedStart = MOffset;
            bool ScopedHasDot = false;
            while (MOffset < MSource.size())
            {
                const char ScopedNext = MSource[MOffset];
                if (ScopedNext == '.')
                {
                    if (ScopedHasDot)
                    {
                        break;
                    }
                    ScopedHasDot = true;
                }
                else if (!std::isdigit(static_cast<unsigned char>(ScopedNext)))
                {
                    break;
                }
                ++MOffset;
            }
            return MakeToken(TokenKind::Number, MSource.substr(ScopedStart, MOffset - ScopedStart), ScopedStart);
        }
        if (ScopedCharacter == '"')
        {
            const std::size_t ScopedStart = MOffset;
            ++MOffset;
            std::string ScopedText;
            while (MOffset < MSource.size() && MSource[MOffset] != '"')
            {
                ScopedText.push_back(MSource[MOffset++]);
            }
            if (MOffset < MSource.size())
            {
                ++MOffset;
            }
            return MakeToken(TokenKind::Identifier, ScopedText, ScopedStart);
        }
        const std::size_t ScopedStart = MOffset;
        ++MOffset;
        if (ScopedCharacter == '\n')
        {
            ++MLine;
            MLineStart = MOffset;
        }
        return MakeToken(TokenKind::Punctuation, std::string_view(&MSource[ScopedStart], 1), ScopedStart);
    }

private:
    [[nodiscard]] Token MakeToken(TokenKind InKind, std::string_view InText, std::size_t InStart) const
    {
        Token ScopedToken;
        ScopedToken.kind = InKind;
        ScopedToken.text.assign(InText);
        ScopedToken.line = MLine;
        ScopedToken.column = InStart >= MLineStart ? static_cast<std::uint32_t>(InStart - MLineStart + 1) : 1;
        return ScopedToken;
    }

    void SkipSpace()
    {
        for (;;)
        {
            while (MOffset < MSource.size() && std::isspace(static_cast<unsigned char>(MSource[MOffset])))
            {
                if (MSource[MOffset] == '\n')
                {
                    ++MLine;
                    MLineStart = MOffset + 1;
                }
                ++MOffset;
            }
            if (MOffset + 1 < MSource.size() && MSource[MOffset] == '/' && MSource[MOffset + 1] == '/')
            {
                while (MOffset < MSource.size() && MSource[MOffset] != '\n')
                {
                    ++MOffset;
                }
                continue;
            }
            if (MOffset < MSource.size() && MSource[MOffset] == '#')
            {
                while (MOffset < MSource.size() && MSource[MOffset] != '\n')
                {
                    ++MOffset;
                }
                continue;
            }
            break;
        }
    }

    std::string_view MSource;
    std::size_t MOffset = 0;
    std::uint32_t MLine = 1;
    std::size_t MLineStart = 0;
};

class Compiler
{
public:
    Compiler(std::string_view InSource, const Scene::ComponentRegistry& InComponents, const TextCompileOptions& InOptions,
             std::string* InError)
        : MLexer(InSource), MComponents(InComponents), MOptions(InOptions), MError(InError)
    {
    }

    [[nodiscard]] bool Run(std::vector<std::byte>* OutProgram)
    {
        MBuilder.SetSourceName(MOptions.sourceName);
        MToken = MLexer.Next();
        while (!MFailed && MToken.kind != TokenKind::End)
        {
            if (!Expect("event"))
            {
                break;
            }
            const std::string ScopedEventName = MToken.text;
            if (!ExpectIdentifier())
            {
                break;
            }
            const ScriptEvent ScopedEvent = EventFromName(ScopedEventName);
            if (ScopedEvent == ScriptEvent::Count)
            {
                Fail("unknown event '" + ScopedEventName + "'");
                break;
            }
            const std::uint32_t ScopedOffset = static_cast<std::uint32_t>(MBuilder.CodeSize());
            if (!ParseBlock())
            {
                break;
            }
            Emit(OpCode::Halt);
            MBuilder.AddFunction(ScopedEvent, ScopedOffset);
        }
        if (MFailed)
        {
            return false;
        }
        if (MBuilder.CodeSize() == 0)
        {
            Fail("text script has no event block");
            return false;
        }
        std::string ScopedBuildError;
        const std::vector<std::byte> ScopedProgram =
            MBuilder.Build(MOptions.sourceHash, MOptions.graphHash, &ScopedBuildError);
        if (ScopedProgram.empty())
        {
            Fail(ScopedBuildError.empty() ? "text script build failed" : ScopedBuildError);
            return false;
        }
        *OutProgram = ScopedProgram;
        return true;
    }

private:
    static ScriptEvent EventFromName(std::string_view InName) noexcept
    {
        if (InName == "start")
        {
            return ScriptEvent::Start;
        }
        if (InName == "update")
        {
            return ScriptEvent::Update;
        }
        if (InName == "fixed_update" || InName == "fixed")
        {
            return ScriptEvent::FixedUpdate;
        }
        return ScriptEvent::Count;
    }

    static NativeId NativeFromName(std::string_view InName) noexcept
    {
        if (InName == "log_float")
        {
            return NativeId::LogFloat;
        }
        if (InName == "log_int")
        {
            return NativeId::LogInt;
        }
        if (InName == "log_vec2")
        {
            return NativeId::LogVec2;
        }
        if (InName == "log_message")
        {
            return NativeId::LogMessage;
        }
        if (InName == "despawn_self")
        {
            return NativeId::DespawnSelf;
        }
        if (InName == "spawn_prefab")
        {
            return NativeId::SpawnPrefab;
        }
        if (InName == "random_float")
        {
            return NativeId::RandomFloat;
        }
        if (InName == "random_range")
        {
            return NativeId::RandomRange;
        }
        if (InName == "play_sound")
        {
            return NativeId::PlaySound;
        }
        if (InName == "get_delta_time")
        {
            return NativeId::GetDeltaTime;
        }
        if (InName == "get_frame_index")
        {
            return NativeId::GetFrameIndex;
        }
        if (InName == "BigIntFromInt")
        {
            return NativeId::BigIntFromInt;
        }
        if (InName == "BigIntAdd")
        {
            return NativeId::BigIntAdd;
        }
        if (InName == "BigIntSub")
        {
            return NativeId::BigIntSub;
        }
        if (InName == "BigIntMul")
        {
            return NativeId::BigIntMul;
        }
        if (InName == "BigIntDiv")
        {
            return NativeId::BigIntDiv;
        }
        if (InName == "BigIntMod")
        {
            return NativeId::BigIntMod;
        }
        if (InName == "BigIntCmp")
        {
            return NativeId::BigIntCmp;
        }
        if (InName == "BigIntToInt")
        {
            return NativeId::BigIntToInt;
        }
        if (InName == "BigIntLog")
        {
            return NativeId::BigIntLog;
        }
        if (InName == "BigIntRelease")
        {
            return NativeId::BigIntRelease;
        }
        if (InName == "DecimalFromInt")
        {
            return NativeId::DecimalFromInt;
        }
        if (InName == "DecimalAdd")
        {
            return NativeId::DecimalAdd;
        }
        if (InName == "DecimalSub")
        {
            return NativeId::DecimalSub;
        }
        if (InName == "DecimalMul")
        {
            return NativeId::DecimalMul;
        }
        if (InName == "DecimalDiv")
        {
            return NativeId::DecimalDiv;
        }
        if (InName == "DecimalDivRounded")
        {
            return NativeId::DecimalDivRounded;
        }
        if (InName == "DecimalCmp")
        {
            return NativeId::DecimalCmp;
        }
        if (InName == "DecimalToInt")
        {
            return NativeId::DecimalToInt;
        }
        if (InName == "DecimalLog")
        {
            return NativeId::DecimalLog;
        }
        if (InName == "DecimalRelease")
        {
            return NativeId::DecimalRelease;
        }
        return NativeId::Count;
    }

    [[nodiscard]] bool ParseBlock()
    {
        if (!Expect("{"))
        {
            return false;
        }
        while (!MFailed && !Accept("}"))
        {
            if (!ParseStatement())
            {
                return false;
            }
        }
        return !MFailed;
    }

    std::uint32_t Emit(OpCode InOp, std::uint8_t InDst = 0, std::uint8_t InSrc = 0, std::uint16_t InOperand = 0,
                       std::uint8_t InFlags = 0)
    {
        const std::uint32_t ScopedPc = MBuilder.Emit(InOp, InDst, InSrc, InOperand, InFlags);
        MBuilder.AddSpan(ScopedPc, MToken.line, MToken.column);
        return ScopedPc;
    }

    [[nodiscard]] bool ParseStatement()
    {
        if (Accept("let"))
        {
            const std::string ScopedName = MToken.text;
            if (!ExpectIdentifier())
            {
                return false;
            }
            if (!Expect("=") || !ParseExpression(nullptr))
            {
                return false;
            }
            if (!Expect(";"))
            {
                return false;
            }
            MVariables[ScopedName] = MLastRegister;
            MBuilder.AddVariable(static_cast<std::uint8_t>(MLastRegister), ScopedName);
            return true;
        }
        if (Accept("if"))
        {
            if (!Expect("(") || !ParseExpression(nullptr) || !Expect(")"))
            {
                return false;
            }
            const std::uint32_t ScopedCondition = MLastRegister;
            const std::uint32_t ScopedJumpElse = Emit(OpCode::JumpIfZero, static_cast<std::uint8_t>(ScopedCondition));
            if (!ParseBlock())
            {
                return false;
            }
            if (Accept("else"))
            {
                const std::uint32_t ScopedJumpEnd = Emit(OpCode::Jump);
                MBuilder.PatchJump(ScopedJumpElse, MBuilder.CodeSize());
                if (!ParseBlock())
                {
                    return false;
                }
                MBuilder.PatchJump(ScopedJumpEnd, MBuilder.CodeSize());
            }
            else
            {
                MBuilder.PatchJump(ScopedJumpElse, MBuilder.CodeSize());
            }
            return true;
        }
        if (Accept("self"))
        {
            const std::uint32_t ScopedEntity = EntityRegister();
            if (!Expect("."))
            {
                return false;
            }
            const std::string ScopedComponent = MToken.text;
            if (!ExpectIdentifier() || !Expect("."))
            {
                return false;
            }
            const std::string ScopedField = MToken.text;
            if (!ExpectIdentifier() || !Expect("=") || !ParseExpression(nullptr) || !Expect(";"))
            {
                return false;
            }
            const std::uint32_t ScopedFieldIndex = ResolveField(ScopedComponent, ScopedField);
            if (MFailed)
            {
                return false;
            }
            const FieldBinding& ScopedBinding = MFieldBindings[ScopedFieldIndex];
            if (ScopedBinding.kind == static_cast<std::uint8_t>(Reflect::FieldKind::Float) && ScopedBinding.size == 4)
            {
                Emit(OpCode::StoreFieldF, static_cast<std::uint8_t>(ScopedEntity),
                              static_cast<std::uint8_t>(MLastRegister), static_cast<std::uint16_t>(ScopedFieldIndex));
                return true;
            }
            Fail("text assignment supports float fields only");
            return false;
        }
        if (MToken.kind == TokenKind::Identifier)
        {
            const std::string ScopedName = MToken.text;
            if (!ExpectIdentifier())
            {
                return false;
            }
            if (Accept("="))
            {
                if (!ParseExpression(nullptr) || !Expect(";"))
                {
                    return false;
                }
                const auto ScopedVariable = MVariables.find(ScopedName);
                if (ScopedVariable == MVariables.end())
                {
                    Fail("unknown variable '" + ScopedName + "'");
                    return false;
                }
                Emit(OpCode::MoveF, ScopedVariable->second, static_cast<std::uint8_t>(MLastRegister));
                return true;
            }
            if (Accept("("))
            {
                const NativeId ScopedNative = NativeFromName(ScopedName);
                if (ScopedNative == NativeId::Count)
                {
                    Fail("unknown native '" + ScopedName + "'");
                    return false;
                }
                const std::uint8_t ScopedBase = Allocate();
                for (std::uint32_t ScopedSlot = 1; ScopedSlot < NativeArgumentCount(ScopedNative); ++ScopedSlot)
                {
                    (void)Allocate();
                }
                std::uint32_t ScopedArgument = 0;
                while (!MFailed && !Accept(")"))
                {
                    if (ScopedArgument > 0 && !Expect(","))
                    {
                        return false;
                    }
                    if (!ParseExpression(nullptr))
                    {
                        return false;
                    }
                    Emit(OpCode::MoveF, static_cast<std::uint8_t>(ScopedBase + ScopedArgument),
                                  static_cast<std::uint8_t>(MLastRegister));
                    ++ScopedArgument;
                }
                const std::uint16_t ScopedNativeIndex = MBuilder.AddNative(ScopedNative);
                Emit(OpCode::CallNative, ScopedBase, 0, ScopedNativeIndex);
                if (!Expect(";"))
                {
                    return false;
                }
                return true;
            }
            Fail("expected '=' or '(' after identifier");
            return false;
        }
        Fail("expected a statement");
        return false;
    }

    [[nodiscard]] bool ParseExpression(std::uint8_t* OutRegister)
    {
        if (!ParseComparison())
        {
            return false;
        }
        if (OutRegister != nullptr)
        {
            *OutRegister = static_cast<std::uint8_t>(MLastRegister);
        }
        return true;
    }

    [[nodiscard]] bool ParseComparison()
    {
        if (!ParseAdditive())
        {
            return false;
        }
        while (!MFailed)
        {
            OpCode ScopedOp = OpCode::Count;
            if (Accept("<"))
            {
                ScopedOp = OpCode::LtF;
            }
            else if (Accept("<="))
            {
                ScopedOp = OpCode::LeF;
            }
            else if (Accept(">"))
            {
                ScopedOp = OpCode::GtF;
            }
            else if (Accept(">="))
            {
                ScopedOp = OpCode::GeF;
            }
            else if (Accept("=="))
            {
                ScopedOp = OpCode::EqF;
            }
            else if (Accept("!="))
            {
                ScopedOp = OpCode::NeF;
            }
            if (ScopedOp == OpCode::Count)
            {
                break;
            }
            const std::uint32_t ScopedLeft = MLastRegister;
            if (!ParseAdditive())
            {
                return false;
            }
            const std::uint8_t ScopedDestination = Allocate();
            Emit(ScopedOp, ScopedDestination, static_cast<std::uint8_t>(ScopedLeft),
                          static_cast<std::uint16_t>(MLastRegister));
            MLastRegister = ScopedDestination;
        }
        return !MFailed;
    }

    [[nodiscard]] bool ParseAdditive()
    {
        if (!ParseMultiplicative())
        {
            return false;
        }
        while (!MFailed)
        {
            OpCode ScopedOp = OpCode::Count;
            if (Accept("+"))
            {
                ScopedOp = OpCode::AddF;
            }
            else if (Accept("-"))
            {
                ScopedOp = OpCode::SubF;
            }
            if (ScopedOp == OpCode::Count)
            {
                break;
            }
            const std::uint32_t ScopedLeft = MLastRegister;
            if (!ParseMultiplicative())
            {
                return false;
            }
            const std::uint8_t ScopedDestination = Allocate();
            Emit(ScopedOp, ScopedDestination, static_cast<std::uint8_t>(ScopedLeft),
                          static_cast<std::uint16_t>(MLastRegister));
            MLastRegister = ScopedDestination;
        }
        return !MFailed;
    }

    [[nodiscard]] bool ParseMultiplicative()
    {
        if (!ParseUnary())
        {
            return false;
        }
        while (!MFailed)
        {
            OpCode ScopedOp = OpCode::Count;
            if (Accept("*"))
            {
                ScopedOp = OpCode::MulF;
            }
            else if (Accept("/"))
            {
                ScopedOp = OpCode::DivF;
            }
            if (ScopedOp == OpCode::Count)
            {
                break;
            }
            const std::uint32_t ScopedLeft = MLastRegister;
            if (!ParseUnary())
            {
                return false;
            }
            const std::uint8_t ScopedDestination = Allocate();
            Emit(ScopedOp, ScopedDestination, static_cast<std::uint8_t>(ScopedLeft),
                          static_cast<std::uint16_t>(MLastRegister));
            MLastRegister = ScopedDestination;
        }
        return !MFailed;
    }

    [[nodiscard]] bool ParseUnary()
    {
        if (Accept("-"))
        {
            if (!ParseUnary())
            {
                return false;
            }
            const std::uint8_t ScopedDestination = Allocate();
            Emit(OpCode::NegF, ScopedDestination, static_cast<std::uint8_t>(MLastRegister));
            MLastRegister = ScopedDestination;
            return true;
        }
        return ParsePrimary();
    }

    [[nodiscard]] bool ParsePrimary()
    {
        if (Accept("("))
        {
            if (!ParseExpression(nullptr) || !Expect(")"))
            {
                return false;
            }
            return true;
        }
        if (MToken.kind == TokenKind::Number)
        {
            const float ScopedValue = std::strtof(MToken.text.c_str(), nullptr);
            if (!ExpectNumber())
            {
                return false;
            }
            const std::uint8_t ScopedDestination = Allocate();
            Emit(OpCode::LoadF32, ScopedDestination, 0, MBuilder.AddConstant(ScopedValue));
            MLastRegister = ScopedDestination;
            return true;
        }
        if (MToken.text == "true" || MToken.text == "false")
        {
            const bool ScopedValue = MToken.text == "true";
            MToken = MLexer.Next();
            const std::uint8_t ScopedDestination = Allocate();
            Emit(OpCode::LoadI32, ScopedDestination, 0, ScopedValue ? 1 : 0);
            MLastRegister = ScopedDestination;
            return true;
        }
        if (Accept("self"))
        {
            const std::uint32_t ScopedEntity = EntityRegister();
            if (!Expect("."))
            {
                return false;
            }
            const std::string ScopedComponent = MToken.text;
            if (!ExpectIdentifier() || !Expect("."))
            {
                return false;
            }
            const std::string ScopedFieldName = MToken.text;
            if (!ExpectIdentifier())
            {
                return false;
            }
            std::string ScopedSuffix;
            if (Accept(".") && ExpectIdentifier())
            {
                ScopedSuffix = MToken.text;
            }
            const std::uint32_t ScopedFieldIndex = ResolveField(ScopedComponent, ScopedFieldName);
            if (MFailed)
            {
                return false;
            }
            const FieldBinding& ScopedField = MFieldBindings[ScopedFieldIndex];
            if (ScopedField.kind == static_cast<std::uint8_t>(Reflect::FieldKind::Float) && ScopedField.size == 4)
            {
                const std::uint8_t ScopedDestination = Allocate();
                Emit(OpCode::LoadFieldF, ScopedDestination, static_cast<std::uint8_t>(ScopedEntity),
                              static_cast<std::uint16_t>(ScopedFieldIndex));
                MLastRegister = ScopedDestination;
                return true;
            }
            if (ScopedField.kind == static_cast<std::uint8_t>(Reflect::FieldKind::Vector2D) && ScopedField.size == 8)
            {
                const std::uint8_t ScopedDestination = Allocate();
                if (ScopedSuffix == "X" || ScopedSuffix == "x")
                {
                    Emit(OpCode::LoadFieldVec2, ScopedDestination, static_cast<std::uint8_t>(ScopedEntity),
                                  static_cast<std::uint16_t>(ScopedFieldIndex));
                    MLastRegister = ScopedDestination;
                    return true;
                }
                if (ScopedSuffix == "Y" || ScopedSuffix == "y")
                {
                    Emit(OpCode::LoadFieldVec2, ScopedDestination, static_cast<std::uint8_t>(ScopedEntity),
                                  static_cast<std::uint16_t>(ScopedFieldIndex));
                    MLastRegister = static_cast<std::uint8_t>(ScopedDestination + 1);
                    return true;
                }
                Fail("vector field requires .X or .Y in an expression");
                return false;
            }
            Fail("text expression supports float and Vec2.X/Y fields only");
            return false;
        }
        if (MToken.kind == TokenKind::Identifier)
        {
            const std::string ScopedName = MToken.text;
            if (!ExpectIdentifier())
            {
                return false;
            }
            const auto ScopedVariable = MVariables.find(ScopedName);
            if (ScopedVariable != MVariables.end())
            {
                MLastRegister = ScopedVariable->second;
                return true;
            }
            const NativeId ScopedNative = NativeFromName(ScopedName);
            if (ScopedNative == NativeId::Count || !Accept("("))
            {
                Fail("unknown variable '" + ScopedName + "'");
                return false;
            }
            const std::uint8_t ScopedBase = Allocate();
            for (std::uint32_t ScopedSlot = 1; ScopedSlot < NativeArgumentCount(ScopedNative); ++ScopedSlot)
            {
                (void)Allocate();
            }
            std::uint32_t ScopedArgument = 0;
            while (!MFailed && !Accept(")"))
            {
                if (ScopedArgument > 0 && !Expect(","))
                {
                    return false;
                }
                if (!ParseExpression(nullptr))
                {
                    return false;
                }
                Emit(OpCode::MoveF, static_cast<std::uint8_t>(ScopedBase + ScopedArgument),
                              static_cast<std::uint8_t>(MLastRegister));
                ++ScopedArgument;
            }
            Emit(OpCode::CallNative, ScopedBase, 0, MBuilder.AddNative(ScopedNative));
            MLastRegister = ScopedBase;
            return true;
        }
        Fail("expected an expression");
        return false;
    }

    [[nodiscard]] std::uint32_t ResolveField(const std::string& InComponent, const std::string& InField)
    {
        const std::string ScopedKey = InComponent + "." + InField;
        if (const auto ScopedExisting = MFieldCache.find(ScopedKey); ScopedExisting != MFieldCache.end())
        {
            return ScopedExisting->second;
        }
        const Scene::ComponentOps* ScopedOps = MComponents.Find(InComponent);
        if (ScopedOps == nullptr)
        {
            Fail("unknown component '" + InComponent + "'");
            return 0;
        }
        const auto ScopedField = std::find_if(ScopedOps->fields.begin(), ScopedOps->fields.end(),
                                              [&InField](const Reflect::FieldDesc& InDesc)
                                              { return InDesc.name == InField; });
        if (ScopedField == ScopedOps->fields.end())
        {
            Fail("component '" + InComponent + "' has no field '" + InField + "'");
            return 0;
        }
        const auto ScopedComponent = MComponentCache.find(InComponent);
        std::uint16_t ScopedComponentIndex = 0;
        if (ScopedComponent == MComponentCache.end())
        {
            ScopedComponentIndex = MBuilder.AddComponent(ScopedOps->layoutHash, ScopedOps->size);
            MComponentCache[InComponent] = ScopedComponentIndex;
        }
        else
        {
            ScopedComponentIndex = ScopedComponent->second;
        }
        const std::uint16_t ScopedFieldIndex =
            MBuilder.AddField(ScopedComponentIndex, ScopedField->kind, ScopedField->offset, ScopedField->size);
        MFieldBindings.push_back(FieldBinding{ScopedComponentIndex, static_cast<std::uint8_t>(ScopedField->kind),
                                              static_cast<std::uint8_t>(ScopedField->size), ScopedField->offset, 0});
        MFieldCache[ScopedKey] = MFieldBindings.size() - 1;
        return static_cast<std::uint32_t>(MFieldBindings.size() - 1);
    }

    [[nodiscard]] std::uint8_t EntityRegister()
    {
        if (MEntityRegister == 0xFF)
        {
            MEntityRegister = Allocate();
            Emit(OpCode::LoadSelf, MEntityRegister);
        }
        return MEntityRegister;
    }

    [[nodiscard]] std::uint8_t Allocate()
    {
        if (MNextRegister >= kMaxVmRegisters)
        {
            Fail("text script exceeds the 128-register file");
            return 0;
        }
        return static_cast<std::uint8_t>(MNextRegister++);
    }

    [[nodiscard]] bool Expect(std::string_view InText)
    {
        if (MFailed)
        {
            return false;
        }
        if (MToken.text != InText)
        {
            Fail("expected '" + std::string(InText) + "'");
            return false;
        }
        MToken = MLexer.Next();
        return true;
    }

    [[nodiscard]] bool Accept(std::string_view InText)
    {
        if (MFailed || MToken.text != InText)
        {
            return false;
        }
        MToken = MLexer.Next();
        return true;
    }

    [[nodiscard]] bool ExpectIdentifier()
    {
        if (MFailed || MToken.kind != TokenKind::Identifier)
        {
            Fail("expected an identifier");
            return false;
        }
        MToken = MLexer.Next();
        return true;
    }

    [[nodiscard]] bool ExpectNumber()
    {
        if (MFailed || MToken.kind != TokenKind::Number)
        {
            Fail("expected a number");
            return false;
        }
        MToken = MLexer.Next();
        return true;
    }

    void Fail(const std::string& InMessage)
    {
        if (MFailed)
        {
            return;
        }
        MFailed = true;
        if (MError != nullptr)
        {
            *MError = "text script line " + std::to_string(MToken.line) + ": " + InMessage;
        }
    }

    Lexer MLexer;
    const Scene::ComponentRegistry& MComponents;
    TextCompileOptions MOptions;
    std::string* MError = nullptr;
    ProgramBuilder MBuilder;
    Token MToken;
    std::unordered_map<std::string, std::uint8_t> MVariables;
    std::unordered_map<std::string, std::uint16_t> MComponentCache;
    std::unordered_map<std::string, std::uint32_t> MFieldCache;
    std::vector<FieldBinding> MFieldBindings;
    std::uint32_t MNextRegister = 0;
    std::uint32_t MLastRegister = 0;
    std::uint8_t MEntityRegister = 0xFF;
    bool MFailed = false;
};
}

bool CompileTextScript(std::string_view InSource, const Scene::ComponentRegistry& InComponents,
                       const TextCompileOptions& InOptions, std::vector<std::byte>* OutProgram, std::string* InError)
{
    if (OutProgram == nullptr)
    {
        if (InError != nullptr)
        {
            *InError = "CompileTextScript: null output";
        }
        return false;
    }
    OutProgram->clear();
    if (InError != nullptr)
    {
        InError->clear();
    }
    Compiler ScopedCompiler(InSource, InComponents, InOptions, InError);
    return ScopedCompiler.Run(OutProgram);
}
}
