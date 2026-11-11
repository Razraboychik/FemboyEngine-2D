// FemboyEngine2D - small editor widgets shared by the viewport and the toolbar.
#include "Editor/Infrastructure/EditorWidgets.hpp"

#include "Editor/Infrastructure/EditorTheme.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace Fby::Editor
{

bool PlaybackIconButton(const char* InId, PlaybackIcon InIcon, bool InEngaged, bool InDisabled, const char* InTooltip)
{
    const float ScopedHeight = ImGui::GetFrameHeight();
    const ImVec2 ScopedSize{ScopedHeight, ScopedHeight};
    ImGui::PushID(InId);
    const bool ScopedClicked = ImGui::InvisibleButton("##icon", ScopedSize);
    const bool ScopedHovered = ImGui::IsItemHovered();
    const ImVec2 ScopedMin = ImGui::GetItemRectMin();
    const ImVec2 ScopedMax = ImGui::GetItemRectMax();
    ImGui::PopID();

    float ScopedAlpha = 1.0f;
    if (InEngaged)
    {
        // The engaged function reads as pressed: the whole square turns translucent.
        ScopedAlpha = 0.55f;
    }
    if (InDisabled)
    {
        ScopedAlpha *= 0.45f;
    }

    ImDrawList* ScopedDraw = ImGui::GetWindowDrawList();
    const EditorPalette& ScopedPalette = CurrentEditorPalette();
    const ImVec4 ScopedBackground =
        HexColor(ScopedHovered && !InDisabled ? ScopedPalette.FrameHover : ScopedPalette.Frame, ScopedAlpha);
    ScopedDraw->AddRectFilled(ScopedMin, ScopedMax, ImGui::GetColorU32(ScopedBackground), 4.0f);
    const ImVec4 ScopedBorder = HexColor(ScopedPalette.Border, ScopedAlpha);
    ScopedDraw->AddRect(ScopedMin, ScopedMax, ImGui::GetColorU32(ScopedBorder), 4.0f);

    EditorSymbolRole ScopedRole = EditorSymbolRole::Play;
    switch (InIcon)
    {
        case PlaybackIcon::Pause:
            ScopedRole = EditorSymbolRole::Pause;
            break;
        case PlaybackIcon::Step:
            ScopedRole = EditorSymbolRole::Step;
            break;
        case PlaybackIcon::Stop:
            ScopedRole = EditorSymbolRole::Stop;
            break;
        case PlaybackIcon::Multiplayer:
            ScopedRole = EditorSymbolRole::Multiplayer;
            break;
        case PlaybackIcon::Play:
        default:
            break;
    }
    ImVec4 ScopedGlyphColor = SymbolColor(ScopedRole, ScopedAlpha * (InEngaged ? 0.95f : 1.0f));
    if (InEngaged)
    {
        const ImVec4 ScopedText = HexColor(ScopedPalette.Text, ScopedAlpha);
        ScopedGlyphColor.x += (ScopedText.x - ScopedGlyphColor.x) * 0.28f;
        ScopedGlyphColor.y += (ScopedText.y - ScopedGlyphColor.y) * 0.28f;
        ScopedGlyphColor.z += (ScopedText.z - ScopedGlyphColor.z) * 0.28f;
    }
    const ImU32 ScopedGlyph = ImGui::ColorConvertFloat4ToU32(ScopedGlyphColor);
    const ImVec2 ScopedCenter{(ScopedMin.x + ScopedMax.x) * 0.5f, (ScopedMin.y + ScopedMax.y) * 0.5f};
    const float ScopedHalf = ScopedHeight * 0.26f;
    const auto ScopedBar = [&](float InLeft, float InRight)
    {
        ScopedDraw->AddRectFilled(ImVec2(ScopedCenter.x + InLeft, ScopedCenter.y - ScopedHalf),
                                  ImVec2(ScopedCenter.x + InRight, ScopedCenter.y + ScopedHalf), ScopedGlyph, 1.0f);
    };
    const auto ScopedTriangle = [&](float InLeft, float InRight)
    {
        ScopedDraw->AddTriangleFilled(ImVec2(ScopedCenter.x + InLeft, ScopedCenter.y - ScopedHalf),
                                      ImVec2(ScopedCenter.x + InLeft, ScopedCenter.y + ScopedHalf),
                                      ImVec2(ScopedCenter.x + InRight, ScopedCenter.y), ScopedGlyph);
    };
    switch (InIcon)
    {
        case PlaybackIcon::Play:
            ScopedTriangle(-ScopedHalf * 0.75f, ScopedHalf);
            break;
        case PlaybackIcon::Pause:
            ScopedBar(-ScopedHalf * 0.75f, -ScopedHalf * 0.20f);
            ScopedBar(ScopedHalf * 0.20f, ScopedHalf * 0.75f);
            break;
        case PlaybackIcon::Step:
            ScopedTriangle(-ScopedHalf * 0.85f, ScopedHalf * 0.35f);
            ScopedBar(ScopedHalf * 0.55f, ScopedHalf * 0.95f);
            break;
        case PlaybackIcon::Stop:
            ScopedDraw->AddRectFilled(ImVec2(ScopedCenter.x - ScopedHalf * 0.75f, ScopedCenter.y - ScopedHalf * 0.75f),
                                      ImVec2(ScopedCenter.x + ScopedHalf * 0.75f, ScopedCenter.y + ScopedHalf * 0.75f),
                                      ScopedGlyph, 1.5f);
            break;
        case PlaybackIcon::Multiplayer:
            ScopedTriangle(-ScopedHalf * 0.95f, -ScopedHalf * 0.05f);
            ScopedTriangle(-ScopedHalf * 0.05f, ScopedHalf * 0.85f);
            break;
    }

    if (ScopedHovered && InTooltip != nullptr)
    {
        ImGui::SetTooltip("%s", InTooltip);
    }
    return ScopedClicked && !InDisabled;
}

bool EditorSlider(const char* InId, float* InValue, float InMin, float InMax, const char* InFormat, float InWidth,
                  bool InOverflow, bool InLogarithmic, float InTrackHeight, bool InShowValue)
{
    ImGui::PushID(InId);
    const ImVec2 ScopedSize{InWidth, ImGui::GetFrameHeight()};
    ImGui::InvisibleButton("##slider", ScopedSize);
    const bool ScopedActive = ImGui::IsItemActive();
    const bool ScopedHovered = ImGui::IsItemHovered();
    const ImVec2 ScopedMin = ImGui::GetItemRectMin();
    ImGui::PopID();

    const float ScopedCenterY = ScopedMin.y + ScopedSize.y * 0.5f;
    const float ScopedGrabWidth = std::max(10.0f, ScopedSize.y * 0.32f);
    // The grab is deliberately thicker than the track so it reads as a handle,
    // not as a second fill bar. Overflow keeps the old taller variant available.
    const float ScopedGrabHeight = InOverflow ? InTrackHeight * 3.0f : std::max(InTrackHeight * 3.0f, 12.0f);
    const float ScopedTrackLeft = ScopedMin.x + ScopedGrabWidth * 0.5f;
    const float ScopedTrackRight = ScopedMin.x + InWidth - ScopedGrabWidth * 0.5f;
    const float ScopedTrackSpan = std::max(ScopedTrackRight - ScopedTrackLeft, 1.0f);

    const auto ScopedToT = [&](float InName)
    {
        if (InLogarithmic)
        {
            return (std::log(InName) - std::log(InMin)) / (std::log(InMax) - std::log(InMin));
        }
        return (InName - InMin) / (InMax - InMin);
    };
    const auto ScopedFromT = [&](float InT)
    {
        if (InLogarithmic)
        {
            return std::exp(std::log(InMin) + InT * (std::log(InMax) - std::log(InMin)));
        }
        return InMin + InT * (InMax - InMin);
    };

    bool ScopedChanged = false;
    if (ScopedActive)
    {
        const float ScopedMouseX = ImGui::GetIO().MousePos.x;
        const float ScopedT = std::clamp((ScopedMouseX - ScopedTrackLeft) / ScopedTrackSpan, 0.0f, 1.0f);
        const float ScopedValue = std::clamp(ScopedFromT(ScopedT), InMin, InMax);
        if (ScopedValue != *InValue)
        {
            *InValue = ScopedValue;
            ScopedChanged = true;
        }
    }

    const float ScopedT = std::clamp(ScopedToT(std::clamp(*InValue, InMin, InMax)), 0.0f, 1.0f);
    const float ScopedGrabX = ScopedTrackLeft + ScopedT * ScopedTrackSpan;
    ImDrawList* ScopedDraw = ImGui::GetWindowDrawList();
    const ImU32 ScopedTrack = IM_COL32(46, 46, 54, 255);
    const ImU32 ScopedFill = ScopedActive || ScopedHovered ? IM_COL32(126, 126, 138, 255) : IM_COL32(92, 92, 102, 255);
    ScopedDraw->AddRectFilled(ImVec2(ScopedMin.x, ScopedCenterY - InTrackHeight * 0.5f),
                              ImVec2(ScopedMin.x + InWidth, ScopedCenterY + InTrackHeight * 0.5f), ScopedTrack,
                              InTrackHeight * 0.5f);
    ScopedDraw->AddRectFilled(ImVec2(ScopedMin.x, ScopedCenterY - InTrackHeight * 0.5f),
                              ImVec2(ScopedGrabX, ScopedCenterY + InTrackHeight * 0.5f), ScopedFill,
                              InTrackHeight * 0.5f);
    // The grab is mid gray so it stays readable against both the dark track and the
    // white value text; it either fits the track or spills over it.
    ScopedDraw->AddRectFilled(ImVec2(ScopedGrabX - ScopedGrabWidth * 0.5f, ScopedCenterY - ScopedGrabHeight * 0.5f),
                              ImVec2(ScopedGrabX + ScopedGrabWidth * 0.5f, ScopedCenterY + ScopedGrabHeight * 0.5f),
                              IM_COL32(138, 138, 147, 255), 2.0f);

    std::array<char, 64> ScopedText{};
    std::snprintf(ScopedText.data(), ScopedText.size(), InFormat, static_cast<double>(*InValue));
    if (InShowValue)
    {
        const ImVec2 ScopedTextSize = ImGui::CalcTextSize(ScopedText.data());
        ScopedDraw->AddText(
            ImVec2(ScopedMin.x + (InWidth - ScopedTextSize.x) * 0.5f, ScopedCenterY - ScopedTextSize.y * 0.5f),
            ImGui::GetColorU32(ImGuiCol_Text), ScopedText.data());
    }
    return ScopedChanged;
}

}
