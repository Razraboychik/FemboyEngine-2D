// FemboyEngine2D - reactive voice SFX event layer and playback helper.
#include "Engine/Audio/VoiceReactiveSfx.hpp"

#include "Engine/Audio/Audio.hpp"

#include <algorithm>

namespace Fby
{
const char* VoiceSfxKindName(VoiceSfxKind InKind) noexcept
{
    switch (InKind)
    {
        case VoiceSfxKind::None:
            return "none";
        case VoiceSfxKind::BubbleSmall:
            return "bubble-small";
        case VoiceSfxKind::BubbleBig:
            return "bubble-big";
        case VoiceSfxKind::BubbleCluster:
            return "bubble-cluster";
        case VoiceSfxKind::Splash:
            return "splash";
        case VoiceSfxKind::ExplosionMuffled:
            return "explosion-muffled";
        case VoiceSfxKind::RadioClick:
            return "radio-click";
        case VoiceSfxKind::Count:
            break;
    }
    return "none";
}

void VoiceReactiveSfxSettings::Clamp() noexcept
{
    MasterGain = std::clamp(MasterGain, 0.0f, 1.0f);
    BubbleGain = std::clamp(BubbleGain, 0.0f, 1.0f);
    SplashGain = std::clamp(SplashGain, 0.0f, 1.0f);
    ModerationGain = std::clamp(ModerationGain, 0.0f, 1.0f);
    SpeakerCooldownSeconds = std::clamp(SpeakerCooldownSeconds, 0.1f, 10.0f);
    GlobalCooldownSeconds = std::clamp(GlobalCooldownSeconds, 0.0f, 5.0f);
}

void VoiceReactiveSfx::Configure(const VoiceReactiveSfxSettings& InSettings) noexcept
{
    MSettings = InSettings;
    MSettings.Clamp();
    Reset();
}

void VoiceReactiveSfx::Reset() noexcept
{
    MStates = {};
    MGlobalCooldown = 0.0f;
}

std::uint32_t VoiceReactiveSfx::Update(float InDeltaSeconds, const VoiceReactiveSfxInput& InInput,
                                       std::span<VoiceSfxEvent> Out) noexcept
{
    const float ScopedDelta = std::clamp(InDeltaSeconds, 0.0f, 0.25f);
    MGlobalCooldown = std::max(0.0f, MGlobalCooldown - ScopedDelta);
    for (PlayerState& ScopedState : MStates)
    {
        ScopedState.BubbleCooldown = std::max(0.0f, ScopedState.BubbleCooldown - ScopedDelta);
    }
    if (!MSettings.Enabled || Out.empty())
    {
        return 0;
    }

    std::uint32_t ScopedCount = 0;
    const auto ScopedEmit = [&](VoiceSfxKind InKind, std::uint8_t InPlayer, const VoiceReactiveSfxPlayer& InSource,
                                float InBaseGain, bool InGlobalLimit)
    {
        if (ScopedCount >= Out.size() || (InGlobalLimit && MGlobalCooldown > 0.0f))
        {
            return;
        }
        VoiceSfxEvent& ScopedEvent = Out[ScopedCount++];
        ScopedEvent.Kind = InKind;
        ScopedEvent.Player = InPlayer;
        ScopedEvent.Gain = std::clamp(MSettings.MasterGain * InBaseGain, 0.0f, 1.0f);
        ScopedEvent.Pan = std::clamp(InSource.Pan, -1.0f, 1.0f);
        ScopedEvent.Position = InSource.Position;
        if (InGlobalLimit)
        {
            MGlobalCooldown = MSettings.GlobalCooldownSeconds;
        }
    };

    for (std::uint8_t ScopedPlayer = 0; ScopedPlayer < static_cast<std::uint8_t>(kVoiceMaxPeers); ++ScopedPlayer)
    {
        const VoiceReactiveSfxPlayer& ScopedSource = InInput.Players[ScopedPlayer];
        PlayerState& ScopedState = MStates[ScopedPlayer];
        if (!ScopedSource.Present)
        {
            ScopedState = PlayerState{};
            continue;
        }
        if (MSettings.Splashes && ScopedSource.Submerged != ScopedState.WasSubmerged)
        {
            ScopedEmit(ScopedSource.Submerged ? VoiceSfxKind::Splash : VoiceSfxKind::BubbleCluster, ScopedPlayer,
                       ScopedSource, MSettings.SplashGain, true);
        }
        if (MSettings.ModerationFeedback && ScopedSource.LocallyMuted != ScopedState.WasMuted)
        {
            ScopedEmit(ScopedSource.LocallyMuted ? VoiceSfxKind::ExplosionMuffled : VoiceSfxKind::RadioClick,
                       ScopedPlayer, ScopedSource, MSettings.ModerationGain, true);
        }
        if (MSettings.Bubbles && ScopedSource.Submerged && ScopedSource.Talking && !ScopedState.WasTalking &&
            ScopedState.BubbleCooldown <= 0.0f)
        {
            ScopedEmit(VoiceSfxKind::BubbleCluster, ScopedPlayer, ScopedSource, MSettings.BubbleGain, false);
            ScopedState.BubbleCooldown = MSettings.SpeakerCooldownSeconds;
        }
        ScopedState.WasTalking = ScopedSource.Talking;
        ScopedState.WasSubmerged = ScopedSource.Submerged;
        ScopedState.WasMuted = ScopedSource.LocallyMuted;
    }
    return ScopedCount;
}

bool PlayVoiceSfx(AudioSystem& InAudio, AssetId InClip, const VoiceSfxEvent& InEvent) noexcept
{
    if (!InClip.IsValid() || InEvent.Gain <= 0.0f)
    {
        return false;
    }
    const float ScopedGain = std::clamp(InEvent.Gain, 0.0f, 1.0f);
    const std::uint32_t ScopedVoice = InAudio.Play(InClip, AudioBus::Sfx, ScopedGain);
    if (ScopedVoice == kInvalidAudioVoice)
    {
        return false;
    }
    (void)InAudio.SetVoiceSpatial(ScopedVoice, ScopedGain, std::clamp(InEvent.Pan, -1.0f, 1.0f));
    return true;
}
}
