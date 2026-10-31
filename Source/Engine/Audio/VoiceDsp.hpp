// FemboyEngine2D - microphone processing chain: DC, HPF, gate, denoise, AGC, limiter.
#pragma once

#include <array>
#include <cstdint>

namespace Fby
{

enum class VoiceDenoiseTier : std::uint8_t
{
    None = 0,
    Gate,
    Spectral,
    Rnnoise,
};

[[nodiscard]] constexpr const char* VoiceDenoiseTierName(VoiceDenoiseTier InTier) noexcept
{
    switch (InTier)
    {
        case VoiceDenoiseTier::None:
            return "none";
        case VoiceDenoiseTier::Gate:
            return "gate";
        case VoiceDenoiseTier::Spectral:
            return "spectral";
        case VoiceDenoiseTier::Rnnoise:
            return "rnnoise";
    }
    return "?";
}

// 128-point analysis with 50 percent overlap; the chain is block independent
// from the caller's block size.
inline constexpr std::uint32_t kVoiceDspFrame = 128;
inline constexpr std::uint32_t kVoiceDspHop = 64;
// RNNoise works on 10 ms frames at 48 kHz (int16-scaled floats).
inline constexpr std::uint32_t kVoiceRnnoiseFrame = 480;

[[nodiscard]] bool VoiceRnnoiseAvailable() noexcept;

struct VoiceDspSettings
{
    VoiceDenoiseTier Denoise = VoiceDenoiseTier::Spectral;
    float GateThresholdDb = -45.0f;
    float GateAttackMs = 5.0f;
    float GateReleaseMs = 120.0f;
    float GateHoldMs = 60.0f;
    // 0 = no over-subtraction, 2 = aggressive.
    float DenoiseStrength = 1.0f;
    float DenoiseFloorDb = -18.0f;
    bool AgcEnabled = true;
    float AgcTargetDbfs = -18.0f;
    float AgcMaxGainDb = 24.0f;
    float AgcAttackMs = 10.0f;
    float AgcReleaseMs = 250.0f;
    float HighPassHz = 80.0f;
    float LimiterCeilingDb = -1.0f;

    void Clamp() noexcept;
    // Human-readable problem, or null when the settings are usable.
    [[nodiscard]] const char* Issue() const noexcept;

    [[nodiscard]] friend bool operator==(const VoiceDspSettings&, const VoiceDspSettings&) noexcept = default;
};

enum class VoicePresetKind : std::uint8_t
{
    Headset = 0,
    LaptopMic,
    NoisyRoom,
    LowBandwidth,
    Count,
};

[[nodiscard]] constexpr const char* VoicePresetName(VoicePresetKind InPreset) noexcept
{
    switch (InPreset)
    {
        case VoicePresetKind::Headset:
            return "headset";
        case VoicePresetKind::LaptopMic:
            return "laptop-mic";
        case VoicePresetKind::NoisyRoom:
            return "noisy-room";
        case VoicePresetKind::LowBandwidth:
            return "low-bandwidth";
        case VoicePresetKind::Count:
            break;
    }
    return "?";
}

[[nodiscard]] VoiceDspSettings VoicePresetSettings(VoicePresetKind InPreset) noexcept;

// Fixed-state chain. Configure once, then Process any number of mono f32 frames;
// the spectral tier keeps its own overlap-add history and never allocates.
class VoiceDspChain
{
public:
    VoiceDspChain() = default;
    ~VoiceDspChain();

    void Configure(const VoiceDspSettings& InSettings, std::uint32_t InSampleRate) noexcept;
    void Reset() noexcept;

    // Processes mono f32 samples in place. `InSamples` may point into the capture
    // ring scratch; the chain only touches the given range.
    void Process(float* InSamples, std::uint32_t InFrameCount) noexcept;

    [[nodiscard]] bool GateOpen() const noexcept
    {
        return MGateOpen;
    }
    [[nodiscard]] float LastLevelDbfs() const noexcept
    {
        return MLastLevelDb;
    }
    [[nodiscard]] float CurrentAgcGainDb() const noexcept
    {
        return MAgcGainDb;
    }
    // Last RNNoise voice-activity probability (0..1); 0 when the tier has not
    // produced a frame yet or the model is unavailable.
    [[nodiscard]] float RnnoiseSpeechProbability() const noexcept
    {
        return MRnnoiseProbability;
    }

protected:
    void RunSpectralHop() noexcept;
    void UpdateGate(float InSample) noexcept;
    void ResetSpectral() noexcept;

    VoiceDspSettings MSettings{};
    std::uint32_t MSampleRate = 48000;

    // DC blocker and 2nd-order high-pass state.
    float MDcX = 0.0f;
    float MDcY = 0.0f;
    float MHpB0 = 1.0f;
    float MHpB1 = 0.0f;
    float MHpB2 = 0.0f;
    float MHpA1 = 0.0f;
    float MHpA2 = 0.0f;
    float MHpX1 = 0.0f;
    float MHpX2 = 0.0f;
    float MHpY1 = 0.0f;
    float MHpY2 = 0.0f;

    // Gate envelope in linear gain.
    float MGateEnvelope = 1.0f;
    float MGateHold = 0.0f;
    bool MGateOpen = true;
    float MGateAttackCoeff = 0.5f;
    float MGateReleaseCoeff = 0.01f;
    float MGateHoldSeconds = 0.06f;

    // AGC envelope.
    float MAgcGainDb = 0.0f;
    float MAgcAttackCoeff = 0.1f;
    float MAgcReleaseCoeff = 0.01f;
    float MLimiterCeiling = 1.0f;
    float MLastLevelDb = -120.0f;

    // Spectral tier: analysis/synthesis history and the magnitude estimate.
    float MInHop[kVoiceDspHop]{};
    std::uint32_t MInHopCount = 0;
    float MOutHop[kVoiceDspHop]{};
    std::uint32_t MOutHopRead = kVoiceDspHop;
    float MAnalysis[kVoiceDspFrame]{};
    float MOverlap[kVoiceDspFrame]{};
    float MNoise[kVoiceDspFrame / 2 + 1]{};
    float MWindow[kVoiceDspFrame]{};
    float MCosTable[(kVoiceDspFrame / 2 + 1) * kVoiceDspFrame]{};
    float MSinTable[(kVoiceDspFrame / 2 + 1) * kVoiceDspFrame]{};
    std::uint32_t MNoiseFrames = 0;
    bool MTablesReady = false;

    // RNNoise tier state: 480-sample buffering around the model callback. The
    // dry frame is kept next to the wet one so DenoiseStrength can mix them.
    void* MRnnoiseState = nullptr;
    std::array<float, kVoiceRnnoiseFrame> MRnnoiseInput{};
    std::array<float, kVoiceRnnoiseFrame> MRnnoiseDry{};
    std::array<float, kVoiceRnnoiseFrame> MRnnoiseOutput{};
    std::uint32_t MRnnoiseInputCount = 0;
    std::uint32_t MRnnoiseRead = kVoiceRnnoiseFrame;
    float MRnnoiseProbability = 0.0f;
    bool MRnnoiseActive = false;
};
}
