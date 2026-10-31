#include "Engine/ECS/SystemGraph.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <semaphore>

namespace Fby::Ecs
{
namespace
{
// One independently runnable node. The semaphore is per Run() call and lives on
struct GraphInvocation
{
    GraphSystemFn run = nullptr;
    void* user = nullptr;
    SystemContext* context = nullptr;
    std::counting_semaphore<kMaxGraphSystems>* completed = nullptr;
};

void InvokeGraphNode(void* InRaw, std::uint32_t, std::uint32_t) noexcept
{
    auto* ScopedInvocation = static_cast<GraphInvocation*>(InRaw);
    ScopedInvocation->run(*ScopedInvocation->context, ScopedInvocation->user);
    ScopedInvocation->completed->release();
}
}

bool SystemGraph::Build()
{
    if (MBuilt)
    {
        return true;
    }
    // A failed build must not leave a partial order behind.
    MOrderCount = 0;
    MLevelCount = 0;
    MBuilt = false;
    std::array<std::uint64_t, kMaxGraphSystems> ScopedDependencies{};
    const std::uint64_t ScopedValidMask =
        MCount >= kMaxGraphSystems ? ~std::uint64_t{0} : ((std::uint64_t{1} << MCount) - 1ull);
    for (std::uint32_t ScopedI = 0; ScopedI < MCount; ++ScopedI)
    {
        // A mask that names a node which does not exist is a caller bug; fail
        // instead of silently dropping the edge.
        if (((MSystems[ScopedI].after | MSystems[ScopedI].before) & ~ScopedValidMask) != 0)
        {
            return false;
        }
        ScopedDependencies[ScopedI] = MSystems[ScopedI].after & ScopedValidMask;
        if (ScopedDependencies[ScopedI] != 0)
        {
            const std::uint64_t ScopedMask = ScopedDependencies[ScopedI];
            for (std::uint32_t ScopedDependency = 0; ScopedDependency < MCount; ++ScopedDependency)
            {
                if ((ScopedMask & (1ull << ScopedDependency)) == 0)
                {
                    continue;
                }
                if (static_cast<std::uint32_t>(MSystems[ScopedDependency].phase) >
                    static_cast<std::uint32_t>(MSystems[ScopedI].phase))
                {
                    // A later phase cannot feed an earlier one
                    return false;
                }
            }
        }
    }
    // before: if i.before has j, then j depends on i.
    for (std::uint32_t ScopedI = 0; ScopedI < MCount; ++ScopedI)
    {
        for (std::uint32_t ScopedJ = 0; ScopedJ < MCount; ++ScopedJ)
        {
            if ((MSystems[ScopedI].before & (1ull << ScopedJ)) != 0)
            {
                if (static_cast<std::uint32_t>(MSystems[ScopedJ].phase) <
                    static_cast<std::uint32_t>(MSystems[ScopedI].phase))
                {
                    return false;
                }
                ScopedDependencies[ScopedJ] |= (1ull << ScopedI);
            }
        }
    }

    std::array<bool, kMaxGraphSystems> ScopedEmitted{};
    MOrderCount = 0;
    for (std::uint32_t ScopedPhase = 0; ScopedPhase < static_cast<std::uint32_t>(Phase::Count); ++ScopedPhase)
    {
        bool ScopedProgress = true;
        while (ScopedProgress)
        {
            ScopedProgress = false;
            for (std::uint32_t ScopedI = 0; ScopedI < MCount; ++ScopedI)
            {
                if (ScopedEmitted[ScopedI] || static_cast<std::uint32_t>(MSystems[ScopedI].phase) != ScopedPhase)
                {
                    continue;
                }
                bool ScopedReady = true;
                for (std::uint32_t ScopedDependency = 0; ScopedDependency < MCount; ++ScopedDependency)
                {
                    if ((ScopedDependencies[ScopedI] & (1ull << ScopedDependency)) != 0 &&
                        !ScopedEmitted[ScopedDependency])
                    {
                        ScopedReady = false;
                        break;
                    }
                }
                if (!ScopedReady)
                {
                    continue;
                }
                ScopedEmitted[ScopedI] = true;
                MOrder[MOrderCount++] = ScopedI;
                ScopedProgress = true;
            }
        }
    }

    if (MOrderCount != MCount)
    {
        // Cycle: do not leave a partial order for Run to observe.
        MOrderCount = 0;
        MLevelCount = 0;
        return false;
    }

    // Persist the resolved masks. `before` edges are already folded into
    for (std::uint32_t ScopedI = 0; ScopedI < MCount; ++ScopedI)
    {
        MDependencies[ScopedI] = ScopedDependencies[ScopedI];
        MLevels[ScopedI] = 0;
    }

    // Longest-path level per node. A dependency on a node from an earlier phase
    MLevelCount = 0;
    for (std::uint32_t ScopedPosition = 0; ScopedPosition < MOrderCount; ++ScopedPosition)
    {
        const std::uint32_t ScopedNode = MOrder[ScopedPosition];
        std::uint32_t ScopedLevel = 0;
        const std::uint64_t ScopedMask = MDependencies[ScopedNode];
        for (std::uint32_t ScopedDependency = 0; ScopedDependency < MCount; ++ScopedDependency)
        {
            if ((ScopedMask & (std::uint64_t{1} << ScopedDependency)) == 0)
            {
                continue;
            }
            if (MSystems[ScopedDependency].phase != MSystems[ScopedNode].phase)
            {
                continue;
            }
            ScopedLevel = std::max(ScopedLevel, MLevels[ScopedDependency] + 1u);
        }
        MLevels[ScopedNode] = ScopedLevel;
        MLevelCount = std::max(MLevelCount, ScopedLevel + 1u);
    }

    MBuilt = true;
    return true;
}

void SystemGraph::Run(Phase InPhase, SystemContext& InContext) const
{
    if (!MBuilt)
    {
        return;
    }

    const auto ScopedRunNode = [&InContext](const GraphSystem& InSystem)
    {
        if (InSystem.enabled && InSystem.run != nullptr)
        {
            InSystem.run(InContext, InSystem.user);
        }
    };

    // Render owns the RHI command stream and must stay on the render thread.
    if (InPhase == Phase::Render || InContext.jobs.ThreadCount() == 0)
    {
        for (std::uint32_t ScopedI = 0; ScopedI < MOrderCount; ++ScopedI)
        {
            const GraphSystem& ScopedSystem = MSystems[MOrder[ScopedI]];
            if (ScopedSystem.phase == InPhase)
            {
                ScopedRunNode(ScopedSystem);
            }
        }
        return;
    }

    for (std::uint32_t ScopedLevel = 0; ScopedLevel < MLevelCount; ++ScopedLevel)
    {
        std::array<GraphInvocation, kMaxGraphSystems> ScopedInvocations{};
        std::uint32_t ScopedWaveSize = 0;
        for (std::uint32_t ScopedI = 0; ScopedI < MOrderCount; ++ScopedI)
        {
            const std::uint32_t ScopedNode = MOrder[ScopedI];
            const GraphSystem& ScopedSystem = MSystems[ScopedNode];
            if (!ScopedSystem.enabled || ScopedSystem.phase != InPhase || MLevels[ScopedNode] != ScopedLevel)
            {
                continue;
            }
            ScopedInvocations[ScopedWaveSize] =
                GraphInvocation{ScopedSystem.run, ScopedSystem.user, &InContext, nullptr};
            ++ScopedWaveSize;
        }
        if (ScopedWaveSize == 0)
        {
            continue;
        }
        if (ScopedWaveSize == 1)
        {
            ScopedInvocations[0].run(InContext, ScopedInvocations[0].user);
            continue;
        }

        std::counting_semaphore<kMaxGraphSystems> ScopedCompleted{0};
        // Leave one node for the caller: a one-worker pool still makes progress
        std::uint32_t ScopedSubmitted = 0;
        for (std::uint32_t ScopedI = 1; ScopedI < ScopedWaveSize; ++ScopedI)
        {
            ScopedInvocations[ScopedI].completed = &ScopedCompleted;
            InContext.jobs.Submit(&InvokeGraphNode, &ScopedInvocations[ScopedI], 0, 0);
            ++ScopedSubmitted;
        }
        ScopedInvocations[0].run(InContext, ScopedInvocations[0].user);
        for (std::uint32_t ScopedI = 0; ScopedI < ScopedSubmitted; ++ScopedI)
        {
            ScopedCompleted.acquire();
        }
    }
}

bool SystemGraph::RemoveUser(void* InUser)
{
    std::array<std::uint32_t, kMaxGraphSystems> ScopedRemap{};
    // Every slot outside the current range must map to "removed": a zero tail
    // would make a high mask bit look like a dependency on node 0.
    ScopedRemap.fill(kMaxGraphSystems);

    std::uint32_t ScopedNewCount = 0;
    bool ScopedRemoved = false;
    for (std::uint32_t ScopedI = 0; ScopedI < MCount; ++ScopedI)
    {
        if (MSystems[ScopedI].user == InUser)
        {
            ScopedRemoved = true;
            continue;
        }
        ScopedRemap[ScopedI] = ScopedNewCount;
        MSystems[ScopedNewCount] = MSystems[ScopedI];
        ++ScopedNewCount;
    }
    if (!ScopedRemoved)
    {
        return false;
    }

    // Remap after/before masks so a dependency on a surviving node keeps
    const auto ScopedRemapMask = [&ScopedRemap](std::uint64_t InMask) -> std::uint64_t
    {
        std::uint64_t ScopedResult = 0;
        for (std::uint32_t ScopedI = 0; ScopedI < kMaxGraphSystems; ++ScopedI)
        {
            if ((InMask & (std::uint64_t{1} << ScopedI)) == 0)
            {
                continue;
            }
            if (ScopedRemap[ScopedI] != kMaxGraphSystems)
            {
                ScopedResult |= (std::uint64_t{1} << ScopedRemap[ScopedI]);
            }
        }
        return ScopedResult;
    };
    for (std::uint32_t ScopedI = 0; ScopedI < ScopedNewCount; ++ScopedI)
    {
        MSystems[ScopedI].after = ScopedRemapMask(MSystems[ScopedI].after);
        MSystems[ScopedI].before = ScopedRemapMask(MSystems[ScopedI].before);
        MSystems[ScopedI].order = ScopedI;
    }
    MCount = ScopedNewCount;
    MOrderCount = 0;
    MBuilt = false;
    return true;
}
}
