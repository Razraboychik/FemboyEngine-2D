// FemboyEngine2D - miniaudio-backed ECS audio.
#pragma once

#include "Engine/Assets/AudioClipFormat.hpp"
#include "Engine/Audio/AudioDevices.hpp"
#include "Engine/Audio/DspGraph.hpp"
#include "Engine/Audio/Limiter.hpp"
#include "Engine/Core/AssetId.hpp"
#include "Engine/Core/Types.hpp"
#include "Engine/ECS/Components.hpp"
#include "Engine/ECS/Schedule.hpp"
#include "Engine/Core/Mirror.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace Fby
{

class FloatRing;

enum class AudioBus : std::uint8_t
{
    Master = 0,
    Sfx,
    Music,
    Count,
};

inline constexpr std::uint32_t kMaxAudioVoices = kMaxVoices;
inline constexpr std::uint32_t kMaxAudioClips = 512;
// Legacy AudioSystem::Play() contract: failure is 0xFFFFFFFF, a value that can
inline constexpr std::uint32_t kInvalidAudioVoice = 0xFFFFFFFFu;

// A mix-in authoring bus. `busId` is a runtime DspGraph bus id: the built-in
struct AudioBusComponent
{
    std::uint16_t BusID = kInvalidBus;
    std::uint16_t ParentBus = kMasterBus;
    float Volume = 1.0f;
    // Bit 0 = muted
    std::uint32_t Flags = 0;
};

// Bit in AudioBusComponent::flags.
inline constexpr std::uint32_t kAudioBusFlagMuted = 1u << 0;

// A flat audio emitter. The runtime voice handle is kept by AudioSystem, not in
struct AudioSourceComponent
{
    AssetId Clip{};
    Vector2D Position{};
    float Volume = 1.0f;
    float Pitch = 1.0f;
    std::uint16_t BusID = kSfxBus;
    bool Loop = false;
    bool Playing = false;
    bool Spatial = false;
};
static_assert(std::is_trivially_copyable_v<AudioBusComponent>);
static_assert(std::is_standard_layout_v<AudioBusComponent>);
static_assert(std::is_trivially_copyable_v<AudioSourceComponent>);
static_assert(std::is_standard_layout_v<AudioSourceComponent>);

struct AudioClip
{
    AssetId id{};
    // Interleaved f32, owned by the caller/VFS
    const float* samples = nullptr;
    std::uint32_t frameCount = 0;
    std::uint32_t channels = 2;
    std::uint32_t sampleRate = 48000;
};

struct AudioSource
{
    AssetId clip{};
    Vector2D position{};
    float volume = 1.0f;
    float pitch = 1.0f;
    float maxDistance = 4096.0f;
    AudioBus bus = AudioBus::Sfx;
    bool playing = false;
    bool loop = false;
    bool spatial = false;
    // Runtime handle, not serialized
    std::uint32_t voice = kInvalidAudioVoice;
};

struct AudioListener
{
    Vector2D position{};
    bool active = true;
};

class AudioSystem
{
public:
    AudioSystem() = default;
    ~AudioSystem();

    AudioSystem(const AudioSystem&) = delete;
    AudioSystem& operator=(const AudioSystem&) = delete;

    [[nodiscard]] bool Initialize(std::uint32_t InSampleRate = 48000);
    // Reopens the playback device on another output without touching the mixer
    // state. A negative index keeps the system default.
    [[nodiscard]] bool ReopenOutput(const AudioDeviceEnumerator& InDevices, AudioDeviceKind InKind,
                                    std::uint32_t InDeviceIndex) noexcept;
    void Shutdown();

    [[nodiscard]] bool IsValid() const noexcept
    {
        return MInitialized;
    }

    [[nodiscard]] bool RegisterClip(AssetId InId, std::span<const float> InInterleavedSamples, std::uint32_t InChannels,
                                    std::uint32_t InSampleRate);

    // Registers a validated .fpcm block (magic/version/payload checks) without
    [[nodiscard]] bool RegisterCookedClip(AssetId InId, std::span<const std::byte> InCookedBlock,
                                          std::string* InError = nullptr);

    // Decodes a whole cooked Ogg Opus block into an owned f32 stereo buffer and
    [[nodiscard]] bool RegisterOggMusic(AssetId InId, std::span<const std::byte> InCookedBlock,
                                        std::string* InError = nullptr);

    void SetBusVolume(AudioBus InBus, float InVolume);
    [[nodiscard]] float BusVolume(AudioBus InBus) const noexcept;

    // AudioZoneSystem's only graph entry point. ReverbGain and the one-pole
    [[nodiscard]] bool SetBusZoneParameters(AudioBus InBus, float InReverbGain, float InLowPassHz,
                                            float InSmoothingSeconds) noexcept;

    // Root safety switches. Muting Master is the DeafMode contract: the whole
    void SetMasterMuted(bool InMuted) noexcept;
    [[nodiscard]] bool MasterMuted() const noexcept;

    // Last stage after DspGraph. Configure once after Initialize(); the limiter
    void ConfigureLimiter(const LimiterDesc& InDesc = {}) noexcept;
    [[nodiscard]] const AudioLimiter& Limiter() const noexcept
    {
        return MLimiter;
    }

    // Starts a one-shot/looped voice. Returns kInvalidVoice when the pool or the
    [[nodiscard]] std::uint32_t Play(AssetId InId, AudioBus InBus = AudioBus::Sfx, float InVolume = 1.0f,
                                     float InPitch = 1.0f, bool InLoop = false);
    void Stop(std::uint32_t InVoice);
    void StopAll();

    // Live spatial update of a playing voice: distance attenuation and stereo
    // pan are re-applied every frame, so a moving source fades and pans.
    [[nodiscard]] bool SetVoiceSpatial(std::uint32_t InVoice, float InVolume, float InPan) noexcept;

    // Syncs AudioSource/AudioListener and AudioBusComponent/AudioSourceComponent
    void Update(Ecs::Registry& InRegistry);

    // Called only by the miniaudio callback; public because the C callback
    void Mix(float* Output, std::uint32_t InFrameCount) noexcept;

    // Mono downmix tap of the mastered output. The voice AEC reads it as the
    // playback reference; passing null detaches the tap.
    void SetReferenceRing(FloatRing* InRing) noexcept
    {
        MReferenceRing = InRing;
    }
    [[nodiscard]] FloatRing* ReferenceRing() const noexcept
    {
        return MReferenceRing;
    }

    [[nodiscard]] DspGraph& Mixer() noexcept
    {
        return MMixer;
    }
    [[nodiscard]] const DspGraph& Mixer() const noexcept
    {
        return MMixer;
    }

private:
    struct CookedClipRecord
    {
        AssetId id{};
        Assets::CookedAudioView view{};
    };

    // Stable storage for a decoded music track: the graph voice points at
    struct OwnedPcmRecord
    {
        AssetId id{};
        // Interleaved stereo
        std::vector<float> samples;
        std::uint32_t frameCount = 0;
        std::uint32_t channels = 2;
    };

    // Runtime state for AudioSourceComponent, outside the registry. `stamp`
    struct SourceRuntime
    {
        std::uint32_t entity = 0xFFFFFFFFu;
        std::uint32_t voice = kInvalidAudioVoice;
        std::uint32_t stamp = 0;
    };

    // Runtime state for AudioBusComponent so a routing change is queued once,
    struct BusRuntime
    {
        bool applied = false;
        std::uint16_t busId = kInvalidBus;
        std::uint16_t parent = kInvalidBus;
        float volume = 1.0f;
        std::uint32_t flags = 0;
        std::uint32_t stamp = 0;
    };

    [[nodiscard]] const AudioClip* FindClip(AssetId InId) const noexcept;
    [[nodiscard]] const CookedClipRecord* FindCookedClip(AssetId InId) const noexcept;
    [[nodiscard]] const OwnedPcmRecord* FindOwnedPcm(AssetId InId) const noexcept;
    [[nodiscard]] std::uint32_t StartClip(AssetId InId, BusId InBus, float InVolume, float InPitch,
                                          bool InLoop) noexcept;
    void SyncBusComponents(Ecs::Registry& InRegistry);
    void SyncSourceComponents(Ecs::Registry& InRegistry, const AudioListener* InListener);
    void ResetRuntimeState();

    bool MInitialized = false;
    // Ma_device*, hidden from the header
    void* MDevice = nullptr;
    void* MContext = nullptr;
    std::uint32_t MSampleRate = 48000;

    DspGraph MMixer{};
    AudioLimiter MLimiter{};
    FloatRing* MReferenceRing = nullptr;

    AudioClip MClips[kMaxAudioClips]{};
    std::uint32_t MClipCount = 0;

    CookedClipRecord MCookedClips[kMaxAudioClips]{};
    std::uint32_t MCookedClipCount = 0;

    OwnedPcmRecord MOwnedPcm[kMaxAudioClips]{};
    std::uint32_t MOwnedPcmCount = 0;

    SourceRuntime MSources[kMaxAudioVoices]{};
    BusRuntime MBusRuntime[kMaxBuses]{};
    std::uint32_t MUpdateStamp = 0;
};
}

// Mirror registration
FBY_REFLECT_COMPONENT(Fby::AudioBusComponent, "AudioBusComponent", FBY_FIELD(Fby::AudioBusComponent, BusID, UInt32),
                      FBY_FIELD(Fby::AudioBusComponent, ParentBus, UInt32),
                      FBY_FIELD(Fby::AudioBusComponent, Volume, Float),
                      FBY_FIELD(Fby::AudioBusComponent, Flags, UInt32))

FBY_REFLECT_COMPONENT(
    Fby::AudioSourceComponent, "AudioSourceComponent", FBY_FIELD(Fby::AudioSourceComponent, Clip, UInt64),
    FBY_FIELD(Fby::AudioSourceComponent, Position, Vector2D), FBY_FIELD(Fby::AudioSourceComponent, Volume, Float),
    FBY_FIELD(Fby::AudioSourceComponent, Pitch, Float), FBY_FIELD(Fby::AudioSourceComponent, BusID, UInt32),
    FBY_FIELD(Fby::AudioSourceComponent, Loop, Bool), FBY_FIELD(Fby::AudioSourceComponent, Playing, Bool),
    FBY_FIELD(Fby::AudioSourceComponent, Spatial, Bool))
