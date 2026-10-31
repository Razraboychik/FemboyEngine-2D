// FemboyEngine2D - FemboyScript 2.0 bytecode container.
#pragma once

#include "Engine/Core/Mirror.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace Fby::Script
{

// Native function ids are part of the bytecode ABI: the compiler emits them and
enum class NativeId : std::uint16_t
{
    LogFloat = 0,
    LogInt,
    LogVec2,
    LogMessage,
    DespawnSelf,
    SpawnPrefab,
    RandomFloat,
    RandomRange,
    PlaySound,
    GetDeltaTime,
    GetFrameIndex,
    BigIntFromInt,
    BigIntAdd,
    BigIntSub,
    BigIntMul,
    BigIntDiv,
    BigIntMod,
    BigIntCmp,
    BigIntToInt,
    BigIntLog,
    BigIntRelease,
    DecimalFromInt,
    DecimalAdd,
    DecimalSub,
    DecimalMul,
    DecimalDiv,
    DecimalDivRounded,
    DecimalCmp,
    DecimalToInt,
    DecimalLog,
    DecimalRelease,
    Count,
};

inline constexpr std::uint32_t kNativeFunctionCount = static_cast<std::uint32_t>(NativeId::Count);

// Number of consecutive registers a native reads/writes starting at the call
[[nodiscard]] constexpr std::uint32_t NativeArgumentCount(NativeId InNative) noexcept
{
    switch (InNative)
    {
        case NativeId::LogVec2:
        case NativeId::RandomRange:
        case NativeId::BigIntAdd:
        case NativeId::BigIntSub:
        case NativeId::BigIntMul:
        case NativeId::BigIntDiv:
        case NativeId::BigIntMod:
        case NativeId::BigIntCmp:
        case NativeId::DecimalAdd:
        case NativeId::DecimalSub:
        case NativeId::DecimalMul:
        case NativeId::DecimalDiv:
        case NativeId::DecimalCmp:
            return 2;
        case NativeId::DecimalDivRounded:
            return 3;
        case NativeId::LogFloat:
        case NativeId::LogInt:
        case NativeId::LogMessage:
        case NativeId::DespawnSelf:
        case NativeId::SpawnPrefab:
        case NativeId::RandomFloat:
        case NativeId::PlaySound:
        case NativeId::GetDeltaTime:
        case NativeId::GetFrameIndex:
        case NativeId::BigIntFromInt:
        case NativeId::BigIntToInt:
        case NativeId::BigIntLog:
        case NativeId::BigIntRelease:
        case NativeId::DecimalFromInt:
        case NativeId::DecimalToInt:
        case NativeId::DecimalLog:
        case NativeId::DecimalRelease:
            return 1;
        case NativeId::Count:
            break;
    }
    return 0;
}

// "FSBK" little-endian
inline constexpr std::uint32_t kProgramMagic = 0x4B425346u;
inline constexpr std::uint16_t kProgramVersion = 1;

// Optional debug data appended after the program tables. The flag keeps old
// programs loadable and lets a reader skip the section without parsing it.
inline constexpr std::uint16_t kProgramHeaderFlagDebug = 1u << 0;
inline constexpr std::uint32_t kDebugSectionMagic = 0x47424446u;
inline constexpr std::uint16_t kDebugSectionVersion = 1;
inline constexpr std::uint32_t kMaxDebugVariables = 256;
inline constexpr std::uint32_t kMaxDebugNameBytes = 8192;
inline constexpr std::uint32_t kMaxDebugSpans = 65536;

enum DebugSpanFlags : std::uint16_t
{
    DebugSpanNone = 0,
    // `Line` is a LogicGraph node id, not a text line.
    DebugSpanGraphNode = 1u << 0,
};

// One instruction-to-source mapping. Graph-compiled programs use `Line` as the
// node id; the text front-end stores the 1-based source line.
struct DebugSpan
{
    std::uint32_t Pc = 0;
    std::uint32_t Line = 0;
    std::uint32_t Column = 0;
    std::uint16_t Flags = 0;
    std::uint16_t Reserved = 0;
};

static_assert(sizeof(DebugSpan) == 16);

// A named register local (`let half = 0.5;`). `NameOffset` indexes the debug
// name blob.
struct DebugVariable
{
    std::uint8_t Register = 0;
    std::uint8_t Kind = 0;
    std::uint16_t NameOffset = 0;
};

static_assert(sizeof(DebugVariable) == 4);

struct DebugSectionHeader
{
    std::uint32_t Magic = kDebugSectionMagic;
    std::uint16_t Version = kDebugSectionVersion;
    std::uint16_t Flags = 0;
    std::uint32_t SpanCount = 0;
    std::uint32_t VariableCount = 0;
    std::uint32_t NameBytes = 0;
    std::uint32_t SourceNameOffset = 0;
    std::uint32_t Reserved[2] = {};
};

static_assert(sizeof(DebugSectionHeader) == 32);

inline constexpr std::uint32_t kMaxVmRegisters = 128;
inline constexpr std::uint32_t kMaxInstructions = 65536;
inline constexpr std::uint32_t kMaxConstants = 1024;
inline constexpr std::uint32_t kMaxComponentBindings = 32;
inline constexpr std::uint32_t kMaxFieldBindings = 256;
inline constexpr std::uint32_t kMaxFunctions = 16;
inline constexpr std::uint32_t kMaxProgramNatives = 32;
inline constexpr std::uint32_t kMaxStepsPerRun = 16384;

// A program entry point. Visual graphs compile to these; text scripts compile
enum class ScriptEvent : std::uint16_t
{
    Start = 0,
    Update = 1,
    FixedUpdate = 2,
    Count = 3,
};

[[nodiscard]] constexpr const char* ScriptEventName(ScriptEvent InEvent) noexcept
{
    switch (InEvent)
    {
        case ScriptEvent::Start:
            return "Start";
        case ScriptEvent::Update:
            return "Update";
        case ScriptEvent::FixedUpdate:
            return "FixedUpdate";
        case ScriptEvent::Count:
            break;
    }
    return "?";
}

// Deliberately small and closed so the AOT transpiler can enumerate every case.
enum class OpCode : std::uint8_t
{
    Nop = 0,
    Halt,

    // Dst = constants[operand]
    LoadF32,
    // Dst = (int32)(int16)operand: a signed 16-bit immediate, so negatives encode
    LoadI32,
    MoveF,
    MoveI,

    AddF,
    SubF,
    MulF,
    DivF,
    AddI,
    SubI,
    MulI,
    DivI,
    ModI,
    NegF,
    NegI,

    AndI,
    OrI,
    NotI,

    LtF,
    LeF,
    GtF,
    GeF,
    EqF,
    NeF,
    LtI,
    LeI,
    GtI,
    GeI,
    EqI,
    NeI,

    // Pc = operand
    Jump,
    // If (regs[dst].i == 0) pc = operand
    JumpIfZero,
    JumpIfNonZero,

    // Dst = self entity value
    LoadSelf,

    // Field access. `operand` indexes Program::fields; the component is resolved
    LoadFieldF,
    // Dst = field(src entity), 32-bit integral kinds
    LoadFieldI,
    // Dst, dst+1 = field(src entity)
    LoadFieldVec2,
    // Field(dst entity) = src
    StoreFieldF,
    StoreFieldI,
    // Field(dst entity) = src, src+1
    StoreFieldVec2,

    // Operand indexes Program::natives; dst is the native's arg base
    CallNative,

    // Dst = entity handle from VmContext::spawn, operand = prefab id
    SpawnPrefab,
    // Despawns src entity through the CommandBuffer
    DespawnEntity,

    // Dst = [0,1)
    RandomF,
    // Dst = [src, constants[operand])
    RandomRangeF,

    Count
};

[[nodiscard]] constexpr const char* OpCodeName(OpCode InOp) noexcept
{
    switch (InOp)
    {
        case OpCode::Nop:
            return "nop";
        case OpCode::Halt:
            return "halt";
        case OpCode::LoadF32:
            return "load.f32";
        case OpCode::LoadI32:
            return "load.i32";
        case OpCode::MoveF:
            return "move.f";
        case OpCode::MoveI:
            return "move.i";
        case OpCode::AddF:
            return "add.f";
        case OpCode::SubF:
            return "sub.f";
        case OpCode::MulF:
            return "mul.f";
        case OpCode::DivF:
            return "div.f";
        case OpCode::AddI:
            return "add.i";
        case OpCode::SubI:
            return "sub.i";
        case OpCode::MulI:
            return "mul.i";
        case OpCode::DivI:
            return "div.i";
        case OpCode::ModI:
            return "mod.i";
        case OpCode::NegF:
            return "neg.f";
        case OpCode::NegI:
            return "neg.i";
        case OpCode::AndI:
            return "and.i";
        case OpCode::OrI:
            return "or.i";
        case OpCode::NotI:
            return "not.i";
        case OpCode::LtF:
            return "lt.f";
        case OpCode::LeF:
            return "le.f";
        case OpCode::GtF:
            return "gt.f";
        case OpCode::GeF:
            return "ge.f";
        case OpCode::EqF:
            return "eq.f";
        case OpCode::NeF:
            return "ne.f";
        case OpCode::LtI:
            return "lt.i";
        case OpCode::LeI:
            return "le.i";
        case OpCode::GtI:
            return "gt.i";
        case OpCode::GeI:
            return "ge.i";
        case OpCode::EqI:
            return "eq.i";
        case OpCode::NeI:
            return "ne.i";
        case OpCode::Jump:
            return "jump";
        case OpCode::JumpIfZero:
            return "jump.zero";
        case OpCode::JumpIfNonZero:
            return "jump.nonzero";
        case OpCode::LoadSelf:
            return "load.self";
        case OpCode::LoadFieldF:
            return "load.field.f";
        case OpCode::LoadFieldI:
            return "load.field.i";
        case OpCode::LoadFieldVec2:
            return "load.field.vec2";
        case OpCode::StoreFieldF:
            return "store.field.f";
        case OpCode::StoreFieldI:
            return "store.field.i";
        case OpCode::StoreFieldVec2:
            return "store.field.vec2";
        case OpCode::CallNative:
            return "call.native";
        case OpCode::SpawnPrefab:
            return "spawn";
        case OpCode::DespawnEntity:
            return "despawn";
        case OpCode::RandomF:
            return "random.f";
        case OpCode::RandomRangeF:
            return "random.range";
        case OpCode::Count:
            break;
    }
    return "?";
}

struct Instruction
{
    std::uint8_t op = static_cast<std::uint8_t>(OpCode::Nop);
    std::uint8_t dst = 0;
    std::uint8_t src = 0;
    std::uint8_t flags = 0;
    std::uint16_t operand = 0;
    std::uint16_t reserved = 0;

    [[nodiscard]] OpCode Code() const noexcept
    {
        return static_cast<OpCode>(op);
    }
};

static_assert(sizeof(Instruction) == 8,
              "bytecode is an 8-byte stream; the VM decodes one instruction per cache line slot");
static_assert(std::is_trivially_copyable_v<Instruction>);

// One component type a program touches. The layout hash is the same hash the
struct ComponentBinding
{
    std::uint64_t layoutHash = 0;
    std::uint32_t size = 0;
    std::uint16_t firstField = 0;
    std::uint16_t fieldCount = 0;
};

static_assert(sizeof(ComponentBinding) == 16);

struct FieldBinding
{
    std::uint16_t component = 0;
    // Reflect::FieldKind
    std::uint8_t kind = 0;
    // Sizeof(member)
    std::uint8_t size = 0;
    std::uint32_t offset = 0;
    std::uint32_t reserved = 0;
};

static_assert(sizeof(FieldBinding) == 12);

struct FunctionEntry
{
    std::uint32_t codeOffset = 0;
    // ScriptEvent
    std::uint16_t event = 0;
    std::uint16_t registerHighWater = 0;
};

static_assert(sizeof(FunctionEntry) == 8);

struct ProgramHeader
{
    std::uint32_t magic = kProgramMagic;
    std::uint16_t version = kProgramVersion;
    std::uint16_t flags = 0;
    std::uint32_t codeCount = 0;
    std::uint32_t constantCount = 0;
    std::uint32_t fieldCount = 0;
    std::uint32_t componentCount = 0;
    std::uint32_t functionCount = 0;
    std::uint32_t nativeCount = 0;
    std::uint32_t registerHighWater = 0;
    std::uint32_t reserved = 0;
    std::uint64_t sourceHash = 0;
    std::uint64_t graphHash = 0;
    std::uint64_t reserved2 = 0;
};

static_assert(sizeof(ProgramHeader) == 64, "the program header is one cache line");
static_assert(offsetof(ProgramHeader, sourceHash) == 40);

// Optional instruction-to-source map parsed from the debug section. Every span
// is sorted by `Pc`, so a lookup is a binary search.
struct CompiledDebug
{
    std::span<const DebugSpan> spans{};
    std::span<const DebugVariable> variables{};
    std::string_view names{};
    std::string_view sourceName{};
    bool present = false;

    // Source line (or graph node id) that owns the instruction at `InPc`. The
    // lookup returns the last span at or before the pc, so a graph node span
    // covers its whole emitted range.
    [[nodiscard]] bool Locate(std::uint32_t InPc, DebugSpan* OutSpan) const noexcept;
};

// Zero-copy view of a cooked program. All spans point into `storage`, which the
struct CompiledProgram
{
    ProgramHeader header{};
    std::span<const Instruction> code{};
    std::span<const float> constants{};
    std::span<const FieldBinding> fields{};
    std::span<const ComponentBinding> components{};
    std::span<const FunctionEntry> functions{};
    std::span<const std::uint16_t> natives{};
    std::span<const std::byte> storage{};
    CompiledDebug debug{};
    bool valid = false;

    [[nodiscard]] const FunctionEntry* FindFunction(ScriptEvent InEvent) const noexcept
    {
        for (const FunctionEntry& ScopedFunction : functions)
        {
            if (ScopedFunction.event == static_cast<std::uint16_t>(InEvent))
            {
                return &ScopedFunction;
            }
        }
        return nullptr;
    }
};

// Validates magic, version, table sizes and cross-references (every instruction
[[nodiscard]] bool ParseProgram(std::span<const std::byte> InStorage, CompiledProgram* Out,
                                std::string* InError = nullptr);

// Compile-time builder
class ProgramBuilder
{
public:
    [[nodiscard]] std::uint16_t AddComponent(std::uint64_t InLayoutHash, std::uint32_t InComponentSize);
    [[nodiscard]] std::uint16_t AddField(std::uint16_t InComponent, Reflect::FieldKind InKind, std::uint32_t InOffset,
                                         std::uint32_t InFieldSize);
    [[nodiscard]] std::uint16_t AddConstant(float InValue);
    [[nodiscard]] std::uint16_t AddNative(NativeId InNative);
    std::uint32_t Emit(OpCode InOp, std::uint8_t InDst = 0, std::uint8_t InSrc = 0, std::uint16_t InOperand = 0,
                       std::uint8_t InFlags = 0);
    void PatchJump(std::uint32_t InInstructionIndex, std::uint32_t InTarget);
    [[nodiscard]] std::uint16_t AddFunction(ScriptEvent InEvent, std::uint32_t InCodeOffset);

    // Debug information. Optional; when absent the program is byte-identical to
    // a debug-free build apart from the header flag.
    void SetSourceName(std::string_view InName);
    void AddSpan(std::uint32_t InPc, std::uint32_t InLine, std::uint32_t InColumn, std::uint16_t InFlags = 0);
    void AddVariable(std::uint8_t InRegister, std::string_view InName, std::uint8_t InKind = 0);

    [[nodiscard]] std::uint32_t CodeSize() const noexcept
    {
        return static_cast<std::uint32_t>(MCode.size());
    }
    [[nodiscard]] std::uint32_t CurrentOffset() const noexcept
    {
        return static_cast<std::uint32_t>(MCode.size());
    }
    [[nodiscard]] bool HasDebugInfo() const noexcept
    {
        return !MSpans.empty() || !MVariables.empty() || !MSourceName.empty();
    }
    [[nodiscard]] std::vector<std::byte> Build(std::uint64_t InSourceHash, std::uint64_t InGraphHash,
                                               std::string* InError = nullptr) const;

private:
    struct PendingField
    {
        std::uint16_t component = 0;
        Reflect::FieldKind kind = Reflect::FieldKind::Float;
        std::uint32_t offset = 0;
        std::uint32_t size = 0;
    };

    std::vector<Instruction> MCode;
    std::vector<float> MConstants;
    std::vector<PendingField> MFields;
    std::vector<ComponentBinding> MComponents;
    std::vector<std::uint16_t> MNatives;
    std::vector<FunctionEntry> MFunctions;
    std::vector<DebugSpan> MSpans;
    std::vector<DebugVariable> MVariables;
    std::vector<std::string> MVariableNames;
    std::string MSourceName;
    std::uint32_t MRegisterHighWater = 0;
};

// The AOT path needs the exact bytes; expose a stable "light" operand limit so
inline constexpr std::uint32_t kMaxFieldBindingsLimit = kMaxFieldBindings;

// Stable identity of a cooked program: FNV-1a over the bytes. A cooked build
// keys its AOT manifest by this value, so the same program always finds the same
// native module regardless of load order or runtime handles.
[[nodiscard]] std::uint64_t HashProgramBytes(std::span<const std::byte> InCooked) noexcept;
}
