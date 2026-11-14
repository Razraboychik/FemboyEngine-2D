// FemboyEngine2D - PinkUI to semantic accessibility tree projection.
#include "Engine/UI/AccessibilityProjection.hpp"

#include <algorithm>
#include <array>

namespace Fby::Ui
{
namespace
{
[[nodiscard]] bool IsVisible(const WidgetCanvas& InCanvas, std::uint32_t InWidget) noexcept
{
    return HasFlag(InCanvas.flags[InWidget], WidgetFlags::Visible);
}

[[nodiscard]] Vector4D ToScreenBounds(const WidgetCanvas& InCanvas, std::int32_t InWidget) noexcept
{
    const Rect ScopedLocal = InCanvas.RectOf(WidgetHandle{InWidget, InCanvas.Generation});
    const Vector2D ScopedMin = InCanvas.CanvasToScreen(ScopedLocal.min);
    const Vector2D ScopedMax = InCanvas.CanvasToScreen(ScopedLocal.max);
    return Vector4D{ScopedMin.X, ScopedMin.Y, ScopedMax.X, ScopedMax.Y};
}

[[nodiscard]] std::uint32_t StatesOf(const WidgetCanvas& InCanvas, std::uint32_t InWidget) noexcept
{
    const std::uint32_t ScopedFlags = InCanvas.flags[InWidget];
    std::uint32_t ScopedStates = AccessibilityStateNone;
    if (HasFlag(ScopedFlags, WidgetFlags::Enabled))
    {
        ScopedStates |= AccessibilityStateEnabled;
    }
    if (HasFlag(ScopedFlags, WidgetFlags::Checked))
    {
        ScopedStates |= AccessibilityStateChecked;
    }
    if (HasFlag(ScopedFlags, WidgetFlags::ReadOnly))
    {
        ScopedStates |= AccessibilityStateReadOnly;
    }
    if (HasFlag(ScopedFlags, WidgetFlags::Modal))
    {
        ScopedStates |= AccessibilityStateModal;
    }
    if (HasFlag(ScopedFlags, WidgetFlags::LiveRegion))
    {
        ScopedStates |= AccessibilityStateLiveRegion;
    }
    if (InCanvas.focusIndex == static_cast<std::int32_t>(InWidget))
    {
        ScopedStates |= AccessibilityStateFocused;
    }
    const Vector4D ScopedBounds = ToScreenBounds(InCanvas, static_cast<std::int32_t>(InWidget));
    if (ScopedBounds.Z <= 0.0f || ScopedBounds.W <= 0.0f || ScopedBounds.X >= InCanvas.size.X ||
        ScopedBounds.Y >= InCanvas.size.Y)
    {
        ScopedStates |= AccessibilityStateOffscreen;
    }
    return ScopedStates;
}
}

std::uint32_t MakeWidgetNodeId(std::uint32_t InCanvasGeneration, std::uint32_t InWidget) noexcept
{
    // Ten bits cover every widget slot (kMaxWidgets is 512); the rest carries
    // the canvas generation so a Clear() invalidates every previous id.
    const std::uint32_t ScopedGeneration = InCanvasGeneration & 0x003FFFFFu;
    return (ScopedGeneration << 10) | (InWidget & 0x3FFu);
}

AccessibilityRole RoleOfWidget(WidgetKind InKind, std::uint32_t InFlags) noexcept
{
    switch (InKind)
    {
        case WidgetKind::Panel:
        case WidgetKind::Border:
            return AccessibilityRole::Pane;
        case WidgetKind::Button:
            return AccessibilityRole::Button;
        case WidgetKind::Label:
        case WidgetKind::Hypertext:
        case WidgetKind::TextBlock:
            return AccessibilityRole::Text;
        case WidgetKind::Image:
        case WidgetKind::NineSlice:
            return AccessibilityRole::Image;
        case WidgetKind::ProgressBar:
            return AccessibilityRole::ProgressBar;
        case WidgetKind::Spacer:
            return AccessibilityRole::Group;
        case WidgetKind::Slider:
            return AccessibilityRole::Slider;
        case WidgetKind::Checkbox:
            return AccessibilityRole::CheckBox;
        case WidgetKind::Scrollbar:
            return AccessibilityRole::ScrollBar;
        case WidgetKind::Switcher:
            return AccessibilityRole::Tab;
        case WidgetKind::Overlay:
            return HasFlag(InFlags, WidgetFlags::Modal) ? AccessibilityRole::Dialog : AccessibilityRole::Pane;
        case WidgetKind::Custom:
            return AccessibilityRole::Group;
        case WidgetKind::Tooltip:
            return AccessibilityRole::Text;
        case WidgetKind::TextInput:
            return AccessibilityRole::TextInput;
        case WidgetKind::ToggleSwitch:
            return AccessibilityRole::ToggleButton;
        case WidgetKind::RadioButton:
            return AccessibilityRole::RadioButton;
        case WidgetKind::Count:
            break;
    }
    return AccessibilityRole::Unknown;
}

std::uint32_t ProjectWidgetCanvas(const WidgetCanvas& InCanvas, AccessibilityTree& InTree,
                                  AccessibilityProjectionStats* OutStats) noexcept
{
    AccessibilityProjectionStats ScopedStats;
    InTree.Reset();

    // Widget index to node index. kNoAccessibilityNode means the widget was
    // skipped or the tree was full, and its children attach higher up.
    std::array<std::uint32_t, kMaxWidgets> ScopedNodes{};
    ScopedNodes.fill(kNoAccessibilityNode);

    const std::uint32_t ScopedWidgetCount = std::min(InCanvas.widgetCount, kMaxWidgets);
    for (std::uint32_t ScopedWidget = 0; ScopedWidget < ScopedWidgetCount; ++ScopedWidget)
    {
        if (!IsVisible(InCanvas, ScopedWidget))
        {
            ++ScopedStats.SkippedInvisible;
            continue;
        }

        // Nearest visible ancestor that produced a node.
        std::uint32_t ScopedParent = kNoAccessibilityNode;
        std::int32_t ScopedAncestor = InCanvas.parents[ScopedWidget];
        std::uint32_t ScopedDepth = 0;
        while (ScopedAncestor >= 0 && ScopedDepth < kMaxWidgets)
        {
            const std::uint32_t ScopedAncestorIndex = static_cast<std::uint32_t>(ScopedAncestor);
            if (ScopedNodes[ScopedAncestorIndex] != kNoAccessibilityNode)
            {
                ScopedParent = ScopedNodes[ScopedAncestorIndex];
                break;
            }
            ScopedAncestor = InCanvas.parents[ScopedAncestorIndex];
            ++ScopedDepth;
        }

        const std::uint32_t ScopedNameId =
            InCanvas.accessibleNameIds[ScopedWidget] != 0
                ? InCanvas.accessibleNameIds[ScopedWidget]
                : (InCanvas.TextKey(static_cast<std::int32_t>(ScopedWidget)) != 0xFFFFFFFFu
                       ? InCanvas.TextKey(static_cast<std::int32_t>(ScopedWidget))
                       : 0u);
        const std::string_view ScopedText =
            ScopedNameId != 0 ? std::string_view{} : InCanvas.Text(static_cast<std::int32_t>(ScopedWidget));

        const std::uint32_t ScopedNode =
            InTree.AddNode(MakeWidgetNodeId(InCanvas.Generation, ScopedWidget),
                           RoleOfWidget(InCanvas.kinds[ScopedWidget], InCanvas.flags[ScopedWidget]), ScopedParent,
                           ToScreenBounds(InCanvas, static_cast<std::int32_t>(ScopedWidget)),
                           StatesOf(InCanvas, ScopedWidget), ScopedNameId, ScopedText);
        if (ScopedNode == kNoAccessibilityNode)
        {
            ++ScopedStats.Overflow;
            continue;
        }
        ScopedNodes[ScopedWidget] = ScopedNode;
        ++ScopedStats.Nodes;
    }

    const std::int32_t ScopedFocused = FocusedWidget(InCanvas);
    if (ScopedFocused >= 0 && static_cast<std::uint32_t>(ScopedFocused) < ScopedWidgetCount)
    {
        const std::uint32_t ScopedFocusedNode = ScopedNodes[static_cast<std::uint32_t>(ScopedFocused)];
        InTree.SetFocusedId(ScopedFocusedNode != kNoAccessibilityNode ? InTree.At(ScopedFocusedNode).Id : 0u);
    }
    else
    {
        InTree.SetFocusedId(0);
    }

    if (OutStats != nullptr)
    {
        *OutStats = ScopedStats;
    }
    return ScopedStats.Nodes;
}
}
