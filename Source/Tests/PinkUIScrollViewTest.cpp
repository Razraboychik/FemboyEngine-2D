// FemboyEngine2D - ScrollView controller over a clipped panel.
#include "Engine/UI/PinkUI.hpp"

#include <cmath>
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
    ScopedCanvas.SetCanvasSize(Vector2D{100.0f, 100.0f});

    WidgetDesc ScopedPanelDesc;
    ScopedPanelDesc.kind = WidgetKind::Panel;
    const std::int32_t ScopedPanel = ScopedCanvas.AddWidget(ScopedPanelDesc);

    WidgetDesc ScopedContentDesc;
    ScopedContentDesc.kind = WidgetKind::Panel;
    ScopedContentDesc.parent = ScopedPanel;
    ScopedContentDesc.anchorMin = Vector2D{0.0f, 0.0f};
    ScopedContentDesc.anchorMax = Vector2D{0.0f, 0.0f};
    ScopedContentDesc.offsetMin = Vector2D{0.0f, 0.0f};
    ScopedContentDesc.offsetMax = Vector2D{100.0f, 300.0f};
    const std::int32_t ScopedContent = ScopedCanvas.AddWidget(ScopedContentDesc);

    WidgetDesc ScopedBarDesc;
    ScopedBarDesc.kind = WidgetKind::Scrollbar;
    ScopedBarDesc.parent = ScopedPanel;
    ScopedBarDesc.anchorMin = Vector2D{1.0f, 0.0f};
    ScopedBarDesc.anchorMax = Vector2D{1.0f, 1.0f};
    ScopedBarDesc.offsetMin = Vector2D{-8.0f, 0.0f};
    ScopedBarDesc.offsetMax = Vector2D{0.0f, 0.0f};
    const std::int32_t ScopedBar = ScopedCanvas.AddWidget(ScopedBarDesc);

    ScrollView ScopedScroll;
    ScopedScroll.Bind(ScopedCanvas, ScopedCanvas.Handle(ScopedPanel), ScopedCanvas.Handle(ScopedContent),
                      ScopedCanvas.Handle(ScopedBar));
    Check(ScopedScroll.Offset() == 0.0f, "bind starts at the top");
    Check(std::fabs(ScopedScroll.MaxOffset(ScopedCanvas) - 200.0f) < 0.001f, "max offset is content minus viewport");

    ScopedScroll.Update(ScopedCanvas, -1.0f);
    Check(std::fabs(ScopedScroll.Offset() - 48.0f) < 0.001f, "one wheel notch scrolls 48 units");
    const Rect ScopedMoved = ScopedCanvas.RectOf(ScopedCanvas.Handle(ScopedContent));
    Check(std::fabs(ScopedMoved.min.Y + 48.0f) < 0.001f, "content rect follows the offset");
    Check(ScopedCanvas.progresses[static_cast<std::uint32_t>(ScopedBar)] > 0.0f, "scrollbar progress follows");

    ScopedScroll.Update(ScopedCanvas, -100.0f);
    Check(std::fabs(ScopedScroll.Offset() - 200.0f) < 0.001f, "offset clamps at the bottom");
    Check(std::fabs(ScopedCanvas.progresses[static_cast<std::uint32_t>(ScopedBar)] - 1.0f) < 0.001f,
          "scrollbar reaches the end");
    Check(ScopedCanvas.ratios[static_cast<std::uint32_t>(ScopedBar)] < 0.34f, "scrollbar thumb reflects the ratio");

    ScopedScroll.Update(ScopedCanvas, 100.0f);
    Check(ScopedScroll.Offset() == 0.0f, "offset clamps at the top");

    if (GFailures == 0)
    {
        std::printf("pinkui_scrollview: OK\n");
        return 0;
    }
    std::fprintf(stderr, "pinkui_scrollview: %d failure(s)\n", GFailures);
    return 1;
}
