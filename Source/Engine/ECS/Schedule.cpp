#include "Engine/ECS/Schedule.hpp"

namespace Fby::Ecs
{

bool Schedule::Add(Phase InPhase, const char* InName, SystemFunction InRun, void* InUser)
{
    if (MCount >= kMaxSystems || InRun == nullptr || InName == nullptr)
    {
        return false;
    }
    MSystems[MCount++] = Entry{InName, InPhase, InRun, InUser};
    return true;
}

void Schedule::Clear() noexcept
{
    MCount = 0;
}

void Schedule::Run(Phase InPhase, SystemContext& InContext) const
{
    for (std::uint32_t ScopedI = 0; ScopedI < MCount; ++ScopedI)
    {
        const Entry& ScopedSystem = MSystems[ScopedI];
        if (ScopedSystem.phase == InPhase)
        {
            ScopedSystem.run(InContext, ScopedSystem.user);
        }
    }
}

std::size_t Schedule::Count(Phase InPhase) const noexcept
{
    std::size_t ScopedCount = 0;
    for (std::uint32_t ScopedI = 0; ScopedI < MCount; ++ScopedI)
    {
        ScopedCount += MSystems[ScopedI].phase == InPhase ? 1 : 0;
    }
    return ScopedCount;
}
}
