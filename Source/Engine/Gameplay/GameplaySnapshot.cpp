// FemboyEngine2D - raw snapshot codecs for gameplay components.
#include "Engine/Gameplay/GameplaySnapshot.hpp"

#include "Engine/Gameplay/GameplayEffects.hpp"
#include "Engine/Gameplay/GameplaySkills.hpp"
#include "Engine/Gameplay/GameplayTags.hpp"

#include <cstring>

namespace Fby
{
namespace
{
using Registry = Ecs::Registry;

template <typename T> [[nodiscard]] bool HasComponent(const Registry& InRegistry, Entity InEntity)
{
    return InRegistry.all_of<T>(InEntity);
}

template <typename T>
[[nodiscard]] bool RestoreRaw(Registry& InRegistry, Entity InEntity, std::span<const std::byte> InSource)
{
    if (InSource.size() != sizeof(T))
    {
        return false;
    }
    T* ScopedComponent = InRegistry.try_get<T>(InEntity);
    if (ScopedComponent == nullptr)
    {
        ScopedComponent = &InRegistry.emplace<T>(InEntity);
    }
    std::memcpy(ScopedComponent, InSource.data(), sizeof(T));
    return true;
}

template <typename T> void RemoveComponent(Registry& InRegistry, Entity InEntity)
{
    InRegistry.remove<T>(InEntity);
}

// The tag component has trailing padding; copying a value-initialized record
// keeps those bytes deterministic for the rollback checksum.
bool CaptureTags(const Registry& InRegistry, Entity InEntity, std::span<std::byte> InDestination)
{
    const auto* ScopedTags = InRegistry.try_get<GameplayTagsComponent>(InEntity);
    if (ScopedTags == nullptr || InDestination.size() < sizeof(GameplayTagsComponent))
    {
        return false;
    }
    GameplayTagsComponent ScopedCanonical{};
    ScopedCanonical.Tags = ScopedTags->Tags;
    ScopedCanonical.BaseTags = ScopedTags->BaseTags;
    ScopedCanonical.Revision = ScopedTags->Revision;
    std::memcpy(InDestination.data(), &ScopedCanonical, sizeof(ScopedCanonical));
    return true;
}

template <typename T> bool CaptureRaw(const Registry& InRegistry, Entity InEntity, std::span<std::byte> InDestination)
{
    const auto* ScopedComponent = InRegistry.try_get<T>(InEntity);
    if (ScopedComponent == nullptr || InDestination.size() < sizeof(T))
    {
        return false;
    }
    std::memcpy(InDestination.data(), ScopedComponent, sizeof(T));
    return true;
}
}

void RegisterGameplaySnapshotCodecs(Ecs::SnapshotSchema& InSchema)
{
    const auto ScopedAdd = [&InSchema](std::string_view InName, std::uint32_t InSize,
                                       bool (*InHas)(const Registry&, Entity),
                                       bool (*InCapture)(const Registry&, Entity, std::span<std::byte>),
                                       bool (*InRestore)(Registry&, Entity, std::span<const std::byte>),
                                       void (*InRemove)(Registry&, Entity), std::uint64_t InLayoutTag)
    {
        Ecs::SnapshotComponent ScopedCodec;
        ScopedCodec.name = InName;
        ScopedCodec.typeId = Ecs::SnapshotTypeId(InName);
        ScopedCodec.layoutHash = InLayoutTag;
        ScopedCodec.size = InSize;
        ScopedCodec.has = InHas;
        ScopedCodec.capture = InCapture;
        ScopedCodec.restore = InRestore;
        ScopedCodec.remove = InRemove;
        (void)InSchema.AddCodec(ScopedCodec);
    };

    ScopedAdd("GameplayTagsComponent", static_cast<std::uint32_t>(sizeof(GameplayTagsComponent)),
              &HasComponent<GameplayTagsComponent>, &CaptureTags, &RestoreRaw<GameplayTagsComponent>,
              &RemoveComponent<GameplayTagsComponent>, Ecs::SnapshotTypeId("GameplayTagsComponent/v1"));
    ScopedAdd("GameplayAttributesComponent", static_cast<std::uint32_t>(sizeof(GameplayAttributesComponent)),
              &HasComponent<GameplayAttributesComponent>, &CaptureRaw<GameplayAttributesComponent>,
              &RestoreRaw<GameplayAttributesComponent>, &RemoveComponent<GameplayAttributesComponent>,
              Ecs::SnapshotTypeId("GameplayAttributesComponent/v1"));
    ScopedAdd("GameplayEffectsComponent", static_cast<std::uint32_t>(sizeof(GameplayEffectsComponent)),
              &HasComponent<GameplayEffectsComponent>, &CaptureRaw<GameplayEffectsComponent>,
              &RestoreRaw<GameplayEffectsComponent>, &RemoveComponent<GameplayEffectsComponent>,
              Ecs::SnapshotTypeId("GameplayEffectsComponent/v1"));
    ScopedAdd("GameplaySkillsComponent", static_cast<std::uint32_t>(sizeof(GameplaySkillsComponent)),
              &HasComponent<GameplaySkillsComponent>, &CaptureRaw<GameplaySkillsComponent>,
              &RestoreRaw<GameplaySkillsComponent>, &RemoveComponent<GameplaySkillsComponent>,
              Ecs::SnapshotTypeId("GameplaySkillsComponent/v1"));
}
}
