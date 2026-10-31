// FemboyEngine2D - hierarchical audio zones.
#pragma once

#include "Engine/Audio/Audio.hpp"
#include "Engine/Audio/DspGraph.hpp"
#include "Engine/ECS/Components.hpp"

#include <cstdint>
#include <type_traits>

namespace Fby
{

enum class AudioZoneKind : std::uint32_t
{
    None = 0,
    Cave = 1,
    Underwater = 2,
    Forest = 3,
    Indoor = 4,
    Custom = 5,
};

inline constexpr std::uint32_t kAudioZoneFlagEnabled = 1u << 0;
// Per-bus target mask. Bit 0 Master, bit 1 SFX, bit 2 Music.
inline constexpr std::uint32_t kAudioZoneBusMaster = 1u << 0;
inline constexpr std::uint32_t kAudioZoneBusSfx = 1u << 1;
inline constexpr std::uint32_t kAudioZoneBusMusic = 1u << 2;
inline constexpr std::uint32_t kAudioZoneBusAll = kAudioZoneBusMaster | kAudioZoneBusSfx | kAudioZoneBusMusic;

// The authored zone. `zoneId` is a stable gameplay id (not an EnTT id) so a
struct AudioZoneComponent
{
    Vector2D Min{-128.0f, -128.0f};
    Vector2D Max{128.0f, 128.0f};
    std::uint32_t ZoneID = 1;
    // AudioZoneKind
    std::uint32_t Kind = static_cast<std::uint32_t>(AudioZoneKind::Cave);
    std::uint32_t Priority = 0;
    std::uint32_t BusMask = kAudioZoneBusAll;
    std::uint32_t Flags = kAudioZoneFlagEnabled;
    // Reverb return gain, 0..4. 1 keeps the authored bus wet level.
    float ReverbGain = 1.0f;
    // One-pole cutoff in Hz. 20000 is effectively open.
    float LowPassHz = 20000.0f;
    // Time constant of the cross-fade when this zone becomes active.
    float FadeSeconds = 0.35f;

    [[nodiscard]] bool Enabled() const noexcept
    {
        return (Flags & kAudioZoneFlagEnabled) != 0;
    }
    [[nodiscard]] bool Contains(Vector2D InPoint) const noexcept
    {
        return InPoint.X >= Min.X && InPoint.X <= Max.X && InPoint.Y >= Min.Y && InPoint.Y <= Max.Y;
    }
    [[nodiscard]] bool TargetsBus(std::uint32_t InBit) const noexcept
    {
        return (BusMask & InBit) != 0;
    }
};

static_assert(std::is_trivially_copyable_v<AudioZoneComponent>);
static_assert(std::is_standard_layout_v<AudioZoneComponent>);

// Per-listener resolved environment. `blend` is 0 on a zone boundary and ramps
struct AudioContextComponent
{
    std::uint32_t ActiveZoneID = 0;
    std::uint32_t PreviousZoneID = 0;
    std::uint32_t ActiveKind = 0;
    std::uint32_t Flags = 0;
    float Blend = 1.0f;
    float ReverbGain = 1.0f;
    float LowPassHz = 20000.0f;
    float TargetReverbGain = 1.0f;
    float TargetLowPassHz = 20000.0f;
    float FadeSeconds = 0.35f;
    Vector2D ListenerPosition{};
    // Last values actually queued, so a steady zone does not flood the command
    float LastSentReverbGain = -1.0f;
    float LastSentLowPassHz = -1.0f;
    // Buses that received the previous zone, so leaving a Master-only cave
    std::uint32_t LastBusMask = 0;
};

static_assert(std::is_trivially_copyable_v<AudioContextComponent>);
static_assert(std::is_standard_layout_v<AudioContextComponent>);

// PostUpdate system. The caller owns the AudioSystem and must keep it alive for
class AudioZoneSystem
{
public:
    struct Stats
    {
        std::uint32_t contexts = 0;
        std::uint32_t activeZones = 0;
        std::uint32_t zoneChanges = 0;
        std::uint32_t busCommands = 0;
    };

    static constexpr std::uint32_t kMaxZones = 64;

    void Update(Ecs::Registry& InRegistry, AudioSystem& InAudio, float InDeltaSeconds) noexcept;
    void Update(Ecs::Registry& InRegistry, AudioSystem* InAudio, float InDeltaSeconds) noexcept;

    [[nodiscard]] const Stats& LastStats() const noexcept
    {
        return MStats;
    }

private:
    struct ResolvedZone
    {
        const AudioZoneComponent* zone = nullptr;
        std::uint32_t index = 0;
    };

    [[nodiscard]] ResolvedZone FindDominant(Ecs::Registry& InRegistry, Vector2D InPoint) noexcept;

    Stats MStats{};
};

// Schedule trampoline for SystemGraph/Schedule: register in Phase::PostUpdate.
inline void AudioZoneSystemUpdate(Ecs::SystemContext& InContext, void* InUser) noexcept
{
    auto* ScopedSystem = static_cast<AudioZoneSystem*>(InUser);
    if (ScopedSystem != nullptr)
    {
        ScopedSystem->Update(InContext.registry, InContext.audio, InContext.dt);
    }
}
}

FBY_REFLECT_COMPONENT(
    Fby::AudioZoneComponent, "AudioZoneComponent", FBY_FIELD(Fby::AudioZoneComponent, Min, Vector2D),
    FBY_FIELD(Fby::AudioZoneComponent, Max, Vector2D), FBY_FIELD(Fby::AudioZoneComponent, ZoneID, UInt32),
    FBY_FIELD(Fby::AudioZoneComponent, Kind, UInt32), FBY_FIELD(Fby::AudioZoneComponent, Priority, UInt32),
    FBY_FIELD(Fby::AudioZoneComponent, BusMask, UInt32), FBY_FIELD(Fby::AudioZoneComponent, Flags, UInt32),
    FBY_FIELD(Fby::AudioZoneComponent, ReverbGain, Float), FBY_FIELD(Fby::AudioZoneComponent, LowPassHz, Float),
    FBY_FIELD(Fby::AudioZoneComponent, FadeSeconds, Float))

FBY_REFLECT_COMPONENT(Fby::AudioContextComponent, "AudioContextComponent",
                      FBY_FIELD(Fby::AudioContextComponent, ActiveZoneID, UInt32),
                      FBY_FIELD(Fby::AudioContextComponent, PreviousZoneID, UInt32),
                      FBY_FIELD(Fby::AudioContextComponent, ActiveKind, UInt32),
                      FBY_FIELD(Fby::AudioContextComponent, Flags, UInt32),
                      FBY_FIELD(Fby::AudioContextComponent, Blend, Float),
                      FBY_FIELD(Fby::AudioContextComponent, ReverbGain, Float),
                      FBY_FIELD(Fby::AudioContextComponent, LowPassHz, Float),
                      FBY_FIELD(Fby::AudioContextComponent, TargetReverbGain, Float),
                      FBY_FIELD(Fby::AudioContextComponent, TargetLowPassHz, Float),
                      FBY_FIELD(Fby::AudioContextComponent, FadeSeconds, Float),
                      FBY_FIELD(Fby::AudioContextComponent, ListenerPosition, Vector2D))
