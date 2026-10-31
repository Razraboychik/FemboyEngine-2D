// FemboyEngine2D - voice participant state, gating, talking and mono policy.
#pragma once

#include "Engine/Audio/DspGraph.hpp"

#include <array>
#include <cstdint>

namespace Fby
{

inline constexpr std::uint32_t kMaxVoiceParticipants = 8;
// Below this RMS floor a participant is considered silent.
inline constexpr float kVoiceTalkingThreshold = 0.02f;
// Talking stays true this long after the level drops, so gaps in speech do not
// flicker the indicator.
inline constexpr float kVoiceTalkingHoldSeconds = 0.3f;

struct VoiceParticipant
{
    bool Present = false;
    bool LocalMuted = false;
    bool Talking = false;
    float Volume = 1.0f;
    float Pan = 0.0f;
    float Level = 0.0f;
    float Hold = 0.0f;
    std::uint32_t VoiceSlot = kInvalidVoice;
};

class VoiceSession
{
public:
    VoiceSession() = default;

    // Binds the mix graph; without it the session still tracks state for tests
    // and for a server that never opens a playback device.
    void SetGraph(DspGraph* InGraph) noexcept
    {
        MGraph = InGraph;
    }

    [[nodiscard]] bool AddParticipant(std::uint8_t InPlayerId) noexcept;
    void RemoveParticipant(std::uint8_t InPlayerId) noexcept;
    void SetParticipantVoice(std::uint8_t InPlayerId, std::uint32_t InVoiceSlot) noexcept;
    void SetParticipantLevel(std::uint8_t InPlayerId, float InRms) noexcept;
    void SetParticipantMuted(std::uint8_t InPlayerId, bool InMuted) noexcept;
    void SetParticipantVolume(std::uint8_t InPlayerId, float InVolume) noexcept;
    void SetParticipantPan(std::uint8_t InPlayerId, float InPan) noexcept;

    void SetLocalLevel(float InRms) noexcept;
    void SetLocalMuted(bool InMuted) noexcept;
    void SetDeafened(bool InDeafened) noexcept;
    void SetPushToTalkEnabled(bool InEnabled) noexcept;
    void SetPushToTalkHeld(bool InHeld) noexcept;
    // Transmission is allowed only when the microphone is not muted and either
    // push-to-talk is disabled or its key is held. Deafen also stops output.
    [[nodiscard]] bool CanTransmit() const noexcept;
    void SetMono(bool InMono) noexcept;

    [[nodiscard]] bool Mono() const noexcept
    {
        return MMono;
    }
    [[nodiscard]] bool Deafened() const noexcept
    {
        return MDeafened;
    }
    [[nodiscard]] bool LocalMuted() const noexcept
    {
        return MLocalMuted;
    }
    [[nodiscard]] bool LocalTalking() const noexcept
    {
        return MLocalTalking;
    }
    [[nodiscard]] std::uint32_t Count() const noexcept
    {
        return MCount;
    }
    [[nodiscard]] const VoiceParticipant* Participant(std::uint8_t InPlayerId) const noexcept;
    [[nodiscard]] float ParticipantGain(std::uint8_t InPlayerId) const noexcept;

    // Applies talking hysteresis and pushes gain/pan to the bound graph voices.
    void Update(float InDeltaSeconds) noexcept;

protected:
    [[nodiscard]] bool UpdateTalking(float InLevel, float* InHold, bool InWasTalking) noexcept;

    DspGraph* MGraph = nullptr;
    std::array<VoiceParticipant, kMaxVoiceParticipants> MParticipants{};
    std::uint32_t MCount = 0;
    float MLocalLevel = 0.0f;
    float MLocalHold = 0.0f;
    bool MLocalTalking = false;
    bool MLocalMuted = false;
    bool MDeafened = false;
    bool MPttEnabled = false;
    bool MPttHeld = false;
    bool MMono = false;
};
}
