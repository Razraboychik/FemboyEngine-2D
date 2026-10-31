// FemboyEngine2D - SceneDiagnosticsPanel.
#pragma once

#include "Engine/Core/JobSystem.hpp"
#include "Engine/ECS/Schedule.hpp"
#include "Engine/RHI/RHI.hpp"
#include "Editor/Infrastructure/ReleaseCooker.hpp"

#include <array>
#include <cstdint>

namespace Fby::Editor
{

struct SceneDiagnosticsSample
{
    std::uint64_t frameIndex = 0;
    std::uint64_t simulationMicroseconds = 0;
    std::uint64_t renderMicroseconds = 0;
    std::uint64_t frameMicroseconds = 0;
    std::uint64_t uploadedBytes = 0;
    std::uint32_t arenaUsed = 0;
    std::uint32_t arenaCapacity = 0;
    std::uint32_t entities = 0;
    std::uint32_t physicsContacts = 0;
    std::uint32_t meshItems = 0;
    std::uint32_t meshIndirectDraws = 0;
    std::uint32_t skinDispatches = 0;
    std::uint32_t drawCalls = 0;
    std::uint32_t items = 0;
    std::uint32_t batches = 0;
    std::uint32_t textureBinds = 0;
    std::uint32_t threadSlots = 0;
    bool hostMeasuredSimulation = false;
};

class SceneDiagnosticsPanel
{
public:
    static constexpr std::uint32_t kHistoryFrames = 240;
    static constexpr std::uint32_t kMaxThreadSlots = 65;

    void Draw(Ecs::SystemContext& InContext, bool* InOpen);

    // ReleaseCooker publishes this POD once per editor frame. The panel never
    void SetReleaseProgress(const ReleaseCookProgress& InProgress) noexcept
    {
        MReleaseProgress = InProgress;
    }

    // Optional: a host that wraps its scheduler can report the exact wall time
    void RecordSimulation(double InMicroseconds) noexcept;
    // Optional: bypass sampling and push a fully prepared sample (tests, replay
    void PushSample(const SceneDiagnosticsSample& InSample) noexcept;

    [[nodiscard]] const SceneDiagnosticsSample& Latest() const noexcept
    {
        return MLatest;
    }
    [[nodiscard]] bool Visible() const noexcept
    {
        return MVisible;
    }
    [[nodiscard]] float ThreadUtilization(std::uint32_t InSlot) const noexcept;
    [[nodiscard]] std::uint32_t ThreadSlotCount() const noexcept
    {
        return MSlotCount;
    }

private:
    void Sample(Ecs::SystemContext& InContext);
    void SampleThreads(JobSystem& InJobs, std::uint64_t InNowNanoseconds);
    void PushHistory(const SceneDiagnosticsSample& InSample);
    void DrawSummary();
    void DrawFrameTimePlot();
    void DrawThreadPlot();
    void DrawMemoryPlot();
    void DrawRenderPlot();
    void DrawReleaseProgress();
    [[nodiscard]] float Average(const std::array<float, kHistoryFrames>& InSeries, std::uint32_t InCount) const;
    [[nodiscard]] float Percentile(const std::array<float, kHistoryFrames>& InSeries, std::uint32_t InCount,
                                   float InPercentile) const;
    [[nodiscard]] float Maximum(const std::array<float, kHistoryFrames>& InSeries, std::uint32_t InCount) const;

    SceneDiagnosticsSample MLatest{};
    std::array<float, kHistoryFrames> MSimulationMicroseconds{};
    std::array<float, kHistoryFrames> MRenderMicroseconds{};
    std::array<float, kHistoryFrames> MFrameMicroseconds{};
    std::array<float, kHistoryFrames> MArenaKilobytes{};
    std::array<float, kHistoryFrames> MArenaPercent{};
    std::array<float, kHistoryFrames> MMeshItems{};
    std::array<float, kHistoryFrames> MIndirectDraws{};
    std::array<float, kHistoryFrames> MDrawCalls{};
    std::array<float, kHistoryFrames> MUploadedKilobytes{};

    // Row-major: [slot * kHistoryFrames + column], oldest column first.
    std::array<float, kMaxThreadSlots * kHistoryFrames> MThreadHeat{};
    std::array<float, kMaxThreadSlots> MThreadUtilization{};
    std::array<JobSystem::ThreadLoad, kMaxThreadSlots> MThreadNow{};
    std::array<JobSystem::ThreadLoad, kMaxThreadSlots> MThreadPrevious{};
    ReleaseCookProgress MReleaseProgress{};

    std::uint32_t MHistoryCount = 0;
    std::uint32_t MSlotCount = 0;
    std::uint64_t MLastSampleNanoseconds = 0;
    std::uint64_t MLastThreadSampleNanoseconds = 0;
    double MHostSimulationMicroseconds = 0.0;
    bool MVisible = false;
    bool MPaused = false;
    bool MShowHeatmap = true;
    bool MShowThreadBars = true;
};
}
