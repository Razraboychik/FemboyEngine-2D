// FemboyEngine2D - wall-clock soak for timers and process memory.
#include "Engine/Core/Time.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <thread>

#if defined(_WIN32)
#include <windows.h>
#include <psapi.h>
#elif defined(__linux__)
#include <fstream>
#include <string>
#elif defined(__APPLE__)
#include <sys/resource.h>
#endif

namespace
{
std::size_t ResidentBytes()
{
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS_EX ScopedCounters{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&ScopedCounters),
                             sizeof(ScopedCounters)) == 0)
    {
        return 0;
    }
    return static_cast<std::size_t>(ScopedCounters.WorkingSetSize);
#elif defined(__linux__)
    std::ifstream ScopedStatus("/proc/self/status");
    std::string ScopedLine;
    while (std::getline(ScopedStatus, ScopedLine))
    {
        if (ScopedLine.rfind("VmRSS:", 0) != 0)
        {
            continue;
        }
        std::size_t ScopedValue = 0;
        if (std::sscanf(ScopedLine.c_str(), "VmRSS: %zu", &ScopedValue) == 1)
        {
            return ScopedValue * 1024u;
        }
    }
    return 0;
#elif defined(__APPLE__)
    rusage ScopedUsage{};
    if (getrusage(RUSAGE_SELF, &ScopedUsage) != 0)
    {
        return 0;
    }
    return static_cast<std::size_t>(ScopedUsage.ru_maxrss);
#else
    return 0;
#endif
}
}

int main()
{
    double ScopedSeconds = 3.0;
    if (const char* ScopedEnv = std::getenv("FBY_SOAK_SECONDS"); ScopedEnv != nullptr)
    {
        ScopedSeconds = std::strtod(ScopedEnv, nullptr);
    }
    if (ScopedSeconds < 1.0)
    {
        ScopedSeconds = 1.0;
    }

    Fby::Core::Clock ScopedClock(0);
    Fby::Core::TimerPool<64> ScopedTimers;
    std::array<Fby::Core::TimerFired, 8> ScopedFired{};
    std::size_t ScopedInitialBytes = ResidentBytes();
    std::size_t ScopedPeakBytes = ScopedInitialBytes;
    std::uint64_t ScopedFrames = 0;
    std::uint64_t ScopedFires = 0;
    Fby::Core::TimerHandle ScopedTimer = ScopedTimers.Create(0.01, true);
    const auto ScopedStart = std::chrono::steady_clock::now();
    while (std::chrono::duration<double>(std::chrono::steady_clock::now() - ScopedStart).count() < ScopedSeconds)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        ScopedClock.Tick();
        const std::uint32_t ScopedWritten = ScopedTimers.Advance(ScopedClock.Delta(), ScopedFired);
        for (std::uint32_t ScopedI = 0; ScopedI < ScopedWritten; ++ScopedI)
        {
            ScopedFires += ScopedFired[ScopedI].FireCount;
        }
        if ((ScopedFrames % 100u) == 0u)
        {
            ScopedPeakBytes = std::max(ScopedPeakBytes, ResidentBytes());
        }
        if ((ScopedFrames % 500u) == 0u)
        {
            if (ScopedTimers.IsActive(ScopedTimer))
            {
                (void)ScopedTimers.Cancel(ScopedTimer);
            }
            ScopedTimer = ScopedTimers.Create(0.01, true);
        }
        ++ScopedFrames;
    }

    if (ScopedFrames == 0u || ScopedFires == 0u)
    {
        std::fprintf(stderr, "wall_clock_soak: no frames or timer fires\n");
        return 1;
    }
    if (ScopedInitialBytes != 0u && ScopedPeakBytes > ScopedInitialBytes * 2u + (64u * 1024u * 1024u))
    {
        std::fprintf(stderr, "wall_clock_soak: resident memory grew from %zu to %zu\n", ScopedInitialBytes,
                     ScopedPeakBytes);
        return 1;
    }
    std::printf("wall_clock_soak: seconds=%.1f frames=%llu fires=%llu rss=%zu peak=%zu\n", ScopedSeconds,
                static_cast<unsigned long long>(ScopedFrames), static_cast<unsigned long long>(ScopedFires),
                ScopedInitialBytes, ScopedPeakBytes);
    return 0;
}
