// FemboyEngine2D - data-oriented DSP mixer graph.
#pragma once

#include "Engine/Assets/AudioClipFormat.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <type_traits>

namespace Fby
{

// Capacities. The numbers are part of the ABI of this module: changing one
inline constexpr std::uint32_t kMaxBuses = 32;
inline constexpr std::uint32_t kMaxVoices = 128;
inline constexpr std::uint32_t kMaxCommands = 256;
// Upper bound on the frames rendered into the internal scratch buffers per
inline constexpr std::uint32_t kMaxBlockFrames = 256;

// A small integer handle, not a pointer: ids stay valid across a snapshot and
using BusId = std::uint16_t;
inline constexpr BusId kInvalidBus = 0xFFFFu;
inline constexpr BusId kMasterBus = 0;
inline constexpr BusId kSfxBus = 1;
inline constexpr BusId kMusicBus = 2;
inline constexpr BusId kFirstDynamicBus = 3;
inline constexpr BusId kInvalidVoice = 0xFFFFu;

class FloatRing;

enum class DspResult : std::uint8_t
{
    Ok = 0,
    NotInitialized,
    InvalidArgument,
    OutOfBuses,
    OutOfVoices,
    CommandQueueFull,
    NotFound,
    Cycle,
};

[[nodiscard]] constexpr const char* DspResultName(DspResult InResult) noexcept
{
    switch (InResult)
    {
        case DspResult::Ok:
            return "Ok";
        case DspResult::NotInitialized:
            return "NotInitialized";
        case DspResult::InvalidArgument:
            return "InvalidArgument";
        case DspResult::OutOfBuses:
            return "OutOfBuses";
        case DspResult::OutOfVoices:
            return "OutOfVoices";
        case DspResult::CommandQueueFull:
            return "CommandQueueFull";
        case DspResult::NotFound:
            return "NotFound";
        case DspResult::Cycle:
            return "Cycle";
    }
    return "?";
}

// Effect flags a bus carries. A flag is the cheap "does this bus have work"
inline constexpr std::uint32_t kBusFlagReverb = 1u << 0;
inline constexpr std::uint32_t kBusFlagDucking = 1u << 1;
// Set by SetBusFilter(): the bus has a low-pass stage and/or a zone return gain.
inline constexpr std::uint32_t kBusFlagFilter = 1u << 2;

// Playback flags carried inside AudioCommand::flags (the integer field keeps
inline constexpr std::uint32_t DspClipFlagLoop = 1u << 0;

// Ducking sidechain. The source bus's signal is the detector; the target bus's
struct DuckingParams
{
    BusId sourceBus = kInvalidBus;
    // Linear peak that starts ducking
    float threshold = 0.25f;
    // Reduction slope above the threshold
    float ratio = 2.0f;
    // Maximum gain reduction (0..1)
    float depth = 0.8f;
    // Detector/gain movement downwards
    float attackSeconds = 0.01f;
    // Detector/gain movement upwards
    float releaseSeconds = 0.25f;
};

// Schroeder reverb: four parallel combs into two series allpass filters. The
struct ReverbParams
{
    bool enabled = false;
    // Comb feedback, 0..1
    float roomSize = 0.7f;
    // High-frequency absorption in the comb loop
    float damping = 0.3f;
    // Wet level added to the dry bus signal
    float wet = 0.25f;
};

// Environment parameters written by AudioZoneSystem. They are float controls,
struct BusFilterParams
{
    // Gain applied to the reverb return only (0..4)
    float reverbGain = 1.0f;
    // One-pole low-pass cutoff in Hz (20..Nyquist)
    float lowPassHz = 20000.0f;
    // Time constant of the per-sample ramp (seconds)
    float smoothingSeconds = 0.25f;
};

static_assert(std::is_trivially_copyable_v<BusFilterParams>);

// One command in the SPSC ring. Trivially copyable, fixed size, no function
enum class AudioCommandType : std::uint8_t
{
    None = 0,
    CreateBus,
    DestroyBus,
    SetGain,
    SetRouting,
    SetDucking,
    SetReverb,
    SetFilter,
    PlayVoice,
    PlayStream,
    StopVoice,
    // Live gain/pan update for a playing voice (spatial audio).
    SetVoice,
};

struct AudioCommand
{
    AudioCommandType type = AudioCommandType::None;
    BusId bus = kInvalidBus;
    BusId parent = kInvalidBus;
    std::uint16_t voice = kInvalidVoice;
    std::uint16_t reserved = 0;
    // Identifies one claimed voice slot
    std::uint32_t generation = 0;
    std::uint32_t flags = 0;
    float gain = 1.0f;
    float volume = 1.0f;
    float pitch = 1.0f;
    // Stereo position in [-1, 1]; 0 leaves the clip untouched.
    float pan = 0.0f;
    // Interleaved PCM or a live stream ring, owned by the caller
    const std::byte* samples = nullptr;
    FloatRing* stream = nullptr;
    std::uint32_t frameCount = 0;
    std::uint32_t channels = 0;
    std::uint32_t sampleRate = 0;
    // Assets::PcmFormat
    std::uint32_t format = 0;
    DuckingParams ducking{};
    ReverbParams reverb{};
    BusFilterParams filter{};
};

static_assert(std::is_trivially_copyable_v<AudioCommand>, "AudioCommand must be memcpy-able into the ring");
static_assert(std::is_standard_layout_v<AudioCommand>, "AudioCommand must stay a flat POD");

// CommandQueue - single-producer / single-consumer ring.
class CommandQueue
{
public:
    static constexpr std::uint32_t kCapacity = kMaxCommands;

    CommandQueue() = default;
    CommandQueue(const CommandQueue&) = delete;
    CommandQueue& operator=(const CommandQueue&) = delete;

    // Producer side. Returns false when the ring is full; the caller drops the
    [[nodiscard]] bool Push(const AudioCommand& InCommand) noexcept;

    // Consumer side. Returns false when empty.
    [[nodiscard]] bool Pop(AudioCommand* InCommand) noexcept;

    // Only safe while neither side is running (init/shutdown/tests).
    void Clear() noexcept;

    [[nodiscard]] std::uint32_t Size() const noexcept;
    [[nodiscard]] std::uint32_t DroppedCount() const noexcept
    {
        return MDropped.load(std::memory_order_relaxed);
    }

private:
    AudioCommand MCommands[kCapacity]{};
    // Separate cache lines: the producer and the consumer write different
    alignas(64) std::atomic<std::uint32_t> MHead{0};
    // Producer index
    alignas(64) std::atomic<std::uint32_t> MTail{0};
    // Producer-side statistic
    std::atomic<std::uint32_t> MDropped{0};
};

// What a voice needs in order to play one cooked clip. `samples` points into a
struct DspClipView
{
    const std::byte* samples = nullptr;
    std::uint32_t frameCount = 0;
    std::uint32_t channels = 0;
    std::uint32_t sampleRate = 48000;
    Assets::PcmFormat format = Assets::PcmFormat::PcmF32;
    std::uint32_t flags = 0;
};

// DspGraph
class DspGraph
{
public:
    DspGraph() = default;
    ~DspGraph();

    DspGraph(const DspGraph&) = delete;
    DspGraph& operator=(const DspGraph&) = delete;

    // Creates Master/Sfx/Music, sizes the reverb delay memory for `sampleRate`
    [[nodiscard]] bool Initialize(std::uint32_t InSampleRate = 48000);
    void Shutdown() noexcept;

    [[nodiscard]] bool IsInitialized() const noexcept
    {
        return MInitialized;
    }
    [[nodiscard]] std::uint32_t SampleRate() const noexcept
    {
        return MSampleRate;
    }

    // Producer API (game / render thread). Every structural change is queued;

    // Allocates the lowest free bus slot under `parent`. `outBus` receives the
    [[nodiscard]] DspResult CreateBus(BusId InParent, float InGain, BusId* OutBus = nullptr) noexcept;
    // Detaches the bus and reparents its children to its parent. Master cannot
    [[nodiscard]] DspResult DestroyBus(BusId InBus) noexcept;

    [[nodiscard]] DspResult SetBusGain(BusId InBus, float InGain) noexcept;
    [[nodiscard]] DspResult SetBusRouting(BusId InBus, BusId InParent) noexcept;
    [[nodiscard]] DspResult SetBusMuted(BusId InBus, bool InMuted) noexcept;
    [[nodiscard]] DspResult SetBusDucking(BusId InTargetBus, const DuckingParams& InParams) noexcept;
    [[nodiscard]] DspResult SetBusReverb(BusId InBus, const ReverbParams& InParams) noexcept;
    // Zone / environment controls. Lock-free command, then per-sample smoothing
    [[nodiscard]] DspResult SetBusFilter(BusId InBus, const BusFilterParams& InParams) noexcept;
    [[nodiscard]] BusFilterParams BusFilter(BusId InBus) const noexcept;

    // Claims a voice slot and queues the playback command. On success the handle
    [[nodiscard]] DspResult PlayOneShot(const DspClipView& InClip, BusId InBus, float InVolume, float InPitch,
                                        bool InLoop, std::uint32_t* OutVoice = nullptr) noexcept;
    [[nodiscard]] DspResult PlayOneShot(const Assets::CookedAudioView& InClip, BusId InBus, float InVolume,
                                        float InPitch, bool InLoop, std::uint32_t* OutVoice = nullptr) noexcept;
    // Starts a voice fed by a live ring (capture buffer, decoded network voice).
    // Reads happen on the audio thread; a short read is silence plus a ring
    // underrun counter, never a click loop.
    [[nodiscard]] DspResult PlayStream(FloatRing* InRing, BusId InBus, float InVolume, float InPan,
                                       std::uint32_t* OutVoice = nullptr) noexcept;
    [[nodiscard]] DspResult StopVoice(std::uint32_t InVoice) noexcept;
    void StopAllVoices() noexcept;

    // Updates the gain and stereo pan of a claimed voice. The audio thread ramps
    // both across the next block, so a per-frame spatial update never clicks.
    // Returns InvalidArgument for a stale or free voice handle.
    [[nodiscard]] DspResult SetVoiceSpatial(std::uint32_t InVoice, float InVolume, float InPan) noexcept;

    // Escape hatch for callers that already built an AudioCommand (tests, the
    [[nodiscard]] bool PushCommand(const AudioCommand& InCommand) noexcept;

    // Producer-side queries. They read the atomic shadow, never the audio
    [[nodiscard]] bool IsBusActive(BusId InBus) const noexcept;
    [[nodiscard]] BusId ParentBus(BusId InBus) const noexcept;
    [[nodiscard]] float BusGain(BusId InBus) const noexcept;
    [[nodiscard]] bool BusMuted(BusId InBus) const noexcept;
    [[nodiscard]] float BusPeak(BusId InBus) const noexcept;
    [[nodiscard]] std::uint32_t QueuedCommandCount() const noexcept;
    [[nodiscard]] std::uint32_t DroppedCommandCount() const noexcept;

    // Audio-thread entry point. Renders `frameCount` stereo frames into
    void Process(float* InInterleavedStereo, std::uint32_t InFrameCount) noexcept;
    // Same render path, exposed under a name the offline tests can call.
    void ProcessOffline(float* InInterleavedStereo, std::uint32_t InFrameCount) noexcept;

private:
    // Bushadow: the producer's mirror of the parent pointers. It exists only so
    struct BusShadow
    {
        BusId parent = kInvalidBus;
        bool active = false;
    };

    // Per-bus ducking runtime. The detector envelope and the applied gain are
    struct DuckingState
    {
        BusId sourceBus = kInvalidBus;
        float threshold = 0.25f;
        float ratio = 2.0f;
        float depth = 0.8f;
        float attackCoeff = 0.01f;
        float releaseCoeff = 0.001f;
        float envelope = 0.0f;
        float gain = 1.0f;
    };

    // Schroeder reverb state for one bus. All indices point into the graph's
    struct ReverbState
    {
        bool enabled = false;
        float roomSize = 0.7f;
        float damping = 0.3f;
        float wet = 0.25f;
        std::uint32_t combOffset[4]{};
        std::uint32_t combLength[4]{};
        std::uint32_t combCursor[4]{};
        float combLowPass[4]{};
        std::uint32_t allpassOffset[2]{};
        std::uint32_t allpassLength[2]{};
        std::uint32_t allpassCursor[2]{};
    };

    // Audio-thread filter state. `current*` is advanced per sample towards
    struct FilterState
    {
        float targetReverbGain = 1.0f;
        float currentReverbGain = 1.0f;
        float targetLowPassHz = 20000.0f;
        float currentLowPassHz = 20000.0f;
        float smoothingSeconds = 0.25f;
        // One-pole low-pass memory, left/right.
        float lowPassState[2]{};
    };

    struct BusState
    {
        BusId parent = kInvalidBus;
        std::uint32_t flags = 0;
        bool active = false;
        std::uint8_t childCount = 0;
        BusId children[kMaxBuses]{};
        DuckingState ducking{};
        ReverbState reverb{};
        FilterState filter{};
    };

    struct VoiceState
    {
        const std::byte* samples = nullptr;
        FloatRing* stream = nullptr;
        double cursor = 0.0;
        double increment = 1.0;
        float volume = 1.0f;
        float pan = 0.0f;
        float targetVolume = 1.0f;
        float targetPan = 0.0f;
        BusId bus = kSfxBus;
        std::uint32_t generation = 0;
        std::uint32_t frameCount = 0;
        std::uint32_t channels = 0;
        std::uint32_t sampleRate = 48000;
        std::uint32_t format = 0;
        bool loop = false;
    };

    // Lock-free bookkeeping for one bus: the producer writes gain/mute, the
    struct BusAtomics
    {
        std::atomic<float> gain{1.0f};
        std::atomic<float> peak{0.0f};
        std::atomic<std::uint32_t> muted{0};
    };

    void DrainCommands() noexcept;
    void ApplyCommand(const AudioCommand& InCommand) noexcept;
    void ProcessBlock(float* Output, std::uint32_t InFrameCount) noexcept;
    void RenderVoices(std::uint32_t InFrameCount) noexcept;
    void ApplyReverb(BusId InBus, float* InBuffer, std::uint32_t InFrameCount) noexcept;
    void ApplyFilter(BusId InBus, float* InBuffer, std::uint32_t InFrameCount) noexcept;
    void ApplyDucking(BusId InBus, float* InBuffer, std::uint32_t InFrameCount, const bool* InRendered) noexcept;
    void ApplyBusGain(BusId InBus, float* InBuffer, std::uint32_t InFrameCount) noexcept;

    // Topology helpers. All are O(kMaxBuses) over fixed arrays: no allocation.
    void AddChild(BusId InParent, BusId InChild) noexcept;
    void RemoveChild(BusId InParent, BusId InChild) noexcept;
    void RebuildOrder() noexcept;
    void ResetBusSlot(BusId InBus) noexcept;
    void DestroyBusOnAudioThread(BusId InBus) noexcept;

    [[nodiscard]] bool IsBusInGraph(BusId InBus) const noexcept;
    [[nodiscard]] bool WouldCycle(BusId InParent, BusId InChild) const noexcept;

    // Bus/slot state.
    bool MInitialized = false;
    std::uint32_t MSampleRate = 48000;
    BusState MBuses[kMaxBuses]{};
    BusAtomics MBusAtomics[kMaxBuses]{};
    // Producer-visible mirror of BusFilterParams, so a UI can read back the
    std::atomic<float> MFilterReverbGain[kMaxBuses]{};
    std::atomic<float> MFilterLowPassHz[kMaxBuses]{};
    std::atomic<float> MFilterSmoothing[kMaxBuses]{};
    BusShadow MShadow[kMaxBuses]{};
    // Topological order, children before parents, Master last.
    BusId MOrder[kMaxBuses]{};
    std::uint32_t MOrderCount = 0;
    bool MTopologyDirty = true;

    // Voice pool. `voiceClaimed_` is set by the producer when it allocates a
    VoiceState MVoiceStates[kMaxVoices]{};
    std::atomic<bool> MVoiceClaimed[kMaxVoices]{};
    std::atomic<bool> MVoicePlaybackActive[kMaxVoices]{};
    // Producer-owned
    std::uint32_t MVoiceGeneration[kMaxVoices]{};
    std::uint32_t MNextVoiceGeneration = 1;

    // Per-block scratch. 64 KiB: 32 buses x 256 frames x 2 channels.
    float MBusOut[kMaxBuses][kMaxBlockFrames * 2]{};
    // Ducking fallback for later sources
    float MLastBlockPeak[kMaxBuses]{};
    // Mono scratch for one stream voice block; reused sequentially per voice.
    float MStreamScratch[kMaxBlockFrames]{};

    // Reverb delay memory, allocated once in Initialize().
    std::unique_ptr<float[]> MReverbMemory;
    std::uint32_t MReverbSamplesPerBus = 0;
    std::uint32_t MReverbCombLength[4]{};
    std::uint32_t MReverbAllpassLength[2]{};

    // SPSC command ring. Must stay last-ish in the object; nothing depends on
    CommandQueue MCommands{};
};
}
