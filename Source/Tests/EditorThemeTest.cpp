// FemboyEngine2D - editor palette and contrast tests.
#include "Editor/Infrastructure/EditorSettings.hpp"
#include "Editor/Infrastructure/EditorTheme.hpp"

#include <cstdio>

namespace
{
int GFailures = 0;

void Check(bool InCondition, const char* InMessage)
{
    if (!InCondition)
    {
        std::printf("FAIL: %s\n", InMessage);
        ++GFailures;
    }
}

[[nodiscard]] float Luminance(const ImVec4& InColor)
{
    return 0.2126f * InColor.x + 0.7152f * InColor.y + 0.0722f * InColor.z;
}

[[nodiscard]] float Contrast(const ImVec4& InFirst, const ImVec4& InSecond)
{
    const float ScopedA = Luminance(InFirst);
    const float ScopedB = Luminance(InSecond);
    const float ScopedLighter = ScopedA > ScopedB ? ScopedA : ScopedB;
    const float ScopedDarker = ScopedA > ScopedB ? ScopedB : ScopedA;
    return (ScopedLighter + 0.05f) / (ScopedDarker + 0.05f);
}
}

int main()
{
    Check(Fby::Editor::ParseEditorTheme("graphite") == Fby::Editor::EditorThemeId::Graphite, "graphite parses");
    Check(Fby::Editor::ParseEditorTheme("very-dark") == Fby::Editor::EditorThemeId::VeryDark,
          "very dark parses");
    Check(Fby::Editor::ParseEditorTheme("midnight") == Fby::Editor::EditorThemeId::Midnight, "midnight parses");
    Check(Fby::Editor::ParseEditorTheme("amber") == Fby::Editor::EditorThemeId::Amber, "amber parses");
    Check(Fby::Editor::ParseEditorTheme("crimson") == Fby::Editor::EditorThemeId::Crimson, "crimson parses");

    const Fby::Editor::EditorPalette ScopedGraphite = Fby::Editor::BuildEditorPalette(
        Fby::Editor::EditorThemeId::Graphite, 1.0f, 0xF2F2F2, 1.0f, false, {});
    const Fby::Editor::EditorPalette ScopedVeryDark = Fby::Editor::BuildEditorPalette(
        Fby::Editor::EditorThemeId::VeryDark, 1.0f, 0xF2F2F2, 1.0f, false, {});
    Check(ScopedGraphite.Background != ScopedVeryDark.Background, "very dark changes panel tints");
    Check(ScopedVeryDark.Background != 0x000000, "very dark is not pure black");

    Fby::Editor::EditorSymbolColors ScopedCustom{};
    ScopedCustom.Play = 0x123456;
    const Fby::Editor::EditorPalette ScopedOverridden = Fby::Editor::BuildEditorPalette(
        Fby::Editor::EditorThemeId::Graphite, 1.0f, 0xF2F2F2, 1.0f, true, ScopedCustom);
    Check(ScopedOverridden.Play == 0x123456, "custom symbol colour overrides the theme");

    const Fby::Editor::EditorPalette ScopedContrast = Fby::Editor::BuildEditorPalette(
        Fby::Editor::EditorThemeId::Graphite, 1.8f, 0xF2F2F2, 1.0f, false, {});
    const float ScopedRatio = Contrast(Fby::Editor::HexColor(ScopedContrast.Text),
                                       Fby::Editor::HexColor(ScopedContrast.Background));
    Check(ScopedRatio >= 4.5f, "text contrast reaches the WCAG target");

    if (GFailures != 0)
    {
        std::printf("%d editor theme test failure(s)\n", GFailures);
        return 1;
    }
    std::printf("editor theme tests passed\n");
    return 0;
}
