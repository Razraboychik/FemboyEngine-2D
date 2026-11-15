// FemboyEngine2D - semantic accessibility tree shared by the game UI, the editor and the OS bridges.
#pragma once

#include "Engine/Core/Types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace Fby
{
class LocalizationTable;
}

namespace Fby::Ui
{

// Role vocabulary shared by the projection, the narrator and every bridge. The
// ordering is part of the public ABI: plugins and the editor use the value as
// the index into their localized role-name tables.
enum class AccessibilityRole : std::uint8_t
{
    Unknown = 0,
    Application,
    Window,
    Pane,
    Group,
    Button,
    CheckBox,
    RadioButton,
    ToggleButton,
    Slider,
    ScrollBar,
    ProgressBar,
    TextInput,
    Text,
    List,
    ListItem,
    Tree,
    TreeItem,
    Tab,
    Menu,
    MenuItem,
    ComboBox,
    Separator,
    Image,
    Dialog,
    Count
};

// English fallback for a role. A game that ships localized role names installs
// a Narrator role-name resolver; the engine never embeds translated strings.
[[nodiscard]] const char* AccessibilityRoleName(AccessibilityRole InRole) noexcept;

// Node state bits. They mirror the widget flags one level up the stack so the
// bridge does not have to know about PinkUI.
enum AccessibilityStateBits : std::uint32_t
{
    AccessibilityStateNone = 0,
    AccessibilityStateEnabled = 1u << 0,
    AccessibilityStateFocused = 1u << 1,
    AccessibilityStateChecked = 1u << 2,
    AccessibilityStateExpanded = 1u << 3,
    AccessibilityStateSelected = 1u << 4,
    AccessibilityStateReadOnly = 1u << 5,
    // The node exists but its rect is outside the canvas.
    AccessibilityStateOffscreen = 1u << 6,
    // Changes are announced without waiting for focus.
    AccessibilityStateLiveRegion = 1u << 7,
    AccessibilityStateModal = 1u << 8,
};

inline constexpr std::uint32_t kMaxAccessibilityNodes = 512;
inline constexpr std::uint32_t kMaxAccessibilityTextBytes = 8192;
inline constexpr std::uint32_t kMaxAccessibilityEvents = 64;
inline constexpr std::uint32_t kNoAccessibilityNode = 0xFFFFFFFFu;

// Detached copy of one node. The tree stores the arrays separately (SoA); this
// POD is the shape consumers and tests use, so no one holds a pointer into the
// live storage across a rebuild.
struct AccessibilityNodeView
{
    std::uint32_t Id = 0;
    std::uint32_t ParentIndex = kNoAccessibilityNode;
    AccessibilityRole Role = AccessibilityRole::Unknown;
    std::uint32_t States = AccessibilityStateNone;
    // Localization id of the name; 0 means the node carries literal text.
    std::uint32_t NameId = 0;
    // Localization id of the value (slider position, text input contents).
    std::uint32_t ValueId = 0;
    // Screen-space bounds: X and Y are the minimum corner, Z and W the maximum.
    Vector4D Bounds{};
    // Literal name when NameId is zero.
    std::string_view Name{};
};

enum class AccessibilityEventKind : std::uint8_t
{
    Structure = 0,
    Focus = 1,
    State = 2,
    Value = 3,
    Invoke = 4,
};

struct AccessibilityEvent
{
    AccessibilityEventKind Kind = AccessibilityEventKind::Structure;
    std::uint32_t Id = 0;
    AccessibilityRole Role = AccessibilityRole::Unknown;
    std::uint32_t States = AccessibilityStateNone;
};

// Fixed-capacity semantic tree. The producer rebuilds it every frame: Reset()
// keeps the focus id, so a rebuild does not fire a spurious focus event.
class AccessibilityTree
{
public:
    AccessibilityTree() = default;

    // Clears the nodes and the text pool but keeps the focus id, the bound
    // localization table and the activate handler.
    void Reset() noexcept;

    // Optional localization table. When set, NameOf() resolves NameId through
    // the active locale with the pack default as the fallback.
    void SetLocalization(const LocalizationTable* InTable) noexcept
    {
        MLocalization = InTable;
    }
    [[nodiscard]] const LocalizationTable* Localization() const noexcept
    {
        return MLocalization;
    }

    // Appends one node. ParentIndex must be a node added earlier in this
    // rebuild, or kNoAccessibilityNode for a root. Returns the node index, or
    // kNoAccessibilityNode when the capacity is full.
    [[nodiscard]] std::uint32_t AddNode(std::uint32_t InId, AccessibilityRole InRole, std::uint32_t InParentIndex,
                                        Vector4D InBounds, std::uint32_t InStates, std::uint32_t InNameId,
                                        std::string_view InName) noexcept;

    void SetStates(std::uint32_t InIndex, std::uint32_t InStates) noexcept;
    void SetBounds(std::uint32_t InIndex, Vector4D InBounds) noexcept;
    void SetNameId(std::uint32_t InIndex, std::uint32_t InNameId) noexcept;
    void SetValueId(std::uint32_t InIndex, std::uint32_t InValueId) noexcept;
    [[nodiscard]] bool SetName(std::uint32_t InIndex, std::string_view InName) noexcept;

    [[nodiscard]] std::uint32_t Count() const noexcept
    {
        return MCount;
    }
    [[nodiscard]] AccessibilityNodeView At(std::uint32_t InIndex) const noexcept;
    [[nodiscard]] std::uint32_t FindIndexById(std::uint32_t InId) const noexcept;

    // Child links. Both return kNoAccessibilityNode when there is no child.
    [[nodiscard]] std::uint32_t FirstChildOf(std::uint32_t InIndex) const noexcept;
    [[nodiscard]] std::uint32_t NextSiblingOf(std::uint32_t InIndex) const noexcept;

    // Focus is tracked by stable id, not by index, because the producer rebuilds
    // the index space every frame. Changing it queues a Focus event.
    void SetFocusedId(std::uint32_t InId) noexcept;
    [[nodiscard]] std::uint32_t FocusedId() const noexcept
    {
        return MFocusedId;
    }
    [[nodiscard]] std::uint32_t FocusedIndex() const noexcept
    {
        return MFocusedId == 0 ? kNoAccessibilityNode : FindIndexById(MFocusedId);
    }

    // Resolved name: the localized string for NameId, otherwise the literal
    // text. Writes at most InCapacity - 1 bytes and always terminates.
    [[nodiscard]] std::uint32_t NameOf(const AccessibilityNodeView& InNode, char* OutText,
                                       std::size_t InCapacity) const noexcept;
    [[nodiscard]] std::string_view NameOf(const AccessibilityNodeView& InNode) const noexcept;

    // Invoke action. The producer installs a handler (the game canvas activates
    // the focused widget); without one, Activate() reports false.
    using ActivateHandler = bool (*)(void* InUser, std::uint32_t InId) noexcept;
    void SetActivateHandler(ActivateHandler InHandler, void* InUser = nullptr) noexcept;
    [[nodiscard]] bool Activate(std::uint32_t InId) noexcept;

    // Event ring. Bridges drain it once per frame; an overflowing ring keeps the
    // newest events and counts the dropped ones.
    void PushEvent(AccessibilityEventKind InKind, std::uint32_t InIndex) noexcept;
    [[nodiscard]] bool PollEvent(AccessibilityEvent& OutEvent) noexcept;
    [[nodiscard]] std::uint64_t DroppedEvents() const noexcept
    {
        return MDroppedEvents;
    }

private:
    [[nodiscard]] bool AppendText(std::string_view InText, std::uint32_t* OutOffset, std::uint16_t* OutLength) noexcept;

    std::array<std::uint32_t, kMaxAccessibilityNodes> MIds{};
    std::array<std::uint32_t, kMaxAccessibilityNodes> MParents{};
    std::array<AccessibilityRole, kMaxAccessibilityNodes> MRoles{};
    std::array<std::uint32_t, kMaxAccessibilityNodes> MStates{};
    std::array<std::uint32_t, kMaxAccessibilityNodes> MNameIds{};
    std::array<std::uint32_t, kMaxAccessibilityNodes> MValueIds{};
    std::array<Vector4D, kMaxAccessibilityNodes> MBounds{};
    std::array<std::uint32_t, kMaxAccessibilityNodes> MNameOffsets{};
    std::array<std::uint16_t, kMaxAccessibilityNodes> MNameLengths{};
    std::array<char, kMaxAccessibilityTextBytes> MText{};
    std::uint32_t MTextUsed = 0;
    std::uint32_t MCount = 0;

    std::array<AccessibilityEvent, kMaxAccessibilityEvents> MEvents{};
    std::uint32_t MEventHead = 0;
    std::uint32_t MEventTail = 0;
    std::uint64_t MDroppedEvents = 0;

    std::uint32_t MFocusedId = 0;
    // Rolling fingerprint of the current rebuild. A stable screen keeps the
    // bridge silent; a different one publishes a single structure change.
    std::uint64_t MHash = 0;
    std::uint64_t MLastHash = 0;
    const LocalizationTable* MLocalization = nullptr;
    ActivateHandler MActivate = nullptr;
    void* MActivateUser = nullptr;
};
}
