#include "Engine/Modding/ModApiHost.hpp"

#include "Engine/Core/Log.hpp"
#include "Engine/Gameplay/GameplayEffects.hpp"
#include "Engine/Gameplay/GameplaySkills.hpp"
#include "Engine/Gameplay/GameplayTags.hpp"

#include <algorithm>
#include <cstring>

namespace Fby::Modding
{
namespace
{
constexpr std::uint32_t Fnv1a(std::string_view InText) noexcept
{
    std::uint32_t ScopedHash = 2166136261u;
    for (const char ScopedChar : InText)
    {
        ScopedHash ^= static_cast<std::uint32_t>(static_cast<unsigned char>(ScopedChar));
        ScopedHash *= 16777619u;
    }
    return ScopedHash == 0 ? 1u : ScopedHash;
}
}

ModApiHost::ModApiHost(Config InConfig) noexcept : MConfig(InConfig)
{
    MApi.struct_size = static_cast<std::uint32_t>(sizeof(FbyModApi));
    MApi.abi_version = FBY_MOD_API_VERSION;
    MApi.user = this;
    MApi.entity_alive = &ModApiHost::EntityAlive;
    MApi.entity_create = &ModApiHost::EntityCreate;
    MApi.entity_destroy = &ModApiHost::EntityDestroy;
    MApi.component_find = &ModApiHost::ComponentFind;
    MApi.component_size = &ModApiHost::ComponentSize;
    MApi.component_layout_hash = &ModApiHost::ComponentLayoutHash;
    MApi.component_read = &ModApiHost::ComponentRead;
    MApi.command_write = &ModApiHost::CommandWrite;
    MApi.command_remove = &ModApiHost::CommandRemove;
    MApi.flush_commands = &ModApiHost::Flush;
    MApi.query_begin = &ModApiHost::QueryBegin;
    MApi.query_next = &ModApiHost::QueryNext;
    MApi.query_end = &ModApiHost::QueryEnd;
    MApi.event_register = &ModApiHost::EventRegister;
    MApi.event_publish = &ModApiHost::EventPublish;
    MApi.event_subscribe = &ModApiHost::EventSubscribe;
    MApi.log = &ModApiHost::Log;
    MApi.register_tag = &ModApiHost::RegisterTag;
    MApi.register_effect = &ModApiHost::RegisterEffect;
    MApi.register_skill = &ModApiHost::RegisterSkill;
}

void ModApiHost::SetNamespace(std::string_view InNamespace) noexcept
{
    MNamespaceLength = std::min(InNamespace.size(), MNamespace.size() - 1u);
    if (MNamespaceLength != 0)
    {
        std::memcpy(MNamespace.data(), InNamespace.data(), MNamespaceLength);
    }
    MNamespace[MNamespaceLength] = '\0';
}

const Plugin::ComponentType* ModApiHost::FindType(FbyModComponentId InId) const noexcept
{
    if (MConfig.Components == nullptr || InId == 0)
    {
        return nullptr;
    }
    for (const auto& ScopedEntry : MConfig.Components->All())
    {
        if (ScopedEntry.second.id == InId)
        {
            return &ScopedEntry.second;
        }
    }
    return nullptr;
}

bool ModApiHost::EntityValid(FbyModEntity InEntity) const noexcept
{
    return MConfig.Registry != nullptr && MConfig.Registry->valid(static_cast<Fby::Entity>(InEntity));
}

int ModApiHost::EntityAlive(void* InUser, FbyModEntity InEntity)
{
    auto* ScopedHost = static_cast<ModApiHost*>(InUser);
    return ScopedHost != nullptr && ScopedHost->EntityValid(InEntity) ? 1 : 0;
}

FbyModEntity ModApiHost::EntityCreate(void* InUser)
{
    auto* ScopedHost = static_cast<ModApiHost*>(InUser);
    if (ScopedHost == nullptr || ScopedHost->MConfig.Registry == nullptr || ScopedHost->MQueryActive != 0 ||
        ScopedHost->MEventDepth != 0)
    {
        return Fby::kNullEntity;
    }
    ScopedHost->BumpStructuralRevision();
    return static_cast<FbyModEntity>(ScopedHost->MConfig.Registry->create());
}

int ModApiHost::EntityDestroy(void* InUser, FbyModEntity InEntity)
{
    auto* ScopedHost = static_cast<ModApiHost*>(InUser);
    if (ScopedHost == nullptr || !ScopedHost->EntityValid(InEntity))
    {
        return FBY_MOD_STALE_ENTITY;
    }
    if (ScopedHost->MQueryActive != 0 || ScopedHost->MEventDepth != 0)
    {
        return FBY_MOD_FORBIDDEN;
    }
    ScopedHost->BumpStructuralRevision();
    ScopedHost->MConfig.Registry->destroy(static_cast<Fby::Entity>(InEntity));
    return FBY_MOD_OK;
}

int ModApiHost::ComponentFind(void* InUser, const char* InName, FbyModComponentId* OutId)
{
    auto* ScopedHost = static_cast<ModApiHost*>(InUser);
    if (ScopedHost == nullptr || InName == nullptr || OutId == nullptr || ScopedHost->MConfig.Components == nullptr)
    {
        return FBY_MOD_INVALID_ARGUMENT;
    }
    const Plugin::ComponentType* ScopedType = ScopedHost->MConfig.Components->Find(InName);
    if (ScopedType == nullptr)
    {
        return FBY_MOD_NOT_FOUND;
    }
    *OutId = ScopedType->id;
    return FBY_MOD_OK;
}

int ModApiHost::ComponentSize(void* InUser, FbyModComponentId InId, std::uint32_t* OutSize)
{
    auto* ScopedHost = static_cast<ModApiHost*>(InUser);
    const Plugin::ComponentType* ScopedType = ScopedHost != nullptr ? ScopedHost->FindType(InId) : nullptr;
    if (ScopedType == nullptr || OutSize == nullptr)
    {
        return FBY_MOD_NOT_FOUND;
    }
    *OutSize = ScopedType->size;
    return FBY_MOD_OK;
}

int ModApiHost::ComponentLayoutHash(void* InUser, FbyModComponentId InId, std::uint64_t* OutHash)
{
    auto* ScopedHost = static_cast<ModApiHost*>(InUser);
    const Plugin::ComponentType* ScopedType = ScopedHost != nullptr ? ScopedHost->FindType(InId) : nullptr;
    if (ScopedType == nullptr || OutHash == nullptr)
    {
        return FBY_MOD_NOT_FOUND;
    }
    *OutHash = ScopedType->layoutHash;
    return FBY_MOD_OK;
}

int ModApiHost::ComponentRead(void* InUser, FbyModEntity InEntity, FbyModComponentId InId, void* OutData,
                              std::size_t InCapacity, std::size_t* OutSize)
{
    auto* ScopedHost = static_cast<ModApiHost*>(InUser);
    if (ScopedHost == nullptr || OutData == nullptr || OutSize == nullptr)
    {
        return FBY_MOD_INVALID_ARGUMENT;
    }
    if (!ScopedHost->EntityValid(InEntity))
    {
        return FBY_MOD_STALE_ENTITY;
    }
    const Plugin::ComponentType* ScopedType = ScopedHost->FindType(InId);
    if (ScopedType == nullptr || ScopedType->read == nullptr)
    {
        return FBY_MOD_NOT_FOUND;
    }
    *OutSize = ScopedType->size;
    if (InCapacity < ScopedType->size)
    {
        return FBY_MOD_WRONG_SIZE;
    }
    if (!ScopedType->read(ScopedHost->MConfig.Registry, static_cast<Fby::Entity>(InEntity), OutData, InCapacity,
                          OutSize))
    {
        return FBY_MOD_NOT_FOUND;
    }
    return FBY_MOD_OK;
}

int ModApiHost::CommandWrite(void* InUser, FbyModEntity InEntity, FbyModComponentId InId, const void* InData,
                             std::size_t InSize)
{
    auto* ScopedHost = static_cast<ModApiHost*>(InUser);
    if (ScopedHost == nullptr || InData == nullptr || ScopedHost->MQueryActive != 0)
    {
        return FBY_MOD_FORBIDDEN;
    }
    if (!ScopedHost->EntityValid(InEntity))
    {
        return FBY_MOD_STALE_ENTITY;
    }
    const Plugin::ComponentType* ScopedType = ScopedHost->FindType(InId);
    if (ScopedType == nullptr)
    {
        return FBY_MOD_NOT_FOUND;
    }
    if (InSize != ScopedType->size || InSize > FBY_MOD_MAX_COMMAND_BYTES)
    {
        return FBY_MOD_WRONG_SIZE;
    }
    if (ScopedHost->MCommandCount >= ScopedHost->MCommands.size())
    {
        return FBY_MOD_FULL;
    }
    Command& ScopedCommand = ScopedHost->MCommands[ScopedHost->MCommandCount++];
    ScopedCommand.Entity = InEntity;
    ScopedCommand.Type = InId;
    ScopedCommand.Size = static_cast<std::uint32_t>(InSize);
    std::memcpy(ScopedCommand.Bytes.data(), InData, InSize);
    return FBY_MOD_OK;
}

int ModApiHost::CommandRemove(void* InUser, FbyModEntity InEntity, FbyModComponentId InId)
{
    auto* ScopedHost = static_cast<ModApiHost*>(InUser);
    if (ScopedHost == nullptr || ScopedHost->MQueryActive != 0)
    {
        return FBY_MOD_FORBIDDEN;
    }
    if (!ScopedHost->EntityValid(InEntity))
    {
        return FBY_MOD_STALE_ENTITY;
    }
    if (ScopedHost->FindType(InId) == nullptr)
    {
        return FBY_MOD_NOT_FOUND;
    }
    if (ScopedHost->MCommandCount >= ScopedHost->MCommands.size())
    {
        return FBY_MOD_FULL;
    }
    Command& ScopedCommand = ScopedHost->MCommands[ScopedHost->MCommandCount++];
    ScopedCommand.Entity = InEntity;
    ScopedCommand.Type = InId;
    ScopedCommand.Remove = true;
    return FBY_MOD_OK;
}

bool ModApiHost::FlushCommands() noexcept
{
    return Flush(this) == FBY_MOD_OK;
}

void ModApiHost::FlushSystem(Ecs::SystemContext&, void* InUser)
{
    auto* ScopedHost = static_cast<ModApiHost*>(InUser);
    if (ScopedHost != nullptr)
    {
        (void)ScopedHost->FlushCommands();
    }
}

bool ModApiHost::InstallFlushSystem(Ecs::SystemGraph& InGraph, Ecs::Phase InPhase) noexcept
{
    return InGraph.Add(InPhase, "Fby.Mod.FlushCommands", &ModApiHost::FlushSystem, this);
}

int ModApiHost::Flush(void* InUser)
{
    auto* ScopedHost = static_cast<ModApiHost*>(InUser);
    if (ScopedHost == nullptr || ScopedHost->MQueryActive != 0 || ScopedHost->MEventDepth != 0)
    {
        return FBY_MOD_FORBIDDEN;
    }
    bool ScopedStructural = false;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedHost->MCommandCount; ++ScopedIndex)
    {
        const Command& ScopedCommand = ScopedHost->MCommands[ScopedIndex];
        if (!ScopedHost->EntityValid(ScopedCommand.Entity))
        {
            continue;
        }
        const Plugin::ComponentType* ScopedType = ScopedHost->FindType(ScopedCommand.Type);
        if (ScopedType == nullptr)
        {
            continue;
        }
        if (ScopedCommand.Remove)
        {
            if (ScopedType->remove != nullptr)
            {
                (void)ScopedType->remove(ScopedHost->MConfig.Registry,
                                         static_cast<Fby::Entity>(ScopedCommand.Entity));
                ScopedStructural = true;
            }
        }
        else if (ScopedType->write != nullptr)
        {
            (void)ScopedType->write(ScopedHost->MConfig.Registry, static_cast<Fby::Entity>(ScopedCommand.Entity),
                                    ScopedCommand.Bytes.data(), ScopedCommand.Size);
            ScopedStructural = true;
        }
    }
    ScopedHost->MCommandCount = 0;
    if (ScopedStructural)
    {
        ScopedHost->BumpStructuralRevision();
    }
    return FBY_MOD_OK;
}

int ModApiHost::QueryBegin(void* InUser, const FbyModComponentId* InTypes, std::uint32_t InTypeCount,
                           FbyModQuery* OutQuery)
{
    auto* ScopedHost = static_cast<ModApiHost*>(InUser);
    if (ScopedHost == nullptr || InTypes == nullptr || OutQuery == nullptr || InTypeCount == 0 ||
        InTypeCount > FBY_MOD_MAX_QUERY_TYPES)
    {
        return FBY_MOD_INVALID_ARGUMENT;
    }
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < InTypeCount; ++ScopedIndex)
    {
        if (ScopedHost->FindType(InTypes[ScopedIndex]) == nullptr)
        {
            return FBY_MOD_NOT_FOUND;
        }
    }
    *OutQuery = FbyModQuery{};
    OutQuery->type_count = InTypeCount;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < InTypeCount; ++ScopedIndex)
    {
        OutQuery->types[ScopedIndex] = InTypes[ScopedIndex];
    }
    OutQuery->structural_revision = ScopedHost->MStructuralRevision;
    ++ScopedHost->MQueryActive;
    return FBY_MOD_OK;
}

int ModApiHost::QueryNext(void* InUser, FbyModQuery* InQuery, FbyModEntity* OutEntity)
{
    auto* ScopedHost = static_cast<ModApiHost*>(InUser);
    if (ScopedHost == nullptr || InQuery == nullptr || OutEntity == nullptr || ScopedHost->MConfig.Registry == nullptr)
    {
        return FBY_MOD_INVALID_ARGUMENT;
    }
    if (InQuery->structural_revision != ScopedHost->MStructuralRevision)
    {
        return FBY_MOD_STALE_QUERY;
    }
    while (InQuery->cursor < ScopedHost->MConfig.Registry->RecordCount())
    {
        const Fby::Entity ScopedEntity = ScopedHost->MConfig.Registry->EntityAt(InQuery->cursor++);
        if (!ScopedHost->MConfig.Registry->valid(ScopedEntity))
        {
            continue;
        }
        bool ScopedMatches = true;
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < InQuery->type_count; ++ScopedIndex)
        {
            const Plugin::ComponentType* ScopedType = ScopedHost->FindType(InQuery->types[ScopedIndex]);
            if (ScopedType == nullptr || ScopedType->has == nullptr ||
                !ScopedType->has(ScopedHost->MConfig.Registry, ScopedEntity))
            {
                ScopedMatches = false;
                break;
            }
        }
        if (ScopedMatches)
        {
            *OutEntity = static_cast<FbyModEntity>(ScopedEntity);
            return FBY_MOD_OK;
        }
    }
    return FBY_MOD_NOT_FOUND;
}

void ModApiHost::QueryEnd(void* InUser, FbyModQuery* InQuery)
{
    auto* ScopedHost = static_cast<ModApiHost*>(InUser);
    if (ScopedHost != nullptr && ScopedHost->MQueryActive != 0)
    {
        --ScopedHost->MQueryActive;
    }
    if (InQuery != nullptr)
    {
        InQuery->flags = 0;
    }
}

int ModApiHost::EventRegister(void* InUser, const char* InName, FbyModEventId* OutEvent)
{
    auto* ScopedHost = static_cast<ModApiHost*>(InUser);
    if (ScopedHost == nullptr || InName == nullptr || InName[0] == '\0' || OutEvent == nullptr)
    {
        return FBY_MOD_INVALID_ARGUMENT;
    }
    const std::string_view ScopedName(InName);
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedHost->MEventCount; ++ScopedIndex)
    {
        if (ScopedName == ScopedHost->MEventTypes[ScopedIndex].Name)
        {
            *OutEvent = ScopedHost->MEventTypes[ScopedIndex].Id;
            return FBY_MOD_OK;
        }
    }
    if (ScopedHost->MEventCount >= ScopedHost->MEventTypes.size() || ScopedName.size() >= 64)
    {
        return FBY_MOD_FULL;
    }
    EventType& ScopedEvent = ScopedHost->MEventTypes[ScopedHost->MEventCount];
    ScopedEvent.Id = Fnv1a(ScopedName);
    std::memcpy(ScopedEvent.Name, InName, ScopedName.size() + 1);
    ++ScopedHost->MEventCount;
    *OutEvent = ScopedEvent.Id;
    return FBY_MOD_OK;
}

int ModApiHost::EventSubscribe(void* InUser, FbyModEventId InEvent, FbyModEventFn InCallback, void* InCallbackUser)
{
    auto* ScopedHost = static_cast<ModApiHost*>(InUser);
    if (ScopedHost == nullptr || InEvent == 0 || InCallback == nullptr)
    {
        return FBY_MOD_INVALID_ARGUMENT;
    }
    if (ScopedHost->MSubscriberCount >= ScopedHost->MSubscribers.size())
    {
        return FBY_MOD_FULL;
    }
    Subscriber& ScopedSubscriber = ScopedHost->MSubscribers[ScopedHost->MSubscriberCount++];
    ScopedSubscriber.Event = InEvent;
    ScopedSubscriber.Callback = InCallback;
    ScopedSubscriber.User = InCallbackUser;
    return FBY_MOD_OK;
}

int ModApiHost::EventPublish(void* InUser, FbyModEventId InEvent, const void* InData, std::size_t InSize)
{
    auto* ScopedHost = static_cast<ModApiHost*>(InUser);
    if (ScopedHost == nullptr || InEvent == 0 || InSize > FBY_MOD_MAX_EVENT_BYTES ||
        (InData == nullptr && InSize != 0))
    {
        return FBY_MOD_INVALID_ARGUMENT;
    }
    std::array<std::byte, FBY_MOD_MAX_EVENT_BYTES> ScopedCopy{};
    if (InSize != 0)
    {
        std::memcpy(ScopedCopy.data(), InData, InSize);
    }
    ++ScopedHost->MEventDepth;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedHost->MSubscriberCount; ++ScopedIndex)
    {
        const Subscriber& ScopedSubscriber = ScopedHost->MSubscribers[ScopedIndex];
        if (ScopedSubscriber.Event == InEvent)
        {
            try
            {
                ScopedSubscriber.Callback(ScopedSubscriber.User, InEvent, ScopedCopy.data(), InSize);
            }
            catch (...)
            {
                LogError("mod", "event callback threw; exception contained at the C ABI boundary");
            }
        }
    }
    --ScopedHost->MEventDepth;
    return FBY_MOD_OK;
}

void ModApiHost::Log(void* InUser, int InLevel, const char* InMessage)
{
    (void)InUser;
    if (InMessage == nullptr)
    {
        return;
    }
    if (InLevel >= 2)
    {
        LogError("mod", "{}", InMessage);
    }
    else if (InLevel == 1)
    {
        LogWarn("mod", "{}", InMessage);
    }
    else
    {
        LogInfo("mod", "{}", InMessage);
    }
}

int ModApiHost::RegisterTag(void* InUser, const char* InPath, std::uint32_t* OutTagId)
{
    auto* ScopedHost = static_cast<ModApiHost*>(InUser);
    if (ScopedHost == nullptr || InPath == nullptr || OutTagId == nullptr || ScopedHost->MConfig.Tags == nullptr)
    {
        return FBY_MOD_INVALID_ARGUMENT;
    }
    const std::string_view ScopedPath(InPath);
    if (ScopedHost->MNamespaceLength != 0)
    {
        std::string ScopedPrefix = "mod.";
        ScopedPrefix.append(ScopedHost->MNamespace.data(), ScopedHost->MNamespaceLength);
        ScopedPrefix += '.';
        if (ScopedPath.size() <= ScopedPrefix.size() ||
            ScopedPath.compare(0, ScopedPrefix.size(), ScopedPrefix) != 0)
        {
            return FBY_MOD_FORBIDDEN;
        }
    }
    const GameplayTag ScopedTag = ScopedHost->MConfig.Tags->Register(ScopedPath);
    if (!ScopedTag.IsValid())
    {
        return FBY_MOD_FULL;
    }
    *OutTagId = ScopedTag.Id;
    return FBY_MOD_OK;
}

int ModApiHost::RegisterEffect(void* InUser, const FbyModEffectDef* InDef)
{
    auto* ScopedHost = static_cast<ModApiHost*>(InUser);
    if (ScopedHost == nullptr || InDef == nullptr || ScopedHost->MConfig.Effects == nullptr ||
        InDef->modifier_count > 4 || InDef->granted_tag_count > 4)
    {
        return FBY_MOD_INVALID_ARGUMENT;
    }
    GameplayEffectDef ScopedDef;
    ScopedDef.Id = InDef->id;
    ScopedDef.Flags = InDef->flags;
    ScopedDef.Duration = InDef->duration;
    ScopedDef.Period = InDef->period;
    ScopedDef.MaxStacks = InDef->max_stacks;
    ScopedDef.StackPolicy = static_cast<GameplayEffectStackPolicy>(InDef->stack_policy);
    ScopedDef.ModifierCount = InDef->modifier_count;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < InDef->modifier_count; ++ScopedIndex)
    {
        ScopedDef.Modifiers[ScopedIndex].Attribute = GameplayAttribute{InDef->modifiers[ScopedIndex].attribute};
        ScopedDef.Modifiers[ScopedIndex].Op = static_cast<GameplayModifierOp>(InDef->modifiers[ScopedIndex].op);
        ScopedDef.Modifiers[ScopedIndex].Magnitude = InDef->modifiers[ScopedIndex].magnitude;
    }
    ScopedDef.GrantedTagCount = InDef->granted_tag_count;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < InDef->granted_tag_count; ++ScopedIndex)
    {
        ScopedDef.GrantedTags[ScopedIndex] = GameplayTag{InDef->granted_tags[ScopedIndex]};
    }
    return ScopedHost->MConfig.Effects->AddDef(ScopedDef) ? FBY_MOD_OK : FBY_MOD_INVALID_ARGUMENT;
}

int ModApiHost::RegisterSkill(void* InUser, const FbyModSkillDef* InDef)
{
    auto* ScopedHost = static_cast<ModApiHost*>(InUser);
    if (ScopedHost == nullptr || InDef == nullptr || ScopedHost->MConfig.Skills == nullptr ||
        InDef->self_effect_count > 3 || InDef->target_effect_count > 3 || InDef->target_kind > 3)
    {
        return FBY_MOD_INVALID_ARGUMENT;
    }
    GameplaySkillDef ScopedDef;
    ScopedDef.Id = InDef->id;
    ScopedDef.Cooldown = InDef->cooldown;
    ScopedDef.CastTime = InDef->cast_time;
    ScopedDef.CostAttribute = GameplayAttribute{InDef->cost_attribute};
    ScopedDef.CostAmount = InDef->cost_amount;
    ScopedDef.SelfEffectCount = InDef->self_effect_count;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < InDef->self_effect_count; ++ScopedIndex)
    {
        ScopedDef.SelfEffectIds[ScopedIndex] = InDef->self_effects[ScopedIndex];
    }
    ScopedDef.TargetEffectCount = InDef->target_effect_count;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < InDef->target_effect_count; ++ScopedIndex)
    {
        ScopedDef.TargetEffectIds[ScopedIndex] = InDef->target_effects[ScopedIndex];
    }
    ScopedDef.TargetKind = static_cast<GameplaySkillTargetKind>(InDef->target_kind);
    ScopedDef.AnimationTag = GameplayTag{InDef->animation_tag};
    ScopedDef.CueId = InDef->cue_id;
    return ScopedHost->MConfig.Skills->AddDef(ScopedDef) ? FBY_MOD_OK : FBY_MOD_INVALID_ARGUMENT;
}
}
