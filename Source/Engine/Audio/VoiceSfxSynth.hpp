// FemboyEngine2D - deterministic synthesis of the built-in voice reaction SFX.
#pragma once

#include "Engine/Audio/VoiceReactiveSfx.hpp"

#include <cstdint>
#include <span>

namespace Fby
{

// Longest built-in cue. kVoiceSfxMaxSamples is the default 48 kHz buffer size.
inline constexpr float kVoiceSfxMaxCueSeconds = 1.0f;
inline constexpr std::uint32_t kVoiceSfxMaxSamples = 48000;

// Required sample count for `InKind` at `InSampleRate`; 0 for an unsupported
// kind or a zero rate. Callers that use a fixed 48 kHz buffer can size it with
// this and fail explicitly instead of receiving a truncated cue.
[[nodiscard]] std::uint32_t VoiceSfxSampleCount(VoiceSfxKind InKind, std::uint32_t InSampleRate) noexcept;

enum class VoiceSfxSynthesisStatus : std::uint8_t
{
    Ok = 0,
    UnsupportedKind,
    InvalidSampleRate,
    BufferTooSmall,
};

// Status-returning form: writes *OutCount only on Ok. This distinguishes the
// failure modes that the legacy sample-count return value conflates.
[[nodiscard]] VoiceSfxSynthesisStatus SynthesizeVoiceSfxChecked(VoiceSfxKind InKind, std::uint32_t InSampleRate,
                                                               std::span<float> Out,
                                                               std::uint32_t* OutCount) noexcept;

// Renders exactly Out[0, return) and leaves the rest untouched. Returns 0 on any
// failure; use the checked form when the caller must distinguish them. The
// recipes are deterministic and allocation-free, so the same call feeds the
// offline WAV tool and the in-memory library.
[[nodiscard]] std::uint32_t SynthesizeVoiceSfx(VoiceSfxKind InKind, std::uint32_t InSampleRate,
                                               std::span<float> Out) noexcept;
}
