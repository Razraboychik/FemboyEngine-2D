// FemboyEngine2D - platform-neutral AT-SPI2 mapping for the Linux bridge.
//
// The D-Bus adapter needs exactly this data: the AT-SPI role string, the
// interface mask and the state bit set for one semantic node. Keeping it in a
// portable translation unit keeps the mapping testable off Linux while
// the Linux adapter stays a thin D-Bus translation layer.
#pragma once

#include "Engine/UI/AccessibilityTree.hpp"

#include <cstdint>

namespace Fby
{

enum class AtspiInterface : std::uint32_t
{
    None = 0,
    Accessible = 1u << 0,
    Component = 1u << 1,
    Action = 1u << 2,
    Text = 1u << 3,
    Value = 1u << 4,
    Selection = 1u << 5,
};

inline constexpr std::uint32_t kAtspiAllInterfaces = (1u << 6) - 1u;

// AT-SPI state bits. The numbering matches the AtspiStateType enum the adapter
// serialises; a test pins the values that matter for the engine roles.
// Bit positions match the AtspiStateType enum: GetState sends the low and the
// high word of this 64-bit set.
enum class AtspiState : std::uint64_t
{
    Enabled = 1ull << 8,
    Focused = 1ull << 12,
    Checked = 1ull << 4,
    Expanded = 1ull << 10,
    Selected = 1ull << 23,
    Showing = 1ull << 25,
    Visible = 1ull << 30,
    Modal = 1ull << 16,
    ReadOnly = 1ull << 43,
};

// AT-SPI role name for one engine role, e.g. "push button" or "page tab".
[[nodiscard]] const char* AtspiRoleName(Ui::AccessibilityRole InRole) noexcept;

// Numeric AtspiRole id from atspi-constants.h; the name is informative, the id
// is what screen readers dispatch on.
[[nodiscard]] std::uint32_t AtspiRoleId(Ui::AccessibilityRole InRole) noexcept;

// Interfaces the node implements. Accessible is always present; Component
// needs a non-empty rectangle; Action needs an activatable role; Text and Value
// follow the widget kind.
[[nodiscard]] std::uint32_t AtspiInterfacesFor(Ui::AccessibilityRole InRole, std::uint32_t InStates,
                                               const Vector4D& InBounds) noexcept;

// State set translated from the engine bits. Offscreen nodes are not Showing.
[[nodiscard]] std::uint64_t AtspiStateBitsFor(std::uint32_t InStates) noexcept;

[[nodiscard]] bool AtspiIsActionable(Ui::AccessibilityRole InRole) noexcept;

// AT-SPI event families the D-Bus adapter emits. Names are the signal names on
// org.a11y.atspi.Event.Object, plus the "focus" family from the registry.
enum class AtspiEventKind : std::uint8_t
{
    StateChanged = 0,
    ChildrenChanged = 1,
    PropertyChanged = 2,
    ValueChanged = 3,
    Focus = 4,
};

[[nodiscard]] AtspiEventKind AtspiEventKindFor(Ui::AccessibilityEventKind InKind) noexcept;
[[nodiscard]] const char* AtspiEventName(AtspiEventKind InKind) noexcept;
}
