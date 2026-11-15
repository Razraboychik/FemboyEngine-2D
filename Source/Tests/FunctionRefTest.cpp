// FemboyEngine2D - non-owning FunctionRef: free functions and capturing lambdas.
#include "Engine/Core/FunctionRef.hpp"

#include <cstdio>

namespace
{
int GFailures = 0;

void Check(bool InCondition, const char* InExpression = "")
{
    if (!InCondition)
    {
        ++GFailures;
        std::fprintf(stderr, "FAIL: %s\n", InExpression);
    }
}

int AddOne(int InValue)
{
    return InValue + 1;
}
}

int main()
{
    Fby::FunctionRef<int(int)> ScopedFree(AddOne);
    Check(ScopedFree(41) == 42);

    int ScopedBias = 7;
    const auto ScopedLambda = [&ScopedBias](int InValue) { return InValue + ScopedBias; };
    Fby::FunctionRef<int(int)> ScopedCaptured(ScopedLambda);
    Check(ScopedCaptured(35) == 42);

    ScopedBias = 0;
    Check(ScopedCaptured(42) == 42);
    Check(static_cast<bool>(ScopedFree));

    if (GFailures != 0)
    {
        std::fprintf(stderr, "function_ref: %d check(s) failed\n", GFailures);
        return 1;
    }
    std::printf("function_ref: all checks passed\n");
    return 0;
}
