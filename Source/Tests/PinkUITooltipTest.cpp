// FemboyEngine2D - tooltip auto-placement with edge flipping.
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

bool Near(float InA, float InB)
{
    return std::fabs(InA - InB) < 0.001f;
}
}

int main()
{
    using namespace Fby;
    using namespace Fby::Ui;

    WidgetCanvas ScopedCanvas;
    ScopedCanvas.SetCanvasSize(Vector2D{200.0f, 100.0f});

    WidgetDesc ScopedTargetDesc;
    ScopedTargetDesc.kind = WidgetKind::Button;
    ScopedTargetDesc.anchorMin = Vector2D{0.0f, 0.0f};
    ScopedTargetDesc.anchorMax = Vector2D{0.0f, 0.0f};
    ScopedTargetDesc.offsetMin = Vector2D{10.0f, 10.0f};
    ScopedTargetDesc.offsetMax = Vector2D{60.0f, 40.0f};
    const std::int32_t ScopedTarget = ScopedCanvas.AddWidget(ScopedTargetDesc);

    WidgetDesc ScopedTipDesc;
    ScopedTipDesc.kind = WidgetKind::Tooltip;
    ScopedTipDesc.anchorMin = Vector2D{0.0f, 0.0f};
    ScopedTipDesc.anchorMax = Vector2D{0.0f, 0.0f};
    ScopedTipDesc.offsetMin = Vector2D{0.0f, 0.0f};
    ScopedTipDesc.offsetMax = Vector2D{80.0f, 30.0f};
    const std::int32_t ScopedTip = ScopedCanvas.AddWidget(ScopedTipDesc);

    TooltipPlacer ScopedPlacer;
    ScopedPlacer.Bind(ScopedCanvas, ScopedCanvas.Handle(ScopedTip), ScopedCanvas.Handle(ScopedTarget));
    ScopedPlacer.Place(ScopedCanvas);
    Rect ScopedRect = ScopedCanvas.RectOf(ScopedCanvas.Handle(ScopedTip));
    Check(Near(ScopedRect.min.X, 72.0f) && Near(ScopedRect.min.Y, 48.0f), "default placement is below-right");
    Check(Near(ScopedRect.max.X - ScopedRect.min.X, 80.0f) && Near(ScopedRect.max.Y - ScopedRect.min.Y, 30.0f),
          "the tooltip keeps its size");

    // A target at the bottom-right corner flips the tooltip left and up.
    WidgetDesc ScopedCornerDesc = ScopedTargetDesc;
    ScopedCornerDesc.offsetMin = Vector2D{150.0f, 80.0f};
    ScopedCornerDesc.offsetMax = Vector2D{190.0f, 95.0f};
    const std::int32_t ScopedCorner = ScopedCanvas.AddWidget(ScopedCornerDesc);
    ScopedPlacer.target = ScopedCorner;
    ScopedPlacer.Place(ScopedCanvas);
    ScopedRect = ScopedCanvas.RectOf(ScopedCanvas.Handle(ScopedTip));
    Check(Near(ScopedRect.min.X, 58.0f), "the tooltip flips to the left of the target");
    Check(Near(ScopedRect.min.Y, 42.0f), "the tooltip flips above the target");

    // A tooltip larger than the canvas is clamped inside it.
    ScopedPlacer.tooltipSize = Vector2D{400.0f, 200.0f};
    ScopedPlacer.Place(ScopedCanvas);
    ScopedRect = ScopedCanvas.RectOf(ScopedCanvas.Handle(ScopedTip));
    Check(ScopedRect.min.X >= 0.0f && ScopedRect.min.Y >= 0.0f, "an oversized tooltip is clamped");

    if (GFailures == 0)
    {
        std::printf("pinkui_tooltip: OK\n");
        return 0;
    }
    std::fprintf(stderr, "pinkui_tooltip: %d failure(s)\n", GFailures);
    return 1;
}
