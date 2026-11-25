// FemboyEngine2D - registry view of the existing accessibility profile.
#pragma once

#include "Engine/Core/SettingsRegistry.hpp"
#include "Engine/UI/Accessibility.hpp"

namespace Fby::Editor
{

// Registers the accessibility fields that are already serialized in
// project.femproj. The profile remains the authoritative storage; the registry
// provides the generated panel, scopes, validation and safe-mode metadata.
void RegisterAccessibilitySettings(Settings::Registry& InRegistry, AccessibilityProfile& InProfile);

}