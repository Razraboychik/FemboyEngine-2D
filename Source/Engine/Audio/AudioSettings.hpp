// FemboyEngine2D - developer-authored audio bus settings.
#pragma once

#include "Engine/Audio/Audio.hpp"
#include "Engine/Core/SettingsRegistry.hpp"

#include <span>
#include <string_view>

namespace Fby::Audio
{

struct AudioBusSetting
{
    std::string_view Id;
    std::string_view Label;
    BusId Bus = kSfxBus;
    float DefaultDb = 0.0f;
};

// Registers one user-facing dB setting per developer-declared bus. The caller
// owns the bus table and maps each id to its DSP graph bus; the engine does not
// invent master/SFX/music categories.
void RegisterAudioBusSettings(Settings::Registry& InRegistry, std::span<const AudioBusSetting> InBuses,
                              AudioSystem* InAudio);

}