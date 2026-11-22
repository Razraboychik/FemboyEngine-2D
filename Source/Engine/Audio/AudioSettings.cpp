// FemboyEngine2D - developer-authored audio bus settings.
#include "Engine/Audio/AudioSettings.hpp"

#include <string>
#include <unordered_map>

namespace Fby::Audio
{

bool CreateAudioBuses(AudioSystem& InAudio, std::span<const AudioBusDefinition> InBuses, std::span<BusId> OutBuses)
{
    if (OutBuses.size() < InBuses.size())
    {
        return false;
    }
    std::unordered_map<std::string, BusId> ScopedIds;
    ScopedIds.emplace("master", kMasterBus);
    for (std::size_t ScopedIndex = 0; ScopedIndex < InBuses.size(); ++ScopedIndex)
    {
        const AudioBusDefinition& ScopedBus = InBuses[ScopedIndex];
        BusId ScopedParent = kMasterBus;
        if (!ScopedBus.Parent.empty())
        {
            const auto ScopedFound = ScopedIds.find(std::string(ScopedBus.Parent));
            if (ScopedFound == ScopedIds.end())
            {
                return false;
            }
            ScopedParent = ScopedFound->second;
        }
        BusId ScopedCreated = kInvalidBus;
        if (!InAudio.CreateBus(ScopedParent, 1.0f, &ScopedCreated))
        {
            return false;
        }
        ScopedIds.emplace(std::string(ScopedBus.Id), ScopedCreated);
        OutBuses[ScopedIndex] = ScopedCreated;
    }
    return true;
}

void RegisterAudioBusSettings(Settings::Registry& InRegistry, std::span<const AudioBusDefinition> InBuses,
                              std::span<const BusId> InBusIds, AudioSystem* InAudio)
{
    const std::size_t ScopedCount = InBuses.size() < InBusIds.size() ? InBuses.size() : InBusIds.size();
    for (std::size_t ScopedIndex = 0; ScopedIndex < ScopedCount; ++ScopedIndex)
    {
        const AudioBusDefinition& ScopedBus = InBuses[ScopedIndex];
        const BusId ScopedBusId = InBusIds[ScopedIndex];
        InRegistry.Float("audio.bus." + std::string(ScopedBus.Id))
            .Category("Audio")
            .Label(std::string(ScopedBus.Label))
            .Unit("dB")
            .Default(Settings::Value::MakeFloat(ScopedBus.DefaultDb))
            .Range(-60.0, 6.0)
            .Scope(Settings::Scope::User)
            .Apply(
                [InAudio, ScopedBusId](const Settings::Value& InValue)
                {
                    if (InAudio != nullptr)
                    {
                        (void)InAudio->SetBusVolumeDb(ScopedBusId,
                                                      static_cast<float>(InValue.FloatValue));
                    }
                });
    }
}

}