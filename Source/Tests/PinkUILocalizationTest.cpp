// FemboyEngine2D - widget localization keys.
#include "Engine/Assets/Localization.hpp"
#include "Engine/Assets/LocalizationCompiler.hpp"
#include "Engine/UI/PinkUI.hpp"

#include <cstdio>
#include <string>
#include <vector>

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
    using namespace Fby;
    using namespace Fby::Ui;

    std::string ScopedError;
    const std::vector<std::byte> ScopedBytes = Assets::CompileLocalization(
        "default = \"en\"\n"
        "[en]\n"
        "42 = \"Play\"\n"
        "[de]\n"
        "42 = \"Spielen\"\n",
        &ScopedError);
    LocalizationTable ScopedTable = LocalizationTable::Open(ScopedBytes, &ScopedError);
    Check(ScopedTable.IsValid(), "localization table opens");

    WidgetCanvas ScopedCanvas;
    ScopedCanvas.SetCanvasSize(Vector2D{200.0f, 100.0f});
    WidgetDesc ScopedDesc;
    ScopedDesc.kind = WidgetKind::Button;
    const std::int32_t ScopedButton = ScopedCanvas.AddWidget(ScopedDesc);
    Check(ScopedCanvas.SetText(ScopedButton, "Play"), "initial caption is set");
    ScopedCanvas.SetTextKey(ScopedButton, 42);
    Check(ScopedCanvas.TextKey(ScopedButton) == 42, "the key is stored");

    Check(ScopedTable.SelectLocale("de"), "German locale is selected");
    Check(ScopedCanvas.ApplyLocalization(ScopedTable) == 1, "one caption is localized");
    Check(ScopedCanvas.Text(ScopedButton) == "Spielen", "the German string replaces the caption");

    Check(ScopedTable.SelectLocale("en"), "English locale is selected");
    Check(ScopedCanvas.ApplyLocalization(ScopedTable) == 1, "the caption is localized again");
    Check(ScopedCanvas.Text(ScopedButton) == "Play", "the English string replaces the caption");

    // A key the active locale lacks falls back to the default locale; an
    // unknown key leaves the previous caption alone.
    ScopedCanvas.SetTextKey(ScopedButton, 7);
    Check(ScopedCanvas.ApplyLocalization(ScopedTable) == 0, "an unknown key writes nothing");
    Check(ScopedCanvas.Text(ScopedButton) == "Play", "an unknown key keeps the caption");

    // Widgets without a key are untouched.
    ScopedCanvas.SetTextKey(ScopedButton, 42);
    const std::int32_t ScopedOther = ScopedCanvas.AddWidget(ScopedDesc);
    (void)ScopedCanvas.SetText(ScopedOther, "Static");
    Check(ScopedCanvas.ApplyLocalization(ScopedTable) == 1, "only keyed captions are localized");
    Check(ScopedCanvas.Text(ScopedOther) == "Static", "an unkeyed caption is untouched");

    if (GFailures == 0)
    {
        std::printf("pinkui_localization: OK\n");
        return 0;
    }
    std::fprintf(stderr, "pinkui_localization: %d failure(s)\n", GFailures);
    return 1;
}
