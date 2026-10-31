// FemboyEngine2D - declarative system graph.
#pragma once

#include "Engine/ECS/Schedule.hpp"

#include <array>
#include <cstdint>
#include <type_traits>

namespace Fby::Ecs
{

inline constexpr std::uint32_t kMaxGraphSystems = 64;

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
};

class SystemGraph
{
public:
    [[nodiscard]] bool Add(Phase InPhase, const char* InName, GraphSystemFn InRun, void* InUser = nullptr,
                           std::uint64_t InAfter = 0, std::uint64_t InBefore = 0)
    {
        if (MCount >= kMaxGraphSystems || InRun == nullptr || InName == nullptr)
        {
            return false;
        }
        MSystems[MCount] = GraphSystem{InName, InPhase, InRun, InUser, InAfter, InBefore, MCount, true};
        ++MCount;
        MBuilt = false;
        return true;
    }

    template <auto Function>
        requires std::is_invocable_r_v<void, decltype(Function), SystemContext&>
    [[nodiscard]] bool Add(Phase InPhase, const char* InName, std::uint64_t InAfter = 0, std::uint64_t InBefore = 0)
    {
        return Add(
            InPhase, InName, +[](SystemContext& InContext, void*) { Function(InContext); }, nullptr, InAfter, InBefore);
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
};
}
