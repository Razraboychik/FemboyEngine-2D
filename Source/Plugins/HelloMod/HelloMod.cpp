// FemboyEngine2D - test plugin.
#include "Engine/ECS/Components.hpp"
#include "Engine/Core/PluginAPI.h"
#include "Engine/Core/Mirror.hpp"

#include <cstdio>
#include <cstring>
#include <iterator>

namespace
{
FbyHostApi* GHost = nullptr;

#if defined(FBY_PLUGIN_BAD_LAYOUT)
constexpr std::uint64_t kTransformHash = Fby::Reflect::LayoutHash<Fby::Transform2D>() ^ 0xDEADBEEFCAFEF00Dull;
constexpr std::uint32_t kTransformSize = sizeof(Fby::Transform2D) + 4;
#else
constexpr std::uint64_t kTransformHash = Fby::Reflect::LayoutHash<Fby::Transform2D>();
constexpr std::uint32_t kTransformSize = sizeof(Fby::Transform2D);
#endif

const FbyComponentInfo kComponents[] = {
    {"Transform2D", kTransformSize, static_cast<std::uint32_t>(alignof(Fby::Transform2D)), kTransformHash},
    {"Velocity", static_cast<std::uint32_t>(sizeof(Fby::Velocity)), static_cast<std::uint32_t>(alignof(Fby::Velocity)),
     Fby::Reflect::LayoutHash<Fby::Velocity>()},
};

#if defined(FBY_PLUGIN_BAD_LAYOUT)
constexpr const char* kPluginName = "hello_mod_bad_layout";
constexpr const char* kPluginVersion = "0.1.0-alpha.1-bad";
#else
constexpr const char* kPluginName = "hello_mod";
constexpr const char* kPluginVersion = "0.1.0-alpha.1";
#endif

const FbyPluginInfo kInfo = {
    static_cast<std::uint32_t>(sizeof(FbyPluginInfo)),
    FBY_PLUGIN_ABI_VERSION,
    kPluginName,
    kPluginVersion,
    "FemboyEngine test suite",
    static_cast<std::uint32_t>(std::size(kComponents)),
    kComponents,
    "clang " __VERSION__,
};

void Log(const char* InMessage)
{
    if (GHost != nullptr && GHost->log != nullptr)
    {
        GHost->log(GHost->user, FBY_LOG_INFO, kPluginName, InMessage);
    }
    else
    {
        std::fprintf(stderr, "[%s] %s\n", kPluginName, InMessage);
    }
}

void FixedTick(void* InUser, float InDt)
{
    (void)InUser;
    static float ScopedElapsed = 0.0f;
    ScopedElapsed += InDt;
    if (ScopedElapsed < 5.0f)
    {
        return;
    }
    ScopedElapsed = 0.0f;

    char ScopedMessage[160]{};
    std::snprintf(ScopedMessage, sizeof(ScopedMessage), "tick after %.2f s (dt %.4f)", 5.0f, static_cast<double>(InDt));
    Log(ScopedMessage);
}
}

FBY_PLUGIN_ENTRY const FbyPluginInfo* fby_plugin_info()
{
    return &kInfo;
}

FBY_PLUGIN_ENTRY int fby_plugin_load(FbyHostApi* InHost)
{
    if (InHost == nullptr || InHost->abi_version != FBY_PLUGIN_ABI_VERSION || InHost->struct_size < sizeof(FbyHostApi))
    {
        return 1;
    }
    GHost = InHost;
    Log("loaded; component layout hashes accepted");

    if (InHost->add_system != nullptr)
    {
        InHost->add_system(InHost->user, "hello_mod_fixed_tick", FBY_PHASE_FIXED_UPDATE, 100, &FixedTick);
    }
    return 0;
}

FBY_PLUGIN_ENTRY void fby_plugin_unload()
{
    Log("unloaded");
    GHost = nullptr;
}
