// FemboyEngine2D - test plugin.
#include "Engine/ECS/Components.hpp"
#include "Engine/Core/Plugin.hpp"

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
constexpr const char* kPluginVersion = FBY_ENGINE_VERSION "-bad";
#else
constexpr const char* kPluginName = "hello_mod";
constexpr const char* kPluginVersion = FBY_ENGINE_VERSION;
#endif


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

void UiTick(void* InUser, float InDt)
{
    (void)InUser;
    (void)InDt;
}
}

// MSVC has no __VERSION__; report its toolset version instead so the plugin
// descriptor stays valid with both supported Windows compilers.
#if defined(__VERSION__)
#define FBY_HELLO_COMPILER "clang " __VERSION__
#elif defined(_MSC_FULL_VER)
#define FBY_HELLO_STRINGIFY_IMPL(InValue) #InValue
#define FBY_HELLO_STRINGIFY(InValue) FBY_HELLO_STRINGIFY_IMPL(InValue)
#define FBY_HELLO_COMPILER "msvc " FBY_HELLO_STRINGIFY(_MSC_FULL_VER)
#else
#define FBY_HELLO_COMPILER "unknown compiler"
#endif

FBY_DEFINE_PLUGIN(kPluginName, kPluginVersion, "FemboyEngine test suite", FBY_HELLO_COMPILER, kComponents,
                  {
                      GHost = rawHost;
                      Log("loaded; component layout hashes accepted");
                      if (rawHost->accessibility != nullptr && rawHost->accessibility->register_theme != nullptr)
                      {
                          FbyThemeDesc ScopedTheme{};
                          ScopedTheme.struct_size = static_cast<std::uint32_t>(sizeof(FbyThemeDesc));
                          ScopedTheme.id = "Hello.Theme";
                          ScopedTheme.display_name = "Hello theme";
                          for (std::uint32_t ScopedI = 0; ScopedI < FBY_THEME_COLOR_COUNT; ++ScopedI)
                          {
                              ScopedTheme.colors[ScopedI] = 0xFF000000u | (ScopedI * 0x0A0A0Au);
                          }
                          ScopedTheme.text_color = 0xFFFFFFFFu;
                          ScopedTheme.text_outline_color = 0xFF000000u;
                          ScopedTheme.text_opacity = 1.0f;
                          ScopedTheme.text_contrast = 1.0f;
                          ScopedTheme.text_outline_size = 0.0f;
                          ScopedTheme.text_font_scale = 1.0f;
                          (void)rawHost->accessibility->register_theme(rawHost->user, &ScopedTheme);
                      }
                      if (rawHost->add_system != nullptr)
                      {
                          rawHost->add_system(rawHost->user, "hello_mod_fixed_tick", FBY_PHASE_FIXED_UPDATE, 100,
                                              &FixedTick);
                          rawHost->add_system(rawHost->user, "hello_mod_ui_tick", FBY_PHASE_UI, 0, &UiTick);
                      }
                  })
