// FemboyEngine2D - conditional breakpoint expressions for the script debugger.
#pragma once

#include <cctype>
#include <cstdlib>
#include <charconv>
#include <cstdint>
#include <string>
#include <string_view>

namespace Fby::Script
{
struct DebuggerCondition
{
    enum class Op : std::uint8_t
    {
        Equal,
        NotEqual,
        Less,
        LessEqual,
        Greater,
        GreaterEqual
    };

    std::uint32_t Register = 0;
    Op Operator = Op::Equal;
    float Value = 0.0f;
};

// Parses `r<register> <op> <number>`, for example `r3 >= 12.5`.
[[nodiscard]] inline bool ParseDebuggerCondition(std::string_view InText, DebuggerCondition* OutCondition) noexcept
{
    if (OutCondition == nullptr)
    {
        return false;
    }
    while (!InText.empty() && std::isspace(static_cast<unsigned char>(InText.front())) != 0)
    {
        InText.remove_prefix(1);
    }
    while (!InText.empty() && std::isspace(static_cast<unsigned char>(InText.back())) != 0)
    {
        InText.remove_suffix(1);
    }
    if (InText.size() < 5 || InText.front() != 'r')
    {
        return false;
    }
    std::size_t ScopedCursor = 1;
    std::uint32_t ScopedRegister = 0;
    const auto ScopedRegisterResult =
        std::from_chars(InText.data() + ScopedCursor, InText.data() + InText.size(), ScopedRegister);
    if (ScopedRegisterResult.ec != std::errc{} || ScopedRegisterResult.ptr == InText.data() + ScopedCursor)
    {
        return false;
    }
    ScopedCursor = static_cast<std::size_t>(ScopedRegisterResult.ptr - InText.data());
    while (ScopedCursor < InText.size() && std::isspace(static_cast<unsigned char>(InText[ScopedCursor])) != 0)
    {
        ++ScopedCursor;
    }
    if (ScopedCursor >= InText.size())
    {
        return false;
    }
    DebuggerCondition::Op ScopedOp = DebuggerCondition::Op::Equal;
    if (InText.substr(ScopedCursor, 2) == "==")
    {
        ScopedOp = DebuggerCondition::Op::Equal;
        ScopedCursor += 2;
    }
    else if (InText.substr(ScopedCursor, 2) == "!=")
    {
        ScopedOp = DebuggerCondition::Op::NotEqual;
        ScopedCursor += 2;
    }
    else if (InText.substr(ScopedCursor, 2) == "<=")
    {
        ScopedOp = DebuggerCondition::Op::LessEqual;
        ScopedCursor += 2;
    }
    else if (InText.substr(ScopedCursor, 2) == ">=")
    {
        ScopedOp = DebuggerCondition::Op::GreaterEqual;
        ScopedCursor += 2;
    }
    else if (InText[ScopedCursor] == '<')
    {
        ScopedOp = DebuggerCondition::Op::Less;
        ++ScopedCursor;
    }
    else if (InText[ScopedCursor] == '>')
    {
        ScopedOp = DebuggerCondition::Op::Greater;
        ++ScopedCursor;
    }
    else
    {
        return false;
    }
    while (ScopedCursor < InText.size() && std::isspace(static_cast<unsigned char>(InText[ScopedCursor])) != 0)
    {
        ++ScopedCursor;
    }
    if (ScopedCursor >= InText.size())
    {
        return false;
    }
    const std::string ScopedNumber(InText.substr(ScopedCursor));
    char* ScopedEnd = nullptr;
    const float ScopedValue = std::strtof(ScopedNumber.c_str(), &ScopedEnd);
    if (ScopedEnd != ScopedNumber.c_str() + ScopedNumber.size())
    {
        return false;
    }
    OutCondition->Register = ScopedRegister;
    OutCondition->Operator = ScopedOp;
    OutCondition->Value = ScopedValue;
    return true;
}

[[nodiscard]] inline bool EvaluateDebuggerCondition(const DebuggerCondition& InCondition, float InRegisterValue) noexcept
{
    switch (InCondition.Operator)
    {
        case DebuggerCondition::Op::Equal:
            return InRegisterValue == InCondition.Value;
        case DebuggerCondition::Op::NotEqual:
            return InRegisterValue != InCondition.Value;
        case DebuggerCondition::Op::Less:
            return InRegisterValue < InCondition.Value;
        case DebuggerCondition::Op::LessEqual:
            return InRegisterValue <= InCondition.Value;
        case DebuggerCondition::Op::Greater:
            return InRegisterValue > InCondition.Value;
        case DebuggerCondition::Op::GreaterEqual:
            return InRegisterValue >= InCondition.Value;
    }
    return false;
}
}
