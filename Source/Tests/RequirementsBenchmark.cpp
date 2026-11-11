// FemboyEngine2D - editor-side requirements probe.
//
// The probe measures the engine frame cost and peak resident memory for two
// reference scenes. It is intentionally headless: the editor adds ImGui, asset
// caches and the project browser on top of these numbers, so the table in the
// documentation is derived from this baseline plus the measured editor delta.

#include "Engine/ECS/Components.hpp"
#include "Engine/RHI/FrameComposer.hpp"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <vector>

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

struct SceneStats
{
    std::uint32_t Sprites = 0;
    double AverageFrameMilliseconds = 0.0;
    std::size_t PeakResidentBytes = 0;
    std::size_t EstimatedInstanceBytes = 0;
};

[[nodiscard]] std::size_t PeakResidentBytes() noexcept
{
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS_EX ScopedCounters{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&ScopedCounters),
                             sizeof(ScopedCounters)) == 0)
    {
        return 0;
    }
    return static_cast<std::size_t>(ScopedCounters.PeakWorkingSetSize);
#elif defined(__linux__)
    std::ifstream ScopedStatus("/proc/self/status");
    std::string ScopedLine;
    while (std::getline(ScopedStatus, ScopedLine))
    {
        if (ScopedLine.rfind("VmHWM:", 0) != 0)
        {
            continue;
        }
        std::size_t ScopedValue = 0;
        if (std::sscanf(ScopedLine.c_str(), "VmHWM: %zu", &ScopedValue) == 1)
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

void SpawnSprites(Fby::Ecs::Registry& InRegistry, std::uint32_t InCount)
{
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < InCount; ++ScopedIndex)
    {
        const Fby::Entity ScopedEntity = InRegistry.create();
        const float ScopedX = static_cast<float>(ScopedIndex % 4096u);
        const float ScopedY = static_cast<float>((ScopedIndex / 4096u) % 4096u);
        InRegistry.emplace<Fby::Transform2D>(
            ScopedEntity, Fby::Transform2D{Fby::Vector2D{ScopedX, ScopedY}, Fby::Vector2D{1.0f, 1.0f}, 0.0f});
        InRegistry.emplace<Fby::Sprite2D>(ScopedEntity);
        InRegistry.emplace<Fby::DrawOrder>(
            ScopedEntity, Fby::DrawOrder{static_cast<std::uint16_t>((ScopedIndex / 64u) % 4u), 0.0f});
    }
}

[[nodiscard]] SceneStats RunScene(std::uint32_t InSprites, std::uint32_t InFrames)
{
    Fby::Ecs::Registry ScopedRegistry;
    SpawnSprites(ScopedRegistry, InSprites);

    Fby::Rhi::FrameComposer ScopedComposer;
    ScopedComposer.Reserve(InSprites + 64u);

    Fby::Rhi::CameraData ScopedCamera{};
    ScopedCamera.center = Fby::Vector2D{2048.0f, 2048.0f};
    ScopedCamera.viewport = Fby::Vector2D{1920.0f, 1080.0f};
    ScopedCamera.zoom = 1.0f;

    const auto ScopedStart = std::chrono::steady_clock::now();
    for (std::uint32_t ScopedFrame = 0; ScopedFrame < InFrames; ++ScopedFrame)
    {
        ScopedComposer.BuildFramePacket(ScopedRegistry, ScopedCamera, Fby::Color32::Black(), 0u);
    }
    const auto ScopedEnd = std::chrono::steady_clock::now();

    SceneStats ScopedStats;
    ScopedStats.Sprites = InSprites;
    ScopedStats.AverageFrameMilliseconds =
        std::chrono::duration<double, std::milli>(ScopedEnd - ScopedStart).count() / static_cast<double>(InFrames);
    ScopedStats.PeakResidentBytes = PeakResidentBytes();
    // Three frames in flight, one 48-byte item each.
    ScopedStats.EstimatedInstanceBytes = static_cast<std::size_t>(InSprites) * 48u * 3u;
    return ScopedStats;
}

void PrintStats(const char* InName, const SceneStats& InStats)
{
    std::printf("%s: sprites=%u avg_frame_ms=%.3f peak_rss_mib=%.1f instance_mib=%.1f\n", InName, InStats.Sprites,
                InStats.AverageFrameMilliseconds,
                static_cast<double>(InStats.PeakResidentBytes) / (1024.0 * 1024.0),
                static_cast<double>(InStats.EstimatedInstanceBytes) / (1024.0 * 1024.0));
}

}

int main()
{
    constexpr std::uint32_t kSmallSprites = 10000;
    constexpr std::uint32_t kMediumSprites = 100000;
    constexpr std::uint32_t kFrames = 120;

    const SceneStats ScopedSmall = RunScene(kSmallSprites, kFrames);
    const SceneStats ScopedMedium = RunScene(kMediumSprites, kFrames);
    PrintStats("requirements_small", ScopedSmall);
    PrintStats("requirements_medium", ScopedMedium);

    // The editor target is the engine baseline plus the measured editor delta.
    // The numbers are reported, not enforced: a CI machine is not a target
    // laptop and must not turn the probe into a hardware lottery.
    return 0;
}
