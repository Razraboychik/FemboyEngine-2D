// FemboyEngine2D - gameplay skill activation, casting and cooldowns.
#include "Engine/Gameplay/GameplaySkills.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace Fby
{
void GameplaySkillSystem::Initialize(const GameplayTagRegistry& InTags, const GameplayAttributeRegistry& InAttributes,
                                     GameplayEffectSystem& InEffects) noexcept
{
    MTags = &InTags;
    MAttributes = &InAttributes;
    MEffects = &InEffects;
}

bool GameplaySkillSystem::AddDef(const GameplaySkillDef& InDef) noexcept
{
    if (InDef.Id == 0 || MDefCount >= kMaxGameplaySkillDefs || FindDef(InDef.Id) != nullptr || InDef.Cooldown < 0.0f ||
        InDef.CastTime < 0.0f || InDef.CostAmount < 0.0f || InDef.SelfEffectCount > kMaxSkillEffectIds ||
        InDef.TargetEffectCount > kMaxSkillEffectIds)
    {
        return false;
    }
    MDefs[MDefCount++] = InDef;
    return true;
}

const GameplaySkillDef* GameplaySkillSystem::FindDef(std::uint32_t InId) const noexcept
{
    for (std::uint32_t ScopedI = 0; ScopedI < MDefCount; ++ScopedI)
    {
        if (MDefs[ScopedI].Id == InId)
        {
            return &MDefs[ScopedI];
        }
    }
    return nullptr;
}

void GameplaySkillSystem::PushEvent(Entity InCaster, Entity InTarget, std::uint32_t InSkillId, std::uint16_t InSlot,
                                    GameplaySkillEventType InType, GameplaySkillFailReason InReason) noexcept
{
    if (MEventCount >= kMaxGameplaySkillEvents)
    {
        return;
    }
    GameplaySkillEvent& ScopedEvent = MEvents[MEventCount++];
    ScopedEvent = GameplaySkillEvent{};
    ScopedEvent.Caster = InCaster;
    ScopedEvent.Target = InTarget;
    ScopedEvent.SkillId = InSkillId;
    ScopedEvent.Slot = InSlot;
    ScopedEvent.Type = InType;
    ScopedEvent.Reason = InReason;
}

bool GameplaySkillSystem::SetSlot(Registry& InRegistry, Entity InCaster, std::uint32_t InSlot,
                                  std::uint32_t InSkillId) noexcept
{
    if (InSlot >= kMaxGameplaySkillSlots || !InRegistry.valid(InCaster))
    {
        return false;
    }
    auto* ScopedSkills = InRegistry.try_get<GameplaySkillsComponent>(InCaster);
    if (ScopedSkills == nullptr)
    {
        ScopedSkills = &InRegistry.emplace<GameplaySkillsComponent>(InCaster);
    }
    GameplaySkillSlot& ScopedSlot = ScopedSkills->Slots[InSlot];
    ScopedSlot.SkillId = InSkillId;
    ScopedSlot.CooldownRemaining = 0.0f;
    ScopedSlot.CastRemaining = 0.0f;
    ScopedSlot.Target = Fby::kNullEntity;
    ScopedSlot.State = GameplaySkillState::Ready;
    ++ScopedSkills->Revision;
    return true;
}

bool GameplaySkillSystem::PassesTagChecks(Registry& InRegistry, Entity InCaster,
                                          const GameplaySkillDef& InDef) const noexcept
{
    if (MTags == nullptr)
    {
        return InDef.RequiredTags.IsEmpty() && InDef.BlockedTags.IsEmpty();
    }
    const auto* ScopedTags = InRegistry.try_get<GameplayTagsComponent>(InCaster);
    if (ScopedTags == nullptr)
    {
        return InDef.RequiredTags.IsEmpty();
    }
    if (!GameplayTagContainerHasAll(ScopedTags->Tags, *MTags, InDef.RequiredTags))
    {
        return false;
    }
    return !GameplayTagContainerHasAny(ScopedTags->Tags, *MTags, InDef.BlockedTags);
}

bool GameplaySkillSystem::PayCost(Registry& InRegistry, Entity InCaster, const GameplaySkillDef& InDef) noexcept
{
    if (!InDef.CostAttribute.IsValid() || InDef.CostAmount <= 0.0f)
    {
        return true;
    }
    auto* ScopedAttributes = InRegistry.try_get<GameplayAttributesComponent>(InCaster);
    if (ScopedAttributes == nullptr || InDef.CostAttribute.Id > kMaxGameplayAttributes)
    {
        return false;
    }
    const float ScopedCurrent = GameplayAttributeValue(*ScopedAttributes, InDef.CostAttribute);
    if (ScopedCurrent < InDef.CostAmount)
    {
        return false;
    }
    const float ScopedBase = ScopedAttributes->Values.Base[InDef.CostAttribute.Id - 1u];
    SetGameplayAttributeBase(*ScopedAttributes, InDef.CostAttribute, ScopedBase - InDef.CostAmount);
    if (MEffects != nullptr)
    {
        MEffects->RebuildEntity(InRegistry, InCaster);
    }
    return true;
}

void GameplaySkillSystem::CompleteCast(Registry& InRegistry, Entity InCaster, GameplaySkillSlot& InSlot,
                                       const GameplaySkillDef& InDef, std::uint16_t InSlotIndex) noexcept
{
    const Entity ScopedTarget = InDef.TargetKind == GameplaySkillTargetKind::Self ? InCaster : InSlot.Target;
    if (MEffects != nullptr)
    {
        for (std::uint32_t ScopedI = 0; ScopedI < InDef.SelfEffectCount; ++ScopedI)
        {
            (void)MEffects->Apply(InRegistry, InCaster, InDef.SelfEffectIds[ScopedI], InCaster);
        }
        if (ScopedTarget != Fby::kNullEntity && InRegistry.valid(ScopedTarget))
        {
            for (std::uint32_t ScopedI = 0; ScopedI < InDef.TargetEffectCount; ++ScopedI)
            {
                (void)MEffects->Apply(InRegistry, ScopedTarget, InDef.TargetEffectIds[ScopedI], InCaster);
            }
        }
    }
    // Effects add components to the caster and move its archetype, so the slot
    // reference captured by the caller is stale after Apply(). Re-fetch it by
    // index before writing the completed state.
    if (GameplaySkillsComponent* ScopedSkills = InRegistry.try_get<GameplaySkillsComponent>(InCaster);
        ScopedSkills != nullptr)
    {
        GameplaySkillSlot& ScopedCompleted = ScopedSkills->Slots[InSlotIndex];
        ScopedCompleted.State = GameplaySkillState::Ready;
        ScopedCompleted.CastRemaining = 0.0f;
        ScopedCompleted.Target = Fby::kNullEntity;
    }
    (void)InSlot;
    PushEvent(InCaster, ScopedTarget, InDef.Id, InSlotIndex, GameplaySkillEventType::Completed,
              GameplaySkillFailReason::None);
}

bool GameplaySkillSystem::TryActivate(Registry& InRegistry, Entity InCaster, std::uint32_t InSlot,
                                      Entity InTarget) noexcept
{
    if (MTags == nullptr || MAttributes == nullptr || MEffects == nullptr || !InRegistry.valid(InCaster) ||
        InSlot >= kMaxGameplaySkillSlots)
    {
        return false;
    }
    auto* ScopedSkills = InRegistry.try_get<GameplaySkillsComponent>(InCaster);
    if (ScopedSkills == nullptr)
    {
        ScopedSkills = &InRegistry.emplace<GameplaySkillsComponent>(InCaster);
    }
    GameplaySkillSlot& ScopedSlot = ScopedSkills->Slots[InSlot];
    if (ScopedSlot.SkillId == 0)
    {
        PushEvent(InCaster, InTarget, 0, static_cast<std::uint16_t>(InSlot), GameplaySkillEventType::Failed,
                  GameplaySkillFailReason::NoSlot);
        return false;
    }
    const GameplaySkillDef* ScopedDef = FindDef(ScopedSlot.SkillId);
    if (ScopedDef == nullptr)
    {
        PushEvent(InCaster, InTarget, ScopedSlot.SkillId, static_cast<std::uint16_t>(InSlot),
                  GameplaySkillEventType::Failed, GameplaySkillFailReason::NoDefinition);
        return false;
    }
    if (ScopedSlot.State == GameplaySkillState::Casting)
    {
        PushEvent(InCaster, InTarget, ScopedDef->Id, static_cast<std::uint16_t>(InSlot), GameplaySkillEventType::Failed,
                  GameplaySkillFailReason::Casting);
        return false;
    }
    if (ScopedSlot.CooldownRemaining > 0.0f)
    {
        PushEvent(InCaster, InTarget, ScopedDef->Id, static_cast<std::uint16_t>(InSlot), GameplaySkillEventType::Failed,
                  GameplaySkillFailReason::Cooldown);
        return false;
    }
    if (!PassesTagChecks(InRegistry, InCaster, *ScopedDef))
    {
        PushEvent(InCaster, InTarget, ScopedDef->Id, static_cast<std::uint16_t>(InSlot), GameplaySkillEventType::Failed,
                  GameplaySkillFailReason::Tags);
        return false;
    }
    if (ScopedDef->TargetKind == GameplaySkillTargetKind::TargetEntity &&
        (InTarget == Fby::kNullEntity || !InRegistry.valid(InTarget)))
    {
        PushEvent(InCaster, InTarget, ScopedDef->Id, static_cast<std::uint16_t>(InSlot), GameplaySkillEventType::Failed,
                  GameplaySkillFailReason::Target);
        return false;
    }
    if (!PayCost(InRegistry, InCaster, *ScopedDef))
    {
        PushEvent(InCaster, InTarget, ScopedDef->Id, static_cast<std::uint16_t>(InSlot), GameplaySkillEventType::Failed,
                  GameplaySkillFailReason::Cost);
        return false;
    }
    // PayCost may rebuild effects and move the caster; re-fetch the component
    // before writing the cooldown and cast state.
    ScopedSkills = InRegistry.try_get<GameplaySkillsComponent>(InCaster);
    if (ScopedSkills == nullptr)
    {
        return false;
    }
    GameplaySkillSlot& ScopedSlotAfter = ScopedSkills->Slots[InSlot];
    ScopedSlotAfter.CooldownRemaining = ScopedDef->Cooldown;
    ScopedSlotAfter.Target =
        ScopedDef->TargetKind == GameplaySkillTargetKind::Self ? InCaster : InTarget;
    ++ScopedSkills->Revision;
    PushEvent(InCaster, InTarget, ScopedDef->Id, static_cast<std::uint16_t>(InSlot), GameplaySkillEventType::Activated,
              GameplaySkillFailReason::None);
    if (ScopedDef->CastTime > 0.0f)
    {
        ScopedSlotAfter.State = GameplaySkillState::Casting;
        ScopedSlotAfter.CastRemaining = ScopedDef->CastTime;
        return true;
    }
    CompleteCast(InRegistry, InCaster, ScopedSlotAfter, *ScopedDef, static_cast<std::uint16_t>(InSlot));
    return true;
}

bool GameplaySkillSystem::Cancel(Registry& InRegistry, Entity InCaster, std::uint32_t InSlot) noexcept
{
    auto* ScopedSkills = InRegistry.try_get<GameplaySkillsComponent>(InCaster);
    if (ScopedSkills == nullptr || InSlot >= kMaxGameplaySkillSlots)
    {
        return false;
    }
    GameplaySkillSlot& ScopedSlot = ScopedSkills->Slots[InSlot];
    if (ScopedSlot.State != GameplaySkillState::Casting)
    {
        return false;
    }
    PushEvent(InCaster, ScopedSlot.Target, ScopedSlot.SkillId, static_cast<std::uint16_t>(InSlot),
              GameplaySkillEventType::Cancelled, GameplaySkillFailReason::None);
    ScopedSlot.State = GameplaySkillState::Ready;
    ScopedSlot.CastRemaining = 0.0f;
    ScopedSlot.Target = Fby::kNullEntity;
    ++ScopedSkills->Revision;
    return true;
}

void GameplaySkillSystem::Update(Registry& InRegistry, float InDt) noexcept
{
    if (InDt <= 0.0f)
    {
        return;
    }
    // CompleteCast applies effects, which may add components and move the
    // caster between archetypes. Collect the casters first and re-fetch the
    // component after every potentially structural call.
    std::vector<Entity> ScopedCasters;
    for (const Entity ScopedCaster : InRegistry.view<GameplaySkillsComponent>())
    {
        ScopedCasters.push_back(ScopedCaster);
    }
    for (const Entity ScopedCaster : ScopedCasters)
    {
        bool ScopedChanged = false;
        for (std::uint32_t ScopedI = 0; ScopedI < kMaxGameplaySkillSlots; ++ScopedI)
        {
            GameplaySkillsComponent* ScopedSkills = InRegistry.try_get<GameplaySkillsComponent>(ScopedCaster);
            if (ScopedSkills == nullptr)
            {
                break;
            }
            GameplaySkillSlot& ScopedSlot = ScopedSkills->Slots[ScopedI];
            if (ScopedSlot.CooldownRemaining > 0.0f)
            {
                ScopedSlot.CooldownRemaining = std::max(ScopedSlot.CooldownRemaining - InDt, 0.0f);
                // A running cooldown is continuous telemetry; only becoming
                // ready changes the observable skill state.
                if (ScopedSlot.CooldownRemaining <= 0.0f)
                {
                    ScopedChanged = true;
                }
            }
            if (ScopedSlot.State != GameplaySkillState::Casting)
            {
                continue;
            }
            const GameplaySkillDef* ScopedDef = FindDef(ScopedSlot.SkillId);
            if (ScopedDef == nullptr || !PassesTagChecks(InRegistry, ScopedCaster, *ScopedDef))
            {
                PushEvent(ScopedCaster, ScopedSlot.Target, ScopedSlot.SkillId, static_cast<std::uint16_t>(ScopedI),
                          GameplaySkillEventType::Cancelled, GameplaySkillFailReason::Tags);
                ScopedSlot.State = GameplaySkillState::Ready;
                ScopedSlot.CastRemaining = 0.0f;
                ScopedSlot.Target = Fby::kNullEntity;
                ScopedChanged = true;
                continue;
            }
            ScopedSlot.CastRemaining -= InDt;
            if (ScopedSlot.CastRemaining <= 0.0f)
            {
                CompleteCast(InRegistry, ScopedCaster, ScopedSlot, *ScopedDef, static_cast<std::uint16_t>(ScopedI));
                ScopedChanged = true;
            }
        }
        if (ScopedChanged)
        {
            if (GameplaySkillsComponent* ScopedSkills = InRegistry.try_get<GameplaySkillsComponent>(ScopedCaster);
                ScopedSkills != nullptr)
            {
                ++ScopedSkills->Revision;
            }
        }
    }
}
}
