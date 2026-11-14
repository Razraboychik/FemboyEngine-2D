// FemboyEngine2D - accessibility bridge stub for platforms without an OS provider.
#include "Engine/UI/AccessibilityBridge.hpp"

namespace Fby
{

bool PlatformAccessibilityBridgeStart(void* InNativeWindow, const AccessibilityBridgeSettings& InSettings) noexcept
{
    (void)InNativeWindow;
    (void)InSettings;
    return false;
}

void PlatformAccessibilityBridgeStop() noexcept
{
}

void PlatformAccessibilityBridgeUpdate(Ui::AccessibilityTree& InTree) noexcept
{
    (void)InTree;
}

bool PlatformAccessibilityBridgeActive() noexcept
{
    return false;
}

const char* PlatformAccessibilityBridgeName() noexcept
{
    return "none";
}
}
