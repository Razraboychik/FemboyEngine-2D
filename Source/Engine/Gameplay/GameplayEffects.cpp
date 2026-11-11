// FemboyEngine2D - gameplay effect runtime and attribute/tag aggregation.
#include "Engine/Gameplay/GameplayEffects.hpp"

#include "Engine/Core/SimMath.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace Fby
{
namespace
{
[[nodiscard]] float EffectiveMagnitude(const GameplayAttributeModifier& InModifier, std::uint16_t InStacks) noexcept
{
    const float ScopedStacks = static_cast<float>(InStacks == 0 ? 1u : InStacks);
    switch (InModifier.Op)
    {
        case GameplayModifierOp::Add:
            return InModifier.Magnitude * ScopedStacks;
        case GameplayModifierOp::Multiply:
            return Sim::Pow(InModifier.Magnitude, ScopedStacks);
        case GameplayModifierOp::Override:
            return InModifier.Magnitude;
    }
    return InModifier.Magnitude;
}
}

void GameplayEffectSystem::Initialize(const GameplayTagRegistry& InTags,
                                      const GameplayAttributeRegistry& InAttributes) noexcept
{
    MTags = &InTags;
    MAttributes = &InAttributes;
}

bool GameplayEffectSystem::AddDef(const GameplayEffectDef& InDef) noexcept
{
    if (InDef.Id == 0 || MDefCount >= kMaxGameplayEffectDefs || FindDef(InDef.Id) != nullptr ||
        InDef.ModifierCount > kMaxEffectModifiers || InDef.GrantedTagCount > kMaxEffectGrantedTags ||
        InDef.Duration < 0.0f || InDef.Period < 0.0f || InDef.MaxStacks == 0 ||
        (InDef.Flags & (GameplayEffectInstant | GameplayEffectPeriodic)) ==
            (GameplayEffectInstant | GameplayEffectPeriodic))
    {
        return false;
    }
    const std::uint32_t ScopedSlot = MDefCount++;
    MDefs[ScopedSlot] = InDef;
    std::uint32_t ScopedCursor = (InDef.Id * 2654435761u) & (static_cast<std::uint32_t>(MDefIndex.size()) - 1u);
    while (MDefIndex[ScopedCursor].Id != 0)
    {
        ScopedCursor = (ScopedCursor + 1u) & (static_cast<std::uint32_t>(MDefIndex.size()) - 1u);
    }
    MDefIndex[ScopedCursor] = DefIndexEntry{InDef.Id, ScopedSlot};
    return true;
}

const GameplayEffectDef* GameplayEffectSystem::FindDef(std::uint32_t InId) const noexcept
{
    if (InId == 0)
    {
        return nullptr;
    }
    std::uint32_t ScopedCursor = (InId * 2654435761u) & (static_cast<std::uint32_t>(MDefIndex.size()) - 1u);
    for (std::uint32_t ScopedProbe = 0; ScopedProbe < MDefIndex.size(); ++ScopedProbe)
    {
        const DefIndexEntry& ScopedEntry = MDefIndex[ScopedCursor];
        if (ScopedEntry.Id == InId)
        {
            return &MDefs[ScopedEntry.Slot];
        }
        if (ScopedEntry.Id == 0)
        {
            return nullptr;
        }
        ScopedCursor = (ScopedCursor + 1u) & (static_cast<std::uint32_t>(MDefIndex.size()) - 1u);
    }
    return nullptr;
}

void GameplayEffectSystem::PushEvent(Entity InTarget, Entity InSource, std::uint32_t InDefId, std::uint16_t InStacks,
                                     GameplayEffectEventType InType) noexcept
{
    if (MEventCount >= kMaxGameplayEffectEvents)
    {
        ++MDroppedEvents;
        return;
    }
    GameplayEffectEvent& ScopedEvent = MEvents[MEventCount++];
    ScopedEvent = GameplayEffectEvent{};
    ScopedEvent.Target = InTarget;
    ScopedEvent.Source = InSource;
    ScopedEvent.DefId = InDefId;
    ScopedEvent.Stacks = InStacks;
    ScopedEvent.Type = InType;
}

bool GameplayEffectSystem::Apply(Registry& InRegistry, Entity InTarget, std::uint32_t InDefId, Entity InSource) noexcept
{
    const GameplayEffectDef* ScopedDef = FindDef(InDefId);
    if (ScopedDef == nullptr || !InRegistry.valid(InTarget))
    {
        return false;
    }
    if ((ScopedDef->Flags & GameplayEffectInstant) != 0)
    {
        if (ScopedDef->ModifierCount > 0)
        {
            auto* ScopedAttributes = InRegistry.try_get<GameplayAttributesComponent>(InTarget);
            if (ScopedAttributes == nullptr)
            {
                ScopedAttributes = &InRegistry.emplace<GameplayAttributesComponent>(InTarget);
            }
            ApplyInstantAttributeModifiers(
                *ScopedAttributes,
                std::span<const GameplayAttributeModifier>(ScopedDef->Modifiers.data(), ScopedDef->ModifierCount), 1);
        }
        RebuildEntity(InRegistry, InTarget);
        PushEvent(InTarget, InSource, InDefId, 1, GameplayEffectEventType::Applied);
        return true;
    }

    auto* ScopedEffects = InRegistry.try_get<GameplayEffectsComponent>(InTarget);
    if (ScopedEffects == nullptr)
    {
        ScopedEffects = &InRegistry.emplace<GameplayEffectsComponent>(InTarget);
    }

    bool ScopedFound = false;
    std::uint16_t ScopedStacks = 1;
    if (ScopedDef->StackPolicy != GameplayEffectStackPolicy::Independent)
    {
        for (std::uint32_t ScopedI = 0; ScopedI < ScopedEffects->Count; ++ScopedI)
        {
            ActiveGameplayEffect& ScopedActive = ScopedEffects->Active[ScopedI];
            if (ScopedActive.DefId != InDefId)
            {
                continue;
            }
            if (ScopedDef->StackPolicy == GameplayEffectStackPolicy::Stack)
            {
                const std::uint16_t ScopedMax =
                    static_cast<std::uint16_t>(std::max<std::uint32_t>(ScopedDef->MaxStacks, 1u));
                ScopedActive.Stacks = static_cast<std::uint16_t>(
                    std::min<std::uint32_t>(static_cast<std::uint32_t>(ScopedActive.Stacks) + 1u, ScopedMax));
            }
            ScopedActive.Remaining = ScopedDef->Duration;
            ScopedActive.NextTick = ScopedDef->Period;
            ScopedActive.Flags = static_cast<std::uint16_t>(ScopedDef->Flags);
            ScopedStacks = ScopedActive.Stacks;
            ScopedFound = true;
            break;
        }
    }
    if (!ScopedFound)
    {
        if (ScopedEffects->Count >= kMaxActiveGameplayEffects)
        {
            ++MDroppedApplications;
            return false;
        }
        ActiveGameplayEffect& ScopedActive = ScopedEffects->Active[ScopedEffects->Count++];
        ScopedActive = ActiveGameplayEffect{};
        ScopedActive.DefId = InDefId;
        ScopedActive.Source = InSource;
        ScopedActive.Remaining = ScopedDef->Duration;
        ScopedActive.NextTick = ScopedDef->Period;
        ScopedActive.Stacks = 1;
        ScopedStacks = 1;
        ScopedActive.Flags = static_cast<std::uint16_t>(ScopedDef->Flags);
    }
    ++ScopedEffects->Revision;
    RebuildEntity(InRegistry, InTarget);
    PushEvent(InTarget, InSource, InDefId, ScopedStacks, GameplayEffectEventType::Applied);
    return true;
}

bool GameplayEffectSystem::Remove(Registry& InRegistry, Entity InTarget, std::uint32_t InDefId) noexcept
{
    auto* ScopedEffects = InRegistry.try_get<GameplayEffectsComponent>(InTarget);
    if (ScopedEffects == nullptr)
    {
        return false;
    }
    bool ScopedRemoved = false;
    std::uint32_t ScopedWrite = 0;
    for (std::uint32_t ScopedI = 0; ScopedI < ScopedEffects->Count; ++ScopedI)
    {
        const ActiveGameplayEffect& ScopedActive = ScopedEffects->Active[ScopedI];
        if (ScopedActive.DefId == InDefId)
        {
            PushEvent(InTarget, ScopedActive.Source, ScopedActive.DefId, ScopedActive.Stacks,
                      GameplayEffectEventType::Removed);
            ScopedRemoved = true;
            continue;
        }
        if (ScopedWrite != ScopedI)
        {
            ScopedEffects->Active[ScopedWrite] = ScopedActive;
        }
        ++ScopedWrite;
    }
    if (!ScopedRemoved)
    {
        return false;
    }
    ScopedEffects->Count = ScopedWrite;
    ++ScopedEffects->Revision;
    RebuildEntity(InRegistry, InTarget);
    return true;
}

void GameplayEffectSystem::RemoveAll(Registry& InRegistry, Entity InTarget) noexcept
{
    if (InRegistry.try_get<GameplayEffectsComponent>(InTarget) == nullptr)
    {
        return;
    }
    InRegistry.remove<GameplayEffectsComponent>(InTarget);
    RebuildEntity(InRegistry, InTarget);
}

void GameplayEffectSystem::Update(Registry& InRegistry, float InDt) noexcept
{
    if (InDt <= 0.0f)
    {
        return;
    }
    // Structural preprocessing first: components that RebuildEntity would add
    // must not be emplaced while the effect view is alive. Collect the entities,
    // create the components they will need, then process without structural
    // mutations.
    std::vector<Entity> ScopedEffectEntities;
    for (const Entity ScopedEntity : InRegistry.view<GameplayEffectsComponent>())
    {
        ScopedEffectEntities.push_back(ScopedEntity);
    }
    for (const Entity ScopedEntity : ScopedEffectEntities)
    {
        auto* ScopedEffects = InRegistry.try_get<GameplayEffectsComponent>(ScopedEntity);
        if (ScopedEffects == nullptr)
        {
            continue;
        }
        bool ScopedNeedsAttributes = false;
        bool ScopedNeedsTags = false;
        for (std::uint32_t ScopedI = 0; ScopedI < ScopedEffects->Count; ++ScopedI)
        {
            const GameplayEffectDef* ScopedDef = FindDef(ScopedEffects->Active[ScopedI].DefId);
            if (ScopedDef == nullptr)
            {
                continue;
            }
            ScopedNeedsAttributes = ScopedNeedsAttributes || ScopedDef->ModifierCount > 0;
            ScopedNeedsTags = ScopedNeedsTags || ScopedDef->GrantedTagCount > 0;
        }
        if (ScopedNeedsAttributes && InRegistry.try_get<GameplayAttributesComponent>(ScopedEntity) == nullptr)
        {
            (void)InRegistry.emplace<GameplayAttributesComponent>(ScopedEntity);
        }
        if (ScopedNeedsTags && InRegistry.try_get<GameplayTagsComponent>(ScopedEntity) == nullptr)
        {
            (void)InRegistry.emplace<GameplayTagsComponent>(ScopedEntity);
        }
    }

    std::vector<Entity> ScopedRebuildEntities;
    for (const Entity ScopedEntity : ScopedEffectEntities)
    {
        auto* ScopedEffectsPtr = InRegistry.try_get<GameplayEffectsComponent>(ScopedEntity);
        if (ScopedEffectsPtr == nullptr)
        {
            continue;
        }
        GameplayEffectsComponent& ScopedEffects = *ScopedEffectsPtr;
        bool ScopedChanged = false;
        std::uint32_t ScopedWrite = 0;
        for (std::uint32_t ScopedI = 0; ScopedI < ScopedEffects.Count; ++ScopedI)
        {
            ActiveGameplayEffect ScopedActive = ScopedEffects.Active[ScopedI];
            const GameplayEffectDef* ScopedDef = FindDef(ScopedActive.DefId);
            if (ScopedDef == nullptr)
            {
                ScopedChanged = true;
                continue;
            }
            bool ScopedExpired = false;
            if (ScopedDef->Duration > 0.0f)
            {
                ScopedActive.Remaining -= InDt;
                if (ScopedActive.Remaining <= 0.0f)
                {
                    ScopedExpired = true;
                }
            }
            // A tick that falls on the expiry step still fires; damage over time
            // must not lose its last application because the duration ended.
            if ((ScopedDef->Flags & GameplayEffectPeriodic) != 0 && ScopedDef->Period > 0.0f)
            {
                ScopedActive.NextTick -= InDt;
                std::uint32_t ScopedGuard = 0;
                while (ScopedActive.NextTick <= 0.0f && ScopedGuard < 32)
                {
                    if (ScopedDef->ModifierCount > 0)
                    {
                        auto* ScopedAttributes = InRegistry.try_get<GameplayAttributesComponent>(ScopedEntity);
                        if (ScopedAttributes == nullptr)
                        {
                            ScopedAttributes = &InRegistry.emplace<GameplayAttributesComponent>(ScopedEntity);
                        }
                        ApplyInstantAttributeModifiers(*ScopedAttributes,
                                                       std::span<const GameplayAttributeModifier>(
                                                           ScopedDef->Modifiers.data(), ScopedDef->ModifierCount),
                                                       ScopedActive.Stacks);
                    }
                    PushEvent(ScopedEntity, ScopedActive.Source, ScopedActive.DefId, ScopedActive.Stacks,
                              GameplayEffectEventType::Periodic);
                    ScopedActive.NextTick += ScopedDef->Period;
                    ++ScopedGuard;
                }
                ScopedChanged = true;
            }
            if (ScopedExpired)
            {
                PushEvent(ScopedEntity, ScopedActive.Source, ScopedActive.DefId, ScopedActive.Stacks,
                          GameplayEffectEventType::Removed);
                ScopedChanged = true;
                continue;
            }
            ScopedEffects.Active[ScopedWrite++] = ScopedActive;
        }
        if (ScopedWrite != ScopedEffects.Count)
        {
            ScopedChanged = true;
        }
        ScopedEffects.Count = ScopedWrite;
        if (ScopedChanged)
        {
            ++ScopedEffects.Revision;
            ScopedRebuildEntities.push_back(ScopedEntity);
        }
    }
    // Rebuilding can still add attributes/tags; do it after the effect view is
    // gone so the FCS archetype storage is not mutated mid-iteration.
    for (const Entity ScopedEntity : ScopedRebuildEntities)
    {
        RebuildEntity(InRegistry, ScopedEntity);
    }
}

void GameplayEffectSystem::RebuildEntity(Registry& InRegistry, Entity InEntity) noexcept
{
    auto* ScopedEffects = InRegistry.try_get<GameplayEffectsComponent>(InEntity);
    std::array<GameplayAttributeModifier, kMaxActiveGameplayEffects * kMaxEffectModifiers> ScopedModifiers{};
    std::uint32_t ScopedModifierCount = 0;
    GameplayTagContainer ScopedGranted;
    bool ScopedHasGranted = false;
    if (ScopedEffects != nullptr)
    {
        for (std::uint32_t ScopedI = 0; ScopedI < ScopedEffects->Count; ++ScopedI)
        {
            const ActiveGameplayEffect& ScopedActive = ScopedEffects->Active[ScopedI];
            const GameplayEffectDef* ScopedDef = FindDef(ScopedActive.DefId);
            if (ScopedDef == nullptr)
            {
                continue;
            }
            if ((ScopedDef->Flags & GameplayEffectPeriodic) == 0)
            {
                for (std::uint32_t ScopedM = 0; ScopedM < ScopedDef->ModifierCount; ++ScopedM)
                {
                    if (ScopedModifierCount >= ScopedModifiers.size())
                    {
                        break;
                    }
                    GameplayAttributeModifier ScopedModifier = ScopedDef->Modifiers[ScopedM];
                    ScopedModifier.Magnitude = EffectiveMagnitude(ScopedModifier, ScopedActive.Stacks);
                    ScopedModifiers[ScopedModifierCount++] = ScopedModifier;
                }
            }
            for (std::uint32_t ScopedG = 0; ScopedG < ScopedDef->GrantedTagCount; ++ScopedG)
            {
                ScopedGranted.Add(ScopedDef->GrantedTags[ScopedG]);
                ScopedHasGranted = true;
            }
        }
    }

    auto* ScopedTags = InRegistry.try_get<GameplayTagsComponent>(InEntity);
    if (ScopedTags != nullptr || ScopedHasGranted)
    {
        if (ScopedTags == nullptr)
        {
            ScopedTags = &InRegistry.emplace<GameplayTagsComponent>(InEntity);
        }
        ScopedTags->Tags = ScopedTags->BaseTags;
        ScopedTags->Tags.Bits.Union(ScopedGranted.Bits);
        ++ScopedTags->Revision;
    }

    auto* ScopedAttributes = InRegistry.try_get<GameplayAttributesComponent>(InEntity);
    if (ScopedAttributes != nullptr || ScopedModifierCount > 0)
    {
        if (ScopedAttributes == nullptr)
        {
            ScopedAttributes = &InRegistry.emplace<GameplayAttributesComponent>(InEntity);
        }
        RebuildGameplayAttributes(
            *ScopedAttributes, std::span<const GameplayAttributeModifier>(ScopedModifiers.data(), ScopedModifierCount));
    }
}
}
