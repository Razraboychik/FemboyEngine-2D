// FemboyEngine2D - voice session implementation.
#include "Engine/Audio/VoiceSession.hpp"

#include <algorithm>

namespace Fby
{

bool VoiceSession::AddParticipant(std::uint8_t InPlayerId) noexcept
{
    if (InPlayerId >= kMaxVoiceParticipants || MParticipants[InPlayerId].Present)
    {
        return false;
    }
    MParticipants[InPlayerId] = VoiceParticipant{};
    MParticipants[InPlayerId].Present = true;
    ++MCount;
    return true;
}

void VoiceSession::RemoveParticipant(std::uint8_t InPlayerId) noexcept
{
    if (InPlayerId >= kMaxVoiceParticipants || !MParticipants[InPlayerId].Present)
    {
        return;
    }
    MParticipants[InPlayerId] = VoiceParticipant{};
    if (MCount > 0)
    {
        --MCount;
    }
}

void VoiceSession::SetParticipantVoice(std::uint8_t InPlayerId, std::uint32_t InVoiceSlot) noexcept
{
    if (InPlayerId < kMaxVoiceParticipants)
    {
        MParticipants[InPlayerId].VoiceSlot = InVoiceSlot;
    }
}

void VoiceSession::SetParticipantLevel(std::uint8_t InPlayerId, float InRms) noexcept
{
    if (InPlayerId < kMaxVoiceParticipants)
    {
        MParticipants[InPlayerId].Level = InRms < 0.0f ? 0.0f : InRms;
    }
}

void VoiceSession::SetParticipantMuted(std::uint8_t InPlayerId, bool InMuted) noexcept
{
    if (InPlayerId < kMaxVoiceParticipants)
    {
        MParticipants[InPlayerId].LocalMuted = InMuted;
    }
}

void VoiceSession::SetParticipantVolume(std::uint8_t InPlayerId, float InVolume) noexcept
{
    if (InPlayerId < kMaxVoiceParticipants)
    {
        MParticipants[InPlayerId].Volume = std::clamp(InVolume, 0.0f, 4.0f);
    }
}

void VoiceSession::SetParticipantPan(std::uint8_t InPlayerId, float InPan) noexcept
{
    if (InPlayerId < kMaxVoiceParticipants)
    {
        MParticipants[InPlayerId].Pan = std::clamp(InPan, -1.0f, 1.0f);
    }
}

void VoiceSession::SetLocalLevel(float InRms) noexcept
{
    MLocalLevel = InRms < 0.0f ? 0.0f : InRms;
}

void VoiceSession::SetLocalMuted(bool InMuted) noexcept
{
    MLocalMuted = InMuted;
}

void VoiceSession::SetDeafened(bool InDeafened) noexcept
{
    MDeafened = InDeafened;
}

void VoiceSession::SetPushToTalkEnabled(bool InEnabled) noexcept
{
    MPttEnabled = InEnabled;
    if (!InEnabled)
    {
        MPttHeld = false;
    }
}

void VoiceSession::SetPushToTalkHeld(bool InHeld) noexcept
{
    MPttHeld = InHeld;
}

void VoiceSession::SetMono(bool InMono) noexcept
{
    MMono = InMono;
}

bool VoiceSession::CanTransmit() const noexcept
{
    if (MDeafened || MLocalMuted)
    {
        return false;
    }
    return !MPttEnabled || MPttHeld;
}

const VoiceParticipant* VoiceSession::Participant(std::uint8_t InPlayerId) const noexcept
{
    if (InPlayerId >= kMaxVoiceParticipants || !MParticipants[InPlayerId].Present)
    {
        return nullptr;
    }
    return &MParticipants[InPlayerId];
}

float VoiceSession::ParticipantGain(std::uint8_t InPlayerId) const noexcept
{
    if (InPlayerId >= kMaxVoiceParticipants || !MParticipants[InPlayerId].Present)
    {
        return 0.0f;
    }
    const VoiceParticipant& ScopedParticipant = MParticipants[InPlayerId];
    if (MDeafened || ScopedParticipant.LocalMuted)
    {
        return 0.0f;
    }
    return ScopedParticipant.Volume;
}

bool VoiceSession::UpdateTalking(float InLevel, float* InHold, bool InWasTalking) noexcept
{
    if (InLevel >= kVoiceTalkingThreshold)
    {
        *InHold = kVoiceTalkingHoldSeconds;
        return true;
    }
    return InWasTalking && *InHold > 0.0f;
}

void VoiceSession::Update(float InDeltaSeconds) noexcept
{
    const float ScopedDelta = InDeltaSeconds > 0.0f ? InDeltaSeconds : 0.0f;
    MLocalTalking = UpdateTalking(MLocalLevel, &MLocalHold, MLocalTalking);
    MLocalHold = std::max(0.0f, MLocalHold - ScopedDelta);
    if (MLocalTalking && MLocalHold <= 0.0f)
    {
        MLocalTalking = false;
    }

    for (VoiceParticipant& ScopedParticipant : MParticipants)
    {
        if (!ScopedParticipant.Present)
        {
            continue;
        }
        ScopedParticipant.Talking =
            UpdateTalking(ScopedParticipant.Level, &ScopedParticipant.Hold, ScopedParticipant.Talking);
        ScopedParticipant.Hold = std::max(0.0f, ScopedParticipant.Hold - ScopedDelta);
        if (ScopedParticipant.Talking && ScopedParticipant.Hold <= 0.0f)
        {
            ScopedParticipant.Talking = false;
        }

        if (MGraph != nullptr && ScopedParticipant.VoiceSlot != kInvalidVoice)
        {
            const float ScopedGain = ParticipantGain(static_cast<std::uint8_t>(&ScopedParticipant - MParticipants.data()));
            const float ScopedPan = MMono ? 0.0f : ScopedParticipant.Pan;
            (void)MGraph->SetVoiceSpatial(ScopedParticipant.VoiceSlot, ScopedGain, ScopedPan);
        }
    }
}
}
