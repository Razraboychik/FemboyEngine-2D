// FemboyEngine2D - GOG Galaxy provider.
#include "PlatformAPI.h"

#include <cstdint>
#include <cstdio>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <cstdlib>
#else
#include <dlfcn.h>
#include <cstdlib>
#endif

namespace
{
using GogInitializeFn = int(FBY_PLATFORM_CALL*)(const char*, const char*);
using GogVoidFn = void(FBY_PLATFORM_CALL*)();
using GogIntFn = int(FBY_PLATFORM_CALL*)();
using GogUserIdFn = std::uint64_t(FBY_PLATFORM_CALL*)();
using GogNameFn = int(FBY_PLATFORM_CALL*)(const char*);
using GogStatIntFn = int(FBY_PLATFORM_CALL*)(const char*, std::int64_t);
using GogStatFloatFn = int(FBY_PLATFORM_CALL*)(const char*, double);
using GogReadCloudFn = int(FBY_PLATFORM_CALL*)(const char*, void*, std::uint32_t, std::uint32_t*);
using GogWriteCloudFn = int(FBY_PLATFORM_CALL*)(const char*, const void*, std::uint32_t);
using GogPendingFn = std::uint32_t(FBY_PLATFORM_CALL*)();

struct GogFlatApi
{
    GogInitializeFn initialize = nullptr;
    GogVoidFn shutdown = nullptr;
    GogVoidFn processData = nullptr;
    GogUserIdFn signedInUser = nullptr;
    GogNameFn unlockAchievement = nullptr;
    GogStatIntFn setStatInt = nullptr;
    GogStatFloatFn setStatFloat = nullptr;
    GogIntFn storeStats = nullptr;
    GogReadCloudFn readCloud = nullptr;
    GogWriteCloudFn writeCloud = nullptr;
    GogNameFn deleteCloud = nullptr;
    GogNameFn showOverlay = nullptr;
    GogPendingFn pendingCloudWrites = nullptr;
};

struct GogState
{
    void* library = nullptr;
    GogFlatApi api{};
    FbyPlatformCallbacks callbacks{};
    std::uint64_t userId = 0;
    bool initialized = false;
    bool resolved = false;
};

GogState GState{};

void LogGog(int InLevel, const char* InMessage) noexcept
{
    if (GState.callbacks.onLog != nullptr)
    {
        GState.callbacks.onLog(GState.callbacks.user, InLevel, InMessage);
    }
}

template <typename T> [[nodiscard]] bool ResolveFlat(void* InLibrary, const char* InName, T& OutFunction) noexcept
{
#if defined(_WIN32)
    OutFunction = reinterpret_cast<T>(GetProcAddress(static_cast<HMODULE>(InLibrary), InName));
#else
    OutFunction = reinterpret_cast<T>(dlsym(InLibrary, InName));
#endif
    return OutFunction != nullptr;
}

[[nodiscard]] void* OpenGalaxyLibrary() noexcept
{
    // The official wrapper can be pointed at with GALAXY_SDK_PATH; that keeps a
    if (const char* ScopedDirectory = std::getenv("GALAXY_SDK_PATH");
        ScopedDirectory != nullptr && ScopedDirectory[0] != '\0')
    {
        char ScopedPath[1024]{};
#if defined(_WIN32)
        std::snprintf(ScopedPath, sizeof(ScopedPath), "%s\\galaxy64.dll", ScopedDirectory);
        if (HMODULE ScopedModule = LoadLibraryA(ScopedPath); ScopedModule != nullptr)
        {
            return ScopedModule;
        }
#elif defined(__APPLE__)
        std::snprintf(ScopedPath, sizeof(ScopedPath), "%s/libgalaxy64.dylib", ScopedDirectory);
        if (void* ScopedModule = dlopen(ScopedPath, RTLD_NOW | RTLD_LOCAL); ScopedModule != nullptr)
        {
            return ScopedModule;
        }
#else
        std::snprintf(ScopedPath, sizeof(ScopedPath), "%s/libgalaxy64.so", ScopedDirectory);
        if (void* ScopedModule = dlopen(ScopedPath, RTLD_NOW | RTLD_LOCAL); ScopedModule != nullptr)
        {
            return ScopedModule;
        }
#endif
    }

#if defined(_WIN32)
    const char* const ScopedCandidates[] = {"galaxy64.dll", "Galaxy64.dll", "galaxy.dll"};
    for (const char* ScopedCandidate : ScopedCandidates)
    {
        if (HMODULE ScopedModule = LoadLibraryA(ScopedCandidate); ScopedModule != nullptr)
        {
            return ScopedModule;
        }
    }
#elif defined(__APPLE__)
    const char* const ScopedCandidates[] = {"libgalaxy64.dylib", "@rpath/libgalaxy64.dylib"};
    for (const char* ScopedCandidate : ScopedCandidates)
    {
        if (void* ScopedModule = dlopen(ScopedCandidate, RTLD_NOW | RTLD_LOCAL); ScopedModule != nullptr)
        {
            return ScopedModule;
        }
    }
#else
    const char* const ScopedCandidates[] = {"libgalaxy64.so", "libGalaxy64.so"};
    for (const char* ScopedCandidate : ScopedCandidates)
    {
        if (void* ScopedModule = dlopen(ScopedCandidate, RTLD_NOW | RTLD_LOCAL); ScopedModule != nullptr)
        {
            return ScopedModule;
        }
    }
#endif
    return nullptr;
}

[[nodiscard]] bool ResolveAll(void* InLibrary) noexcept
{
    bool ScopedOk = true;
    ScopedOk &= ResolveFlat(InLibrary, "fby_gog_initialize", GState.api.initialize);
    ScopedOk &= ResolveFlat(InLibrary, "fby_gog_shutdown", GState.api.shutdown);
    ScopedOk &= ResolveFlat(InLibrary, "fby_gog_process_data", GState.api.processData);
    ScopedOk &= ResolveFlat(InLibrary, "fby_gog_signed_in_user", GState.api.signedInUser);
    ScopedOk &= ResolveFlat(InLibrary, "fby_gog_unlock_achievement", GState.api.unlockAchievement);
    ScopedOk &= ResolveFlat(InLibrary, "fby_gog_set_stat_int", GState.api.setStatInt);
    ScopedOk &= ResolveFlat(InLibrary, "fby_gog_set_stat_float", GState.api.setStatFloat);
    ScopedOk &= ResolveFlat(InLibrary, "fby_gog_store_stats", GState.api.storeStats);
    ScopedOk &= ResolveFlat(InLibrary, "fby_gog_read_cloud", GState.api.readCloud);
    ScopedOk &= ResolveFlat(InLibrary, "fby_gog_write_cloud", GState.api.writeCloud);
    ScopedOk &= ResolveFlat(InLibrary, "fby_gog_delete_cloud", GState.api.deleteCloud);
    ScopedOk &= ResolveFlat(InLibrary, "fby_gog_show_overlay", GState.api.showOverlay);
    ScopedOk &= ResolveFlat(InLibrary, "fby_gog_pending_cloud_writes", GState.api.pendingCloudWrites);
    return ScopedOk;
}

[[nodiscard]] bool EnsureLibrary() noexcept
{
    if (GState.resolved)
    {
        return GState.library != nullptr;
    }
    GState.library = OpenGalaxyLibrary();
    if (GState.library == nullptr)
    {
        LogGog(1, "Galaxy shim not found (galaxy64.dll / libgalaxy64.so / libgalaxy64.dylib)");
        GState.resolved = true;
        return false;
    }
    if (!ResolveAll(GState.library))
    {
        LogGog(2, "Galaxy shim is present but the flat fby_gog_* ABI is incomplete");
        GState.resolved = true;
        return false;
    }
    GState.resolved = true;
    return true;
}

[[nodiscard]] FbyPlatformResult FromGogResult(int InResult) noexcept
{
    switch (InResult)
    {
        case 0:
            return FbyPlatformResult_Ok;
        case 1:
            return FbyPlatformResult_Pending;
        case 2:
            return FbyPlatformResult_NotInitialized;
        case 3:
            return FbyPlatformResult_NotAvailable;
        case 4:
            return FbyPlatformResult_InvalidArgument;
        case 5:
            return FbyPlatformResult_BufferTooSmall;
        case 6:
            return FbyPlatformResult_NotFound;
        default:
            return FbyPlatformResult_BackendError;
    }
}

[[nodiscard]] bool Ready() noexcept
{
    return GState.initialized;
}

FbyPlatformResult FBY_PLATFORM_CALL GogInitialize(const FbyPlatformInitDesc* InDesc,
                                                  const FbyPlatformCallbacks* InCallbacks)
{
    if (GState.initialized)
    {
        return FbyPlatformResult_Ok;
    }
    if (InCallbacks != nullptr)
    {
        GState.callbacks = *InCallbacks;
    }
    if (!EnsureLibrary())
    {
        return FbyPlatformResult_NotAvailable;
    }
    if (InDesc == nullptr || InDesc->clientId == nullptr || InDesc->clientSecret == nullptr)
    {
        LogGog(2, "GOG Galaxy requires clientId and clientSecret in FbyPlatformInitDesc");
        return FbyPlatformResult_InvalidArgument;
    }
    const FbyPlatformResult ScopedResult =
        FromGogResult(GState.api.initialize(InDesc->clientId, InDesc->clientSecret));
    if (ScopedResult != FbyPlatformResult_Ok)
    {
        LogGog(2, "fby_gog_initialize failed");
        return ScopedResult;
    }
    GState.initialized = true;
    GState.userId = GState.api.signedInUser != nullptr ? GState.api.signedInUser() : 0;
    if (GState.callbacks.onAuth != nullptr)
    {
        GState.callbacks.onAuth(GState.callbacks.user, GState.userId, GState.userId != 0 ? 1 : 0);
    }
    LogGog(0, "GOG Galaxy shim initialised");
    return FbyPlatformResult_Ok;
}

void FBY_PLATFORM_CALL GogShutdown()
{
    if (GState.initialized && GState.api.shutdown != nullptr)
    {
        GState.api.shutdown();
    }
    GState.initialized = false;
    GState.userId = 0;
}

void FBY_PLATFORM_CALL GogPump()
{
    if (Ready() && GState.api.processData != nullptr)
    {
        GState.api.processData();
    }
}

FbyPlatformResult FBY_PLATFORM_CALL GogUnlock(const char* InApiName)
{
    if (!Ready() || InApiName == nullptr)
    {
        return FbyPlatformResult_NotInitialized;
    }
    const FbyPlatformResult ScopedResult = FromGogResult(GState.api.unlockAchievement(InApiName));
    if (ScopedResult == FbyPlatformResult_Ok || ScopedResult == FbyPlatformResult_Pending)
    {
        if (GState.callbacks.onAchievement != nullptr)
        {
            GState.callbacks.onAchievement(GState.callbacks.user, InApiName, 1);
        }
    }
    return ScopedResult;
}

FbyPlatformResult FBY_PLATFORM_CALL GogSetStatInt(const char* InApiName, std::int64_t InValue)
{
    if (!Ready() || InApiName == nullptr)
    {
        return FbyPlatformResult_NotInitialized;
    }
    return FromGogResult(GState.api.setStatInt(InApiName, InValue));
}

FbyPlatformResult FBY_PLATFORM_CALL GogSetStatFloat(const char* InApiName, double InValue)
{
    if (!Ready() || InApiName == nullptr)
    {
        return FbyPlatformResult_NotInitialized;
    }
    return FromGogResult(GState.api.setStatFloat(InApiName, InValue));
}

FbyPlatformResult FBY_PLATFORM_CALL GogStoreStats()
{
    if (!Ready())
    {
        return FbyPlatformResult_NotInitialized;
    }
    return FromGogResult(GState.api.storeStats());
}

std::uint64_t FBY_PLATFORM_CALL GogUserId()
{
    return Ready() ? GState.userId : 0;
}

FbyPlatformResult FBY_PLATFORM_CALL GogReadCloud(const char* InName, void* OutData, std::uint32_t InCapacity,
                                                 std::uint32_t* OutSize)
{
    if (!Ready() || InName == nullptr)
    {
        return FbyPlatformResult_NotInitialized;
    }
    if (OutSize == nullptr)
    {
        return FbyPlatformResult_InvalidArgument;
    }
    std::uint32_t ScopedSize = 0;
    const FbyPlatformResult ScopedResult =
        FromGogResult(GState.api.readCloud(InName, OutData, InCapacity, &ScopedSize));
    *OutSize = ScopedSize;
    if (ScopedResult == FbyPlatformResult_Ok && GState.callbacks.onCloud != nullptr)
    {
        GState.callbacks.onCloud(GState.callbacks.user, InName, 1, OutData, ScopedSize);
    }
    return ScopedResult;
}

FbyPlatformResult FBY_PLATFORM_CALL GogWriteCloud(const char* InName, const void* InData, std::uint32_t InSize)
{
    if (!Ready() || InName == nullptr)
    {
        return FbyPlatformResult_NotInitialized;
    }
    const FbyPlatformResult ScopedResult = FromGogResult(GState.api.writeCloud(InName, InData, InSize));
    if ((ScopedResult == FbyPlatformResult_Ok || ScopedResult == FbyPlatformResult_Pending) &&
        GState.callbacks.onCloud != nullptr)
    {
        GState.callbacks.onCloud(GState.callbacks.user, InName, 1, InData, InSize);
    }
    return ScopedResult;
}

FbyPlatformResult FBY_PLATFORM_CALL GogDeleteCloud(const char* InName)
{
    if (!Ready() || InName == nullptr)
    {
        return FbyPlatformResult_NotInitialized;
    }
    return FromGogResult(GState.api.deleteCloud(InName));
}

FbyPlatformResult FBY_PLATFORM_CALL GogShowOverlay(const char* InDialog)
{
    if (!Ready())
    {
        return FbyPlatformResult_NotInitialized;
    }
    return FromGogResult(GState.api.showOverlay(InDialog));
}

std::uint32_t FBY_PLATFORM_CALL GogPendingCloudWrites()
{
    return Ready() ? GState.api.pendingCloudWrites() : 0;
}

const FbyPlatformApi GGogApi = {
    FBY_PLATFORM_ABI_VERSION,
    static_cast<std::uint32_t>(sizeof(FbyPlatformApi)),
    FbyPlatformKind_Gog,
    "gog-galaxy",
    FbyPlatformCapability_Achievements | FbyPlatformCapability_Stats | FbyPlatformCapability_CloudSave |
        FbyPlatformCapability_Overlay | FbyPlatformCapability_UserIdentity | FbyPlatformCapability_AsyncCloud,
    &GogInitialize,
    &GogShutdown,
    &GogPump,
    &GogUnlock,
    &GogSetStatInt,
    &GogSetStatFloat,
    &GogStoreStats,
    &GogUserId,
    &GogReadCloud,
    &GogWriteCloud,
    &GogDeleteCloud,
    &GogShowOverlay,
    &GogPendingCloudWrites,
};
}

extern "C" FBY_PLATFORM_EXPORT const FbyPlatformApi* FBY_PLATFORM_CALL fby_platform_get_api(std::uint32_t InHostAbi)
{
    return InHostAbi == FBY_PLATFORM_ABI_VERSION ? &GGogApi : nullptr;
}

extern "C" FBY_PLATFORM_EXPORT FbyPlatformResult FBY_PLATFORM_CALL
fby_gog_initialize(const FbyPlatformInitDesc* InDesc, const FbyPlatformCallbacks* InCallbacks)
{
    return GogInitialize(InDesc, InCallbacks);
}
