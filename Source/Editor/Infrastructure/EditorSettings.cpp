// FemboyEngine2D - editor settings serialization.
#include "Editor/Infrastructure/EditorSettings.hpp"

#include <algorithm>
#include <cstddef>

namespace Fby::Editor
{
namespace
{
[[nodiscard]] std::string ThemeText(const char* InTheme, std::size_t InCapacity) noexcept
{
    std::size_t ScopedLength = 0;
    while (ScopedLength < InCapacity && InTheme[ScopedLength] != '\0')
    {
        ++ScopedLength;
    }
    return std::string(InTheme, ScopedLength);
}
}

std::uint32_t ClampViewportViewMode(std::uint32_t InMode) noexcept
{
    return InMode < Rhi::kViewportModeCount ? InMode : 0u;
}

void AppendEditorSettingsIni(std::string& InText, const EditorSettings& InEditor,
                             const AccessibilityEditorSettings& InIde)
{
    const std::size_t ScopedBadge = InText.find("[FemboyEditor]");
    if (ScopedBadge != std::string::npos)
    {
        InText.erase(ScopedBadge);
    }
    while (!InText.empty() && (InText.back() == '\n' || InText.back() == '\r'))
    {
        InText.pop_back();
    }
    if (!InText.empty())
    {
        InText += "\n\n";
    }
    InText += "[FemboyEditor]\n";
    InText += "ui_scale = ";
    InText += std::to_string(InEditor.uiScale);
    InText += "\nfont_panel_title = ";
    InText += std::to_string(InEditor.fontPanelTitle);
    InText += "\nfont_panel_body = ";
    InText += std::to_string(InEditor.fontPanelBody);
    InText += "\ntext_line_spacing = ";
    InText += std::to_string(InEditor.textLineSpacing);
    InText += "\ntext_letter_spacing = ";
    InText += std::to_string(InEditor.textLetterSpacing);
    InText += "\ntext_word_spacing = ";
    InText += std::to_string(InEditor.textWordSpacing);
    InText += "\nfont_tooltip = ";
    InText += std::to_string(InEditor.fontTooltip);
    InText += "\nfont_script_node_title = ";
    InText += std::to_string(InEditor.fontScriptNodeTitle);
    InText += "\nfont_script_pin_input = ";
    InText += std::to_string(InEditor.fontScriptPinInput);
    InText += "\nfont_script_pin_output = ";
    InText += std::to_string(InEditor.fontScriptPinOutput);
    InText += "\nfont_scene_text = ";
    InText += std::to_string(InEditor.fontSceneText);
    InText += "\nlog_max_files = ";
    InText += std::to_string(InEditor.logMaxFiles);
    InText += "\nlog_max_lines = ";
    InText += std::to_string(InEditor.logMaxLinesPerFile);
    InText += "\nundo_memory_mb = ";
    InText += std::to_string(InEditor.undoMemoryMB);
    InText += "\nundo_command_limit = ";
    InText += std::to_string(InEditor.undoCommandLimit);
    InText += "\nthumbnail_cache_limit = ";
    InText += std::to_string(InEditor.thumbnailCacheLimit);
    InText += "\nthumbnail_cache_enabled = ";
    InText += InEditor.thumbnailCacheEnabled ? "true" : "false";
    InText += "\nstocking_theme = ";
    InText += InEditor.stockingTheme ? "true" : "false";
    InText += "\nstocking_preset = ";
    InText += std::to_string(InEditor.stockingPreset);
    InText += "\nstocking_color_a = ";
    InText += std::to_string(InEditor.stockingColorA);
    InText += "\nstocking_color_b = ";
    InText += std::to_string(InEditor.stockingColorB);
    InText += "\nthemed_cursor = ";
    InText += InEditor.themedCursor ? "true" : "false";
    InText += "\ncursor_image = ";
    InText += InEditor.cursorImage;
    InText += "\nperformance_profile = ";
    InText += std::to_string(InEditor.performanceProfile);
    InText += "\nframe_rate_limit = ";
    InText += std::to_string(InEditor.frameRateLimit);
    InText += "\npause_when_unfocused = ";
    InText += InEditor.pauseWhenUnfocused ? "true" : "false";
    InText += "\nvsync = ";
    InText += InEditor.vsync ? "true" : "false";
    InText += "\nshow_demo = ";
    InText += InEditor.showDemoWindow ? "true" : "false";
    InText += "\nauto_refresh_browser = ";
    InText += InEditor.autoRefreshBrowser ? "true" : "false";
    InText += "\nwatch_assets = ";
    InText += InEditor.watchAssets ? "true" : "false";
    InText += "\nshow_grid = ";
    InText += InEditor.showGrid ? "true" : "false";
    InText += "\nwindow_mode = ";
    InText += std::to_string(std::min<std::uint8_t>(InEditor.windowMode, 2));
    InText += "\nwindow_width = ";
    InText += std::to_string(InEditor.windowWidth);
    InText += "\nwindow_height = ";
    InText += std::to_string(InEditor.windowHeight);
    InText += "\nviewport_view_mode = ";
    InText += std::to_string(ClampViewportViewMode(InEditor.viewportViewMode));
    InText += "\nviewport_complexity_budget = ";
    InText += std::to_string(InEditor.viewportComplexityBudget);
    InText += "\nsave_ini_on_commit = ";
    InText += InEditor.saveEditorIniOnCommit ? "true" : "false";
    InText += "\nide_blind_mode = ";
    InText += InIde.blindMode ? "true" : "false";
    InText += "\nide_deaf_mode = ";
    InText += InIde.deafMode ? "true" : "false";
    InText += "\nide_flash_guard = ";
    InText += InIde.flashGuard ? "true" : "false";
    InText += "\nide_high_contrast = ";
    InText += InIde.highContrast ? "true" : "false";
    InText += "\nide_reduce_motion = ";
    InText += InIde.reduceMotion ? "true" : "false";
    InText += "\nide_color_vision = ";
    InText += std::to_string(static_cast<unsigned>(InIde.colorVision));
    InText += "\nide_color_vision_strength = ";
    InText += std::to_string(InIde.colorVisionStrength);
    InText += "\nide_flash_threshold = ";
    InText += std::to_string(InIde.flashThreshold);
    // Editor narrator and bridge. The values mirror the [editor] section of
    // project.femproj so an exported editor.ini describes the same environment.
    InText += "\nnarration_enabled = ";
    InText += InEditor.narration.Enabled ? "true" : "false";
    InText += "\nnarration_focus = ";
    InText += InEditor.narration.AnnounceFocus ? "true" : "false";
    InText += "\nnarration_role = ";
    InText += InEditor.narration.AnnounceRole ? "true" : "false";
    InText += "\nnarration_state = ";
    InText += InEditor.narration.AnnounceState ? "true" : "false";
    InText += "\nnarration_interrupt = ";
    InText += InEditor.narration.InterruptOnFocus ? "true" : "false";
    InText += "\nnarration_verbosity = ";
    InText += std::to_string(static_cast<unsigned>(InEditor.narration.Verbosity));
    InText += "\nnarration_rate = ";
    InText += std::to_string(InEditor.narration.Rate);
    InText += "\nnarration_max_queue = ";
    InText += std::to_string(InEditor.narration.MaxQueued);
    InText += "\nnarration_language = \"";
    InText += ThemeText(InEditor.narration.Language, sizeof(InEditor.narration.Language));
    InText += "\"";
    InText += "\nnarration_voice = \"";
    InText += ThemeText(InEditor.narration.VoiceId, sizeof(InEditor.narration.VoiceId));
    InText += "\"";
    InText += "\nbridge_enabled = ";
    InText += InEditor.bridge.Enabled ? "true" : "false";
    InText += "\nbridge_focus_events = ";
    InText += InEditor.bridge.RaiseFocusEvents ? "true" : "false";
    InText += "\nbridge_structure_events = ";
    InText += InEditor.bridge.RaiseStructureEvents ? "true" : "false";
    InText += "\nbridge_state_events = ";
    InText += InEditor.bridge.RaiseStateEvents ? "true" : "false";
    InText += "\ntheme = \"";
    InText += ThemeText(InEditor.theme, sizeof(InEditor.theme));
    InText += "\"\n";
    InText += "text_color = ";
    InText += std::to_string(InEditor.textColor);
    InText += "\ntext_opacity = ";
    InText += std::to_string(InEditor.textOpacity);
    InText += "\ntext_contrast = ";
    InText += std::to_string(InEditor.textContrast);
    InText += "\npanel_glow = ";
    InText += std::to_string(InEditor.panelGlow);
    InText += "\ntext_outline = ";
    InText += InEditor.textOutline ? "true" : "false";
    InText += "\ntext_outline_color = ";
    InText += std::to_string(InEditor.textOutlineColor);
    InText += "\ntext_outline_size = ";
    InText += std::to_string(InEditor.textOutlineSize);
    InText += "\ncustom_symbol_colors = ";
    InText += InEditor.customSymbolColors ? "true" : "false";
    InText += "\nsymbol_axis_x = ";
    InText += std::to_string(InEditor.symbolColors.AxisX);
    InText += "\nsymbol_axis_y = ";
    InText += std::to_string(InEditor.symbolColors.AxisY);
    InText += "\nsymbol_grid = ";
    InText += std::to_string(InEditor.symbolColors.Grid);
    InText += "\nsymbol_play = ";
    InText += std::to_string(InEditor.symbolColors.Play);
    InText += "\nsymbol_pause = ";
    InText += std::to_string(InEditor.symbolColors.Pause);
    InText += "\nsymbol_step = ";
    InText += std::to_string(InEditor.symbolColors.Step);
    InText += "\nsymbol_stop = ";
    InText += std::to_string(InEditor.symbolColors.Stop);
    InText += "\nsymbol_rotate = ";
    InText += std::to_string(InEditor.symbolColors.Rotate);
    InText += "\nsymbol_multiplayer = ";
    InText += std::to_string(InEditor.symbolColors.Multiplayer);
    for (std::size_t ScopedI = 0; ScopedI < InEditor.symbolColors.Plot.size(); ++ScopedI)
    {
        InText += "\nsymbol_plot_";
        InText += std::to_string(ScopedI);
        InText += " = ";
        InText += std::to_string(InEditor.symbolColors.Plot[ScopedI]);
    }
}
}
