// FemboyEngine2D - replay record/save/load benchmark.
#include "Engine/Core/Replay.hpp"

#include <chrono>
#include <cstdio>
#include <vector>

int main()
{
    constexpr std::uint32_t kFrames = 100000;
    Fby::Core::ReplaySession ScopedSession;
    ScopedSession.BeginRecord(60);
    std::vector<std::byte> ScopedPayload(16, std::byte{0x2A});
    std::uint64_t ScopedState = 1469598103934665603ull;
    const auto ScopedRecordStart = std::chrono::steady_clock::now();
    for (std::uint32_t ScopedTick = 0; ScopedTick < kFrames; ++ScopedTick)
    {
        ScopedState ^= ScopedTick;
        ScopedState *= 1099511628211ull;
        if (!ScopedSession.RecordFrame(ScopedTick, ScopedPayload, ScopedState))
        {
            std::fprintf(stderr, "replay_benchmark: record failed\n");
            return 1;
        }
    }
    ScopedSession.EndRecord();
    const auto ScopedRecordEnd = std::chrono::steady_clock::now();
    const double ScopedRecordMs =
        std::chrono::duration<double, std::milli>(ScopedRecordEnd - ScopedRecordStart).count();

    std::vector<std::byte> ScopedBytes;
    const auto ScopedSaveStart = std::chrono::steady_clock::now();
    if (!ScopedSession.Save(&ScopedBytes))
    {
        std::fprintf(stderr, "replay_benchmark: save failed\n");
        return 1;
    }
    const auto ScopedSaveEnd = std::chrono::steady_clock::now();
    Fby::Core::ReplaySession ScopedLoaded;
    const auto ScopedLoadStart = std::chrono::steady_clock::now();
    if (!ScopedLoaded.Load(ScopedBytes))
    {
        std::fprintf(stderr, "replay_benchmark: load failed\n");
        return 1;
    }
    const auto ScopedLoadEnd = std::chrono::steady_clock::now();
    if (ScopedLoaded.FrameCount() != kFrames)
    {
        std::fprintf(stderr, "replay_benchmark: frame count mismatch\n");
        return 1;
    }
    std::printf("replay_benchmark: %u frames, record %.3f ms, save %.3f ms, load %.3f ms, %zu bytes\n", kFrames,
                ScopedRecordMs,
                std::chrono::duration<double, std::milli>(ScopedSaveEnd - ScopedSaveStart).count(),
                std::chrono::duration<double, std::milli>(ScopedLoadEnd - ScopedLoadStart).count(),
                ScopedBytes.size());

#if defined(NDEBUG)
    constexpr double kSaveBudgetMilliseconds = 60.0;
    const double ScopedSaveMs =
        std::chrono::duration<double, std::milli>(ScopedSaveEnd - ScopedSaveStart).count();
    const double ScopedLoadMs =
        std::chrono::duration<double, std::milli>(ScopedLoadEnd - ScopedLoadStart).count();
    if (ScopedSaveMs > kSaveBudgetMilliseconds || ScopedLoadMs > kSaveBudgetMilliseconds)
    {
        std::fprintf(stderr, "replay_benchmark: save or load over the %.1f ms budget\n", kSaveBudgetMilliseconds);
        return 1;
    }
#endif
    std::printf("replay_benchmark: OK\n");
    return 0;
}
