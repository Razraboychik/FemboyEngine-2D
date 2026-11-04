// FemboyEngine2D - data-driven gameplay effects: duration, periodic and stacks.
#pragma once

#include "Engine/ECS/Schedule.hpp"
#include "Engine/Gameplay/GameplayAttributes.hpp"
#include "Engine/Gameplay/GameplayTags.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <type_traits>

namespace Fby
{

inline constexpr std::uint32_t kMaxGameplayEffectDefs = 64;
inline constexpr std::uint32_t kMaxActiveGameplayEffects = 8;
inline constexpr std::uint32_t kMaxEffectModifiers = 4;
inline constexpr std::uint32_t kMaxEffectGrantedTags = 4;
inline constexpr std::uint32_t kMaxGameplayEffectEvents = 128;

enum class GameplayEffectStackPolicy : std::uint8_t
{
    // Re-application resets the remaining duration.
    Refresh = 0,
    // Re-application adds a stack up to MaxStacks and resets the duration.
    Stack = 1,
    // Every application owns a separate active record.
    Independent = 2,
};

enum GameplayEffectFlags : std::uint32_t
{
    GameplayEffectNone = 0,
    // Apply the modifiers once to Base and never keep an active record.
    GameplayEffectInstant = 1u << 0,
    // While active, apply the modifiers instantly once per Period.
    GameplayEffectPeriodic = 1u << 1,
    // The effect is invisible to gameplay queries and cues.
    GameplayEffectHidden = 1u << 2,
};

// Authored definition. The system owns the fixed table; entities only store the
// id, source and runtime clocks.
struct GameplayEffectDef
{
    std::uint32_t Id = 0;
    std::uint32_t Flags = GameplayEffectNone;
    // Seconds; zero means an infinite duration for a non-instant effect.
    float Duration = 0.0f;
    // Seconds between periodic applications; ignored unless Periodic is set.
    float Period = 0.0f;
    std::uint32_t MaxStacks = 1;
    GameplayEffectStackPolicy StackPolicy = GameplayEffectStackPolicy::Refresh;
    std::array<GameplayAttributeModifier, kMaxEffectModifiers> Modifiers{};
    std::uint32_t ModifierCount = 0;
    // Tags granted to the target while the effect is active.
    std::array<GameplayTag, kMaxEffectGrantedTags> GrantedTags{};
    std::uint32_t GrantedTagCount = 0;
};

static_assert(std::is_trivially_copyable_v<GameplayEffectDef>);

// Runtime record. Fields are packed to 20 bytes so the component has no
// padding and can be snapshotted byte for byte.
struct ActiveGameplayEffect
{
    std::uint32_t DefId = 0;
    Entity Source = entt::null;
    float Remaining = 0.0f;
    float NextTick = 0.0f;
    std::uint16_t Stacks = 1;
    std::uint16_t Flags = 0;
};

static_assert(sizeof(ActiveGameplayEffect) == 20);

struct GameplayEffectsComponent
{
    std::array<ActiveGameplayEffect, kMaxActiveGameplayEffects> Active{};
    std::uint32_t Count = 0;
    std::uint32_t Revision = 0;
};

static_assert(std::is_trivially_copyable_v<GameplayEffectsComponent>);
static_assert(std::is_standard_layout_v<GameplayEffectsComponent>);

enum class GameplayEffectEventType : std::uint8_t
{
    Applied = 0,
    Removed = 1,
    Periodic = 2,
};

struct GameplayEffectEvent
{
    Entity Target = entt::null;
    Entity Source = entt::null;
    std::uint32_t DefId = 0;
    std::uint16_t Stacks = 1;
    GameplayEffectEventType Type = GameplayEffectEventType::Applied;
    std::uint8_t Reserved = 0;
};

static_assert(sizeof(GameplayEffectEvent) == 16);

class GameplayEffectSystem
{
public:
    using Registry = Ecs::Registry;

    // Binds the registries used to rebuild tags and attributes. Without it the
    // system still tracks effects but cannot resolve project-owned ids.
    void Initialize(const GameplayTagRegistry& InTags, const GameplayAttributeRegistry& InAttributes) noexcept;

    [[nodiscard]] bool AddDef(const GameplayEffectDef& InDef) noexcept;
    [[nodiscard]] const GameplayEffectDef* FindDef(std::uint32_t InId) const noexcept;
    [[nodiscard]] std::uint32_t DefCount() const noexcept
    {
        return MDefCount;
    }

    // Applies one effect. Instant defs mutate Base and disappear; duration defs
    // create or stack a runtime record.
    [[nodiscard]] bool Apply(Registry& InRegistry, Entity InTarget, std::uint32_t InDefId, Entity InSource) noexcept;
    // Removes every record of a definition from the target.
    [[nodiscard]] bool Remove(Registry& InRegistry, Entity InTarget, std::uint32_t InDefId) noexcept;
    void RemoveAll(Registry& InRegistry, Entity InTarget) noexcept;

    // Advances timers, periodic applications and expiry. Allocation-free.
    void Update(Registry& InRegistry, float InDt) noexcept;

    // Recomputes Tags and Current attributes from Base and the active effects.
    // Skills call it after paying a cost or changing a base value.
    void RebuildEntity(Registry& InRegistry, Entity InEntity) noexcept;

    void ClearEvents() noexcept
    {
        MEventCount = 0;
    }
    [[nodiscard]] std::span<const GameplayEffectEvent> Events() const noexcept
    {
        return {MEvents.data(), MEventCount};
    }

private:
    void PushEvent(Entity InTarget, Entity InSource, std::uint32_t InDefId, std::uint16_t InStacks,
                   GameplayEffectEventType InType) noexcept;
    void Compact(Registry& InRegistry, Entity InEntity, GameplayEffectsComponent& InEffects) noexcept;

    const GameplayTagRegistry* MTags = nullptr;
    const GameplayAttributeRegistry* MAttributes = nullptr;
    std::array<GameplayEffectDef, kMaxGameplayEffectDefs> MDefs{};
    std::uint32_t MDefCount = 0;
    std::array<GameplayEffectEvent, kMaxGameplayEffectEvents> MEvents{};
    std::uint32_t MEventCount = 0;
};
}

FBY_REFLECT_COMPONENT(Fby::GameplayEffectsComponent, "GameplayEffectsComponent",
                      FBY_FIELD(Fby::GameplayEffectsComponent, Count, UInt32),
                      FBY_FIELD(Fby::GameplayEffectsComponent, Revision, UInt32))

namespace Fby::Reflect
{
template <> inline constexpr bool kRawCommandPayload<Fby::GameplayEffectsComponent> = true;
}
