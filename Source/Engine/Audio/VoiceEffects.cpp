// FemboyEngine2D - voice effect chain and condition tracker implementation.
#include "Engine/Audio/VoiceEffects.hpp"

#include <algorithm>
#include <cmath>

namespace Fby
{
namespace
{
constexpr float kTwoPi = 6.28318530717958647692f;

[[nodiscard]] float Clamp01(float InValue) noexcept
{
    return std::clamp(InValue, 0.0f, 1.0f);
}

[[nodiscard]] float DbToLinear(float InDb) noexcept
{
    return std::pow(10.0f, InDb * 0.05f);
}

[[nodiscard]] float SmoothingCoefficient(float InMilliseconds, std::uint32_t InSampleRate) noexcept
{
    if (InMilliseconds <= 0.0f)
    {
        return 1.0f;
    }
    const float ScopedSamples = InMilliseconds * 0.001f * static_cast<float>(InSampleRate);
    return 1.0f - std::exp(-1.0f / std::max(ScopedSamples, 1.0f));
}

[[nodiscard]] float SmoothValue(float InCurrent, float InTarget, float InSeconds, float InDeltaSeconds) noexcept
{
    if (InSeconds <= 0.0f || InDeltaSeconds <= 0.0f)
    {
        return InTarget;
    }
    const float ScopedCoefficient = 1.0f - std::exp(-InDeltaSeconds / InSeconds);
    return InCurrent + (InTarget - InCurrent) * ScopedCoefficient;
}

[[nodiscard]] float WrapPhase(float InPhase) noexcept
{
    if (InPhase >= kTwoPi)
    {
        InPhase -= kTwoPi * std::floor(InPhase / kTwoPi);
    }
    else if (InPhase < 0.0f)
    {
        InPhase += kTwoPi * std::floor(-InPhase / kTwoPi + 1.0f);
    }
    return InPhase;
}

[[nodiscard]] float WrapUnit(float InValue) noexcept
{
    InValue -= std::floor(InValue);
    return InValue;
}

[[nodiscard]] float NextNoise(Pcg32& InRng) noexcept
{
    return InRng.NextFloat() * 2.0f - 1.0f;
}

}

void VoiceMuteSettings::Clamp() noexcept
{
    FadeSeconds = std::clamp(FadeSeconds, 0.0f, 2.0f);
}

const char* VoiceMuteSettings::Issue() const noexcept
{
    VoiceMuteSettings ScopedCopy = *this;
    ScopedCopy.Clamp();
    return ScopedCopy == *this ? nullptr : "voice mute settings were clamped into range";
}

void VoiceMuffleSettings::Clamp() noexcept
{
    LowCutHz = std::clamp(LowCutHz, 20.0f, 1000.0f);
    HighCutHz = std::clamp(HighCutHz, 100.0f, 12000.0f);
    if (LowCutHz >= HighCutHz)
    {
        LowCutHz = HighCutHz * 0.5f;
    }
    Resonance = std::clamp(Resonance, 0.1f, 8.0f);
    DriveDb = std::clamp(DriveDb, 0.0f, 36.0f);
    WobbleHz = std::clamp(WobbleHz, 0.1f, 12.0f);
    WobbleDepth = std::clamp(WobbleDepth, 0.0f, 1.0f);
    BreathLevel = std::clamp(BreathLevel, 0.0f, 1.0f);
}

const char* VoiceMuffleSettings::Issue() const noexcept
{
    VoiceMuffleSettings ScopedCopy = *this;
    ScopedCopy.Clamp();
    return ScopedCopy == *this ? nullptr : "voice muffle settings were clamped into range";
}

void VoiceUnderwaterSettings::Clamp() noexcept
{
    LowCutHz = std::clamp(LowCutHz, 20.0f, 1000.0f);
    HighCutHz = std::clamp(HighCutHz, 100.0f, 8000.0f);
    if (LowCutHz >= HighCutHz)
    {
        LowCutHz = HighCutHz * 0.5f;
    }
    Resonance = std::clamp(Resonance, 0.1f, 8.0f);
    WowHz = std::clamp(WowHz, 0.05f, 8.0f);
    WowDepth = std::clamp(WowDepth, 0.0f, 1.0f);
}

const char* VoiceUnderwaterSettings::Issue() const noexcept
{
    VoiceUnderwaterSettings ScopedCopy = *this;
    ScopedCopy.Clamp();
    return ScopedCopy == *this ? nullptr : "voice underwater settings were clamped into range";
}

void VoiceRadioSettings::Clamp() noexcept
{
    LowCutHz = std::clamp(LowCutHz, 50.0f, 3000.0f);
    HighCutHz = std::clamp(HighCutHz, 500.0f, 12000.0f);
    if (LowCutHz >= HighCutHz)
    {
        LowCutHz = HighCutHz * 0.5f;
    }
    StaticLevel = std::clamp(StaticLevel, 0.0f, 1.0f);
    CrackleRate = std::clamp(CrackleRate, 0.0f, 40.0f);
    Squelch = std::clamp(Squelch, 0.0f, 1.0f);
    DriveDb = std::clamp(DriveDb, 0.0f, 36.0f);
}

const char* VoiceRadioSettings::Issue() const noexcept
{
    VoiceRadioSettings ScopedCopy = *this;
    ScopedCopy.Clamp();
    return ScopedCopy == *this ? nullptr : "voice radio settings were clamped into range";
}

void VoicePitchSettings::Clamp() noexcept
{
    Semitones = std::clamp(Semitones, -24.0f, 24.0f);
    WindowMs = std::clamp(WindowMs, 8.0f, 21.0f);
}

const char* VoicePitchSettings::Issue() const noexcept
{
    VoicePitchSettings ScopedCopy = *this;
    ScopedCopy.Clamp();
    return ScopedCopy == *this ? nullptr : "voice pitch settings were clamped into range";
}

void VoiceFormantSettings::Clamp() noexcept
{
    Semitones = std::clamp(Semitones, -24.0f, 24.0f);
    StrengthDb = std::clamp(StrengthDb, 0.0f, 24.0f);
}

const char* VoiceFormantSettings::Issue() const noexcept
{
    VoiceFormantSettings ScopedCopy = *this;
    ScopedCopy.Clamp();
    return ScopedCopy == *this ? nullptr : "voice formant settings were clamped into range";
}

void VoiceDistortionSettings::Clamp() noexcept
{
    DriveDb = std::clamp(DriveDb, 0.0f, 48.0f);
    LowCutHz = std::clamp(LowCutHz, 20.0f, 2000.0f);
    HighCutHz = std::clamp(HighCutHz, 500.0f, 16000.0f);
    if (LowCutHz >= HighCutHz)
    {
        LowCutHz = HighCutHz * 0.5f;
    }
    BitDepth = std::clamp(BitDepth, 2.0f, 16.0f);
}

const char* VoiceDistortionSettings::Issue() const noexcept
{
    VoiceDistortionSettings ScopedCopy = *this;
    ScopedCopy.Clamp();
    return ScopedCopy == *this ? nullptr : "voice distortion settings were clamped into range";
}

void VoiceRingModulationSettings::Clamp() noexcept
{
    FrequencyHz = std::clamp(FrequencyHz, 1.0f, 500.0f);
    Depth = std::clamp(Depth, 0.0f, 1.0f);
}

const char* VoiceRingModulationSettings::Issue() const noexcept
{
    VoiceRingModulationSettings ScopedCopy = *this;
    ScopedCopy.Clamp();
    return ScopedCopy == *this ? nullptr : "voice ring modulation settings were clamped into range";
}

void VoiceWhisperSettings::Clamp() noexcept
{
    NoiseLevel = std::clamp(NoiseLevel, 0.0f, 1.0f);
    LowCutHz = std::clamp(LowCutHz, 100.0f, 6000.0f);
    HighCutHz = std::clamp(HighCutHz, 1000.0f, 16000.0f);
    if (LowCutHz >= HighCutHz)
    {
        LowCutHz = HighCutHz * 0.5f;
    }
}

const char* VoiceWhisperSettings::Issue() const noexcept
{
    VoiceWhisperSettings ScopedCopy = *this;
    ScopedCopy.Clamp();
    return ScopedCopy == *this ? nullptr : "voice whisper settings were clamped into range";
}

void VoiceEffectSlot::Clamp() noexcept
{
    if (Kind >= VoiceEffectKind::Count)
    {
        Kind = VoiceEffectKind::None;
    }
    Mix = Clamp01(Mix);
    Mute.Clamp();
    Muffle.Clamp();
    Underwater.Clamp();
    Radio.Clamp();
    Pitch.Clamp();
    Formant.Clamp();
    Distortion.Clamp();
    RingModulation.Clamp();
    Whisper.Clamp();
}

const char* VoiceEffectSlot::Issue() const noexcept
{
    VoiceEffectSlot ScopedCopy = *this;
    ScopedCopy.Clamp();
    return ScopedCopy == *this ? nullptr : "voice effect slot was clamped into range";
}

void VoiceEffectChainSettings::Clear() noexcept
{
    for (VoiceEffectSlot& ScopedSlot : Slots)
    {
        ScopedSlot = VoiceEffectSlot{};
    }
    Count = 0;
}

bool VoiceEffectChainSettings::Add(const VoiceEffectSlot& InSlot) noexcept
{
    if (Count >= kVoiceEffectMaxSlots)
    {
        return false;
    }
    Slots[Count] = InSlot;
    ++Count;
    return true;
}

void VoiceEffectChainSettings::Clamp() noexcept
{
    Count = std::min(Count, kVoiceEffectMaxSlots);
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < Count; ++ScopedIndex)
    {
        Slots[ScopedIndex].Clamp();
    }
}

const char* VoiceEffectChainSettings::Issue() const noexcept
{
    VoiceEffectChainSettings ScopedCopy = *this;
    ScopedCopy.Clamp();
    if (!(ScopedCopy == *this))
    {
        return "voice effect chain was clamped into range";
    }
    return nullptr;
}

void VoiceConditionSettings::Clamp() noexcept
{
    UnderwaterFadeSeconds = std::clamp(UnderwaterFadeSeconds, 0.0f, 2.0f);
    UnderwaterLowCutHz = std::clamp(UnderwaterLowCutHz, 20.0f, 1000.0f);
    UnderwaterHighCutHz = std::clamp(UnderwaterHighCutHz, 100.0f, 8000.0f);
    if (UnderwaterLowCutHz >= UnderwaterHighCutHz)
    {
        UnderwaterLowCutHz = UnderwaterHighCutHz * 0.5f;
    }
    UnderwaterResonance = std::clamp(UnderwaterResonance, 0.1f, 8.0f);
    UnderwaterWowHz = std::clamp(UnderwaterWowHz, 0.05f, 8.0f);
    UnderwaterWowDepth = std::clamp(UnderwaterWowDepth, 0.0f, 1.0f);
    InterferenceFadeSeconds = std::clamp(InterferenceFadeSeconds, 0.0f, 2.0f);
    InterferenceStaticLevel = std::clamp(InterferenceStaticLevel, 0.0f, 1.0f);
    InterferenceCrackleRate = std::clamp(InterferenceCrackleRate, 0.0f, 60.0f);
    InterferenceMuteThreshold = std::clamp(InterferenceMuteThreshold, 0.0f, 1.0f);
}

const char* VoiceConditionSettings::Issue() const noexcept
{
    VoiceConditionSettings ScopedCopy = *this;
    ScopedCopy.Clamp();
    return ScopedCopy == *this ? nullptr : "voice condition settings were clamped into range";
}

void VoiceEffectProfile::Clamp() noexcept
{
    BaseChain.Clamp();
    Conditions.Clamp();
}

const char* VoiceEffectProfile::Issue() const noexcept
{
    VoiceEffectProfile ScopedCopy = *this;
    ScopedCopy.Clamp();
    return ScopedCopy == *this ? nullptr : "voice effect profile was clamped into range";
}

VoiceEffectChainSettings VoiceEffectPresetChain(VoiceEffectPresetKind InPreset) noexcept
{
    VoiceEffectChainSettings ScopedChain{};
    VoiceEffectSlot ScopedSlot{};
    switch (InPreset)
    {
        case VoiceEffectPresetKind::None:
            break;
        case VoiceEffectPresetKind::Muted:
            ScopedSlot.Kind = VoiceEffectKind::Mute;
            ScopedSlot.Mix = 1.0f;
            ScopedSlot.Mute.FadeSeconds = 0.05f;
            (void)ScopedChain.Add(ScopedSlot);
            break;
        case VoiceEffectPresetKind::Mumbling:
            ScopedSlot.Kind = VoiceEffectKind::Muffle;
            ScopedSlot.Mix = 1.0f;
            (void)ScopedChain.Add(ScopedSlot);
            break;
        case VoiceEffectPresetKind::Underwater:
            ScopedSlot.Kind = VoiceEffectKind::Underwater;
            ScopedSlot.Mix = 1.0f;
            (void)ScopedChain.Add(ScopedSlot);
            break;
        case VoiceEffectPresetKind::Radio:
            ScopedSlot.Kind = VoiceEffectKind::Radio;
            ScopedSlot.Mix = 1.0f;
            (void)ScopedChain.Add(ScopedSlot);
            break;
        case VoiceEffectPresetKind::RadioMuted:
            ScopedSlot.Kind = VoiceEffectKind::Radio;
            ScopedSlot.Mix = 1.0f;
            (void)ScopedChain.Add(ScopedSlot);
            ScopedSlot = VoiceEffectSlot{};
            ScopedSlot.Kind = VoiceEffectKind::Mute;
            ScopedSlot.Mix = 1.0f;
            ScopedSlot.Mute.FadeSeconds = 0.12f;
            (void)ScopedChain.Add(ScopedSlot);
            break;
        case VoiceEffectPresetKind::Chipmunk:
            ScopedSlot.Kind = VoiceEffectKind::Pitch;
            ScopedSlot.Mix = 1.0f;
            ScopedSlot.Pitch.Semitones = 12.0f;
            ScopedSlot.Pitch.WindowMs = 20.0f;
            (void)ScopedChain.Add(ScopedSlot);
            ScopedSlot = VoiceEffectSlot{};
            ScopedSlot.Kind = VoiceEffectKind::Formant;
            ScopedSlot.Mix = 0.65f;
            ScopedSlot.Formant.Semitones = 6.0f;
            (void)ScopedChain.Add(ScopedSlot);
            break;
        case VoiceEffectPresetKind::Deep:
            ScopedSlot.Kind = VoiceEffectKind::Pitch;
            ScopedSlot.Mix = 1.0f;
            ScopedSlot.Pitch.Semitones = -7.0f;
            ScopedSlot.Pitch.WindowMs = 21.0f;
            (void)ScopedChain.Add(ScopedSlot);
            ScopedSlot = VoiceEffectSlot{};
            ScopedSlot.Kind = VoiceEffectKind::Formant;
            ScopedSlot.Mix = 0.55f;
            ScopedSlot.Formant.Semitones = -4.0f;
            (void)ScopedChain.Add(ScopedSlot);
            break;
        case VoiceEffectPresetKind::Robot:
            ScopedSlot.Kind = VoiceEffectKind::RingModulation;
            ScopedSlot.Mix = 0.85f;
            ScopedSlot.RingModulation.FrequencyHz = 28.0f;
            ScopedSlot.RingModulation.Depth = 0.9f;
            (void)ScopedChain.Add(ScopedSlot);
            ScopedSlot = VoiceEffectSlot{};
            ScopedSlot.Kind = VoiceEffectKind::Distortion;
            ScopedSlot.Mix = 0.4f;
            ScopedSlot.Distortion.DriveDb = 10.0f;
            ScopedSlot.Distortion.BitDepth = 8.0f;
            (void)ScopedChain.Add(ScopedSlot);
            break;
        case VoiceEffectPresetKind::Whisper:
            ScopedSlot.Kind = VoiceEffectKind::Whisper;
            ScopedSlot.Mix = 1.0f;
            (void)ScopedChain.Add(ScopedSlot);
            break;
        case VoiceEffectPresetKind::Count:
            break;
    }
    ScopedChain.Clamp();
    return ScopedChain;
}

void VoiceEffectChain::BiquadState::Reset() noexcept
{
    B0 = 1.0f;
    B1 = 0.0f;
    B2 = 0.0f;
    A1 = 0.0f;
    A2 = 0.0f;
    X1 = 0.0f;
    X2 = 0.0f;
    Y1 = 0.0f;
    Y2 = 0.0f;
}

float VoiceEffectChain::BiquadState::Process(float InSample) noexcept
{
    const float ScopedOutput = B0 * InSample + B1 * X1 + B2 * X2 - A1 * Y1 - A2 * Y2;
    X2 = X1;
    X1 = InSample;
    Y2 = Y1;
    Y1 = ScopedOutput;
    return ScopedOutput;
}

void VoiceEffectChain::Configure(const VoiceEffectChainSettings& InSettings, std::uint32_t InSampleRate) noexcept
{
    MSettings = InSettings;
    MSettings.Clamp();
    MSlotCount = std::min(MSettings.Count, kVoiceEffectMaxSlots);
    MSettings.Count = MSlotCount;
    MSampleRate = InSampleRate == 0 ? kVoiceEffectDefaultSampleRate : InSampleRate;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < kVoiceEffectMaxSlots; ++ScopedIndex)
    {
        ResetSlot(ScopedIndex);
        PrepareSlot(ScopedIndex);
    }
    MConfigured = true;
}

void VoiceEffectChain::UpdateSlot(std::uint32_t InIndex, const VoiceEffectSlot& InSlot) noexcept
{
    if (InIndex >= kVoiceEffectMaxSlots)
    {
        return;
    }
    VoiceEffectSlot ScopedSlot = InSlot;
    ScopedSlot.Clamp();
    const bool ScopedKindChanged = MSettings.Slots[InIndex].Kind != ScopedSlot.Kind;
    MSettings.Slots[InIndex] = ScopedSlot;
    if (InIndex >= MSlotCount)
    {
        MSlotCount = InIndex + 1;
    }
    MSettings.Count = MSlotCount;
    if (ScopedKindChanged)
    {
        ResetSlot(InIndex);
    }
    PrepareSlot(InIndex);
}

void VoiceEffectChain::Reset() noexcept
{
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < kVoiceEffectMaxSlots; ++ScopedIndex)
    {
        ResetSlot(ScopedIndex);
        PrepareSlot(ScopedIndex);
    }
}

void VoiceEffectChain::ResetSlot(std::uint32_t InIndex) noexcept
{
    SlotRuntime& ScopedRuntime = MRuntime[InIndex];
    ScopedRuntime = SlotRuntime{};
    ScopedRuntime.Rng.Seed(0x9E3779B97F4A7C15ull ^
                           (static_cast<std::uint64_t>(InIndex) + 1ull) * 0xD1B54A32D192ED03ull);
}

void VoiceEffectChain::PrepareSlot(std::uint32_t InIndex) noexcept
{
    SlotRuntime& ScopedRuntime = MRuntime[InIndex];
    const VoiceEffectSlot& ScopedSlot = MSettings.Slots[InIndex];
    const float ScopedSampleRate = static_cast<float>(MSampleRate);
    const float ScopedNyquist = ScopedSampleRate * 0.45f;

    auto ScopedSetBypass = [](BiquadState& InFilter) noexcept
    {
        InFilter.B0 = 1.0f;
        InFilter.B1 = 0.0f;
        InFilter.B2 = 0.0f;
        InFilter.A1 = 0.0f;
        InFilter.A2 = 0.0f;
    };
    auto ScopedSetLowPass = [ScopedSampleRate, ScopedNyquist](BiquadState& InFilter, float InHz, float InQ) noexcept
    {
        const float ScopedHz = std::clamp(InHz, 20.0f, ScopedNyquist);
        const float ScopedQ = std::clamp(InQ, 0.1f, 8.0f);
        const float ScopedW0 = kTwoPi * ScopedHz / ScopedSampleRate;
        const float ScopedCos = std::cos(ScopedW0);
        const float ScopedAlpha = std::sin(ScopedW0) / (2.0f * ScopedQ);
        const float ScopedA0 = 1.0f + ScopedAlpha;
        InFilter.B0 = (1.0f - ScopedCos) * 0.5f / ScopedA0;
        InFilter.B1 = (1.0f - ScopedCos) / ScopedA0;
        InFilter.B2 = InFilter.B0;
        InFilter.A1 = -2.0f * ScopedCos / ScopedA0;
        InFilter.A2 = (1.0f - ScopedAlpha) / ScopedA0;
    };
    auto ScopedSetHighPass = [ScopedSampleRate, ScopedNyquist](BiquadState& InFilter, float InHz, float InQ) noexcept
    {
        const float ScopedHz = std::clamp(InHz, 20.0f, ScopedNyquist);
        const float ScopedQ = std::clamp(InQ, 0.1f, 8.0f);
        const float ScopedW0 = kTwoPi * ScopedHz / ScopedSampleRate;
        const float ScopedCos = std::cos(ScopedW0);
        const float ScopedAlpha = std::sin(ScopedW0) / (2.0f * ScopedQ);
        const float ScopedA0 = 1.0f + ScopedAlpha;
        InFilter.B0 = (1.0f + ScopedCos) * 0.5f / ScopedA0;
        InFilter.B1 = -(1.0f + ScopedCos) / ScopedA0;
        InFilter.B2 = InFilter.B0;
        InFilter.A1 = -2.0f * ScopedCos / ScopedA0;
        InFilter.A2 = (1.0f - ScopedAlpha) / ScopedA0;
    };
    auto ScopedSetPeaking =
        [ScopedSampleRate, ScopedNyquist](BiquadState& InFilter, float InHz, float InQ, float InGainDb) noexcept
    {
        const float ScopedHz = std::clamp(InHz, 20.0f, ScopedNyquist);
        const float ScopedQ = std::clamp(InQ, 0.1f, 12.0f);
        const float ScopedW0 = kTwoPi * ScopedHz / ScopedSampleRate;
        const float ScopedCos = std::cos(ScopedW0);
        const float ScopedAlpha = std::sin(ScopedW0) / (2.0f * ScopedQ);
        const float ScopedA = std::pow(10.0f, InGainDb / 40.0f);
        const float ScopedA0 = 1.0f + ScopedAlpha / ScopedA;
        InFilter.B0 = (1.0f + ScopedAlpha * ScopedA) / ScopedA0;
        InFilter.B1 = -2.0f * ScopedCos / ScopedA0;
        InFilter.B2 = (1.0f - ScopedAlpha * ScopedA) / ScopedA0;
        InFilter.A1 = -2.0f * ScopedCos / ScopedA0;
        InFilter.A2 = (1.0f - ScopedAlpha / ScopedA) / ScopedA0;
    };

    ScopedSetBypass(ScopedRuntime.LowCut);
    ScopedSetBypass(ScopedRuntime.HighCut);
    ScopedSetBypass(ScopedRuntime.PeakA);
    ScopedSetBypass(ScopedRuntime.PeakB);
    ScopedSetBypass(ScopedRuntime.PeakC);

    ScopedRuntime.MixTarget = Clamp01(ScopedSlot.Mix);
    ScopedRuntime.MixCoefficient = SmoothingCoefficient(6.0f, MSampleRate);
    ScopedRuntime.FadeCoefficient = ScopedSlot.Mute.FadeSeconds <= 0.0f
                                        ? 1.0f
                                        : SmoothingCoefficient(ScopedSlot.Mute.FadeSeconds * 1000.0f, MSampleRate);
    ScopedRuntime.PitchRatio = std::exp2(ScopedSlot.Pitch.Semitones / 12.0f);
    ScopedRuntime.PitchWindow = std::clamp(ScopedSlot.Pitch.WindowMs * 0.001f * ScopedSampleRate, 64.0f,
                                           static_cast<float>(kVoiceEffectDelaySamples) * 0.5f - 2.0f);
    ScopedRuntime.WowDelaySamples = 0.012f * ScopedSampleRate;
    ScopedRuntime.WowDepthSamples = ScopedSlot.Underwater.WowDepth * 0.008f * ScopedSampleRate;
    if (ScopedRuntime.CrackleTimer <= 0.0f)
    {
        ScopedRuntime.CrackleTimer = 0.05f + ScopedRuntime.Rng.NextFloat() * 0.1f;
    }
    if (ScopedRuntime.SquelchTimer <= 0.0f)
    {
        ScopedRuntime.SquelchTimer = 0.1f + ScopedRuntime.Rng.NextFloat() * 0.2f;
    }

    switch (ScopedSlot.Kind)
    {
        case VoiceEffectKind::Muffle:
            ScopedSetHighPass(ScopedRuntime.LowCut, ScopedSlot.Muffle.LowCutHz, 0.707f);
            ScopedSetLowPass(ScopedRuntime.HighCut, ScopedSlot.Muffle.HighCutHz, ScopedSlot.Muffle.Resonance);
            ScopedSetLowPass(ScopedRuntime.PeakA, ScopedSlot.Muffle.HighCutHz, 0.707f);
            break;
        case VoiceEffectKind::Underwater:
            ScopedSetHighPass(ScopedRuntime.LowCut, ScopedSlot.Underwater.LowCutHz, 0.707f);
            ScopedSetLowPass(ScopedRuntime.HighCut, ScopedSlot.Underwater.HighCutHz, 0.707f);
            ScopedSetLowPass(ScopedRuntime.PeakA, ScopedSlot.Underwater.HighCutHz, 0.707f);
            ScopedSetPeaking(ScopedRuntime.PeakB, std::max(100.0f, ScopedSlot.Underwater.HighCutHz * 0.35f),
                             ScopedSlot.Underwater.Resonance, 5.0f);
            break;
        case VoiceEffectKind::Radio:
            ScopedSetHighPass(ScopedRuntime.LowCut, ScopedSlot.Radio.LowCutHz, 0.707f);
            ScopedSetLowPass(ScopedRuntime.HighCut, ScopedSlot.Radio.HighCutHz, 0.707f);
            break;
        case VoiceEffectKind::Formant:
        {
            const float ScopedRatio = std::exp2(ScopedSlot.Formant.Semitones / 12.0f);
            ScopedSetHighPass(ScopedRuntime.LowCut, 80.0f, 0.707f);
            ScopedSetLowPass(ScopedRuntime.HighCut, 10000.0f, 0.707f);
            ScopedSetPeaking(ScopedRuntime.PeakA, 600.0f * ScopedRatio, 5.0f, ScopedSlot.Formant.StrengthDb);
            ScopedSetPeaking(ScopedRuntime.PeakB, 1200.0f * ScopedRatio, 5.0f, ScopedSlot.Formant.StrengthDb);
            ScopedSetPeaking(ScopedRuntime.PeakC, 2400.0f * ScopedRatio, 5.0f, ScopedSlot.Formant.StrengthDb);
            break;
        }
        case VoiceEffectKind::Distortion:
            ScopedSetHighPass(ScopedRuntime.LowCut, ScopedSlot.Distortion.LowCutHz, 0.707f);
            ScopedSetLowPass(ScopedRuntime.HighCut, ScopedSlot.Distortion.HighCutHz, 0.707f);
            break;
        case VoiceEffectKind::Whisper:
            ScopedSetHighPass(ScopedRuntime.LowCut, ScopedSlot.Whisper.LowCutHz, 0.707f);
            ScopedSetLowPass(ScopedRuntime.HighCut, ScopedSlot.Whisper.HighCutHz, 0.707f);
            break;
        case VoiceEffectKind::None:
        case VoiceEffectKind::Mute:
        case VoiceEffectKind::Pitch:
        case VoiceEffectKind::RingModulation:
        case VoiceEffectKind::Count:
            break;
    }
}

float VoiceEffectChain::ReadDelay(const SlotRuntime& InRuntime, float InDelaySamples) const noexcept
{
    const float ScopedCapacity = static_cast<float>(kVoiceEffectDelaySamples);
    const float ScopedDelay = std::clamp(InDelaySamples, 1.0f, ScopedCapacity - 2.0f);
    float ScopedRead = static_cast<float>(InRuntime.WriteIndex) - ScopedDelay;
    while (ScopedRead < 0.0f)
    {
        ScopedRead += ScopedCapacity;
    }
    while (ScopedRead >= ScopedCapacity)
    {
        ScopedRead -= ScopedCapacity;
    }
    const auto ScopedIndex0 = static_cast<std::uint32_t>(ScopedRead);
    const std::uint32_t ScopedIndex1 = (ScopedIndex0 + 1u) % kVoiceEffectDelaySamples;
    const float ScopedFraction = ScopedRead - static_cast<float>(ScopedIndex0);
    return InRuntime.Delay[ScopedIndex0] +
           (InRuntime.Delay[ScopedIndex1] - InRuntime.Delay[ScopedIndex0]) * ScopedFraction;
}

float VoiceEffectChain::EffectiveMix(const SlotRuntime& InRuntime) const noexcept
{
    return Clamp01(InRuntime.MixCurrent);
}

void VoiceEffectChain::Process(float* InSamples, std::uint32_t InFrameCount) noexcept
{
    if (!MConfigured || InSamples == nullptr || InFrameCount == 0)
    {
        return;
    }
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < MSlotCount; ++ScopedIndex)
    {
        if (MSettings.Slots[ScopedIndex].Kind == VoiceEffectKind::None)
        {
            continue;
        }
        ProcessSlot(ScopedIndex, InSamples, InFrameCount);
    }
}

void VoiceEffectChain::ProcessSlot(std::uint32_t InIndex, float* InSamples, std::uint32_t InFrameCount) noexcept
{
    SlotRuntime& ScopedRuntime = MRuntime[InIndex];
    const VoiceEffectSlot& ScopedSlot = MSettings.Slots[InIndex];
    const float ScopedSampleRate = static_cast<float>(MSampleRate);
    const float ScopedInverseRate = 1.0f / ScopedSampleRate;
    const float ScopedCrackleDecay = std::exp(-ScopedInverseRate / 0.004f);
    const float ScopedSquelchCoefficient = SmoothingCoefficient(4.0f, MSampleRate);

    for (std::uint32_t ScopedFrame = 0; ScopedFrame < InFrameCount; ++ScopedFrame)
    {
        const float ScopedDry = InSamples[ScopedFrame];
        ScopedRuntime.Delay[ScopedRuntime.WriteIndex] = ScopedDry;
        ScopedRuntime.LastEnvelope += (std::fabs(ScopedDry) - ScopedRuntime.LastEnvelope) * 0.002f;
        float ScopedWet = ScopedDry;

        switch (ScopedSlot.Kind)
        {
            case VoiceEffectKind::Mute:
                ScopedRuntime.FadeGain += (0.0f - ScopedRuntime.FadeGain) * ScopedRuntime.FadeCoefficient;
                ScopedWet = ScopedDry * ScopedRuntime.FadeGain;
                break;
            case VoiceEffectKind::Muffle:
            {
                float ScopedSample = ScopedRuntime.LowCut.Process(ScopedDry);
                ScopedSample = ScopedRuntime.HighCut.Process(ScopedSample);
                ScopedSample = ScopedRuntime.PeakA.Process(ScopedSample);
                const float ScopedDrive = std::max(1.0f, DbToLinear(ScopedSlot.Muffle.DriveDb));
                ScopedSample = std::tanh(ScopedSample * ScopedDrive) / std::tanh(ScopedDrive);
                ScopedRuntime.TremoloPhase =
                    WrapPhase(ScopedRuntime.TremoloPhase + kTwoPi * ScopedSlot.Muffle.WobbleHz * ScopedInverseRate);
                const float ScopedTremolo =
                    1.0f - ScopedSlot.Muffle.WobbleDepth * (0.5f - 0.5f * std::cos(ScopedRuntime.TremoloPhase));
                ScopedSample *= ScopedTremolo;
                if (ScopedSlot.Muffle.BreathLevel > 0.0f)
                {
                    const float ScopedNoise = NextNoise(ScopedRuntime.Rng);
                    ScopedRuntime.StaticLowPass += (ScopedNoise - ScopedRuntime.StaticLowPass) * 0.08f;
                    ScopedSample += ScopedRuntime.StaticLowPass * ScopedSlot.Muffle.BreathLevel *
                                    (0.2f + ScopedRuntime.LastEnvelope * 4.0f);
                }
                ScopedWet = ScopedSample;
                break;
            }
            case VoiceEffectKind::Underwater:
            {
                float ScopedSample = ScopedRuntime.LowCut.Process(ScopedDry);
                ScopedSample = ScopedRuntime.HighCut.Process(ScopedSample);
                ScopedSample = ScopedRuntime.PeakA.Process(ScopedSample);
                ScopedSample = ScopedRuntime.PeakB.Process(ScopedSample);
                ScopedRuntime.WowPhase =
                    WrapPhase(ScopedRuntime.WowPhase + kTwoPi * ScopedSlot.Underwater.WowHz * ScopedInverseRate);
                const float ScopedDelay =
                    ScopedRuntime.WowDelaySamples + ScopedRuntime.WowDepthSamples * std::sin(ScopedRuntime.WowPhase);
                const float ScopedDelayed = ReadDelay(ScopedRuntime, ScopedDelay);
                ScopedSample = ScopedSample * 0.85f + ScopedDelayed * 0.35f;
                ScopedWet = ScopedSample;
                break;
            }
            case VoiceEffectKind::Radio:
            {
                float ScopedSample = ScopedRuntime.LowCut.Process(ScopedDry);
                ScopedSample = ScopedRuntime.HighCut.Process(ScopedSample);
                const float ScopedDrive = std::max(1.0f, DbToLinear(ScopedSlot.Radio.DriveDb));
                ScopedSample = std::tanh(ScopedSample * ScopedDrive) / std::tanh(ScopedDrive);
                const float ScopedNoise = NextNoise(ScopedRuntime.Rng);
                ScopedRuntime.StaticLowPass += (ScopedNoise - ScopedRuntime.StaticLowPass) * 0.2f;
                ScopedSample += ScopedRuntime.StaticLowPass * ScopedSlot.Radio.StaticLevel * 0.6f;
                ScopedRuntime.CrackleTimer -= ScopedInverseRate;
                if (ScopedRuntime.CrackleTimer <= 0.0f)
                {
                    ScopedRuntime.CrackleTimer =
                        (1.0f / std::max(ScopedSlot.Radio.CrackleRate, 0.1f)) * (0.5f + ScopedRuntime.Rng.NextFloat());
                    ScopedRuntime.CrackleEnvelope = 1.0f;
                }
                ScopedRuntime.CrackleEnvelope *= ScopedCrackleDecay;
                ScopedSample += ScopedNoise * ScopedRuntime.CrackleEnvelope * 0.6f;
                if (ScopedSlot.Radio.Squelch > 0.0f)
                {
                    ScopedRuntime.SquelchTimer -= ScopedInverseRate;
                    if (ScopedRuntime.SquelchTimer <= 0.0f)
                    {
                        ScopedRuntime.SquelchTimer = 0.04f + ScopedRuntime.Rng.NextFloat() * 0.2f;
                        ScopedRuntime.SquelchGain =
                            ScopedRuntime.Rng.NextFloat() < ScopedSlot.Radio.Squelch ? 0.0f : 1.0f;
                    }
                    ScopedRuntime.SquelchCurrent +=
                        (ScopedRuntime.SquelchGain - ScopedRuntime.SquelchCurrent) * ScopedSquelchCoefficient;
                    const float ScopedGate = ScopedSlot.Radio.MuteWhenSquelched
                                                 ? ScopedRuntime.SquelchCurrent
                                                 : 0.3f + 0.7f * ScopedRuntime.SquelchCurrent;
                    ScopedSample *= ScopedGate;
                }
                ScopedWet = ScopedSample;
                break;
            }
            case VoiceEffectKind::Pitch:
            {
                if (std::fabs(ScopedSlot.Pitch.Semitones) <= 0.01f)
                {
                    ScopedWet = ScopedDry;
                    break;
                }
                ScopedRuntime.PitchPhase += (ScopedRuntime.PitchRatio - 1.0f) / ScopedRuntime.PitchWindow;
                ScopedRuntime.PitchPhase = WrapUnit(ScopedRuntime.PitchPhase);
                const float ScopedDelayA = ScopedRuntime.PitchWindow * (1.0f - ScopedRuntime.PitchPhase);
                const float ScopedDelayB =
                    ScopedRuntime.PitchWindow * (1.0f - WrapUnit(ScopedRuntime.PitchPhase + 0.5f));
                const float ScopedGainA = 1.0f - std::fabs(2.0f * ScopedRuntime.PitchPhase - 1.0f);
                const float ScopedGainB = 1.0f - ScopedGainA;
                ScopedWet = ReadDelay(ScopedRuntime, ScopedDelayA) * ScopedGainA +
                            ReadDelay(ScopedRuntime, ScopedDelayB) * ScopedGainB;
                break;
            }
            case VoiceEffectKind::Formant:
            {
                float ScopedSample = ScopedRuntime.LowCut.Process(ScopedDry);
                ScopedSample = ScopedRuntime.HighCut.Process(ScopedSample);
                ScopedSample = ScopedRuntime.PeakA.Process(ScopedSample);
                ScopedSample = ScopedRuntime.PeakB.Process(ScopedSample);
                ScopedSample = ScopedRuntime.PeakC.Process(ScopedSample);
                ScopedWet = ScopedSample;
                break;
            }
            case VoiceEffectKind::Distortion:
            {
                float ScopedSample = ScopedRuntime.LowCut.Process(ScopedDry);
                ScopedSample = ScopedRuntime.HighCut.Process(ScopedSample);
                const float ScopedDrive = std::max(1.0f, DbToLinear(ScopedSlot.Distortion.DriveDb));
                ScopedSample = std::tanh(ScopedSample * ScopedDrive) / std::tanh(ScopedDrive);
                const float ScopedLevels = std::pow(2.0f, ScopedSlot.Distortion.BitDepth - 1.0f);
                ScopedSample = std::round(ScopedSample * ScopedLevels) / ScopedLevels;
                ScopedWet = ScopedSample;
                break;
            }
            case VoiceEffectKind::RingModulation:
            {
                ScopedRuntime.RingPhase = WrapPhase(ScopedRuntime.RingPhase +
                                                    kTwoPi * ScopedSlot.RingModulation.FrequencyHz * ScopedInverseRate);
                const float ScopedModulation = 1.0f - ScopedSlot.RingModulation.Depth +
                                               ScopedSlot.RingModulation.Depth * std::sin(ScopedRuntime.RingPhase);
                ScopedWet = ScopedDry * ScopedModulation;
                break;
            }
            case VoiceEffectKind::Whisper:
            {
                float ScopedSample = ScopedRuntime.LowCut.Process(ScopedDry);
                ScopedSample = ScopedRuntime.HighCut.Process(ScopedSample);
                const float ScopedNoise = NextNoise(ScopedRuntime.Rng);
                ScopedRuntime.StaticLowPass += (ScopedNoise - ScopedRuntime.StaticLowPass) * 0.18f;
                const float ScopedNoiseGain =
                    ScopedSlot.Whisper.NoiseLevel * std::clamp(0.25f + ScopedRuntime.LastEnvelope * 5.0f, 0.0f, 1.0f);
                ScopedWet = ScopedSample * (1.0f - ScopedNoiseGain) + ScopedRuntime.StaticLowPass * ScopedNoiseGain;
                break;
            }
            case VoiceEffectKind::None:
            case VoiceEffectKind::Count:
                break;
        }

        ScopedRuntime.MixCurrent += (ScopedRuntime.MixTarget - ScopedRuntime.MixCurrent) * ScopedRuntime.MixCoefficient;
        const float ScopedMix = EffectiveMix(ScopedRuntime);
        const float ScopedOutput = ScopedDry + (ScopedWet - ScopedDry) * ScopedMix;
        InSamples[ScopedFrame] = std::clamp(ScopedOutput, -4.0f, 4.0f);
        ScopedRuntime.WriteIndex = (ScopedRuntime.WriteIndex + 1u) % kVoiceEffectDelaySamples;
    }
}

void VoiceEffectRack::Configure(const VoiceEffectProfile& InProfile, std::uint32_t InSampleRate) noexcept
{
    MProfile = InProfile;
    MProfile.Clamp();
    MSampleRate = InSampleRate == 0 ? kVoiceEffectDefaultSampleRate : InSampleRate;
    MOutputGainCoefficient = SmoothingCoefficient(10.0f, MSampleRate);
    Reset();
    BuildEffectiveChain();
}

void VoiceEffectRack::Reset() noexcept
{
    MChain.Reset();
    MCondition = VoiceConditionOutput{};
    MUnderwaterBlend = 0.0f;
    MInterference = 0.0f;
    MOutputGain = 1.0f;
}

void VoiceEffectRack::SetConditionState(const VoiceConditionState& InState) noexcept
{
    MState = InState;
    MState.Oxygen01 = Clamp01(InState.Oxygen01);
    MState.Interference01 = Clamp01(InState.Interference01);
}

void VoiceEffectRack::Advance(float InDeltaSeconds) noexcept
{
    const float ScopedDelta = std::clamp(InDeltaSeconds, 0.0f, 0.25f);
    const VoiceConditionSettings& ScopedSettings = MProfile.Conditions;
    MCondition.Submerged = MState.Submerged;
    MCondition.Oxygen01 = MState.Oxygen01;

    if (!MProfile.EnableConditions)
    {
        MCondition.UnderwaterBlend = 0.0f;
        MCondition.Interference = 0.0f;
        MCondition.OutputGain = 1.0f;
        return;
    }

    MUnderwaterBlend = SmoothValue(MUnderwaterBlend, MState.Submerged ? 1.0f : 0.0f,
                                   ScopedSettings.UnderwaterFadeSeconds, ScopedDelta);
    MInterference =
        SmoothValue(MInterference, MState.Interference01, ScopedSettings.InterferenceFadeSeconds, ScopedDelta);
    if (!ScopedSettings.EnableUnderwater)
    {
        MUnderwaterBlend = 0.0f;
    }
    if (!ScopedSettings.EnableInterference)
    {
        MInterference = 0.0f;
    }
    MCondition.UnderwaterBlend = MUnderwaterBlend;
    MCondition.Interference = MInterference;

    UpdateUnderwaterSlot(ScopedDelta);
    UpdateRadioSlot(ScopedDelta);

    MCondition.OutputGain = 1.0f;
    if (ScopedSettings.EnableInterference && ScopedSettings.InterferenceHardMute &&
        MInterference >= ScopedSettings.InterferenceMuteThreshold)
    {
        MCondition.OutputGain = 0.0f;
    }
}

void VoiceEffectRack::BuildEffectiveChain() noexcept
{
    MEffectiveChain = MProfile.BaseChain;
    MEffectiveChain.Clamp();
    MUnderwaterSlot = kVoiceEffectInvalidSlot;
    MRadioSlot = kVoiceEffectInvalidSlot;
    MUnderwaterOwned = false;
    MRadioOwned = false;
    MUnderwaterBaseMix = 0.0f;
    MRadioBaseMix = 0.0f;
    if (!MProfile.EnableConditions)
    {
        MChain.Configure(MEffectiveChain, MSampleRate);
        MActive = MEffectiveChain.Count > 0;
        return;
    }

    if (MProfile.Conditions.EnableUnderwater)
    {
        MUnderwaterSlot = FindSlot(VoiceEffectKind::Underwater);
        if (MUnderwaterSlot == kVoiceEffectInvalidSlot && MEffectiveChain.Count < kVoiceEffectMaxSlots)
        {
            VoiceEffectSlot ScopedSlot{};
            ScopedSlot.Kind = VoiceEffectKind::Underwater;
            ScopedSlot.Mix = 0.0f;
            ScopedSlot.Underwater.LowCutHz = MProfile.Conditions.UnderwaterLowCutHz;
            ScopedSlot.Underwater.HighCutHz = MProfile.Conditions.UnderwaterHighCutHz;
            ScopedSlot.Underwater.Resonance = MProfile.Conditions.UnderwaterResonance;
            ScopedSlot.Underwater.WowHz = MProfile.Conditions.UnderwaterWowHz;
            ScopedSlot.Underwater.WowDepth = MProfile.Conditions.UnderwaterWowDepth;
            MEffectiveChain.Slots[MEffectiveChain.Count] = ScopedSlot;
            ++MEffectiveChain.Count;
            MUnderwaterSlot = MEffectiveChain.Count - 1;
            MUnderwaterOwned = true;
        }
        else if (MUnderwaterSlot != kVoiceEffectInvalidSlot)
        {
            MUnderwaterBaseMix = MEffectiveChain.Slots[MUnderwaterSlot].Mix;
        }
    }

    if (MProfile.Conditions.EnableInterference)
    {
        MRadioSlot = FindSlot(VoiceEffectKind::Radio);
        if (MRadioSlot == kVoiceEffectInvalidSlot && MEffectiveChain.Count < kVoiceEffectMaxSlots)
        {
            VoiceEffectSlot ScopedSlot{};
            ScopedSlot.Kind = VoiceEffectKind::Radio;
            ScopedSlot.Mix = 0.0f;
            ScopedSlot.Radio.StaticLevel = MProfile.Conditions.InterferenceStaticLevel;
            ScopedSlot.Radio.CrackleRate = MProfile.Conditions.InterferenceCrackleRate;
            MEffectiveChain.Slots[MEffectiveChain.Count] = ScopedSlot;
            ++MEffectiveChain.Count;
            MRadioSlot = MEffectiveChain.Count - 1;
            MRadioOwned = true;
        }
        else if (MRadioSlot != kVoiceEffectInvalidSlot)
        {
            MRadioBaseMix = MEffectiveChain.Slots[MRadioSlot].Mix;
        }
    }

    MEffectiveChain.Count = std::min(MEffectiveChain.Count, kVoiceEffectMaxSlots);
    MChain.Configure(MEffectiveChain, MSampleRate);
    MActive = MEffectiveChain.Count > 0;
}

std::uint32_t VoiceEffectRack::FindSlot(VoiceEffectKind InKind) const noexcept
{
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < MEffectiveChain.Count; ++ScopedIndex)
    {
        if (MEffectiveChain.Slots[ScopedIndex].Kind == InKind)
        {
            return ScopedIndex;
        }
    }
    return kVoiceEffectInvalidSlot;
}

void VoiceEffectRack::UpdateUnderwaterSlot(float InDeltaSeconds) noexcept
{
    (void)InDeltaSeconds;
    if (MUnderwaterSlot == kVoiceEffectInvalidSlot || MUnderwaterSlot >= MEffectiveChain.Count)
    {
        return;
    }
    VoiceEffectSlot& ScopedSlot = MEffectiveChain.Slots[MUnderwaterSlot];
    ScopedSlot.Mix = std::max(MUnderwaterBaseMix, MUnderwaterBlend);
    if (MUnderwaterOwned)
    {
        ScopedSlot.Underwater.LowCutHz = MProfile.Conditions.UnderwaterLowCutHz;
        ScopedSlot.Underwater.HighCutHz = MProfile.Conditions.UnderwaterHighCutHz;
        ScopedSlot.Underwater.Resonance = MProfile.Conditions.UnderwaterResonance;
        ScopedSlot.Underwater.WowHz = MProfile.Conditions.UnderwaterWowHz;
        ScopedSlot.Underwater.WowDepth = MProfile.Conditions.UnderwaterWowDepth;
    }
    if (!(ScopedSlot == MChain.Settings().Slots[MUnderwaterSlot]))
    {
        MChain.UpdateSlot(MUnderwaterSlot, ScopedSlot);
    }
}

void VoiceEffectRack::UpdateRadioSlot(float InDeltaSeconds) noexcept
{
    (void)InDeltaSeconds;
    if (MRadioSlot == kVoiceEffectInvalidSlot || MRadioSlot >= MEffectiveChain.Count)
    {
        return;
    }
    VoiceEffectSlot& ScopedSlot = MEffectiveChain.Slots[MRadioSlot];
    ScopedSlot.Mix = std::max(MRadioBaseMix, MInterference);
    if (MRadioOwned)
    {
        ScopedSlot.Radio.StaticLevel = MProfile.Conditions.InterferenceStaticLevel * (0.25f + 0.75f * MInterference);
        ScopedSlot.Radio.CrackleRate = MProfile.Conditions.InterferenceCrackleRate * MInterference;
        ScopedSlot.Radio.Squelch = MProfile.Conditions.InterferenceHardMute ? MInterference : 0.0f;
        ScopedSlot.Radio.MuteWhenSquelched = MProfile.Conditions.InterferenceHardMute;
    }
    if (!(ScopedSlot == MChain.Settings().Slots[MRadioSlot]))
    {
        MChain.UpdateSlot(MRadioSlot, ScopedSlot);
    }
}

void VoiceEffectRack::Process(float* InSamples, std::uint32_t InFrameCount) noexcept
{
    if (!MActive || InSamples == nullptr || InFrameCount == 0)
    {
        return;
    }
    MChain.Process(InSamples, InFrameCount);
    for (std::uint32_t ScopedFrame = 0; ScopedFrame < InFrameCount; ++ScopedFrame)
    {
        MOutputGain += (MCondition.OutputGain - MOutputGain) * MOutputGainCoefficient;
        InSamples[ScopedFrame] = std::clamp(InSamples[ScopedFrame] * MOutputGain, -4.0f, 4.0f);
    }
}
}
