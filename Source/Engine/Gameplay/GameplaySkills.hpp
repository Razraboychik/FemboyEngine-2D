// FemboyEngine2D - data-driven gameplay skills: costs, casts and cooldowns.
#pragma once

#include "Engine/Gameplay/GameplayEffects.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <type_traits>

namespace Fby
{

inline constexpr std::uint32_t kMaxGameplaySkillDefs = 64;
inline constexpr std::uint32_t kMaxGameplaySkillSlots = 4;
inline constexpr std::uint32_t kMaxSkillEffectIds = 3;
inline constexpr std::uint32_t kMaxGameplaySkillEvents = 128;

enum class GameplaySkillTargetKind : std::uint8_t
{
    Self = 0,
    TargetEntity = 1,
    Point = 2,
    Direction = 3,
};

enum class GameplaySkillState : std::uint8_t
{
    Ready = 0,
    Casting = 1,
};

enum class GameplaySkillEventType : std::uint8_t
{
    Activated = 0,
    Completed = 1,
    Failed = 2,
    Cancelled = 3,
};

enum class GameplaySkillFailReason : std::uint8_t
{
    None = 0,
    NoSlot,
    NoDefinition,
    Casting,
    Cooldown,
    Tags,
    Cost,
    Target,
};

// Authored definition. The system owns the fixed table; a slot stores only the
// id and its runtime clocks.
struct GameplaySkillDef
{
    std::uint32_t Id = 0;
    float Cooldown = 0.0f;
    float CastTime = 0.0f;
    GameplayAttribute CostAttribute{};
    float CostAmount = 0.0f;
    GameplayTagContainer RequiredTags{};
    GameplayTagContainer BlockedTags{};
    std::array<std::uint32_t, kMaxSkillEffectIds> SelfEffectIds{};
    std::uint32_t SelfEffectCount = 0;
    std::array<std::uint32_t, kMaxSkillEffectIds> TargetEffectIds{};
    std::uint32_t TargetEffectCount = 0;
    GameplaySkillTargetKind TargetKind = GameplaySkillTargetKind::Self;
    // Optional tag the animation/gameplay-cue layer listens for.
    GameplayTag AnimationTag{};
    std::uint32_t CueId = 0;
};

static_assert(std::is_trivially_copyable_v<GameplaySkillDef>);

// Runtime slot. 20 bytes, no padding inside, so the component snapshots cleanly.
struct GameplaySkillSlot
{
    std::uint32_t SkillId = 0;
    float CooldownRemaining = 0.0f;
    float CastRemaining = 0.0f;
    Entity Target = Fby::kNullEntity;
    GameplaySkillState State = GameplaySkillState::Ready;
    std::uint8_t Reserved[3] = {};
};

static_assert(sizeof(GameplaySkillSlot) == 20);

struct GameplaySkillsComponent
{
    std::array<GameplaySkillSlot, kMaxGameplaySkillSlots> Slots{};
    std::uint32_t Revision = 0;
};

static_assert(std::is_trivially_copyable_v<GameplaySkillsComponent>);
static_assert(std::is_standard_layout_v<GameplaySkillsComponent>);

struct GameplaySkillEvent
{
    Entity Caster = Fby::kNullEntity;
    Entity Target = Fby::kNullEntity;
    std::uint32_t SkillId = 0;
    std::uint16_t Slot = 0;
    GameplaySkillEventType Type = GameplaySkillEventType::Activated;
    GameplaySkillFailReason Reason = GameplaySkillFailReason::None;
};

static_assert(sizeof(GameplaySkillEvent) == 16);

class GameplaySkillSystem
{
public:
    using Registry = Ecs::Registry;

    void Initialize(const GameplayTagRegistry& InTags, const GameplayAttributeRegistry& InAttributes,
                    GameplayEffectSystem& InEffects) noexcept;

    [[nodiscard]] bool AddDef(const GameplaySkillDef& InDef) noexcept;
    [[nodiscard]] const GameplaySkillDef* FindDef(std::uint32_t InId) const noexcept;
    [[nodiscard]] std::uint32_t DefCount() const noexcept
    {
        return MDefCount;
    }

    // Binds a skill id to a slot. Zero clears the slot.
    [[nodiscard]] bool SetSlot(Registry& InRegistry, Entity InCaster, std::uint32_t InSlot,
                               std::uint32_t InSkillId) noexcept;

    // Validates tags, cost and cooldown. With CastTime > 0 the cast starts and
    // Update completes it; otherwise the effects are applied immediately.
    [[nodiscard]] bool TryActivate(Registry& InRegistry, Entity InCaster, std::uint32_t InSlot,
                                   Entity InTarget) noexcept;
    [[nodiscard]] bool Cancel(Registry& InRegistry, Entity InCaster, std::uint32_t InSlot) noexcept;

    void Update(Registry& InRegistry, float InDt) noexcept;

    void ClearEvents() noexcept
    {
        MEventCount = 0;
    }
    [[nodiscard]] std::span<const GameplaySkillEvent> Events() const noexcept
    {
        return {MEvents.data(), MEventCount};
    }

private:
    [[nodiscard]] bool PassesTagChecks(Registry& InRegistry, Entity InCaster,
                                       const GameplaySkillDef& InDef) const noexcept;
    [[nodiscard]] bool PayCost(Registry& InRegistry, Entity InCaster, const GameplaySkillDef& InDef) noexcept;
    void CompleteCast(Registry& InRegistry, Entity InCaster, GameplaySkillSlot& InSlot, const GameplaySkillDef& InDef,
                      std::uint16_t InSlotIndex) noexcept;
    void PushEvent(Entity InCaster, Entity InTarget, std::uint32_t InSkillId, std::uint16_t InSlot,
                   GameplaySkillEventType InType, GameplaySkillFailReason InReason) noexcept;

    const GameplayTagRegistry* MTags = nullptr;
    const GameplayAttributeRegistry* MAttributes = nullptr;
    GameplayEffectSystem* MEffects = nullptr;
    std::array<GameplaySkillDef, kMaxGameplaySkillDefs> MDefs{};
    std::uint32_t MDefCount = 0;
    std::array<GameplaySkillEvent, kMaxGameplaySkillEvents> MEvents{};
    std::uint32_t MEventCount = 0;
};
}

FBY_REFLECT_COMPONENT(Fby::GameplaySkillsComponent, "GameplaySkillsComponent",
                      FBY_FIELD(Fby::GameplaySkillsComponent, Revision, UInt32))

namespace Fby::Reflect
{
template <> inline constexpr bool kRawCommandPayload<Fby::GameplaySkillsComponent> = true;
}
