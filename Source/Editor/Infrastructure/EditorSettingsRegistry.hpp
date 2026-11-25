// FemboyEngine2D - registry view of editor-only settings.
#pragma once

#include "Editor/Infrastructure/EditorSettings.hpp"
#include "Engine/Core/SettingsRegistry.hpp"

namespace Fby::Editor
{

// Registers the editor-facing subset that is safe to expose through the generated
// panel. EditorSettings remains the serialized authority; apply callbacks write
// back into the same struct.
void RegisterEditorSettings(Settings::Registry& InRegistry, EditorSettings& InSettings);

}