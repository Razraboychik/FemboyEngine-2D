// FemboyEngine2D - developer-authored audio bus definitions and settings.
#pragma once

#include "Engine/Audio/Audio.hpp"
#include "Engine/Core/SettingsRegistry.hpp"

#include <span>
#include <string_view>

namespace Fby::Audio
{

struct AudioBusDefinition
{
    std::string_view Id;
    std::string_view Label;
    std::string_view Parent;
    float DefaultDb = 0.0f;
};

// Creates the declared buses under their parents. Parents must appear before
// their children; an empty parent means Master. OutBuses receives one id per
// definition so the caller can map gameplay channels to the DSP graph.
[[nodiscard]] bool CreateAudioBuses(AudioSystem& InAudio, std::span<const AudioBusDefinition> InBuses,
                                    std::span<BusId> OutBuses);

// Registers one user-facing dB setting per declared bus. The caller owns the
// definitions; the engine does not invent master/SFX/music categories.
void RegisterAudioBusSettings(Settings::Registry& InRegistry, std::span<const AudioBusDefinition> InBuses,
                              std::span<const BusId> InBusIds, AudioSystem* InAudio);

}