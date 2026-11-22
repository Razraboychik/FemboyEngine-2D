// FemboyEngine2D - generated settings panel model tests.
#include "Editor/Infrastructure/AccessibilitySettings.hpp"
#include "Editor/Infrastructure/EditorSettingsRegistry.hpp"
#include "Editor/Infrastructure/ProjectConfig.hpp"
#include "Editor/Infrastructure/SettingsPanelModel.hpp"

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
}

int main()
{
    Fby::Settings::Registry ScopedRegistry;
    ScopedRegistry.Float("audio.voice").Category("Audio").Label("Voice").Range(-60.0, 6.0).Default(Fby::Settings::Value::MakeFloat(-3.0)).Scope(Fby::Settings::Scope::User);
    ScopedRegistry.Bool("video.hdr").Category("Video").Label("HDR").Default(Fby::Settings::Value::MakeBool(true));
    ScopedRegistry.Bool("advanced.debug").Category("Video").Label("Debug").Advanced().Default(Fby::Settings::Value::MakeBool(false));
    ScopedRegistry.Bool("hidden.secret").Category("Video").Label("Secret").Flags(Fby::Settings::FlagHidden).Default(Fby::Settings::Value::MakeBool(false));
    ScopedRegistry.Bool("render.disabled").Category("Rendering").Label("Disable render").Dangerous("Disable render output", "Ctrl+Alt+F12").Scope(Fby::Settings::Scope::User).Safe(Fby::Settings::Value::MakeBool(false));
    Check(ScopedRegistry.Freeze(), "panel schema freezes");

    Fby::Editor::SettingsPanelModel ScopedModel;
    Fby::Editor::SettingsPanelModel::Config ScopedConfig;
    ScopedConfig.Scope = Fby::Settings::Scope::User;
    ScopedModel.SetConfig(ScopedConfig);
    ScopedModel.Rebuild(ScopedRegistry);
    Check(ScopedModel.Rows().size() == 3, "advanced and hidden rows are filtered by default");
    Check(ScopedModel.Find("audio.voice") != nullptr, "audio row exists");

    ScopedConfig.ShowAdvanced = true;
    ScopedConfig.ShowHidden = true;
    ScopedModel.SetConfig(ScopedConfig);
    ScopedModel.Rebuild(ScopedRegistry);
    Check(ScopedModel.Rows().size() == 5, "advanced and hidden rows can be shown");

    ScopedConfig.Search = "voice";
    ScopedModel.SetConfig(ScopedConfig);
    ScopedModel.Rebuild(ScopedRegistry);
    Check(ScopedModel.Rows().size() == 1, "search filters by label or id");

    ScopedConfig.Search.clear();
    ScopedModel.SetConfig(ScopedConfig);
    ScopedModel.Rebuild(ScopedRegistry);
    std::string ScopedError;
    Check(!ScopedModel.Set(ScopedRegistry, "render.disabled", Fby::Settings::Value::MakeBool(true), false, &ScopedError),
          "dangerous panel edit needs confirmation");
    Check(ScopedModel.NeedsConfirmation("render.disabled"), "dangerous metadata is visible to the panel");
    Check(ScopedModel.ConfirmationText("render.disabled") == "Disable render output", "confirmation text is exposed");
    Check(ScopedModel.RecoveryHotkey("render.disabled") == "Ctrl+Alt+F12", "recovery hotkey is exposed");
    Check(ScopedModel.Set(ScopedRegistry, "render.disabled", Fby::Settings::Value::MakeBool(true), true),
          "confirmed dangerous edit is accepted");
    Check(ScopedModel.Set(ScopedRegistry, "audio.voice", Fby::Settings::Value::MakeFloat(-20.0), false),
          "ordinary panel edit is accepted");
    Check(ScopedRegistry.Effective("audio.voice").FloatValue == -20.0, "panel edit reaches the registry");
    Check(ScopedModel.Reset(ScopedRegistry, "audio.voice"), "reset removes the user override");
    Check(ScopedRegistry.Effective("audio.voice").FloatValue == -3.0, "reset restores the default");

    Fby::Editor::ProjectConfig ScopedProject;
    ScopedProject.project.audioBusCount = 2;
    std::snprintf(ScopedProject.project.audioBuses[0].id, sizeof(ScopedProject.project.audioBuses[0].id), "%s", "music");
    std::snprintf(ScopedProject.project.audioBuses[0].name, sizeof(ScopedProject.project.audioBuses[0].name), "%s",
                  "Music");
    std::snprintf(ScopedProject.project.audioBuses[0].parent, sizeof(ScopedProject.project.audioBuses[0].parent), "%s",
                  "master");
    ScopedProject.project.audioBuses[0].defaultDb = -6.0f;
    std::snprintf(ScopedProject.project.audioBuses[1].id, sizeof(ScopedProject.project.audioBuses[1].id), "%s", "voice");
    std::snprintf(ScopedProject.project.audioBuses[1].name, sizeof(ScopedProject.project.audioBuses[1].name), "%s",
                  "Dialogue");
    std::snprintf(ScopedProject.project.audioBuses[1].parent, sizeof(ScopedProject.project.audioBuses[1].parent), "%s",
                  "music");
    ScopedProject.project.audioBuses[1].defaultDb = -2.0f;
    Fby::Editor::ProjectConfig ScopedDecodedProject;
    std::string ScopedDecodeError;
    const bool ScopedDecoded = Fby::Editor::DecodeProjectToml(Fby::Editor::EncodeProjectToml(ScopedProject),
                                                              &ScopedDecodedProject, &ScopedDecodeError);
    Check(ScopedDecoded, "project audio buses encode and decode");
    Check(ScopedDecodedProject.project.audioBusCount == 2, "audio bus count round-trips");
    Check(std::string(ScopedDecodedProject.project.audioBuses[1].id) == "voice", "audio bus id round-trips");
    Check(std::string(ScopedDecodedProject.project.audioBuses[1].parent) == "music",
          "audio bus parent round-trips");

    Fby::AccessibilityProfile ScopedProfile;
    Fby::Settings::Registry ScopedAccessibility;
    Fby::Editor::RegisterAccessibilitySettings(ScopedAccessibility, ScopedProfile);
    Check(ScopedAccessibility.Freeze(), "accessibility schema freezes");
    Check(ScopedAccessibility.Find("accessibility.deaf") != nullptr, "deaf mode is registered");
    Check(ScopedAccessibility.SetProject("accessibility.deaf", Fby::Settings::Value::MakeBool(true)),
          "deaf mode is editable");
    Check(Fby::AccessibilityHasFlag(ScopedProfile.runtime.flags, Fby::AccessibilityDeafMode),
          "deaf mode reaches the profile");
    Check(ScopedAccessibility.SetProject("accessibility.minimum_contrast", Fby::Settings::Value::MakeFloat(99.0)),
          "contrast is clamped");
    Check(ScopedProfile.presentation.minimumContrastRatio == 21.0f, "contrast reaches the profile");

    Fby::Editor::EditorSettings ScopedEditorSettings;
    Fby::Settings::Registry ScopedEditorRegistry;
    Fby::Editor::RegisterEditorSettings(ScopedEditorRegistry, ScopedEditorSettings);
    Check(ScopedEditorRegistry.Freeze(), "editor settings schema freezes");
    Check(ScopedEditorRegistry.SetProject("editor.ui_scale", Fby::Settings::Value::MakeFloat(1.5)),
          "editor UI scale is editable");
    Check(ScopedEditorSettings.uiScale == 1.5f, "editor UI scale reaches the struct");
    Check(ScopedEditorRegistry.SetProject("editor.undo_memory_mb", Fby::Settings::Value::MakeInt(4096)),
          "editor undo budget is clamped");
    Check(ScopedEditorSettings.undoMemoryMB == 256, "editor undo budget clamp is visible");

    if (GFailures != 0)
    {
        std::fprintf(stderr, "%d settings panel test(s) failed\n", GFailures);
        return 1;
    }
    std::printf("settings_panel_model: OK\n");
    return 0;
}
