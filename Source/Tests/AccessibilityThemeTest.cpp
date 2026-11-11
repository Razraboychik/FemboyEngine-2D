// FemboyEngine2D - runtime accessibility palette registry tests.
#include "Engine/UI/AccessibilityTheme.hpp"

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
}

int main()
{
    using namespace Fby::Ui;
    AccessibilityThemeRegistry ScopedRegistry;
    const AccessibilityTheme ScopedDefault = DefaultAccessibilityTheme();
    Check(ScopedRegistry.Register(ScopedDefault), "default theme registers");
    Check(ScopedRegistry.Active() != nullptr && std::string_view(ScopedRegistry.Active()->Id) == "Default",
          "first theme becomes active");
    Check(!ScopedRegistry.Register(ScopedDefault), "duplicate id is rejected");

    const AccessibilityTheme ScopedNvg = MakeNvgTheme();
    Check(ScopedRegistry.Register(ScopedNvg), "NVG theme registers");
    Check(ScopedRegistry.SetActive("Rainbow.Nvg"), "NVG becomes active");
    Check(ScopedRegistry.Active() != nullptr && ScopedRegistry.Active()->Resolve(AccessibilitySlot::Text).A() != 0,
          "active theme has text colour");

    const AccessibilityTheme ScopedThermal = MakeThermalTheme();
    Check(ScopedRegistry.Register(ScopedThermal), "thermal theme registers");
    UiThemeAsset ScopedUi = ScopedRegistry.BuildUiTheme();
    Check(std::string_view(ScopedUi.name) == "NVG", "active theme name reaches the UI theme");
    Check(ScopedUi.Resolve(UiPaletteSlot::Text).A() != 0, "UI theme has readable text");

    AccessibilityThemeRegistry ScopedOwned;
    Check(ScopedOwned.Register(ScopedNvg, reinterpret_cast<const void*>(1)), "owned theme registers");
    Check(ScopedOwned.Count() == 1, "owned theme count");
    ScopedOwned.RemoveOwner(reinterpret_cast<const void*>(1));
    Check(ScopedOwned.Count() == 0 && ScopedOwned.Active() == nullptr, "owner removal clears themes");

    Check(MakeTrafficTheme(true, false, false).Id[0] != '\0', "protan traffic theme has an id");
    Check(MakeExtendedTrafficTheme().DisplayName[0] != '\0', "extended traffic theme has a name");

    if (GFailures != 0)
    {
        std::printf("%d accessibility theme test failure(s)\n", GFailures);
        return 1;
    }
    std::printf("accessibility theme tests passed\n");
    return 0;
}
