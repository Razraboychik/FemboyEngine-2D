// FemboyEngine2D - SystemGraph serial/parallel scheduling benchmark.
#include "Engine/ECS/Components.hpp"
#include "Engine/ECS/SystemGraph.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>

namespace
{
constexpr std::uint32_t kSystemCount = 8;
constexpr std::uint64_t kIterations = 2'000'000;
constexpr std::uint32_t kRunCount = 5;

std::array<std::atomic<std::uint64_t>, kSystemCount> GResults{};

template <std::uint32_t Index> void RunWork(Fby::Ecs::SystemContext&, void*)
{
    std::uint64_t ScopedSum = Index;
    for (std::uint64_t ScopedI = 0; ScopedI < kIterations; ++ScopedI)
    {
        ScopedSum += (ScopedI ^ Index) * (ScopedI + 1u);
    }
    GResults[Index].store(ScopedSum, std::memory_order_relaxed);
}

using RunFunction = void (*)(Fby::Ecs::SystemContext&, void*);

constexpr std::array<RunFunction, kSystemCount> kRunFunctions{&RunWork<0>, &RunWork<1>, &RunWork<2>, &RunWork<3>,
                                                              &RunWork<4>, &RunWork<5>, &RunWork<6>, &RunWork<7>};

constexpr std::array<const char*, kSystemCount> kNames{"work_0", "work_1", "work_2", "work_3",
                                                       "work_4", "work_5", "work_6", "work_7"};

[[nodiscard]] std::uint64_t Measure(bool InParallel, Fby::Ecs::Registry& InRegistry, Fby::JobSystem& InJobs)
{
    Fby::Ecs::SystemContext ScopedContext{InRegistry, InJobs};
    Fby::Ecs::SystemGraph ScopedGraph;
    for (std::uint32_t ScopedI = 0; ScopedI < kSystemCount; ++ScopedI)
    {
        Fby::Ecs::SystemAccess ScopedAccess;
        ScopedAccess.ParallelSafe = InParallel;
        ScopedAccess.NoEcsAccess = true;
        (void)ScopedGraph.Add(Fby::Ecs::Phase::PreUpdate, kNames[ScopedI], kRunFunctions[ScopedI], nullptr, 0, 0,
                              ScopedAccess);
    }
    if (!ScopedGraph.Build())
    {
        return 0;
    }

    std::uint64_t ScopedBest = UINT64_MAX;
    for (std::uint32_t ScopedRun = 0; ScopedRun < kRunCount; ++ScopedRun)
    {
        const auto ScopedStart = std::chrono::steady_clock::now();
        ScopedGraph.Run(Fby::Ecs::Phase::PreUpdate, ScopedContext);
        const auto ScopedEnd = std::chrono::steady_clock::now();
        const std::uint64_t ScopedElapsed = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(ScopedEnd - ScopedStart).count());
        if (ScopedElapsed < ScopedBest)
        {
            ScopedBest = ScopedElapsed;
        }
    }
    return ScopedBest;
}
}

int main()
{
    Fby::Ecs::Registry ScopedRegistry;
    Fby::JobSystem ScopedJobs(8);

    const std::uint64_t ScopedSerial = Measure(false, ScopedRegistry, ScopedJobs);
    const std::uint64_t ScopedParallel = Measure(true, ScopedRegistry, ScopedJobs);
    const double ScopedSpeedup =
        ScopedParallel == 0 ? 0.0 : static_cast<double>(ScopedSerial) / static_cast<double>(ScopedParallel);
    std::printf("system_graph_benchmark: serial=%.3f ms parallel=%.3f ms speedup=%.2fx\n",
                static_cast<double>(ScopedSerial) / 1.0e6, static_cast<double>(ScopedParallel) / 1.0e6, ScopedSpeedup);
    return 0;
}
