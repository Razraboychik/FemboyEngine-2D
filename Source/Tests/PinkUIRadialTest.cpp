// FemboyEngine2D - radial progress ring painted with custom quads.
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

struct EmitStats
{
    std::uint32_t quads = 0;
    std::uint32_t fillQuads = 0;
    std::uint32_t trackQuads = 0;
    float lastRotation = 0.0f;
};

std::uint32_t GTrackColor = 0;
std::uint32_t GFillColor = 0;

bool EmitQuad(void* InState, Fby::Rect, std::uint32_t, std::uint32_t InColor, Fby::Vector2D, Fby::Vector2D,
              float InRotation) noexcept
{
    auto* ScopedStats = static_cast<EmitStats*>(InState);
    ++ScopedStats->quads;
    if (InColor == GFillColor)
    {
        ++ScopedStats->fillQuads;
    }
    else if (InColor == GTrackColor)
    {
        ++ScopedStats->trackQuads;
    }
    ScopedStats->lastRotation = InRotation;
    return true;
}
}

int main()
{
    using namespace Fby;
    using namespace Fby::Ui;

    GTrackColor = Color32::FromBytes(40, 40, 48).rgba;
    GFillColor = Color32::FromBytes(255, 120, 200).rgba;

    WidgetCanvas ScopedCanvas;
    ScopedCanvas.SetCanvasSize(Vector2D{100.0f, 100.0f});
    WidgetPaintContext ScopedContext{ScopedCanvas};
    ScopedContext.LocalRect = Rect{Vector2D{0.0f, 0.0f}, Vector2D{100.0f, 100.0f}};
    ScopedContext.Params = Vector4D{0.5f, 0.2f, 16.0f, -1.57079633f};
    ScopedContext.Tint = Color32::FromBytes(40, 40, 48);
    ScopedContext.Fill = Color32::FromBytes(255, 120, 200);
    EmitStats ScopedStats;
    ScopedContext.Emit = &EmitQuad;
    ScopedContext.EmitState = &ScopedStats;

    RadialProgress::Paint(ScopedContext);
    Check(ScopedStats.quads == 16, "one quad per segment");
    Check(ScopedStats.fillQuads == 8, "half of the segments are filled");
    Check(ScopedStats.trackQuads == 8, "the rest is the track");
    Check(std::fabs(ScopedStats.lastRotation - (-1.57079633f + 15.5f * 0.39269908f)) < 0.001f,
          "segments are rotated around the ring");

    // Progress clamps: zero fills nothing, one fills every segment.
    ScopedStats = EmitStats{};
    ScopedContext.Params.X = -1.0f;
    RadialProgress::Paint(ScopedContext);
    Check(ScopedStats.fillQuads == 0, "negative progress clamps to zero");
    ScopedStats = EmitStats{};
    ScopedContext.Params.X = 2.0f;
    RadialProgress::Paint(ScopedContext);
    Check(ScopedStats.fillQuads == 16, "progress above one fills the ring");

    // Configure sets the painter and the parameter block; SetProgress updates it.
    WidgetDesc ScopedDesc;
    ScopedDesc.kind = WidgetKind::Custom;
    const std::int32_t ScopedWidget = ScopedCanvas.AddWidget(ScopedDesc);
    RadialProgress::Configure(ScopedCanvas, ScopedCanvas.Handle(ScopedWidget), 0.25f, 0.3f, 12, 0.0f);
    Check(ScopedCanvas.customPaints[static_cast<std::uint32_t>(ScopedWidget)] == &RadialProgress::Paint,
          "configure installs the painter");
    Check(ScopedCanvas.customParams[static_cast<std::uint32_t>(ScopedWidget)].X == 0.25f, "configure sets progress");
    RadialProgress::SetProgress(ScopedCanvas, ScopedCanvas.Handle(ScopedWidget), 0.75f);
    Check(ScopedCanvas.customParams[static_cast<std::uint32_t>(ScopedWidget)].X == 0.75f,
          "set progress updates the parameter block");

    if (GFailures == 0)
    {
        std::printf("pinkui_radial: OK\n");
        return 0;
    }
    std::fprintf(stderr, "pinkui_radial: %d failure(s)\n", GFailures);
    return 1;
}
