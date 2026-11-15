// FemboyEngine2D - OS accessibility bridge: exposes the semantic tree to screen readers.
#pragma once

#include "Engine/UI/AccessibilityTree.hpp"

#include <cstdint>

namespace Fby
{

// What the bridge publishes to the operating system. The game stores this in
// the project accessibility profile; the editor stores its own copy in the
// editor settings. A disabled bridge leaves the narrator and the tree intact.
struct AccessibilityBridgeSettings
{
    [[nodiscard]] friend bool operator==(const AccessibilityBridgeSettings&,
                                         const AccessibilityBridgeSettings&) noexcept = default;

    bool Enabled = true;
    // A screen reader that navigates by focus needs the focus events to follow
    // the keyboard. Turning this off keeps the tree queryable but silent.
    bool RaiseFocusEvents = true;
    // Structure changes are cheap to raise but a rebuild every frame would
    // flood the client, so the tree only reports a real change.
    bool RaiseStructureEvents = true;
    bool RaiseStateEvents = true;
};

// Starts the platform bridge for the process window. `InNativeWindow` is the
// SDL_Window pointer the engine already owns; the bridge reads the native
// handle from it. Returns false when the platform has no bridge or the window
// is not ready.
[[nodiscard]] bool StartAccessibilityBridge(void* InNativeWindow,
                                            const AccessibilityBridgeSettings& InSettings) noexcept;
void StopAccessibilityBridge() noexcept;
// Publishes the current tree. The bridge drains the tree's event ring, so the
// call is a producer-to-consumer handoff and needs a mutable tree.
void UpdateAccessibilityBridge(Ui::AccessibilityTree& InTree) noexcept;
[[nodiscard]] bool AccessibilityBridgeRunning() noexcept;
// "Windows", "Linux", "macOS" or "none". Diagnostics only.
[[nodiscard]] const char* AccessibilityBridgePlatformName() noexcept;

// Platform entry points. Exactly one translation unit defines them: the
// Windows implementation, the stub for every other platform, or a test double.
[[nodiscard]] bool PlatformAccessibilityBridgeStart(void* InNativeWindow,
                                                    const AccessibilityBridgeSettings& InSettings) noexcept;
void PlatformAccessibilityBridgeStop() noexcept;
void PlatformAccessibilityBridgeUpdate(Ui::AccessibilityTree& InTree) noexcept;
[[nodiscard]] bool PlatformAccessibilityBridgeActive() noexcept;
[[nodiscard]] const char* PlatformAccessibilityBridgeName() noexcept;
}
