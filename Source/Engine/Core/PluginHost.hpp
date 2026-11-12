// FemboyEngine2D - native plugin host.
#pragma once

#include "Engine/Core/FlatMap.hpp"
#include "Engine/ECS/SystemGraph.hpp"
#include "Engine/Audio/VoicePreset.hpp"
#include "Engine/Core/PluginAPI.h"
#include "Engine/Core/Mirror.hpp"
#include "Engine/UI/AccessibilityTheme.hpp"
#include "Engine/UI/AccessibilityEffects.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Fby::Plugin
{

static_assert(static_cast<int>(Ecs::Phase::PreUpdate) == FBY_PHASE_PRE_UPDATE, "plugin phase ABI drifted");
static_assert(static_cast<int>(Ecs::Phase::FixedUpdate) == FBY_PHASE_FIXED_UPDATE, "plugin phase ABI drifted");
static_assert(static_cast<int>(Ecs::Phase::PostUpdate) == FBY_PHASE_POST_UPDATE, "plugin phase ABI drifted");
static_assert(static_cast<int>(Ecs::Phase::Ui) == FBY_PHASE_UI, "plugin phase ABI drifted");
static_assert(static_cast<int>(Ecs::Phase::Render) == FBY_PHASE_RENDER, "plugin phase ABI drifted");
static_assert(static_cast<int>(Ecs::Phase::Count) == FBY_PHASE_COUNT, "plugin phase ABI drifted");

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
    // `ecsRegistry` is the active ECS registry pointer; it is passed to plugins
    // verbatim and is opaque to the C ABI.
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

    // Optional runtime theme registry. Plugins receive the accessibility table
    // only after this is bound; the editor binds the same registry used by the
    // game project and never by the editor's own palette.
    void BindAccessibilityThemes(Ui::AccessibilityThemeRegistry* InThemes) noexcept
    {
        MThemes = InThemes;
        MAccessibilityApi.struct_size = static_cast<std::uint32_t>(sizeof(FbyAccessibilityApi));
        MAccessibilityApi.abi_version = FBY_PLUGIN_ABI_VERSION;
        MAccessibilityApi.register_theme = &PluginHost::HostRegisterTheme;
        MAccessibilityApi.set_theme = &PluginHost::HostSetTheme;
        MAccessibilityApi.register_palette_extension = &PluginHost::HostRegisterExtension;
        MAccessibilityApi.register_effect = &PluginHost::HostRegisterEffect;
        MAccessibilityApi.set_effect = &PluginHost::HostSetEffect;
        MApi.accessibility =
            (InThemes != nullptr || MExtensions != nullptr || MEffects != nullptr) ? &MAccessibilityApi : nullptr;
    }

    void BindAccessibilityEffects(Ui::AccessibilityExtensionRegistry* InExtensions,
                                  Ui::AccessibilityEffectRegistry* InEffects) noexcept
    {
        MExtensions = InExtensions;
        MEffects = InEffects;
        MAccessibilityApi.struct_size = static_cast<std::uint32_t>(sizeof(FbyAccessibilityApi));
        MAccessibilityApi.abi_version = FBY_PLUGIN_ABI_VERSION;
        MAccessibilityApi.register_theme = &PluginHost::HostRegisterTheme;
        MAccessibilityApi.set_theme = &PluginHost::HostSetTheme;
        MAccessibilityApi.register_palette_extension = &PluginHost::HostRegisterExtension;
        MAccessibilityApi.register_effect = &PluginHost::HostRegisterEffect;
        MAccessibilityApi.set_effect = &PluginHost::HostSetEffect;
        MApi.accessibility =
            (MThemes != nullptr || InExtensions != nullptr || InEffects != nullptr) ? &MAccessibilityApi : nullptr;
    }

    // Optional voice preset registry. The table is exposed through the
    // append-only `voice` pointer, so plugins built for ABI 6 keep loading and
    // a plugin that needs voice checks FbyHostHasVoice().
    void BindVoicePresets(VoicePresetRegistry* InPresets) noexcept
    {
        MVoicePresets = InPresets;
        MVoiceApi.struct_size = static_cast<std::uint32_t>(sizeof(FbyVoiceApi));
        MVoiceApi.abi_version = FBY_VOICE_API_VERSION;
        MVoiceApi.register_preset = &PluginHost::HostRegisterVoicePreset;
        MApi.voice = InPresets != nullptr ? &MVoiceApi : nullptr;
    }

    // Returns false and fills `error` when the library is missing, the ABI does
    bool Load(const std::string& InPath, std::string* InError);

    // Unloads the plugin that came from `path` (its systems leave the graph, its
    // unload entry runs and the library closes) and loads it again. Other plugins
    // are untouched. When `path` is not loaded this behaves like Load().
    bool Reload(const std::string& InPath, std::string* InError);

    // Unloads one plugin. Missing paths return false with an error.
    bool Unload(const std::string& InPath, std::string* InError);

    // Calls fby_plugin_unload on every plugin and closes the libraries.
    void UnloadAll();

    [[nodiscard]] const std::vector<std::unique_ptr<LoadedPlugin>>& Plugins() const
    {
        return MPlugins;
    }
    // The host table handed to plugins.
    [[nodiscard]] const FbyHostApi& Api() const noexcept
    {
        return MApi;
    }

private:
    // Rebuilds the bound graph after a plugin changed its node set. A graph that
    // is left dirty by Add/RemoveUser would stop every phase at Run, so every
    // mutation path calls this and rolls back when it fails.
    [[nodiscard]] bool RebuildGraph(std::string* InError);

    static void* HostAlloc(void* InUser, std::size_t InSize, std::size_t InAlignment);
    static void HostFree(void* InUser, void* InPointer, std::size_t InSize, std::size_t InAlignment);
    static void HostLog(void* InUser, int InLevel, const char* InChannel, const char* InMessage);
    static std::uint64_t HostLayoutHash(void* InUser, const char* InName);
    static int HostAddSystem(void* InUser, const char* InName, int InPhase, int InOrder,
                             void (*InTick)(void* InUser, float InDt));
    static int HostRegisterTheme(void* InUser, const FbyThemeDesc* InTheme);
    static int HostSetTheme(void* InUser, const char* InId);
    static int HostRegisterExtension(void* InUser, const FbyPaletteExtensionDesc* InExtension);
    static int HostRegisterEffect(void* InUser, const FbyEffectDesc* InEffect);
    static int HostSetEffect(void* InUser, const char* InId);
    static int HostRegisterVoicePreset(void* InUser, const FbyVoicePresetDesc* InPreset);
    static void PluginSystemThunk(Ecs::SystemContext& InContext, void* InUser);

    ComponentRegistry& MComponents;
    void* MEcsRegistry = nullptr;
    FbyHostApi MApi{};
    FbyAccessibilityApi MAccessibilityApi{};
    FbyVoiceApi MVoiceApi{};
    VoicePresetRegistry* MVoicePresets = nullptr;
    Ui::AccessibilityThemeRegistry* MThemes = nullptr;
    Ui::AccessibilityExtensionRegistry* MExtensions = nullptr;
    Ui::AccessibilityEffectRegistry* MEffects = nullptr;
    LoadedPlugin* MLoading = nullptr;
    Ecs::SystemGraph* MSystemGraph = nullptr;
    std::vector<std::unique_ptr<LoadedPlugin>> MPlugins;
};
}
