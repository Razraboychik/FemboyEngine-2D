// FemboyEngine2D - widget layout TOML round-trip.
#include "Engine/UI/PinkUI.hpp"
#include "Engine/UI/WidgetLayoutAsset.hpp"

#include <cmath>
#include <cstdio>
#include <string>

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

bool SameRect(Fby::Rect InA, Fby::Rect InB)
{
    return std::fabs(InA.min.X - InB.min.X) < 0.001f && std::fabs(InA.min.Y - InB.min.Y) < 0.001f &&
           std::fabs(InA.max.X - InB.max.X) < 0.001f && std::fabs(InA.max.Y - InB.max.Y) < 0.001f;
}
}

int main()
{
    using namespace Fby;
    using namespace Fby::Ui;

    WidgetCanvas ScopedSource;
    ScopedSource.SetCanvasSize(Vector2D{200.0f, 100.0f});

    WidgetDesc ScopedPanelDesc;
    ScopedPanelDesc.kind = WidgetKind::Panel;
    const std::int32_t ScopedPanel = ScopedSource.AddWidget(ScopedPanelDesc);

    WidgetDesc ScopedLabelDesc;
    ScopedLabelDesc.kind = WidgetKind::Label;
    ScopedLabelDesc.parent = ScopedPanel;
    ScopedLabelDesc.anchorMin = Vector2D{0.0f, 0.0f};
    ScopedLabelDesc.anchorMax = Vector2D{0.0f, 0.0f};
    ScopedLabelDesc.offsetMin = Vector2D{12.0f, 10.0f};
    ScopedLabelDesc.offsetMax = Vector2D{112.0f, 34.0f};
    const std::int32_t ScopedLabel = ScopedSource.AddWidget(ScopedLabelDesc);
    Check(ScopedSource.SetText(ScopedLabel, "Hello layout"), "label text is set");

    WidgetDesc ScopedButtonDesc;
    ScopedButtonDesc.kind = WidgetKind::Button;
    ScopedButtonDesc.parent = ScopedPanel;
    ScopedButtonDesc.anchorMin = Vector2D{1.0f, 0.0f};
    ScopedButtonDesc.anchorMax = Vector2D{1.0f, 0.0f};
    ScopedButtonDesc.offsetMin = Vector2D{-96.0f, 10.0f};
    ScopedButtonDesc.offsetMax = Vector2D{-8.0f, 40.0f};
    const std::int32_t ScopedButton = ScopedSource.AddWidget(ScopedButtonDesc);
    ScopedSource.SetTextKey(ScopedButton, 42);

    const std::string ScopedToml = WidgetLayoutAsset::Serialize(ScopedSource);
    Check(ScopedToml.find("[[widget]]") != std::string::npos, "serialized layout has widget entries");
    Check(ScopedToml.find("text = \"Hello layout\"") != std::string::npos, "serialized layout keeps text");
    Check(ScopedToml.find("textKey = 42") != std::string::npos, "serialized layout keeps the key");

    WidgetCanvas ScopedLoaded;
    ScopedLoaded.SetCanvasSize(Vector2D{200.0f, 100.0f});
    std::string ScopedError;
    Check(WidgetLayoutAsset::Load(ScopedLoaded, ScopedToml, &ScopedError), "layout loads");
    Check(ScopedLoaded.widgetCount == 3, "every widget is restored");
    Check(ScopedLoaded.kinds[0] == WidgetKind::Panel && ScopedLoaded.kinds[1] == WidgetKind::Label &&
              ScopedLoaded.kinds[2] == WidgetKind::Button,
          "kinds are restored in order");
    Check(ScopedLoaded.parents[1] == ScopedPanel, "the label parent is restored");
    Check(ScopedLoaded.Text(ScopedLabel) == "Hello layout", "the label text is restored");
    Check(ScopedLoaded.TextKey(ScopedButton) == 42, "the localization key is restored");
    Check(SameRect(ScopedLoaded.RectOf(ScopedLoaded.Handle(ScopedLabel)),
                   ScopedSource.RectOf(ScopedSource.Handle(ScopedLabel))),
          "the label rect is identical after the round-trip");
    Check(SameRect(ScopedLoaded.RectOf(ScopedLoaded.Handle(ScopedButton)),
                   ScopedSource.RectOf(ScopedSource.Handle(ScopedButton))),
          "the button rect is identical after the round-trip");

    // Malformed input is rejected with an error and leaves the canvas empty.
    Check(!WidgetLayoutAsset::Load(ScopedLoaded, "version =", &ScopedError), "bad TOML is rejected");
    Check(!ScopedError.empty(), "a parse error is reported");
    Check(!WidgetLayoutAsset::Load(ScopedLoaded, "version = 1\n[[widget]]\nkind = \"Nope\"\n", &ScopedError),
          "an unknown kind is rejected");
    Check(ScopedLoaded.widgetCount == 0, "a rejected layout leaves the canvas empty");

    if (GFailures == 0)
    {
        std::printf("pinkui_layout_asset: OK\n");
        return 0;
    }
    std::fprintf(stderr, "pinkui_layout_asset: %d failure(s)\n", GFailures);
    return 1;
}
