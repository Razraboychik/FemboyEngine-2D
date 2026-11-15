// FemboyEngine2D - voice preset registry, policy resolution and applier.
#include "Engine/Audio/VoicePreset.hpp"

#include "Engine/Audio/VoiceSession.hpp"
#include "Engine/Audio/VoiceSystem.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace Fby
{
namespace
{
[[nodiscard]] bool IsIdCharacter(char InCharacter) noexcept
{
    return (InCharacter >= 'A' && InCharacter <= 'Z') || (InCharacter >= 'a' && InCharacter <= 'z') ||
           (InCharacter >= '0' && InCharacter <= '9') || InCharacter == '.' || InCharacter == '-';
}

[[nodiscard]] bool IsValidText(const char* InText, std::size_t InCapacity) noexcept
{
    if (InText == nullptr || InText[0] == '\0')
    {
        return false;
    }
    const std::size_t ScopedLength = std::strlen(InText);
    return ScopedLength < InCapacity;
}

[[nodiscard]] bool IsValidId(const char* InId) noexcept
{
    if (!IsValidText(InId, kVoicePresetIdBytes))
    {
        return false;
    }
    for (const char* ScopedCharacter = InId; *ScopedCharacter != '\0'; ++ScopedCharacter)
    {
        if (!IsIdCharacter(*ScopedCharacter))
        {
            return false;
        }
    }
    return true;
}

void CopyText(char* OutText, std::size_t InCapacity, std::string_view InText) noexcept
{
    if (OutText == nullptr || InCapacity == 0)
    {
        return;
    }
    const std::size_t ScopedLength = std::min(InText.size(), InCapacity - 1);
    if (ScopedLength > 0)
    {
        std::memcpy(OutText, InText.data(), ScopedLength);
    }
    OutText[ScopedLength] = '\0';
}

[[nodiscard]] bool SameId(const char* InLeft, const char* InRight) noexcept
{
    return std::strncmp(InLeft, InRight, kVoicePresetIdBytes) == 0;
}

[[nodiscard]] VoiceEffectSlot MakeSlot(VoiceEffectKind InKind, float InMix) noexcept
{
    VoiceEffectSlot ScopedSlot{};
    ScopedSlot.Kind = InKind;
    ScopedSlot.Mix = InMix;
    return ScopedSlot;
}

[[nodiscard]] VoiceEffectSlot MuteSlot(float InFadeSeconds, float InMix = 1.0f) noexcept
{
    VoiceEffectSlot ScopedSlot = MakeSlot(VoiceEffectKind::Mute, InMix);
    ScopedSlot.Mute.FadeSeconds = InFadeSeconds;
    return ScopedSlot;
}

[[nodiscard]] VoiceEffectSlot MuffleSlot(float InLowCutHz, float InHighCutHz, float InResonance, float InDriveDb,
                                         float InWobbleHz, float InWobbleDepth, float InMix = 1.0f,
                                         float InBreathLevel = 0.06f) noexcept
{
    VoiceEffectSlot ScopedSlot = MakeSlot(VoiceEffectKind::Muffle, InMix);
    ScopedSlot.Muffle.LowCutHz = InLowCutHz;
    ScopedSlot.Muffle.HighCutHz = InHighCutHz;
    ScopedSlot.Muffle.Resonance = InResonance;
    ScopedSlot.Muffle.DriveDb = InDriveDb;
    ScopedSlot.Muffle.WobbleHz = InWobbleHz;
    ScopedSlot.Muffle.WobbleDepth = InWobbleDepth;
    ScopedSlot.Muffle.BreathLevel = InBreathLevel;
    return ScopedSlot;
}

[[nodiscard]] VoiceEffectSlot UnderwaterSlot(float InLowCutHz, float InHighCutHz, float InResonance, float InWowHz,
                                             float InWowDepth, float InMix = 1.0f) noexcept
{
    VoiceEffectSlot ScopedSlot = MakeSlot(VoiceEffectKind::Underwater, InMix);
    ScopedSlot.Underwater.LowCutHz = InLowCutHz;
    ScopedSlot.Underwater.HighCutHz = InHighCutHz;
    ScopedSlot.Underwater.Resonance = InResonance;
    ScopedSlot.Underwater.WowHz = InWowHz;
    ScopedSlot.Underwater.WowDepth = InWowDepth;
    return ScopedSlot;
}

[[nodiscard]] VoiceEffectSlot RadioSlot(float InLowCutHz, float InHighCutHz, float InStaticLevel, float InCrackleRate,
                                        float InSquelch, float InDriveDb, bool InMuteWhenSquelched,
                                        float InMix = 1.0f) noexcept
{
    VoiceEffectSlot ScopedSlot = MakeSlot(VoiceEffectKind::Radio, InMix);
    ScopedSlot.Radio.LowCutHz = InLowCutHz;
    ScopedSlot.Radio.HighCutHz = InHighCutHz;
    ScopedSlot.Radio.StaticLevel = InStaticLevel;
    ScopedSlot.Radio.CrackleRate = InCrackleRate;
    ScopedSlot.Radio.Squelch = InSquelch;
    ScopedSlot.Radio.DriveDb = InDriveDb;
    ScopedSlot.Radio.MuteWhenSquelched = InMuteWhenSquelched;
    return ScopedSlot;
}

[[nodiscard]] VoiceEffectSlot PitchSlot(float InSemitones, float InWindowMs, float InMix = 1.0f) noexcept
{
    VoiceEffectSlot ScopedSlot = MakeSlot(VoiceEffectKind::Pitch, InMix);
    ScopedSlot.Pitch.Semitones = InSemitones;
    ScopedSlot.Pitch.WindowMs = InWindowMs;
    return ScopedSlot;
}

[[nodiscard]] VoiceEffectSlot FormantSlot(float InSemitones, float InStrengthDb, float InMix = 1.0f) noexcept
{
    VoiceEffectSlot ScopedSlot = MakeSlot(VoiceEffectKind::Formant, InMix);
    ScopedSlot.Formant.Semitones = InSemitones;
    ScopedSlot.Formant.StrengthDb = InStrengthDb;
    return ScopedSlot;
}

[[nodiscard]] VoiceEffectSlot DistortionSlot(float InDriveDb, float InBitDepth, float InMix = 1.0f) noexcept
{
    VoiceEffectSlot ScopedSlot = MakeSlot(VoiceEffectKind::Distortion, InMix);
    ScopedSlot.Distortion.DriveDb = InDriveDb;
    ScopedSlot.Distortion.BitDepth = InBitDepth;
    return ScopedSlot;
}

[[nodiscard]] VoiceEffectSlot RingSlot(float InFrequencyHz, float InDepth, float InMix = 1.0f) noexcept
{
    VoiceEffectSlot ScopedSlot = MakeSlot(VoiceEffectKind::RingModulation, InMix);
    ScopedSlot.RingModulation.FrequencyHz = InFrequencyHz;
    ScopedSlot.RingModulation.Depth = InDepth;
    return ScopedSlot;
}

[[nodiscard]] VoiceEffectSlot WhisperSlot(float InNoiseLevel, float InLowCutHz, float InHighCutHz,
                                          float InMix = 1.0f) noexcept
{
    VoiceEffectSlot ScopedSlot = MakeSlot(VoiceEffectKind::Whisper, InMix);
    ScopedSlot.Whisper.NoiseLevel = InNoiseLevel;
    ScopedSlot.Whisper.LowCutHz = InLowCutHz;
    ScopedSlot.Whisper.HighCutHz = InHighCutHz;
    return ScopedSlot;
}

[[nodiscard]] VoicePresetResolution ResolveChoice(const VoicePresetRegistry& InPresets,
                                                  const VoicePresetChoice& InChoice, bool InModeration) noexcept
{
    VoicePresetResolution ScopedResolution{};
    if (!InChoice.Enabled)
    {
        return ScopedResolution;
    }
    ScopedResolution.Preset = InPresets.Find(InChoice.Id);
    if (ScopedResolution.Preset == nullptr)
    {
        return ScopedResolution;
    }
    ScopedResolution.Intensity =
        std::clamp(InChoice.Intensity, ScopedResolution.Preset->MinIntensity, ScopedResolution.Preset->MaxIntensity);
    ScopedResolution.Moderation =
        InModeration || (ScopedResolution.Preset->Flags & VoicePresetFlagModeration) != VoicePresetFlagNone;
    return ScopedResolution;
}

[[nodiscard]] bool SameApplied(const char* InId, float InIntensity, bool InModeration, const char* InOtherId,
                               float InOtherIntensity, bool InOtherModeration, bool InOtherValid) noexcept
{
    return InOtherValid && SameId(InId, InOtherId) && InIntensity == InOtherIntensity &&
           InModeration == InOtherModeration;
}

void StoreApplied(char (&OutId)[kVoicePresetIdBytes], float& OutIntensity, bool& OutModeration, bool& OutValid,
                  const VoicePresetResolution& InResolution) noexcept
{
    CopyText(OutId, kVoicePresetIdBytes,
             InResolution.Preset != nullptr ? std::string_view(InResolution.Preset->Id) : std::string_view{});
    OutIntensity = InResolution.Intensity;
    OutModeration = InResolution.Moderation;
    OutValid = true;
}
}

const char* VoicePresetCategoryName(VoicePresetCategory InCategory) noexcept
{
    switch (InCategory)
    {
        case VoicePresetCategory::None:
            return "none";
        case VoicePresetCategory::Environment:
            return "environment";
        case VoicePresetCategory::Moderation:
            return "moderation";
        case VoicePresetCategory::Cosmetic:
            return "cosmetic";
        case VoicePresetCategory::Character:
            return "character";
        case VoicePresetCategory::Scene:
            return "scene";
        case VoicePresetCategory::Count:
            break;
    }
    return "none";
}

VoicePresetCategory VoicePresetCategoryFromName(std::string_view InName) noexcept
{
    for (std::uint8_t ScopedIndex = 0; ScopedIndex < static_cast<std::uint8_t>(VoicePresetCategory::Count);
         ++ScopedIndex)
    {
        const auto ScopedCategory = static_cast<VoicePresetCategory>(ScopedIndex);
        if (InName == VoicePresetCategoryName(ScopedCategory))
        {
            return ScopedCategory;
        }
    }
    return VoicePresetCategory::None;
}

bool VoicePresetRegistry::Add(const VoicePresetDesc& InPreset, const void* InOwner, std::string* OutError)
{
    const auto ScopedFail = [OutError](const char* InMessage)
    {
        if (OutError != nullptr)
        {
            *OutError = InMessage;
        }
        return false;
    };
    if (!IsValidId(InPreset.Id))
    {
        return ScopedFail("voice preset id must be 1..63 characters of [A-Za-z0-9.-]");
    }
    if (!IsValidText(InPreset.NameKey, kVoicePresetNameKeyBytes))
    {
        return ScopedFail("voice preset name key is empty or too long");
    }
    if (InPreset.Category == VoicePresetCategory::None || InPreset.Category >= VoicePresetCategory::Count)
    {
        return ScopedFail("voice preset category is invalid");
    }
    constexpr std::uint32_t ScopedKnownFlags =
        VoicePresetFlagCosmetic | VoicePresetFlagModeration | VoicePresetFlagAllowUplink | VoicePresetFlagAllowDownlink;
    if ((InPreset.Flags & ~ScopedKnownFlags) != 0)
    {
        return ScopedFail("voice preset uses an unknown flag");
    }
    if (Find(InPreset.Id) != nullptr)
    {
        return ScopedFail("voice preset id already exists");
    }
    if (MCount >= kVoicePresetMaxCount)
    {
        return ScopedFail("voice preset registry is full");
    }

    VoicePresetDesc ScopedDesc = InPreset;
    ScopedDesc.Profile.Clamp();
    if (ScopedDesc.Profile.Issue() != nullptr)
    {
        return ScopedFail("voice preset profile was rejected after clamping");
    }
    ScopedDesc.MinIntensity = std::clamp(ScopedDesc.MinIntensity, 0.0f, 1.0f);
    ScopedDesc.MaxIntensity = std::clamp(ScopedDesc.MaxIntensity, ScopedDesc.MinIntensity, 1.0f);

    MEntries[MCount].Desc = ScopedDesc;
    MEntries[MCount].Owner = InOwner;
    ++MCount;
    return true;
}

void VoicePresetRegistry::RemoveOwner(const void* InOwner) noexcept
{
    if (InOwner == nullptr)
    {
        return;
    }
    std::uint32_t ScopedWrite = 0;
    for (std::uint32_t ScopedRead = 0; ScopedRead < MCount; ++ScopedRead)
    {
        if (MEntries[ScopedRead].Owner == InOwner)
        {
            continue;
        }
        if (ScopedWrite != ScopedRead)
        {
            MEntries[ScopedWrite] = MEntries[ScopedRead];
        }
        ++ScopedWrite;
    }
    for (std::uint32_t ScopedIndex = ScopedWrite; ScopedIndex < MCount; ++ScopedIndex)
    {
        MEntries[ScopedIndex] = Entry{};
    }
    MCount = ScopedWrite;
}

void VoicePresetRegistry::Clear() noexcept
{
    MEntries = {};
    MCount = 0;
}

const VoicePresetDesc* VoicePresetRegistry::Find(std::string_view InId) const noexcept
{
    if (InId.empty() || InId.size() >= kVoicePresetIdBytes)
    {
        return nullptr;
    }
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < MCount; ++ScopedIndex)
    {
        if (InId == MEntries[ScopedIndex].Desc.Id)
        {
            return &MEntries[ScopedIndex].Desc;
        }
    }
    return nullptr;
}

const VoicePresetDesc* VoicePresetRegistry::At(std::uint32_t InIndex) const noexcept
{
    return InIndex < MCount ? &MEntries[InIndex].Desc : nullptr;
}

void VoicePresetChoice::Set(std::string_view InId, float InIntensity) noexcept
{
    CopyText(Id, kVoicePresetIdBytes, InId);
    Intensity = std::clamp(InIntensity, 0.0f, 1.0f);
    Enabled = Id[0] != '\0';
}

void VoicePresetChoice::Clear() noexcept
{
    Id[0] = '\0';
    Intensity = 1.0f;
    Enabled = false;
}

void VoicePresetSelection::Clear() noexcept
{
    Local.Clear();
    for (VoicePresetChoice& ScopedChoice : Participants)
    {
        ScopedChoice.Clear();
    }
}

VoicePresetResolution ResolveVoicePreset(const VoicePresetRegistry& InPresets, const VoiceEffectPolicy& InPolicy,
                                         const VoiceSession& InSession, std::uint8_t InPlayer,
                                         const VoicePresetChoice& InChoice) noexcept
{
    const VoiceParticipant* ScopedParticipant = InSession.Participant(InPlayer);
    if (ScopedParticipant != nullptr && ScopedParticipant->LocalMuted)
    {
        // A moderated participant never keeps a cosmetic preset: the policy
        // chooses mumbling or a hard mute, and the listener's choice is ignored.
        VoicePresetChoice ScopedForced{};
        ScopedForced.Set(InPolicy.MumbleWhenModerated ? InPolicy.ModerationPresetId : InPolicy.HardMutePresetId, 1.0f);
        VoicePresetResolution ScopedResolution = ResolveChoice(InPresets, ScopedForced, true);
        if (ScopedResolution.Preset == nullptr)
        {
            ScopedForced.Set(InPolicy.HardMutePresetId, 1.0f);
            ScopedResolution = ResolveChoice(InPresets, ScopedForced, true);
        }
        return ScopedResolution;
    }
    return ResolveChoice(InPresets, InChoice, false);
}

VoiceEffectProfile BuildVoicePresetProfile(const VoicePresetResolution& InResolution) noexcept
{
    VoiceEffectProfile ScopedProfile{};
    if (InResolution.Preset == nullptr)
    {
        return ScopedProfile;
    }
    ScopedProfile = InResolution.Preset->Profile;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedProfile.BaseChain.Count; ++ScopedIndex)
    {
        ScopedProfile.BaseChain.Slots[ScopedIndex].Mix =
            std::clamp(ScopedProfile.BaseChain.Slots[ScopedIndex].Mix * InResolution.Intensity, 0.0f, 1.0f);
    }
    ScopedProfile.Clamp();
    return ScopedProfile;
}

void VoicePresetApplier::Reset() noexcept
{
    for (Applied& ScopedApplied : MParticipants)
    {
        ScopedApplied = Applied{};
    }
    MLocal = Applied{};
}

void VoicePresetApplier::Apply(VoiceSystem& InVoice, const VoiceSession& InSession,
                               const VoicePresetRegistry& InPresets, const VoiceEffectPolicy& InPolicy,
                               const VoicePresetSelection& InSelection) noexcept
{
    for (std::uint8_t ScopedPlayer = 0; ScopedPlayer < static_cast<std::uint8_t>(kVoiceMaxPeers); ++ScopedPlayer)
    {
        const VoicePresetResolution ScopedResolution =
            ResolveVoicePreset(InPresets, InPolicy, InSession, ScopedPlayer, InSelection.Participants[ScopedPlayer]);
        const char* ScopedId = ScopedResolution.Preset != nullptr ? ScopedResolution.Preset->Id : "";
        Applied& ScopedApplied = MParticipants[ScopedPlayer];
        if (SameApplied(ScopedId, ScopedResolution.Intensity, ScopedResolution.Moderation, ScopedApplied.Id,
                        ScopedApplied.Intensity, ScopedApplied.Moderation, ScopedApplied.Valid))
        {
            continue;
        }
        InVoice.SetParticipantEffects(ScopedPlayer, BuildVoicePresetProfile(ScopedResolution));
        StoreApplied(ScopedApplied.Id, ScopedApplied.Intensity, ScopedApplied.Moderation, ScopedApplied.Valid,
                     ScopedResolution);
    }

    VoicePresetChoice ScopedLocalChoice = InSelection.Local;
    if (!InPolicy.AllowLocalUplink)
    {
        ScopedLocalChoice.Enabled = false;
    }
    VoicePresetResolution ScopedLocal = ResolveChoice(InPresets, ScopedLocalChoice, false);
    if (ScopedLocal.Preset != nullptr &&
        (ScopedLocal.Preset->Flags & VoicePresetFlagAllowUplink) == VoicePresetFlagNone)
    {
        ScopedLocal = VoicePresetResolution{};
    }
    const char* ScopedLocalId = ScopedLocal.Preset != nullptr ? ScopedLocal.Preset->Id : "";
    if (!SameApplied(ScopedLocalId, ScopedLocal.Intensity, ScopedLocal.Moderation, MLocal.Id, MLocal.Intensity,
                     MLocal.Moderation, MLocal.Valid))
    {
        InVoice.SetUplinkEffects(BuildVoicePresetProfile(ScopedLocal));
        StoreApplied(MLocal.Id, MLocal.Intensity, MLocal.Moderation, MLocal.Valid, ScopedLocal);
    }
}

void VoicePresetRegistry::RegisterBuiltIns()
{
    const auto AddPreset = [this](const char* InId, const char* InNameKey, VoicePresetCategory InCategory,
                                  std::uint32_t InFlags, const VoiceEffectChainSettings& InChain)
    {
        VoicePresetDesc ScopedDesc{};
        CopyText(ScopedDesc.Id, kVoicePresetIdBytes, InId);
        CopyText(ScopedDesc.NameKey, kVoicePresetNameKeyBytes, InNameKey);
        ScopedDesc.Category = InCategory;
        ScopedDesc.Flags = InFlags;
        ScopedDesc.Profile.BaseChain = InChain;
        ScopedDesc.Profile.EnableConditions = true;
        (void)Add(ScopedDesc, nullptr, nullptr);
    };
    const auto BuildChain = [](std::initializer_list<VoiceEffectSlot> InSlots)
    {
        VoiceEffectChainSettings ScopedChain{};
        for (const VoiceEffectSlot& ScopedSlot : InSlots)
        {
            (void)ScopedChain.Add(ScopedSlot);
        }
        return ScopedChain;
    };
    constexpr std::uint32_t ScopedCosmetic =
        VoicePresetFlagCosmetic | VoicePresetFlagAllowUplink | VoicePresetFlagAllowDownlink;
    constexpr std::uint32_t ScopedDownlink = VoicePresetFlagCosmetic | VoicePresetFlagAllowDownlink;
    constexpr std::uint32_t ScopedModeration = VoicePresetFlagModeration | VoicePresetFlagAllowDownlink;

    AddPreset("Fby.Voice.Muted", "voice.preset.muted", VoicePresetCategory::Moderation, ScopedModeration,
              BuildChain({MuteSlot(0.05f)}));
    AddPreset("Fby.Voice.ModerationMumble", "voice.preset.moderation-mumble", VoicePresetCategory::Moderation,
              ScopedModeration, BuildChain({MuffleSlot(180.0f, 500.0f, 1.4f, 10.0f, 3.0f, 0.25f)}));
    AddPreset("Fby.Voice.ImitationMumble", "voice.preset.imitation-mumble", VoicePresetCategory::Cosmetic,
              ScopedCosmetic, BuildChain({MuffleSlot(150.0f, 700.0f, 1.1f, 7.0f, 4.5f, 0.35f, 1.0f, 0.1f)}));
    AddPreset("Fby.Voice.CursedMumble", "voice.preset.cursed-mumble", VoicePresetCategory::Cosmetic, ScopedCosmetic,
              BuildChain({MuffleSlot(160.0f, 650.0f, 1.3f, 9.0f, 3.5f, 0.3f), DistortionSlot(18.0f, 6.0f, 0.35f)}));
    AddPreset("Fby.Voice.Underwater", "voice.preset.underwater", VoicePresetCategory::Environment, ScopedCosmetic,
              BuildChain({UnderwaterSlot(140.0f, 700.0f, 1.35f, 0.7f, 0.35f)}));
    AddPreset("Fby.Voice.DeepWater", "voice.preset.deep-water", VoicePresetCategory::Environment, ScopedCosmetic,
              BuildChain({UnderwaterSlot(90.0f, 420.0f, 1.6f, 0.45f, 0.5f)}));
    AddPreset("Fby.Voice.Swamp", "voice.preset.swamp", VoicePresetCategory::Environment, ScopedCosmetic,
              BuildChain({UnderwaterSlot(160.0f, 560.0f, 1.2f, 0.8f, 0.25f),
                          MuffleSlot(150.0f, 800.0f, 1.0f, 4.0f, 2.5f, 0.2f, 0.35f)}));
    AddPreset("Fby.Voice.SubmarineRadio", "voice.preset.submarine-radio", VoicePresetCategory::Environment,
              ScopedCosmetic,
              BuildChain({UnderwaterSlot(120.0f, 600.0f, 1.4f, 0.6f, 0.3f, 0.7f),
                          RadioSlot(300.0f, 2600.0f, 0.1f, 5.0f, 0.05f, 4.0f, false, 0.45f)}));
    AddPreset("Fby.Voice.Chipmunk", "voice.preset.chipmunk", VoicePresetCategory::Character, ScopedCosmetic,
              BuildChain({PitchSlot(12.0f, 20.0f), FormantSlot(9.0f, 6.0f, 0.7f)}));
    AddPreset("Fby.Voice.Helium", "voice.preset.helium", VoicePresetCategory::Character, ScopedCosmetic,
              BuildChain({PitchSlot(12.0f, 18.0f), FormantSlot(12.0f, 9.0f, 0.9f)}));
    AddPreset("Fby.Voice.SqueakyToy", "voice.preset.squeaky-toy", VoicePresetCategory::Character, ScopedCosmetic,
              BuildChain({PitchSlot(7.0f, 16.0f), FormantSlot(12.0f, 10.0f, 0.8f), RingSlot(40.0f, 0.2f, 0.25f)}));
    AddPreset("Fby.Voice.Deep", "voice.preset.deep", VoicePresetCategory::Character, ScopedCosmetic,
              BuildChain({PitchSlot(-7.0f, 21.0f), FormantSlot(-5.0f, 6.0f, 0.55f)}));
    AddPreset(
        "Fby.Voice.Giant", "voice.preset.giant", VoicePresetCategory::Character, ScopedCosmetic,
        BuildChain({PitchSlot(-12.0f, 21.0f), FormantSlot(-9.0f, 9.0f, 0.8f), DistortionSlot(8.0f, 12.0f, 0.2f)}));
    AddPreset("Fby.Voice.Satan", "voice.preset.satan", VoicePresetCategory::Character, ScopedCosmetic,
              BuildChain({PitchSlot(-12.0f, 21.0f), FormantSlot(-7.0f, 10.0f, 0.7f), RingSlot(33.0f, 0.6f, 0.5f),
                          DistortionSlot(10.0f, 10.0f, 0.25f)}));
    AddPreset("Fby.Voice.Robot", "voice.preset.robot", VoicePresetCategory::Character, ScopedCosmetic,
              BuildChain({RingSlot(28.0f, 0.9f, 0.85f), DistortionSlot(10.0f, 8.0f, 0.4f)}));
    AddPreset("Fby.Voice.Alien", "voice.preset.alien", VoicePresetCategory::Character, ScopedCosmetic,
              BuildChain({RingSlot(55.0f, 0.85f, 0.8f), FormantSlot(7.0f, 8.0f, 0.6f)}));
    AddPreset(
        "Fby.Voice.Ghost", "voice.preset.ghost", VoicePresetCategory::Character, ScopedCosmetic,
        BuildChain({WhisperSlot(0.4f, 900.0f, 6500.0f), MuffleSlot(220.0f, 900.0f, 1.0f, 3.0f, 1.5f, 0.15f, 0.5f)}));
    AddPreset(
        "Fby.Voice.OldRadio", "voice.preset.old-radio", VoicePresetCategory::Character, ScopedCosmetic,
        BuildChain({RadioSlot(350.0f, 3200.0f, 0.18f, 7.0f, 0.12f, 6.0f, false), DistortionSlot(6.0f, 12.0f, 0.15f)}));
    AddPreset("Fby.Voice.WalkieTalkie", "voice.preset.walkie-talkie", VoicePresetCategory::Character, ScopedCosmetic,
              BuildChain({RadioSlot(420.0f, 2800.0f, 0.25f, 9.0f, 0.2f, 8.0f, true)}));
    AddPreset(
        "Fby.Voice.Telephone", "voice.preset.telephone", VoicePresetCategory::Character, ScopedCosmetic,
        BuildChain({RadioSlot(500.0f, 3400.0f, 0.05f, 5.0f, 0.03f, 5.0f, false), DistortionSlot(6.0f, 12.0f, 0.15f)}));
    AddPreset(
        "Fby.Voice.Megaphone", "voice.preset.megaphone", VoicePresetCategory::Character, ScopedCosmetic,
        BuildChain({RadioSlot(700.0f, 5000.0f, 0.08f, 4.0f, 0.02f, 12.0f, false), DistortionSlot(12.0f, 10.0f, 0.3f)}));
    AddPreset("Fby.Voice.Arcade", "voice.preset.arcade", VoicePresetCategory::Character, ScopedCosmetic,
              BuildChain({DistortionSlot(16.0f, 5.0f), RingSlot(120.0f, 0.25f, 0.3f)}));
    AddPreset("Fby.Voice.Drunk", "voice.preset.drunk", VoicePresetCategory::Scene, ScopedDownlink,
              BuildChain({MuffleSlot(190.0f, 850.0f, 1.25f, 6.0f, 1.2f, 0.45f), PitchSlot(-2.0f, 21.0f, 0.5f)}));
    AddPreset("Fby.Voice.TapeWobble", "voice.preset.tape-wobble", VoicePresetCategory::Scene, ScopedCosmetic,
              BuildChain({MuffleSlot(180.0f, 1400.0f, 1.0f, 4.0f, 0.9f, 0.5f, 0.8f), DistortionSlot(6.0f, 10.0f, 0.15f),
                          PitchSlot(-1.0f, 21.0f, 0.3f)}));
}
}
