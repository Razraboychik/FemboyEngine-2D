// FemboyEngine2D - registry view of editor-only settings.
#include "Editor/Infrastructure/EditorSettingsRegistry.hpp"

#include <cstdio>

namespace Fby::Editor
{

void RegisterEditorSettings(Settings::Registry& InRegistry, EditorSettings& InSettings)
{
    InRegistry.Float("editor.ui_scale")
        .Category("Editor")
        .Label("UI scale")
        .Default(Settings::Value::MakeFloat(InSettings.uiScale))
        .Range(0.75, 2.0)
        .Scope(Settings::Scope::Project)
        .Apply([&InSettings](const Settings::Value& InValue)
               { InSettings.uiScale = static_cast<float>(InValue.FloatValue); });
    InRegistry.Float("editor.font_panel_body")
        .Category("Editor")
        .Label("Panel body font")
        .Default(Settings::Value::MakeFloat(InSettings.fontPanelBody))
        .Range(10.0, 48.0)
        .Scope(Settings::Scope::Project)
        .Apply([&InSettings](const Settings::Value& InValue)
               { InSettings.fontPanelBody = static_cast<float>(InValue.FloatValue); });
    InRegistry.Float("editor.text_line_spacing")
        .Category("Editor")
        .Label("Text line spacing")
        .Default(Settings::Value::MakeFloat(InSettings.textLineSpacing))
        .Range(-4.0, 16.0)
        .Scope(Settings::Scope::Project)
        .Apply([&InSettings](const Settings::Value& InValue)
               { InSettings.textLineSpacing = static_cast<float>(InValue.FloatValue); });
    InRegistry.Int("editor.log_max_files")
        .Category("Editor")
        .Label("Log files")
        .Default(Settings::Value::MakeInt(InSettings.logMaxFiles))
        .Range(1.0, 64.0)
        .Scope(Settings::Scope::Project)
        .Apply([&InSettings](const Settings::Value& InValue)
               { InSettings.logMaxFiles = static_cast<std::uint32_t>(InValue.IntValue); });
    InRegistry.Int("editor.undo_memory_mb")
        .Category("Editor")
        .Label("Undo memory MB")
        .Default(Settings::Value::MakeInt(InSettings.undoMemoryMB))
        .Range(1.0, 256.0)
        .Scope(Settings::Scope::Project)
        .Apply([&InSettings](const Settings::Value& InValue)
               { InSettings.undoMemoryMB = static_cast<std::uint32_t>(InValue.IntValue); });
    InRegistry.Int("editor.frame_rate_limit")
        .Category("Editor")
        .Label("Frame rate limit")
        .Default(Settings::Value::MakeInt(InSettings.frameRateLimit))
        .Range(0.0, 360.0)
        .Scope(Settings::Scope::Project)
        .Apply([&InSettings](const Settings::Value& InValue)
               { InSettings.frameRateLimit = static_cast<std::uint16_t>(InValue.IntValue); });
    InRegistry.Bool("editor.vsync")
        .Category("Editor")
        .Label("VSync")
        .Default(Settings::Value::MakeBool(InSettings.vsync))
        .Scope(Settings::Scope::Project)
        .Apply([&InSettings](const Settings::Value& InValue) { InSettings.vsync = InValue.BoolValue; });
    InRegistry.String("editor.theme")
        .Category("Editor")
        .Label("Theme")
        .Default(Settings::Value::MakeString(InSettings.theme))
        .Scope(Settings::Scope::Project)
        .Apply(
            [&InSettings](const Settings::Value& InValue)
            {
                std::snprintf(InSettings.theme, sizeof(InSettings.theme), "%s", InValue.StringValue.c_str());
            });
}

}