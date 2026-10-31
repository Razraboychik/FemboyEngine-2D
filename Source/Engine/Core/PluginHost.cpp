#include "Engine/Core/PluginHost.hpp"

#include "Engine/Core/Log.hpp"
#include "Engine/Core/Mirror.hpp"

#include <algorithm>
#include <cstring>
#include <format>
#include <new>
#include <string>

#if defined(_WIN32)
#if !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace Fby::Plugin
{
namespace
{
#if defined(_WIN32)
using LibraryHandle = HMODULE;

LibraryHandle OpenLibrary(const std::string& InPath)
{
    return ::LoadLibraryExA(InPath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
}

void CloseLibrary(LibraryHandle InHandle)
{
    ::FreeLibrary(InHandle);
}

void* FindSymbol(LibraryHandle InHandle, const char* InName)
{
    return reinterpret_cast<void*>(::GetProcAddress(InHandle, InName));
}
#else
using LibraryHandle = void*;

LibraryHandle OpenLibrary(const std::string& path)
{
    return ::dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
}

void CloseLibrary(LibraryHandle handle)
{
    ::dlclose(handle);
}

void* FindSymbol(LibraryHandle handle, const char* name)
{
    return ::dlsym(handle, name);
}
#endif
}

const ComponentType* ComponentRegistry::Find(std::string_view InName) const
{
    const auto ScopedIt = MTypes.find(std::string(InName));
    return ScopedIt != MTypes.end() ? &ScopedIt->second : nullptr;
}

std::uint64_t ComponentRegistry::HashOf(std::string_view InName) const
{
    const ComponentType* ScopedType = Find(InName);
    return ScopedType != nullptr ? ScopedType->layoutHash : 0;
}

PluginHost::PluginHost(ComponentRegistry& InComponents, void* InEcsRegistry)
    : MComponents(InComponents), MEcsRegistry(InEcsRegistry)
{
    MApi.struct_size = static_cast<std::uint32_t>(sizeof(FbyHostApi));
    MApi.abi_version = FBY_PLUGIN_ABI_VERSION;
    MApi.user = this;
    MApi.registry = MEcsRegistry;
    MApi.alloc = &PluginHost::HostAlloc;
    MApi.free = &PluginHost::HostFree;
    MApi.log = &PluginHost::HostLog;
    MApi.component_layout_hash = &PluginHost::HostLayoutHash;
    MApi.add_system = &PluginHost::HostAddSystem;
}

PluginHost::~PluginHost()
{
    UnloadAll();
}

bool PluginHost::Load(const std::string& InPath, std::string* InError)
{
    const auto ScopedFail = [InError](std::string InMessage)
    {
        if (InError != nullptr)
        {
            *InError = std::move(InMessage);
        }
        return false;
    };

    LibraryHandle ScopedLibrary = OpenLibrary(InPath);
    if (ScopedLibrary == nullptr)
    {
        return ScopedFail(std::format("cannot open '{}'", InPath));
    }

    const auto ScopedInfoEntry = reinterpret_cast<FbyPluginInfoFn>(FindSymbol(ScopedLibrary, FBY_PLUGIN_INFO_SYMBOL));
    const auto ScopedLoadEntry = reinterpret_cast<FbyPluginLoadFn>(FindSymbol(ScopedLibrary, FBY_PLUGIN_LOAD_SYMBOL));
    const auto ScopedUnloadEntry =
        reinterpret_cast<FbyPluginUnloadFn>(FindSymbol(ScopedLibrary, FBY_PLUGIN_UNLOAD_SYMBOL));
    if (ScopedInfoEntry == nullptr || ScopedLoadEntry == nullptr || ScopedUnloadEntry == nullptr)
    {
        CloseLibrary(ScopedLibrary);
        return ScopedFail(std::format("'{}' does not export the fby_plugin_* entry points", InPath));
    }

    const FbyPluginInfo* ScopedInfo = ScopedInfoEntry();
    if (ScopedInfo == nullptr || ScopedInfo->struct_size < sizeof(FbyPluginInfo))
    {
        CloseLibrary(ScopedLibrary);
        return ScopedFail(std::format("'{}' returned an invalid plugin descriptor", InPath));
    }
    if (ScopedInfo->abi_version != FBY_PLUGIN_ABI_VERSION)
    {
        const std::uint32_t ScopedPluginAbi = ScopedInfo->abi_version;
        CloseLibrary(ScopedLibrary);
        return ScopedFail(std::format("'{}' was built for plugin ABI {}, host is {}", InPath, ScopedPluginAbi,
                                      FBY_PLUGIN_ABI_VERSION));
    }

    auto ScopedPlugin = std::make_unique<LoadedPlugin>();
    ScopedPlugin->path = InPath;
    ScopedPlugin->name = ScopedInfo->name != nullptr ? ScopedInfo->name : "<unnamed>";
    ScopedPlugin->version = ScopedInfo->version != nullptr ? ScopedInfo->version : "0";
    ScopedPlugin->author = ScopedInfo->author != nullptr ? ScopedInfo->author : "";
    ScopedPlugin->buildInfo = ScopedInfo->build_info != nullptr ? ScopedInfo->build_info : "";
    ScopedPlugin->handle = ScopedLibrary;

    for (std::uint32_t ScopedI = 0; ScopedI < ScopedInfo->component_count; ++ScopedI)
    {
        const FbyComponentInfo& ScopedDescribed = ScopedInfo->components[ScopedI];
        const ComponentType* ScopedHostType =
            MComponents.Find(ScopedDescribed.name != nullptr ? ScopedDescribed.name : "");
        if (ScopedHostType == nullptr)
        {
            // Build the message while the DLL is still mapped: `described.name`
            std::string ScopedMessage = std::format("'{}' uses component '{}', which the host does not know", InPath,
                                                    ScopedDescribed.name != nullptr ? ScopedDescribed.name : "<null>");
            CloseLibrary(ScopedLibrary);
            return ScopedFail(std::move(ScopedMessage));
        }
        if (ScopedHostType->layoutHash != ScopedDescribed.layout_hash)
        {
            std::string ScopedMessage = std::format(
                "layout mismatch for component '{}': plugin hash {:016x}, host hash {:016x} "
                "(size {} vs {}) - rebuild the plugin against this engine",
                ScopedDescribed.name != nullptr ? ScopedDescribed.name : "<null>", ScopedDescribed.layout_hash,
                ScopedHostType->layoutHash, ScopedDescribed.size, ScopedHostType->size);
            CloseLibrary(ScopedLibrary);
            return ScopedFail(std::move(ScopedMessage));
        }
        ScopedPlugin->components.push_back(*ScopedHostType);
    }

    MLoading = ScopedPlugin.get();
    const int ScopedResult = ScopedLoadEntry(&MApi);
    MLoading = nullptr;

    if (ScopedResult != 0)
    {
        CloseLibrary(ScopedLibrary);
        return ScopedFail(std::format("'{}' refused to load (fby_plugin_load returned {})", InPath, ScopedResult));
    }

    // Plugins register through the C ABI during load(); now that the plugin's
    if (MSystemGraph != nullptr && !ScopedPlugin->systems.empty())
    {
        for (PluginSystem& ScopedSystem : ScopedPlugin->systems)
        {
            ScopedSystem.hostUser = MApi.user;
            const int ScopedPhaseIndex = std::clamp(ScopedSystem.phase, 0, static_cast<int>(Ecs::Phase::Count) - 1);
            if (!MSystemGraph->Add(static_cast<Ecs::Phase>(ScopedPhaseIndex), ScopedSystem.name.c_str(),
                                   &PluginHost::PluginSystemThunk, &ScopedSystem))
            {
                for (PluginSystem& ScopedRegistered : ScopedPlugin->systems)
                {
                    MSystemGraph->RemoveUser(&ScopedRegistered);
                }
                CloseLibrary(ScopedLibrary);
                return ScopedFail(std::format("'{}': SystemGraph rejected system '{}'", InPath, ScopedSystem.name));
            }
        }
    }

    LogInfo("plugin", "loaded {} {} ({} components, {} systems) from {}", ScopedPlugin->name, ScopedPlugin->version,
            ScopedPlugin->components.size(), ScopedPlugin->systems.size(), InPath);
    MPlugins.push_back(std::move(ScopedPlugin));
    return true;
}

void PluginHost::UnloadAll()
{
    for (auto& ScopedPlugin : MPlugins)
    {
        if (ScopedPlugin->handle == nullptr)
        {
            continue;
        }
        if (MSystemGraph != nullptr)
        {
            for (PluginSystem& ScopedSystem : ScopedPlugin->systems)
            {
                MSystemGraph->RemoveUser(&ScopedSystem);
            }
        }
        const auto ScopedUnloadEntry = reinterpret_cast<FbyPluginUnloadFn>(
            FindSymbol(static_cast<LibraryHandle>(ScopedPlugin->handle), FBY_PLUGIN_UNLOAD_SYMBOL));
        if (ScopedUnloadEntry != nullptr)
        {
            ScopedUnloadEntry();
        }
        CloseLibrary(static_cast<LibraryHandle>(ScopedPlugin->handle));
        ScopedPlugin->handle = nullptr;
        LogInfo("plugin", "unloaded {}", ScopedPlugin->name);
    }
    MPlugins.clear();
}

void* PluginHost::HostAlloc(void* InUser, std::size_t InSize, std::size_t InAlignment)
{
    (void)InUser;
    if (InAlignment > __STDCPP_DEFAULT_NEW_ALIGNMENT__)
    {
        return ::operator new(InSize, std::align_val_t(InAlignment), std::nothrow);
    }
    return ::operator new(InSize, std::nothrow);
}

void PluginHost::HostFree(void* InUser, void* InPointer, std::size_t InSize, std::size_t InAlignment)
{
    (void)InUser;
    (void)InSize;
    if (InAlignment > __STDCPP_DEFAULT_NEW_ALIGNMENT__)
    {
        ::operator delete(InPointer, std::align_val_t(InAlignment));
        return;
    }
    ::operator delete(InPointer);
}

void PluginHost::HostLog(void* InUser, int InLevel, const char* InChannel, const char* InMessage)
{
    (void)InUser;
    const auto ScopedSeverity = static_cast<LogLevel>(std::clamp(InLevel, 0, 4));
    LogLine(ScopedSeverity, InChannel != nullptr ? InChannel : "plugin", InMessage != nullptr ? InMessage : "");
}

std::uint64_t PluginHost::HostLayoutHash(void* InUser, const char* InName)
{
    auto* ScopedHost = static_cast<PluginHost*>(InUser);
    return InName != nullptr ? ScopedHost->MComponents.HashOf(InName) : 0;
}

int PluginHost::HostAddSystem(void* InUser, const char* InName, int InPhase, int InOrder,
                              void (*InTick)(void* InUser, float InDt))
{
    auto* ScopedHost = static_cast<PluginHost*>(InUser);
    if (ScopedHost->MLoading == nullptr || InTick == nullptr)
    {
        return 0;
    }
    ScopedHost->MLoading->systems.push_back(
        PluginSystem{InName != nullptr ? InName : "plugin_system", InPhase, InOrder, InTick, ScopedHost->MApi.user});
    return 1;
}

void PluginHost::PluginSystemThunk(Ecs::SystemContext& InContext, void* InUser)
{
    auto* ScopedSystem = static_cast<PluginSystem*>(InUser);
    if (ScopedSystem != nullptr && ScopedSystem->tick != nullptr)
    {
        ScopedSystem->tick(ScopedSystem->hostUser, InContext.dt);
    }
}
}
