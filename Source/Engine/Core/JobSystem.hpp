// FemboyEngine2D - job system.
#pragma once

#include <atomic>
#include <array>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <semaphore>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <vector>

namespace Fby
{

// A range callback used by ParallelFor: (begin, end) in element units.
using RangeFunction = void (*)(void* InContext, std::uint32_t InBegin, std::uint32_t InEnd);

class JobSystem
{
public:
    static constexpr std::uint32_t kMaxThreadSlots = 65;

    // One diagnostics row per thread: slot 0 is the main thread, slots
    struct ThreadLoad
    {
        std::uint64_t busyNanoseconds = 0;
        std::uint64_t taskCount = 0;
        char name[16] = {};
    };

    // ThreadCount == 0 picks hardware_concurrency() - 1 (the main thread is a
    explicit JobSystem(std::uint32_t InThreadCount = 0);
    ~JobSystem();

    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    // Queues one range task. The returned token is not needed by the engine
    void Submit(RangeFunction InRun, void* InContext, std::uint32_t InBegin, std::uint32_t InEnd);

    // Splits [0, count) into chunks of at least `grain` elements and runs them
    template <typename Body> void ParallelFor(std::uint32_t InCount, std::uint32_t InGrain, Body&& InBody)
    {
        if (InCount == 0)
        {
            return;
        }
        if (InGrain == 0)
        {
            InGrain = 1;
        }

        // Small ranges and worker-thread reentry run inline: submitting a task
        if (InCount <= InGrain || MWorkers.empty() || IsWorkerThread())
        {
            InBody(0, InCount);
            return;
        }

        auto* ScopedContext = static_cast<void*>(std::addressof(InBody));
        DispatchParallel(InCount, InGrain, ScopedContext, [](void* InRaw, std::uint32_t InBegin, std::uint32_t InEnd)
                         { (*static_cast<std::remove_reference_t<Body>*>(InRaw))(InBegin, InEnd); });
    }

    // Blocks until every queued task and every in-flight ParallelFor chunk has
    void WaitIdle();

    [[nodiscard]] std::uint32_t ThreadCount() const noexcept
    {
        return static_cast<std::uint32_t>(MWorkers.size());
    }
    // Workers plus the main-thread slot, i.e. the number of rows a diagnostics
    [[nodiscard]] std::uint32_t ThreadSlotCount() const noexcept
    {
        return ThreadCount() + 1;
    }
    [[nodiscard]] std::uint32_t PendingTasks() const noexcept
    {
        return MPending.load(std::memory_order_relaxed);
    }
    // Copies the per-thread counters into `InLoads` (slot 0 = main). The copy is
    std::uint32_t SampleThreadLoad(std::span<ThreadLoad> InLoads) const noexcept;
    [[nodiscard]] static bool IsWorkerThread() noexcept;

    // Human readable identity of the current thread: "main" or "worker-N".
    [[nodiscard]] static std::string_view CurrentThreadName() noexcept;

private:
    struct Task
    {
        RangeFunction run = nullptr;
        void* context = nullptr;
        std::uint32_t begin = 0;
        std::uint32_t end = 0;
    };

    void WorkerMain(std::stop_token InToken, std::uint32_t InIndex);
    void DispatchParallel(std::uint32_t InCount, std::uint32_t InGrain, void* InContext, RangeFunction InFunction);
    void AddBusy(std::uint32_t InSlot, std::uint64_t InNanoseconds) noexcept;

    // Diagnostics counters. Each row owns its cache line so two workers
    struct ThreadCounter
    {
        alignas(64) std::atomic<std::uint64_t> busyNanoseconds{0};
        std::atomic<std::uint64_t> taskCount{0};
        char name[16] = {};
    };

    std::vector<std::jthread> MWorkers;
    mutable std::mutex MMutex;
    std::condition_variable_any MWake;
    std::deque<Task> MQueue;
    // The two hot counters are written by every submit and every completed
    alignas(64) std::atomic<std::uint32_t> MPending{0};
    alignas(64) std::atomic<std::uint32_t> MRunning{0};
    std::condition_variable MIdle;
    bool MStopping = false;
    std::array<ThreadCounter, kMaxThreadSlots> MCounters{};
};
}
