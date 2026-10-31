// FemboyEngine2D - Steamworks provider.
#include "PlatformAPI.h"

#include <array>
#include <cstdint>
#include <cstring>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace
{
using SteamApiInitFn = int (*)();
using SteamApiShutdownFn = void (*)();
using SteamApiRunCallbacksFn = void (*)();
using SteamInterfaceFn = void* (*)();
using SteamRequestStatsFn = int (*)(void*);
using SteamSetAchievementFn = int (*)(void*, const char*);
using SteamGetAchievementFn = int (*)(void*, const char*, int*);
using SteamStoreStatsFn = int (*)(void*);
using SteamSetStatIntFn = int (*)(void*, const char*, int);
using SteamSetStatFloatFn = int (*)(void*, const char*, float);
using SteamGetSteamIdFn = std::uint64_t (*)(void*);
using SteamFileWriteFn = int (*)(void*, const char*, const void*, int);
using SteamFileReadFn = int (*)(void*, const char*, void*, int);
using SteamGetFileSizeFn = int (*)(void*, const char*);
using SteamFileDeleteFn = int (*)(void*, const char*);
using SteamFileFlushFn = int (*)(void*, const char*);
using SteamActivateOverlayFn = void (*)(void*, const char*);

struct SteamState
{
    void* library = nullptr;
    SteamApiInitFn init = nullptr;
    SteamApiShutdownFn shutdown = nullptr;
    SteamApiRunCallbacksFn runCallbacks = nullptr;
    SteamRequestStatsFn requestStats = nullptr;
    SteamSetAchievementFn setAchievement = nullptr;
    SteamGetAchievementFn getAchievement = nullptr;
    SteamStoreStatsFn storeStats = nullptr;
    SteamSetStatIntFn setStatInt = nullptr;
    SteamSetStatFloatFn setStatFloat = nullptr;
    SteamGetSteamIdFn getSteamId = nullptr;
    SteamFileWriteFn fileWrite = nullptr;
    SteamFileReadFn fileRead = nullptr;
    SteamGetFileSizeFn getFileSize = nullptr;
    SteamFileDeleteFn fileDelete = nullptr;
    SteamFileFlushFn fileFlush = nullptr;
    SteamActivateOverlayFn activateOverlay = nullptr;
    void* userStats = nullptr;
    void* user = nullptr;
    void* remoteStorage = nullptr;
    void* friends = nullptr;
    FbyPlatformCallbacks callbacks{};
    bool initialized = false;
};

SteamState GState{};

template <typename T> [[nodiscard]] bool ResolveSymbol(void* InLibrary, const char* InName, T& OutFunction) noexcept
{
#if defined(_WIN32)
    OutFunction = reinterpret_cast<T>(GetProcAddress(static_cast<HMODULE>(InLibrary), InName));
#else
    OutFunction = reinterpret_cast<T>(dlsym(InLibrary, InName));
#endif
    return OutFunction != nullptr;
}

void* OpenSteamLibrary() noexcept
{
#if defined(_WIN32)
    const char* const ScopedCandidates[] = {"steam_api64.dll", "steam_api.dll"};
    for (const char* ScopedCandidate : ScopedCandidates)
    {
        HMODULE ScopedModule = LoadLibraryA(ScopedCandidate);
        if (ScopedModule != nullptr)
        {
            return ScopedModule;
        }
    }
#elif defined(__APPLE__)
    void* ScopedModule = dlopen("libsteam_api.dylib", RTLD_NOW | RTLD_LOCAL);
    if (ScopedModule == nullptr)
    {
        ScopedModule = dlopen("@rpath/libsteam_api.dylib", RTLD_NOW | RTLD_LOCAL);
    }
    if (ScopedModule != nullptr)
    {
        return ScopedModule;
    }
#else
    void* ScopedModule = dlopen("libsteam_api.so", RTLD_NOW | RTLD_LOCAL);
    if (ScopedModule != nullptr)
    {
        return ScopedModule;
    }
#endif
    return nullptr;
}

template <typename T> [[nodiscard]] T ResolveInterface(const char* const* InNames, std::size_t InCount) noexcept
{
    for (std::size_t ScopedI = 0; ScopedI < InCount; ++ScopedI)
    {
        SteamInterfaceFn ScopedFunction = nullptr;
        if (ResolveSymbol(GState.library, InNames[ScopedI], ScopedFunction) && ScopedFunction != nullptr)
        {
            return reinterpret_cast<T>(ScopedFunction());
        }
    }
    return nullptr;
}

void InvokeLog(const char* InMessage) noexcept
{
    if (GState.callbacks.onLog != nullptr)
    {
        GState.callbacks.onLog(GState.callbacks.user, 0, InMessage);
    }
}

FbyPlatformResult FBY_PLATFORM_CALL SteamInitialize(const FbyPlatformInitDesc* InDesc,
                                                    const FbyPlatformCallbacks* InCallbacks)
{
    if (GState.initialized)
    {
        return FbyPlatformResult_Ok;
    }
    if (GState.library == nullptr)
    {
        GState.library = OpenSteamLibrary();
    }
    if (GState.library == nullptr)
    {
        return FbyPlatformResult_NotAvailable;
    }
    if (!ResolveSymbol(GState.library, "SteamAPI_Init", GState.init) ||
        !ResolveSymbol(GState.library, "SteamAPI_Shutdown", GState.shutdown) ||
        !ResolveSymbol(GState.library, "SteamAPI_RunCallbacks", GState.runCallbacks))
    {
        return FbyPlatformResult_NotAvailable;
    }
    if (InCallbacks != nullptr)
    {
        GState.callbacks = *InCallbacks;
    }
    if (GState.init() == 0)
    {
        InvokeLog("SteamAPI_Init failed: is Steam running and is the AppId valid?");
        return FbyPlatformResult_BackendError;
    }
    static const char* const ScopedUserStatsNames[] = {"SteamAPI_SteamUserStats_v012", "SteamAPI_SteamUserStats_v011",
                                                       "SteamAPI_SteamUserStats_v010"};
    static const char* const ScopedUserNames[] = {"SteamAPI_SteamUser_v023", "SteamAPI_SteamUser_v022",
                                                 "SteamAPI_SteamUser_v021"};
    static const char* const ScopedRemoteNames[] = {"SteamAPI_SteamRemoteStorage_v016",
                                                   "SteamAPI_SteamRemoteStorage_v015",
                                                   "SteamAPI_SteamRemoteStorage_v014"};
    static const char* const ScopedFriendsNames[] = {"SteamAPI_SteamFriends_v018", "SteamAPI_SteamFriends_v017"};
    GState.userStats = ResolveInterface<void*>(ScopedUserStatsNames, std::size(ScopedUserStatsNames));
    GState.user = ResolveInterface<void*>(ScopedUserNames, std::size(ScopedUserNames));
    GState.remoteStorage = ResolveInterface<void*>(ScopedRemoteNames, std::size(ScopedRemoteNames));
    GState.friends = ResolveInterface<void*>(ScopedFriendsNames, std::size(ScopedFriendsNames));

    (void)ResolveSymbol(GState.library, "SteamAPI_ISteamUserStats_RequestCurrentStats", GState.requestStats);
    (void)ResolveSymbol(GState.library, "SteamAPI_ISteamUserStats_SetAchievement", GState.setAchievement);
    (void)ResolveSymbol(GState.library, "SteamAPI_ISteamUserStats_GetAchievement", GState.getAchievement);
    (void)ResolveSymbol(GState.library, "SteamAPI_ISteamUserStats_StoreStats", GState.storeStats);
    (void)ResolveSymbol(GState.library, "SteamAPI_ISteamUserStats_SetStat", GState.setStatInt);
    (void)ResolveSymbol(GState.library, "SteamAPI_ISteamUserStats_SetStatFloat", GState.setStatFloat);
    (void)ResolveSymbol(GState.library, "SteamAPI_ISteamUser_GetSteamID", GState.getSteamId);
    (void)ResolveSymbol(GState.library, "SteamAPI_ISteamRemoteStorage_FileWrite", GState.fileWrite);
    (void)ResolveSymbol(GState.library, "SteamAPI_ISteamRemoteStorage_FileRead", GState.fileRead);
    (void)ResolveSymbol(GState.library, "SteamAPI_ISteamRemoteStorage_GetFileSize", GState.getFileSize);
    (void)ResolveSymbol(GState.library, "SteamAPI_ISteamRemoteStorage_FileDelete", GState.fileDelete);
    (void)ResolveSymbol(GState.library, "SteamAPI_ISteamRemoteStorage_FileFlush", GState.fileFlush);
    (void)ResolveSymbol(GState.library, "SteamAPI_ISteamFriends_ActivateGameOverlay", GState.activateOverlay);
    GState.initialized = true;
    if (GState.requestStats != nullptr && GState.userStats != nullptr)
    {
        (void)GState.requestStats(GState.userStats);
    }
    if (GState.callbacks.onAuth != nullptr)
    {
        GState.callbacks.onAuth(GState.callbacks.user, GState.getSteamId != nullptr && GState.user != nullptr
                                                              ? GState.getSteamId(GState.user)
                                                              : 0,
                                1);
    }
    (void)InDesc;
    return FbyPlatformResult_Ok;
}

void FBY_PLATFORM_CALL SteamShutdown()
{
    if (GState.initialized && GState.shutdown != nullptr)
    {
        GState.shutdown();
    }
    GState.initialized = false;
}

void FBY_PLATFORM_CALL SteamPump()
{
    if (GState.initialized && GState.runCallbacks != nullptr)
    {
        GState.runCallbacks();
    }
}

FbyPlatformResult FBY_PLATFORM_CALL SteamUnlockAchievement(const char* InApiName)
{
    if (!GState.initialized)
    {
        return FbyPlatformResult_NotInitialized;
    }
    if (InApiName == nullptr || GState.userStats == nullptr || GState.setAchievement == nullptr)
    {
        return FbyPlatformResult_NotAvailable;
    }
    if (GState.setAchievement(GState.userStats, InApiName) == 0)
    {
        return FbyPlatformResult_BackendError;
    }
    if (GState.storeStats != nullptr)
    {
        (void)GState.storeStats(GState.userStats);
    }
    if (GState.callbacks.onAchievement != nullptr)
    {
        GState.callbacks.onAchievement(GState.callbacks.user, InApiName, 1);
    }
    return FbyPlatformResult_Ok;
}

FbyPlatformResult FBY_PLATFORM_CALL SteamSetStatInt(const char* InApiName, std::int64_t InValue)
{
    if (!GState.initialized)
    {
        return FbyPlatformResult_NotInitialized;
    }
    if (InApiName == nullptr || GState.userStats == nullptr || GState.setStatInt == nullptr)
    {
        return FbyPlatformResult_NotAvailable;
    }
    const int ScopedValue = InValue > 2147483647ll ? 2147483647 : (InValue < -2147483648ll ? -2147483648 : static_cast<int>(InValue));
    return GState.setStatInt(GState.userStats, InApiName, ScopedValue) != 0 ? FbyPlatformResult_Ok
                                                                             : FbyPlatformResult_BackendError;
}

FbyPlatformResult FBY_PLATFORM_CALL SteamSetStatFloat(const char* InApiName, double InValue)
{
    if (!GState.initialized)
    {
        return FbyPlatformResult_NotInitialized;
    }
    if (InApiName == nullptr || GState.userStats == nullptr || GState.setStatFloat == nullptr)
    {
        return FbyPlatformResult_NotAvailable;
    }
    return GState.setStatFloat(GState.userStats, InApiName, static_cast<float>(InValue)) != 0
               ? FbyPlatformResult_Ok
               : FbyPlatformResult_BackendError;
}

FbyPlatformResult FBY_PLATFORM_CALL SteamStoreStats()
{
    if (!GState.initialized)
    {
        return FbyPlatformResult_NotInitialized;
    }
    if (GState.storeStats == nullptr || GState.userStats == nullptr)
    {
        return FbyPlatformResult_NotAvailable;
    }
    return GState.storeStats(GState.userStats) != 0 ? FbyPlatformResult_Ok : FbyPlatformResult_BackendError;
}

std::uint64_t FBY_PLATFORM_CALL SteamUserId()
{
    if (!GState.initialized || GState.getSteamId == nullptr || GState.user == nullptr)
    {
        return 0;
    }
    return GState.getSteamId(GState.user);
}

FbyPlatformResult FBY_PLATFORM_CALL SteamReadCloud(const char* InName, void* OutData, std::uint32_t InCapacity,
                                                   std::uint32_t* OutSize)
{
    if (!GState.initialized)
    {
        return FbyPlatformResult_NotInitialized;
    }
    if (InName == nullptr || OutSize == nullptr || GState.remoteStorage == nullptr || GState.getFileSize == nullptr ||
        GState.fileRead == nullptr)
    {
        return FbyPlatformResult_NotAvailable;
    }
    const int ScopedSize = GState.getFileSize(GState.remoteStorage, InName);
    if (ScopedSize < 0)
    {
        return FbyPlatformResult_NotFound;
    }
    *OutSize = static_cast<std::uint32_t>(ScopedSize);
    if (OutData == nullptr)
    {
        return FbyPlatformResult_Ok;
    }
    if (static_cast<std::uint32_t>(ScopedSize) > InCapacity)
    {
        return FbyPlatformResult_BufferTooSmall;
    }
    const int ScopedRead = GState.fileRead(GState.remoteStorage, InName, OutData, ScopedSize);
    return ScopedRead == ScopedSize ? FbyPlatformResult_Ok : FbyPlatformResult_BackendError;
}

FbyPlatformResult FBY_PLATFORM_CALL SteamWriteCloud(const char* InName, const void* InData, std::uint32_t InSize)
{
    if (!GState.initialized)
    {
        return FbyPlatformResult_NotInitialized;
    }
    if (InName == nullptr || (InData == nullptr && InSize != 0) || GState.remoteStorage == nullptr ||
        GState.fileWrite == nullptr)
    {
        return FbyPlatformResult_NotAvailable;
    }
    const int ScopedWritten = GState.fileWrite(GState.remoteStorage, InName, InData, static_cast<int>(InSize));
    if (ScopedWritten != static_cast<int>(InSize))
    {
        return FbyPlatformResult_BackendError;
    }
    if (GState.fileFlush != nullptr)
    {
        (void)GState.fileFlush(GState.remoteStorage, InName);
    }
    if (GState.callbacks.onCloud != nullptr)
    {
        GState.callbacks.onCloud(GState.callbacks.user, InName, 1, nullptr, 0);
    }
    return FbyPlatformResult_Ok;
}

FbyPlatformResult FBY_PLATFORM_CALL SteamDeleteCloud(const char* InName)
{
    if (!GState.initialized)
    {
        return FbyPlatformResult_NotInitialized;
    }
    if (InName == nullptr || GState.remoteStorage == nullptr || GState.fileDelete == nullptr)
    {
        return FbyPlatformResult_NotAvailable;
    }
    return GState.fileDelete(GState.remoteStorage, InName) != 0 ? FbyPlatformResult_Ok
                                                               : FbyPlatformResult_BackendError;
}

FbyPlatformResult FBY_PLATFORM_CALL SteamShowOverlay(const char* InDialog)
{
    if (!GState.initialized)
    {
        return FbyPlatformResult_NotInitialized;
    }
    if (GState.friends == nullptr || GState.activateOverlay == nullptr)
    {
        return FbyPlatformResult_NotAvailable;
    }
    GState.activateOverlay(GState.friends, InDialog == nullptr ? "achievements" : InDialog);
    return FbyPlatformResult_Ok;
}

std::uint32_t FBY_PLATFORM_CALL SteamPendingCloudWrites()
{
    return 0;
}

const FbyPlatformApi GSteamApi = {
    FBY_PLATFORM_ABI_VERSION,
    static_cast<std::uint32_t>(sizeof(FbyPlatformApi)),
    FbyPlatformKind_Steam,
    "steam",
    FbyPlatformCapability_Achievements | FbyPlatformCapability_Stats | FbyPlatformCapability_CloudSave |
        FbyPlatformCapability_Overlay | FbyPlatformCapability_UserIdentity,
    &SteamInitialize,
    &SteamShutdown,
    &SteamPump,
    &SteamUnlockAchievement,
    &SteamSetStatInt,
    &SteamSetStatFloat,
    &SteamStoreStats,
    &SteamUserId,
    &SteamReadCloud,
    &SteamWriteCloud,
    &SteamDeleteCloud,
    &SteamShowOverlay,
    &SteamPendingCloudWrites,
};
}

extern "C" FBY_PLATFORM_EXPORT const FbyPlatformApi* FBY_PLATFORM_CALL fby_platform_get_api(std::uint32_t InHostAbi)
{
    return InHostAbi == FBY_PLATFORM_ABI_VERSION ? &GSteamApi : nullptr;
}

extern "C" FBY_PLATFORM_EXPORT FbyPlatformResult FBY_PLATFORM_CALL
fby_steam_initialize(const FbyPlatformInitDesc* InDesc, const FbyPlatformCallbacks* InCallbacks)
{
    return SteamInitialize(InDesc, InCallbacks);
}

extern "C" FBY_PLATFORM_EXPORT FbyPlatformResult FBY_PLATFORM_CALL
fby_egs_initialize(const FbyPlatformInitDesc* InDesc, const FbyPlatformCallbacks* InCallbacks)
{
    (void)InDesc;
    (void)InCallbacks;
    return FbyPlatformResult_NotAvailable;
}
