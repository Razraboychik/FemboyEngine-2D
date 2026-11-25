// FemboyEngine2D - host implementation of the safe mod data ABI.
#pragma once

#include "Engine/Core/PluginHost.hpp"
#include "Engine/ECS/FCS/Registry.hpp"
#include "Engine/Modding/ModAPI.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace Fby
{
class GameplayTagRegistry;
class GameplayEffectSystem;
class GameplaySkillSystem;
}

namespace Fby::Modding
{
class ModApiHost
{
public:
    struct Config
    {
        Fby::Fcs::Registry* Registry = nullptr;
        Plugin::ComponentRegistry* Components = nullptr;
        Fby::GameplayTagRegistry* Tags = nullptr;
        Fby::GameplayEffectSystem* Effects = nullptr;
        Fby::GameplaySkillSystem* Skills = nullptr;
    };

    explicit ModApiHost(Config InConfig) noexcept;

    ModApiHost(const ModApiHost&) = delete;
    ModApiHost& operator=(const ModApiHost&) = delete;

    [[nodiscard]] const FbyModApi* Api() const noexcept
    {
        return &MApi;
    }
    [[nodiscard]] FbyModApi* Api() noexcept
    {
        return &MApi;
    }
    // Optional namespace enforced for tags registered by this mod.
    void SetNamespace(std::string_view InNamespace) noexcept;

    // Applies queued component writes. Must run outside a mod query and at a
    // CommandBuffer-safe point.
    [[nodiscard]] bool FlushCommands() noexcept;

    // Registers a PostUpdate flush node. The graph must not outlive this host.
    [[nodiscard]] bool InstallFlushSystem(Ecs::SystemGraph& InGraph,
                                          Ecs::Phase InPhase = Ecs::Phase::PostUpdate) noexcept;

private:
    static int EntityAlive(void* InUser, FbyModEntity InEntity);
    static FbyModEntity EntityCreate(void* InUser);
    static int EntityDestroy(void* InUser, FbyModEntity InEntity);
    static int ComponentFind(void* InUser, const char* InName, FbyModComponentId* OutId);
    static int ComponentSize(void* InUser, FbyModComponentId InId, std::uint32_t* OutSize);
    static int ComponentLayoutHash(void* InUser, FbyModComponentId InId, std::uint64_t* OutHash);
    static int ComponentRead(void* InUser, FbyModEntity InEntity, FbyModComponentId InId, void* OutData,
                             std::size_t InCapacity, std::size_t* OutSize);
    static int CommandWrite(void* InUser, FbyModEntity InEntity, FbyModComponentId InId, const void* InData,
                            std::size_t InSize);
    static int CommandRemove(void* InUser, FbyModEntity InEntity, FbyModComponentId InId);
    static int Flush(void* InUser);
    static int QueryBegin(void* InUser, const FbyModComponentId* InTypes, std::uint32_t InTypeCount,
                          FbyModQuery* OutQuery);
    static int QueryNext(void* InUser, FbyModQuery* InQuery, FbyModEntity* OutEntity);
    static void QueryEnd(void* InUser, FbyModQuery* InQuery);
    static int EventRegister(void* InUser, const char* InName, FbyModEventId* OutEvent);
    static int EventPublish(void* InUser, FbyModEventId InEvent, const void* InData, std::size_t InSize);
    static int EventSubscribe(void* InUser, FbyModEventId InEvent, FbyModEventFn InCallback, void* InCallbackUser);
    static void Log(void* InUser, int InLevel, const char* InMessage);
    static int RegisterTag(void* InUser, const char* InPath, std::uint32_t* OutTagId);
    static int RegisterEffect(void* InUser, const FbyModEffectDef* InDef);
    static int RegisterSkill(void* InUser, const FbyModSkillDef* InDef);
    static void FlushSystem(Ecs::SystemContext& InContext, void* InUser);

    struct Command
    {
        FbyModEntity Entity = 0;
        FbyModComponentId Type = 0;
        std::uint32_t Size = 0;
        bool Remove = false;
        std::array<std::byte, FBY_MOD_MAX_COMMAND_BYTES> Bytes{};
    };

    struct EventType
    {
        FbyModEventId Id = 0;
        char Name[64]{};
    };

    struct Subscriber
    {
        FbyModEventId Event = 0;
        FbyModEventFn Callback = nullptr;
        void* User = nullptr;
    };

    [[nodiscard]] const Plugin::ComponentType* FindType(FbyModComponentId InId) const noexcept;
    [[nodiscard]] bool EntityValid(FbyModEntity InEntity) const noexcept;
    void BumpStructuralRevision() noexcept
    {
        ++MStructuralRevision;
    }

    Config MConfig;
    FbyModApi MApi{};
    std::array<Command, 256> MCommands{};
    std::uint32_t MCommandCount = 0;
    std::uint32_t MQueryActive = 0;
    std::uint32_t MEventDepth = 0;
    std::uint64_t MStructuralRevision = 1;
    std::array<EventType, 64> MEventTypes{};
    std::uint32_t MEventCount = 0;
    std::array<Subscriber, 32> MSubscribers{};
    std::uint32_t MSubscriberCount = 0;
    std::array<char, 64> MNamespace{};
    std::size_t MNamespaceLength = 0;
};
}
