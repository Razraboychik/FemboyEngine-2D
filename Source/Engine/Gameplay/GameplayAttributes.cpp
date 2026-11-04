// FemboyEngine2D - gameplay attribute registry and aggregation.
#include "Engine/Gameplay/GameplayAttributes.hpp"

#include <cmath>
#include <cstring>

namespace Fby
{
namespace
{
[[nodiscard]] bool IsValidAttributeName(std::string_view InName) noexcept
{
    if (InName.empty() || InName.size() >= kMaxGameplayAttributeName)
    {
        return false;
    }
    for (const char ScopedChar : InName)
    {
        if (static_cast<unsigned char>(ScopedChar) < 0x20)
        {
            return false;
        }
    }
    return true;
}
}

GameplayAttribute GameplayAttributeRegistry::Register(std::string_view InName) noexcept
{
    if (!IsValidAttributeName(InName))
    {
        return {};
    }
    const GameplayAttribute ScopedExisting = Find(InName);
    if (ScopedExisting.IsValid())
    {
        return ScopedExisting;
    }
    if (MCount >= kCapacity)
    {
        return {};
    }
    Entry& ScopedEntry = MEntries[MCount];
    std::memcpy(ScopedEntry.Name.data(), InName.data(), InName.size());
    ScopedEntry.Name[InName.size()] = '\0';
    ScopedEntry.NameLength = static_cast<std::uint32_t>(InName.size());
    return GameplayAttribute{++MCount};
}

GameplayAttribute GameplayAttributeRegistry::Find(std::string_view InName) const noexcept
{
    for (std::uint32_t ScopedI = 0; ScopedI < MCount; ++ScopedI)
    {
        const Entry& ScopedEntry = MEntries[ScopedI];
        if (ScopedEntry.NameLength == InName.size() &&
            std::string_view(ScopedEntry.Name.data(), ScopedEntry.NameLength) == InName)
        {
            return GameplayAttribute{ScopedI + 1u};
        }
    }
    return {};
}

std::string_view GameplayAttributeRegistry::Name(GameplayAttribute InAttribute) const noexcept
{
    if (!InAttribute.IsValid() || InAttribute.Id > MCount)
    {
        return {};
    }
    const Entry& ScopedEntry = MEntries[InAttribute.Id - 1u];
    return std::string_view(ScopedEntry.Name.data(), ScopedEntry.NameLength);
}

void GameplayAttributeRegistry::Clear() noexcept
{
    MEntries = {};
    MCount = 0;
}

void RebuildGameplayAttributes(GameplayAttributesComponent& InComponent,
                               std::span<const GameplayAttributeModifier> InModifiers) noexcept
{
    std::array<float, kMaxGameplayAttributes> ScopedAdds{};
    std::array<float, kMaxGameplayAttributes> ScopedMuls{};
    ScopedMuls.fill(1.0f);
    std::array<float, kMaxGameplayAttributes> ScopedOverrides{};
    std::array<bool, kMaxGameplayAttributes> ScopedHasOverride{};
    for (const GameplayAttributeModifier& ScopedModifier : InModifiers)
    {
        const std::uint32_t ScopedIndex = ScopedModifier.Attribute.Id;
        if (ScopedIndex == 0 || ScopedIndex > kMaxGameplayAttributes)
        {
            continue;
        }
        const std::uint32_t ScopedSlot = ScopedIndex - 1u;
        switch (ScopedModifier.Op)
        {
            case GameplayModifierOp::Add:
                ScopedAdds[ScopedSlot] += ScopedModifier.Magnitude;
                break;
            case GameplayModifierOp::Multiply:
                ScopedMuls[ScopedSlot] *= ScopedModifier.Magnitude;
                break;
            case GameplayModifierOp::Override:
                ScopedOverrides[ScopedSlot] = ScopedModifier.Magnitude;
                ScopedHasOverride[ScopedSlot] = true;
                break;
        }
    }
    for (std::uint32_t ScopedI = 0; ScopedI < kMaxGameplayAttributes; ++ScopedI)
    {
        float ScopedValue = (InComponent.Values.Base[ScopedI] + ScopedAdds[ScopedI]) * ScopedMuls[ScopedI];
        if (ScopedHasOverride[ScopedI])
        {
            ScopedValue = ScopedOverrides[ScopedI];
        }
        InComponent.Values.Current[ScopedI] = ScopedValue;
    }
    ++InComponent.Revision;
}

void ApplyInstantAttributeModifiers(GameplayAttributesComponent& InComponent,
                                    std::span<const GameplayAttributeModifier> InModifiers,
                                    std::uint16_t InStacks) noexcept
{
    const float ScopedStacks = static_cast<float>(InStacks == 0 ? 1u : InStacks);
    for (const GameplayAttributeModifier& ScopedModifier : InModifiers)
    {
        const std::uint32_t ScopedIndex = ScopedModifier.Attribute.Id;
        if (ScopedIndex == 0 || ScopedIndex > kMaxGameplayAttributes)
        {
            continue;
        }
        float& ScopedBase = InComponent.Values.Base[ScopedIndex - 1u];
        switch (ScopedModifier.Op)
        {
            case GameplayModifierOp::Add:
                ScopedBase += ScopedModifier.Magnitude * ScopedStacks;
                break;
            case GameplayModifierOp::Multiply:
                ScopedBase *= std::pow(ScopedModifier.Magnitude, ScopedStacks);
                break;
            case GameplayModifierOp::Override:
                ScopedBase = ScopedModifier.Magnitude;
                break;
        }
    }
    // Instant changes are not modifiers; Current keeps whatever the effect
    // system rebuilds on the next pass.
    for (std::uint32_t ScopedI = 0; ScopedI < kMaxGameplayAttributes; ++ScopedI)
    {
        InComponent.Values.Current[ScopedI] = InComponent.Values.Base[ScopedI];
    }
    ++InComponent.Revision;
}

float GameplayAttributeValue(const GameplayAttributesComponent& InComponent, GameplayAttribute InAttribute) noexcept
{
    if (!InAttribute.IsValid() || InAttribute.Id > kMaxGameplayAttributes)
    {
        return 0.0f;
    }
    return InComponent.Values.Current[InAttribute.Id - 1u];
}

void SetGameplayAttributeBase(GameplayAttributesComponent& InComponent, GameplayAttribute InAttribute,
                              float InValue) noexcept
{
    if (!InAttribute.IsValid() || InAttribute.Id > kMaxGameplayAttributes)
    {
        return;
    }
    InComponent.Values.Base[InAttribute.Id - 1u] = InValue;
    InComponent.Values.Current[InAttribute.Id - 1u] = InValue;
    ++InComponent.Revision;
}
}
