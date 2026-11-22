// FemboyEngine2D - developer-authored audio bus settings.
#include "Engine/Audio/AudioSettings.hpp"

#include <string>

namespace Fby::Audio
{

void RegisterAudioBusSettings(Settings::Registry& InRegistry, std::span<const AudioBusSetting> InBuses,
                              AudioSystem* InAudio)
{
    for (const AudioBusSetting& ScopedBus : InBuses)
    {
        const std::string ScopedId = "audio.bus." + std::string(ScopedBus.Id);
        InRegistry.Float(ScopedId)
            .Category("Audio")
            .Label(std::string(ScopedBus.Label))
            .Unit("dB")
            .Default(Settings::Value::MakeFloat(ScopedBus.DefaultDb))
            .Range(-60.0, 6.0)
            .Scope(Settings::Scope::User)
            .Apply(
                [InAudio, ScopedBus](const Settings::Value& InValue)
                {
                    if (InAudio != nullptr)
                    {
                        (void)InAudio->SetBusVolumeDb(ScopedBus.Bus,
                                                      static_cast<float>(InValue.FloatValue));
                    }
                });
    }
}

}