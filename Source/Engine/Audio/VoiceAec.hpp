// FemboyEngine2D - full-band acoustic echo canceller (partitioned block NLMS).
#pragma once

#include <array>
#include <cstdint>

namespace Fby
{

// The canceller runs at the capture rate (48 kHz by default). One block is half
// the FFT size, and the tail is split into partitions of that block, so a
// 160 ms tail costs at most 60 partitions.
inline constexpr std::uint32_t kVoiceAecFftSize = 256;
inline constexpr std::uint32_t kVoiceAecBlock = kVoiceAecFftSize / 2;
inline constexpr std::uint32_t kVoiceAecMaxPartitions = 64;
inline constexpr std::uint32_t kVoiceAecSearchWindow = 2048;
inline constexpr std::uint32_t kVoiceAecMaxDelaySamples = 5760;
// Power-of-two rings, indexed by absolute sample count.
inline constexpr std::uint32_t kVoiceAecRefRing = 8192;
inline constexpr std::uint32_t kVoiceAecMicHistory = 4096;

struct VoiceAecSettings
{
    bool Enabled = true;
    // Echo path length the adaptive filter can model (8 ms .. 160 ms).
    float TailMs = 128.0f;
    // Maximum reference delay searched at lock time (0 .. 120 ms).
    float MaxDelayMs = 120.0f;
    // NLMS step size, 0.01 .. 1.
    float StepSize = 0.25f;

    void Clamp() noexcept;
    [[nodiscard]] const char* Issue() const noexcept;

    [[nodiscard]] friend bool operator==(const VoiceAecSettings&, const VoiceAecSettings&) noexcept = default;
};

// Wideband canceller: overlap-save partitioned block frequency-domain NLMS at
// the capture rate. The caller feeds the playback reference next to the
// microphone block; the estimated echo is subtracted in place. The bulk delay
// is locked from a normalized cross-correlation, tracked slowly for clock
// drift, and the weight update is frozen while near-end speech dominates
// (double talk). The output is delayed by one block (2.67 ms at 48 kHz).
class VoiceAec
{
public:
    VoiceAec() = default;

    void Configure(const VoiceAecSettings& InSettings, std::uint32_t InSampleRate = 48000) noexcept;
    void Reset() noexcept;

    // `InReference` may be null: the canceller then stays bypassed.
    void Process(float* InMic, const float* InReference, std::uint32_t InFrameCount) noexcept;

    [[nodiscard]] bool Enabled() const noexcept
    {
        return MSettings.Enabled;
    }
    [[nodiscard]] bool DelayLocked() const noexcept
    {
        return MDelayLocked;
    }
    // Bulk delay in full-band samples.
    [[nodiscard]] std::uint32_t DelaySamples() const noexcept
    {
        return MDelay;
    }
    [[nodiscard]] bool DoubleTalk() const noexcept
    {
        return MDoubleTalk;
    }
    [[nodiscard]] float ErleDb() const noexcept
    {
        return MErleDb;
    }
    [[nodiscard]] std::uint32_t Partitions() const noexcept
    {
        return MPartitions;
    }

protected:
    void ProcessBlock() noexcept;
    void LockDelay() noexcept;
    void TrackDelay() noexcept;
    [[nodiscard]] float Correlate(std::uint32_t InWindow, std::uint32_t InLag) const noexcept;

    VoiceAecSettings MSettings{};
    std::uint32_t MSampleRate = 48000;
    std::uint32_t MPartitions = 48;
    std::uint32_t MDelay = 0;
    std::uint32_t MMaxDelay = 0;

    // Partitioned spectra, newest block at MWritePart.
    std::array<std::array<float, kVoiceAecFftSize>, kVoiceAecMaxPartitions> MWre{};
    std::array<std::array<float, kVoiceAecFftSize>, kVoiceAecMaxPartitions> MWim{};
    std::array<std::array<float, kVoiceAecFftSize>, kVoiceAecMaxPartitions> MXre{};
    std::array<std::array<float, kVoiceAecFftSize>, kVoiceAecMaxPartitions> MXim{};
    std::array<float, kVoiceAecFftSize> MPower{};
    std::uint32_t MWritePart = 0;

    // Time-domain block state.
    std::array<float, kVoiceAecBlock> MRefPrev{};
    std::array<float, kVoiceAecBlock> MRefCur{};
    std::array<float, kVoiceAecBlock> MMicBlock{};
    std::array<float, kVoiceAecBlock> MResidual{};
    std::uint32_t MBlockFill = 0;
    std::uint32_t MResidualRead = kVoiceAecBlock;

    // Scratch spectra.
    std::array<float, kVoiceAecFftSize> MRe{};
    std::array<float, kVoiceAecFftSize> MIm{};
    std::array<float, kVoiceAecFftSize> MEre{};
    std::array<float, kVoiceAecFftSize> MEim{};
    std::array<float, kVoiceAecBlock> MEchoBlock{};

    // Raw reference and microphone history for the delay search.
    std::array<float, kVoiceAecRefRing> MRefRaw{};
    std::array<float, kVoiceAecMicHistory> MMicHistory{};
    std::uint64_t MSampleCount = 0;
    std::uint64_t MBlocksSinceTrack = 0;

    float MMicPower = 0.0f;
    float MErrorPower = 0.0f;
    float MEchoPower = 0.0f;
    float MErleDb = 0.0f;
    bool MDoubleTalk = false;
    bool MDelayLocked = false;
};
}
