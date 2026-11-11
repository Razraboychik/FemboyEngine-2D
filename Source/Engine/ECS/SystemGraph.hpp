// FemboyEngine2D - declarative system graph.
#pragma once

#include "Engine/ECS/FCS/World.hpp"
#include "Engine/ECS/Schedule.hpp"

#include <array>
#include <cstdint>
#include <type_traits>

namespace Fby::Ecs
{

inline constexpr std::uint32_t kMaxGraphSystems = 64;

// Engine services a system may touch. Declaring access is optional; when two
// same-phase systems conflict on a service, Build() orders them instead of
// running them in parallel.
inline constexpr std::uint32_t kServicePhysics = 1u << 0;
inline constexpr std::uint32_t kServiceAudio = 1u << 1;
inline constexpr std::uint32_t kServiceRenderer = 1u << 2;
inline constexpr std::uint32_t kServiceWindow = 1u << 3;
inline constexpr std::uint32_t kServiceText = 1u << 4;
inline constexpr std::uint32_t kServiceScript = 1u << 5;
inline constexpr std::uint32_t kServiceNetwork = 1u << 6;
inline constexpr std::uint32_t kServiceAssets = 1u << 7;
inline constexpr std::uint32_t kServiceNavigation = 1u << 8;
inline constexpr std::uint32_t kServiceInput = 1u << 9;

// Component IDs are runtime ids from the FCS table, so the access mask is four
// 64-bit words. A zero mask keeps the previous behavior.
using ComponentAccessMask = std::array<std::uint64_t, 4>;

template <typename... Components> [[nodiscard]] ComponentAccessMask MakeComponentAccessMask() noexcept
{
    ComponentAccessMask ScopedMask{};
    const auto ScopedAdd = [&ScopedMask](std::uint16_t InId)
    {
        ScopedMask[InId / 64u] |= (std::uint64_t{1} << (InId % 64u));
    };
    (ScopedAdd(Fby::Fcs::Detail::ComponentIdOf<Components>()), ...);
    return ScopedMask;
}

[[nodiscard]] constexpr bool ComponentAccessMasksConflict(const ComponentAccessMask& InLeftReads,
                                                          const ComponentAccessMask& InLeftWrites,
                                                          const ComponentAccessMask& InRightReads,
                                                          const ComponentAccessMask& InRightWrites) noexcept
{
    for (std::size_t ScopedWord = 0; ScopedWord < InLeftReads.size(); ++ScopedWord)
    {
        if ((InLeftWrites[ScopedWord] & (InRightReads[ScopedWord] | InRightWrites[ScopedWord])) != 0u ||
            (InLeftReads[ScopedWord] & InRightWrites[ScopedWord]) != 0u)
        {
            return true;
        }
    }
    return false;
}

// What a system reads and writes outside the registry and in the ECS. Service
// bits are the constants above; component masks are optional but let Build()
// serialize systems that declare overlapping component access.
struct SystemAccess
{
    std::uint32_t Reads = 0;
    std::uint32_t Writes = 0;
    ComponentAccessMask ComponentReads{};
    ComponentAccessMask ComponentWrites{};
    // Same-phase systems run in parallel only when both explicitly opt in.
    // The graph cannot prove that an arbitrary function touches disjoint ECS
    // data, so serial is the safe default for a public game API.
    bool ParallelSafe = false;
    // A parallel system that touches neither a service nor an ECS component must
    // say so explicitly; Add() rejects an undeclared parallel system.
    bool NoEcsAccess = false;
};

// A parallel system must declare at least one access: a service bit, a
// component mask, or NoEcsAccess. Without that declaration the graph cannot
// tell whether it is safe to run next to another system.
[[nodiscard]] constexpr bool IsSystemAccessDeclared(const SystemAccess& InAccess) noexcept
{
    return InAccess.Reads != 0 || InAccess.Writes != 0 || InAccess.NoEcsAccess ||
           InAccess.ComponentReads != ComponentAccessMask{} || InAccess.ComponentWrites != ComponentAccessMask{};
}

using GraphSystemFn = void (*)(SystemContext&, void*);

struct GraphSystem
{
    const char* name = nullptr;
    Phase phase = Phase::PreUpdate;
    GraphSystemFn run = nullptr;
    void* user = nullptr;
    std::uint64_t after = 0;
    std::uint64_t before = 0;
    std::uint32_t order = 0;
    bool enabled = true;
    SystemAccess access{};
    // Filled by Run() when timing is enabled; mutable because Run is const and
    // the field is diagnostics, not scheduling state.
    mutable std::uint64_t LastNs = 0;
};

class SystemGraph
{
public:
    [[nodiscard]] bool Add(Phase InPhase, const char* InName, GraphSystemFn InRun, void* InUser = nullptr,
                           std::uint64_t InAfter = 0, std::uint64_t InBefore = 0, SystemAccess InAccess = {})
    {
        if (MCount >= kMaxGraphSystems || InRun == nullptr || InName == nullptr)
        {
            return false;
        }
        if (InAccess.ParallelSafe && !IsSystemAccessDeclared(InAccess))
        {
            return false;
        }
        MSystems[MCount] = GraphSystem{InName, InPhase, InRun, InUser, InAfter, InBefore, MCount, true, InAccess};
        ++MCount;
        MBuilt = false;
        return true;
    }

    template <auto Function>
        requires std::is_invocable_r_v<void, decltype(Function), SystemContext&>
    [[nodiscard]] bool Add(Phase InPhase, const char* InName, std::uint64_t InAfter = 0, std::uint64_t InBefore = 0,
                           SystemAccess InAccess = {})
    {
        return Add(
            InPhase, InName,
            +[](SystemContext& InContext, void*)
            {
                Function(InContext);
            },
            nullptr, InAfter, InBefore, InAccess);
    }

    // Resolves the dependency graph. Returns false on a cycle or a dependency
    [[nodiscard]] bool Build();

    // Removes every node whose `user` matches. Used by PluginHost on unload:
    bool RemoveUser(void* InUser);

    void Clear() noexcept
    {
        MCount = 0;
        MOrderCount = 0;
        MLevelCount = 0;
        MBuilt = false;
    }

    [[nodiscard]] bool IsBuilt() const noexcept
    {
        return MBuilt;
    }

    // Variant A: deterministic simulation uses one caller thread for every
    // non-render phase; presentation jobs outside the graph stay parallel.
    void SetDeterministicSimulation(bool InEnabled) noexcept
    {
        MDeterministicSimulation = InEnabled;
    }
    [[nodiscard]] bool DeterministicSimulation() const noexcept
    {
        return MDeterministicSimulation;
    }

    void SetTimingEnabled(bool InEnabled) noexcept
    {
        MTimingEnabled = InEnabled;
    }
    [[nodiscard]] bool TimingEnabled() const noexcept
    {
        return MTimingEnabled;
    }
    // A paused graph keeps rendering but stops every simulation and
    // presentation phase. Ui can be kept alive for an animated pause HUD.
    void SetPaused(bool InPaused, bool InRunUiWhilePaused = false) noexcept
    {
        MPaused = InPaused;
        MRunUiWhilePaused = InRunUiWhilePaused;
    }
    [[nodiscard]] bool Paused() const noexcept
    {
        return MPaused;
    }
    [[nodiscard]] bool RunsUiWhilePaused() const noexcept
    {
        return MRunUiWhilePaused;
    }
    [[nodiscard]] std::uint32_t LevelCount() const noexcept
    {
        return MLevelCount;
    }
    [[nodiscard]] std::uint32_t SystemLevel(std::uint32_t InIndex) const noexcept
    {
        return MLevels[InIndex];
    }

    // Runs one phase through the JobSystem. Nodes with no dependency edge
    void Run(Phase InPhase, SystemContext& InContext) const;

    [[nodiscard]] std::uint32_t Count() const noexcept
    {
        return MCount;
    }
    [[nodiscard]] const GraphSystem& At(std::uint32_t InIndex) const noexcept
    {
        return MSystems[InIndex];
    }

private:
    std::array<GraphSystem, kMaxGraphSystems> MSystems{};
    std::array<std::uint32_t, kMaxGraphSystems> MOrder{};
    // Dependency masks after `before` edges have been folded in, plus the
    std::array<std::uint64_t, kMaxGraphSystems> MDependencies{};
    std::array<std::uint32_t, kMaxGraphSystems> MLevels{};
    std::uint32_t MCount = 0;
    std::uint32_t MOrderCount = 0;
    std::uint32_t MLevelCount = 0;
    bool MBuilt = false;
    bool MDeterministicSimulation = false;
    bool MPaused = false;
    bool MRunUiWhilePaused = false;
    bool MTimingEnabled =
#if defined(FBY_ENABLE_SYSTEM_TIMING) && FBY_ENABLE_SYSTEM_TIMING
        true;
#else
        false;
#endif
};
}
