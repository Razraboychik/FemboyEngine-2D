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
    InText += "\ntheme = \"";
    InText += ThemeText(InEditor.theme, sizeof(InEditor.theme));
    InText += "\"\n";
}
}
