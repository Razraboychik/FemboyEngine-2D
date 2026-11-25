// FemboyEngine2D - AT-SPI2 mapping tests (portable, runs on every platform).
#include "Engine/UI/AtspiModel.hpp"

#include <cstdio>
#include <cstring>

namespace
{
std::uint32_t GFailures = 0;

#define FBY_CHECK(condition)                                                                                           \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);                                  \
            ++GFailures;                                                                                               \
        }                                                                                                              \
    } while (false)

bool Has(const std::uint32_t InMask, const Fby::AtspiInterface InInterface)
{
    return (InMask & static_cast<std::uint32_t>(InInterface)) != 0;
}
}

int main()
{
    using namespace Fby;
    using Ui::AccessibilityRole;

    // Role strings follow the AT-SPI vocabulary.
    FBY_CHECK(std::strcmp(AtspiRoleName(AccessibilityRole::Button), "push button") == 0);
    FBY_CHECK(std::strcmp(AtspiRoleName(AccessibilityRole::Tab), "page tab") == 0);
    FBY_CHECK(std::strcmp(AtspiRoleName(AccessibilityRole::TextInput), "entry") == 0);
    FBY_CHECK(std::strcmp(AtspiRoleName(AccessibilityRole::Unknown), "filler") == 0);
    FBY_CHECK(AtspiRoleId(AccessibilityRole::Button) == 43);
    FBY_CHECK(AtspiRoleId(AccessibilityRole::CheckBox) == 7);
    FBY_CHECK(AtspiRoleId(AccessibilityRole::TextInput) == 79);
    FBY_CHECK(AtspiRoleId(AccessibilityRole::Text) == 29);
    for (std::uint8_t ScopedRole = 0; ScopedRole < static_cast<std::uint8_t>(AccessibilityRole::Count); ++ScopedRole)
    {
        const char* ScopedName = AtspiRoleName(static_cast<AccessibilityRole>(ScopedRole));
        FBY_CHECK(ScopedName != nullptr && ScopedName[0] != '\0');
    }

    const Vector4D ScopedBounds{0.0f, 0.0f, 100.0f, 40.0f};
    const std::uint32_t ScopedEnabled = Ui::AccessibilityStateEnabled;
    const std::uint32_t ScopedOffscreen = Ui::AccessibilityStateOffscreen;

    // Accessible is always present; Component needs bounds and visibility.
    std::uint32_t ScopedMask = AtspiInterfacesFor(AccessibilityRole::Text, ScopedEnabled, ScopedBounds);
    FBY_CHECK(Has(ScopedMask, AtspiInterface::Accessible));
    FBY_CHECK(Has(ScopedMask, AtspiInterface::Component));
    FBY_CHECK(!Has(ScopedMask, AtspiInterface::Text));
    FBY_CHECK(!Has(ScopedMask, AtspiInterface::Action));
    ScopedMask = AtspiInterfacesFor(AccessibilityRole::Text, ScopedEnabled, Vector4D{});
    FBY_CHECK(!Has(ScopedMask, AtspiInterface::Component));
    ScopedMask = AtspiInterfacesFor(AccessibilityRole::Text, ScopedOffscreen, ScopedBounds);
    FBY_CHECK(!Has(ScopedMask, AtspiInterface::Component));

    // Actionable roles expose Action only when enabled.
    ScopedMask = AtspiInterfacesFor(AccessibilityRole::Button, ScopedEnabled, ScopedBounds);
    FBY_CHECK(Has(ScopedMask, AtspiInterface::Action));
    ScopedMask = AtspiInterfacesFor(AccessibilityRole::Button, 0, ScopedBounds);
    FBY_CHECK(!Has(ScopedMask, AtspiInterface::Action));

    // Only implemented interfaces are advertised: Accessible, Component, Action.
    FBY_CHECK(
        !Has(AtspiInterfacesFor(AccessibilityRole::TextInput, ScopedEnabled, ScopedBounds), AtspiInterface::Text));
    FBY_CHECK(!Has(AtspiInterfacesFor(AccessibilityRole::Slider, ScopedEnabled, ScopedBounds), AtspiInterface::Value));
    FBY_CHECK(
        !Has(AtspiInterfacesFor(AccessibilityRole::List, ScopedEnabled, ScopedBounds), AtspiInterface::Selection));

    // Actionable role list is explicit.
    FBY_CHECK(AtspiIsActionable(AccessibilityRole::Button));
    FBY_CHECK(AtspiIsActionable(AccessibilityRole::CheckBox));
    FBY_CHECK(AtspiIsActionable(AccessibilityRole::MenuItem));
    FBY_CHECK(AtspiIsActionable(AccessibilityRole::TreeItem));
    FBY_CHECK(!AtspiIsActionable(AccessibilityRole::Text));
    FBY_CHECK(!AtspiIsActionable(AccessibilityRole::Slider));

    // State bits: every mapped engine bit appears exactly once.
    std::uint64_t ScopedStates = AtspiStateBitsFor(
        ScopedEnabled | Ui::AccessibilityStateFocused | Ui::AccessibilityStateChecked | Ui::AccessibilityStateExpanded |
        Ui::AccessibilityStateSelected | Ui::AccessibilityStateReadOnly | Ui::AccessibilityStateModal);
    FBY_CHECK((ScopedStates & static_cast<std::uint64_t>(AtspiState::Enabled)) != 0);
    FBY_CHECK((ScopedStates & static_cast<std::uint64_t>(AtspiState::Focused)) != 0);
    FBY_CHECK((ScopedStates & static_cast<std::uint64_t>(AtspiState::Checked)) != 0);
    FBY_CHECK((ScopedStates & static_cast<std::uint64_t>(AtspiState::Expanded)) != 0);
    FBY_CHECK((ScopedStates & static_cast<std::uint64_t>(AtspiState::Selected)) != 0);
    FBY_CHECK((ScopedStates & static_cast<std::uint64_t>(AtspiState::ReadOnly)) != 0);
    FBY_CHECK((ScopedStates & static_cast<std::uint64_t>(AtspiState::Modal)) != 0);
    FBY_CHECK((ScopedStates & static_cast<std::uint64_t>(AtspiState::Showing)) != 0);
    FBY_CHECK((ScopedStates & static_cast<std::uint64_t>(AtspiState::Visible)) != 0);

    // Offscreen clears Showing and Visible but keeps the rest.
    ScopedStates = AtspiStateBitsFor(ScopedOffscreen | ScopedEnabled);
    FBY_CHECK((ScopedStates & static_cast<std::uint64_t>(AtspiState::Showing)) == 0);
    FBY_CHECK((ScopedStates & static_cast<std::uint64_t>(AtspiState::Visible)) == 0);
    FBY_CHECK((ScopedStates & static_cast<std::uint64_t>(AtspiState::Enabled)) != 0);

    // Event families translate one-to-one and keep stable signal names.
    FBY_CHECK(AtspiEventKindFor(Ui::AccessibilityEventKind::Structure) == AtspiEventKind::ChildrenChanged);
    FBY_CHECK(AtspiEventKindFor(Ui::AccessibilityEventKind::Focus) == AtspiEventKind::Focus);
    FBY_CHECK(AtspiEventKindFor(Ui::AccessibilityEventKind::State) == AtspiEventKind::StateChanged);
    FBY_CHECK(AtspiEventKindFor(Ui::AccessibilityEventKind::Value) == AtspiEventKind::ValueChanged);
    FBY_CHECK(std::strcmp(AtspiEventName(AtspiEventKind::StateChanged), "object:state-changed") == 0);
    FBY_CHECK(std::strcmp(AtspiEventName(AtspiEventKind::ChildrenChanged), "object:children-changed") == 0);
    FBY_CHECK(std::strcmp(AtspiEventName(AtspiEventKind::Focus), "focus") == 0);

    if (GFailures == 0)
    {
        std::printf("atspi_model_test: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "atspi_model_test: %u failure(s)\n", GFailures);
    return 1;
}
