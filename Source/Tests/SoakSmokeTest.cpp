// FemboyEngine2D - operation-budgeted simulated-clock soak.
#include "Engine/Core/Time.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdint>

int main()
{
    constexpr std::uint64_t kFrames = 200000;
    constexpr double kFrameSeconds = 1.0 / 60.0;
    Fby::Core::Clock ScopedClock(0);
    Fby::Core::TimerPool<256> ScopedTimers;
    std::array<Fby::Core::TimerFired, 16> ScopedFired{};
    std::uint64_t ScopedFires = 0;
    Fby::Core::TimerHandle ScopedRepeating{};

    for (std::uint64_t ScopedFrame = 1; ScopedFrame <= kFrames; ++ScopedFrame)
    {
        ScopedClock.Tick(static_cast<std::int64_t>(ScopedFrame * kFrameSeconds * Fby::Core::kNanosecondsPerSecond));
        if (ScopedFrame % 1000u == 0u)
        {
            if (ScopedTimers.IsActive(ScopedRepeating))
            {
                ScopedTimers.Cancel(ScopedRepeating);
            }
            ScopedRepeating = ScopedTimers.Create(0.05, true);
        }
        const std::uint32_t ScopedWritten = ScopedTimers.Advance(ScopedClock.Delta(), ScopedFired);
        for (std::uint32_t ScopedI = 0; ScopedI < ScopedWritten; ++ScopedI)
        {
            ScopedFires += ScopedFired[ScopedI].FireCount;
        }
    }

    if (ScopedClock.Frame() != kFrames)
    {
        std::fprintf(stderr, "soak_smoke: frame count diverged\n");
        return 1;
    }
    if (std::fabs(ScopedClock.Elapsed() - static_cast<double>(kFrames) * kFrameSeconds) > 0.01)
    {
        std::fprintf(stderr, "soak_smoke: simulated time diverged\n");
        return 1;
    }
    if (ScopedFires == 0u)
    {
        std::fprintf(stderr, "soak_smoke: repeating timer never fired\n");
        return 1;
    }
    if (ScopedTimers.ActiveCount() > ScopedTimers.PoolCapacity())
    {
        std::fprintf(stderr, "soak_smoke: timer pool exceeded its capacity\n");
        return 1;
    }
    std::printf("soak_smoke: frames=%llu fires=%llu active=%u\n", static_cast<unsigned long long>(kFrames),
                static_cast<unsigned long long>(ScopedFires), ScopedTimers.ActiveCount());
    return 0;
}
