// FemboyEngine2D - small editor widgets shared by the viewport and the toolbar.
#pragma once

#include <imgui.h>

#include "Editor/Infrastructure/EditorTheme.hpp"

namespace Fby::Editor
{

// Compact square buttons for the playback bar. The glyph is drawn white on a dark
// square; the engaged state (the one the runtime is currently in) is drawn slightly
// transparent so it reads as pressed, while the other buttons stay fully opaque.
enum class PlaybackIcon
{
    Play,
    Pause,
    Step,
    Stop,
    Multiplayer,
};

// Returns true on the frame the button is released over it. InEngaged dims the
// button, InDisabled draws it dimmer still and swallows clicks.
bool PlaybackIconButton(const char* InId, PlaybackIcon InIcon, bool InEngaged, bool InDisabled, const char* InTooltip);

// A round, theme-coloured text button. The label is also the accessibility
// name, so a screen reader sees the same action as the drawn glyph.
bool CircleTextButton(const char* InId, const char* InLabel, EditorSymbolRole InRole, const char* InTooltip);

// Slim slider with a gray grab. The grab either fits inside the track or is taller
// than it and spills over both sides, depending on InOverflow. InLogarithmic maps
// the value logarithmically, which is what the viewport zoom wants. InFormat is a
// printf format with one float argument. Returns true when the value changed.
bool EditorSlider(const char* InId, float* InValue, float InMin, float InMax, const char* InFormat, float InWidth,
                  bool InOverflow, bool InLogarithmic = false, float InTrackHeight = 3.0f, bool InShowValue = true);

}
