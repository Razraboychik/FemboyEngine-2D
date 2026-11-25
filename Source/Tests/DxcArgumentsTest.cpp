// FemboyEngine2D - dxc command-line contract regression test.
#include "Tools/MaterialCompiler/DxcArguments.hpp"

#include <cstdio>
#include <string>
#include <vector>

namespace
{
int GFailures = 0;

void Check(bool InCondition, const char* InMessage)
{
    if (!InCondition)
    {
        std::printf("FAIL: %s\n", InMessage);
        std::fflush(stdout);
        ++GFailures;
    }
}
}

int main()
{
    using Fby::Tools::MaterialCompiler::BuildDxcArguments;
    using Fby::Tools::MaterialCompiler::IsSafeProcessArgument;
    using Fby::Tools::MaterialCompiler::QuoteWindowsArgument;

    Check(!IsSafeProcessArgument(""), "empty arguments are rejected");
    Check(!IsSafeProcessArgument("line\nbreak"), "line feeds are rejected");
    Check(!IsSafeProcessArgument("carriage\rreturn"), "carriage returns are rejected");
    Check(IsSafeProcessArgument("path;with & metacharacters"), "shell metacharacters remain literal data");

    const std::string ScopedInjection = "input; touch pwned.hlsl";
    const auto ScopedArguments = BuildDxcArguments("dxc", ScopedInjection, "out.femmat", "ps_6_0");
    Check(ScopedArguments.has_value(), "the argument builder accepts a metacharacter path");
    if (ScopedArguments.has_value())
    {
        const std::vector<std::string>& ScopedArgv = *ScopedArguments;
        if (ScopedArgv.size() != 8)
        {
            Check(false, "dxc receives one argument per token");
        }
        else
        {
        Check(ScopedArgv[0] == "dxc", "compiler remains argv[0]");
        Check(ScopedArgv[1] == "-T" && ScopedArgv[2] == "ps_6_0", "profile is a separate argument");
        Check(ScopedArgv[3] == "-E" && ScopedArgv[4] == "main", "entry point is a separate argument");
        Check(ScopedArgv[5] == "-Fo" && ScopedArgv[6] == "out.femmat", "output is a separate argument");
        Check(ScopedArgv[7] == ScopedInjection, "the injected path stays one literal argument");
        }
    }

    std::string ScopedNul("dxc\0evil", 8);
    Check(!BuildDxcArguments(ScopedNul, "in.hlsl", "out.femmat", "ps_6_0"), "embedded NULs are rejected");
    Check(!BuildDxcArguments("dxc", "in.hlsl", "out.femmat", "ps_6_0\n"), "profile newlines are rejected");

    Check(QuoteWindowsArgument(L"plain") == L"plain", "plain Windows arguments are not quoted");
    Check(QuoteWindowsArgument(L"a b") == L"\"a b\"", "spaces force Windows quoting");
    Check(QuoteWindowsArgument(L"quote\"inside") == L"\"quote\\\"inside\"", "quotes are escaped for Windows");
    Check(QuoteWindowsArgument(L"a b\\") == L"\"a b\\\\\"", "trailing backslashes survive Windows quoting");
    Check(QuoteWindowsArgument(L"") == L"\"\"", "empty Windows arguments are quoted");

    if (GFailures != 0)
    {
        std::printf("%d dxc argument test(s) failed\n", GFailures);
        return 1;
    }
    std::printf("dxc_arguments_test: all tests passed\n");
    return 0;
}
