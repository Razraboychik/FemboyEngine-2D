// FemboyEngine2D - frame phases and the legacy flat schedule.
#pragma once

#include "Engine/ECS/SystemContext.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

namespace Fby::Ecs
{

class SystemGraph;

enum class Phase : std::uint8_t
{
    PreUpdate = 0,
    FixedUpdate,
    PostUpdate,
    // HUD/UI animation can keep running while the simulation graph is paused.
    Ui,
    Render,
    Count
};

[[nodiscard]] constexpr const char* PhaseName(Phase InPhase) noexcept
{
    switch (InPhase)
    {
        case Phase::PreUpdate:
            return "PreUpdate";
        case Phase::FixedUpdate:
            return "FixedUpdate";
        case Phase::PostUpdate:
            return "PostUpdate";
        case Phase::Ui:
            return "Ui";
        case Phase::Render:
            return "Render";
        case Phase::Count:
            break;
    }
    return "?";
}

class Schedule
{
public:
    static constexpr std::uint32_t kMaxSystems = 128;

    struct Entry
    {
        const char* name = nullptr;
        Phase phase = Phase::PreUpdate;
        SystemFunction run = nullptr;
        void* user = nullptr;
    };

    [[nodiscard]] bool Add(Phase InPhase, const char* InName, SystemFunction InRun, void* InUser = nullptr);
    template <auto Function>
        requires std::is_invocable_r_v<void, decltype(Function), SystemContext&>
    [[nodiscard]] bool Add(Phase InPhase, const char* InName)
    {
        return Add(
            InPhase, InName,
            +[](SystemContext& InContext, void*)
            {
                Function(InContext);
            },
            nullptr);
    }
    void Clear() noexcept;
    void Run(Phase InPhase, SystemContext& InContext) const;
    [[nodiscard]] std::size_t Count(Phase InPhase) const noexcept;
    [[nodiscard]] std::span<const Entry> Systems() const noexcept
    {
        return {MSystems.data(), MCount};
    }

private:
    std::array<Entry, kMaxSystems> MSystems{};
    std::uint32_t MCount = 0;
};
}
