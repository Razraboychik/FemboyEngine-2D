// FemboyEngine2D - developer-facing render and audio policy settings.
#pragma once

#include "Engine/Core/OutputPolicy.hpp"
#include "Engine/Core/SettingsRegistry.hpp"

namespace Fby::Settings
{

// Registers the two channel policies as dangerous enum settings. The safe value
// is Full for both; a game may leave the settings hidden or expose them through
// its accessibility profile.
void RegisterOutputPolicySettings(Registry& InRegistry);

}