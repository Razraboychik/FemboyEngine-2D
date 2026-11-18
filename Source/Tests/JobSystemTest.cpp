// FemboyEngine2D - JobSystem exception and shutdown contract.
#include "Engine/Core/JobSystem.hpp"
#include "Engine/ECS/ParallelForEach.hpp"

#include <atomic>
#include <cstdio>
#include <stdexcept>
#include <vector>

namespace
{
int GFailures = 0;

void Check(bool InCondition, const char* InMessage)
{
    if (!InCondition)
    {
        std::fprintf(stderr, "FAIL: %s\n", InMessage);
        ++GFailures;
    }
}

void IncrementThunk(void* InUser, std::uint32_t, std::uint32_t)
{
    static_cast<std::atomic<std::uint32_t>*>(InUser)->fetch_add(1, std::memory_order_relaxed);
}

void ThrowThunk(void*, std::uint32_t, std::uint32_t)
{
    throw std::runtime_error("task failure must not terminate the pool");
}
}

int main()
{
    // A throwing task must be contained: the queue keeps moving and WaitIdle
    // returns instead of terminating the process.
    {
        Fby::JobSystem ScopedJobs(2);
        std::atomic<std::uint32_t> ScopedDone{0};
        ScopedJobs.Submit(&ThrowThunk, nullptr, 0, 0);
        ScopedJobs.Submit(&IncrementThunk, &ScopedDone, 0, 0);
        ScopedJobs.WaitIdle();
        Check(ScopedDone.load() == 1, "a task after a throwing task still runs");
        Check(ScopedJobs.HasFaulted(), "the throwing task is latched as a fault");
        ScopedJobs.ClearFault();
        Check(!ScopedJobs.HasFaulted(), "the fault latch can be cleared after handling");
    }

    // The destructor drains queued work instead of dropping it.
    {
        std::atomic<std::uint32_t> ScopedDone{0};
        {
            Fby::JobSystem ScopedJobs(2);
            for (std::uint32_t ScopedI = 0; ScopedI < 16; ++ScopedI)
            {
                ScopedJobs.Submit(&IncrementThunk, &ScopedDone, 0, 0);
            }
        }
        Check(ScopedDone.load() == 16, "destructor drains every queued task");
    }

    // A zero grain must not divide by zero; it is treated as one element per chunk.
    {
        Fby::JobSystem ScopedJobs(2);
        std::atomic<std::uint32_t> ScopedVisited{0};
        ScopedJobs.ParallelFor(32, 0, [&ScopedVisited](std::uint32_t InBegin, std::uint32_t InEnd)
                               { ScopedVisited.fetch_add(InEnd - InBegin, std::memory_order_relaxed); });
        ScopedJobs.WaitIdle();
        Check(ScopedVisited.load() == 32, "zero grain visits every element once");
    }

    // A zero grain must not divide by zero or stall the boundary loop.
    {
        Fby::JobSystem ScopedJobs(2);
        std::vector<std::uint32_t> ScopedValues(1);
        std::atomic<std::uint32_t> ScopedVisited{0};
        Fby::Ecs::ParallelForEach(ScopedJobs, ScopedValues, 0,
                                  [&ScopedVisited](std::uint32_t&, auto&)
                                  { ScopedVisited.fetch_add(1, std::memory_order_relaxed); });
        Check(ScopedVisited.load() == 1, "ParallelForEach zero grain visits each element");
    }

    if (GFailures == 0)
    {
        std::printf("job_system: OK (exception isolation, shutdown drain)\n");
        return 0;
    }
    std::fprintf(stderr, "job_system: %d failure(s)\n", GFailures);
    return 1;
}
