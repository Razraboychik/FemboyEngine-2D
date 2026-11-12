#include "Engine/Core/PluginHost.hpp"

#include "Engine/Core/Error.hpp"
#include "Engine/Core/Log.hpp"
#include "Engine/Core/Mirror.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
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

[[nodiscard]] Core::Result<const FbyPluginInfo*> ReadPluginInfo(FbyPluginInfoFn InEntry) noexcept
{
    if (InEntry == nullptr)
    {
        return std::unexpected(Core::MakeError(Core::ErrorCode::AbiViolation));
    }
    try
    {
        const FbyPluginInfo* ScopedInfo = InEntry();
        if (ScopedInfo == nullptr || ScopedInfo->struct_size < sizeof(FbyPluginInfo))
        {
            return std::unexpected(Core::MakeError(Core::ErrorCode::InvalidState));
        }
        return ScopedInfo;
    }
    catch (...)
    {
        return std::unexpected(Core::MakeError(Core::ErrorCode::AbiViolation));
    }
}

void InvokePluginUnload(FbyPluginUnloadFn InUnload) noexcept
{
    if (InUnload == nullptr)
    {
        return;
    }
    try
    {
        InUnload();
    }
    catch (...)
    {
        LogWarn("plugin", "fby_plugin_unload threw");
    }
}
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

std::vector<std::filesystem::path> DiscoverPluginFiles(const std::filesystem::path& InDirectory)
{
    std::vector<std::filesystem::path> ScopedFiles;
    std::error_code ScopedError;
    if (!std::filesystem::is_directory(InDirectory, ScopedError))
    {
        return ScopedFiles;
    }
    for (const std::filesystem::directory_entry& ScopedEntry :
         std::filesystem::directory_iterator(InDirectory, ScopedError))
    {
        if (ScopedError)
        {
            break;
        }
        if (!ScopedEntry.is_regular_file(ScopedError))
        {
            ScopedError.clear();
            continue;
        }
        // The extension comparison is case-insensitive so a Windows build that
        // spells .DLL still shows up in the list.
        std::string ScopedExtension = ScopedEntry.path().extension().string();
        std::transform(ScopedExtension.begin(), ScopedExtension.end(), ScopedExtension.begin(),
                       [](unsigned char InCharacter) { return static_cast<char>(std::tolower(InCharacter)); });
        if (ScopedExtension == ".dll" || ScopedExtension == ".so" || ScopedExtension == ".dylib")
        {
            ScopedFiles.push_back(ScopedEntry.path());
        }
    }
    std::sort(ScopedFiles.begin(), ScopedFiles.end());
    return ScopedFiles;
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
    MApi.voice = nullptr;
}

PluginHost::~PluginHost()
{
    UnloadAll();
}

bool PluginHost::RebuildGraph(std::string* InError)
{
    if (MSystemGraph == nullptr || MSystemGraph->Build())
    {
        return true;
    }
    if (InError != nullptr)
    {
        *InError = "system graph rebuild failed (cycle or invalid phase dependency)";
    }
    LogError("plugin", "system graph rebuild failed after a plugin changed its system set");
    return false;
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

    for (const auto& ScopedExisting : MPlugins)
    {
        if (ScopedExisting != nullptr && ScopedExisting->path == InPath)
        {
            return ScopedFail("plugin is already loaded");
        }
    }

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

    const Core::Result<const FbyPluginInfo*> ScopedInfoResult = ReadPluginInfo(ScopedInfoEntry);
    if (!ScopedInfoResult)
    {
        CloseLibrary(ScopedLibrary);
        return ScopedFail(std::format("'{}' returned an invalid plugin descriptor", InPath));
    }
    const FbyPluginInfo* ScopedInfo = *ScopedInfoResult;
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
    int ScopedResult = 1;
    bool ScopedLoadThrew = false;
    try
    {
        ScopedResult = ScopedLoadEntry(&MApi);
    }
    catch (...)
    {
        ScopedLoadThrew = true;
    }
    MLoading = nullptr;

    if (ScopedLoadThrew || ScopedResult != 0)
    {
        if (ScopedLoadThrew)
        {
            InvokePluginUnload(ScopedUnloadEntry);
        }
        if (MThemes != nullptr)
        {
            MThemes->RemoveOwner(ScopedPlugin.get());
        }
        if (MExtensions != nullptr)
        {
            MExtensions->RemoveOwner(ScopedPlugin.get());
        }
        if (MEffects != nullptr)
        {
            MEffects->RemoveOwner(ScopedPlugin.get());
        }
        if (MVoicePresets != nullptr)
        {
            MVoicePresets->RemoveOwner(ScopedPlugin.get());
        }
        CloseLibrary(ScopedLibrary);
        return ScopedFail(
            ScopedLoadThrew ? std::format("'{}' threw from fby_plugin_load", InPath)
                            : std::format("'{}' refused to load (fby_plugin_load returned {})", InPath, ScopedResult));
    }

    const auto ScopedAbortLoaded = [&]()
    {
        if (MSystemGraph != nullptr)
        {
            for (PluginSystem& ScopedSystem : ScopedPlugin->systems)
            {
                MSystemGraph->RemoveUser(&ScopedSystem);
            }
            (void)RebuildGraph(nullptr);
        }
        InvokePluginUnload(ScopedUnloadEntry);
        if (MThemes != nullptr)
        {
            MThemes->RemoveOwner(ScopedPlugin.get());
        }
        if (MExtensions != nullptr)
        {
            MExtensions->RemoveOwner(ScopedPlugin.get());
        }
        if (MEffects != nullptr)
        {
            MEffects->RemoveOwner(ScopedPlugin.get());
        }
        if (MVoicePresets != nullptr)
        {
            MVoicePresets->RemoveOwner(ScopedPlugin.get());
        }
        CloseLibrary(ScopedLibrary);
    };

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
                ScopedAbortLoaded();
                return ScopedFail(std::format("'{}': SystemGraph rejected system '{}'", InPath, ScopedSystem.name));
            }
        }
        std::string ScopedGraphError;
        if (!RebuildGraph(&ScopedGraphError))
        {
            ScopedAbortLoaded();
            return ScopedFail(std::format("'{}': {}", InPath, ScopedGraphError));
        }
    }

    LogInfo("plugin", "loaded {} {} ({} components, {} systems) from {}", ScopedPlugin->name, ScopedPlugin->version,
            ScopedPlugin->components.size(), ScopedPlugin->systems.size(), InPath);
    MPlugins.push_back(std::move(ScopedPlugin));
    return true;
}

bool PluginHost::Unload(const std::string& InPath, std::string* InError)
{
    for (auto ScopedIt = MPlugins.begin(); ScopedIt != MPlugins.end(); ++ScopedIt)
    {
        LoadedPlugin& ScopedPlugin = **ScopedIt;
        if (ScopedPlugin.path != InPath)
        {
            continue;
        }
        if (MSystemGraph != nullptr)
        {
            for (PluginSystem& ScopedSystem : ScopedPlugin.systems)
            {
                MSystemGraph->RemoveUser(&ScopedSystem);
            }
        }
        const auto ScopedUnloadEntry = reinterpret_cast<FbyPluginUnloadFn>(
            FindSymbol(static_cast<LibraryHandle>(ScopedPlugin.handle), FBY_PLUGIN_UNLOAD_SYMBOL));
        InvokePluginUnload(ScopedUnloadEntry);
        if (MThemes != nullptr)
        {
            MThemes->RemoveOwner(&ScopedPlugin);
        }
        if (MExtensions != nullptr)
        {
            MExtensions->RemoveOwner(&ScopedPlugin);
        }
        if (MEffects != nullptr)
        {
            MEffects->RemoveOwner(&ScopedPlugin);
        }
        if (MVoicePresets != nullptr)
        {
            MVoicePresets->RemoveOwner(&ScopedPlugin);
        }
        CloseLibrary(static_cast<LibraryHandle>(ScopedPlugin.handle));
        ScopedPlugin.handle = nullptr;
        MPlugins.erase(ScopedIt);
        (void)RebuildGraph(nullptr);
        LogInfo("plugin", "unloaded {}", InPath);
        return true;
    }
    if (InError != nullptr)
    {
        *InError = "plugin is not loaded";
    }
    return false;
}

std::uint32_t PluginHost::LoadDirectory(const std::filesystem::path& InDirectory)
{
    std::uint32_t ScopedLoaded = 0;
    for (const std::filesystem::path& ScopedPath : DiscoverPluginFiles(InDirectory))
    {
        std::string ScopedError;
        if (Load(ScopedPath.string(), &ScopedError))
        {
            ++ScopedLoaded;
        }
        else
        {
            LogWarn("plugin", "directory plugin '{}' not loaded: {}", ScopedPath.filename().string(), ScopedError);
        }
    }
    return ScopedLoaded;
}

bool PluginHost::Reload(const std::string& InPath, std::string* InError)
{
    for (auto ScopedIt = MPlugins.begin(); ScopedIt != MPlugins.end(); ++ScopedIt)
    {
        LoadedPlugin& ScopedPlugin = **ScopedIt;
        if (ScopedPlugin.path != InPath)
        {
            continue;
        }
        if (MSystemGraph != nullptr)
        {
            for (PluginSystem& ScopedSystem : ScopedPlugin.systems)
            {
                MSystemGraph->RemoveUser(&ScopedSystem);
            }
        }
        const auto ScopedUnloadEntry = reinterpret_cast<FbyPluginUnloadFn>(
            FindSymbol(static_cast<LibraryHandle>(ScopedPlugin.handle), FBY_PLUGIN_UNLOAD_SYMBOL));
        InvokePluginUnload(ScopedUnloadEntry);
        if (MThemes != nullptr)
        {
            MThemes->RemoveOwner(&ScopedPlugin);
        }
        if (MExtensions != nullptr)
        {
            MExtensions->RemoveOwner(&ScopedPlugin);
        }
        if (MEffects != nullptr)
        {
            MEffects->RemoveOwner(&ScopedPlugin);
        }
        if (MVoicePresets != nullptr)
        {
            MVoicePresets->RemoveOwner(&ScopedPlugin);
        }
        CloseLibrary(static_cast<LibraryHandle>(ScopedPlugin.handle));
        ScopedPlugin.handle = nullptr;
        MPlugins.erase(ScopedIt);
        // The removed nodes leave the graph dirty even when the following Load
        // fails, so the surviving systems must be rebuilt before Load runs.
        (void)RebuildGraph(nullptr);
        LogInfo("plugin", "reloading {}", InPath);
        return Load(InPath, InError);
    }
    return Load(InPath, InError);
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
        InvokePluginUnload(ScopedUnloadEntry);
        if (MThemes != nullptr)
        {
            MThemes->RemoveOwner(ScopedPlugin.get());
        }
        if (MExtensions != nullptr)
        {
            MExtensions->RemoveOwner(ScopedPlugin.get());
        }
        if (MEffects != nullptr)
        {
            MEffects->RemoveOwner(ScopedPlugin.get());
        }
        if (MVoicePresets != nullptr)
        {
            MVoicePresets->RemoveOwner(ScopedPlugin.get());
        }
        CloseLibrary(static_cast<LibraryHandle>(ScopedPlugin->handle));
        ScopedPlugin->handle = nullptr;
        LogInfo("plugin", "unloaded {}", ScopedPlugin->name);
    }
    MPlugins.clear();
    // Unloading happens at shutdown, but a later Run still needs a compiled
    // order; rebuilding here keeps the graph usable after a partial unload.
    (void)RebuildGraph(nullptr);
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

int PluginHost::HostRegisterTheme(void* InUser, const FbyThemeDesc* InTheme)
{
    auto* ScopedHost = static_cast<PluginHost*>(InUser);
    if (ScopedHost == nullptr || ScopedHost->MThemes == nullptr || ScopedHost->MLoading == nullptr ||
        InTheme == nullptr || InTheme->struct_size < sizeof(FbyThemeDesc) || InTheme->id == nullptr ||
        InTheme->display_name == nullptr)
    {
        return 0;
    }
    Ui::AccessibilityTheme ScopedTheme{};
    std::snprintf(ScopedTheme.Id, sizeof(ScopedTheme.Id), "%s", InTheme->id);
    std::snprintf(ScopedTheme.DisplayName, sizeof(ScopedTheme.DisplayName), "%s", InTheme->display_name);
    for (std::size_t ScopedI = 0; ScopedI < Ui::kAccessibilityPaletteSlots; ++ScopedI)
    {
        ScopedTheme.Colors[ScopedI] = Color32{InTheme->colors[ScopedI]};
    }
    ScopedTheme.Text.Color = Color32{InTheme->text_color};
    ScopedTheme.Text.Outline = Color32{InTheme->text_outline_color};
    ScopedTheme.Text.Opacity = std::clamp(InTheme->text_opacity, 0.0f, 1.0f);
    ScopedTheme.Text.Contrast = std::clamp(InTheme->text_contrast, 0.5f, 2.0f);
    ScopedTheme.Text.OutlineSize = std::clamp(InTheme->text_outline_size, 0.0f, 4.0f);
    ScopedTheme.Text.FontScale = std::clamp(InTheme->text_font_scale, 0.5f, 3.0f);
    std::string ScopedError;
    if (!ScopedHost->MThemes->Register(ScopedTheme, ScopedHost->MLoading, &ScopedError))
    {
        LogWarn("plugin", "theme '{}' rejected: {}", InTheme->id, ScopedError);
        return 0;
    }
    return 1;
}

int PluginHost::HostRegisterExtension(void* InUser, const FbyPaletteExtensionDesc* InExtension)
{
    auto* ScopedHost = static_cast<PluginHost*>(InUser);
    if (ScopedHost == nullptr || ScopedHost->MExtensions == nullptr || ScopedHost->MLoading == nullptr ||
        InExtension == nullptr || InExtension->struct_size < sizeof(FbyPaletteExtensionDesc) ||
        InExtension->id == nullptr || InExtension->display_name == nullptr)
    {
        return 0;
    }
    Ui::AccessibilityPaletteExtension ScopedPalette{};
    std::snprintf(ScopedPalette.Id, sizeof(ScopedPalette.Id), "%s", InExtension->id);
    std::snprintf(ScopedPalette.DisplayName, sizeof(ScopedPalette.DisplayName), "%s", InExtension->display_name);
    for (std::size_t ScopedI = 0; ScopedI < Ui::kAccessibilityExtensionColors; ++ScopedI)
    {
        ScopedPalette.Colors[ScopedI] = Color32{InExtension->colors[ScopedI]};
    }
    return ScopedHost->MExtensions->Register(ScopedPalette, ScopedHost->MLoading) ? 1 : 0;
}

int PluginHost::HostRegisterEffect(void* InUser, const FbyEffectDesc* InEffect)
{
    auto* ScopedHost = static_cast<PluginHost*>(InUser);
    if (ScopedHost == nullptr || ScopedHost->MEffects == nullptr || ScopedHost->MLoading == nullptr ||
        InEffect == nullptr || InEffect->struct_size < sizeof(FbyEffectDesc) || InEffect->id == nullptr ||
        InEffect->display_name == nullptr)
    {
        return 0;
    }
    Ui::AccessibilityEffectProfile ScopedEffect{};
    std::snprintf(ScopedEffect.Id, sizeof(ScopedEffect.Id), "%s", InEffect->id);
    std::snprintf(ScopedEffect.DisplayName, sizeof(ScopedEffect.DisplayName), "%s", InEffect->display_name);
    ScopedEffect.Kind = InEffect->kind == 1   ? Ui::AccessibilityEffectKind::Thermal
                        : InEffect->kind == 2 ? Ui::AccessibilityEffectKind::Alcohol
                                              : Ui::AccessibilityEffectKind::Nvg;
    ScopedEffect.Intensity = std::clamp(InEffect->intensity, 0.0f, 1.0f);
    ScopedEffect.Blur = std::clamp(InEffect->blur, 0.0f, 16.0f);
    ScopedEffect.Sharpness = std::clamp(InEffect->sharpness, 0.0f, 4.0f);
    ScopedEffect.Pattern = static_cast<Ui::NvgPattern>(std::min<std::uint32_t>(InEffect->pattern, 4));
    ScopedEffect.PatternScale = std::clamp(InEffect->pattern_scale, 0.1f, 8.0f);
    ScopedEffect.Generation = static_cast<std::uint8_t>(std::min<std::uint32_t>(InEffect->generation, 255));
    ScopedEffect.Tint = Color32{InEffect->tint};
    ScopedEffect.TagCount =
        static_cast<std::uint8_t>(std::min<std::uint32_t>(InEffect->tag_count, Ui::kAccessibilityEffectTags));
    for (std::size_t ScopedI = 0; ScopedI < ScopedEffect.TagCount; ++ScopedI)
    {
        std::snprintf(ScopedEffect.Tags[ScopedI].Tag, sizeof(ScopedEffect.Tags[ScopedI].Tag), "%s",
                      InEffect->tags[ScopedI]);
    }
    ScopedEffect.AlcoholEffectCount =
        static_cast<std::uint8_t>(std::min<std::uint32_t>(InEffect->alcohol_effect_count, Ui::kMaxAlcoholEffects));
    for (std::size_t ScopedI = 0; ScopedI < ScopedEffect.AlcoholEffectCount; ++ScopedI)
    {
        ScopedEffect.AlcoholEffects[ScopedI].Effect =
            static_cast<Ui::AlcoholEffect>(std::min<std::uint32_t>(InEffect->alcohol_effects[ScopedI].effect, 9));
        ScopedEffect.AlcoholEffects[ScopedI].Intensity =
            std::clamp(InEffect->alcohol_effects[ScopedI].intensity, 0.0f, 1.0f);
        ScopedEffect.AlcoholEffects[ScopedI].Frequency =
            std::clamp(InEffect->alcohol_effects[ScopedI].frequency, 0.1f, 8.0f);
    }
    for (std::size_t ScopedI = 0; ScopedI < Ui::kAccessibilityTemperatureBands; ++ScopedI)
    {
        std::snprintf(ScopedEffect.Temperatures[ScopedI].Tag, sizeof(ScopedEffect.Temperatures[ScopedI].Tag), "%s",
                      InEffect->temperatures[ScopedI].tag);
        ScopedEffect.Temperatures[ScopedI].Color = Color32{InEffect->temperatures[ScopedI].color};
    }
    return ScopedHost->MEffects->Register(ScopedEffect, ScopedHost->MLoading) ? 1 : 0;
}

int PluginHost::HostSetEffect(void* InUser, const char* InId)
{
    auto* ScopedHost = static_cast<PluginHost*>(InUser);
    if (ScopedHost == nullptr || ScopedHost->MEffects == nullptr || InId == nullptr)
    {
        return 0;
    }
    return ScopedHost->MEffects->SetActive(InId) ? 1 : 0;
}

int PluginHost::HostRegisterVoicePreset(void* InUser, const FbyVoicePresetDesc* InPreset)
{
    static_assert(static_cast<int>(VoiceEffectKind::None) == FBY_VOICE_EFFECT_NONE, "voice effect ABI drifted");
    static_assert(static_cast<int>(VoiceEffectKind::Whisper) == FBY_VOICE_EFFECT_WHISPER, "voice effect ABI drifted");
    static_assert(static_cast<int>(VoicePresetCategory::Scene) == FBY_VOICE_CATEGORY_SCENE,
                  "voice category ABI drifted");
    auto* ScopedHost = static_cast<PluginHost*>(InUser);
    if (ScopedHost == nullptr || ScopedHost->MVoicePresets == nullptr || ScopedHost->MLoading == nullptr ||
        InPreset == nullptr)
    {
        return 0;
    }
    if (InPreset->struct_size < sizeof(FbyVoicePresetDesc) || InPreset->id == nullptr ||
        InPreset->name_key == nullptr || InPreset->slot_count == 0 || InPreset->slot_count > FBY_VOICE_SLOT_COUNT)
    {
        return 0;
    }
    VoicePresetDesc ScopedDesc{};
    std::snprintf(ScopedDesc.Id, sizeof(ScopedDesc.Id), "%s", InPreset->id);
    std::snprintf(ScopedDesc.NameKey, sizeof(ScopedDesc.NameKey), "%s", InPreset->name_key);
    ScopedDesc.Category = static_cast<VoicePresetCategory>(InPreset->category);
    ScopedDesc.Flags = InPreset->flags;
    ScopedDesc.MinIntensity = InPreset->min_intensity;
    ScopedDesc.MaxIntensity = InPreset->max_intensity;
    ScopedDesc.Profile.EnableConditions = InPreset->enable_conditions != 0;
    for (std::uint32_t ScopedSlot = 0; ScopedSlot < InPreset->slot_count; ++ScopedSlot)
    {
        const FbyVoiceEffectSlotDesc& ScopedSource = InPreset->slots[ScopedSlot];
        if (ScopedSource.kind == FBY_VOICE_EFFECT_NONE ||
            ScopedSource.kind > static_cast<std::uint32_t>(VoiceEffectKind::Whisper))
        {
            return 0;
        }
        VoiceEffectSlot ScopedValue{};
        ScopedValue.Kind = static_cast<VoiceEffectKind>(ScopedSource.kind);
        ScopedValue.Mix = ScopedSource.mix;
        ScopedValue.Mute.FadeSeconds = ScopedSource.mute.fade_seconds;
        ScopedValue.Muffle.LowCutHz = ScopedSource.muffle.low_cut_hz;
        ScopedValue.Muffle.HighCutHz = ScopedSource.muffle.high_cut_hz;
        ScopedValue.Muffle.Resonance = ScopedSource.muffle.resonance;
        ScopedValue.Muffle.DriveDb = ScopedSource.muffle.drive_db;
        ScopedValue.Muffle.WobbleHz = ScopedSource.muffle.wobble_hz;
        ScopedValue.Muffle.WobbleDepth = ScopedSource.muffle.wobble_depth;
        ScopedValue.Muffle.BreathLevel = ScopedSource.muffle.breath_level;
        ScopedValue.Underwater.LowCutHz = ScopedSource.underwater.low_cut_hz;
        ScopedValue.Underwater.HighCutHz = ScopedSource.underwater.high_cut_hz;
        ScopedValue.Underwater.Resonance = ScopedSource.underwater.resonance;
        ScopedValue.Underwater.WowHz = ScopedSource.underwater.wow_hz;
        ScopedValue.Underwater.WowDepth = ScopedSource.underwater.wow_depth;
        ScopedValue.Radio.LowCutHz = ScopedSource.radio.low_cut_hz;
        ScopedValue.Radio.HighCutHz = ScopedSource.radio.high_cut_hz;
        ScopedValue.Radio.StaticLevel = ScopedSource.radio.static_level;
        ScopedValue.Radio.CrackleRate = ScopedSource.radio.crackle_rate;
        ScopedValue.Radio.Squelch = ScopedSource.radio.squelch;
        ScopedValue.Radio.DriveDb = ScopedSource.radio.drive_db;
        ScopedValue.Radio.MuteWhenSquelched = ScopedSource.radio.mute_when_squelched != 0;
        ScopedValue.Pitch.Semitones = ScopedSource.pitch.semitones;
        ScopedValue.Pitch.WindowMs = ScopedSource.pitch.window_ms;
        ScopedValue.Formant.Semitones = ScopedSource.formant.semitones;
        ScopedValue.Formant.StrengthDb = ScopedSource.formant.strength_db;
        ScopedValue.Distortion.DriveDb = ScopedSource.distortion.drive_db;
        ScopedValue.Distortion.LowCutHz = ScopedSource.distortion.low_cut_hz;
        ScopedValue.Distortion.HighCutHz = ScopedSource.distortion.high_cut_hz;
        ScopedValue.Distortion.BitDepth = ScopedSource.distortion.bit_depth;
        ScopedValue.RingModulation.FrequencyHz = ScopedSource.ring_modulation.frequency_hz;
        ScopedValue.RingModulation.Depth = ScopedSource.ring_modulation.depth;
        ScopedValue.Whisper.NoiseLevel = ScopedSource.whisper.noise_level;
        ScopedValue.Whisper.LowCutHz = ScopedSource.whisper.low_cut_hz;
        ScopedValue.Whisper.HighCutHz = ScopedSource.whisper.high_cut_hz;
        if (!ScopedDesc.Profile.BaseChain.Add(ScopedValue))
        {
            return 0;
        }
    }
    std::string ScopedError;
    if (!ScopedHost->MVoicePresets->Add(ScopedDesc, ScopedHost->MLoading, &ScopedError))
    {
        LogWarn("plugin", "voice preset '{}' rejected: {}", InPreset->id, ScopedError);
        return 0;
    }
    return 1;
}

int PluginHost::HostSetTheme(void* InUser, const char* InId)
{
    auto* ScopedHost = static_cast<PluginHost*>(InUser);
    if (ScopedHost == nullptr || ScopedHost->MThemes == nullptr || InId == nullptr)
    {
        return 0;
    }
    std::string ScopedError;
    if (!ScopedHost->MThemes->SetActive(InId, &ScopedError))
    {
        LogWarn("plugin", "theme '{}' rejected: {}", InId, ScopedError);
        return 0;
    }
    return 1;
}

int PluginHost::HostAddSystem(void* InUser, const char* InName, int InPhase, int InOrder,
                              void (*InTick)(void* InUser, float InDt))
{
    auto* ScopedHost = static_cast<PluginHost*>(InUser);
    if (ScopedHost->MLoading == nullptr || InTick == nullptr || InPhase < 0 || InPhase >= FBY_PHASE_COUNT)
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
        try
        {
            ScopedSystem->tick(ScopedSystem->hostUser, InContext.dt);
        }
        catch (...)
        {
            InContext.jobs.ReportFault("plugin tick");
        }
    }
}
}
