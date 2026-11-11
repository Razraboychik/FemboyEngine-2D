// FemboyEngine2D - editor palettes, text contrast and semantic symbol colours.
#include "Editor/Infrastructure/EditorTheme.hpp"

#include "Editor/Infrastructure/EditorSettings.hpp"

#include <algorithm>
#include <cmath>

namespace Fby::Editor
{
namespace
{
std::uint64_t GThemeGeneration = 1;
EditorPalette GCurrentPalette{};

[[nodiscard]] float LinearLuminance(const ImVec4& InColor) noexcept
{
    return 0.2126f * InColor.x + 0.7152f * InColor.y + 0.0722f * InColor.z;
}

[[nodiscard]] float ContrastRatio(const ImVec4& InFirst, const ImVec4& InSecond) noexcept
{
    const float ScopedA = LinearLuminance(InFirst);
    const float ScopedB = LinearLuminance(InSecond);
    const float ScopedLighter = std::max(ScopedA, ScopedB);
    const float ScopedDarker = std::min(ScopedA, ScopedB);
    return (ScopedLighter + 0.05f) / (ScopedDarker + 0.05f);
}

[[nodiscard]] std::uint32_t PackSrgb(const ImVec4& InLinear) noexcept
{
    const auto ScopedChannel = [](float InValue)
    {
        const float Scoped = std::clamp(InValue, 0.0f, 1.0f);
        const float ScopedSrgb =
            Scoped <= 0.0031308f ? 12.92f * Scoped : 1.055f * std::pow(Scoped, 1.0f / 2.4f) - 0.055f;
        return static_cast<std::uint32_t>(std::lround(std::clamp(ScopedSrgb, 0.0f, 1.0f) * 255.0f));
    };
    return (ScopedChannel(InLinear.x) << 16u) | (ScopedChannel(InLinear.y) << 8u) | ScopedChannel(InLinear.z);
}

[[nodiscard]] ImVec4 EnsureContrast(const ImVec4& InColor, const ImVec4& InBackground, float InMinimumRatio) noexcept
{
    if (ContrastRatio(InColor, InBackground) >= InMinimumRatio)
    {
        return InColor;
    }
    const ImVec4 ScopedWhite{1.0f, 1.0f, 1.0f, InColor.w};
    const ImVec4 ScopedBlack{0.0f, 0.0f, 0.0f, InColor.w};
    const ImVec4 ScopedFallback =
        ContrastRatio(ScopedWhite, InBackground) >= ContrastRatio(ScopedBlack, InBackground) ? ScopedWhite : ScopedBlack;
    return ScopedFallback;
}

[[nodiscard]] std::uint32_t Pick(std::uint32_t InCustom, std::uint32_t InTheme) noexcept
{
    return InCustom != 0 ? InCustom : InTheme;
}

void ApplyThemeColors(const EditorPalette& InPalette, float InPanelGlow) noexcept
{
    ImGuiStyle& ScopedStyle = ImGui::GetStyle();
    ScopedStyle = ImGuiStyle{};

    const ImVec4 ScopedBackground = HexColor(InPalette.Background);
    const ImVec4 ScopedPopup = HexColor(InPalette.Popup);
    const ImVec4 ScopedBorder = HexColor(InPalette.Border);
    const ImVec4 ScopedFrame = HexColor(InPalette.Frame);
    const ImVec4 ScopedFrameHover = HexColor(InPalette.FrameHover);
    const ImVec4 ScopedFrameActive = HexColor(InPalette.FrameActive);
    const ImVec4 ScopedHeader = HexColor(InPalette.Header);
    const ImVec4 ScopedHeaderHover = HexColor(InPalette.HeaderHover);
    const ImVec4 ScopedHeaderActive = HexColor(InPalette.HeaderActive);
    const ImVec4 ScopedText = HexColor(InPalette.Text);
    const ImVec4 ScopedTextDisabled = HexColor(InPalette.TextDisabled);
    const ImVec4 ScopedAccent = HexColor(InPalette.Accent);
    const ImVec4 ScopedAccentActive = HexColor(InPalette.AccentActive);

    ImVec4* ScopedColors = ScopedStyle.Colors;
    ScopedColors[ImGuiCol_Text] = ScopedText;
    ScopedColors[ImGuiCol_TextDisabled] = ScopedTextDisabled;
    ScopedColors[ImGuiCol_WindowBg] = ScopedBackground;
    ScopedColors[ImGuiCol_ChildBg] = ScopedBackground;
    ScopedColors[ImGuiCol_PopupBg] = ScopedPopup;
    ScopedColors[ImGuiCol_Border] = ScopedBorder;
    const float ScopedGlow = std::clamp(InPanelGlow, 0.0f, 1.0f);
    ScopedColors[ImGuiCol_BorderShadow] = HexColor(InPalette.Accent, 0.02f + ScopedGlow * 0.10f);

    ScopedColors[ImGuiCol_FrameBg] = ScopedFrame;
    ScopedColors[ImGuiCol_FrameBgHovered] = ScopedFrameHover;
    ScopedColors[ImGuiCol_FrameBgActive] = ScopedFrameActive;

    ScopedColors[ImGuiCol_TitleBg] = ScopedPopup;
    ScopedColors[ImGuiCol_TitleBgActive] = ScopedHeader;
    ScopedColors[ImGuiCol_TitleBgCollapsed] = ScopedBackground;
    ScopedColors[ImGuiCol_MenuBarBg] = ScopedBackground;

    ScopedColors[ImGuiCol_ScrollbarBg] = ScopedBackground;
    ScopedColors[ImGuiCol_ScrollbarGrab] = ScopedBorder;
    ScopedColors[ImGuiCol_ScrollbarGrabHovered] = ScopedFrameHover;
    ScopedColors[ImGuiCol_ScrollbarGrabActive] = ScopedFrameActive;

    ScopedColors[ImGuiCol_CheckMark] = ScopedAccent;
    ScopedColors[ImGuiCol_SliderGrab] = ScopedAccent;
    ScopedColors[ImGuiCol_SliderGrabActive] = ScopedAccentActive;

    ScopedColors[ImGuiCol_Button] = ScopedHeader;
    ScopedColors[ImGuiCol_ButtonHovered] = ScopedHeaderHover;
    ScopedColors[ImGuiCol_ButtonActive] = ScopedHeaderActive;

    ScopedColors[ImGuiCol_Header] = ScopedHeader;
    ScopedColors[ImGuiCol_HeaderHovered] = ScopedHeaderHover;
    ScopedColors[ImGuiCol_HeaderActive] = ScopedHeaderActive;

    ScopedColors[ImGuiCol_Separator] = ScopedBorder;
    ScopedColors[ImGuiCol_SeparatorHovered] = ScopedHeaderHover;
    ScopedColors[ImGuiCol_SeparatorActive] = ScopedHeaderActive;
    ScopedColors[ImGuiCol_ResizeGrip] = ScopedBorder;
    ScopedColors[ImGuiCol_ResizeGripHovered] = ScopedHeaderHover;
    ScopedColors[ImGuiCol_ResizeGripActive] = ScopedHeaderActive;

    ScopedColors[ImGuiCol_Tab] = ScopedPopup;
    ScopedColors[ImGuiCol_TabHovered] = ScopedFrameHover;
    ScopedColors[ImGuiCol_TabSelected] = ScopedHeader;
    ScopedColors[ImGuiCol_TabSelectedOverline] = ScopedAccent;
    ScopedColors[ImGuiCol_TabDimmed] = ScopedPopup;
    ScopedColors[ImGuiCol_TabDimmedSelected] = ScopedHeader;
    ScopedColors[ImGuiCol_TabDimmedSelectedOverline] = ScopedBorder;

    ScopedColors[ImGuiCol_DockingPreview] = HexColor(InPalette.Accent, 0.55f);
    ScopedColors[ImGuiCol_DockingEmptyBg] = ScopedBackground;
    ScopedColors[ImGuiCol_DragDropTarget] = ScopedAccent;
    ScopedColors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.64f);

    ScopedColors[ImGuiCol_PlotLines] = HexColor(InPalette.Plot[0]);
    ScopedColors[ImGuiCol_PlotLinesHovered] = ScopedText;
    ScopedColors[ImGuiCol_PlotHistogram] = HexColor(InPalette.Plot[1]);
    ScopedColors[ImGuiCol_PlotHistogramHovered] = ScopedText;

    ScopedColors[ImGuiCol_TableHeaderBg] = ScopedHeader;
    ScopedColors[ImGuiCol_TableBorderStrong] = ScopedBorder;
    ScopedColors[ImGuiCol_TableBorderLight] = ScopedFrameActive;
    ScopedColors[ImGuiCol_TableRowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    ScopedColors[ImGuiCol_TableRowBgAlt] = ScopedPopup;

    ScopedColors[ImGuiCol_TextSelectedBg] = HexColor(InPalette.Accent, 0.28f);
    ScopedColors[ImGuiCol_NavCursor] = ScopedAccent;
    ScopedColors[ImGuiCol_NavWindowingHighlight] = ScopedAccentActive;
    ScopedColors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.50f);

    ScopedStyle.WindowPadding = ImVec2(8.0f, 8.0f);
    ScopedStyle.FramePadding = ImVec2(6.0f, 4.0f);
    ScopedStyle.CellPadding = ImVec2(4.0f, 2.0f);
    ScopedStyle.ItemSpacing = ImVec2(6.0f, 6.0f);
    ScopedStyle.ItemInnerSpacing = ImVec2(4.0f, 4.0f);
    ScopedStyle.IndentSpacing = 14.0f;
    ScopedStyle.ScrollbarSize = 11.0f;
    ScopedStyle.GrabMinSize = 8.0f;
    ScopedStyle.WindowBorderSize = 1.0f;
    ScopedStyle.ChildBorderSize = 1.0f;
    ScopedStyle.PopupBorderSize = 1.0f;
    ScopedStyle.FrameBorderSize = 1.0f;
    ScopedStyle.TabBorderSize = 1.0f;
    ScopedStyle.TabBarBorderSize = 1.0f;
    ScopedStyle.TabBarOverlineSize = 0.0f;
    ScopedStyle.DockingSeparatorSize = 1.0f;
    ScopedStyle.WindowRounding = 0.0f;
    ScopedStyle.ChildRounding = 0.0f;
    ScopedStyle.FrameRounding = 2.0f;
    ScopedStyle.PopupRounding = 0.0f;
    ScopedStyle.ScrollbarRounding = 0.0f;
    ScopedStyle.GrabRounding = 2.0f;
    ScopedStyle.TabRounding = 0.0f;
    ScopedStyle.ImageBorderSize = 0.0f;
    ScopedStyle.WindowTitleAlign = ImVec2(0.0f, 0.5f);
    ScopedStyle.WindowMenuButtonPosition = ImGuiDir_None;
    ScopedStyle.SeparatorTextBorderSize = 1.0f;
    ScopedStyle.SeparatorTextPadding = ImVec2(8.0f, 3.0f);
    ScopedStyle.DisabledAlpha = 0.60f;
}

[[nodiscard]] EditorPalette BasePalette(EditorThemeId InTheme) noexcept
{
    EditorPalette ScopedPalette{};
    switch (InTheme)
    {
    case EditorThemeId::VeryDark:
        ScopedPalette.Background = 0x08080A;
        ScopedPalette.Popup = 0x0C0C0F;
        ScopedPalette.Border = 0x24242A;
        ScopedPalette.Frame = 0x101014;
        ScopedPalette.FrameHover = 0x1A1A20;
        ScopedPalette.FrameActive = 0x26262E;
        ScopedPalette.Header = 0x121216;
        ScopedPalette.HeaderHover = 0x1E1E24;
        ScopedPalette.HeaderActive = 0x2A2A32;
        ScopedPalette.Text = 0xF2F2F2;
        ScopedPalette.TextDisabled = 0xB8B8BE;
        ScopedPalette.Mono = 0xC8C8CE;
        ScopedPalette.Accent = 0xD05280;
        ScopedPalette.AccentActive = 0xE06290;
        break;
    case EditorThemeId::Midnight:
        ScopedPalette.Background = 0x080D18;
        ScopedPalette.Popup = 0x0D1422;
        ScopedPalette.Border = 0x26344A;
        ScopedPalette.Frame = 0x101A2A;
        ScopedPalette.FrameHover = 0x1A2A40;
        ScopedPalette.FrameActive = 0x243A58;
        ScopedPalette.Header = 0x0C1524;
        ScopedPalette.HeaderHover = 0x18263A;
        ScopedPalette.HeaderActive = 0x243A58;
        ScopedPalette.Text = 0xF2F5FA;
        ScopedPalette.TextDisabled = 0xB8C2D0;
        ScopedPalette.Mono = 0xC8D2E0;
        ScopedPalette.Accent = 0x6AA9FF;
        ScopedPalette.AccentActive = 0x9AC8FF;
        break;
    case EditorThemeId::Forest:
        ScopedPalette.Background = 0x08100B;
        ScopedPalette.Popup = 0x0D1811;
        ScopedPalette.Border = 0x263C2C;
        ScopedPalette.Frame = 0x101C14;
        ScopedPalette.FrameHover = 0x1A2E20;
        ScopedPalette.FrameActive = 0x264430;
        ScopedPalette.Header = 0x0B1610;
        ScopedPalette.HeaderHover = 0x16281C;
        ScopedPalette.HeaderActive = 0x24402C;
        ScopedPalette.Text = 0xF2FAF4;
        ScopedPalette.TextDisabled = 0xB8CCBE;
        ScopedPalette.Mono = 0xC8DCCE;
        ScopedPalette.Accent = 0x7AD69A;
        ScopedPalette.AccentActive = 0xA8F0C0;
        break;
    case EditorThemeId::Plum:
        ScopedPalette.Background = 0x100814;
        ScopedPalette.Popup = 0x180D1E;
        ScopedPalette.Border = 0x3A2648;
        ScopedPalette.Frame = 0x1A1022;
        ScopedPalette.FrameHover = 0x2A1A36;
        ScopedPalette.FrameActive = 0x402850;
        ScopedPalette.Header = 0x160C1C;
        ScopedPalette.HeaderHover = 0x261630;
        ScopedPalette.HeaderActive = 0x3A2448;
        ScopedPalette.Text = 0xFAF2FF;
        ScopedPalette.TextDisabled = 0xCCB8D8;
        ScopedPalette.Mono = 0xDCC8E8;
        ScopedPalette.Accent = 0xD08AFF;
        ScopedPalette.AccentActive = 0xE8B8FF;
        break;
    case EditorThemeId::Amber:
        ScopedPalette.Background = 0x121006;
        ScopedPalette.Popup = 0x1A160A;
        ScopedPalette.Border = 0x504428;
        ScopedPalette.Frame = 0x201A0C;
        ScopedPalette.FrameHover = 0x342A14;
        ScopedPalette.FrameActive = 0x4A3C20;
        ScopedPalette.Header = 0x1C170A;
        ScopedPalette.HeaderHover = 0x302814;
        ScopedPalette.HeaderActive = 0x463A1E;
        ScopedPalette.Text = 0xFFF8E8;
        ScopedPalette.TextDisabled = 0xD0C4A8;
        ScopedPalette.Mono = 0xDCCCA8;
        ScopedPalette.Accent = 0xE0B040;
        ScopedPalette.AccentActive = 0xF4D070;
        break;
    case EditorThemeId::Crimson:
        ScopedPalette.Background = 0x140808;
        ScopedPalette.Popup = 0x1C0C0C;
        ScopedPalette.Border = 0x502828;
        ScopedPalette.Frame = 0x241010;
        ScopedPalette.FrameHover = 0x3A1A1A;
        ScopedPalette.FrameActive = 0x542828;
        ScopedPalette.Header = 0x1E0C0C;
        ScopedPalette.HeaderHover = 0x341616;
        ScopedPalette.HeaderActive = 0x4E2424;
        ScopedPalette.Text = 0xFFF0F0;
        ScopedPalette.TextDisabled = 0xD0B0B0;
        ScopedPalette.Mono = 0xD8B8B8;
        ScopedPalette.Accent = 0xE06060;
        ScopedPalette.AccentActive = 0xFF9090;
        break;
    case EditorThemeId::Graphite:
    default:
        break;
    }
    return ScopedPalette;
}
}

const char* EditorThemeName(EditorThemeId InTheme) noexcept
{
    switch (InTheme)
    {
    case EditorThemeId::VeryDark:
        return "Very dark";
    case EditorThemeId::Midnight:
        return "Midnight RGB";
    case EditorThemeId::Forest:
        return "Forest RGB";
    case EditorThemeId::Plum:
        return "Plum RGB";
    case EditorThemeId::Amber:
        return "Amber";
    case EditorThemeId::Crimson:
        return "Crimson";
    case EditorThemeId::Graphite:
    default:
        return "Graphite";
    }
}

EditorThemeId ParseEditorTheme(std::string_view InName) noexcept
{
    if (InName == "very-dark" || InName == "very dark" || InName == "VeryDark")
    {
        return EditorThemeId::VeryDark;
    }
    if (InName == "midnight" || InName == "midnight-rgb" || InName == "Midnight")
    {
        return EditorThemeId::Midnight;
    }
    if (InName == "forest" || InName == "forest-rgb" || InName == "Forest")
    {
        return EditorThemeId::Forest;
    }
    if (InName == "plum" || InName == "plum-rgb" || InName == "Plum")
    {
        return EditorThemeId::Plum;
    }
    if (InName == "amber" || InName == "dark-amber" || InName == "Amber")
    {
        return EditorThemeId::Amber;
    }
    if (InName == "crimson" || InName == "dark-red" || InName == "Crimson")
    {
        return EditorThemeId::Crimson;
    }
    return EditorThemeId::Graphite;
}

EditorPalette BuildEditorPalette(EditorThemeId InTheme, float InTextContrast, std::uint32_t InTextColor,
                                 float InTextOpacity, bool InCustomSymbols,
                                 const EditorSymbolColors& InColors) noexcept
{
    EditorPalette ScopedPalette = BasePalette(InTheme);
    if (InCustomSymbols)
    {
        ScopedPalette.AxisX = Pick(InColors.AxisX, ScopedPalette.AxisX);
        ScopedPalette.AxisY = Pick(InColors.AxisY, ScopedPalette.AxisY);
        ScopedPalette.Grid = Pick(InColors.Grid, ScopedPalette.Grid);
        ScopedPalette.Play = Pick(InColors.Play, ScopedPalette.Play);
        ScopedPalette.Pause = Pick(InColors.Pause, ScopedPalette.Pause);
        ScopedPalette.Step = Pick(InColors.Step, ScopedPalette.Step);
        ScopedPalette.Stop = Pick(InColors.Stop, ScopedPalette.Stop);
        ScopedPalette.Rotate = Pick(InColors.Rotate, ScopedPalette.Rotate);
        ScopedPalette.Multiplayer = Pick(InColors.Multiplayer, ScopedPalette.Multiplayer);
        for (std::size_t ScopedI = 0; ScopedI < ScopedPalette.Plot.size(); ++ScopedI)
        {
            ScopedPalette.Plot[ScopedI] = Pick(InColors.Plot[ScopedI], ScopedPalette.Plot[ScopedI]);
        }
    }

    const float ScopedTarget = 4.5f * std::clamp(InTextContrast, 0.5f, 2.0f);
    const ImVec4 ScopedBackground = HexColor(ScopedPalette.Background);
    const ImVec4 ScopedAuthoredText = HexColor(InTextColor, std::clamp(InTextOpacity, 0.0f, 1.0f));
    const ImVec4 ScopedText = EnsureContrast(ScopedAuthoredText, ScopedBackground, ScopedTarget);
    const ImVec4 ScopedDisabledInput =
        HexColor(InTextColor, std::clamp(InTextOpacity, 0.0f, 1.0f) * 0.55f);
    const ImVec4 ScopedDisabled =
        EnsureContrast(ScopedDisabledInput, ScopedBackground, std::max(3.0f, ScopedTarget * 0.75f));
    ScopedPalette.Text = PackSrgb(ScopedText);
    ScopedPalette.TextDisabled = PackSrgb(ScopedDisabled);
    return ScopedPalette;
}

const EditorPalette& CurrentEditorPalette() noexcept
{
    return GCurrentPalette;
}

std::uint64_t EditorThemeGeneration() noexcept
{
    return GThemeGeneration;
}

ImVec4 SymbolColor(EditorSymbolRole InRole, float InAlpha) noexcept
{
    const EditorPalette& ScopedPalette = GCurrentPalette;
    std::uint32_t ScopedColor = ScopedPalette.Grid;
    switch (InRole)
    {
    case EditorSymbolRole::AxisX:
        ScopedColor = ScopedPalette.AxisX;
        break;
    case EditorSymbolRole::AxisY:
        ScopedColor = ScopedPalette.AxisY;
        break;
    case EditorSymbolRole::Play:
        ScopedColor = ScopedPalette.Play;
        break;
    case EditorSymbolRole::Pause:
        ScopedColor = ScopedPalette.Pause;
        break;
    case EditorSymbolRole::Step:
        ScopedColor = ScopedPalette.Step;
        break;
    case EditorSymbolRole::Stop:
        ScopedColor = ScopedPalette.Stop;
        break;
    case EditorSymbolRole::Rotate:
        ScopedColor = ScopedPalette.Rotate;
        break;
    case EditorSymbolRole::Multiplayer:
        ScopedColor = ScopedPalette.Multiplayer;
        break;
    case EditorSymbolRole::Plot0:
    case EditorSymbolRole::Plot1:
    case EditorSymbolRole::Plot2:
    case EditorSymbolRole::Plot3:
    {
        const auto ScopedIndex = static_cast<std::size_t>(InRole) - static_cast<std::size_t>(EditorSymbolRole::Plot0);
        ScopedColor = ScopedPalette.Plot[std::min<std::size_t>(ScopedIndex, ScopedPalette.Plot.size() - 1)];
        break;
    }
    case EditorSymbolRole::Grid:
    default:
        break;
    }
    return HexColor(ScopedColor, InAlpha);
}

ImU32 SymbolColorU32(EditorSymbolRole InRole, float InAlpha) noexcept
{
    return ImGui::ColorConvertFloat4ToU32(SymbolColor(InRole, InAlpha));
}

ImVec4 PlotColor(std::uint32_t InIndex, float InAlpha) noexcept
{
    return HexColor(GCurrentPalette.Plot[std::min<std::size_t>(InIndex, GCurrentPalette.Plot.size() - 1)], InAlpha);
}

void ApplyEditorTheme(const EditorSettings& InSettings) noexcept
{
    GCurrentPalette = BuildEditorPalette(ParseEditorTheme(std::string_view(InSettings.theme)), InSettings.textContrast,
                                         InSettings.textColor, InSettings.textOpacity, InSettings.customSymbolColors,
                                         InSettings.symbolColors);
    ++GThemeGeneration;
    ApplyThemeColors(GCurrentPalette, InSettings.panelGlow);
    ImGuiStyle& ScopedStyle = ImGui::GetStyle();
    ScopedStyle.TextBorderSize = InSettings.textOutline ? std::clamp(InSettings.textOutlineSize, 0.5f, 4.0f) : 0.0f;
    ScopedStyle.TextLineSpacing = std::clamp(InSettings.textLineSpacing, -4.0f, 24.0f);
    ScopedStyle.TextLetterSpacing = std::clamp(InSettings.textLetterSpacing, -2.0f, 12.0f);
    ScopedStyle.TextWordSpacing = std::clamp(InSettings.textWordSpacing, -4.0f, 24.0f);
    ScopedStyle.TextBorderColor = HexColor(InSettings.textOutlineColor);
}
}
