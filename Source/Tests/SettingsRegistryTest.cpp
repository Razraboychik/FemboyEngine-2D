// FemboyEngine2D - settings schema, scopes, danger and safe-mode tests.
#include "Engine/Audio/Audio.hpp"
#include "Engine/Core/OutputPolicy.hpp"
#include "Engine/Core/SettingsRegistry.hpp"
#include "Engine/Core/SettingsSerialization.hpp"

#include <cstdio>
#include <filesystem>
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
}

int main()
{
    Fby::Settings::Registry ScopedRegistry;
    int ScopedApplied = 0;
    ScopedRegistry.Float("audio.master")
        .Category("Audio")
        .Label("Master")
        .Unit("dB")
        .Default(Fby::Settings::Value::MakeFloat(-3.0))
        .Range(-60.0, 0.0)
        .Scope(Fby::Settings::Scope::User)
        .Apply(
            [&ScopedApplied](const Fby::Settings::Value& InValue)
            {
                Check(InValue.FloatValue >= -60.0 && InValue.FloatValue <= 0.0, "applied volume stays in range");
                ++ScopedApplied;
            });
    ScopedRegistry.Bool("render.enabled").Default(Fby::Settings::Value::MakeBool(true));
    ScopedRegistry.Bool("render.disabled")
        .Default(Fby::Settings::Value::MakeBool(false))
        .Dangerous("Disable render", "Ctrl+Alt+F12")
        .Scope(Fby::Settings::Scope::User)
        .Safe(Fby::Settings::Value::MakeBool(false));
    ScopedRegistry.ColorSetting("theme.accent")
        .Default(Fby::Settings::Value::MakeColor({1.0f, 0.5f, 0.0f, 1.0f}));

    Check(ScopedRegistry.Freeze(), "schema freezes");
    Check(ScopedRegistry.IsFrozen(), "schema reports frozen");
    Check(!ScopedRegistry.Freeze(), "second freeze is rejected");

    Check(ScopedRegistry.Effective("audio.master").FloatValue == -3.0, "default is effective");
    Check(ScopedRegistry.SetProject("audio.master", Fby::Settings::Value::MakeFloat(-8.0)), "project value accepted");
    Check(ScopedRegistry.Effective("audio.master").FloatValue == -8.0, "project value is effective");
    Check(ScopedRegistry.SetUser("audio.master", Fby::Settings::Value::MakeFloat(-20.0)), "user value accepted");
    Check(ScopedRegistry.Effective("audio.master").FloatValue == -20.0, "user value wins");
    Check(ScopedRegistry.SetUser("audio.master", Fby::Settings::Value::MakeFloat(-100.0)), "out-of-range value is clamped");
    Check(ScopedRegistry.Effective("audio.master").FloatValue == -60.0, "clamp is visible");
    Check(ScopedRegistry.SetSession("audio.master", Fby::Settings::Value::MakeFloat(-1.0)), "session value accepted");
    Check(ScopedRegistry.Effective("audio.master").FloatValue == -1.0, "session wins over user");
    ScopedApplied = 0;
    Check(ScopedRegistry.Apply("audio.master"), "apply callback runs");
    Check(ScopedApplied == 1, "apply callback count");

    std::string ScopedError;
    Check(!ScopedRegistry.SetUser("render.disabled", Fby::Settings::Value::MakeBool(true), false, &ScopedError),
          "dangerous setting needs confirmation");
    Check(!ScopedError.empty(), "dangerous rejection has a reason");
    Check(ScopedRegistry.SetUser("render.disabled", Fby::Settings::Value::MakeBool(true), true),
          "confirmed dangerous value accepted");
    Check(ScopedRegistry.Effective("render.disabled").BoolValue, "dangerous value is active");
    ScopedRegistry.RestoreSafeMode();
    Check(!ScopedRegistry.Effective("render.disabled").BoolValue, "safe mode restores dangerous value");
    Check(!ScopedRegistry.IsOverridden("render.disabled", Fby::Settings::Scope::User), "safe mode drops user override");

    Check(ScopedRegistry.SetProject("theme.accent", Fby::Settings::Value::MakeColor({2.0f, -1.0f, 0.5f, 2.0f})),
          "colour accepted");
    Check(ScopedRegistry.Effective("theme.accent").ColorValue.R == 1.0f, "colour red clamps");
    Check(ScopedRegistry.Effective("theme.accent").ColorValue.G == 0.0f, "colour green clamps");

    Check(!ScopedRegistry.SetProject("missing", Fby::Settings::Value::MakeBool(true)), "unknown setting rejected");
    Check(ScopedRegistry.ApplyAll(), "apply all succeeds");

    Fby::AudioSystem ScopedAudio;
    Check(ScopedAudio.ProcessingEnabled(), "audio processing starts enabled");
    ScopedAudio.SetProcessingEnabled(false);
    Check(!ScopedAudio.ProcessingEnabled(), "audio processing can be disabled");
    float ScopedSilence[8] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    ScopedAudio.Mix(ScopedSilence, 4);
    for (float ScopedSample : ScopedSilence)
    {
        Check(ScopedSample == 0.0f, "disabled audio emits silence");
    }
    ScopedAudio.SetProcessingEnabled(true);
    Check(ScopedAudio.ProcessingEnabled(), "audio processing can be re-enabled");

    Fby::Output::Policy& ScopedOutput = Fby::Output::Policy::Instance();
    ScopedOutput.RestoreSafeMode();
    Check(ScopedOutput.RenderEnabled() && ScopedOutput.AudioEnabled(), "output starts fully enabled");
    ScopedOutput.SetRenderPolicy(Fby::Output::RenderPolicy::NoWorld);
    Check(ScopedOutput.RenderEnabled() && !ScopedOutput.RenderWorldEnabled(), "NoWorld skips the world pass");
    ScopedOutput.SetRenderPolicy(Fby::Output::RenderPolicy::Disabled);
    ScopedOutput.SetAudioPolicy(Fby::Output::AudioPolicy::Disabled);
    Check(!ScopedOutput.IsPlayerVisible(), "both outputs disabled is detected");
    ScopedOutput.RequestSafeMode();
    Check(ScopedOutput.SafeModeRequested(), "safe mode request is visible");
    ScopedOutput.RestoreSafeMode();
    Check(!ScopedOutput.SafeModeRequested() && ScopedOutput.IsPlayerVisible(), "safe mode restores both outputs");

    const std::filesystem::path ScopedPath = std::filesystem::path("build") / "settings_registry_test.toml";
    std::error_code ScopedFileError;
    std::filesystem::remove(ScopedPath, ScopedFileError);
    std::string ScopedSaveError;
    const bool ScopedSaved =
        Fby::Settings::SaveSettingsToml(ScopedRegistry, Fby::Settings::Scope::User, ScopedPath, &ScopedSaveError);
    if (!ScopedSaved)
    {
        std::fprintf(stderr, "settings save error: %s\n", ScopedSaveError.c_str());
    }
    Check(ScopedSaved, "user settings save");
    Fby::Settings::Registry ScopedLoaded;
    ScopedLoaded.Float("audio.master")
        .Default(Fby::Settings::Value::MakeFloat(-3.0))
        .Range(-60.0, 0.0)
        .Scope(Fby::Settings::Scope::User);
    Check(ScopedLoaded.Freeze(), "load schema freezes");
    std::size_t ScopedLoadedCount = 0;
    Check(Fby::Settings::LoadSettingsToml(ScopedLoaded, Fby::Settings::Scope::User, ScopedPath, &ScopedLoadedCount),
          "user settings load");
    Check(ScopedLoadedCount == 1, "one known user setting loads");
    Check(ScopedLoaded.Effective("audio.master").FloatValue == -60.0, "loaded user value survives round-trip");
    std::filesystem::remove(ScopedPath, ScopedFileError);

    if (GFailures != 0)
    {
        std::fprintf(stderr, "%d settings registry test(s) failed\n", GFailures);
        return 1;
    }
    std::printf("settings_registry: OK\n");
    return 0;
}
