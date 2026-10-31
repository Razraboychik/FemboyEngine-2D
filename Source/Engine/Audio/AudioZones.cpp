// FemboyEngine2D - audio zone resolution and context interpolation.
#include "Engine/Audio/AudioZones.hpp"

#include <algorithm>
#include <cmath>

namespace Fby
{
namespace
{
[[nodiscard]] float Saturate(float InValue) noexcept
{
    return InValue < 0.0f ? 0.0f : (InValue > 1.0f ? 1.0f : InValue);
}

[[nodiscard]] float ClampLowPass(float InHz) noexcept
{
    return InHz < 20.0f ? 20.0f : (InHz > 20000.0f ? 20000.0f : InHz);
}
}

void AudioZoneSystem::Update(Ecs::Registry& InRegistry, AudioSystem& InAudio, float InDeltaSeconds) noexcept
{
    Update(InRegistry, &InAudio, InDeltaSeconds);
}

void AudioZoneSystem::Update(Ecs::Registry& InRegistry, AudioSystem* InAudio, float InDeltaSeconds) noexcept
{
    MStats = Stats{};
    const float ScopedDelta = InDeltaSeconds > 0.0f ? InDeltaSeconds : 0.0f;

    // One listener baseline. A context entity that owns its own AudioListener
    Vector2D ScopedGlobalListener{};
    bool ScopedHasGlobalListener = false;
    auto ScopedListenerView = InRegistry.view<AudioListener>();
    for (auto [ScopedEntity, ScopedListener] : ScopedListenerView.each())
    {
        (void)ScopedEntity;
        if (ScopedListener.active)
        {
            ScopedGlobalListener = ScopedListener.position;
            ScopedHasGlobalListener = true;
            break;
        }
    }

    auto ScopedContextView = InRegistry.view<AudioContextComponent>();
    for (auto [ScopedEntity, ScopedContext] : ScopedContextView.each())
    {
        ++MStats.contexts;
        Vector2D ScopedListenerPosition = ScopedContext.ListenerPosition;
        if (const auto* ScopedOwnListener = InRegistry.try_get<AudioListener>(ScopedEntity))
        {
            if (ScopedOwnListener->active)
            {
                ScopedListenerPosition = ScopedOwnListener->position;
            }
        }
        else if (ScopedHasGlobalListener)
        {
            ScopedListenerPosition = ScopedGlobalListener;
        }
        ScopedContext.ListenerPosition = ScopedListenerPosition;

        const ResolvedZone ScopedResolved = FindDominant(InRegistry, ScopedListenerPosition);
        const AudioZoneComponent* ScopedZone = ScopedResolved.zone;
        const std::uint32_t ScopedNewZone = ScopedZone != nullptr ? ScopedZone->ZoneID : 0u;
        const bool ScopedChanged = ScopedNewZone != ScopedContext.ActiveZoneID;
        if (ScopedChanged)
        {
            ScopedContext.PreviousZoneID = ScopedContext.ActiveZoneID;
            ScopedContext.ActiveZoneID = ScopedNewZone;
            ScopedContext.Blend = 0.0f;
            ++MStats.zoneChanges;
        }
        if (ScopedZone != nullptr)
        {
            ++MStats.activeZones;
            ScopedContext.ActiveKind = ScopedZone->Kind;
            ScopedContext.TargetReverbGain = ScopedZone->ReverbGain;
            ScopedContext.TargetLowPassHz = ClampLowPass(ScopedZone->LowPassHz);
            ScopedContext.FadeSeconds = ScopedZone->FadeSeconds;
        }
        else
        {
            ScopedContext.ActiveKind = static_cast<std::uint32_t>(AudioZoneKind::None);
            ScopedContext.TargetReverbGain = 1.0f;
            ScopedContext.TargetLowPassHz = 20000.0f;
            ScopedContext.FadeSeconds = 0.35f;
        }

        const float ScopedFade = ScopedContext.FadeSeconds > 0.001f ? ScopedContext.FadeSeconds : 0.001f;
        const float ScopedAlpha = ScopedDelta > 0.0f ? 1.0f - std::exp(-ScopedDelta / ScopedFade) : 0.0f;
        ScopedContext.ReverbGain += (ScopedContext.TargetReverbGain - ScopedContext.ReverbGain) * ScopedAlpha;
        ScopedContext.LowPassHz += (ScopedContext.TargetLowPassHz - ScopedContext.LowPassHz) * ScopedAlpha;
        ScopedContext.ReverbGain = ScopedContext.ReverbGain < 0.0f ? 0.0f : ScopedContext.ReverbGain;
        ScopedContext.LowPassHz = ClampLowPass(ScopedContext.LowPassHz);
        ScopedContext.Blend = Saturate(ScopedContext.Blend + ScopedDelta / ScopedFade);

        if (InAudio == nullptr || !InAudio->IsValid())
        {
            continue;
        }
        const std::uint32_t ScopedMask =
            ScopedZone != nullptr ? (ScopedZone->BusMask & kAudioZoneBusAll) : kAudioZoneBusAll;
        const std::uint32_t ScopedSendMask = ScopedChanged ? (ScopedMask | ScopedContext.LastBusMask) : ScopedMask;
        const bool ScopedNeedsSend = ScopedChanged ||
                                     std::fabs(ScopedContext.ReverbGain - ScopedContext.LastSentReverbGain) > 0.005f ||
                                     std::fabs(ScopedContext.LowPassHz - ScopedContext.LastSentLowPassHz) > 5.0f;
        if (!ScopedNeedsSend)
        {
            continue;
        }
        bool ScopedAllQueued = true;
        for (std::uint32_t ScopedBus = 0; ScopedBus < 3; ++ScopedBus)
        {
            if ((ScopedSendMask & (1u << ScopedBus)) == 0)
            {
                continue;
            }
            if (InAudio->SetBusZoneParameters(static_cast<AudioBus>(ScopedBus), ScopedContext.ReverbGain,
                                              ScopedContext.LowPassHz, ScopedFade))
            {
                ++MStats.busCommands;
            }
            else
            {
                ScopedAllQueued = false;
            }
        }
        if (!ScopedAllQueued)
        {
            // Retry the whole context next frame instead of remembering a
            continue;
        }
        ScopedContext.LastBusMask = ScopedMask;
        ScopedContext.LastSentReverbGain = ScopedContext.ReverbGain;
        ScopedContext.LastSentLowPassHz = ScopedContext.LowPassHz;
    }
}

AudioZoneSystem::ResolvedZone AudioZoneSystem::FindDominant(Ecs::Registry& InRegistry, Vector2D InPoint) noexcept
{
    ResolvedZone ScopedResult;
    std::uint32_t ScopedIndex = 0;
    auto ScopedView = InRegistry.view<AudioZoneComponent>();
    for (auto [ScopedEntity, ScopedZone] : ScopedView.each())
    {
        (void)ScopedEntity;
        const std::uint32_t ScopedCurrent = ScopedIndex++;
        if (!ScopedZone.Enabled() || !ScopedZone.Contains(InPoint))
        {
            continue;
        }
        if (ScopedResult.zone == nullptr || ScopedZone.Priority > ScopedResult.zone->Priority ||
            (ScopedZone.Priority == ScopedResult.zone->Priority && ScopedZone.ZoneID < ScopedResult.zone->ZoneID))
        {
            ScopedResult.zone = &ScopedZone;
            ScopedResult.index = ScopedCurrent;
        }
    }
    return ScopedResult;
}
}
