// FemboyEngine2D - sound cue registry implementation.
#include "Engine/UI/SoundCues.hpp"

namespace Fby
{

bool SoundCueRegistry::Add(const SoundCueDef& InCue) noexcept
{
    // Authored cues must not enter the voice-caption namespace (0x4000..0x40FF).
    if (InCue.CueId == 0 || InCue.LocId == 0 || IsVoiceCaptionCueId(InCue.CueId) || Find(InCue.CueId) != nullptr ||
        MCount >= kMaxSoundCues)
    {
        return false;
    }
    MDefs[MCount] = InCue;
    MLastTriggerMs[MCount] = 0;
    MArmed[MCount] = false;
    ++MCount;
    return true;
}

const SoundCueDef* SoundCueRegistry::Find(std::uint32_t InCueId) const noexcept
{
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < MCount; ++ScopedIndex)
    {
        if (MDefs[ScopedIndex].CueId == InCueId)
        {
            return &MDefs[ScopedIndex];
        }
    }
    return nullptr;
}

bool SoundCueRegistry::TryTrigger(std::uint32_t InCueId, std::uint64_t InNowMs) const noexcept
{
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < MCount; ++ScopedIndex)
    {
        if (MDefs[ScopedIndex].CueId != InCueId)
        {
            continue;
        }
        if (MArmed[ScopedIndex] && InNowMs < MLastTriggerMs[ScopedIndex] + MDefs[ScopedIndex].CooldownMs)
        {
            return false;
        }
        MLastTriggerMs[ScopedIndex] = InNowMs;
        MArmed[ScopedIndex] = true;
        return true;
    }
    return false;
}

void SoundCueRegistry::ResetCooldowns() noexcept
{
    MArmed.fill(false);
    MLastTriggerMs.fill(0);
}
}
