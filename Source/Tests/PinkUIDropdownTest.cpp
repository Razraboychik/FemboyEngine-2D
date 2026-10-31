// FemboyEngine2D - Dropdown/ComboBox over a trigger button and a popup panel.
#include "Engine/UI/PinkUI.hpp"

#include <cstdio>

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

    WidgetCanvas ScopedCanvas;
    ScopedCanvas.SetCanvasSize(Vector2D{320.0f, 200.0f});

    WidgetDesc ScopedTriggerDesc;
    ScopedTriggerDesc.kind = WidgetKind::Button;
    const std::int32_t ScopedTrigger = ScopedCanvas.AddWidget(ScopedTriggerDesc);

    WidgetDesc ScopedPopupDesc;
    ScopedPopupDesc.kind = WidgetKind::Panel;
    const std::int32_t ScopedPopup = ScopedCanvas.AddWidget(ScopedPopupDesc);

    WidgetDesc ScopedItemDesc;
    ScopedItemDesc.kind = WidgetKind::Button;
    ScopedItemDesc.parent = ScopedPopup;
    const std::int32_t ScopedItemA = ScopedCanvas.AddWidget(ScopedItemDesc);
    const std::int32_t ScopedItemB = ScopedCanvas.AddWidget(ScopedItemDesc);
    (void)ScopedCanvas.SetText(ScopedItemA, "Low");
    (void)ScopedCanvas.SetText(ScopedItemB, "High");

    Dropdown ScopedDropdown;
    ScopedDropdown.activeTint = Color32::FromBytes(90, 160, 255);
    ScopedDropdown.Bind(ScopedCanvas, ScopedCanvas.Handle(ScopedTrigger), ScopedCanvas.Handle(ScopedPopup));
    Check(!ScopedCanvas.IsVisible(ScopedPopup), "bind hides the popup");
    Check(ScopedDropdown.AddItem(ScopedCanvas, ScopedCanvas.Handle(ScopedItemA)), "first item adds");
    Check(ScopedDropdown.AddItem(ScopedCanvas, ScopedCanvas.Handle(ScopedItemB)), "second item adds");

    Check(ScopedDropdown.HandleClick(ScopedCanvas, ScopedTrigger), "trigger click is handled");
    Check(ScopedDropdown.open, "trigger opens the dropdown");
    Check(ScopedCanvas.IsVisible(ScopedPopup), "popup becomes visible");

    Check(ScopedDropdown.HandleClick(ScopedCanvas, ScopedItemB), "item click is handled");
    Check(ScopedDropdown.SelectedItem() == 1, "the second item is selected");
    Check(!ScopedDropdown.open, "selection closes the dropdown");
    Check(!ScopedCanvas.IsVisible(ScopedPopup), "popup is hidden again");
    Check(ScopedCanvas.Text(ScopedTrigger) == "High", "the trigger shows the selected caption");
    Check(ScopedCanvas.tints[static_cast<std::uint32_t>(ScopedItemB)] == ScopedDropdown.activeTint,
          "the selected row is tinted");

    Check(!ScopedDropdown.HandleClick(ScopedCanvas, ScopedPopup), "a click on the popup itself is ignored");
    Check(ScopedDropdown.SelectedItem() == 1, "the ignored click keeps the selection");

    if (GFailures == 0)
    {
        std::printf("pinkui_dropdown: OK\n");
        return 0;
    }
    std::fprintf(stderr, "pinkui_dropdown: %d failure(s)\n", GFailures);
    return 1;
}
