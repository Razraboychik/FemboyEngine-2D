// FemboyEngine2D - JobSystem exception and shutdown contract.
#include "Engine/Core/JobSystem.hpp"

#include <atomic>
#include <cstdio>
#include <stdexcept>

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

    if (GFailures == 0)
    {
        std::printf("job_system: OK (exception isolation, shutdown drain)\n");
        return 0;
    }
    std::fprintf(stderr, "job_system: %d failure(s)\n", GFailures);
    return 1;
}
