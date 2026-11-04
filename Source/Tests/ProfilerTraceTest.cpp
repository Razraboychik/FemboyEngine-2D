// FemboyEngine2D - Chrome trace export tests.
#include "Engine/Core/ProfilerTrace.hpp"

#include <cstdio>
#include <string>
#include <string_view>

namespace
{
int GFailures = 0;

void Check(bool InCondition, const char* InMessage)
{
    if (!InCondition)
    {
        std::fprintf(stderr, "FAIL: %s\n", InMessage);
        ++GFailures;
    }
}

[[nodiscard]] std::size_t CountCharacter(const std::string& InText, char InCharacter)
{
    std::size_t ScopedCount = 0;
    for (const char ScopedCharacter : InText)
    {
        if (ScopedCharacter == InCharacter)
        {
            ++ScopedCount;
        }
    }
    return ScopedCount;
}

void TestExport()
{
    Fby::Core::Profiler ScopedProfiler(64);
    ScopedProfiler.RecordAt(0, 1000, 500);
    ScopedProfiler.RecordAt(1, 2000, 250);
    const std::string_view ScopedNames[] = {"Lighting", "Physics"};
    const std::string ScopedJson = Fby::Core::ToChromeTraceJson(ScopedProfiler, ScopedNames);
    Check(ScopedJson.find("\"traceEvents\":[") != std::string::npos, "the trace list is present");
    Check(ScopedJson.find("\"name\":\"Lighting\"") != std::string::npos, "the first name is exported");
    Check(ScopedJson.find("\"name\":\"Physics\"") != std::string::npos, "the second name is exported");
    Check(ScopedJson.find("\"ts\":1.000") != std::string::npos, "the start is in microseconds");
    Check(ScopedJson.find("\"dur\":0.500") != std::string::npos, "the duration is in microseconds");
    Check(ScopedJson.find("\"pid\":1") != std::string::npos, "the process id is exported");
    Check(ScopedJson.find("\"tid\":0") != std::string::npos, "the thread id is exported");
    Check(CountCharacter(ScopedJson, '{') == CountCharacter(ScopedJson, '}'), "the braces balance");
    Check(CountCharacter(ScopedJson, '[') == CountCharacter(ScopedJson, ']'), "the brackets balance");
}

void TestEscaping()
{
    Fby::Core::Profiler ScopedProfiler(8);
    ScopedProfiler.RecordAt(0, 0, 1000);
    const std::string_view ScopedNames[] = {"quo\"te\\slash"};
    const std::string ScopedJson = Fby::Core::ToChromeTraceJson(ScopedProfiler, ScopedNames);
    Check(ScopedJson.find("quo\\\"te\\\\slash") != std::string::npos, "quotes and backslashes are escaped");
    Check(CountCharacter(ScopedJson, '{') == CountCharacter(ScopedJson, '}'), "escaped braces still balance");
}

void TestEmptyAndUnknown()
{
    Fby::Core::Profiler ScopedProfiler(8);
    const std::string_view ScopedNames[] = {"Only"};
    const std::string ScopedEmpty = Fby::Core::ToChromeTraceJson(ScopedProfiler, ScopedNames);
    Check(ScopedEmpty == "{\"traceEvents\":[]}", "an empty profiler exports an empty trace");

    // A category without a name is skipped rather than exported as an empty name.
    ScopedProfiler.RecordAt(5, 0, 100);
    const std::string ScopedUnknown = Fby::Core::ToChromeTraceJson(ScopedProfiler, ScopedNames);
    Check(ScopedUnknown == "{\"traceEvents\":[]}", "an unnamed category is skipped");
}
}

int main()
{
    TestExport();
    TestEscaping();
    TestEmptyAndUnknown();
    if (GFailures != 0)
    {
        std::fprintf(stderr, "%d profiler trace test(s) failed\n", GFailures);
        return 1;
    }
    std::printf("profiler_trace: OK\n");
    return 0;
}
