// FemboyEngine2D - macOS NSAccessibility provider for the semantic tree.
//
// Compiled only on Apple targets (ARC enabled). The engine tree is rebuilt every
// frame; the provider keeps one NSAccessibilityElement per stable node id and
// answers AppKit queries from the live tree, so VoiceOver always reads the same
// interface the narrator and the Windows bridge read.
#include "Engine/UI/AccessibilityBridge.hpp"

#if defined(__APPLE__)

#include "Engine/Core/Log.hpp"

#include <SDL3/SDL.h>

#import <AppKit/AppKit.h>

#include <cstdint>
#include <string>

namespace Fby
{
namespace
{
NSWindow* MWindow = nullptr;
Ui::AccessibilityTree* MTree = nullptr;
AccessibilityBridgeSettings MSettings{};
bool MActive = false;
NSView* MContentView = nil;
NSMutableDictionary<NSNumber*, id>* MElements = nil;
id MRootElement = nil;

// Declared before the Objective-C class because its methods answer AppKit
// queries through the same element cache.
id ElementForId(std::uint32_t InId) noexcept;

[[nodiscard]] std::uint32_t NodeIndexById(std::uint32_t InId) noexcept
{
    if (MTree == nullptr || InId == 0)
    {
        return Ui::kNoAccessibilityNode;
    }
    return MTree->FindIndexById(InId);
}

[[nodiscard]] Ui::AccessibilityNodeView NodeViewById(std::uint32_t InId) noexcept
{
    const std::uint32_t ScopedIndex = NodeIndexById(InId);
    if (ScopedIndex == Ui::kNoAccessibilityNode)
    {
        return Ui::AccessibilityNodeView{};
    }
    return MTree->At(ScopedIndex);
}
}

}

// One element per stable node id. The class answers AppKit from the live tree,
// so a frame rebuild never leaves a stale label or frame behind.
@interface FbyAccessibilityElement : NSAccessibilityElement
@property(nonatomic, assign) std::uint32_t NodeId;
@end

@implementation FbyAccessibilityElement

- (BOOL)isAccessibilityElement
{
    return YES;
}

- (NSAccessibilityRole)accessibilityRole
{
    const Fby::Ui::AccessibilityNodeView ScopedNode = Fby::NodeViewById(self.NodeId);
    if (ScopedNode.Id == 0)
    {
        return NSAccessibilityGroupRole;
    }
    switch (ScopedNode.Role)
    {
        case Fby::Ui::AccessibilityRole::Button:
            return NSAccessibilityButtonRole;
        case Fby::Ui::AccessibilityRole::CheckBox:
            return NSAccessibilityCheckBoxRole;
        case Fby::Ui::AccessibilityRole::RadioButton:
            return NSAccessibilityRadioButtonRole;
        case Fby::Ui::AccessibilityRole::ToggleButton:
            return NSAccessibilityCheckBoxRole;
        case Fby::Ui::AccessibilityRole::Slider:
            return NSAccessibilitySliderRole;
        case Fby::Ui::AccessibilityRole::ScrollBar:
            return NSAccessibilityScrollBarRole;
        case Fby::Ui::AccessibilityRole::ProgressBar:
            return NSAccessibilityProgressIndicatorRole;
        case Fby::Ui::AccessibilityRole::TextInput:
            return NSAccessibilityTextFieldRole;
        case Fby::Ui::AccessibilityRole::Text:
            return NSAccessibilityStaticTextRole;
        case Fby::Ui::AccessibilityRole::List:
            return NSAccessibilityListRole;
        case Fby::Ui::AccessibilityRole::ListItem:
            return NSAccessibilityRowRole;
        case Fby::Ui::AccessibilityRole::Tree:
            return NSAccessibilityOutlineRole;
        case Fby::Ui::AccessibilityRole::TreeItem:
            return NSAccessibilityRowRole;
        case Fby::Ui::AccessibilityRole::Tab:
            return NSAccessibilityRadioButtonRole;
        case Fby::Ui::AccessibilityRole::Menu:
            return NSAccessibilityMenuRole;
        case Fby::Ui::AccessibilityRole::MenuItem:
            return NSAccessibilityMenuItemRole;
        case Fby::Ui::AccessibilityRole::ComboBox:
            return NSAccessibilityComboBoxRole;
        case Fby::Ui::AccessibilityRole::Separator:
            return NSAccessibilitySplitterRole;
        case Fby::Ui::AccessibilityRole::Image:
            return NSAccessibilityImageRole;
        case Fby::Ui::AccessibilityRole::Dialog:
        case Fby::Ui::AccessibilityRole::Window:
        case Fby::Ui::AccessibilityRole::Pane:
        case Fby::Ui::AccessibilityRole::Group:
        case Fby::Ui::AccessibilityRole::Application:
        case Fby::Ui::AccessibilityRole::Unknown:
        case Fby::Ui::AccessibilityRole::Count:
            break;
    }
    return NSAccessibilityGroupRole;
}

- (NSString*)accessibilityLabel
{
    const Fby::Ui::AccessibilityNodeView ScopedNode = Fby::NodeViewById(self.NodeId);
    if (ScopedNode.Id == 0)
    {
        return nil;
    }
    const std::string_view ScopedName = Fby::MTree->NameOf(ScopedNode);
    if (!ScopedName.empty())
    {
        const std::string ScopedText(ScopedName);
        return [NSString stringWithUTF8String:ScopedText.c_str()];
    }
    return [NSString stringWithUTF8String:Fby::Ui::AccessibilityRoleName(ScopedNode.Role)];
}

- (NSRect)accessibilityFrame
{
    const Fby::Ui::AccessibilityNodeView ScopedNode = Fby::NodeViewById(self.NodeId);
    if (ScopedNode.Id == 0 || Fby::MWindow == nil)
    {
        return NSZeroRect;
    }
    NSView* ScopedView = Fby::MContentView;
    if (ScopedView == nil)
    {
        return NSZeroRect;
    }
    // The tree uses logical pixels with a top-left origin; AppKit expects
    // bottom-left screen coordinates.
    const NSRect ScopedBounds = [ScopedView bounds];
    const NSRect ScopedRect =
        NSMakeRect(ScopedNode.Bounds.X, ScopedBounds.size.height - ScopedNode.Bounds.W,
                   ScopedNode.Bounds.Z - ScopedNode.Bounds.X, ScopedNode.Bounds.W - ScopedNode.Bounds.Y);
    const NSRect ScopedWindowRect = [ScopedView convertRect:ScopedRect toView:nil];
    return [Fby::MWindow convertRectToScreen:ScopedWindowRect];
}

- (NSArray*)accessibilityChildren
{
    const std::uint32_t ScopedIndex = Fby::NodeIndexById(self.NodeId);
    if (ScopedIndex == Fby::Ui::kNoAccessibilityNode)
    {
        return @[];
    }
    NSMutableArray* ScopedChildren = [NSMutableArray array];
    std::uint32_t ScopedChild = Fby::MTree->FirstChildOf(ScopedIndex);
    while (ScopedChild != Fby::Ui::kNoAccessibilityNode)
    {
        const Fby::Ui::AccessibilityNodeView ScopedNode = Fby::MTree->At(ScopedChild);
        id ScopedElement = Fby::ElementForId(ScopedNode.Id);
        if (ScopedElement != nil)
        {
            [ScopedChildren addObject:ScopedElement];
        }
        ScopedChild = Fby::MTree->NextSiblingOf(ScopedChild);
    }
    return ScopedChildren;
}

- (id)accessibilityParent
{
    const Fby::Ui::AccessibilityNodeView ScopedNode = Fby::NodeViewById(self.NodeId);
    if (ScopedNode.Id == 0)
    {
        return Fby::MContentView;
    }
    if (ScopedNode.ParentIndex == Fby::Ui::kNoAccessibilityNode)
    {
        return Fby::MContentView;
    }
    const Fby::Ui::AccessibilityNodeView ScopedParent = Fby::MTree->At(ScopedNode.ParentIndex);
    return Fby::ElementForId(ScopedParent.Id);
}

- (BOOL)isAccessibilityEnabled
{
    const Fby::Ui::AccessibilityNodeView ScopedNode = Fby::NodeViewById(self.NodeId);
    return ScopedNode.Id != 0 && (ScopedNode.States & Fby::Ui::AccessibilityStateEnabled) != 0;
}

- (BOOL)isAccessibilityFocused
{
    const Fby::Ui::AccessibilityNodeView ScopedNode = Fby::NodeViewById(self.NodeId);
    return ScopedNode.Id != 0 && Fby::MTree->FocusedId() == ScopedNode.Id;
}

- (BOOL)isAccessibilitySelected
{
    const Fby::Ui::AccessibilityNodeView ScopedNode = Fby::NodeViewById(self.NodeId);
    return ScopedNode.Id != 0 && (ScopedNode.States & Fby::Ui::AccessibilityStateSelected) != 0;
}

- (BOOL)accessibilityPerformPress
{
    const Fby::Ui::AccessibilityNodeView ScopedNode = Fby::NodeViewById(self.NodeId);
    return ScopedNode.Id != 0 && Fby::MTree->Activate(ScopedNode.Id);
}

@end

namespace Fby
{
namespace
{
[[nodiscard]] id ElementForId(std::uint32_t InId) noexcept
{
    if (InId == 0 || MElements == nil)
    {
        return nil;
    }
    NSNumber* ScopedKey = [NSNumber numberWithUnsignedInt:InId];
    FbyAccessibilityElement* ScopedElement = [MElements objectForKey:ScopedKey];
    if (ScopedElement == nil)
    {
        ScopedElement = [[FbyAccessibilityElement alloc] initWithRole:NSAccessibilityGroupRole
                                                                 frame:NSZeroRect
                                                                 label:@""];
        ScopedElement.NodeId = InId;
        [MElements setObject:ScopedElement forKey:ScopedKey];
    }
    return ScopedElement;
}

[[nodiscard]] std::uint32_t FindRootId() noexcept
{
    if (MTree == nullptr)
    {
        return 0;
    }
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < MTree->Count(); ++ScopedIndex)
    {
        const Ui::AccessibilityNodeView ScopedNode = MTree->At(ScopedIndex);
        if (ScopedNode.ParentIndex == Ui::kNoAccessibilityNode)
        {
            return ScopedNode.Id;
        }
    }
    return 0;
}

void PublishRootIfNeeded() noexcept
{
    if (MContentView == nil)
    {
        return;
    }
    const std::uint32_t ScopedRootId = FindRootId();
    if (ScopedRootId == 0)
    {
        return;
    }
    id ScopedRoot = ElementForId(ScopedRootId);
    if (ScopedRoot == nil)
    {
        return;
    }
    MRootElement = ScopedRoot;
    [MContentView setAccessibilityChildren:@[ ScopedRoot ]];
}
}

[[nodiscard]] bool PlatformAccessibilityBridgeStart(void* InNativeWindow,
                                                    const AccessibilityBridgeSettings& InSettings) noexcept
{
    if (![NSThread isMainThread] || InNativeWindow == nullptr)
    {
        return false;
    }
    SDL_Window* ScopedWindow = static_cast<SDL_Window*>(InNativeWindow);
    const SDL_PropertiesID ScopedProperties = SDL_GetWindowProperties(ScopedWindow);
    if (ScopedProperties == 0)
    {
        return false;
    }
    NSWindow* ScopedNSWindow = (__bridge NSWindow*)SDL_GetPointerProperty(
        ScopedProperties, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
    if (ScopedNSWindow == nil)
    {
        return false;
    }
    MWindow = ScopedNSWindow;
    MContentView = [MWindow contentView];
    if (MContentView == nil)
    {
        return false;
    }
    MSettings = InSettings;
    MActive = true;
    MElements = [NSMutableDictionary dictionary];
    // The view hosts the game canvas; make it an accessibility group and let
    // the tree provide the children. The engine tree owns the semantics.
    [MContentView setAccessibilityElement:YES];
    [MContentView setAccessibilityRole:NSAccessibilityGroupRole];
    [MContentView setAccessibilityLabel:@"Game"];
    [MContentView setAccessibilityChildren:@[]];
    LogInfo("a11y", "macOS accessibility bridge started");
    return true;
}

void PlatformAccessibilityBridgeStop() noexcept
{
    if (MContentView != nil)
    {
        [MContentView setAccessibilityChildren:@[]];
    }
    MElements = nil;
    MRootElement = nil;
    MContentView = nil;
    MWindow = nil;
    MTree = nullptr;
    MActive = false;
}

void PlatformAccessibilityBridgeUpdate(Ui::AccessibilityTree& InTree) noexcept
{
    if (!MActive)
    {
        return;
    }
    MTree = &InTree;
    PublishRootIfNeeded();
    Ui::AccessibilityEvent ScopedEvent{};
    while (InTree.PollEvent(ScopedEvent))
    {
        switch (ScopedEvent.Kind)
        {
            case Ui::AccessibilityEventKind::Focus:
                if (MSettings.RaiseFocusEvents && ScopedEvent.Id != 0)
                {
                    id ScopedElement = ElementForId(ScopedEvent.Id);
                    if (ScopedElement != nil)
                    {
                        NSAccessibilityPostNotification(ScopedElement,
                                                         NSAccessibilityFocusedUIElementChangedNotification);
                    }
                }
                break;
            case Ui::AccessibilityEventKind::Structure:
                if (MSettings.RaiseStructureEvents && MRootElement != nil)
                {
                    PublishRootIfNeeded();
                    NSAccessibilityPostNotification(MRootElement, NSAccessibilityLayoutChangedNotification);
                }
                break;
            case Ui::AccessibilityEventKind::State:
            case Ui::AccessibilityEventKind::Value:
                if (MSettings.RaiseStateEvents && ScopedEvent.Id != 0)
                {
                    id ScopedElement = ElementForId(ScopedEvent.Id);
                    if (ScopedElement != nil)
                    {
                        NSAccessibilityPostNotification(ScopedElement, NSAccessibilityValueChangedNotification);
                    }
                }
                break;
            case Ui::AccessibilityEventKind::Invoke:
            default:
                break;
        }
    }
}

bool PlatformAccessibilityBridgeActive() noexcept
{
    return MActive;
}

const char* PlatformAccessibilityBridgeName() noexcept
{
    return "macOS";
}
}
#endif
