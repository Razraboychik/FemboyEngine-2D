// FemboyEngine2D - VisualScript graph -> FemboyScript bytecode compiler.
#pragma once

#include "Engine/Script/Bytecode.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace Fby::Script
{

inline constexpr std::uint32_t kMaxLogicNodes = 128;
inline constexpr std::uint32_t kMaxLogicLinks = 256;
inline constexpr std::uint32_t kMaxLogicInputs = 4;
inline constexpr std::uint32_t kMaxLogicFields = 64;

// Comparison selector for the Condition node. It is stored in LogicNode::param
enum class LogicCompareOp : std::uint8_t
{
    Equal = 0,
    NotEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    Count
};

[[nodiscard]] constexpr const char* LogicCompareOpName(LogicCompareOp InOp) noexcept
{
    switch (InOp)
    {
        case LogicCompareOp::Equal:
            return "==";
        case LogicCompareOp::NotEqual:
            return "!=";
        case LogicCompareOp::Less:
            return "<";
        case LogicCompareOp::LessEqual:
            return "<=";
        case LogicCompareOp::Greater:
            return ">";
        case LogicCompareOp::GreaterEqual:
            return ">=";
        case LogicCompareOp::Count:
            break;
    }
    return "?";
}

// Math node selector. These are the same operations the legacy explicit nodes
enum class LogicMathOp : std::uint8_t
{
    Add = 0,
    Sub,
    Mul,
    Div,
    Min,
    Max,
    Count
};

[[nodiscard]] constexpr const char* LogicMathOpName(LogicMathOp InOp) noexcept
{
    switch (InOp)
    {
        case LogicMathOp::Add:
            return "Add";
        case LogicMathOp::Sub:
            return "Sub";
        case LogicMathOp::Mul:
            return "Mul";
        case LogicMathOp::Div:
            return "Div";
        case LogicMathOp::Min:
            return "Min";
        case LogicMathOp::Max:
            return "Max";
        case LogicMathOp::Count:
            break;
    }
    return "?";
}

// One component field a graph may touch. The LayoutHash/offset pair is exactly
struct LogicFieldBinding
{
    std::uint64_t layoutHash = 0;
    std::uint32_t componentSize = 0;
    std::uint32_t offset = 0;
    std::uint32_t fieldSize = 0;
    // Reflect::FieldKind
    std::uint8_t kind = 0;
    std::uint8_t reserved0 = 0;
    std::uint16_t reserved1 = 0;
    char componentName[24] = {};
    char fieldName[32] = {};

    [[nodiscard]] bool IsVec2() const noexcept
    {
        return kind == static_cast<std::uint8_t>(Reflect::FieldKind::Vector2D);
    }
};

static_assert(sizeof(LogicFieldBinding) == 80, "LogicFieldBinding is an 80-byte self-describing field record");

enum class LogicNodeKind : std::uint8_t
{
    EventStart = 0,
    EventUpdate,
    ConstFloat,
    ConstVec2,
    SelfPosition,
    SelfVelocity,
    SelfRotation,
    DeltaTime,
    Random01,
    Add,
    Sub,
    Mul,
    Div,
    Min,
    Max,
    // Vec2 * float -> vec2
    Vec2Scale,
    LogValue,
    SetPosition,
    SetVelocity,
    DespawnSelf,
    Branch,
    // Reads self's component field; param = field index
    GetField = 22,
    // Exec + value; param = field index
    SetField = 23,
    // Two floats -> float 0.0/1.0; param = LogicCompareOp
    Condition = 24,
    // Two floats -> float; param = LogicMathOp
    Math = 25,
    // Exec -> exec; param = prefab id
    SpawnEntity = 26,
    // Float -> BigInt token
    BigIntFromInt = 27,
    // Two BigInt tokens -> BigInt token
    BigIntAdd = 28,
    BigIntSub = 29,
    BigIntMul = 30,
    BigIntDiv = 31,
    BigIntMod = 32,
    // Two BigInt tokens -> float comparison
    BigIntCmp = 33,
    // BigInt token -> float
    BigIntToInt = 34,
    // Exec + BigInt token
    BigIntLog = 35,
    // Float -> Decimal token
    DecimalFromInt = 36,
    // Two Decimal tokens -> Decimal token
    DecimalAdd = 37,
    DecimalSub = 38,
    DecimalMul = 39,
    DecimalDiv = 40,
    // Two Decimal tokens -> float comparison
    DecimalCmp = 41,
    // Decimal token -> float
    DecimalToInt = 42,
    // Exec + Decimal token
    DecimalLog = 43,
    // Two Decimal tokens + rounding mode float -> Decimal token
    DecimalDivRounded = 44,
    Count
};

[[nodiscard]] constexpr const char* LogicNodeKindName(LogicNodeKind InKind) noexcept
{
    switch (InKind)
    {
        case LogicNodeKind::EventStart:
            return "EventStart";
        case LogicNodeKind::EventUpdate:
            return "EventUpdate";
        case LogicNodeKind::ConstFloat:
            return "ConstFloat";
        case LogicNodeKind::ConstVec2:
            return "ConstVec2";
        case LogicNodeKind::SelfPosition:
            return "SelfPosition";
        case LogicNodeKind::SelfVelocity:
            return "SelfVelocity";
        case LogicNodeKind::SelfRotation:
            return "SelfRotation";
        case LogicNodeKind::DeltaTime:
            return "DeltaTime";
        case LogicNodeKind::Random01:
            return "Random01";
        case LogicNodeKind::Add:
            return "Add";
        case LogicNodeKind::Sub:
            return "Sub";
        case LogicNodeKind::Mul:
            return "Mul";
        case LogicNodeKind::Div:
            return "Div";
        case LogicNodeKind::Min:
            return "Min";
        case LogicNodeKind::Max:
            return "Max";
        case LogicNodeKind::Vec2Scale:
            return "Vec2Scale";
        case LogicNodeKind::LogValue:
            return "LogValue";
        case LogicNodeKind::SetPosition:
            return "SetPosition";
        case LogicNodeKind::SetVelocity:
            return "SetVelocity";
        case LogicNodeKind::DespawnSelf:
            return "DespawnSelf";
        case LogicNodeKind::Branch:
            return "Branch";
        case LogicNodeKind::GetField:
            return "Get Component";
        case LogicNodeKind::SetField:
            return "Set Field";
        case LogicNodeKind::Condition:
            return "Condition";
        case LogicNodeKind::Math:
            return "Math";
        case LogicNodeKind::SpawnEntity:
            return "Spawn Entity";
        case LogicNodeKind::BigIntFromInt:
            return "BigInt From Int";
        case LogicNodeKind::BigIntAdd:
            return "BigInt Add";
        case LogicNodeKind::BigIntSub:
            return "BigInt Sub";
        case LogicNodeKind::BigIntMul:
            return "BigInt Mul";
        case LogicNodeKind::BigIntDiv:
            return "BigInt Div";
        case LogicNodeKind::BigIntMod:
            return "BigInt Mod";
        case LogicNodeKind::BigIntCmp:
            return "BigInt Compare";
        case LogicNodeKind::BigIntToInt:
            return "BigInt To Int";
        case LogicNodeKind::BigIntLog:
            return "BigInt Log";
        case LogicNodeKind::DecimalFromInt:
            return "Decimal From Int";
        case LogicNodeKind::DecimalAdd:
            return "Decimal Add";
        case LogicNodeKind::DecimalSub:
            return "Decimal Sub";
        case LogicNodeKind::DecimalMul:
            return "Decimal Mul";
        case LogicNodeKind::DecimalDiv:
            return "Decimal Div";
        case LogicNodeKind::DecimalDivRounded:
            return "Decimal Div Rounded";
        case LogicNodeKind::DecimalCmp:
            return "Decimal Compare";
        case LogicNodeKind::DecimalToInt:
            return "Decimal To Int";
        case LogicNodeKind::DecimalLog:
            return "Decimal Log";
        case LogicNodeKind::Count:
            break;
    }
    return "?";
}

enum class PinType : std::uint8_t
{
    Exec = 0,
    Float,
    Vector2D,
    // A Set Field value pin is typed by its field binding rather than by the
    Any,
    // Opaque script token into the big number pool.
    BigInt,
    // Opaque script token into the decimal pool.
    Decimal,
    Count
};

// A node is 20 bytes; the graph is three flat arrays plus capacity-bounded
struct LogicNode
{
    LogicNodeKind kind = LogicNodeKind::EventUpdate;
    // Reserved for a packed input table
    std::uint16_t firstInput = 0;
    std::uint8_t inputCount = 0;
    // PinType of the primary output
    std::uint8_t outputType = 0;
    // Float bits / prefab id / layer
    std::uint32_t param = 0;
    // Float bits (second component of ConstVec2)
    std::uint32_t param2 = 0;
    std::uint16_t flags = 0;
    std::uint16_t reserved = 0;
};

static_assert(sizeof(LogicNode) == 20);

struct LogicLink
{
    std::uint16_t fromNode = 0;
    std::uint16_t toNode = 0;
    std::uint8_t fromPin = 0;
    std::uint8_t toPin = 0;
    std::uint16_t reserved = 0;
};

static_assert(sizeof(LogicLink) == 8);

struct LogicGraph
{
    std::array<LogicNode, kMaxLogicNodes> nodes{};
    std::array<LogicLink, kMaxLogicLinks> links{};
    std::array<LogicFieldBinding, kMaxLogicFields> fields{};
    std::uint32_t nodeCount = 0;
    std::uint32_t linkCount = 0;
    std::uint32_t fieldCount = 0;

    void Clear() noexcept
    {
        nodes.fill(LogicNode{});
        links.fill(LogicLink{});
        fields.fill(LogicFieldBinding{});
        nodeCount = 0;
        linkCount = 0;
        fieldCount = 0;
    }

    // Returns 0xFFFF when the graph is full. Indices are stable: links reference
    [[nodiscard]] std::uint16_t AddNode(LogicNodeKind InKind, std::uint32_t InParam = 0) noexcept
    {
        if (nodeCount >= kMaxLogicNodes || InKind >= LogicNodeKind::Count)
        {
            return 0xFFFFu;
        }
        LogicNode ScopedNode;
        ScopedNode.kind = InKind;
        ScopedNode.param = InParam;
        ScopedNode.outputType = static_cast<std::uint8_t>(OutputType(InKind, 0));
        ScopedNode.inputCount = InputCount(InKind);
        nodes[nodeCount] = ScopedNode;
        return static_cast<std::uint16_t>(nodeCount++);
    }

    // Registers a component field and returns its stable graph-local index.
    [[nodiscard]] std::uint16_t AddFieldBinding(const LogicFieldBinding& InBinding) noexcept
    {
        if (fieldCount >= kMaxLogicFields)
        {
            return 0xFFFFu;
        }
        fields[fieldCount] = InBinding;
        return static_cast<std::uint16_t>(fieldCount++);
    }

    // Creates a GetField/SetField node whose value type comes from the bound
    [[nodiscard]] std::uint16_t AddFieldNode(LogicNodeKind InKind, std::uint16_t InFieldIndex) noexcept
    {
        if ((InKind != LogicNodeKind::GetField && InKind != LogicNodeKind::SetField) || InFieldIndex >= fieldCount ||
            nodeCount >= kMaxLogicNodes)
        {
            return 0xFFFFu;
        }
        LogicNode ScopedNode;
        ScopedNode.kind = InKind;
        ScopedNode.param = InFieldIndex;
        ScopedNode.inputCount = InputCount(InKind);
        ScopedNode.outputType = InKind == LogicNodeKind::GetField
                                    ? static_cast<std::uint8_t>(FieldPinType(fields[InFieldIndex].kind))
                                    : static_cast<std::uint8_t>(PinType::Exec);
        nodes[nodeCount] = ScopedNode;
        return static_cast<std::uint16_t>(nodeCount++);
    }

    [[nodiscard]] static constexpr PinType FieldPinType(std::uint8_t InFieldKind) noexcept
    {
        return InFieldKind == static_cast<std::uint8_t>(Reflect::FieldKind::Vector2D) ? PinType::Vector2D
                                                                                      : PinType::Float;
    }

    [[nodiscard]] bool AddLink(std::uint16_t InFromNode, std::uint8_t InFromPin, std::uint16_t InToNode,
                               std::uint8_t InToPin) noexcept
    {
        if (linkCount >= kMaxLogicLinks || InFromNode >= nodeCount || InToNode >= nodeCount)
        {
            return false;
        }
        // One connection per input: replace semantics avoid two links fighting
        for (std::uint32_t ScopedI = 0; ScopedI < linkCount; ++ScopedI)
        {
            if (links[ScopedI].toNode == InToNode && links[ScopedI].toPin == InToPin)
            {
                return false;
            }
        }
        LogicLink ScopedLink;
        ScopedLink.fromNode = InFromNode;
        ScopedLink.fromPin = InFromPin;
        ScopedLink.toNode = InToNode;
        ScopedLink.toPin = InToPin;
        links[linkCount++] = ScopedLink;
        return true;
    }

    [[nodiscard]] bool RemoveLinkToInput(std::uint16_t InToNode, std::uint8_t InToPin) noexcept
    {
        for (std::uint32_t ScopedI = 0; ScopedI < linkCount; ++ScopedI)
        {
            if (links[ScopedI].toNode == InToNode && links[ScopedI].toPin == InToPin)
            {
                links[ScopedI] = links[linkCount - 1];
                --linkCount;
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] static constexpr PinType OutputType(LogicNodeKind InKind, std::uint8_t InPin) noexcept
    {
        switch (InKind)
        {
            case LogicNodeKind::EventStart:
            case LogicNodeKind::EventUpdate:
                return PinType::Exec;
            case LogicNodeKind::SelfPosition:
            case LogicNodeKind::SelfVelocity:
            case LogicNodeKind::ConstVec2:
            case LogicNodeKind::Vec2Scale:
                return PinType::Vector2D;
            case LogicNodeKind::BigIntFromInt:
            case LogicNodeKind::BigIntAdd:
            case LogicNodeKind::BigIntSub:
            case LogicNodeKind::BigIntMul:
            case LogicNodeKind::BigIntDiv:
            case LogicNodeKind::BigIntMod:
                return PinType::BigInt;
            case LogicNodeKind::BigIntCmp:
            case LogicNodeKind::BigIntToInt:
                return PinType::Float;
            case LogicNodeKind::BigIntLog:
                return PinType::Exec;
            case LogicNodeKind::DecimalFromInt:
            case LogicNodeKind::DecimalAdd:
            case LogicNodeKind::DecimalSub:
            case LogicNodeKind::DecimalMul:
            case LogicNodeKind::DecimalDiv:
            case LogicNodeKind::DecimalDivRounded:
                return PinType::Decimal;
            case LogicNodeKind::DecimalCmp:
            case LogicNodeKind::DecimalToInt:
                return PinType::Float;
            case LogicNodeKind::DecimalLog:
                return PinType::Exec;
            case LogicNodeKind::Branch:
                // Pin 0 = "true" exec branch, pin 1 = "false" exec branch.
                return PinType::Exec;
            case LogicNodeKind::LogValue:
            case LogicNodeKind::SetPosition:
            case LogicNodeKind::SetVelocity:
            case LogicNodeKind::DespawnSelf:
                // Statement nodes expose their success/follow-up exec pin first.
                return InPin == 0 ? PinType::Exec : PinType::Float;
            case LogicNodeKind::GetField:
                // The concrete type is stored in LogicNode::outputType when the
                return PinType::Float;
            case LogicNodeKind::SetField:
            case LogicNodeKind::SpawnEntity:
                return PinType::Exec;
            case LogicNodeKind::Condition:
            case LogicNodeKind::Math:
                return PinType::Float;
            default:
                return PinType::Float;
        }
    }

    [[nodiscard]] static constexpr std::uint8_t InputCount(LogicNodeKind InKind) noexcept
    {
        switch (InKind)
        {
            case LogicNodeKind::EventStart:
            case LogicNodeKind::EventUpdate:
            case LogicNodeKind::ConstFloat:
            case LogicNodeKind::ConstVec2:
            case LogicNodeKind::SelfPosition:
            case LogicNodeKind::SelfVelocity:
            case LogicNodeKind::SelfRotation:
            case LogicNodeKind::DeltaTime:
            case LogicNodeKind::Random01:
                return 0;
            case LogicNodeKind::Add:
            case LogicNodeKind::Sub:
            case LogicNodeKind::Mul:
            case LogicNodeKind::Div:
            case LogicNodeKind::Min:
            case LogicNodeKind::Max:
            case LogicNodeKind::BigIntAdd:
            case LogicNodeKind::BigIntSub:
            case LogicNodeKind::BigIntMul:
            case LogicNodeKind::BigIntDiv:
            case LogicNodeKind::BigIntMod:
            case LogicNodeKind::BigIntCmp:
            case LogicNodeKind::DecimalAdd:
            case LogicNodeKind::DecimalSub:
            case LogicNodeKind::DecimalMul:
            case LogicNodeKind::DecimalDiv:
            case LogicNodeKind::DecimalCmp:
                return 2;
            case LogicNodeKind::DecimalDivRounded:
                return 3;
            case LogicNodeKind::BigIntFromInt:
            case LogicNodeKind::BigIntToInt:
            case LogicNodeKind::DecimalFromInt:
            case LogicNodeKind::DecimalToInt:
                return 1;
            case LogicNodeKind::BigIntLog:
            case LogicNodeKind::DecimalLog:
                return 2;
            case LogicNodeKind::Vec2Scale:
                // Vec2, float
                return 2;
            case LogicNodeKind::LogValue:
                // Exec, float
                return 2;
            case LogicNodeKind::SetPosition:
            case LogicNodeKind::SetVelocity:
                // Exec, vec2
                return 2;
            case LogicNodeKind::DespawnSelf:
                // Exec
                return 1;
            case LogicNodeKind::Branch:
                // Exec, float condition
                return 2;
            case LogicNodeKind::GetField:
                return 0;
            case LogicNodeKind::SetField:
                // Exec, value
                return 2;
            case LogicNodeKind::Condition:
            case LogicNodeKind::Math:
                // A, B
                return 2;
            case LogicNodeKind::SpawnEntity:
                // Exec
                return 1;
            case LogicNodeKind::Count:
                break;
        }
        return 0;
    }

    // Expected input type for one pin of `kind`. Pin 0 of a statement is Exec.
    [[nodiscard]] static constexpr PinType InputType(LogicNodeKind InKind, std::uint8_t InPin) noexcept
    {
        switch (InKind)
        {
            case LogicNodeKind::LogValue:
            case LogicNodeKind::Branch:
                return InPin == 0 ? PinType::Exec : PinType::Float;
            case LogicNodeKind::SetPosition:
            case LogicNodeKind::SetVelocity:
                return InPin == 0 ? PinType::Exec : PinType::Vector2D;
            case LogicNodeKind::DespawnSelf:
                return PinType::Exec;
            case LogicNodeKind::Add:
            case LogicNodeKind::Sub:
            case LogicNodeKind::Mul:
            case LogicNodeKind::Div:
            case LogicNodeKind::Min:
            case LogicNodeKind::Max:
                return PinType::Float;
            case LogicNodeKind::BigIntFromInt:
                return PinType::Float;
            case LogicNodeKind::BigIntAdd:
            case LogicNodeKind::BigIntSub:
            case LogicNodeKind::BigIntMul:
            case LogicNodeKind::BigIntDiv:
            case LogicNodeKind::BigIntMod:
            case LogicNodeKind::BigIntCmp:
            case LogicNodeKind::BigIntToInt:
                return PinType::BigInt;
            case LogicNodeKind::BigIntLog:
                return InPin == 0 ? PinType::Exec : PinType::BigInt;
            case LogicNodeKind::DecimalFromInt:
                return PinType::Float;
            case LogicNodeKind::DecimalAdd:
            case LogicNodeKind::DecimalSub:
            case LogicNodeKind::DecimalMul:
            case LogicNodeKind::DecimalDiv:
            case LogicNodeKind::DecimalCmp:
            case LogicNodeKind::DecimalToInt:
                return PinType::Decimal;
            case LogicNodeKind::DecimalDivRounded:
                return InPin == 2 ? PinType::Float : PinType::Decimal;
            case LogicNodeKind::DecimalLog:
                return InPin == 0 ? PinType::Exec : PinType::Decimal;
            case LogicNodeKind::Vec2Scale:
                return InPin == 0 ? PinType::Vector2D : PinType::Float;
            case LogicNodeKind::SetField:
                return InPin == 0 ? PinType::Exec : PinType::Any;
            case LogicNodeKind::Condition:
            case LogicNodeKind::Math:
                return PinType::Float;
            case LogicNodeKind::SpawnEntity:
                return PinType::Exec;
            default:
                return PinType::Exec;
        }
    }
};

// Mirror -> cooked field binding. Returns a zeroed binding when the field does
template <typename Component>
[[nodiscard]] LogicFieldBinding MakeLogicFieldBinding(std::string_view InFieldName) noexcept
{
    static_assert(Reflect::IsRegistered<Component>(), "logic graphs can only bind Mirror components");
    LogicFieldBinding ScopedBinding;
    const Reflect::FieldDesc* ScopedFound = nullptr;
    for (const Reflect::FieldDesc& ScopedField : Reflect::Fields<Component>())
    {
        if (ScopedField.name == InFieldName)
        {
            ScopedFound = &ScopedField;
            break;
        }
    }
    if (ScopedFound == nullptr)
    {
        return ScopedBinding;
    }
    ScopedBinding.layoutHash = Reflect::LayoutHash<Component>();
    ScopedBinding.componentSize = static_cast<std::uint32_t>(sizeof(Component));
    ScopedBinding.offset = ScopedFound->offset;
    ScopedBinding.fieldSize = ScopedFound->size;
    ScopedBinding.kind = static_cast<std::uint8_t>(ScopedFound->kind);
    const std::string_view ScopedComponentName = Reflect::Reflection<Component>::kName;
    const auto ScopedCopy = [](char* InDestination, std::size_t InCapacity, std::string_view InText) noexcept
    {
        const std::size_t ScopedLength = InText.size() < InCapacity - 1 ? InText.size() : InCapacity - 1;
        for (std::size_t ScopedI = 0; ScopedI < ScopedLength; ++ScopedI)
        {
            InDestination[ScopedI] = InText[ScopedI];
        }
        InDestination[ScopedLength] = '\0';
    };
    ScopedCopy(ScopedBinding.componentName, sizeof(ScopedBinding.componentName), ScopedComponentName);
    ScopedCopy(ScopedBinding.fieldName, sizeof(ScopedBinding.fieldName), ScopedFound->name);
    return ScopedBinding;
}

// True for the fields a LogicGraph can read/write natively. Scalar ints/bools
[[nodiscard]] constexpr bool LogicFieldIsScriptable(std::uint8_t InFieldKind) noexcept
{
    return InFieldKind == static_cast<std::uint8_t>(Reflect::FieldKind::Float) ||
           InFieldKind == static_cast<std::uint8_t>(Reflect::FieldKind::Vector2D);
}

struct LogicCompileOptions
{
    // Cook-time hashing: the caller feeds the graph asset hash so two cooks of
    std::uint64_t graphHash = 0;
    std::uint64_t sourceHash = 0;
};

struct LogicCompileResult
{
    std::vector<std::byte> program{};
    std::uint32_t instructionCount = 0;
    std::uint32_t registerHighWater = 0;
    std::uint32_t componentBindings = 0;
    std::uint32_t fieldBindings = 0;
    std::string error{};
};

// Compiles the graph's single event node (EventStart or EventUpdate) into a
[[nodiscard]] std::uint64_t HashLogicGraph(const LogicGraph& InGraph) noexcept;

[[nodiscard]] bool CompileLogicGraph(const LogicGraph& InGraph, const LogicCompileOptions& InOptions,
                                     LogicCompileResult* Out);

// Convenience overload for editor call sites and tests.
[[nodiscard]] inline bool CompileLogicGraph(const LogicGraph& InGraph, LogicCompileResult* Out)
{
    return CompileLogicGraph(InGraph, LogicCompileOptions{}, Out);
}

static_assert(std::is_trivially_copyable_v<LogicGraph>, "LogicGraph is an undo/asset payload");
static_assert(std::is_standard_layout_v<LogicGraph>);
static_assert(sizeof(LogicGraph) <= 32768, "LogicGraph must fit one CommandStack external buffer");
}
