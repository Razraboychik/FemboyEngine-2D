// FemboyEngine2D - voice effect chains and environment conditions.
#pragma once

#include "Engine/Core/Pcg32.hpp"

#include <array>
#include <cstdint>

namespace Fby
{

// Fixed capacities keep every voice effect object out of the heap. A chain slot
// owns one delay line, so the per-sample path never allocates or branches on a
// container.
inline constexpr std::uint32_t kVoiceEffectMaxSlots = 8;
inline constexpr std::uint32_t kVoiceEffectDelaySamples = 2048;
inline constexpr std::uint32_t kVoiceEffectDefaultSampleRate = 48000;
inline constexpr std::uint32_t kVoiceEffectInvalidSlot = 0xFFFFFFFFu;

enum class VoiceEffectKind : std::uint8_t
{
    None = 0,
    Mute,
    Muffle,
    Underwater,
    Radio,
    Pitch,
    Formant,
    Distortion,
    RingModulation,
    Whisper,
    Count,
};

[[nodiscard]] constexpr const char* VoiceEffectKindName(VoiceEffectKind InKind) noexcept
{
    switch (InKind)
    {
        case VoiceEffectKind::None:
            return "none";
        case VoiceEffectKind::Mute:
            return "mute";
        case VoiceEffectKind::Muffle:
            return "muffle";
        case VoiceEffectKind::Underwater:
            return "underwater";
        case VoiceEffectKind::Radio:
            return "radio";
        case VoiceEffectKind::Pitch:
            return "pitch";
        case VoiceEffectKind::Formant:
            return "formant";
        case VoiceEffectKind::Distortion:
            return "distortion";
        case VoiceEffectKind::RingModulation:
            return "ring-modulation";
        case VoiceEffectKind::Whisper:
            return "whisper";
        case VoiceEffectKind::Count:
            break;
    }
    return "?";
}

// Gain fade used by the direct mute effect. Zero makes the mute immediate,
// which is the right choice for a hard moderation mute.
struct VoiceMuteSettings
{
    float FadeSeconds = 0.08f;

    void Clamp() noexcept;
    [[nodiscard]] const char* Issue() const noexcept;

    [[nodiscard]] friend bool operator==(const VoiceMuteSettings&, const VoiceMuteSettings&) noexcept = default;
};

// Unintelligible speech: a narrow low band, saturation and a slow tremolo. The
// result still carries the speaker's rhythm and loudness, so a muted player
// reads as "mumbling" instead of disappearing.
struct VoiceMuffleSettings
{
    float LowCutHz = 120.0f;
    float HighCutHz = 620.0f;
    float Resonance = 1.0f;
    float DriveDb = 9.0f;
    float WobbleHz = 3.2f;
    float WobbleDepth = 0.28f;
    float BreathLevel = 0.06f;

    void Clamp() noexcept;
    [[nodiscard]] const char* Issue() const noexcept;

    [[nodiscard]] friend bool operator==(const VoiceMuffleSettings&, const VoiceMuffleSettings&) noexcept = default;
};

// A resonant low band plus a slow delay wobble. The effect only filters and
// modulates the voice; bubbles and particle effects belong to the game.
struct VoiceUnderwaterSettings
{
    float LowCutHz = 140.0f;
    float HighCutHz = 700.0f;
    float Resonance = 1.35f;
    float WowHz = 0.7f;
    float WowDepth = 0.35f;

    void Clamp() noexcept;
    [[nodiscard]] const char* Issue() const noexcept;

    [[nodiscard]] friend bool operator==(const VoiceUnderwaterSettings&,
                                         const VoiceUnderwaterSettings&) noexcept = default;
};

// Narrow-band radio: band limiting, drive, static and crackle. Squelch is a
// dropout amount; MuteWhenSquelched turns the dropout into a hard silence.
struct VoiceRadioSettings
{
    float LowCutHz = 300.0f;
    float HighCutHz = 3200.0f;
    float StaticLevel = 0.18f;
    float CrackleRate = 7.0f;
    float Squelch = 0.12f;
    float DriveDb = 6.0f;
    bool MuteWhenSquelched = false;

    void Clamp() noexcept;
    [[nodiscard]] const char* Issue() const noexcept;

    [[nodiscard]] friend bool operator==(const VoiceRadioSettings&, const VoiceRadioSettings&) noexcept = default;
};

// Time-domain granular pitch shifter. The window is the grain size in
// milliseconds; the fixed delay line limits it to 8..21 ms at 48 kHz.
struct VoicePitchSettings
{
    float Semitones = 7.0f;
    float WindowMs = 20.0f;

    void Clamp() noexcept;
    [[nodiscard]] const char* Issue() const noexcept;

    [[nodiscard]] friend bool operator==(const VoicePitchSettings&, const VoicePitchSettings&) noexcept = default;
};

// Three peaking resonators approximate a shifted vocal-tract colour. This is a
// timbre control, not an independent spectral-envelope extraction.
struct VoiceFormantSettings
{
    float Semitones = 4.0f;
    float StrengthDb = 6.0f;

    void Clamp() noexcept;
    [[nodiscard]] const char* Issue() const noexcept;

    [[nodiscard]] friend bool operator==(const VoiceFormantSettings&, const VoiceFormantSettings&) noexcept = default;
};

struct VoiceDistortionSettings
{
    float DriveDb = 12.0f;
    float LowCutHz = 180.0f;
    float HighCutHz = 5000.0f;
    float BitDepth = 10.0f;

    void Clamp() noexcept;
    [[nodiscard]] const char* Issue() const noexcept;

    [[nodiscard]] friend bool operator==(const VoiceDistortionSettings&,
                                         const VoiceDistortionSettings&) noexcept = default;
};

struct VoiceRingModulationSettings
{
    float FrequencyHz = 30.0f;
    float Depth = 0.7f;

    void Clamp() noexcept;
    [[nodiscard]] const char* Issue() const noexcept;

    [[nodiscard]] friend bool operator==(const VoiceRingModulationSettings&,
                                         const VoiceRingModulationSettings&) noexcept = default;
};

// Breath noise shaped by the input envelope. LowCutHz removes the voiced body,
// which is what makes the result read as a whisper rather than quiet speech.
struct VoiceWhisperSettings
{
    float NoiseLevel = 0.35f;
    float LowCutHz = 900.0f;
    float HighCutHz = 6500.0f;

    void Clamp() noexcept;
    [[nodiscard]] const char* Issue() const noexcept;

    [[nodiscard]] friend bool operator==(const VoiceWhisperSettings&, const VoiceWhisperSettings&) noexcept = default;
};

// One entry in a serial effect chain. Only the parameters of the selected Kind
// are read; the other groups are carried so a UI can switch kinds without
// losing the last authored values.
struct VoiceEffectSlot
{
    VoiceEffectKind Kind = VoiceEffectKind::None;
    float Mix = 1.0f;
    VoiceMuteSettings Mute{};
    VoiceMuffleSettings Muffle{};
    VoiceUnderwaterSettings Underwater{};
    VoiceRadioSettings Radio{};
    VoicePitchSettings Pitch{};
    VoiceFormantSettings Formant{};
    VoiceDistortionSettings Distortion{};
    VoiceRingModulationSettings RingModulation{};
    VoiceWhisperSettings Whisper{};

    void Clamp() noexcept;
    [[nodiscard]] const char* Issue() const noexcept;

    [[nodiscard]] friend bool operator==(const VoiceEffectSlot&, const VoiceEffectSlot&) noexcept = default;
};

// A serial chain. Slots run from 0 to Count - 1; an unused slot is skipped.
struct VoiceEffectChainSettings
{
    std::array<VoiceEffectSlot, kVoiceEffectMaxSlots> Slots{};
    std::uint32_t Count = 0;

    void Clear() noexcept;
    [[nodiscard]] bool Add(const VoiceEffectSlot& InSlot) noexcept;
    void Clamp() noexcept;
    [[nodiscard]] const char* Issue() const noexcept;

    [[nodiscard]] friend bool operator==(const VoiceEffectChainSettings&,
                                         const VoiceEffectChainSettings&) noexcept = default;
};

enum class VoiceEffectPresetKind : std::uint8_t
{
    None = 0,
    Muted,
    Mumbling,
    Underwater,
    Radio,
    RadioMuted,
    Chipmunk,
    Deep,
    Robot,
    Whisper,
    Count,
};

[[nodiscard]] constexpr const char* VoiceEffectPresetName(VoiceEffectPresetKind InPreset) noexcept
{
    switch (InPreset)
    {
        case VoiceEffectPresetKind::None:
            return "none";
        case VoiceEffectPresetKind::Muted:
            return "muted";
        case VoiceEffectPresetKind::Mumbling:
            return "mumbling";
        case VoiceEffectPresetKind::Underwater:
            return "underwater";
        case VoiceEffectPresetKind::Radio:
            return "radio";
        case VoiceEffectPresetKind::RadioMuted:
            return "radio-muted";
        case VoiceEffectPresetKind::Chipmunk:
            return "chipmunk";
        case VoiceEffectPresetKind::Deep:
            return "deep";
        case VoiceEffectPresetKind::Robot:
            return "robot";
        case VoiceEffectPresetKind::Whisper:
            return "whisper";
        case VoiceEffectPresetKind::Count:
            break;
    }
    return "?";
}

[[nodiscard]] VoiceEffectChainSettings VoiceEffectPresetChain(VoiceEffectPresetKind InPreset) noexcept;

// Condition mapping. It turns game data into the reserved underwater/radio
// slots and exposes the smoothed state; it does not own oxygen or bubbles.
struct VoiceConditionSettings
{
    bool EnableUnderwater = true;
    bool EnableInterference = true;
    float UnderwaterFadeSeconds = 0.25f;
    float UnderwaterLowCutHz = 140.0f;
    float UnderwaterHighCutHz = 700.0f;
    float UnderwaterResonance = 1.35f;
    float UnderwaterWowHz = 0.7f;
    float UnderwaterWowDepth = 0.35f;
    float InterferenceFadeSeconds = 0.1f;
    float InterferenceStaticLevel = 0.25f;
    float InterferenceCrackleRate = 10.0f;
    // At or above this interference the output gain is reduced to zero when
    // InterferenceHardMute is set.
    float InterferenceMuteThreshold = 0.9f;
    bool InterferenceHardMute = false;

    void Clamp() noexcept;
    [[nodiscard]] const char* Issue() const noexcept;

    [[nodiscard]] friend bool operator==(const VoiceConditionSettings&,
                                         const VoiceConditionSettings&) noexcept = default;
};

// Authored profile for one stream: a fixed chain plus optional condition slots.
struct VoiceEffectProfile
{
    VoiceEffectChainSettings BaseChain{};
    VoiceConditionSettings Conditions{};
    bool EnableConditions = true;

    void Clamp() noexcept;
    [[nodiscard]] const char* Issue() const noexcept;

    [[nodiscard]] friend bool operator==(const VoiceEffectProfile&, const VoiceEffectProfile&) noexcept = default;
};

// Game-owned environment sample. The engine reads it to drive filtering and
// gain; oxygen is passed through for the game's own HUD and particle logic.
struct VoiceConditionState
{
    bool Submerged = false;
    float Oxygen01 = 1.0f;
    float Interference01 = 0.0f;
};

// Read-only result for HUD, gameplay and any particle system the game owns.
// OutputGain is the condition-driven gain applied by the rack.
struct VoiceConditionOutput
{
    bool Submerged = false;
    float UnderwaterBlend = 0.0f;
    float Oxygen01 = 1.0f;
    float Interference = 0.0f;
    float OutputGain = 1.0f;
};

// Fixed-state serial chain. Configure once, then Process any block size. Slot
// parameters may be updated between blocks without dropping the filter and
// delay history.
class VoiceEffectChain
{
public:
    VoiceEffectChain() = default;
    ~VoiceEffectChain() = default;

    VoiceEffectChain(const VoiceEffectChain&) = delete;
    VoiceEffectChain& operator=(const VoiceEffectChain&) = delete;

    void Configure(const VoiceEffectChainSettings& InSettings, std::uint32_t InSampleRate) noexcept;
    void UpdateSlot(std::uint32_t InIndex, const VoiceEffectSlot& InSlot) noexcept;
    void Reset() noexcept;
    void Process(float* InSamples, std::uint32_t InFrameCount) noexcept;

    [[nodiscard]] std::uint32_t SlotCount() const noexcept
    {
        return MSlotCount;
    }
    [[nodiscard]] const VoiceEffectChainSettings& Settings() const noexcept
    {
        return MSettings;
    }

private:
    struct BiquadState
    {
        float B0 = 1.0f;
        float B1 = 0.0f;
        float B2 = 0.0f;
        float A1 = 0.0f;
        float A2 = 0.0f;
        float X1 = 0.0f;
        float X2 = 0.0f;
        float Y1 = 0.0f;
        float Y2 = 0.0f;

        void Reset() noexcept;
        [[nodiscard]] float Process(float InSample) noexcept;
    };

    struct SlotRuntime
    {
        BiquadState LowCut{};
        BiquadState HighCut{};
        BiquadState PeakA{};
        BiquadState PeakB{};
        BiquadState PeakC{};
        std::array<float, kVoiceEffectDelaySamples> Delay{};
        std::uint32_t WriteIndex = 0;
        float MixCurrent = 1.0f;
        float MixTarget = 1.0f;
        float MixCoefficient = 0.05f;
        float PitchPhase = 0.0f;
        float PitchRatio = 1.0f;
        float PitchWindow = 512.0f;
        float WowPhase = 0.0f;
        float WowDelaySamples = 0.0f;
        float WowDepthSamples = 0.0f;
        float TremoloPhase = 0.0f;
        float RingPhase = 0.0f;
        float CrackleTimer = 0.0f;
        float CrackleEnvelope = 0.0f;
        float SquelchTimer = 0.0f;
        float SquelchGain = 1.0f;
        float SquelchCurrent = 1.0f;
        float StaticLowPass = 0.0f;
        float FadeGain = 1.0f;
        float FadeCoefficient = 1.0f;
        float LastEnvelope = 0.0f;
        Pcg32 Rng{};
    };

    void PrepareSlot(std::uint32_t InIndex) noexcept;
    void ResetSlot(std::uint32_t InIndex) noexcept;
    [[nodiscard]] float ReadDelay(const SlotRuntime& InRuntime, float InDelaySamples) const noexcept;
    void ProcessSlot(std::uint32_t InIndex, float* InSamples, std::uint32_t InFrameCount) noexcept;
    [[nodiscard]] float EffectiveMix(const SlotRuntime& InRuntime) const noexcept;

    VoiceEffectChainSettings MSettings{};
    std::array<SlotRuntime, kVoiceEffectMaxSlots> MRuntime{};
    std::uint32_t MSlotCount = 0;
    std::uint32_t MSampleRate = kVoiceEffectDefaultSampleRate;
    bool MConfigured = false;
};

// Per-stream effect unit. It combines an authored chain with the reserved
// condition slots and exposes the smoothed condition values for the game. All
// methods are game-thread operations; the DSP itself does not allocate.
class VoiceEffectRack
{
public:
    VoiceEffectRack() = default;
    ~VoiceEffectRack() = default;

    VoiceEffectRack(const VoiceEffectRack&) = delete;
    VoiceEffectRack& operator=(const VoiceEffectRack&) = delete;

    void Configure(const VoiceEffectProfile& InProfile,
                   std::uint32_t InSampleRate = kVoiceEffectDefaultSampleRate) noexcept;
    void Reset() noexcept;
    // Stores the latest environment sample. Advance turns it into smoothed slot
    // parameters; the game reads the result through Condition().
    void SetConditionState(const VoiceConditionState& InState) noexcept;
    void Advance(float InDeltaSeconds) noexcept;
    void Process(float* InSamples, std::uint32_t InFrameCount) noexcept;

    [[nodiscard]] const VoiceConditionOutput& Condition() const noexcept
    {
        return MCondition;
    }
    [[nodiscard]] const VoiceEffectChainSettings& EffectiveChain() const noexcept
    {
        return MEffectiveChain;
    }
    [[nodiscard]] bool Active() const noexcept
    {
        return MActive;
    }

private:
    void BuildEffectiveChain() noexcept;
    [[nodiscard]] std::uint32_t FindSlot(VoiceEffectKind InKind) const noexcept;
    void UpdateUnderwaterSlot(float InDeltaSeconds) noexcept;
    void UpdateRadioSlot(float InDeltaSeconds) noexcept;

    VoiceEffectProfile MProfile{};
    VoiceEffectChainSettings MEffectiveChain{};
    VoiceEffectChain MChain{};
    VoiceConditionState MState{};
    VoiceConditionOutput MCondition{};
    std::uint32_t MUnderwaterSlot = kVoiceEffectInvalidSlot;
    std::uint32_t MRadioSlot = kVoiceEffectInvalidSlot;
    float MUnderwaterBaseMix = 0.0f;
    float MRadioBaseMix = 0.0f;
    bool MUnderwaterOwned = false;
    bool MRadioOwned = false;
    float MUnderwaterBlend = 0.0f;
    float MInterference = 0.0f;
    float MOutputGain = 1.0f;
    float MOutputGainCoefficient = 0.01f;
    std::uint32_t MSampleRate = kVoiceEffectDefaultSampleRate;
    bool MActive = false;
};
}
