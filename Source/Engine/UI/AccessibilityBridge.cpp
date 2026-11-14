// FemboyEngine2D - accessibility bridge dispatcher.
#include "Engine/UI/AccessibilityBridge.hpp"

#include "Engine/Core/Log.hpp"

namespace Fby
{

bool StartAccessibilityBridge(void* InNativeWindow, const AccessibilityBridgeSettings& InSettings) noexcept
{
    if (!InSettings.Enabled || InNativeWindow == nullptr)
    {
        return false;
    }
    const bool ScopedStarted = PlatformAccessibilityBridgeStart(InNativeWindow, InSettings);
    if (ScopedStarted)
    {
        LogInfo("a11y", "accessibility bridge is live on {}", AccessibilityBridgePlatformName());
    }
    else
    {
        LogInfo("a11y", "accessibility bridge is unavailable on {}", AccessibilityBridgePlatformName());
    }
    return ScopedStarted;
}

void StopAccessibilityBridge() noexcept
{
    PlatformAccessibilityBridgeStop();
}

void UpdateAccessibilityBridge(Ui::AccessibilityTree& InTree) noexcept
{
    if (!PlatformAccessibilityBridgeActive())
    {
        return;
    }
    PlatformAccessibilityBridgeUpdate(InTree);
}

bool AccessibilityBridgeRunning() noexcept
{
    return PlatformAccessibilityBridgeActive();
}

const char* AccessibilityBridgePlatformName() noexcept
{
    return PlatformAccessibilityBridgeName();
}
}
