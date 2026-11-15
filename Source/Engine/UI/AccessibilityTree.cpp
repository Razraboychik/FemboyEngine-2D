// FemboyEngine2D - semantic accessibility tree implementation.
#include "Engine/UI/AccessibilityTree.hpp"

#include "Engine/Assets/Localization.hpp"

#include <algorithm>
#include <cstring>

namespace Fby::Ui
{
namespace
{
// FNV-1a offset basis. The structure hash only decides whether the bridge has
// to publish a structure change, so a cheap 64-bit mix is enough.
constexpr std::uint64_t kHashSeed = 1469598103934665603ull;
constexpr std::uint64_t kHashPrime = 1099511628211ull;
}

const char* AccessibilityRoleName(AccessibilityRole InRole) noexcept
{
    switch (InRole)
    {
        case AccessibilityRole::Application:
            return "application";
        case AccessibilityRole::Window:
            return "window";
        case AccessibilityRole::Pane:
            return "pane";
        case AccessibilityRole::Group:
            return "group";
        case AccessibilityRole::Button:
            return "button";
        case AccessibilityRole::CheckBox:
            return "check box";
        case AccessibilityRole::RadioButton:
            return "radio button";
        case AccessibilityRole::ToggleButton:
            return "toggle button";
        case AccessibilityRole::Slider:
            return "slider";
        case AccessibilityRole::ScrollBar:
            return "scroll bar";
        case AccessibilityRole::ProgressBar:
            return "progress bar";
        case AccessibilityRole::TextInput:
            return "text field";
        case AccessibilityRole::Text:
            return "text";
        case AccessibilityRole::List:
            return "list";
        case AccessibilityRole::ListItem:
            return "list item";
        case AccessibilityRole::Tree:
            return "tree";
        case AccessibilityRole::TreeItem:
            return "tree item";
        case AccessibilityRole::Tab:
            return "tab";
        case AccessibilityRole::Menu:
            return "menu";
        case AccessibilityRole::MenuItem:
            return "menu item";
        case AccessibilityRole::ComboBox:
            return "combo box";
        case AccessibilityRole::Separator:
            return "separator";
        case AccessibilityRole::Image:
            return "image";
        case AccessibilityRole::Dialog:
            return "dialog";
        case AccessibilityRole::Unknown:
        case AccessibilityRole::Count:
            break;
    }
    return "element";
}

void AccessibilityTree::Reset() noexcept
{
    // A rebuild that changes the id sequence is a structure change. Re-running
    // the same tree (a static screen) stays silent, which keeps a screen reader
    // from re-reading the whole interface every frame.
    if (MCount > 0 && MHash != MLastHash)
    {
        PushEvent(AccessibilityEventKind::Structure, kNoAccessibilityNode);
    }
    MLastHash = MHash;
    MHash = kHashSeed;
    MCount = 0;
    MTextUsed = 0;
}

bool AccessibilityTree::AppendText(std::string_view InText, std::uint32_t* OutOffset, std::uint16_t* OutLength) noexcept
{
    const std::uint32_t ScopedRemaining = kMaxAccessibilityTextBytes - MTextUsed;
    if (ScopedRemaining == 0 || InText.empty())
    {
        *OutOffset = MTextUsed;
        *OutLength = 0;
        return InText.empty();
    }
    const std::size_t ScopedLength = std::min<std::size_t>(InText.size(), ScopedRemaining);
    std::memcpy(MText.data() + MTextUsed, InText.data(), ScopedLength);
    *OutOffset = MTextUsed;
    *OutLength = static_cast<std::uint16_t>(ScopedLength);
    MTextUsed += static_cast<std::uint32_t>(ScopedLength);
    return ScopedLength == InText.size();
}

std::uint32_t AccessibilityTree::AddNode(std::uint32_t InId, AccessibilityRole InRole, std::uint32_t InParentIndex,
                                         Vector4D InBounds, std::uint32_t InStates, std::uint32_t InNameId,
                                         std::string_view InName) noexcept
{
    if (MCount >= kMaxAccessibilityNodes)
    {
        return kNoAccessibilityNode;
    }
    const std::uint32_t ScopedIndex = MCount++;
    MIds[ScopedIndex] = InId;
    MRoles[ScopedIndex] = InRole;
    MParents[ScopedIndex] = InParentIndex;
    MStates[ScopedIndex] = InStates;
    MNameIds[ScopedIndex] = InNameId;
    MValueIds[ScopedIndex] = 0;
    MBounds[ScopedIndex] = InBounds;

    // A name id with an empty literal keeps the pool untouched: the localized
    // string is resolved on demand and switching locale never rebuilds the UI.
    (void)AppendText(InName, &MNameOffsets[ScopedIndex], &MNameLengths[ScopedIndex]);

    MHash = (MHash ^ InId) * kHashPrime;
    MHash = (MHash ^ static_cast<std::uint64_t>(InRole)) * kHashPrime;
    return ScopedIndex;
}

void AccessibilityTree::SetStates(std::uint32_t InIndex, std::uint32_t InStates) noexcept
{
    if (InIndex >= MCount || MStates[InIndex] == InStates)
    {
        return;
    }
    MStates[InIndex] = InStates;
    PushEvent(AccessibilityEventKind::State, InIndex);
}

void AccessibilityTree::SetBounds(std::uint32_t InIndex, Vector4D InBounds) noexcept
{
    if (InIndex >= MCount)
    {
        return;
    }
    MBounds[InIndex] = InBounds;
}

void AccessibilityTree::SetNameId(std::uint32_t InIndex, std::uint32_t InNameId) noexcept
{
    if (InIndex >= MCount)
    {
        return;
    }
    MNameIds[InIndex] = InNameId;
}

void AccessibilityTree::SetValueId(std::uint32_t InIndex, std::uint32_t InValueId) noexcept
{
    if (InIndex >= MCount || MValueIds[InIndex] == InValueId)
    {
        return;
    }
    MValueIds[InIndex] = InValueId;
    PushEvent(AccessibilityEventKind::Value, InIndex);
}

bool AccessibilityTree::SetName(std::uint32_t InIndex, std::string_view InName) noexcept
{
    if (InIndex >= MCount)
    {
        return false;
    }
    MNameLengths[InIndex] = 0;
    return AppendText(InName, &MNameOffsets[InIndex], &MNameLengths[InIndex]);
}

AccessibilityNodeView AccessibilityTree::At(std::uint32_t InIndex) const noexcept
{
    AccessibilityNodeView ScopedView;
    if (InIndex >= MCount)
    {
        return ScopedView;
    }
    ScopedView.Id = MIds[InIndex];
    ScopedView.ParentIndex = MParents[InIndex];
    ScopedView.Role = MRoles[InIndex];
    ScopedView.States = MStates[InIndex];
    ScopedView.NameId = MNameIds[InIndex];
    ScopedView.ValueId = MValueIds[InIndex];
    ScopedView.Bounds = MBounds[InIndex];
    ScopedView.Name = std::string_view(MText.data() + MNameOffsets[InIndex], MNameLengths[InIndex]);
    return ScopedView;
}

std::uint32_t AccessibilityTree::FindIndexById(std::uint32_t InId) const noexcept
{
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < MCount; ++ScopedIndex)
    {
        if (MIds[ScopedIndex] == InId)
        {
            return ScopedIndex;
        }
    }
    return kNoAccessibilityNode;
}

std::uint32_t AccessibilityTree::FirstChildOf(std::uint32_t InIndex) const noexcept
{
    if (InIndex >= MCount)
    {
        return kNoAccessibilityNode;
    }
    // Linear by design: capacity is bounded, and the bridge asks for a child
    // only when a screen reader actually walks that subtree.
    for (std::uint32_t ScopedIndex = InIndex + 1; ScopedIndex < MCount; ++ScopedIndex)
    {
        if (MParents[ScopedIndex] == InIndex)
        {
            return ScopedIndex;
        }
    }
    return kNoAccessibilityNode;
}

std::uint32_t AccessibilityTree::NextSiblingOf(std::uint32_t InIndex) const noexcept
{
    if (InIndex >= MCount)
    {
        return kNoAccessibilityNode;
    }
    const std::uint32_t ScopedParent = MParents[InIndex];
    for (std::uint32_t ScopedIndex = InIndex + 1; ScopedIndex < MCount; ++ScopedIndex)
    {
        if (MParents[ScopedIndex] == ScopedParent)
        {
            return ScopedIndex;
        }
    }
    return kNoAccessibilityNode;
}

void AccessibilityTree::SetFocusedId(std::uint32_t InId) noexcept
{
    if (MFocusedId == InId)
    {
        return;
    }
    MFocusedId = InId;
    const std::uint32_t ScopedIndex = InId == 0 ? kNoAccessibilityNode : FindIndexById(InId);
    if (ScopedIndex != kNoAccessibilityNode)
    {
        MStates[ScopedIndex] |= AccessibilityStateFocused;
    }
    PushEvent(AccessibilityEventKind::Focus, ScopedIndex);
}

std::uint32_t AccessibilityTree::NameOf(const AccessibilityNodeView& InNode, char* OutText,
                                        std::size_t InCapacity) const noexcept
{
    if (OutText == nullptr || InCapacity == 0)
    {
        return 0;
    }
    std::string_view ScopedText;
    if (InNode.NameId != 0 && MLocalization != nullptr)
    {
        ScopedText = MLocalization->FindWithFallback(InNode.NameId);
    }
    if (ScopedText.empty())
    {
        ScopedText = InNode.Name;
    }
    const std::size_t ScopedLength = std::min(ScopedText.size(), InCapacity - 1);
    if (ScopedLength > 0)
    {
        std::memcpy(OutText, ScopedText.data(), ScopedLength);
    }
    OutText[ScopedLength] = '\0';
    return static_cast<std::uint32_t>(ScopedLength);
}

std::string_view AccessibilityTree::NameOf(const AccessibilityNodeView& InNode) const noexcept
{
    if (InNode.NameId != 0 && MLocalization != nullptr)
    {
        const std::string_view ScopedText = MLocalization->FindWithFallback(InNode.NameId);
        if (!ScopedText.empty())
        {
            return ScopedText;
        }
    }
    return InNode.Name;
}

void AccessibilityTree::SetActivateHandler(ActivateHandler InHandler, void* InUser) noexcept
{
    MActivate = InHandler;
    MActivateUser = InUser;
}

bool AccessibilityTree::Activate(std::uint32_t InId) noexcept
{
    if (MActivate == nullptr)
    {
        return false;
    }
    const std::uint32_t ScopedIndex = FindIndexById(InId);
    if (ScopedIndex == kNoAccessibilityNode)
    {
        return false;
    }
    if (!MActivate(MActivateUser, InId))
    {
        return false;
    }
    PushEvent(AccessibilityEventKind::Invoke, ScopedIndex);
    return true;
}

void AccessibilityTree::PushEvent(AccessibilityEventKind InKind, std::uint32_t InIndex) noexcept
{
    const std::uint32_t ScopedCapacity = kMaxAccessibilityEvents;
    if (MEventTail - MEventHead >= ScopedCapacity)
    {
        ++MDroppedEvents;
        return;
    }
    AccessibilityEvent& ScopedEvent = MEvents[MEventTail & (ScopedCapacity - 1)];
    ScopedEvent = AccessibilityEvent{};
    ScopedEvent.Kind = InKind;
    if (InIndex < MCount)
    {
        ScopedEvent.Id = MIds[InIndex];
        ScopedEvent.Role = MRoles[InIndex];
        ScopedEvent.States = MStates[InIndex];
    }
    ++MEventTail;
}

bool AccessibilityTree::PollEvent(AccessibilityEvent& OutEvent) noexcept
{
    if (MEventHead == MEventTail)
    {
        return false;
    }
    OutEvent = MEvents[MEventHead & (kMaxAccessibilityEvents - 1)];
    ++MEventHead;
    return true;
}
}
