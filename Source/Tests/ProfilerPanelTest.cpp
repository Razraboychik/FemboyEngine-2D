// FemboyEngine2D - profiler panel model tests.
#include "Engine/Core/ProfilerPanel.hpp"

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

void TestRowsAndSorting()
{
    Fby::Core::Profiler ScopedProfiler(256);
    // Lighting: two samples of 5 ms -> total 10.
    ScopedProfiler.Record(0, 5.0);
    ScopedProfiler.Record(0, 5.0);
    // Physics: one sample of 12 ms -> total 12.
    ScopedProfiler.Record(1, 12.0);
    // AI: three samples of 3 ms -> total 9.
    ScopedProfiler.Record(2, 3.0);
    ScopedProfiler.Record(2, 3.0);
    ScopedProfiler.Record(2, 3.0);
    const std::string_view ScopedNames[] = {"Lighting", "Physics", "AI"};

    Fby::Core::ProfilerPanel ScopedPanel;
    ScopedPanel.Update(ScopedProfiler, ScopedNames);
    Check(ScopedPanel.Rows().size() == 3, "one row per non-empty category");
    Check(ScopedPanel.Rows()[0].Name == "Physics", "the total sorts Physics first");
    Check(ScopedPanel.Rows()[1].Name == "Lighting", "the total sorts Lighting second");
    Check(ScopedPanel.Rows()[2].Name == "AI", "the total sorts AI third");
    Check(ScopedPanel.Rows()[0].TotalMs == 12.0, "the total is copied exactly");
    Check(ScopedPanel.Rows()[2].Count == 3, "the sample count is copied exactly");
    Check(ScopedPanel.HiddenCount() == 0, "nothing is hidden with an open cap");

    ScopedPanel.SetSortMode(Fby::Core::ProfileSortMode::P95);
    ScopedPanel.Update(ScopedProfiler, ScopedNames);
    Check(ScopedPanel.Rows()[0].Name == "Physics", "p95 sorts Physics first");
    Check(ScopedPanel.Rows()[1].Name == "Lighting", "p95 sorts Lighting second");
}

void TestCap()
{
    Fby::Core::Profiler ScopedProfiler(64);
    ScopedProfiler.Record(0, 1.0);
    ScopedProfiler.Record(1, 2.0);
    ScopedProfiler.Record(2, 3.0);
    const std::string_view ScopedNames[] = {"A", "B", "C"};
    Fby::Core::ProfilerPanel ScopedPanel;
    ScopedPanel.SetMaxRows(2);
    ScopedPanel.Update(ScopedProfiler, ScopedNames);
    Check(ScopedPanel.Rows().size() == 2, "the row cap holds");
    Check(ScopedPanel.HiddenCount() == 1, "the hidden rows are reported");
    Check(ScopedPanel.Rows()[0].Name == "C", "the largest row survives the cap");

    // Empty categories and empty names are skipped.
    ScopedPanel.SetMaxRows(8);
    ScopedPanel.Update(ScopedProfiler, std::span<const std::string_view>(ScopedNames, 2));
    Check(ScopedPanel.Rows().size() == 2, "only the named non-empty rows remain");
}

void TestFormatting()
{
    char ScopedBuffer[32];
    const std::string_view ScopedText = Fby::Core::FormatMilliseconds(1.2345, ScopedBuffer, sizeof(ScopedBuffer));
    Check(ScopedText == "1.234 ms", "the milliseconds format is fixed");
    const std::string_view ScopedSmall = Fby::Core::FormatMilliseconds(0.0004, ScopedBuffer, sizeof(ScopedBuffer));
    Check(ScopedSmall == "0.000 ms", "small values round down");
    const std::string_view ScopedEmpty = Fby::Core::FormatMilliseconds(1.0, nullptr, 0);
    Check(ScopedEmpty.empty(), "a null buffer yields an empty view");
}
}

int main()
{
    TestRowsAndSorting();
    TestCap();
    TestFormatting();
    if (GFailures != 0)
    {
        std::fprintf(stderr, "%d profiler panel test(s) failed\n", GFailures);
        return 1;
    }
    std::printf("profiler_panel: OK\n");
    return 0;
}
