// FemboyEngine2D - safe-area layout and resolved-rect queries.
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
    ScopedCanvas.SetCanvasSize(Vector2D{1920.0f, 1080.0f});
    ScopedCanvas.SetSafeArea(Vector2D{40.0f, 20.0f}, Vector2D{30.0f, 10.0f});

    WidgetDesc ScopedRoot;
    ScopedRoot.kind = WidgetKind::Panel;
    const std::int32_t ScopedRootIndex = ScopedCanvas.AddWidget(ScopedRoot);
    const WidgetHandle ScopedHandle = ScopedCanvas.Handle(ScopedRootIndex);
    Check(IsHandleSet(ScopedHandle), "root widget is added");

    const Rect ScopedRect = ScopedCanvas.RectOf(ScopedHandle);
    Check(ScopedRect.min.X == 40.0f && ScopedRect.min.Y == 20.0f, "safe area moves the root min corner");
    Check(ScopedRect.max.X == 1890.0f && ScopedRect.max.Y == 1070.0f, "safe area moves the root max corner");

    // An inset larger than the canvas must not invert the rect.
    ScopedCanvas.SetSafeArea(Vector2D{2000.0f, 2000.0f}, Vector2D{2000.0f, 2000.0f});
    const Rect ScopedClamped = ScopedCanvas.RectOf(ScopedHandle);
    Check(ScopedClamped.max.X >= ScopedClamped.min.X && ScopedClamped.max.Y >= ScopedClamped.min.Y,
          "oversized safe area is clamped");

    WidgetDesc ScopedChild = ScopedRoot;
    ScopedChild.parent = ScopedRootIndex;
    ScopedChild.anchorMin = Vector2D{0.0f, 0.0f};
    ScopedChild.anchorMax = Vector2D{1.0f, 1.0f};
    const WidgetHandle ScopedChildHandle = ScopedCanvas.Handle(ScopedCanvas.AddWidget(ScopedChild));
    ScopedCanvas.SetSafeArea(Vector2D{40.0f, 20.0f}, Vector2D{30.0f, 10.0f});
    const Rect ScopedChildRect = ScopedCanvas.RectOf(ScopedChildHandle);
    Check(ScopedChildRect.min.X == 40.0f && ScopedChildRect.max.X == 1890.0f,
          "a stretched child follows the safe-area reference box");

    if (GFailures == 0)
    {
        std::printf("pinkui_safe_area: OK\n");
        return 0;
    }
    std::fprintf(stderr, "pinkui_safe_area: %d failure(s)\n", GFailures);
    return 1;
}
