/* FemboyEngine2D - plugin ABI.
 *
 * This header is plain C: a plugin may be compiled by any compiler that matches
 * the host's C++ ABI (GCC/MinGW on Windows, GCC/Clang on Linux, Clang on
 * macOS/iOS/Android). The exported entry points are `extern "C"`, the tables
 * are POD, and the only C++ type crossing the boundary is the EnTT registry,
 * which the plugin receives as an opaque pointer and casts back after checking
 * the ABI version.
 *
 * Rules a plugin must follow:
 *   * components are standard-layout PODs registered in Mirror;
 *   * every component it uses is listed in its FbyPluginInfo with the layout
 *     hash produced by Fby::Reflect::LayoutHash<T>();
 *   * the host refuses to load the plugin when a hash differs - a component
 *     rebuilt with one extra field is a different ABI, not a warning.
 */
#ifndef FBY_PLUGIN_API_H
#define FBY_PLUGIN_API_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define FBY_PLUGIN_ABI_VERSION 1u

#if defined(_WIN32)
#define FBY_PLUGIN_EXPORT __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
#define FBY_PLUGIN_EXPORT __attribute__((visibility("default")))
#else
#define FBY_PLUGIN_EXPORT
#endif

    typedef enum FbyLogLevel
    {
        FBY_LOG_TRACE = 0,
        FBY_LOG_DEBUG,
        FBY_LOG_INFO,
        FBY_LOG_WARN,
        FBY_LOG_ERROR
    } FbyLogLevel;

    /* Mirrors Fby::Ecs::Phase. Kept as an int ABI so a plugin built against an
     * older header still lands in a sane phase. */
    typedef enum FbyPhase
    {
        FBY_PHASE_PRE_UPDATE = 0,
        FBY_PHASE_FIXED_UPDATE = 1,
        FBY_PHASE_POST_UPDATE = 2,
        FBY_PHASE_RENDER = 3
    } FbyPhase;

    typedef struct FbyComponentInfo
    {
        const char* name;
        uint32_t size;
        uint32_t alignment;
        uint64_t layout_hash;
    } FbyComponentInfo;

    typedef struct FbyPluginInfo
    {
        uint32_t struct_size; /* sizeof(FbyPluginInfo) of the plugin */
        uint32_t abi_version; /* FBY_PLUGIN_ABI_VERSION it was built against */
        const char* name;
        const char* version;
        const char* author;
        uint32_t component_count;
        const FbyComponentInfo* components;
        const char* build_info; /* compiler, standard library, flags */
    } FbyPluginInfo;

    /* The host table handed to fby_plugin_load(). Every function receives the
     * opaque `user` pointer, so the host can move the context without touching
     * the plugin ABI. */
    typedef struct FbyHostApi
    {
        uint32_t struct_size;
        uint32_t abi_version;
        void* user;
        /* entt::registry* - only valid together with `component_layout_hash`
         * agreeing for every component the plugin touches. */
        void* registry;

        void* (*alloc)(void* InUser, size_t InSize, size_t InAlignment);
        void (*free)(void* InUser, void* InPointer, size_t InSize, size_t InAlignment);
        void (*log)(void* InUser, int InLevel, const char* InChannel, const char* InMessage);

        /* Layout hash registered by the host under `name`, or 0 when unknown. */
        uint64_t (*component_layout_hash)(void* InUser, const char* InName);

        /* Registers a system. `tick` runs in `phase`; it must not throw and must
         * not allocate unless its allocator came from this table. */
        int (*add_system)(void* InUser, const char* InName, int InPhase, int InOrder,
                          void (*InTick)(void* InUser, float InDt));
    } FbyHostApi;

    typedef const FbyPluginInfo* (*FbyPluginInfoFn)(void);
    typedef int (*FbyPluginLoadFn)(FbyHostApi* InHost);
    typedef void (*FbyPluginUnloadFn)(void);

/* Standard symbol names the host looks up. */
#define FBY_PLUGIN_INFO_SYMBOL "fby_plugin_info"
#define FBY_PLUGIN_LOAD_SYMBOL "fby_plugin_load"
#define FBY_PLUGIN_UNLOAD_SYMBOL "fby_plugin_unload"

#ifdef __cplusplus
} /* extern "C" */

/* Entry points every plugin shared library must export. */
#define FBY_PLUGIN_ENTRY extern "C" FBY_PLUGIN_EXPORT
#endif

#endif /* FBY_PLUGIN_API_H */
