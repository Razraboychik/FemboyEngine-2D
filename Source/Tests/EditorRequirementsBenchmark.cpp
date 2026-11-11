// FemboyEngine2D - editor backbone requirements probe.
//
// This is the headless part of the editor delta: component registration,
// inspector-style traversal and a bounded undo transaction stack at the two
// reference scene sizes. ImGui draw cost is measured by the editor itself, not
// faked here.
#include "Editor/Infrastructure/CommandStack.hpp"
#include "Editor/Infrastructure/PlayInEditor.hpp"
#include "Engine/Core/JobSystem.hpp"
#include "Engine/ECS/ComponentRegistry.hpp"
#include "Engine/ECS/Components.hpp"
#include "Engine/ECS/Snapshot.hpp"

#include <chrono>
#include <span>
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

struct ProbeResult
{
    std::uint32_t Entities = 0;
    double InspectorMilliseconds = 0.0;
    double TransactionMicroseconds = 0.0;
    double PieStartMilliseconds = 0.0;
    std::size_t PeakResidentBytes = 0;
};

ProbeResult RunScene(std::uint32_t InCount)
{
    ProbeResult ScopedResult;
    ScopedResult.Entities = InCount;
    Fby::Scene::Registry ScopedRegistry;
    Fby::Editor::CommandStack ScopedStack;
    (void)ScopedStack.Initialize();
    ScopedStack.Bind(&ScopedRegistry, nullptr);
    const std::uint16_t ScopedTransformCodec =
        ScopedStack.RegisterComponent<Fby::Transform2D>("Transform2D");

    std::vector<Fby::Entity> ScopedEntities;
    ScopedEntities.reserve(InCount);
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < InCount; ++ScopedIndex)
    {
        const Fby::Entity ScopedEntity = ScopedRegistry.create();
        ScopedEntities.push_back(ScopedEntity);
        (void)ScopedRegistry.emplace<Fby::Transform2D>(
            ScopedEntity, Fby::Transform2D{Fby::Vector2D{static_cast<float>(ScopedIndex % 1024u), 0.0f},
                                           Fby::Vector2D{1.0f, 1.0f}, 0.0f});
        (void)ScopedRegistry.emplace<Fby::Sprite2D>(ScopedEntity);
        (void)ScopedRegistry.emplace<Fby::DrawOrder>(ScopedEntity);
    }

    // Inspector-style read pass: the editor walks every visible entity each
    // frame to build the outliner and the property rows.
    constexpr std::uint32_t ScopedFrames = 120;
    const auto ScopedStart = std::chrono::steady_clock::now();
    float ScopedSum = 0.0f;
    for (std::uint32_t ScopedFrame = 0; ScopedFrame < ScopedFrames; ++ScopedFrame)
    {
        auto ScopedView = ScopedRegistry.view<Fby::Transform2D, Fby::Sprite2D, Fby::DrawOrder>();
        for (const Fby::Entity ScopedEntity : ScopedView)
        {
            ScopedSum += ScopedView.get<Fby::Transform2D>(ScopedEntity).Position.X;
        }
    }
    const auto ScopedEnd = std::chrono::steady_clock::now();
    ScopedResult.InspectorMilliseconds =
        std::chrono::duration<double, std::milli>(ScopedEnd - ScopedStart).count() / ScopedFrames;

    // One bounded inspector transaction on a representative entity. The stack
    // stores sparse deltas; this number is the per-edit cost, not a frame cost.
    constexpr std::uint32_t ScopedEdits = 1000;
    const Fby::Entity ScopedTarget = ScopedEntities.front();
    const auto ScopedEditStart = std::chrono::steady_clock::now();
    for (std::uint32_t ScopedEdit = 0; ScopedEdit < ScopedEdits; ++ScopedEdit)
    {
        Fby::Transform2D ScopedBefore{};
        (void)ScopedStack.CaptureComponent(
            ScopedTarget, ScopedTransformCodec,
            std::span<std::byte>(reinterpret_cast<std::byte*>(&ScopedBefore), sizeof(ScopedBefore)));
        ScopedRegistry.get<Fby::Transform2D>(ScopedTarget).Position.X += 1.0f;
        (void)ScopedStack.BeginTransaction("Inspector");
        (void)ScopedStack.RecordAfterMutation(
            ScopedTarget, ScopedTransformCodec,
            std::span<const std::byte>(reinterpret_cast<const std::byte*>(&ScopedBefore), sizeof(ScopedBefore)));
        (void)ScopedStack.CommitTransaction();
    }
    const auto ScopedEditEnd = std::chrono::steady_clock::now();
    ScopedResult.TransactionMicroseconds =
        std::chrono::duration<double, std::micro>(ScopedEditEnd - ScopedEditStart).count() / ScopedEdits;
    // PIE clones the editor world through the v5 snapshot. This measures the
    // editor-side capture/restore path, not a game frame.
    Fby::Ecs::SnapshotSchema ScopedSchema = Fby::Ecs::SnapshotSchema::BuiltIns();
    const std::size_t ScopedSnapshotBytes = Fby::Ecs::SnapshotSize(ScopedRegistry, ScopedSchema);
    std::vector<std::byte> ScopedPieArena(ScopedSnapshotBytes + (4u << 20));
    Fby::Editor::PlayInEditor ScopedPie;
    if (ScopedPie.Initialize(ScopedPieArena, ScopedSchema, ScopedPieArena.size()))
    {
        Fby::JobSystem ScopedJobs(1);
        Fby::Ecs::SystemContext ScopedServices{ScopedRegistry, ScopedJobs};
        const auto ScopedPieStart = std::chrono::steady_clock::now();
        (void)ScopedPie.Start(ScopedRegistry, ScopedSchema, ScopedServices);
        const auto ScopedPieEnd = std::chrono::steady_clock::now();
        ScopedResult.PieStartMilliseconds =
            std::chrono::duration<double, std::milli>(ScopedPieEnd - ScopedPieStart).count();
        ScopedPie.Stop();
    }
    ScopedResult.PeakResidentBytes = PeakResidentBytes();
    (void)ScopedSum;
    return ScopedResult;
}

void Print(const ProbeResult& InResult)
{
    std::printf("editor_requirements: entities=%u inspector=%.3f ms/frame transaction=%.2f us/edits pie=%.2f ms peak=%.1f MiB\n",
                InResult.Entities, InResult.InspectorMilliseconds, InResult.TransactionMicroseconds,
                InResult.PieStartMilliseconds, static_cast<double>(InResult.PeakResidentBytes) / (1024.0 * 1024.0));
}

}

int main()
{
    std::printf("editor_requirements: synthetic editor backbone (no ImGui draw cost)\n");
    Print(RunScene(10000));
    Print(RunScene(100000));
    return 0;
}
