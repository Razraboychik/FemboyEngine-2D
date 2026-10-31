// FemboyEngine2D - plugin authoring helpers.
#pragma once

#include "Engine/Core/Log.hpp"
#include "Engine/Core/PluginAPI.h"
#include "Engine/Core/Mirror.hpp"

#include <entt/entt.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace Fby::Plugin
{

// What a plugin sees after fby_plugin_load: the host table plus the registry.
struct Host
{
    FbyHostApi* api = nullptr;

    [[nodiscard]] entt::registry& Registry() const
    {
        return *static_cast<entt::registry*>(api->registry);
    }

    void Log(FbyLogLevel level, std::string_view channel, std::string_view message) const
    {
        const std::string channelText(channel);
        const std::string messageText(message);
        api->log(api->user, static_cast<int>(level), channelText.c_str(), messageText.c_str());
    }

    [[nodiscard]] bool AddSystem(std::string_view name, FbyPhase phase, int order,
                                 void (*tick)(void* user, float dt)) const
    {
        const std::string systemName(name);
        return api->add_system(api->user, systemName.c_str(), static_cast<int>(phase), order, tick) != 0;
    }
};

// Per-component identity a plugin publishes. The host compares `layout_hash`
template <typename Component> [[nodiscard]] constexpr FbyComponentInfo Describe(std::string_view name)
{
    return FbyComponentInfo{name.data(), static_cast<std::uint32_t>(sizeof(Component)),
                            static_cast<std::uint32_t>(alignof(Component)), Reflect::LayoutHash<Component>()};
}
}

// Declares one component in the plugin's table. `Type` must be registered with
#define FBY_PLUGIN_COMPONENT(Type, Name) ::Fby::Plugin::Describe<Type>(Name)

// Emits the three exported entry points. `LoadBody` is the body of
#define FBY_DEFINE_PLUGIN(PluginName, PluginVersion, PluginAuthor, BuildInfo, ComponentsArray, LoadBody)               \
    FBY_PLUGIN_ENTRY const FbyPluginInfo* fby_plugin_info(void)                                                        \
    {                                                                                                                  \
        static const FbyPluginInfo info = FbyPluginInfo{static_cast<std::uint32_t>(sizeof(FbyPluginInfo)),             \
                                                        FBY_PLUGIN_ABI_VERSION,                                        \
                                                        PluginName,                                                    \
                                                        PluginVersion,                                                 \
                                                        PluginAuthor,                                                  \
                                                        static_cast<std::uint32_t>(std::size(ComponentsArray)),        \
                                                        (ComponentsArray),                                             \
                                                        BuildInfo};                                                    \
        return &info;                                                                                                  \
    }                                                                                                                  \
    FBY_PLUGIN_ENTRY int fby_plugin_load(FbyHostApi* rawHost)                                                          \
    {                                                                                                                  \
        if (rawHost == nullptr || rawHost->abi_version != FBY_PLUGIN_ABI_VERSION)                                      \
        {                                                                                                              \
            return -1;                                                                                                 \
        }                                                                                                              \
        ::Fby::Plugin::Host host{rawHost};                                                                             \
        (void)host;                                                                                                    \
        LoadBody;                                                                                                      \
        return 0;                                                                                                      \
    }                                                                                                                  \
    FBY_PLUGIN_ENTRY void fby_plugin_unload(void)                                                                      \
    {                                                                                                                  \
    }
