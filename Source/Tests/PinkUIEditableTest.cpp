// FemboyEngine2D - only an explicit Editable Text widget takes text focus.
#include "Engine/UI/PinkUI.hpp"

#include <cstdio>
#include <cstdlib>

using namespace Fby;
using namespace Fby::Ui;

namespace
{
void Check(bool InCondition, const char* InMessage)
{
    if (!InCondition)
    {
        std::fprintf(stderr, "FAIL: %s\n", InMessage);
        std::exit(1);
    }
}
}

int main()
{
    WidgetCanvas ScopedCanvas;
    ScopedCanvas.SetCanvasSize(Vector2D{200.0f, 200.0f});

    WidgetDesc ScopedLabel;
    ScopedLabel.kind = WidgetKind::Label;
    ScopedLabel.anchorMin = Vector2D{0.0f, 0.0f};
    ScopedLabel.anchorMax = Vector2D{0.0f, 0.0f};
    ScopedLabel.offsetMin = Vector2D{0.0f, 0.0f};
    ScopedLabel.offsetMax = Vector2D{100.0f, 40.0f};
    ScopedLabel.flags = static_cast<std::uint32_t>(WidgetFlags::Visible) |
                        static_cast<std::uint32_t>(WidgetFlags::Enabled);
    const std::int32_t ScopedLabelId = ScopedCanvas.AddWidget(ScopedLabel);
    Check(ScopedLabelId != kNoWidget, "label is added");
    Check(ScopedCanvas.FocusPolicyOf(ScopedLabelId) == FocusPolicy::None, "display text is not focusable");

    WidgetDesc ScopedInput;
    ScopedInput.kind = WidgetKind::TextInput;
    ScopedInput.anchorMin = Vector2D{0.0f, 0.0f};
    ScopedInput.anchorMax = Vector2D{0.0f, 0.0f};
    ScopedInput.offsetMin = Vector2D{0.0f, 60.0f};
    ScopedInput.offsetMax = Vector2D{100.0f, 100.0f};
    ScopedInput.focusPolicy = FocusPolicy::Pointer;
    ScopedInput.flags = static_cast<std::uint32_t>(WidgetFlags::Visible) |
                        static_cast<std::uint32_t>(WidgetFlags::Enabled);
    const std::int32_t ScopedInputId = ScopedCanvas.AddWidget(ScopedInput);
    Check(ScopedInputId != kNoWidget, "editable text is added");

    Check(PointerDown(ScopedCanvas, Vector2D{50.0f, 20.0f}) == ScopedLabelId, "label is hit-tested");
    Check(FocusedWidget(ScopedCanvas) == kNoWidget, "clicking display text does not focus editing");

    Check(PointerDown(ScopedCanvas, Vector2D{50.0f, 80.0f}) == ScopedInputId, "editable text is hit-tested");
    Check(FocusedWidget(ScopedCanvas) == ScopedInputId, "only Editable Text takes focus");

    std::printf("pinkui_editable: ok\n");
    return 0;
}
