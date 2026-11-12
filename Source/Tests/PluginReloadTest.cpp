// FemboyEngine2D - plugin load/unload hot reload while jobs run.
#include "Engine/Core/JobSystem.hpp"
#include "Engine/ECS/ComponentRegistry.hpp"
#include "Engine/Audio/VoicePreset.hpp"
#include "Engine/Core/PluginHost.hpp"
#include "Engine/UI/AccessibilityTheme.hpp"
#include "Engine/UI/AccessibilityEffects.hpp"
#include "Engine/ECS/Components.hpp"

#include <atomic>
#include <cstdio>
#include <string>

namespace
{
int GFailures = 0;

void Check(bool InCondition, const char* InMessage)
{
    if (!InCondition)
    {
        std::fprintf(stderr, "FAIL: %s\n", InMessage);
        ++GFailures;
    }
}

std::atomic<std::uint32_t> GJobHits{0};
}

int main()
{
    using namespace Fby;

    Plugin::ComponentRegistry ScopedComponents;
    ScopedComponents.Register<Transform2D>("Transform2D");
    ScopedComponents.Register<Velocity>("Velocity");
    Fby::Scene::Registry ScopedRegistry;
    Ui::AccessibilityThemeRegistry ScopedThemes;
    Ui::AccessibilityExtensionRegistry ScopedAccessibilityExtensions;
    Ui::AccessibilityEffectRegistry ScopedAccessibilityEffects;
    Plugin::PluginHost ScopedHost(ScopedComponents, &ScopedRegistry);
    ScopedHost.BindAccessibilityThemes(&ScopedThemes);
    ScopedHost.BindAccessibilityEffects(&ScopedAccessibilityExtensions, &ScopedAccessibilityEffects);

    std::string ScopedError;
    Check(ScopedHost.Load(FBY_TEST_PLUGIN_PATH, &ScopedError), "the fixture plugin loads");
    Check(ScopedHost.Plugins().size() == 1, "one plugin is registered");
    bool ScopedSawFixedTick = false;
    bool ScopedSawUiTick = false;
    for (const auto& ScopedPlugin : ScopedHost.Plugins())
    {
        for (const Plugin::PluginSystem& ScopedSystem : ScopedPlugin->systems)
        {
            ScopedSawFixedTick = ScopedSawFixedTick || ScopedSystem.phase == FBY_PHASE_FIXED_UPDATE;
            ScopedSawUiTick = ScopedSawUiTick || ScopedSystem.phase == FBY_PHASE_UI;
        }
    }
    Check(ScopedSawFixedTick, "the fixture registers a FixedUpdate system");
    Check(ScopedSawUiTick, "the fixture registers a UI system");
    Check(ScopedThemes.Find("Hello.Theme") != nullptr, "the plugin registered an accessibility theme");
    ScopedHost.UnloadAll();
    Check(ScopedHost.Plugins().empty(), "unload clears the plugin list");
    Check(ScopedThemes.Count() == 0, "unload removes plugin-owned themes");

    // Reload the shared object ten times while four workers run.
    JobSystem ScopedJobs(4);
    GJobHits = 0;
    ScopedJobs.ParallelFor(2048, 64,
                           [](std::uint32_t InBegin, std::uint32_t InEnd)
                           {
                               for (std::uint32_t ScopedI = InBegin; ScopedI < InEnd; ++ScopedI)
                               {
                                   (void)ScopedI;
                                   GJobHits.fetch_add(1, std::memory_order_relaxed);
                               }
                           });

    for (std::uint32_t ScopedIteration = 0; ScopedIteration < 10; ++ScopedIteration)
    {
        const bool ScopedLoaded = ScopedHost.Load(FBY_TEST_PLUGIN_PATH, &ScopedError);
        if (!ScopedLoaded)
        {
            std::fprintf(stderr, "reload failed: %s\n", ScopedError.c_str());
        }
        Check(ScopedLoaded, "the plugin reloads");
        Check(ScopedHost.Plugins().size() == 1, "one plugin after reload");
        ScopedHost.UnloadAll();
        Check(ScopedHost.Plugins().empty(), "unload after reload");
    }

    ScopedJobs.WaitIdle();
    Check(GJobHits.load() == 2048, "the background jobs completed during the reloads");

    // Per-plugin reload keeps the host bookkeeping consistent: the plugin is
    // unloaded through its own entry point and loaded again from the same path.
    Check(ScopedHost.Reload(FBY_TEST_PLUGIN_PATH, &ScopedError), "the loaded plugin reloads in place");
    Check(ScopedHost.Plugins().size() == 1, "in-place reload leaves one plugin");
    Check(ScopedHost.Plugins()[0]->path == FBY_TEST_PLUGIN_PATH, "the reloaded plugin keeps its path");
    Check(ScopedError.empty(), "the in-place reload reports no error");
    ScopedHost.UnloadAll();
    Check(ScopedHost.Plugins().empty(), "unload clears the reloaded plugin");
    Check(ScopedHost.Reload(FBY_TEST_PLUGIN_PATH, &ScopedError), "reload on an empty host loads the plugin");
    Check(ScopedHost.Plugins().size() == 1, "reload loads a missing plugin");
    ScopedHost.UnloadAll();

    // The bad-layout fixture must be rejected, not crash the host.
    if (!ScopedHost.Load(FBY_TEST_BAD_PLUGIN_PATH, &ScopedError))
    {
        Check(!ScopedError.empty(), "the bad-layout plugin reports an error");
    }
    else
    {
        Check(false, "the bad-layout plugin must not load");
        ScopedHost.UnloadAll();
    }

#if defined(FBY_TEST_VOICE_PACK_PATH)
    VoicePresetRegistry ScopedVoicePresets;
    ScopedVoicePresets.RegisterBuiltIns();
    const std::uint32_t ScopedBuiltInCount = ScopedVoicePresets.Count();
    ScopedHost.BindVoicePresets(&ScopedVoicePresets);
    Check(ScopedHost.Load(FBY_TEST_VOICE_PACK_PATH, &ScopedError), "Voice FX Pack loads");
    Check(ScopedVoicePresets.Count() == ScopedBuiltInCount + 3, "Voice FX Pack registers three presets");
    Check(ScopedVoicePresets.Find("Pack.Voice.Helium") != nullptr, "Voice FX Pack registers helium");
    Check(ScopedVoicePresets.Find("Pack.Voice.Swamp") != nullptr, "Voice FX Pack registers swamp");
    Check(ScopedVoicePresets.Find("Pack.Voice.CursedMumble") != nullptr, "Voice FX Pack registers cursed mumble");
    ScopedHost.UnloadAll();
    Check(ScopedVoicePresets.Count() == ScopedBuiltInCount, "unload removes plugin-owned voice presets");
    ScopedHost.BindVoicePresets(nullptr);
    Check(ScopedHost.Load(FBY_TEST_VOICE_PACK_PATH, &ScopedError), "Voice FX Pack loads without a voice registry");
    Check(ScopedVoicePresets.Count() == ScopedBuiltInCount, "no presets register without a registry");
    ScopedHost.UnloadAll();
#endif

#if defined(FBY_TEST_RAINBOW_PALETTE_PATH)
    ScopedThemes.Clear();
    Check(ScopedHost.Load(FBY_TEST_RAINBOW_PALETTE_PATH, &ScopedError), "Rainbow Palette plugin loads");
    const Ui::AccessibilityPaletteExtension* ScopedPalette = ScopedAccessibilityExtensions.Find("Rainbow.Palette80");
    Check(ScopedPalette != nullptr, "Rainbow Palette registers 80 colours");
    Check(ScopedPalette != nullptr && ScopedPalette->Colors[79].A() != 0,
          "palette extension ends with a visible colour");
    ScopedHost.UnloadAll();
    Check(ScopedAccessibilityExtensions.Count() == 0, "Rainbow Palette unload removes its extension");

    ScopedThemes.Clear();
    Check(ScopedHost.Load(FBY_TEST_NVG_PATH, &ScopedError), "NVG plugin loads");
    Check(ScopedAccessibilityEffects.Find("Nvg.Gen1.Scanlines") != nullptr, "NVG registers generation profiles");
    ScopedHost.UnloadAll();

    Check(ScopedHost.Load(FBY_TEST_THERMAL_PATH, &ScopedError), "Thermal plugin loads");
    const Ui::AccessibilityEffectProfile* ScopedThermal = ScopedAccessibilityEffects.Find("Thermal.Standard");
    Check(ScopedThermal != nullptr, "Thermal registers standard palette");
    Check(ScopedThermal != nullptr && ScopedThermal->Temperatures[0].Tag[0] != '\0',
          "Thermal maps five temperature tags");
    ScopedHost.UnloadAll();
    Check(ScopedAccessibilityEffects.Count() == 0, "Thermal unload removes effect profiles");

    Check(ScopedHost.Load(FBY_TEST_ALCOHOL_PATH, &ScopedError), "Alcohol plugin loads");
    const Ui::AccessibilityEffectProfile* ScopedAlcohol = ScopedAccessibilityEffects.Find("Alcohol.Drunk");
    Check(ScopedAlcohol != nullptr, "Alcohol registers drunk profile");
    Check(ScopedAlcohol != nullptr && ScopedAlcohol->AlcoholEffectCount >= 7, "Alcohol profile has an effect pool");
    ScopedHost.UnloadAll();
    Check(ScopedAccessibilityEffects.Count() == 0, "Alcohol unload removes effect profiles");
#endif

    if (GFailures == 0)
    {
        std::printf("plugin_reload: OK\n");
        return 0;
    }
    std::fprintf(stderr, "plugin_reload: %d failure(s)\n", GFailures);
    return 1;
}
