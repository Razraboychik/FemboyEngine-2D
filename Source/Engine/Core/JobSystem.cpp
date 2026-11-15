#include "Engine/Core/Time.hpp"
#include "Engine/Core/JobSystem.hpp"

#include "Engine/Core/Assert.hpp"
#include "Engine/Core/Log.hpp"
#include "Engine/Core/ThreadAffinity.hpp"

#include <algorithm>
#include <cstdio>
#include <exception>
#include <limits>
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
thread_local bool ThreadLocalWorker = false;
// The storage owns the characters; the view is only a cheap read handle.
thread_local std::string ThreadLocalNameStorage = "main";
thread_local std::string_view ThreadLocalName = ThreadLocalNameStorage;

// Shared state of one ParallelFor call. It lives on the caller's stack.
struct ParallelShared
{
    std::atomic<std::uint32_t> next{0};
    std::uint32_t chunks = 0;
    std::uint32_t grain = 0;
    std::uint32_t count = 0;
    void* context = nullptr;
    RangeFunction function = nullptr;
    JobSystem* owner = nullptr;
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
        const std::uint32_t ScopedBegin = static_cast<std::uint32_t>(
            static_cast<std::uint64_t>(ScopedChunk) * static_cast<std::uint64_t>(InShared->grain));
        const std::uint32_t ScopedEnd = static_cast<std::uint32_t>(
            std::min<std::uint64_t>(static_cast<std::uint64_t>(ScopedBegin) + InShared->grain, InShared->count));
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
    try
    {
        const std::scoped_lock ScopedLock(MMutex);
        MQueue.push_back(Task{InRun, InContext, InBegin, InEnd});
    }
    // A task that cannot be queued would leave the caller waiting for a completion
    // that never arrives, so it runs on the submitting thread instead.
    catch (...)
    {
        MPending.fetch_sub(1, std::memory_order_relaxed);
        ReportFault("job queue allocation");
        InRun(InContext, InBegin, InEnd);
        // The inline path bypasses WorkerMain, so it owns the idle wakeup for
        // the drained queue.
        const std::scoped_lock ScopedIdleLock(MMutex);
        if (MQueue.empty() && MPending.load(std::memory_order_acquire) == 0)
        {
            MIdle.notify_all();
        }
        return;
    }
    MWake.notify_one();
}

void JobSystem::ReportFault(std::string_view InOperation) noexcept
{
    const bool ScopedFirst = !MFaulted.exchange(true, std::memory_order_acq_rel);
    if (ScopedFirst)
    {
        try
        {
            LogError("jobs", "a job task failed on thread '{}' ({})", CurrentThreadName(), InOperation);
        }
        catch (...)
        {
            // This is already the failure path; never let diagnostics terminate
            // a noexcept fault latch.
        }
    }
}

void JobSystem::WaitIdle()
{
    std::unique_lock ScopedLock(MMutex);
    MIdle.wait(ScopedLock, [this] { return MQueue.empty() && MPending.load(std::memory_order_acquire) == 0; });
}

bool JobSystem::IsWorkerThread() noexcept
{
    return ThreadLocalWorker;
}

std::string_view JobSystem::CurrentThreadName() noexcept
{
    return ThreadLocalName;
}

void JobSystem::WorkerMain(std::stop_token InToken, std::uint32_t InIndex)
{
    ThreadLocalWorker = true;
    BindThreadRole(ThreadRole::Worker);
    ThreadLocalNameStorage = "worker-" + std::to_string(InIndex);
    ThreadLocalName = ThreadLocalNameStorage;

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
        // A task exception must not terminate the process or leave the queue and
        // the idle waiter stuck, but it is latched so the main loop can stop.
        catch (...)
        {
            ReportFault("worker task");
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

    const std::uint64_t ScopedChunkCount =
        (static_cast<std::uint64_t>(InCount) + InGrain - 1u) / static_cast<std::uint64_t>(InGrain);
    if (ScopedChunkCount <= 1u)
    {
        InFunction(InContext, 0, InCount);
        return;
    }
    const std::uint32_t ScopedChunks = static_cast<std::uint32_t>(
        std::min<std::uint64_t>(ScopedChunkCount, std::numeric_limits<std::uint32_t>::max()));

    ParallelShared ScopedShared;
    ScopedShared.chunks = ScopedChunks;
    ScopedShared.grain = InGrain;
    ScopedShared.count = InCount;
    ScopedShared.context = InContext;
    ScopedShared.function = InFunction;
    // One semaphore per call, on the caller's stack. A shared member would
    std::counting_semaphore<JobSystem::kMaxThreadSlots> ScopedCompleted{0};
    ScopedShared.completed = &ScopedCompleted;

    // The caller keeps one chunk, so a one-worker pool still makes progress.
    const std::uint32_t ScopedHelpers = std::min<std::uint32_t>(ScopedChunks - 1, ThreadCount());

    ScopedShared.owner = this;
    for (std::uint32_t ScopedI = 0; ScopedI < ScopedHelpers; ++ScopedI)
    {
        // Captureless thunk: the context is a pointer, not a std::function.
        Submit(
            +[](void* InRaw, std::uint32_t, std::uint32_t)
            {
                auto* ScopedShared = static_cast<ParallelShared*>(InRaw);
                // The caller blocks until this release, so a throwing chunk is
                // contained here instead of escaping into the worker loop.
                try
                {
                    RunChunks(ScopedShared);
                }
                catch (...)
                {
                    ScopedShared->owner->ReportFault("parallel for chunk");
                }
                ScopedShared->completed->release();
            },
            &ScopedShared, 0, 0);
    }

    const std::uint64_t ScopedDispatchStart = NowNanoseconds();
    // The caller owns one chunk. If its body throws, the helper tasks are still
    // running and reference the stack state above, so drain the wave before the
    // exception leaves this frame.
    std::exception_ptr ScopedCallerError;
    try
    {
        RunChunks(&ScopedShared);
    }
    catch (...)
    {
        ScopedCallerError = std::current_exception();
    }
    AddBusy(0, NowNanoseconds() - ScopedDispatchStart);
    MCounters[0].taskCount.fetch_add(1, std::memory_order_relaxed);

    for (std::uint32_t ScopedI = 0; ScopedI < ScopedHelpers; ++ScopedI)
    {
        ScopedCompleted.acquire();
    }
    if (ScopedCallerError != nullptr)
    {
        std::rethrow_exception(ScopedCallerError);
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
