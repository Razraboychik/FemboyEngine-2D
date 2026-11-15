// FemboyEngine2D - persistent prefab instance expansion.
#include "Engine/ECS/PrefabInstance.hpp"

#include <array>

namespace Fby
{

bool PrefabInstance::Expand(Ecs::Registry& InRegistry, Ecs::CommandBuffer& InCommands, Entity InInstance,
                            const PrefabAsset& InAsset, std::span<Entity> OutChildren)
{
    if (OutChildren.size() < InAsset.count)
    {
        return false;
    }
    Vector2D ScopedOrigin{};
    std::array<PrefabEntityOverride, kPrefabMaxEntities> ScopedEntries{};
    std::uint32_t ScopedEntryCount = 0;
    if (const auto* ScopedInstance = InRegistry.try_get<PrefabInstanceComponent>(InInstance))
    {
        ScopedOrigin = ScopedInstance->Origin;
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < InAsset.count; ++ScopedIndex)
        {
            const std::uint32_t ScopedBit = 1u << ScopedIndex;
            PrefabEntityOverride ScopedEntry;
            ScopedEntry.entityIndex = ScopedIndex;
            if ((ScopedInstance->PositionMask & ScopedBit) != 0)
            {
                ScopedEntry.overridePosition = true;
                ScopedEntry.position = ScopedInstance->PositionOverrides[ScopedIndex];
            }
            if ((ScopedInstance->ScaleMask & ScopedBit) != 0)
            {
                ScopedEntry.overrideScale = true;
                ScopedEntry.scale = ScopedInstance->ScaleOverrides[ScopedIndex];
            }
            if ((ScopedInstance->RotationMask & ScopedBit) != 0)
            {
                ScopedEntry.overrideRotation = true;
                ScopedEntry.rotation = ScopedInstance->RotationOverrides[ScopedIndex].X;
            }
            if ((ScopedInstance->ColorMask & ScopedBit) != 0)
            {
                const Vector2D& ScopedColor = ScopedInstance->ColorOverrides[ScopedIndex];
                ScopedEntry.overrideColor = true;
                ScopedEntry.color = Color32::FromFloats(ScopedColor.X, ScopedColor.Y, ScopedInstance->RotationOverrides[ScopedIndex].Y, 1.0f);
            }
            if (ScopedEntry.overridePosition || ScopedEntry.overrideScale || ScopedEntry.overrideRotation ||
                ScopedEntry.overrideColor)
            {
                ScopedEntries[ScopedEntryCount++] = ScopedEntry;
            }
        }
    }
    const PrefabOverrides ScopedOverrides{
        std::span<const PrefabEntityOverride>(ScopedEntries.data(), ScopedEntryCount)};
    if (!PrefabSystem::Spawn(InRegistry, InCommands, InAsset, ScopedOrigin, OutChildren, ScopedOverrides))
    {
        return false;
    }
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < InAsset.count && ScopedIndex < OutChildren.size(); ++ScopedIndex)
    {
        const Entity ScopedChild = OutChildren[ScopedIndex];
        if (ScopedChild == kNullEntity)
        {
            continue;
        }
        PrefabInstanceMemberComponent ScopedMember;
        ScopedMember.Instance = InInstance;
        ScopedMember.PrefabIndex = ScopedIndex;
        InRegistry.emplace<PrefabInstanceMemberComponent>(ScopedChild, ScopedMember);
    }
    return true;
}

bool PrefabInstance::SetOverride(Ecs::Registry& InRegistry, Entity InInstance,
                                 const PrefabEntityOverride& InOverride)
{
    auto* ScopedInstance = InRegistry.try_get<PrefabInstanceComponent>(InInstance);
    if (ScopedInstance == nullptr || InOverride.entityIndex >= kPrefabMaxEntities)
    {
        return false;
    }
    const std::uint32_t ScopedIndex = InOverride.entityIndex;
    const std::uint32_t ScopedBit = 1u << ScopedIndex;
    if (InOverride.overridePosition)
    {
        ScopedInstance->PositionMask |= ScopedBit;
        ScopedInstance->PositionOverrides[ScopedIndex] = InOverride.position;
    }
    if (InOverride.overrideScale)
    {
        ScopedInstance->ScaleMask |= ScopedBit;
        ScopedInstance->ScaleOverrides[ScopedIndex] = InOverride.scale;
    }
    if (InOverride.overrideRotation)
    {
        ScopedInstance->RotationMask |= ScopedBit;
        ScopedInstance->RotationOverrides[ScopedIndex] = Vector2D{InOverride.rotation, 0.0f};
    }
    if (InOverride.overrideColor)
    {
        ScopedInstance->ColorMask |= ScopedBit;
        ScopedInstance->ColorOverrides[ScopedIndex] = Vector2D{
            static_cast<float>(InOverride.color.R()) / 255.0f, static_cast<float>(InOverride.color.G()) / 255.0f};
        ScopedInstance->RotationOverrides[ScopedIndex].Y =
            static_cast<float>(InOverride.color.B()) / 255.0f;
    }
    return true;
}

void PrefabInstance::ClearOverrides(Ecs::Registry& InRegistry, Entity InInstance)
{
    auto* ScopedInstance = InRegistry.try_get<PrefabInstanceComponent>(InInstance);
    if (ScopedInstance == nullptr)
    {
        return;
    }
    ScopedInstance->PositionMask = 0;
    ScopedInstance->ScaleMask = 0;
    ScopedInstance->RotationMask = 0;
    ScopedInstance->ColorMask = 0;
}

void PrefabInstance::ClearChildren(Ecs::Registry& InRegistry, Entity InInstance)
{
    std::array<Entity, kPrefabMaxEntities> ScopedChildren{};
    std::uint32_t ScopedCount = 0;
    auto ScopedView = InRegistry.view<PrefabInstanceMemberComponent>();
    for (const Entity ScopedChild : ScopedView)
    {
        if (ScopedView.get<PrefabInstanceMemberComponent>(ScopedChild).Instance == InInstance &&
            ScopedCount < ScopedChildren.size())
        {
            ScopedChildren[ScopedCount++] = ScopedChild;
        }
    }
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedCount; ++ScopedIndex)
    {
        InRegistry.destroy(ScopedChildren[ScopedIndex]);
    }
}
}
