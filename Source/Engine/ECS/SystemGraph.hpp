// FemboyEngine2D - declarative system graph.
#pragma once

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

// What a system reads and writes outside the registry. The bits are Service
// constants; a zero declaration keeps the previous behavior.
struct SystemAccess
{
    std::uint32_t Reads = 0;
    std::uint32_t Writes = 0;
};

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
            InPhase, InName, +[](SystemContext& InContext, void*) { Function(InContext); }, nullptr, InAfter, InBefore,
            InAccess);
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
};
}
