/* FemboyEngine2D - plugin ABI.
 *
 * This header is plain C: a plugin may be compiled by any compiler that matches
 * the host's C++ ABI (GCC/MinGW on Windows, GCC/Clang on Linux, Clang on
 * macOS). The exported entry points are `extern "C"`, the tables
 * are POD, and the only C++ type crossing the boundary is the FCS registry,
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

#define FBY_PLUGIN_ABI_VERSION 6u

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

    /* Mirrors Fby::Ecs::Phase exactly. The ABI is versioned, so a plugin must
     * include the matching header instead of relying on a tolerant fallback. */
    typedef enum FbyPhase
    {
        FBY_PHASE_PRE_UPDATE = 0,
        FBY_PHASE_FIXED_UPDATE = 1,
        FBY_PHASE_POST_UPDATE = 2,
        FBY_PHASE_UI = 3,
        FBY_PHASE_RENDER = 4,
        FBY_PHASE_COUNT = 5
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

    /* Optional accessibility/theme table. A plugin may register named palettes
     * and select the active one; it never receives a renderer, UI context or
     * draw list. Colours are packed 0xAABBGGRR (Fby::Color32). */
#define FBY_THEME_COLOR_COUNT 16
    typedef struct FbyThemeDesc
    {
        uint32_t struct_size;
        const char* id;
        const char* display_name;
        uint32_t flags;
        uint32_t colors[FBY_THEME_COLOR_COUNT]; /* background, surface, raised, border,
                                                  text, muted, primary, secondary, accent,
                                                  success, warning, danger, info, axis_x,
                                                  axis_y, highlight */
        uint32_t text_color;
        uint32_t text_outline_color;
        float text_opacity;
        float text_contrast;
        float text_outline_size;
        float text_font_scale;
    } FbyThemeDesc;

#define FBY_PALETTE_EXTENSION_COLORS 80
#define FBY_EFFECT_TAGS 16
#define FBY_ALCOHOL_EFFECTS 10
#define FBY_TEMPERATURE_BANDS 5
    typedef struct FbyPaletteExtensionDesc
    {
        uint32_t struct_size;
        const char* id;
        const char* display_name;
        uint32_t colors[FBY_PALETTE_EXTENSION_COLORS];
    } FbyPaletteExtensionDesc;

    typedef struct FbyTemperatureBandDesc
    {
        char tag[64];
        uint32_t color;
    } FbyTemperatureBandDesc;

    typedef struct FbyAlcoholEffectDesc
    {
        uint32_t effect; /* FbyAlcoholEffect */
        float intensity;
        float frequency;
    } FbyAlcoholEffectDesc;

    typedef struct FbyEffectDesc
    {
        uint32_t struct_size;
        const char* id;
        const char* display_name;
        uint32_t kind; /* 0 = NVG, 1 = thermal */
        float intensity;
        float blur;
        float sharpness;
        uint32_t pattern; /* FbyNvgPattern */
        float pattern_scale;
        uint32_t generation;
        uint32_t tint; /* default NVG/base colour, 0xAABBGGRR */
        uint32_t tag_count;
        char tags[FBY_EFFECT_TAGS][64];
        uint32_t alcohol_effect_count;
        FbyAlcoholEffectDesc alcohol_effects[FBY_ALCOHOL_EFFECTS];
        FbyTemperatureBandDesc temperatures[FBY_TEMPERATURE_BANDS];
    } FbyEffectDesc;

    typedef struct FbyAccessibilityApi
    {
        uint32_t struct_size;
        uint32_t abi_version;
        /* Returns 0 on success. The host copies the descriptor. */
        int (*register_theme)(void* user, const FbyThemeDesc* theme);
        int (*set_theme)(void* user, const char* id);
        int (*register_palette_extension)(void* user, const FbyPaletteExtensionDesc* extension);
        int (*register_effect)(void* user, const FbyEffectDesc* effect);
        int (*set_effect)(void* user, const char* id);
    } FbyAccessibilityApi;

    /* The host table handed to fby_plugin_load(). Every function receives the
     * opaque `user` pointer, so the host can move the context without touching
     * the plugin ABI. */
    typedef struct FbyHostApi
    {
        uint32_t struct_size;
        uint32_t abi_version;
        void* user;
        /* Opaque ECS registry pointer. The host and the plugin must be built
         * with the same storage backend and with `component_layout_hash`
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
        /* Optional in ABI 2; nullptr when the host has no runtime theme registry. */
        const FbyAccessibilityApi* accessibility;
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
