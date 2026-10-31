// FemboyEngine2D - platform provider C ABI.
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define FBY_PLATFORM_ABI_VERSION 1u

#if defined(_WIN32)
#define FBY_PLATFORM_EXPORT __declspec(dllexport)
#define FBY_PLATFORM_CALL __cdecl
#else
#define FBY_PLATFORM_EXPORT __attribute__((visibility("default")))
#define FBY_PLATFORM_CALL
#endif

typedef enum FbyPlatformKind
{
    FbyPlatformKind_None = 0,
    FbyPlatformKind_Steam = 1,
    FbyPlatformKind_Epic = 2,
    FbyPlatformKind_Mock = 3,
    FbyPlatformKind_Gog = 4
} FbyPlatformKind;

typedef enum FbyPlatformResult
{
    FbyPlatformResult_Ok = 0,
    FbyPlatformResult_Pending = 1,
    FbyPlatformResult_NotInitialized = 2,
    FbyPlatformResult_NotAvailable = 3,
    FbyPlatformResult_InvalidArgument = 4,
    FbyPlatformResult_BufferTooSmall = 5,
    FbyPlatformResult_NotFound = 6,
    FbyPlatformResult_BackendError = 7
} FbyPlatformResult;

enum
{
    FbyPlatformCapability_Achievements = 1u << 0,
    FbyPlatformCapability_Stats = 1u << 1,
    FbyPlatformCapability_CloudSave = 1u << 2,
    FbyPlatformCapability_Overlay = 1u << 3,
    FbyPlatformCapability_UserIdentity = 1u << 4,
    FbyPlatformCapability_AsyncCloud = 1u << 5
};

typedef struct FbyPlatformInitDesc
{
    uint32_t structSize;
    uint32_t abiVersion;
    FbyPlatformKind kind;
    // Steam AppId / EOS product id, UTF-8
    const char* applicationId;
    // EOS client credentials; unused by Steam
    const char* clientId;
    const char* clientSecret;
    const char* sandboxId;
    const char* deploymentId;
    const char* displayName;
    // Optional native platform options. On EGS this is a pointer to an
    const void* providerOptions;
    uint32_t providerOptionsSize;
    // SDL_Window* on desktop; opaque to the provider
    void* nativeWindow;
} FbyPlatformInitDesc;

typedef struct FbyPlatformCallbacks
{
    void* user;
    void(FBY_PLATFORM_CALL* onAuth)(void* user, uint64_t userId, int success);
    void(FBY_PLATFORM_CALL* onAchievement)(void* user, const char* apiName, int success);
    void(FBY_PLATFORM_CALL* onCloud)(void* user, const char* name, int success, const void* data, uint32_t size);
    void(FBY_PLATFORM_CALL* onOverlay)(void* user, const char* dialog, int success);
    void(FBY_PLATFORM_CALL* onLog)(void* user, int level, const char* message);
} FbyPlatformCallbacks;

typedef struct FbyPlatformApi
{
    uint32_t abiVersion;
    uint32_t structSize;
    FbyPlatformKind kind;
    const char* providerName;
    uint32_t capabilities;

    FbyPlatformResult(FBY_PLATFORM_CALL* initialize)(const FbyPlatformInitDesc* desc,
                                                     const FbyPlatformCallbacks* callbacks);
    void(FBY_PLATFORM_CALL* shutdown)(void);
    // Pumps the SDK's own callback queue. Must be cheap and must not block.
    void(FBY_PLATFORM_CALL* pump)(void);

    FbyPlatformResult(FBY_PLATFORM_CALL* unlockAchievement)(const char* apiName);
    FbyPlatformResult(FBY_PLATFORM_CALL* setStatInt)(const char* apiName, int64_t value);
    FbyPlatformResult(FBY_PLATFORM_CALL* setStatFloat)(const char* apiName, double value);
    FbyPlatformResult(FBY_PLATFORM_CALL* storeStats)(void);
    uint64_t(FBY_PLATFORM_CALL* userId)(void);

    // Cloud storage. readCloud writes the complete file or returns
    FbyPlatformResult(FBY_PLATFORM_CALL* readCloud)(const char* name, void* outData, uint32_t capacity,
                                                    uint32_t* outSize);
    FbyPlatformResult(FBY_PLATFORM_CALL* writeCloud)(const char* name, const void* data, uint32_t size);
    FbyPlatformResult(FBY_PLATFORM_CALL* deleteCloud)(const char* name);
    FbyPlatformResult(FBY_PLATFORM_CALL* showOverlay)(const char* dialog);
    uint32_t(FBY_PLATFORM_CALL* pendingCloudWrites)(void);
} FbyPlatformApi;

typedef const FbyPlatformApi*(FBY_PLATFORM_CALL* FbyPlatformGetApiFn)(uint32_t hostAbiVersion);

// Every provider shared library exports this symbol. A mismatch returns null so
FBY_PLATFORM_EXPORT const FbyPlatformApi* FBY_PLATFORM_CALL fby_platform_get_api(uint32_t hostAbiVersion);

// Provider-specific entry points. They are optional; the generic table above is
FBY_PLATFORM_EXPORT FbyPlatformResult FBY_PLATFORM_CALL fby_steam_initialize(const FbyPlatformInitDesc* desc,
                                                                             const FbyPlatformCallbacks* callbacks);
FBY_PLATFORM_EXPORT FbyPlatformResult FBY_PLATFORM_CALL fby_egs_initialize(const FbyPlatformInitDesc* desc,
                                                                          const FbyPlatformCallbacks* callbacks);
FBY_PLATFORM_EXPORT FbyPlatformResult FBY_PLATFORM_CALL fby_gog_initialize(const FbyPlatformInitDesc* desc,
                                                                          const FbyPlatformCallbacks* callbacks);

#ifdef __cplusplus
}
#endif
