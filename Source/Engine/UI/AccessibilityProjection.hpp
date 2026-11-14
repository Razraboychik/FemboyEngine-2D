// FemboyEngine2D - projects the retained PinkUI canvas into the semantic accessibility tree.
#pragma once

#include "Engine/UI/AccessibilityTree.hpp"
#include "Engine/UI/PinkUI.hpp"

#include <cstdint>

namespace Fby::Ui
{

struct AccessibilityProjectionStats
{
    std::uint32_t Nodes = 0;
    std::uint32_t SkippedInvisible = 0;
    // Nodes the tree capacity refused after kMaxAccessibilityNodes was reached.
    std::uint32_t Overflow = 0;
    // Widgets whose accessible name was clipped by the tree text pool.
    std::uint32_t TruncatedNames = 0;
};

// Stable id of a widget node. The canvas generation invalidates every id after
// Clear(), so a screen-reader element never points at a recycled slot.
[[nodiscard]] std::uint32_t MakeWidgetNodeId(std::uint32_t InCanvasGeneration, std::uint32_t InWidget) noexcept;

// Role of a widget. The modal overlay wins over the plain panel, because a
// screen reader must announce the dialog boundary.
[[nodiscard]] AccessibilityRole RoleOfWidget(WidgetKind InKind, std::uint32_t InFlags) noexcept;

// Rebuilds InTree from the canvas. Invisible widgets are skipped; a child of an
// invisible parent attaches to its nearest visible ancestor. Returns the number
// of projected nodes and fills the optional statistics.
std::uint32_t ProjectWidgetCanvas(const WidgetCanvas& InCanvas, AccessibilityTree& InTree,
                                  AccessibilityProjectionStats* OutStats = nullptr) noexcept;
}
