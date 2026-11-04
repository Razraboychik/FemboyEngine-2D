// FemboyEngine2D - AI command consumer: StateTree intent to navigation/skills.
#pragma once

#include "Engine/ECS/Schedule.hpp"
#include "Engine/ECS/StateTree.hpp"
#include "Engine/Gameplay/GameplaySkills.hpp"
#include "Engine/Navigation/Navigation.hpp"

#include <cstdint>
#include <type_traits>

namespace Fby::AI
{

// Per-entity tuning that does not belong to a StateTree asset.
struct AIAgentSettings
{
    // Seconds before a repeated ActivateSkill command is tried again; zero
    // means once per tree leaf.
    float SkillRetryInterval = 0.0f;
    std::uint32_t Flags = 0;
    std::uint32_t Reserved = 0;
};

static_assert(std::is_trivially_copyable_v<AIAgentSettings>);
static_assert(std::is_standard_layout_v<AIAgentSettings>);
static_assert(sizeof(AIAgentSettings) == 12);

// Runtime bookkeeping, created by the controller. Kept separate from authored
// settings so a scene save never persists command history.
struct AIControllerComponent
{
    std::uint32_t LastSequence = 0;
    std::uint32_t LastTaskIndex = 0xFFFFFFFFu;
    std::uint32_t LastKind = 0;
    std::uint32_t LastSkillSlot = 0;
    Entity Target = entt::null;
    float SkillRetryTimer = 0.0f;
    std::uint32_t Flags = 0;
};

static_assert(std::is_trivially_copyable_v<AIControllerComponent>);
static_assert(std::is_standard_layout_v<AIControllerComponent>);
static_assert(sizeof(AIControllerComponent) == 28);

// Perception is intentionally tag-driven: a project names the tag that marks a
// valid target (Team.Player, Faction.Enemy, ...) instead of hardcoding a team.
struct AIPerceptionSettings
{
    float Radius = 256.0f;
    float Interval = 0.25f;
    // Zero accepts every entity with a Transform2D and a tag component.
    std::uint32_t TargetTagId = 0;
    std::uint32_t Flags = 0;
};

static_assert(std::is_trivially_copyable_v<AIPerceptionSettings>);

struct AIPerceptionComponent
{
    Entity Target = entt::null;
    float Timer = 0.0f;
};

static_assert(std::is_trivially_copyable_v<AIPerceptionComponent>);

class AIPerceptionSystem
{
public:
    using Registry = Ecs::Registry;

    // FixedUpdate, before StateTree. Writes StateTreeComponent::Target only; a
    // tree decides what to do with the perceived entity.
    void Update(Registry& InRegistry, const GameplayTagRegistry* InTags, float InDeltaSeconds) noexcept;
    void Update(Ecs::SystemContext& InContext) noexcept;

    [[nodiscard]] static bool Register(Ecs::SystemGraph& InGraph, AIPerceptionSystem& InSystem);
};

// Consumes AICommandComponent and writes NavAgentSettings plus skill
// activations. It never moves an entity directly, never applies damage and has
// no combat queue: a game without AI attack simply does not use combat skills,
// while a game with combat observes its own effects/events.
class AIControllerSystem
{
public:
    using Registry = Ecs::Registry;

    void SetSkillSystem(GameplaySkillSystem* InSkills) noexcept
    {
        MSkills = InSkills;
    }
    [[nodiscard]] GameplaySkillSystem* SkillSystem() const noexcept
    {
        return MSkills;
    }

    // FixedUpdate, after StateTree and before NavigationSystem.
    void Update(Registry& InRegistry, float InDeltaSeconds) noexcept;
    void Update(Ecs::SystemContext& InContext) noexcept;

    [[nodiscard]] static bool Register(Ecs::SystemGraph& InGraph, AIControllerSystem& InSystem);

private:
    GameplaySkillSystem* MSkills = nullptr;
};
}

FBY_REFLECT_COMPONENT(Fby::AI::AIAgentSettings, "AIAgentSettings",
                      FBY_FIELD(Fby::AI::AIAgentSettings, SkillRetryInterval, Float),
                      FBY_FIELD(Fby::AI::AIAgentSettings, Flags, UInt32))

FBY_REFLECT_COMPONENT(Fby::AI::AIPerceptionSettings, "AIPerceptionSettings",
                      FBY_FIELD(Fby::AI::AIPerceptionSettings, Radius, Float),
                      FBY_FIELD(Fby::AI::AIPerceptionSettings, Interval, Float),
                      FBY_FIELD(Fby::AI::AIPerceptionSettings, TargetTagId, UInt32),
                      FBY_FIELD(Fby::AI::AIPerceptionSettings, Flags, UInt32))

FBY_REFLECT_COMPONENT(Fby::AI::AIPerceptionComponent, "AIPerceptionComponent",
                      FBY_FIELD(Fby::AI::AIPerceptionComponent, Target, Entity),
                      FBY_FIELD(Fby::AI::AIPerceptionComponent, Timer, Float))

FBY_REFLECT_COMPONENT(Fby::AI::AIControllerComponent, "AIControllerComponent",
                      FBY_FIELD(Fby::AI::AIControllerComponent, LastSequence, UInt32),
                      FBY_FIELD(Fby::AI::AIControllerComponent, LastTaskIndex, UInt32),
                      FBY_FIELD(Fby::AI::AIControllerComponent, LastKind, UInt32),
                      FBY_FIELD(Fby::AI::AIControllerComponent, LastSkillSlot, UInt32),
                      FBY_FIELD(Fby::AI::AIControllerComponent, Target, Entity),
                      FBY_FIELD(Fby::AI::AIControllerComponent, SkillRetryTimer, Float),
                      FBY_FIELD(Fby::AI::AIControllerComponent, Flags, UInt32))
