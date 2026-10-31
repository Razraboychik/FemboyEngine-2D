// FemboyEngine2D - Epic Games Store provider.
#include "PlatformAPI.h"

#include <cstdint>

#if defined(FBY_HAS_EOS_SDK)
#include <eos_sdk.h>
#endif

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
using EosPlatformCreateFn = void* (*)(const void*);
using EosPlatformTickFn = void (*)(void*);
using EosPlatformReleaseFn = void (*)(void*);

struct EgsState
{
    void* library = nullptr;
    void* platform = nullptr;
    EosPlatformCreateFn create = nullptr;
    EosPlatformTickFn tick = nullptr;
    EosPlatformReleaseFn release = nullptr;
    FbyPlatformCallbacks callbacks{};
    bool initialized = false;
};

EgsState GState{};

template <typename T> [[nodiscard]] bool ResolveSymbol(void* InLibrary, const char* InName, T& OutFunction) noexcept
{
#if defined(_WIN32)
    OutFunction = reinterpret_cast<T>(GetProcAddress(static_cast<HMODULE>(InLibrary), InName));
#else
    OutFunction = reinterpret_cast<T>(dlsym(InLibrary, InName));
#endif
    return OutFunction != nullptr;
}

void* OpenEosLibrary() noexcept
{
#if defined(_WIN32)
    const char* const ScopedCandidates[] = {"EOSSDK-Win64-Shipping.dll", "EOSSDK-Win32-Shipping.dll"};
    for (const char* ScopedCandidate : ScopedCandidates)
    {
        HMODULE ScopedModule = LoadLibraryA(ScopedCandidate);
        if (ScopedModule != nullptr)
        {
            return ScopedModule;
        }
    }
#elif defined(__APPLE__)
    void* ScopedModule = dlopen("libEOSSDK-Mac-Shipping.dylib", RTLD_NOW | RTLD_LOCAL);
    if (ScopedModule != nullptr)
    {
        return ScopedModule;
    }
#else
    void* ScopedModule = dlopen("libEOSSDK-Linux-Shipping.so", RTLD_NOW | RTLD_LOCAL);
    if (ScopedModule != nullptr)
    {
        return ScopedModule;
    }
#endif
    return nullptr;
}

void InvokeLog(const char* InMessage) noexcept
{
    if (GState.callbacks.onLog != nullptr)
    {
        GState.callbacks.onLog(GState.callbacks.user, 0, InMessage);
    }
}

FbyPlatformResult FBY_PLATFORM_CALL EgsInitialize(const FbyPlatformInitDesc* InDesc,
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
    if (InDesc == nullptr || InDesc->providerOptions == nullptr)
    {
        InvokeLog("EOS requires EOS_Platform_Options through providerOptions");
        return FbyPlatformResult_InvalidArgument;
    }
    if (GState.library == nullptr)
    {
        GState.library = OpenEosLibrary();
    }
    if (GState.library == nullptr)
    {
        return FbyPlatformResult_NotAvailable;
    }
    if (!ResolveSymbol(GState.library, "EOS_Platform_Create", GState.create) ||
        !ResolveSymbol(GState.library, "EOS_Platform_Tick", GState.tick) ||
        !ResolveSymbol(GState.library, "EOS_Platform_Release", GState.release))
    {
        return FbyPlatformResult_NotAvailable;
    }
#if defined(FBY_HAS_EOS_SDK)
    GState.platform = EOS_Platform_Create(static_cast<const EOS_Platform_Options*>(InDesc->providerOptions));
#else
    GState.platform = GState.create(InDesc->providerOptions);
#endif
    if (GState.platform == nullptr)
    {
        InvokeLog("EOS_Platform_Create returned null");
        return FbyPlatformResult_BackendError;
    }
    GState.initialized = true;
    if (GState.callbacks.onAuth != nullptr)
    {
        GState.callbacks.onAuth(GState.callbacks.user, 0, 1);
    }
    return FbyPlatformResult_Ok;
}

void FBY_PLATFORM_CALL EgsShutdown()
{
    if (GState.initialized && GState.release != nullptr && GState.platform != nullptr)
    {
        GState.release(GState.platform);
    }
    GState.platform = nullptr;
    GState.initialized = false;
}

void FBY_PLATFORM_CALL EgsPump()
{
    if (GState.initialized && GState.tick != nullptr && GState.platform != nullptr)
    {
        GState.tick(GState.platform);
    }
}

FbyPlatformResult FBY_PLATFORM_CALL EgsNotAvailableName(const char*)
{
    return FbyPlatformResult_NotAvailable;
}

FbyPlatformResult FBY_PLATFORM_CALL EgsNotAvailable(const char*, std::int64_t)
{
    return FbyPlatformResult_NotAvailable;
}

FbyPlatformResult FBY_PLATFORM_CALL EgsNotAvailableFloat(const char*, double)
{
    return FbyPlatformResult_NotAvailable;
}

FbyPlatformResult FBY_PLATFORM_CALL EgsStoreStats()
{
    return FbyPlatformResult_NotAvailable;
}

std::uint64_t FBY_PLATFORM_CALL EgsUserId()
{
    return 0;
}

FbyPlatformResult FBY_PLATFORM_CALL EgsReadCloud(const char*, void*, std::uint32_t, std::uint32_t* OutSize)
{
    if (OutSize != nullptr)
    {
        *OutSize = 0;
    }
    return FbyPlatformResult_NotAvailable;
}

FbyPlatformResult FBY_PLATFORM_CALL EgsWriteCloud(const char*, const void*, std::uint32_t)
{
    return FbyPlatformResult_NotAvailable;
}

FbyPlatformResult FBY_PLATFORM_CALL EgsDeleteCloud(const char*)
{
    return FbyPlatformResult_NotAvailable;
}

FbyPlatformResult FBY_PLATFORM_CALL EgsShowOverlay(const char*)
{
    return FbyPlatformResult_NotAvailable;
}

std::uint32_t FBY_PLATFORM_CALL EgsPendingCloudWrites()
{
    return 0;
}

const FbyPlatformApi GEgsApi = {
    FBY_PLATFORM_ABI_VERSION,
    static_cast<std::uint32_t>(sizeof(FbyPlatformApi)),
    FbyPlatformKind_Epic,
    "epic",
    0,
    &EgsInitialize,
    &EgsShutdown,
    &EgsPump,
    &EgsNotAvailableName,
    &EgsNotAvailable,
    &EgsNotAvailableFloat,
    &EgsStoreStats,
    &EgsUserId,
    &EgsReadCloud,
    &EgsWriteCloud,
    &EgsDeleteCloud,
    &EgsShowOverlay,
    &EgsPendingCloudWrites,
};
}

extern "C" FBY_PLATFORM_EXPORT const FbyPlatformApi* FBY_PLATFORM_CALL fby_platform_get_api(std::uint32_t InHostAbi)
{
    return InHostAbi == FBY_PLATFORM_ABI_VERSION ? &GEgsApi : nullptr;
}

extern "C" FBY_PLATFORM_EXPORT FbyPlatformResult FBY_PLATFORM_CALL
fby_egs_initialize(const FbyPlatformInitDesc* InDesc, const FbyPlatformCallbacks* InCallbacks)
{
    return EgsInitialize(InDesc, InCallbacks);
}

extern "C" FBY_PLATFORM_EXPORT FbyPlatformResult FBY_PLATFORM_CALL
fby_steam_initialize(const FbyPlatformInitDesc* InDesc, const FbyPlatformCallbacks* InCallbacks)
{
    (void)InDesc;
    (void)InCallbacks;
    return FbyPlatformResult_NotAvailable;
}
