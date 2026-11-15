// FemboyEngine2D - listener-side voice reactions: bubbles, splashes, moderation cues.
#pragma once

#include "Engine/Audio/VoiceChannel.hpp"
#include "Engine/Core/AssetId.hpp"
#include "Engine/Core/Types.hpp"

#include <array>
#include <cstdint>
#include <span>

namespace Fby
{

class AudioSystem;

enum class VoiceSfxKind : std::uint8_t
{
    None = 0,
    BubbleSmall,
    BubbleBig,
    BubbleCluster,
    Splash,
    ExplosionMuffled,
    RadioClick,
    Count,
};

[[nodiscard]] const char* VoiceSfxKindName(VoiceSfxKind InKind) noexcept;

struct VoiceSfxEvent
{
    VoiceSfxKind Kind = VoiceSfxKind::None;
    std::uint8_t Player = 0;
    float Gain = 0.0f;
    float Pan = 0.0f;
    Vector2D Position{};
};

// What the game knows about one decoded participant. The reactive layer never
// reads the network and never triggers anything for a remote peer: every event
// is a local presentation decision.
struct VoiceReactiveSfxPlayer
{
    bool Present = false;
    bool Talking = false;
    bool Submerged = false;
    bool LocallyMuted = false;
    Vector2D Position{};
    float Pan = 0.0f;
};

struct VoiceReactiveSfxInput
{
    std::array<VoiceReactiveSfxPlayer, kVoiceMaxPeers> Players{};
};

struct VoiceReactiveSfxSettings
{
    bool Enabled = true;
    bool Bubbles = true;
    bool Splashes = true;
    bool ModerationFeedback = true;
    float MasterGain = 0.5f;
    float BubbleGain = 0.35f;
    float SplashGain = 0.5f;
    float ModerationGain = 0.6f;
    float SpeakerCooldownSeconds = 2.5f;
    float GlobalCooldownSeconds = 0.4f;

    void Clamp() noexcept;

    [[nodiscard]] friend bool operator==(const VoiceReactiveSfxSettings&,
                                         const VoiceReactiveSfxSettings&) noexcept = default;
};

// Emits at most `Out.size()` events per update. The caller owns playback, so a
// headless server or a test can run the same layer without an audio device.
class VoiceReactiveSfx
{
public:
    void Configure(const VoiceReactiveSfxSettings& InSettings) noexcept;
    void Reset() noexcept;
    [[nodiscard]] std::uint32_t Update(float InDeltaSeconds, const VoiceReactiveSfxInput& InInput,
                                       std::span<VoiceSfxEvent> Out) noexcept;

private:
    struct PlayerState
    {
        bool WasTalking = false;
        bool WasSubmerged = false;
        bool WasMuted = false;
        float BubbleCooldown = 0.0f;
    };

    VoiceReactiveSfxSettings MSettings{};
    std::array<PlayerState, kVoiceMaxPeers> MStates{};
    float MGlobalCooldown = 0.0f;
};

// Plays one event on the Sfx bus with the event's gain and pan. Returns false
// when the voice pool is exhausted.
[[nodiscard]] bool PlayVoiceSfx(AudioSystem& InAudio, AssetId InClip, const VoiceSfxEvent& InEvent) noexcept;
}
