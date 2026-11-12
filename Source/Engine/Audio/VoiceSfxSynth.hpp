// FemboyEngine2D - deterministic synthesis of the built-in voice reaction SFX.
#pragma once

#include "Engine/Audio/VoiceReactiveSfx.hpp"

#include <cstdint>
#include <span>

namespace Fby
{

// One second at 48 kHz covers every built-in cue.
inline constexpr std::uint32_t kVoiceSfxMaxSamples = 48000;

// Renders mono f32 samples into `Out` and returns the sample count. The recipes
// are deterministic and allocation-free, so the same call feeds the offline
// WAV tool and the in-memory editor library.
[[nodiscard]] std::uint32_t SynthesizeVoiceSfx(VoiceSfxKind InKind, std::uint32_t InSampleRate,
                                               std::span<float> Out) noexcept;
}
