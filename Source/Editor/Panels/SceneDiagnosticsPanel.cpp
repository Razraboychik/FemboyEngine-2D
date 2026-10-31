// FemboyEngine2D - SceneDiagnosticsPanel implementation.
#include "Editor/Panels/SceneDiagnosticsPanel.hpp"

#include "Editor/Infrastructure/EditorTypography.hpp"

#include "Engine/Core/FrameArena.hpp"
#include "Engine/Core/Time.hpp"
#include "Engine/ECS/Components.hpp"
#include "Engine/Physics/Physics.hpp"

#include <imgui.h>
#include <implot.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <span>

namespace Fby::Editor
{
namespace
{
[[nodiscard]] std::uint64_t NowNanoseconds() noexcept
{
    return Fby::Core::Clock::NowNanoseconds();
}

template <std::size_t N> void ShiftAppend(std::array<float, N>& InSeries, float InValue) noexcept
{
    for (std::size_t ScopedIndex = 1; ScopedIndex < N; ++ScopedIndex)
    {
        InSeries[ScopedIndex - 1] = InSeries[ScopedIndex];
    }
    InSeries[N - 1] = InValue;
}

[[nodiscard]] const char* SeverityTag(float InPercent) noexcept
{
    if (InPercent >= 95.0f)
    {
        return "saturated";
    }
    if (InPercent >= 70.0f)
    {
        return "busy";
    }
    if (InPercent >= 25.0f)
    {
        return "warm";
    }
    return "idle";
}
}

float SceneDiagnosticsPanel::ThreadUtilization(std::uint32_t InSlot) const noexcept
{
    return InSlot < MThreadUtilization.size() ? MThreadUtilization[InSlot] : 0.0f;
}

void SceneDiagnosticsPanel::RecordSimulation(double InMicroseconds) noexcept
{
    MHostSimulationMicroseconds = std::max(0.0, InMicroseconds);
}

void SceneDiagnosticsPanel::PushSample(const SceneDiagnosticsSample& InSample) noexcept
{
    MLatest = InSample;
    PushHistory(InSample);
}

void SceneDiagnosticsPanel::Sample(Ecs::SystemContext& InContext)
{
    SceneDiagnosticsSample ScopedSample;
    ScopedSample.frameIndex = InContext.frameIndex;

    const std::uint64_t ScopedNow = NowNanoseconds();
    if (MLastSampleNanoseconds != 0 && ScopedNow > MLastSampleNanoseconds)
    {
        // steady_clock gives nanoseconds; the sample and every series below are
        ScopedSample.frameMicroseconds = (ScopedNow - MLastSampleNanoseconds) / 1000;
    }
    MLastSampleNanoseconds = ScopedNow;

    ScopedSample.hostMeasuredSimulation = MHostSimulationMicroseconds > 0.0;
    if (ScopedSample.hostMeasuredSimulation)
    {
        ScopedSample.simulationMicroseconds = static_cast<std::uint64_t>(MHostSimulationMicroseconds);
    }
    else
    {
        ScopedSample.simulationMicroseconds =
            static_cast<std::uint64_t>(std::max(0.0f, InContext.dt) * 1.0e6f);
    }

    if (InContext.frameArena != nullptr)
    {
        ScopedSample.arenaUsed = static_cast<std::uint32_t>(InContext.frameArena->Used());
        ScopedSample.arenaCapacity = static_cast<std::uint32_t>(InContext.frameArena->Capacity());
    }
    ScopedSample.entities = static_cast<std::uint32_t>(InContext.registry.storage<Entity>().size());
    if (InContext.physics != nullptr)
    {
        ScopedSample.physicsContacts = static_cast<std::uint32_t>(InContext.physics->Contacts().size());
    }

    if (InContext.renderer != nullptr)
    {
        const Rhi::FrameStats ScopedStats = InContext.renderer->Stats();
        ScopedSample.meshItems = ScopedStats.meshItems;
        ScopedSample.meshIndirectDraws = ScopedStats.meshIndirectCalls;
        ScopedSample.skinDispatches = ScopedStats.skinDispatches;
        ScopedSample.drawCalls = ScopedStats.drawCalls;
        ScopedSample.items = ScopedStats.items;
        ScopedSample.batches = ScopedStats.batches;
        ScopedSample.textureBinds = ScopedStats.textureBinds;
        ScopedSample.uploadedBytes = ScopedStats.uploadedBytes;
    }

    SampleThreads(InContext.jobs, ScopedNow);
    ScopedSample.threadSlots = MSlotCount;
    MLatest = ScopedSample;
    PushHistory(ScopedSample);
}

void SceneDiagnosticsPanel::SampleThreads(JobSystem& InJobs, std::uint64_t InNowNanoseconds)
{
    const std::uint32_t ScopedSlots = std::min<std::uint32_t>(InJobs.ThreadSlotCount(), kMaxThreadSlots);
    if (ScopedSlots == 0)
    {
        return;
    }
    const std::uint32_t ScopedPreviousSlots = MSlotCount;
    MSlotCount = ScopedSlots;
    (void)InJobs.SampleThreadLoad(std::span<JobSystem::ThreadLoad>(MThreadNow.data(), ScopedSlots));

    const std::uint64_t ScopedElapsed =
        MLastThreadSampleNanoseconds != 0 && InNowNanoseconds > MLastThreadSampleNanoseconds
            ? InNowNanoseconds - MLastThreadSampleNanoseconds
            : 0;
    MLastThreadSampleNanoseconds = InNowNanoseconds;

    for (std::uint32_t ScopedSlot = 0; ScopedSlot < ScopedSlots; ++ScopedSlot)
    {
        float ScopedPercent = 0.0f;
        const JobSystem::ThreadLoad& ScopedNow = MThreadNow[ScopedSlot];
        if (ScopedElapsed > 0 && ScopedSlot < ScopedPreviousSlots)
        {
            const JobSystem::ThreadLoad& ScopedBefore = MThreadPrevious[ScopedSlot];
            const std::uint64_t ScopedBusy =
                ScopedNow.busyNanoseconds >= ScopedBefore.busyNanoseconds
                    ? ScopedNow.busyNanoseconds - ScopedBefore.busyNanoseconds
                    : 0;
            ScopedPercent = 100.0f * static_cast<float>(ScopedBusy) / static_cast<float>(ScopedElapsed);
        }
        MThreadUtilization[ScopedSlot] = std::clamp(ScopedPercent, 0.0f, 100.0f);

        float* ScopedRow = MThreadHeat.data() + static_cast<std::size_t>(ScopedSlot) * kHistoryFrames;
        std::memmove(ScopedRow, ScopedRow + 1, sizeof(float) * (kHistoryFrames - 1));
        ScopedRow[kHistoryFrames - 1] = MThreadUtilization[ScopedSlot];
    }
    MThreadPrevious = MThreadNow;
}

void SceneDiagnosticsPanel::PushHistory(const SceneDiagnosticsSample& InSample)
{
    ShiftAppend(MSimulationMicroseconds, static_cast<float>(InSample.simulationMicroseconds));
    ShiftAppend(MRenderMicroseconds, static_cast<float>(InSample.renderMicroseconds));
    ShiftAppend(MFrameMicroseconds, static_cast<float>(InSample.frameMicroseconds));
    ShiftAppend(MArenaKilobytes, static_cast<float>(InSample.arenaUsed) / 1024.0f);
    ShiftAppend(MArenaPercent,
                InSample.arenaCapacity > 0
                    ? 100.0f * static_cast<float>(InSample.arenaUsed) / static_cast<float>(InSample.arenaCapacity)
                    : 0.0f);
    ShiftAppend(MMeshItems, static_cast<float>(InSample.meshItems));
    ShiftAppend(MIndirectDraws, static_cast<float>(InSample.meshIndirectDraws));
    ShiftAppend(MDrawCalls, static_cast<float>(InSample.drawCalls));
    ShiftAppend(MUploadedKilobytes, static_cast<float>(InSample.uploadedBytes) / 1024.0f);
    if (MHistoryCount < kHistoryFrames)
    {
        ++MHistoryCount;
    }
}

float SceneDiagnosticsPanel::Average(const std::array<float, kHistoryFrames>& InSeries,
                                     std::uint32_t InCount) const
{
    if (InCount == 0)
    {
        return 0.0f;
    }
    double ScopedSum = 0.0;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < InCount; ++ScopedIndex)
    {
        ScopedSum += static_cast<double>(InSeries[kHistoryFrames - InCount + ScopedIndex]);
    }
    return static_cast<float>(ScopedSum / static_cast<double>(InCount));
}

float SceneDiagnosticsPanel::Maximum(const std::array<float, kHistoryFrames>& InSeries,
                                     std::uint32_t InCount) const
{
    float ScopedMax = 0.0f;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < InCount; ++ScopedIndex)
    {
        ScopedMax = std::max(ScopedMax, InSeries[kHistoryFrames - InCount + ScopedIndex]);
    }
    return ScopedMax;
}

float SceneDiagnosticsPanel::Percentile(const std::array<float, kHistoryFrames>& InSeries, std::uint32_t InCount,
                                        float InPercentile) const
{
    if (InCount == 0)
    {
        return 0.0f;
    }
    std::array<float, kHistoryFrames> ScopedScratch{};
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < InCount; ++ScopedIndex)
    {
        ScopedScratch[ScopedIndex] = InSeries[kHistoryFrames - InCount + ScopedIndex];
    }
    const auto ScopedRank = static_cast<std::size_t>(
        std::clamp(static_cast<float>(InCount - 1) * InPercentile, 0.0f, static_cast<float>(InCount - 1)));
    std::nth_element(ScopedScratch.begin(), ScopedScratch.begin() + static_cast<std::ptrdiff_t>(ScopedRank),
                     ScopedScratch.begin() + static_cast<std::ptrdiff_t>(InCount));
    return ScopedScratch[ScopedRank];
}

void SceneDiagnosticsPanel::DrawSummary()
{
    const float ScopedSimAvg = Average(MSimulationMicroseconds, MHistoryCount);
    const float ScopedSimP99 = Percentile(MSimulationMicroseconds, MHistoryCount, 0.99f);
    const float ScopedSimMax = Maximum(MSimulationMicroseconds, MHistoryCount);
    const float ScopedFrameMs = static_cast<float>(MLatest.frameMicroseconds) / 1000.0f;
    const float ScopedFps = ScopedFrameMs > 0.0f ? 1000.0f / ScopedFrameMs : 0.0f;

    ImGui::Text("Frame #%llu", static_cast<unsigned long long>(MLatest.frameIndex));
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::Text("simulation");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.75f, 1.0f), "%.1f us", static_cast<double>(MLatest.simulationMicroseconds));
    ImGui::SameLine();
    ImGui::TextDisabled("%s", MLatest.hostMeasuredSimulation ? "(host-measured)" : "(context.dt fallback)");

    ImGui::Text("avg %.1f us   p99 %.1f us   peak %.1f us", static_cast<double>(ScopedSimAvg),
                static_cast<double>(ScopedSimP99), static_cast<double>(ScopedSimMax));
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::Text("frame %.2f ms (%.0f fps)", static_cast<double>(ScopedFrameMs), static_cast<double>(ScopedFps));

    if (MLatest.arenaCapacity > 0)
    {
        ImGui::Text("FrameArena %u / %u KiB", static_cast<unsigned>(MLatest.arenaUsed / 1024),
                    static_cast<unsigned>(MLatest.arenaCapacity / 1024));
    }
    else
    {
        ImGui::TextDisabled("FrameArena not bound to SystemContext");
    }
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::Text("entities %u   contacts %u", MLatest.entities, MLatest.physicsContacts);

    ImGui::Text("MeshRenderItem %u   indirect %u   draw calls %u   items %u   batches %u", MLatest.meshItems,
                MLatest.meshIndirectDraws, MLatest.drawCalls, MLatest.items, MLatest.batches);

    std::uint32_t ScopedBusiest = 0;
    float ScopedBusiestValue = 0.0f;
    for (std::uint32_t ScopedSlot = 0; ScopedSlot < MSlotCount; ++ScopedSlot)
    {
        if (MThreadUtilization[ScopedSlot] > ScopedBusiestValue)
        {
            ScopedBusiestValue = MThreadUtilization[ScopedSlot];
            ScopedBusiest = ScopedSlot;
        }
    }
    ImGui::Text("%u thread slot(s)   busiest slot %u at %.0f%% (%s)", MSlotCount, ScopedBusiest,
                static_cast<double>(ScopedBusiestValue), SeverityTag(ScopedBusiestValue));

    if (ImGui::Button(MPaused ? "Resume sampling" : "Pause sampling"))
    {
        MPaused = !MPaused;
    }
    ImGui::SameLine();
    ImGui::Checkbox("Thread bars", &MShowThreadBars);
    ImGui::SameLine();
    ImGui::Checkbox("Occupancy heatmap", &MShowHeatmap);
}

void SceneDiagnosticsPanel::DrawFrameTimePlot()
{
    if (MHistoryCount == 0)
    {
        return;
    }
    const int ScopedCount = static_cast<int>(MHistoryCount);
    const int ScopedOffset = static_cast<int>(kHistoryFrames) - ScopedCount;
    if (ImPlot::BeginPlot("Simulation frame time", ImVec2(-1.0f, 150.0f)))
    {
        ImPlot::SetupAxes("frame", "us", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
        ImPlot::SetupLegend(ImPlotLocation_NorthEast);
        ImPlot::PlotLine("simulation us", MSimulationMicroseconds.data() + ScopedOffset, ScopedCount);
        ImPlot::PlotLine("frame us", MFrameMicroseconds.data() + ScopedOffset, ScopedCount);
        ImPlot::EndPlot();
    }
}

void SceneDiagnosticsPanel::DrawThreadPlot()
{
    if (MSlotCount == 0)
    {
        return;
    }
    if (MShowThreadBars)
    {
        if (ImPlot::BeginPlot("CPU load per thread", ImVec2(-1.0f, 140.0f)))
        {
            ImPlot::SetupAxes("slot", "%", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_None);
            ImPlot::SetupAxisLimits(ImAxis_Y1, 0.0, 100.0, ImPlotCond_Always);
            ImPlot::PlotBars("occupancy", MThreadUtilization.data(), static_cast<int>(MSlotCount), 0.65);
            ImPlot::EndPlot();
        }
    }
    if (MShowHeatmap)
    {
        if (ImPlot::BeginPlot("Thread occupancy history", ImVec2(-1.0f, 150.0f)))
        {
            ImPlot::SetupAxes("frame", "thread", ImPlotAxisFlags_NoTickLabels, ImPlotAxisFlags_NoTickLabels);
            ImPlot::PlotHeatmap("occupancy", MThreadHeat.data(), static_cast<int>(MSlotCount),
                                static_cast<int>(kHistoryFrames), 0.0, 100.0, "%.0f%%", ImPlotPoint(0.0, 0.0),
                                ImPlotPoint(static_cast<double>(kHistoryFrames), static_cast<double>(MSlotCount)));
            ImPlot::EndPlot();
        }
    }
}

void SceneDiagnosticsPanel::DrawMemoryPlot()
{
    if (MHistoryCount == 0)
    {
        return;
    }
    const int ScopedCount = static_cast<int>(MHistoryCount);
    const int ScopedOffset = static_cast<int>(kHistoryFrames) - ScopedCount;
    if (ImPlot::BeginPlot("FrameArena", ImVec2(-1.0f, 130.0f)))
    {
        ImPlot::SetupAxes("frame", "KiB", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
        ImPlot::SetupLegend(ImPlotLocation_NorthEast);
        ImPlot::PlotLine("used KiB", MArenaKilobytes.data() + ScopedOffset, ScopedCount);
        ImPlot::PlotLine("used %", MArenaPercent.data() + ScopedOffset, ScopedCount);
        ImPlot::EndPlot();
    }
}

void SceneDiagnosticsPanel::DrawRenderPlot()
{
    if (MHistoryCount == 0)
    {
        return;
    }
    const int ScopedCount = static_cast<int>(MHistoryCount);
    const int ScopedOffset = static_cast<int>(kHistoryFrames) - ScopedCount;
    if (ImPlot::BeginPlot("Render extract", ImVec2(-1.0f, 130.0f)))
    {
        ImPlot::SetupAxes("frame", "count", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
        ImPlot::SetupLegend(ImPlotLocation_NorthEast);
        ImPlot::PlotLine("MeshRenderItem", MMeshItems.data() + ScopedOffset, ScopedCount);
        ImPlot::PlotLine("indirect draws", MIndirectDraws.data() + ScopedOffset, ScopedCount);
        ImPlot::PlotLine("draw calls", MDrawCalls.data() + ScopedOffset, ScopedCount);
        ImPlot::EndPlot();
    }
}

void SceneDiagnosticsPanel::DrawReleaseProgress()
{
    ImGui::SeparatorText("Build & Pack Game");
    const ReleaseCookProgress& ScopedProgress = MReleaseProgress;
    if (ScopedProgress.stage == ReleaseCookStage::Idle && !ScopedProgress.running && !ScopedProgress.failed)
    {
        ImGui::TextDisabled("idle - use Build > Build & Pack Game");
        return;
    }

    char ScopedOverlay[256]{};
    const std::uint32_t ScopedCompleted = ScopedProgress.completed;
    const std::uint32_t ScopedTotal = ScopedProgress.total;
    std::snprintf(ScopedOverlay, sizeof(ScopedOverlay), "%s  %u/%u", ReleaseCookStageName(ScopedProgress.stage),
                  ScopedCompleted, ScopedTotal);
    ImGui::ProgressBar(ScopedProgress.Fraction(), ImVec2(-1.0f, 0.0f), ScopedOverlay);
    if (ScopedProgress.asset[0] != '\0')
    {
        ImGui::Text("asset: %s", ScopedProgress.asset);
    }
    const double ScopedMegabytes = static_cast<double>(ScopedProgress.bytesPerSecond) / (1024.0 * 1024.0);
    ImGui::Text("LZMA %6.2f MB/s   raw %.1f MiB   stored %.1f MiB", ScopedMegabytes,
                static_cast<double>(ScopedProgress.rawBytes) / (1024.0 * 1024.0),
                static_cast<double>(ScopedProgress.storedBytes) / (1024.0 * 1024.0));
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    if (ScopedProgress.encrypted)
    {
        ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.75f, 1.0f), "encrypted: %s",
                           Fby::Assets::CookCipherName(ScopedProgress.cipher));
    }
    else
    {
        ImGui::TextDisabled("encryption: off");
    }
    if (ScopedProgress.message[0] != '\0')
    {
        const ImVec4 ScopedColor = ScopedProgress.failed ? ImVec4(0.95f, 0.40f, 0.40f, 1.0f)
                                   : ScopedProgress.stage == ReleaseCookStage::Succeeded
                                       ? ImVec4(0.45f, 0.85f, 0.55f, 1.0f)
                                       : ImVec4(0.70f, 0.75f, 0.85f, 1.0f);
        ImGui::TextColored(ScopedColor, "%s", ScopedProgress.message);
    }
}

void SceneDiagnosticsPanel::Draw(Ecs::SystemContext& InContext, bool* InOpen)
{
    if (InOpen != nullptr && !*InOpen)
    {
        MVisible = false;
        return;
    }
    if (!BeginEditorPanel("Scene Diagnostics", InOpen))
    {
        EndEditorPanel();
        return;
    }
    MVisible = true;
    if (!MPaused)
    {
        Sample(InContext);
    }

    DrawSummary();
    ImGui::Separator();
    DrawReleaseProgress();
    DrawFrameTimePlot();
    DrawThreadPlot();
    DrawMemoryPlot();
    DrawRenderPlot();
    EndEditorPanel();
}
}
