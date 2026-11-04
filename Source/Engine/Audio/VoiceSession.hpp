// FemboyEngine2D - voice participant state, gating, talking and mono policy.
#pragma once

#include "Engine/Audio/DspGraph.hpp"
#include "Engine/Core/Types.hpp"

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

// Listener-side spatial policy. Disabled keeps the old manual pan/volume path
// and leaves ParticipantGain equal to the authored volume.
struct VoiceSpatialSettings
{
    bool Enabled = false;
    float MinDistance = 1.0f;
    float MaxDistance = 40.0f;
    float Rolloff = 1.0f;
    float PanWidth = 1.0f;
    bool MuteBeyondMaxDistance = false;

    void Clamp() noexcept;
    [[nodiscard]] friend bool operator==(const VoiceSpatialSettings&, const VoiceSpatialSettings&) noexcept = default;
};

// Resolved listener-side placement for one source. The same helper is used for
// a decoded participant stream and for a game-owned positioned voice.
struct VoiceSpatialPosition
{
    float Gain = 1.0f;
    float Pan = 0.0f;
};

[[nodiscard]] VoiceSpatialPosition EvaluateVoiceSpatial(const VoiceSpatialSettings& InSettings, Vector2D InListener,
                                                        Vector2D InSource) noexcept;

struct VoiceParticipant
{
    bool Present = false;
    bool LocalMuted = false;
    bool Talking = false;
    bool HasSourcePosition = false;
    bool HasPositionOverride = false;
    float Volume = 1.0f;
    float Pan = 0.0f;
    float Level = 0.0f;
    float Hold = 0.0f;
    float Distance = 0.0f;
    float SpatialGain = 1.0f;
    float SpatialPan = 0.0f;
    Vector2D SourcePosition{};
    Vector2D PositionOverride{};
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

    // World-space source for distance/pan. The override is the held-head case:
    // the same decoded stream is heard from another emitter while the body's
    // original position is ignored.
    void SetParticipantPosition(std::uint8_t InPlayerId, Vector2D InPosition) noexcept;
    void SetParticipantPositionOverride(std::uint8_t InPlayerId, Vector2D InPosition) noexcept;
    void ClearParticipantPositionOverride(std::uint8_t InPlayerId) noexcept;
    void SetListenerPosition(Vector2D InPosition) noexcept;
    void SetSpatialSettings(const VoiceSpatialSettings& InSettings) noexcept;
    [[nodiscard]] const VoiceSpatialSettings& SpatialSettings() const noexcept
    {
        return MSpatial;
    }
    [[nodiscard]] Vector2D ListenerPosition() const noexcept
    {
        return MListenerPosition;
    }
    [[nodiscard]] float ParticipantDistance(std::uint8_t InPlayerId) const noexcept;
    [[nodiscard]] float ParticipantPan(std::uint8_t InPlayerId) const noexcept;

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
    void UpdateSpatial(VoiceParticipant& InParticipant) noexcept;

    DspGraph* MGraph = nullptr;
    std::array<VoiceParticipant, kMaxVoiceParticipants> MParticipants{};
    std::uint32_t MCount = 0;
    VoiceSpatialSettings MSpatial{};
    Vector2D MListenerPosition{};
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
