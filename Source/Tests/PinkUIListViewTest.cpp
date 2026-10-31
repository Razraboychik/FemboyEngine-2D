// FemboyEngine2D - virtualised ListView.
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
    ScopedCanvas.SetCanvasSize(Vector2D{200.0f, 100.0f});

    WidgetDesc ScopedViewportDesc;
    ScopedViewportDesc.kind = WidgetKind::Panel;
    ScopedViewportDesc.flags = static_cast<std::uint32_t>(WidgetFlags::Visible) |
                               static_cast<std::uint32_t>(WidgetFlags::Enabled) |
                               static_cast<std::uint32_t>(WidgetFlags::ClipChildren);
    const std::int32_t ScopedViewport = ScopedCanvas.AddWidget(ScopedViewportDesc);

    WidgetDesc ScopedContentDesc;
    ScopedContentDesc.kind = WidgetKind::Panel;
    ScopedContentDesc.parent = ScopedViewport;
    const std::int32_t ScopedContent = ScopedCanvas.AddWidget(ScopedContentDesc);

    ListView ScopedList;
    ScopedList.rowHeight = 20.0f;
    ScopedList.Bind(ScopedCanvas, ScopedCanvas.Handle(ScopedViewport), ScopedCanvas.Handle(ScopedContent), 5);
    Check(ScopedList.RowWidget(0) != kNoWidget, "bind creates the row pool");
    ScopedList.SetItemCount(ScopedCanvas, 100);
    Check(ScopedList.FirstVisible() == 0, "the list starts at the first item");
    Check(ScopedList.ItemAtRow(2) == 2, "row to item mapping");
    Check(ScopedCanvas.IsVisible(ScopedList.RowWidget(4)), "the last pool row is visible");
    const Rect ScopedFirstRow = ScopedCanvas.RectOf(ScopedCanvas.Handle(ScopedList.RowWidget(0)));
    Check(std::fabs(ScopedFirstRow.min.Y) < 0.001f, "the first row sits at the top");

    ScopedList.Update(ScopedCanvas, -4.0f);
    Check(std::fabs(ScopedList.offset - 192.0f) < 0.001f, "wheel scrolls 48 per notch");
    Check(ScopedList.FirstVisible() == 9, "the first visible item follows the offset");
    const Rect ScopedScrolled = ScopedCanvas.RectOf(ScopedCanvas.Handle(ScopedList.RowWidget(0)));
    Check(std::fabs(ScopedScrolled.min.Y + 12.0f) < 0.001f, "rows are positioned inside the viewport");

    ScopedList.Update(ScopedCanvas, -1000.0f);
    Check(std::fabs(ScopedList.offset - 1900.0f) < 0.001f, "offset clamps at content minus viewport");
    Check(ScopedList.ItemAtRow(4) == 99, "the last item is the last row");

    ScopedList.SetItemCount(ScopedCanvas, 3);
    Check(ScopedList.offset == 0.0f, "shrinking the list clamps the offset");
    Check(ScopedCanvas.IsVisible(ScopedList.RowWidget(2)), "the third row stays visible");
    Check(!ScopedCanvas.IsVisible(ScopedList.RowWidget(3)), "rows past the item count are hidden");

    if (GFailures == 0)
    {
        std::printf("pinkui_listview: OK\n");
        return 0;
    }
    std::fprintf(stderr, "pinkui_listview: %d failure(s)\n", GFailures);
    return 1;
}
