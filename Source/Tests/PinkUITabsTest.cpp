// FemboyEngine2D - TabBar over a Switcher.
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

    WidgetDesc ScopedSwitcherDesc;
    ScopedSwitcherDesc.kind = WidgetKind::Switcher;
    const std::int32_t ScopedSwitcher = ScopedCanvas.AddWidget(ScopedSwitcherDesc);

    WidgetDesc ScopedPageDesc;
    ScopedPageDesc.kind = WidgetKind::Panel;
    ScopedPageDesc.parent = ScopedSwitcher;
    const std::int32_t ScopedPageA = ScopedCanvas.AddWidget(ScopedPageDesc);
    const std::int32_t ScopedPageB = ScopedCanvas.AddWidget(ScopedPageDesc);
    (void)ScopedPageA;
    (void)ScopedPageB;

    WidgetDesc ScopedButtonDesc;
    ScopedButtonDesc.kind = WidgetKind::Button;
    const std::int32_t ScopedButtonA = ScopedCanvas.AddWidget(ScopedButtonDesc);
    const std::int32_t ScopedButtonB = ScopedCanvas.AddWidget(ScopedButtonDesc);

    TabBar ScopedTabs;
    ScopedTabs.activeTint = Color32::FromBytes(255, 120, 200);
    ScopedTabs.Bind(ScopedCanvas, ScopedCanvas.Handle(ScopedSwitcher));
    Check(ScopedCanvas.ActivePage(ScopedSwitcher) == 0, "bind selects the first page");
    Check(ScopedTabs.AddTab(ScopedCanvas, ScopedCanvas.Handle(ScopedButtonA)), "first tab adds");
    Check(ScopedTabs.AddTab(ScopedCanvas, ScopedCanvas.Handle(ScopedButtonB)), "second tab adds");
    Check(ScopedTabs.activeTint == ScopedCanvas.tints[static_cast<std::uint32_t>(ScopedButtonA)],
          "first tab starts active");

    Check(ScopedTabs.HandleClick(ScopedCanvas, ScopedButtonB), "click on the second tab is handled");
    Check(ScopedCanvas.ActivePage(ScopedSwitcher) == 1, "the second page becomes active");
    Check(ScopedTabs.ActiveTab(ScopedCanvas) == 1, "ActiveTab reports the selection");
    Check(ScopedCanvas.tints[static_cast<std::uint32_t>(ScopedButtonB)] == ScopedTabs.activeTint,
          "the active tab is tinted");
    Check(ScopedCanvas.tints[static_cast<std::uint32_t>(ScopedButtonA)] != ScopedTabs.activeTint,
          "the previous tab loses the tint");

    Check(!ScopedTabs.HandleClick(ScopedCanvas, ScopedSwitcher), "a click outside the tabs is ignored");
    Check(ScopedCanvas.ActivePage(ScopedSwitcher) == 1, "the ignored click keeps the page");

    if (GFailures == 0)
    {
        std::printf("pinkui_tabs: OK\n");
        return 0;
    }
    std::fprintf(stderr, "pinkui_tabs: %d failure(s)\n", GFailures);
    return 1;
}
