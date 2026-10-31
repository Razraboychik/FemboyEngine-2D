#include "Engine/Core/Time.hpp"
#include "Engine/Core/JobSystem.hpp"

#include "Engine/Core/Assert.hpp"

#include <algorithm>
#include <cstdio>
#include <string>

namespace Fby
{
namespace
{
[[nodiscard]] std::uint64_t NowNanoseconds() noexcept
{
    return Core::Clock::NowNanoseconds();
}

// Every thread the engine creates is tagged once, so debug tooling and log
thread_local bool tls_worker = false;
// The storage owns the characters; the view is only a cheap read handle, so a
thread_local std::string tls_threadNameStorage = "main";
thread_local std::string_view tls_threadName = tls_threadNameStorage;

// Shared state of one ParallelFor call. It lives on the caller's stack; the
struct ParallelShared
{
    std::atomic<std::uint32_t> next{0};
    std::uint32_t chunks = 0;
    std::uint32_t grain = 0;
    std::uint32_t count = 0;
    void* context = nullptr;
    RangeFunction function = nullptr;
    std::counting_semaphore<JobSystem::kMaxThreadSlots>* completed = nullptr;
};

void RunChunks(ParallelShared* InShared) noexcept
{
    for (;;)
    {
        const std::uint32_t ScopedChunk = InShared->next.fetch_add(1, std::memory_order_relaxed);
        if (ScopedChunk >= InShared->chunks)
        {
            return;
        }
        const std::uint32_t ScopedBegin = ScopedChunk * InShared->grain;
        const std::uint32_t ScopedEnd = std::min(ScopedBegin + InShared->grain, InShared->count);
        InShared->function(InShared->context, ScopedBegin, ScopedEnd);
    }
}
}

JobSystem::JobSystem(std::uint32_t InThreadCount)
{
    if (InThreadCount == 0)
    {
        const std::uint32_t ScopedHardware = std::thread::hardware_concurrency();
        InThreadCount = ScopedHardware > 1 ? ScopedHardware - 1 : 1;
    }
    InThreadCount = std::clamp(InThreadCount, 1u, 64u);

    // Names are written once, before any worker exists, so SampleThreadLoad()
    std::snprintf(MCounters[0].name, sizeof(MCounters[0].name), "main");
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < InThreadCount; ++ScopedIndex)
    {
        std::snprintf(MCounters[ScopedIndex + 1].name, sizeof(MCounters[ScopedIndex + 1].name), "worker-%u",
                      ScopedIndex);
    }

    MWorkers.reserve(InThreadCount);
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < InThreadCount; ++ScopedIndex)
    {
        MWorkers.emplace_back([this, ScopedIndex](std::stop_token InToken) { WorkerMain(InToken, ScopedIndex); });
    }
}

JobSystem::~JobSystem()
{
    {
        const std::scoped_lock ScopedLock(MMutex);
        MStopping = true;
    }
    MWake.notify_all();
    // Join explicitly before the jthread destructors run: a jthread destructor
    // requests stop, which would abort the queue. MStopping makes every worker
    // drain the queue and then return.
    for (std::jthread& ScopedWorker : MWorkers)
    {
        if (ScopedWorker.joinable())
        {
            ScopedWorker.join();
        }
    }
}

void JobSystem::Submit(RangeFunction InRun, void* InContext, std::uint32_t InBegin, std::uint32_t InEnd)
{
    FBY_ASSERT(InRun != nullptr);
    MPending.fetch_add(1, std::memory_order_relaxed);
    {
        const std::scoped_lock ScopedLock(MMutex);
        MQueue.push_back(Task{InRun, InContext, InBegin, InEnd});
    }
    MWake.notify_one();
}

void JobSystem::WaitIdle()
{
    std::unique_lock ScopedLock(MMutex);
    MIdle.wait(ScopedLock, [this] { return MQueue.empty() && MPending.load(std::memory_order_acquire) == 0; });
}

bool JobSystem::IsWorkerThread() noexcept
{
    return tls_worker;
}

std::string_view JobSystem::CurrentThreadName() noexcept
{
    return tls_threadName;
}

void JobSystem::WorkerMain(std::stop_token InToken, std::uint32_t InIndex)
{
    tls_worker = true;
    tls_threadNameStorage = "worker-" + std::to_string(InIndex);
    tls_threadName = tls_threadNameStorage;

    while (!InToken.stop_requested())
    {
        Task ScopedTask;
        {
            std::unique_lock ScopedLock(MMutex);
            MWake.wait(ScopedLock, InToken, [this] { return !MQueue.empty() || MStopping; });
            if (InToken.stop_requested() || MQueue.empty())
            {
                if (MStopping)
                {
                    return;
                }
                continue;
            }
            ScopedTask = std::move(MQueue.front());
            MQueue.pop_front();
        }

        MRunning.fetch_add(1, std::memory_order_acquire);
        const std::uint64_t ScopedStart = NowNanoseconds();
        try
        {
            ScopedTask.run(ScopedTask.context, ScopedTask.begin, ScopedTask.end);
        }
        // A task exception must not terminate the process or leave the queue and the idle waiter stuck.
        catch (...)
        {
        }
        AddBusy(InIndex + 1, NowNanoseconds() - ScopedStart);
        MCounters[InIndex + 1].taskCount.fetch_add(1, std::memory_order_relaxed);
        MRunning.fetch_sub(1, std::memory_order_release);
        MPending.fetch_sub(1, std::memory_order_release);

        // Wake WaitIdle() once the tail of the queue has drained.
        {
            const std::scoped_lock ScopedLock(MMutex);
            if (MQueue.empty() && MRunning.load(std::memory_order_relaxed) == 0)
            {
                MIdle.notify_all();
            }
        }
    }
}

void JobSystem::DispatchParallel(std::uint32_t InCount, std::uint32_t InGrain, void* InContext,
                                 RangeFunction InFunction)
{
    FBY_ASSERT(InFunction != nullptr);

    const std::uint32_t ScopedChunks = (InCount + InGrain - 1) / InGrain;
    if (ScopedChunks <= 1)
    {
        InFunction(InContext, 0, InCount);
        return;
    }

    ParallelShared ScopedShared;
    ScopedShared.chunks = ScopedChunks;
    ScopedShared.grain = InGrain;
    ScopedShared.count = InCount;
    ScopedShared.context = InContext;
    ScopedShared.function = InFunction;
    // One semaphore per call, on the caller's stack. A shared member would
    std::counting_semaphore<JobSystem::kMaxThreadSlots> ScopedCompleted{0};
    ScopedShared.completed = &ScopedCompleted;

    // The caller keeps one chunk, so a one-worker pool still makes progress and
    const std::uint32_t ScopedHelpers = std::min<std::uint32_t>(ScopedChunks - 1, ThreadCount());

    for (std::uint32_t ScopedI = 0; ScopedI < ScopedHelpers; ++ScopedI)
    {
        // Captureless thunk: the context is a pointer, not a std::function.
        Submit(
            +[](void* InRaw, std::uint32_t, std::uint32_t)
            {
                auto* ScopedShared = static_cast<ParallelShared*>(InRaw);
                RunChunks(ScopedShared);
                ScopedShared->completed->release();
            },
            &ScopedShared, 0, 0);
    }

    const std::uint64_t ScopedDispatchStart = NowNanoseconds();
    RunChunks(&ScopedShared);
    AddBusy(0, NowNanoseconds() - ScopedDispatchStart);
    MCounters[0].taskCount.fetch_add(1, std::memory_order_relaxed);

    for (std::uint32_t ScopedI = 0; ScopedI < ScopedHelpers; ++ScopedI)
    {
        ScopedCompleted.acquire();
    }
}

void JobSystem::AddBusy(std::uint32_t InSlot, std::uint64_t InNanoseconds) noexcept
{
    if (InSlot < kMaxThreadSlots)
    {
        MCounters[InSlot].busyNanoseconds.fetch_add(InNanoseconds, std::memory_order_relaxed);
    }
}

std::uint32_t JobSystem::SampleThreadLoad(std::span<ThreadLoad> InLoads) const noexcept
{
    if (InLoads.empty())
    {
        return 0;
    }
    const std::uint32_t ScopedSlots = std::min<std::uint32_t>(ThreadSlotCount(), kMaxThreadSlots);
    const std::uint32_t ScopedWritten =
        std::min<std::uint32_t>(ScopedSlots, static_cast<std::uint32_t>(InLoads.size()));
    for (std::uint32_t ScopedSlot = 0; ScopedSlot < ScopedWritten; ++ScopedSlot)
    {
        InLoads[ScopedSlot].busyNanoseconds = MCounters[ScopedSlot].busyNanoseconds.load(std::memory_order_relaxed);
        InLoads[ScopedSlot].taskCount = MCounters[ScopedSlot].taskCount.load(std::memory_order_relaxed);
        std::snprintf(InLoads[ScopedSlot].name, sizeof(InLoads[ScopedSlot].name), "%s", MCounters[ScopedSlot].name);
    }
    return ScopedWritten;
}
}
