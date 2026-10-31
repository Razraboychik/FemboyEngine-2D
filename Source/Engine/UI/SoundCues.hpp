// FemboyEngine2D - sound cue registry: localization ids, categories and cooldowns.
#pragma once

#include <array>
#include <cstdint>

namespace Fby
{

inline constexpr std::uint32_t kMaxSoundCues = 256;
// Voice captions use their own cue namespace so a voice line never collides with
// an authored sound cue. Authored cues must stay outside [0x4000, 0x40FF].
inline constexpr std::uint32_t kVoiceCaptionCueFirst = 0x4000;
inline constexpr std::uint32_t kVoiceCaptionCueLast = 0x40FF;

[[nodiscard]] constexpr bool IsVoiceCaptionCueId(std::uint32_t InCueId) noexcept
{
    return InCueId >= kVoiceCaptionCueFirst && InCueId <= kVoiceCaptionCueLast;
}

[[nodiscard]] constexpr std::uint32_t VoiceCaptionCueId(std::uint8_t InPlayerId) noexcept
{
    return kVoiceCaptionCueFirst + static_cast<std::uint32_t>(InPlayerId);
}

enum class SoundCueCategory : std::uint8_t
{
    Ui = 0,
    World,
    Impact,
    Footstep,
    Voice,
    Ambient,
    Count,
};

[[nodiscard]] constexpr const char* SoundCueCategoryName(SoundCueCategory InCategory) noexcept
{
    switch (InCategory)
    {
        case SoundCueCategory::Ui:
            return "ui";
        case SoundCueCategory::World:
            return "world";
        case SoundCueCategory::Impact:
            return "impact";
        case SoundCueCategory::Footstep:
            return "footstep";
        case SoundCueCategory::Voice:
            return "voice";
        case SoundCueCategory::Ambient:
            return "ambient";
        case SoundCueCategory::Count:
            break;
    }
    return "?";
}

// One authored sound event. `LocId` names a localization string, so subtitles
// are translated; the category and cooldown keep DeafMode captions readable
// (footsteps and impacts must not flood the queue).
struct SoundCueDef
{
    std::uint32_t CueId = 0;
    std::uint32_t LocId = 0;
    SoundCueCategory Category = SoundCueCategory::World;
    std::uint8_t Priority = 0;
    std::uint16_t CooldownMs = 0;
};

class SoundCueRegistry
{
public:
    [[nodiscard]] bool Add(const SoundCueDef& InCue) noexcept;
    [[nodiscard]] const SoundCueDef* Find(std::uint32_t InCueId) const noexcept;
    // True when the cue is defined and its cooldown has elapsed. Every accepted
    // trigger arms the next cooldown window.
    [[nodiscard]] bool TryTrigger(std::uint32_t InCueId, std::uint64_t InNowMs) const noexcept;
    void ResetCooldowns() noexcept;

    [[nodiscard]] std::uint32_t Count() const noexcept
    {
        return MCount;
    }

protected:
    std::array<SoundCueDef, kMaxSoundCues> MDefs{};
    mutable std::array<std::uint64_t, kMaxSoundCues> MLastTriggerMs{};
    mutable std::array<bool, kMaxSoundCues> MArmed{};
    std::uint32_t MCount = 0;
};
}
