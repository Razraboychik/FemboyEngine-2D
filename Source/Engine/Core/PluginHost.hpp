// FemboyEngine2D - native plugin host.
#pragma once

#include "Engine/Core/FlatMap.hpp"
#include "Engine/ECS/SystemGraph.hpp"
#include "Engine/Core/PluginAPI.h"
#include "Engine/Core/Mirror.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Fby::Plugin
{

struct ComponentType
{
    std::string name;
    std::uint32_t size = 0;
    std::uint32_t alignment = 1;
    std::uint64_t layoutHash = 0;
};

// The host side of the component contract. A game registers its components once;
class ComponentRegistry
{
public:
    template <typename Component> void Register(std::string InName)
    {
        // Keep the key separate from the moved value: evaluating
        const std::string ScopedKey = InName;
        ComponentType ScopedType{InName, static_cast<std::uint32_t>(sizeof(Component)),
                                 static_cast<std::uint32_t>(alignof(Component)), Reflect::LayoutHash<Component>()};
        MTypes.insert_or_assign(ScopedKey, std::move(ScopedType));
    }

    [[nodiscard]] const ComponentType* Find(std::string_view InName) const;
    [[nodiscard]] std::uint64_t HashOf(std::string_view InName) const;
    [[nodiscard]] std::size_t Size() const
    {
        return MTypes.size();
    }
    [[nodiscard]] const Fby::FlatMap<std::string, ComponentType>& All() const
    {
        return MTypes;
    }

private:
    Fby::FlatMap<std::string, ComponentType> MTypes;
};

struct PluginSystem
{
    std::string name;
    int phase = 0;
    int order = 0;
    void (*tick)(void* InUser, float InDt) = nullptr;
    // Host API pointer a plugin tick receives. The plugin ABI has no separate
    void* hostUser = nullptr;
};

struct LoadedPlugin
{
    std::string path;
    std::string name;
    std::string version;
    std::string author;
    std::string buildInfo;
    std::vector<ComponentType> components;
    std::vector<PluginSystem> systems;
    void* handle = nullptr;
};

class PluginHost
{
public:
    // `ecsRegistry` is entt::registry*; it is passed to plugins verbatim.
    PluginHost(ComponentRegistry& InComponents, void* InEcsRegistry);
    ~PluginHost();

    PluginHost(const PluginHost&) = delete;
    PluginHost& operator=(const PluginHost&) = delete;

    // The graph must outlive the host. Call before Load(): every plugin system
    void BindSystemGraph(Ecs::SystemGraph& InGraph) noexcept
    {
        MSystemGraph = &InGraph;
    }
    [[nodiscard]] Ecs::SystemGraph* BoundGraph() const noexcept
    {
        return MSystemGraph;
    }

    // Returns false and fills `error` when the library is missing, the ABI does
    bool Load(const std::string& InPath, std::string* InError);

    // Calls fby_plugin_unload on every plugin and closes the libraries.
    void UnloadAll();

    [[nodiscard]] const std::vector<std::unique_ptr<LoadedPlugin>>& Plugins() const
    {
        return MPlugins;
    }
    // The host table handed to plugins. Its `user` field is the pointer a
    [[nodiscard]] const FbyHostApi& Api() const noexcept
    {
        return MApi;
    }

private:
    static void* HostAlloc(void* InUser, std::size_t InSize, std::size_t InAlignment);
    static void HostFree(void* InUser, void* InPointer, std::size_t InSize, std::size_t InAlignment);
    static void HostLog(void* InUser, int InLevel, const char* InChannel, const char* InMessage);
    static std::uint64_t HostLayoutHash(void* InUser, const char* InName);
    static int HostAddSystem(void* InUser, const char* InName, int InPhase, int InOrder,
                             void (*InTick)(void* InUser, float InDt));
    static void PluginSystemThunk(Ecs::SystemContext& InContext, void* InUser);

    ComponentRegistry& MComponents;
    void* MEcsRegistry = nullptr;
    FbyHostApi MApi{};
    LoadedPlugin* MLoading = nullptr;
    Ecs::SystemGraph* MSystemGraph = nullptr;
    std::vector<std::unique_ptr<LoadedPlugin>> MPlugins;
};
}
