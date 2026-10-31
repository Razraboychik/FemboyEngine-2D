// FemboyEngine2D - editor typography categories applied to imperative UI.
#pragma once

namespace Fby::Editor
{

class EditorFonts;

// Installed by EditorSystem after the font atlas is (re)built. Panels only use
// the helpers, so no panel owns or reloads fonts.
void SetEditorFonts(EditorFonts* InFonts) noexcept;

// ImGui::SetTooltip with the configured tooltip category.
void EditorTooltip(const char* InFormat, ...);

// Rich hover card for multi-paragraph help. Begin opens a tooltip window and
// fixes its wrap width before the first text call, so a paragraph never
// stretches into one unbounded line. Every Begin must be paired with End.
void BeginEditorTooltip(float InWrapWidth = 460.0f);
void EndEditorTooltip();

// One wrapped paragraph. Call between BeginEditorTooltip and EndEditorTooltip.
void EditorTooltipParagraph(const char* InText);

// One colour-coded legend row: a swatch followed by its label. The packed
// value is a Color32 rgba word, so the card and the rendered overlay share the
// exact colour.
void EditorTooltipLegendRow(const char* InLabel, unsigned int InColorRgba);

// Top-level editor panel wrapper: title uses PanelTitle, body uses Regular.
bool BeginEditorPanel(const char* InTitle, bool* InOpen = nullptr, int InFlags = 0);
void EndEditorPanel();
}
