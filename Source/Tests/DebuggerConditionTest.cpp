#include "Engine/Script/DebuggerCondition.hpp"

#include <cstdio>

namespace
{
std::uint32_t GFailures = 0;

void Check(bool InCondition, const char* InMessage)
{
    if (!InCondition)
    {
        std::fprintf(stderr, "FAIL: %s\n", InMessage);
        ++GFailures;
    }
}
}

int main()
{
    Fby::Script::DebuggerCondition ScopedCondition;
    Check(Fby::Script::ParseDebuggerCondition("r3 >= 12.5", &ScopedCondition), "greater-equal parses");
    Check(ScopedCondition.Register == 3 && ScopedCondition.Operator == Fby::Script::DebuggerCondition::Op::GreaterEqual &&
              ScopedCondition.Value == 12.5f,
          "condition fields");
    Check(Fby::Script::EvaluateDebuggerCondition(ScopedCondition, 12.5f), "equal boundary passes");
    Check(!Fby::Script::EvaluateDebuggerCondition(ScopedCondition, 12.0f), "smaller value fails");

    Check(Fby::Script::ParseDebuggerCondition("r0 != 0", &ScopedCondition) && ScopedCondition.Register == 0,
          "not-equal parses");
    Check(Fby::Script::EvaluateDebuggerCondition(ScopedCondition, 1.0f), "not-equal evaluates");
    Check(!Fby::Script::ParseDebuggerCondition("r", &ScopedCondition), "missing register rejected");
    Check(!Fby::Script::ParseDebuggerCondition("r1 > 2x", &ScopedCondition), "trailing text rejected");
    Check(!Fby::Script::ParseDebuggerCondition("r1 & 2", &ScopedCondition), "unknown operator rejected");

    std::uint32_t ScopedParsed = 0;
    for (std::uint32_t ScopedIteration = 0; ScopedIteration < 100000; ++ScopedIteration)
    {
        if (Fby::Script::ParseDebuggerCondition("r7 < 99.5", &ScopedCondition) &&
            Fby::Script::EvaluateDebuggerCondition(ScopedCondition, 12.0f))
        {
            ++ScopedParsed;
        }
    }
    Check(ScopedParsed == 100000, "condition parse/evaluate stress");

    if (GFailures == 0)
    {
        std::printf("debugger_condition_test: all checks passed\n");
        return 0;
    }
    return 1;
}
