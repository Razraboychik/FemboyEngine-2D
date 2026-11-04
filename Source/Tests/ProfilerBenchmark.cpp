// FemboyEngine2D - profiler overhead benchmark.
#include "Engine/Core/Profiler.hpp"
#include "Engine/Core/ProfilerPanel.hpp"
#include "Engine/Core/ProfilerTrace.hpp"

#include <chrono>
#include <cstdio>
#include <string_view>

int main()
{
    constexpr std::uint32_t kRecords = 1000000;
    Fby::Core::Profiler ScopedProfiler(4096);
    const auto ScopedStart = std::chrono::steady_clock::now();
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < kRecords; ++ScopedIndex)
    {
        ScopedProfiler.Record(ScopedIndex % 4u, 0.01);
    }
    const auto ScopedEnd = std::chrono::steady_clock::now();
    const double ScopedMilliseconds = std::chrono::duration<double, std::milli>(ScopedEnd - ScopedStart).count();
    const double ScopedNanoseconds = ScopedMilliseconds * 1000000.0 / static_cast<double>(kRecords);
    std::printf("profiler_benchmark: %u records in %.3f ms (%.2f ns per record)\n", kRecords, ScopedMilliseconds,
                ScopedNanoseconds);

    // RAII scopes are the real API; measure a run of them too.
    constexpr std::uint32_t kScopes = 200000;
    const auto ScopedScopeStart = std::chrono::steady_clock::now();
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < kScopes; ++ScopedIndex)
    {
        auto ScopedScope = ScopedProfiler.BeginRound(ScopedIndex % 4u);
        (void)ScopedScope;
    }
    const auto ScopedScopeEnd = std::chrono::steady_clock::now();
    const double ScopedScopeMs =
        std::chrono::duration<double, std::milli>(ScopedScopeEnd - ScopedScopeStart).count();
    std::printf("profiler_benchmark: %u scopes in %.3f ms\n", kScopes, ScopedScopeMs);

    // The trace export runs on demand; keep it cheap enough for a hotkey.
    const std::string_view ScopedNames[] = {"Record", "Scope", "Sim", "Render"};
    const auto ScopedExportStart = std::chrono::steady_clock::now();
    const std::string ScopedJson = Fby::Core::ToChromeTraceJson(ScopedProfiler, ScopedNames);
    const auto ScopedExportEnd = std::chrono::steady_clock::now();
    const double ScopedExportMs =
        std::chrono::duration<double, std::milli>(ScopedExportEnd - ScopedExportStart).count();
    std::printf("profiler_benchmark: trace export %zu bytes in %.3f ms\n", ScopedJson.size(), ScopedExportMs);

    // The panel rebuilds its rows each frame; keep it cheap.
    Fby::Core::ProfilerPanel ScopedPanel;
    const auto ScopedPanelStart = std::chrono::steady_clock::now();
    ScopedPanel.Update(ScopedProfiler, ScopedNames);
    const auto ScopedPanelEnd = std::chrono::steady_clock::now();
    const double ScopedPanelMs = std::chrono::duration<double, std::milli>(ScopedPanelEnd - ScopedPanelStart).count();
    std::printf("profiler_benchmark: panel update %zu rows in %.3f ms\n", ScopedPanel.Rows().size(), ScopedPanelMs);

#if defined(NDEBUG)
    constexpr double kPanelBudgetMilliseconds = 10.0;
    if (ScopedPanelMs > kPanelBudgetMilliseconds)
    {
        std::fprintf(stderr, "profiler_benchmark: panel over the %.1f ms budget\n", kPanelBudgetMilliseconds);
        return 1;
    }
#endif

#if defined(NDEBUG)
    constexpr double kExportBudgetMilliseconds = 20.0;
    if (ScopedExportMs > kExportBudgetMilliseconds)
    {
        std::fprintf(stderr, "profiler_benchmark: export over the %.1f ms budget\n", kExportBudgetMilliseconds);
        return 1;
    }
#endif
#if defined(NDEBUG)
    constexpr double kRecordBudgetNanoseconds = 500.0;
    if (ScopedNanoseconds > kRecordBudgetNanoseconds)
    {
        std::fprintf(stderr, "profiler_benchmark: record over the %.1f ns budget\n", kRecordBudgetNanoseconds);
        return 1;
    }
#endif
    std::printf("profiler_benchmark: OK\n");
    return 0;
}
