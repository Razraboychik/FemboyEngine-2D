// FemboyEngine2D - profiler tests.
#include "Engine/Core/Profiler.hpp"

#include <cmath>
#include <cstdio>

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

void TestStats()
{
    Fby::Core::Profiler ScopedProfiler(256);
    for (std::uint32_t ScopedValue = 1; ScopedValue <= 100; ++ScopedValue)
    {
        ScopedProfiler.Record(0, static_cast<double>(ScopedValue));
    }
    const Fby::Core::ProfileStats ScopedStats = ScopedProfiler.Stats(0);
    Check(ScopedStats.Count == 100, "all samples are counted");
    Check(ScopedStats.MinMs == 1.0, "the minimum is exact");
    Check(ScopedStats.MaxMs == 100.0, "the maximum is exact");
    Check(ScopedStats.TotalMs == 5050.0, "the total is exact");
    Check(std::abs(ScopedStats.AverageMs - 50.5) < 1.0e-9, "the average is exact");
    Check(ScopedStats.P95Ms == 95.0, "the nearest-rank p95 is exact");
    Check(ScopedProfiler.Stats(3).Count == 0, "an empty category reports zero");
}

void TestRing()
{
    Fby::Core::Profiler ScopedProfiler(4);
    for (std::uint32_t ScopedValue = 1; ScopedValue <= 6; ++ScopedValue)
    {
        ScopedProfiler.Record(0, static_cast<double>(ScopedValue));
    }
    Check(ScopedProfiler.SampleCount() == 4, "the ring keeps its capacity");
    const Fby::Core::ProfileStats ScopedStats = ScopedProfiler.Stats(0);
    Check(ScopedStats.Count == 4, "only the retained samples aggregate");
    Check(ScopedStats.MinMs == 3.0, "the oldest samples are dropped");
    Check(ScopedStats.MaxMs == 6.0, "the newest sample is retained");
}

void TestScopes()
{
    Fby::Core::Profiler ScopedProfiler(64);
    {
        auto ScopedOuter = ScopedProfiler.BeginRound(0);
        {
            auto ScopedInner = ScopedProfiler.BeginRound(1);
            (void)ScopedInner;
        }
    }
    Check(ScopedProfiler.Stats(0).Count == 1, "the outer scope records once");
    Check(ScopedProfiler.Stats(1).Count == 1, "the inner scope records once");
    Check(ScopedProfiler.Stats(1).TotalMs >= 0.0, "the inner duration is sane");

    // Nested scopes of the same category both record.
    {
        auto ScopedOuter = ScopedProfiler.BeginRound(2);
        {
            auto ScopedInner = ScopedProfiler.BeginRound(2);
            (void)ScopedInner;
        }
    }
    Check(ScopedProfiler.Stats(2).Count == 2, "nested scopes of one category both record");
}

void TestDeterminism()
{
    Fby::Core::Profiler ScopedFirst(128);
    Fby::Core::Profiler ScopedSecond(128);
    for (std::uint32_t ScopedValue = 0; ScopedValue < 64; ++ScopedValue)
    {
        const double ScopedSample = static_cast<double>(ScopedValue % 7) * 0.25;
        ScopedFirst.Record(1, ScopedSample);
        ScopedSecond.Record(1, ScopedSample);
    }
    const Fby::Core::ProfileStats ScopedA = ScopedFirst.Stats(1);
    const Fby::Core::ProfileStats ScopedB = ScopedSecond.Stats(1);
    Check(ScopedA.Count == ScopedB.Count && ScopedA.TotalMs == ScopedB.TotalMs &&
              ScopedA.P95Ms == ScopedB.P95Ms,
          "identical samples aggregate identically");
    Check(ScopedFirst.Stats(9).Count == 0, "an out-of-range category is ignored");
}
}

int main()
{
    TestStats();
    TestRing();
    TestScopes();
    TestDeterminism();
    if (GFailures != 0)
    {
        std::fprintf(stderr, "%d profiler test(s) failed\n", GFailures);
        return 1;
    }
    std::printf("profiler: OK\n");
    return 0;
}
