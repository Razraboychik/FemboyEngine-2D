// FemboyEngine2D - editor typography helpers.
#include "Editor/Infrastructure/EditorTypography.hpp"

#include "Editor/Infrastructure/EditorFonts.hpp"

#include "imgui.h"

#include <cstdarg>
#include <cstdio>

namespace Fby::Editor
{
namespace
{
EditorFonts* GFonts = nullptr;
}

void SetEditorFonts(EditorFonts* InFonts) noexcept
{
    GFonts = InFonts;
}

bool BeginEditorPanel(const char* InTitle, bool* InOpen, int InFlags)
{
    ImFont* ScopedTitle = GFonts != nullptr ? GFonts->PanelTitle() : nullptr;
    if (ScopedTitle != nullptr)
    {
        ImGui::PushFont(ScopedTitle);
    }
    const bool ScopedOpen = ImGui::Begin(InTitle, InOpen, InFlags);
    if (ScopedTitle != nullptr)
    {
        ImGui::PopFont();
    }
    if (GFonts != nullptr && GFonts->Regular() != nullptr)
    {
        ImGui::PushFont(GFonts->Regular());
    }
    return ScopedOpen;
}

void EndEditorPanel()
{
    if (GFonts != nullptr && GFonts->Regular() != nullptr)
    {
        ImGui::PopFont();
    }
    ImGui::End();
}

void EditorTooltip(const char* InFormat, ...)
{
    char ScopedText[1024] = {};
    if (InFormat != nullptr)
    {
        va_list ScopedArguments;
        va_start(ScopedArguments, InFormat);
        std::vsnprintf(ScopedText, sizeof(ScopedText), InFormat, ScopedArguments);
        va_end(ScopedArguments);
    }
    ImFont* ScopedFont = GFonts != nullptr ? GFonts->Tooltip() : nullptr;
    if (ScopedFont != nullptr)
    {
        ImGui::PushFont(ScopedFont);
    }
    ImGui::SetTooltip("%s", ScopedText);
    if (ScopedFont != nullptr)
    {
        ImGui::PopFont();
    }
}

void BeginEditorTooltip(float InWrapWidth)
{
    ImFont* ScopedFont = GFonts != nullptr ? GFonts->Tooltip() : nullptr;
    if (ScopedFont != nullptr)
    {
        ImGui::PushFont(ScopedFont);
    }
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(InWrapWidth);
}

void EndEditorTooltip()
{
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
    if (GFonts != nullptr && GFonts->Tooltip() != nullptr)
    {
        ImGui::PopFont();
    }
}

void EditorTooltipParagraph(const char* InText)
{
    if (InText == nullptr || InText[0] == '\0')
    {
        return;
    }
    ImGui::TextUnformatted(InText);
    ImGui::Spacing();
}

void EditorTooltipLegendRow(const char* InLabel, unsigned int InColorRgba)
{
    const float ScopedAlpha = static_cast<float>((InColorRgba >> 24) & 0xFFu) / 255.0f;
    const ImVec4 ScopedColor{static_cast<float>(InColorRgba & 0xFFu) / 255.0f,
                             static_cast<float>((InColorRgba >> 8) & 0xFFu) / 255.0f,
                             static_cast<float>((InColorRgba >> 16) & 0xFFu) / 255.0f,
                             ScopedAlpha > 0.0f ? ScopedAlpha : 1.0f};
    ImGui::ColorButton("##legend", ScopedColor, ImGuiColorEditFlags_NoTooltip, ImVec2(14.0f, 14.0f));
    ImGui::SameLine();
    ImGui::TextUnformatted(InLabel != nullptr ? InLabel : "");
}
}
