// FemboyEngine2D - AT-SPI2 mapping implementation.
#include "Engine/UI/AtspiModel.hpp"

namespace Fby
{
namespace
{
[[nodiscard]] bool HasBounds(const Vector4D& InBounds) noexcept
{
    return InBounds.Z > InBounds.X && InBounds.W > InBounds.Y;
}
}

const char* AtspiRoleName(Ui::AccessibilityRole InRole) noexcept
{
    switch (InRole)
    {
        case Ui::AccessibilityRole::Application:
            return "application";
        case Ui::AccessibilityRole::Window:
            return "frame";
        case Ui::AccessibilityRole::Pane:
        case Ui::AccessibilityRole::Group:
            return "panel";
        case Ui::AccessibilityRole::Button:
            return "push button";
        case Ui::AccessibilityRole::CheckBox:
            return "check box";
        case Ui::AccessibilityRole::RadioButton:
            return "radio button";
        case Ui::AccessibilityRole::ToggleButton:
            return "toggle button";
        case Ui::AccessibilityRole::Slider:
            return "slider";
        case Ui::AccessibilityRole::ScrollBar:
            return "scroll bar";
        case Ui::AccessibilityRole::ProgressBar:
            return "progress bar";
        case Ui::AccessibilityRole::TextInput:
            return "entry";
        case Ui::AccessibilityRole::Text:
            return "label";
        case Ui::AccessibilityRole::List:
            return "list";
        case Ui::AccessibilityRole::ListItem:
            return "list item";
        case Ui::AccessibilityRole::Tree:
            return "tree";
        case Ui::AccessibilityRole::TreeItem:
            return "tree item";
        case Ui::AccessibilityRole::Tab:
            return "page tab";
        case Ui::AccessibilityRole::Menu:
            return "menu";
        case Ui::AccessibilityRole::MenuItem:
            return "menu item";
        case Ui::AccessibilityRole::ComboBox:
            return "combo box";
        case Ui::AccessibilityRole::Separator:
            return "separator";
        case Ui::AccessibilityRole::Image:
            return "image";
        case Ui::AccessibilityRole::Dialog:
            return "dialog";
        case Ui::AccessibilityRole::Unknown:
        case Ui::AccessibilityRole::Count:
            break;
    }
    return "filler";
}

std::uint32_t AtspiRoleId(Ui::AccessibilityRole InRole) noexcept
{
    switch (InRole)
    {
        case Ui::AccessibilityRole::Application:
            return 75;
        case Ui::AccessibilityRole::Window:
            return 23;
        case Ui::AccessibilityRole::Pane:
        case Ui::AccessibilityRole::Group:
            return 39;
        case Ui::AccessibilityRole::Button:
            return 132;
        case Ui::AccessibilityRole::CheckBox:
            return 7;
        case Ui::AccessibilityRole::RadioButton:
            return 44;
        case Ui::AccessibilityRole::ToggleButton:
            return 62;
        case Ui::AccessibilityRole::Slider:
            return 51;
        case Ui::AccessibilityRole::ScrollBar:
            return 48;
        case Ui::AccessibilityRole::ProgressBar:
            return 42;
        case Ui::AccessibilityRole::TextInput:
            return 79;
        case Ui::AccessibilityRole::Text:
            return 29;
        case Ui::AccessibilityRole::List:
            return 31;
        case Ui::AccessibilityRole::ListItem:
            return 32;
        case Ui::AccessibilityRole::Tree:
            return 65;
        case Ui::AccessibilityRole::TreeItem:
            return 91;
        case Ui::AccessibilityRole::Tab:
            return 37;
        case Ui::AccessibilityRole::Menu:
            return 33;
        case Ui::AccessibilityRole::MenuItem:
            return 35;
        case Ui::AccessibilityRole::ComboBox:
            return 11;
        case Ui::AccessibilityRole::Separator:
            return 50;
        case Ui::AccessibilityRole::Image:
            return 27;
        case Ui::AccessibilityRole::Dialog:
            return 16;
        case Ui::AccessibilityRole::Unknown:
        case Ui::AccessibilityRole::Count:
            break;
    }
    return 67;
}

bool AtspiIsActionable(Ui::AccessibilityRole InRole) noexcept
{
    switch (InRole)
    {
        case Ui::AccessibilityRole::Button:
        case Ui::AccessibilityRole::CheckBox:
        case Ui::AccessibilityRole::RadioButton:
        case Ui::AccessibilityRole::ToggleButton:
        case Ui::AccessibilityRole::MenuItem:
        case Ui::AccessibilityRole::Tab:
        case Ui::AccessibilityRole::ListItem:
        case Ui::AccessibilityRole::TreeItem:
            return true;
        default:
            return false;
    }
}

std::uint32_t AtspiInterfacesFor(Ui::AccessibilityRole InRole, std::uint32_t InStates,
                                 const Vector4D& InBounds) noexcept
{
    std::uint32_t ScopedMask = static_cast<std::uint32_t>(AtspiInterface::Accessible);
    if (HasBounds(InBounds) && (InStates & Ui::AccessibilityStateOffscreen) == 0)
    {
        ScopedMask |= static_cast<std::uint32_t>(AtspiInterface::Component);
    }
    if (AtspiIsActionable(InRole) && (InStates & Ui::AccessibilityStateEnabled) != 0)
    {
        ScopedMask |= static_cast<std::uint32_t>(AtspiInterface::Action);
    }
    // Text, Value and Selection are not advertised until the D-Bus adapter
    // implements their methods: a client must never see an interface whose
    // calls would answer with an error.
    return ScopedMask;
}

std::uint64_t AtspiStateBitsFor(std::uint32_t InStates) noexcept
{
    std::uint64_t ScopedBits = 0;
    if ((InStates & Ui::AccessibilityStateEnabled) != 0)
    {
        ScopedBits |= static_cast<std::uint64_t>(AtspiState::Enabled);
    }
    if ((InStates & Ui::AccessibilityStateFocused) != 0)
    {
        ScopedBits |= static_cast<std::uint64_t>(AtspiState::Focused);
    }
    if ((InStates & Ui::AccessibilityStateChecked) != 0)
    {
        ScopedBits |= static_cast<std::uint64_t>(AtspiState::Checked);
    }
    if ((InStates & Ui::AccessibilityStateExpanded) != 0)
    {
        ScopedBits |= static_cast<std::uint64_t>(AtspiState::Expanded);
    }
    if ((InStates & Ui::AccessibilityStateSelected) != 0)
    {
        ScopedBits |= static_cast<std::uint64_t>(AtspiState::Selected);
    }
    if ((InStates & Ui::AccessibilityStateOffscreen) == 0)
    {
        ScopedBits |= static_cast<std::uint64_t>(AtspiState::Showing);
        ScopedBits |= static_cast<std::uint64_t>(AtspiState::Visible);
    }
    if ((InStates & Ui::AccessibilityStateReadOnly) != 0)
    {
        ScopedBits |= static_cast<std::uint64_t>(AtspiState::ReadOnly);
    }
    if ((InStates & Ui::AccessibilityStateModal) != 0)
    {
        ScopedBits |= static_cast<std::uint64_t>(AtspiState::Modal);
    }
    return ScopedBits;
}

AtspiEventKind AtspiEventKindFor(Ui::AccessibilityEventKind InKind) noexcept
{
    switch (InKind)
    {
        case Ui::AccessibilityEventKind::Structure:
            return AtspiEventKind::ChildrenChanged;
        case Ui::AccessibilityEventKind::Focus:
            return AtspiEventKind::Focus;
        case Ui::AccessibilityEventKind::State:
            return AtspiEventKind::StateChanged;
        case Ui::AccessibilityEventKind::Value:
            return AtspiEventKind::ValueChanged;
        case Ui::AccessibilityEventKind::Invoke:
        default:
            return AtspiEventKind::PropertyChanged;
    }
}

const char* AtspiEventName(AtspiEventKind InKind) noexcept
{
    switch (InKind)
    {
        case AtspiEventKind::StateChanged:
            return "object:state-changed";
        case AtspiEventKind::ChildrenChanged:
            return "object:children-changed";
        case AtspiEventKind::PropertyChanged:
            return "object:property-change";
        case AtspiEventKind::ValueChanged:
            return "object:property-change:accessible-value";
        case AtspiEventKind::Focus:
            return "focus";
    }
    return "object:property-change";
}
}
