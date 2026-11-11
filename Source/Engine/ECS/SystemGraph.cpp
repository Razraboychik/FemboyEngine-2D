#include "Engine/ECS/SystemGraph.hpp"

#include "Engine/ECS/FCS/AccessValidation.hpp"

#include <chrono>

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
    const char* name = nullptr;
    SystemContext* context = nullptr;
    std::counting_semaphore<kMaxGraphSystems>* completed = nullptr;
    const SystemAccess* access = nullptr;
    const GraphSystem* system = nullptr;
    bool timing = false;
};

[[nodiscard]] std::uint64_t ElapsedNs(std::chrono::steady_clock::time_point InStart) noexcept
{
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - InStart).count());
}

void RunTimed(const GraphInvocation& InInvocation, SystemContext& InContext)
{
    if (!InInvocation.timing)
    {
        InInvocation.run(InContext, InInvocation.user);
        return;
    }
    const auto ScopedStart = std::chrono::steady_clock::now();
    InInvocation.run(InContext, InInvocation.user);
    if (InInvocation.system != nullptr)
    {
        InInvocation.system->LastNs = ElapsedNs(ScopedStart);
    }
}

[[nodiscard]] const std::uint64_t* AccessReads(const SystemAccess* InAccess) noexcept
{
    return InAccess != nullptr && IsSystemAccessDeclared(*InAccess) ? InAccess->ComponentReads.data() : nullptr;
}

[[nodiscard]] const std::uint64_t* AccessWrites(const SystemAccess* InAccess) noexcept
{
    return InAccess != nullptr && IsSystemAccessDeclared(*InAccess) ? InAccess->ComponentWrites.data() : nullptr;
}

void InvokeGraphNode(void* InRaw, std::uint32_t, std::uint32_t) noexcept
{
    auto* ScopedInvocation = static_cast<GraphInvocation*>(InRaw);
    const Fcs::Detail::ScopedAccess ScopedAccessCheck(AccessReads(ScopedInvocation->access),
                                                      AccessWrites(ScopedInvocation->access));
    // The calling Run blocks until this release, so a throwing node is contained
    // here instead of escaping into the worker loop with the semaphore unreleased.
    try
    {
        RunTimed(*ScopedInvocation, *ScopedInvocation->context);
    }
    catch (...)
    {
        ScopedInvocation->context->jobs.ReportFault(ScopedInvocation->name != nullptr ? ScopedInvocation->name
                                                                                      : "system node");
    }
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

    // Access conflicts: two same-phase systems that touch the same engine service
    // are ordered by registration instead of running in parallel on it. A zero
    // declaration keeps the previous behavior.
    for (std::uint32_t ScopedFirst = 0; ScopedFirst < MCount; ++ScopedFirst)
    {
        for (std::uint32_t ScopedSecond = ScopedFirst + 1u; ScopedSecond < MCount; ++ScopedSecond)
        {
            if (MSystems[ScopedFirst].phase != MSystems[ScopedSecond].phase)
            {
                continue;
            }
            const SystemAccess& ScopedLeft = MSystems[ScopedFirst].access;
            const SystemAccess& ScopedRight = MSystems[ScopedSecond].access;
            const bool ScopedServiceConflict = (ScopedLeft.Writes & (ScopedRight.Reads | ScopedRight.Writes)) != 0u ||
                                               (ScopedLeft.Reads & ScopedRight.Writes) != 0u;
            const bool ScopedComponentConflict =
                ComponentAccessMasksConflict(ScopedLeft.ComponentReads, ScopedLeft.ComponentWrites,
                                             ScopedRight.ComponentReads, ScopedRight.ComponentWrites);
            // Parallel execution is opt-in: a system that did not declare its
            // ECS contract is serialized against every same-phase peer.
            const bool ScopedNotParallelSafe = !ScopedLeft.ParallelSafe || !ScopedRight.ParallelSafe;
            if (ScopedServiceConflict || ScopedComponentConflict || ScopedNotParallelSafe)
            {
                ScopedDependencies[ScopedSecond] |= (1ull << ScopedFirst);
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
    if (MPaused && InPhase != Phase::Render && !(MRunUiWhilePaused && InPhase == Phase::Ui))
    {
        return;
    }

    const auto ScopedRunNode = [&InContext, this](const GraphSystem& InSystem)
    {
        if (InSystem.enabled && InSystem.run != nullptr)
        {
            const Fcs::Detail::ScopedAccess ScopedAccessCheck(AccessReads(&InSystem.access),
                                                              AccessWrites(&InSystem.access));
            if (MTimingEnabled)
            {
                const auto ScopedStart = std::chrono::steady_clock::now();
                InSystem.run(InContext, InSystem.user);
                InSystem.LastNs = ElapsedNs(ScopedStart);
                return;
            }
            InSystem.run(InContext, InSystem.user);
        }
    };

    // Render owns the RHI command stream and must stay on the render thread. A
    // worker can re-enter the graph from inside a node; submitting there would
    // wait for workers that are themselves waiting, so those runs stay inline.
    const bool ScopedInline = MDeterministicSimulation || InPhase == Phase::Render ||
                              InContext.jobs.ThreadCount() == 0 || JobSystem::IsWorkerThread();
    if (ScopedInline)
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
                GraphInvocation{ScopedSystem.run, ScopedSystem.user,    ScopedSystem.name, &InContext,
                                nullptr,          &ScopedSystem.access, &ScopedSystem,     MTimingEnabled};
            ++ScopedWaveSize;
        }
        if (ScopedWaveSize == 0)
        {
            continue;
        }
        if (ScopedWaveSize == 1)
        {
            const Fcs::Detail::ScopedAccess ScopedAccessCheck(AccessReads(ScopedInvocations[0].access),
                                                              AccessWrites(ScopedInvocations[0].access));
            RunTimed(ScopedInvocations[0], InContext);
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
        // The caller thread owns one node too. A throw here must not unwind while
        // submitted workers still reference ScopedInvocations and ScopedCompleted;
        // report the fault, then drain the wave before returning.
        try
        {
            const Fcs::Detail::ScopedAccess ScopedAccessCheck(AccessReads(ScopedInvocations[0].access),
                                                              AccessWrites(ScopedInvocations[0].access));
            RunTimed(ScopedInvocations[0], InContext);
        }
        catch (...)
        {
            InContext.jobs.ReportFault(ScopedInvocations[0].name != nullptr ? ScopedInvocations[0].name
                                                                            : "system node");
        }
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
