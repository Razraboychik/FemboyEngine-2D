// FemboyEngine2D - DSP mixer graph implementation.
#include "Engine/Audio/DspGraph.hpp"

#include "Engine/Audio/AudioStream.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <new>

namespace Fby
{
namespace
{
// Freeverb's classic comb/allpass lengths, expressed at 44.1 kHz. Initialize()
constexpr std::uint32_t kReverbCombBase[4] = {1116, 1188, 1277, 1356};
constexpr std::uint32_t kReverbAllpassBase[2] = {556, 441};
constexpr std::uint32_t kReverbBaseSampleRate = 44100;

// Gain clamps match the legacy AudioSystem::SetBusVolume contract (0..8) so a
constexpr float kMaxBusGain = 8.0f;

[[nodiscard]] float ClampGain(float InGain) noexcept
{
    if (InGain < 0.0f)
    {
        return 0.0f;
    }
    return InGain > kMaxBusGain ? kMaxBusGain : InGain;
}

[[nodiscard]] float Clamp01(float InValue) noexcept
{
    if (InValue < 0.0f)
    {
        return 0.0f;
    }
    return InValue > 1.0f ? 1.0f : InValue;
}

[[nodiscard]] std::uint32_t ScaledDelay(std::uint32_t InBase, std::uint32_t InSampleRate) noexcept
{
    const std::uint64_t ScopedScaled =
        (static_cast<std::uint64_t>(InBase) * static_cast<std::uint64_t>(InSampleRate)) / kReverbBaseSampleRate;
    return ScopedScaled == 0 ? 1u : static_cast<std::uint32_t>(ScopedScaled);
}

// One-pole coefficient for an attack/release time in seconds. A time of zero
[[nodiscard]] float SmoothingCoefficient(float InSeconds, std::uint32_t InSampleRate) noexcept
{
    if (InSeconds <= 0.0f || InSampleRate == 0)
    {
        return 1.0f;
    }
    return 1.0f - std::exp(-1.0f / (InSeconds * static_cast<float>(InSampleRate)));
}

// Reads one sample from a cooked PCM block. This is the only place the mixer
[[nodiscard]] float ReadPcmSample(const std::byte* InSamples, Assets::PcmFormat InFormat,
                                  std::uint64_t InSampleIndex) noexcept
{
    switch (InFormat)
    {
        case Assets::PcmFormat::PcmS16:
        {
            std::int16_t ScopedValue = 0;
            std::memcpy(&ScopedValue, InSamples + InSampleIndex * 2, sizeof(ScopedValue));
            return static_cast<float>(ScopedValue) * (1.0f / 32768.0f);
        }
        case Assets::PcmFormat::PcmS24:
        {
            const std::byte* ScopedSource = InSamples + InSampleIndex * 3;
            const auto ScopedB0 = static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(ScopedSource[0]));
            const auto ScopedB1 = static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(ScopedSource[1]));
            const auto ScopedB2 = static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(ScopedSource[2]));
            // Shift the 24-bit value into the top of an int32 and let the arithmetic
            const auto ScopedValue =
                static_cast<std::int32_t>((ScopedB0 << 8) | (ScopedB1 << 16) | (ScopedB2 << 24)) >> 8;
            return static_cast<float>(ScopedValue) * (1.0f / 8388608.0f);
        }
        case Assets::PcmFormat::PcmF32:
        {
            float ScopedValue = 0.0f;
            std::memcpy(&ScopedValue, InSamples + InSampleIndex * 4, sizeof(ScopedValue));
            return ScopedValue;
        }
        case Assets::PcmFormat::OggOpus:
        case Assets::PcmFormat::OggVorbis:
            // Container payloads are decoded before they become a voice
            return 0.0f;
    }
    return 0.0f;
}
}

// CommandQueue
static_assert((kMaxCommands & (kMaxCommands - 1)) == 0, "the command ring mask needs a power-of-two capacity");

bool CommandQueue::Push(const AudioCommand& InCommand) noexcept
{
    const std::uint32_t ScopedTail = MTail.load(std::memory_order_relaxed);
    // The producer only needs to see the consumer's progress, hence acquire.
    const std::uint32_t ScopedHead = MHead.load(std::memory_order_acquire);
    if (ScopedTail - ScopedHead >= kCapacity)
    {
        MDropped.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    MCommands[ScopedTail & (kCapacity - 1)] = InCommand;
    MTail.store(ScopedTail + 1, std::memory_order_release);
    return true;
}

bool CommandQueue::Pop(AudioCommand* InCommand) noexcept
{
    const std::uint32_t ScopedHead = MHead.load(std::memory_order_relaxed);
    const std::uint32_t ScopedTail = MTail.load(std::memory_order_acquire);
    if (ScopedHead == ScopedTail)
    {
        return false;
    }
    *InCommand = MCommands[ScopedHead & (kCapacity - 1)];
    MHead.store(ScopedHead + 1, std::memory_order_release);
    return true;
}

void CommandQueue::Clear() noexcept
{
    MHead.store(0, std::memory_order_relaxed);
    MTail.store(0, std::memory_order_relaxed);
    MDropped.store(0, std::memory_order_relaxed);
}

std::uint32_t CommandQueue::Size() const noexcept
{
    const std::uint32_t ScopedTail = MTail.load(std::memory_order_acquire);
    const std::uint32_t ScopedHead = MHead.load(std::memory_order_acquire);
    return ScopedTail - ScopedHead;
}

// Lifetime
DspGraph::~DspGraph()
{
    Shutdown();
}

bool DspGraph::Initialize(std::uint32_t InSampleRate)
{
    Shutdown();
    if (InSampleRate < Assets::kAudioClipMinSampleRate || InSampleRate > Assets::kAudioClipMaxSampleRate)
    {
        return false;
    }
    MSampleRate = InSampleRate;

    // Reverb memory is sized here, once. Four combs + two allpasses per bus,
    std::uint32_t ScopedSamplesPerBus = 0;
    for (std::uint32_t ScopedComb = 0; ScopedComb < 4; ++ScopedComb)
    {
        MReverbCombLength[ScopedComb] = ScaledDelay(kReverbCombBase[ScopedComb], InSampleRate);
        ScopedSamplesPerBus += MReverbCombLength[ScopedComb];
    }
    for (std::uint32_t ScopedAllpass = 0; ScopedAllpass < 2; ++ScopedAllpass)
    {
        MReverbAllpassLength[ScopedAllpass] = ScaledDelay(kReverbAllpassBase[ScopedAllpass], InSampleRate);
        ScopedSamplesPerBus += MReverbAllpassLength[ScopedAllpass];
    }
    MReverbSamplesPerBus = ScopedSamplesPerBus;
    const std::size_t ScopedMemoryFloats = static_cast<std::size_t>(kMaxBuses) * ScopedSamplesPerBus;
    MReverbMemory.reset(new (std::nothrow) float[ScopedMemoryFloats]);
    if (MReverbMemory == nullptr)
    {
        return false;
    }

    // Reset every slot, then create the three built-in buses. This is the only
    for (std::uint32_t ScopedBus = 0; ScopedBus < kMaxBuses; ++ScopedBus)
    {
        ResetBusSlot(static_cast<BusId>(ScopedBus));
        MShadow[ScopedBus] = BusShadow{};
        MLastBlockPeak[ScopedBus] = 0.0f;
        // The reverb indices are a property of the slot and the sample rate,
        ReverbState& ScopedReverb = MBuses[ScopedBus].reverb;
        std::uint32_t ScopedOffset = ScopedBus * ScopedSamplesPerBus;
        for (std::uint32_t ScopedComb = 0; ScopedComb < 4; ++ScopedComb)
        {
            ScopedReverb.combOffset[ScopedComb] = ScopedOffset;
            ScopedReverb.combLength[ScopedComb] = MReverbCombLength[ScopedComb];
            ScopedOffset += MReverbCombLength[ScopedComb];
        }
        for (std::uint32_t ScopedAllpass = 0; ScopedAllpass < 2; ++ScopedAllpass)
        {
            ScopedReverb.allpassOffset[ScopedAllpass] = ScopedOffset;
            ScopedReverb.allpassLength[ScopedAllpass] = MReverbAllpassLength[ScopedAllpass];
            ScopedOffset += MReverbAllpassLength[ScopedAllpass];
        }
    }

    const auto ScopedActivate = [this](BusId InBus, BusId InParent, float InGain)
    {
        MBuses[InBus].active = true;
        MBuses[InBus].parent = InParent;
        MBusAtomics[InBus].gain.store(InGain, std::memory_order_relaxed);
        MBusAtomics[InBus].peak.store(0.0f, std::memory_order_relaxed);
        MBusAtomics[InBus].muted.store(0, std::memory_order_relaxed);
        MShadow[InBus].active = true;
        MShadow[InBus].parent = InParent;
        if (InParent != kInvalidBus)
        {
            AddChild(InParent, InBus);
        }
    };
    ScopedActivate(kMasterBus, kInvalidBus, 1.0f);
    ScopedActivate(kSfxBus, kMasterBus, 1.0f);
    ScopedActivate(kMusicBus, kMasterBus, 1.0f);

    MCommands.Clear();
    for (std::uint32_t ScopedVoice = 0; ScopedVoice < kMaxVoices; ++ScopedVoice)
    {
        MVoiceStates[ScopedVoice] = VoiceState{};
        MVoiceGeneration[ScopedVoice] = 0;
        MVoiceClaimed[ScopedVoice].store(false, std::memory_order_relaxed);
        MVoicePlaybackActive[ScopedVoice].store(false, std::memory_order_relaxed);
    }
    MNextVoiceGeneration = 1;

    MTopologyDirty = true;
    RebuildOrder();
    MInitialized = true;
    return true;
}

void DspGraph::Shutdown() noexcept
{
    MInitialized = false;
    for (std::uint32_t ScopedVoice = 0; ScopedVoice < kMaxVoices; ++ScopedVoice)
    {
        MVoiceClaimed[ScopedVoice].store(false, std::memory_order_relaxed);
        MVoicePlaybackActive[ScopedVoice].store(false, std::memory_order_relaxed);
        MVoiceStates[ScopedVoice] = VoiceState{};
    }
    for (std::uint32_t ScopedBus = 0; ScopedBus < kMaxBuses; ++ScopedBus)
    {
        MBuses[ScopedBus] = BusState{};
        MShadow[ScopedBus] = BusShadow{};
        MBusAtomics[ScopedBus].gain.store(1.0f, std::memory_order_relaxed);
        MBusAtomics[ScopedBus].peak.store(0.0f, std::memory_order_relaxed);
        MBusAtomics[ScopedBus].muted.store(0, std::memory_order_relaxed);
    }
    MOrderCount = 0;
    MTopologyDirty = true;
    MCommands.Clear();
    MReverbMemory.reset();
    MReverbSamplesPerBus = 0;
}

// Producer API
DspResult DspGraph::CreateBus(BusId InParent, float InGain, BusId* OutBus) noexcept
{
    if (!MInitialized)
    {
        return DspResult::NotInitialized;
    }
    if (InParent >= kMaxBuses || !MShadow[InParent].active)
    {
        return DspResult::InvalidArgument;
    }
    BusId ScopedSlot = kInvalidBus;
    for (BusId ScopedCandidate = 0; ScopedCandidate < static_cast<BusId>(kMaxBuses); ++ScopedCandidate)
    {
        if (!MShadow[ScopedCandidate].active)
        {
            ScopedSlot = ScopedCandidate;
            break;
        }
    }
    if (ScopedSlot == kInvalidBus)
    {
        return DspResult::OutOfBuses;
    }

    AudioCommand ScopedCommand{};
    ScopedCommand.type = AudioCommandType::CreateBus;
    ScopedCommand.bus = ScopedSlot;
    ScopedCommand.parent = InParent;
    ScopedCommand.gain = ClampGain(InGain);
    if (!MCommands.Push(ScopedCommand))
    {
        return DspResult::CommandQueueFull;
    }
    // Commit the shadow only after the command is in the ring: a dropped
    MShadow[ScopedSlot].active = true;
    MShadow[ScopedSlot].parent = InParent;
    if (OutBus != nullptr)
    {
        *OutBus = ScopedSlot;
    }
    return DspResult::Ok;
}

DspResult DspGraph::DestroyBus(BusId InBus) noexcept
{
    if (!MInitialized)
    {
        return DspResult::NotInitialized;
    }
    if (InBus >= kMaxBuses || InBus == kMasterBus || !MShadow[InBus].active)
    {
        return DspResult::InvalidArgument;
    }
    AudioCommand ScopedCommand{};
    ScopedCommand.type = AudioCommandType::DestroyBus;
    ScopedCommand.bus = InBus;
    if (!MCommands.Push(ScopedCommand))
    {
        return DspResult::CommandQueueFull;
    }
    const BusId ScopedParent = MShadow[InBus].parent;
    MShadow[InBus].active = false;
    MShadow[InBus].parent = kInvalidBus;
    // Mirror the audio thread's reparenting so cycle checks against the shadow
    for (BusId ScopedOther = 0; ScopedOther < static_cast<BusId>(kMaxBuses); ++ScopedOther)
    {
        if (MShadow[ScopedOther].active && MShadow[ScopedOther].parent == InBus)
        {
            MShadow[ScopedOther].parent = ScopedParent;
        }
    }
    return DspResult::Ok;
}

DspResult DspGraph::SetBusGain(BusId InBus, float InGain) noexcept
{
    if (!MInitialized)
    {
        return DspResult::NotInitialized;
    }
    if (InBus >= kMaxBuses || !MShadow[InBus].active)
    {
        return DspResult::InvalidArgument;
    }
    // Direct atomic write: a volume slider must not be one buffer late, and a
    MBusAtomics[InBus].gain.store(ClampGain(InGain), std::memory_order_relaxed);
    return DspResult::Ok;
}

DspResult DspGraph::SetBusRouting(BusId InBus, BusId InParent) noexcept
{
    if (!MInitialized)
    {
        return DspResult::NotInitialized;
    }
    if (InBus >= kMaxBuses || InBus == kMasterBus || !MShadow[InBus].active)
    {
        return DspResult::InvalidArgument;
    }
    if (InParent != kInvalidBus && (InParent >= kMaxBuses || !MShadow[InParent].active))
    {
        return DspResult::InvalidArgument;
    }
    if (WouldCycle(InParent, InBus))
    {
        return DspResult::Cycle;
    }

    AudioCommand ScopedCommand{};
    ScopedCommand.type = AudioCommandType::SetRouting;
    ScopedCommand.bus = InBus;
    ScopedCommand.parent = InParent;
    if (!MCommands.Push(ScopedCommand))
    {
        return DspResult::CommandQueueFull;
    }
    MShadow[InBus].parent = InParent;
    return DspResult::Ok;
}

DspResult DspGraph::SetBusMuted(BusId InBus, bool InMuted) noexcept
{
    if (!MInitialized)
    {
        return DspResult::NotInitialized;
    }
    if (InBus >= kMaxBuses || !MShadow[InBus].active)
    {
        return DspResult::InvalidArgument;
    }
    MBusAtomics[InBus].muted.store(InMuted ? 1u : 0u, std::memory_order_relaxed);
    return DspResult::Ok;
}

DspResult DspGraph::SetBusDucking(BusId InTargetBus, const DuckingParams& InParams) noexcept
{
    if (!MInitialized)
    {
        return DspResult::NotInitialized;
    }
    if (InTargetBus >= kMaxBuses || !MShadow[InTargetBus].active)
    {
        return DspResult::InvalidArgument;
    }
    if (InParams.sourceBus != kInvalidBus)
    {
        if (InParams.sourceBus >= kMaxBuses || !MShadow[InParams.sourceBus].active)
        {
            return DspResult::InvalidArgument;
        }
        if (InParams.sourceBus == InTargetBus)
        {
            return DspResult::InvalidArgument;
        }
    }

    AudioCommand ScopedCommand{};
    ScopedCommand.type = AudioCommandType::SetDucking;
    ScopedCommand.bus = InTargetBus;
    ScopedCommand.ducking = InParams;
    if (!MCommands.Push(ScopedCommand))
    {
        return DspResult::CommandQueueFull;
    }
    return DspResult::Ok;
}

DspResult DspGraph::SetBusReverb(BusId InBus, const ReverbParams& InParams) noexcept
{
    if (!MInitialized)
    {
        return DspResult::NotInitialized;
    }
    if (InBus >= kMaxBuses || !MShadow[InBus].active)
    {
        return DspResult::InvalidArgument;
    }
    AudioCommand ScopedCommand{};
    ScopedCommand.type = AudioCommandType::SetReverb;
    ScopedCommand.bus = InBus;
    ScopedCommand.reverb = InParams;
    if (!MCommands.Push(ScopedCommand))
    {
        return DspResult::CommandQueueFull;
    }
    return DspResult::Ok;
}

DspResult DspGraph::SetBusFilter(BusId InBus, const BusFilterParams& InParams) noexcept
{
    if (!MInitialized)
    {
        return DspResult::NotInitialized;
    }
    if (InBus >= kMaxBuses || !MShadow[InBus].active)
    {
        return DspResult::InvalidArgument;
    }
    BusFilterParams ScopedParams = InParams;
    ScopedParams.reverbGain = std::clamp(InParams.reverbGain, 0.0f, 8.0f);
    const float ScopedNyquist = static_cast<float>(MSampleRate) * 0.45f;
    ScopedParams.lowPassHz = std::clamp(InParams.lowPassHz, 20.0f, ScopedNyquist);
    ScopedParams.smoothingSeconds = std::clamp(InParams.smoothingSeconds, 0.001f, 4.0f);

    MFilterReverbGain[InBus].store(ScopedParams.reverbGain, std::memory_order_relaxed);
    MFilterLowPassHz[InBus].store(ScopedParams.lowPassHz, std::memory_order_relaxed);
    MFilterSmoothing[InBus].store(ScopedParams.smoothingSeconds, std::memory_order_relaxed);

    AudioCommand ScopedCommand{};
    ScopedCommand.type = AudioCommandType::SetFilter;
    ScopedCommand.bus = InBus;
    ScopedCommand.filter = ScopedParams;
    if (!MCommands.Push(ScopedCommand))
    {
        return DspResult::CommandQueueFull;
    }
    return DspResult::Ok;
}

BusFilterParams DspGraph::BusFilter(BusId InBus) const noexcept
{
    BusFilterParams ScopedParams;
    if (InBus >= kMaxBuses)
    {
        return ScopedParams;
    }
    ScopedParams.reverbGain = MFilterReverbGain[InBus].load(std::memory_order_relaxed);
    ScopedParams.lowPassHz = MFilterLowPassHz[InBus].load(std::memory_order_relaxed);
    ScopedParams.smoothingSeconds = MFilterSmoothing[InBus].load(std::memory_order_relaxed);
    return ScopedParams;
}

DspResult DspGraph::PlayOneShot(const DspClipView& InClip, BusId InBus, float InVolume, float InPitch, bool InLoop,
                                std::uint32_t* OutVoice) noexcept
{
    if (!MInitialized)
    {
        return DspResult::NotInitialized;
    }
    if (InClip.samples == nullptr || InClip.frameCount == 0 || InClip.channels == 0 ||
        InClip.channels > Assets::kAudioClipMaxChannels || InClip.sampleRate < Assets::kAudioClipMinSampleRate ||
        InClip.sampleRate > Assets::kAudioClipMaxSampleRate || Assets::IsCompressed(InClip.format))
    {
        return DspResult::InvalidArgument;
    }
    if (InBus >= kMaxBuses || !MShadow[InBus].active)
    {
        return DspResult::InvalidArgument;
    }

    std::uint32_t ScopedSlot = kInvalidVoice;
    for (std::uint32_t ScopedCandidate = 0; ScopedCandidate < kMaxVoices; ++ScopedCandidate)
    {
        if (!MVoiceClaimed[ScopedCandidate].load(std::memory_order_acquire))
        {
            ScopedSlot = ScopedCandidate;
            break;
        }
    }
    if (ScopedSlot == kInvalidVoice)
    {
        return DspResult::OutOfVoices;
    }
    MVoiceClaimed[ScopedSlot].store(true, std::memory_order_release);

    const std::uint32_t ScopedGeneration = MNextVoiceGeneration++;
    MVoiceGeneration[ScopedSlot] = ScopedGeneration;

    AudioCommand ScopedCommand{};
    ScopedCommand.type = AudioCommandType::PlayVoice;
    ScopedCommand.voice = static_cast<std::uint16_t>(ScopedSlot);
    ScopedCommand.generation = ScopedGeneration;
    ScopedCommand.bus = InBus;
    ScopedCommand.volume = InVolume < 0.0f ? 0.0f : InVolume;
    ScopedCommand.pitch = InPitch < 0.01f ? 0.01f : (InPitch > 16.0f ? 16.0f : InPitch);
    ScopedCommand.flags = (InLoop ? DspClipFlagLoop : 0u) | InClip.flags;
    ScopedCommand.samples = InClip.samples;
    ScopedCommand.frameCount = InClip.frameCount;
    ScopedCommand.channels = InClip.channels;
    ScopedCommand.sampleRate = InClip.sampleRate;
    ScopedCommand.format = static_cast<std::uint32_t>(InClip.format);
    if (!MCommands.Push(ScopedCommand))
    {
        MVoiceClaimed[ScopedSlot].store(false, std::memory_order_release);
        return DspResult::CommandQueueFull;
    }
    if (OutVoice != nullptr)
    {
        *OutVoice = ScopedSlot;
    }
    return DspResult::Ok;
}

DspResult DspGraph::PlayOneShot(const Assets::CookedAudioView& InClip, BusId InBus, float InVolume, float InPitch,
                                bool InLoop, std::uint32_t* OutVoice) noexcept
{
    DspClipView ScopedView;
    ScopedView.samples = InClip.samples.data();
    ScopedView.frameCount = InClip.frameCount;
    ScopedView.channels = InClip.channels;
    ScopedView.sampleRate = InClip.sampleRate;
    ScopedView.format = InClip.format;
    ScopedView.flags = InClip.flags;
    return PlayOneShot(ScopedView, InBus, InVolume, InPitch, InLoop, OutVoice);
}

DspResult DspGraph::PlayStream(FloatRing* InRing, BusId InBus, float InVolume, float InPan,
                              std::uint32_t* OutVoice) noexcept
{
    if (!MInitialized)
    {
        return DspResult::NotInitialized;
    }
    if (InRing == nullptr || InRing->CapacityFrames() == 0)
    {
        return DspResult::InvalidArgument;
    }
    if (InBus >= kMaxBuses || !MShadow[InBus].active)
    {
        return DspResult::InvalidArgument;
    }

    std::uint32_t ScopedSlot = kInvalidVoice;
    for (std::uint32_t ScopedCandidate = 0; ScopedCandidate < kMaxVoices; ++ScopedCandidate)
    {
        if (!MVoiceClaimed[ScopedCandidate].load(std::memory_order_acquire))
        {
            ScopedSlot = ScopedCandidate;
            break;
        }
    }
    if (ScopedSlot == kInvalidVoice)
    {
        return DspResult::OutOfVoices;
    }
    MVoiceClaimed[ScopedSlot].store(true, std::memory_order_release);

    const std::uint32_t ScopedGeneration = MNextVoiceGeneration++;
    MVoiceGeneration[ScopedSlot] = ScopedGeneration;

    AudioCommand ScopedCommand{};
    ScopedCommand.type = AudioCommandType::PlayStream;
    ScopedCommand.voice = static_cast<std::uint16_t>(ScopedSlot);
    ScopedCommand.generation = ScopedGeneration;
    ScopedCommand.bus = InBus;
    ScopedCommand.volume = InVolume < 0.0f ? 0.0f : InVolume;
    ScopedCommand.pan = InPan < -1.0f ? -1.0f : (InPan > 1.0f ? 1.0f : InPan);
    ScopedCommand.stream = InRing;
    ScopedCommand.channels = 1;
    ScopedCommand.sampleRate = MSampleRate;
    ScopedCommand.format = static_cast<std::uint32_t>(Assets::PcmFormat::PcmF32);
    if (!MCommands.Push(ScopedCommand))
    {
        MVoiceClaimed[ScopedSlot].store(false, std::memory_order_release);
        return DspResult::CommandQueueFull;
    }
    if (OutVoice != nullptr)
    {
        *OutVoice = ScopedSlot;
    }
    return DspResult::Ok;
}

DspResult DspGraph::StopVoice(std::uint32_t InVoice) noexcept
{
    if (!MInitialized)
    {
        return DspResult::NotInitialized;
    }
    if (InVoice >= kMaxVoices)
    {
        return DspResult::InvalidArgument;
    }
    if (!MVoiceClaimed[InVoice].load(std::memory_order_acquire))
    {
        return DspResult::NotFound;
    }
    AudioCommand ScopedCommand{};
    ScopedCommand.type = AudioCommandType::StopVoice;
    ScopedCommand.voice = static_cast<std::uint16_t>(InVoice);
    ScopedCommand.generation = MVoiceGeneration[InVoice];
    if (!MCommands.Push(ScopedCommand))
    {
        return DspResult::CommandQueueFull;
    }
    return DspResult::Ok;
}

DspResult DspGraph::SetVoiceSpatial(std::uint32_t InVoice, float InVolume, float InPan) noexcept
{
    if (!MInitialized)
    {
        return DspResult::NotInitialized;
    }
    if (InVoice >= kMaxVoices)
    {
        return DspResult::InvalidArgument;
    }
    if (!MVoiceClaimed[InVoice].load(std::memory_order_acquire))
    {
        return DspResult::NotFound;
    }
    AudioCommand ScopedCommand{};
    ScopedCommand.type = AudioCommandType::SetVoice;
    ScopedCommand.voice = static_cast<std::uint16_t>(InVoice);
    ScopedCommand.generation = MVoiceGeneration[InVoice];
    ScopedCommand.volume = InVolume < 0.0f ? 0.0f : InVolume;
    ScopedCommand.pan = InPan < -1.0f ? -1.0f : (InPan > 1.0f ? 1.0f : InPan);
    if (!MCommands.Push(ScopedCommand))
    {
        return DspResult::CommandQueueFull;
    }
    return DspResult::Ok;
}

void DspGraph::StopAllVoices() noexcept
{
    for (std::uint32_t ScopedVoice = 0; ScopedVoice < kMaxVoices; ++ScopedVoice)
    {
        if (!MVoiceClaimed[ScopedVoice].load(std::memory_order_acquire))
        {
            continue;
        }
        AudioCommand ScopedCommand{};
        ScopedCommand.type = AudioCommandType::StopVoice;
        ScopedCommand.voice = static_cast<std::uint16_t>(ScopedVoice);
        ScopedCommand.generation = MVoiceGeneration[ScopedVoice];
        if (!MCommands.Push(ScopedCommand))
        {
            // A full ring is transient; remaining voices stop on the next call.
            break;
        }
    }
}

bool DspGraph::PushCommand(const AudioCommand& InCommand) noexcept
{
    if (!MInitialized)
    {
        return false;
    }
    return MCommands.Push(InCommand);
}

// Producer-side queries (atomic shadow / atomics only)
bool DspGraph::IsBusActive(BusId InBus) const noexcept
{
    return InBus < kMaxBuses && MShadow[InBus].active;
}

BusId DspGraph::ParentBus(BusId InBus) const noexcept
{
    return InBus < kMaxBuses ? MShadow[InBus].parent : kInvalidBus;
}

float DspGraph::BusGain(BusId InBus) const noexcept
{
    return InBus < kMaxBuses ? MBusAtomics[InBus].gain.load(std::memory_order_relaxed) : 0.0f;
}

bool DspGraph::BusMuted(BusId InBus) const noexcept
{
    return InBus < kMaxBuses && MBusAtomics[InBus].muted.load(std::memory_order_relaxed) != 0;
}

float DspGraph::BusPeak(BusId InBus) const noexcept
{
    return InBus < kMaxBuses ? MBusAtomics[InBus].peak.load(std::memory_order_relaxed) : 0.0f;
}

std::uint32_t DspGraph::QueuedCommandCount() const noexcept
{
    return MCommands.Size();
}

std::uint32_t DspGraph::DroppedCommandCount() const noexcept
{
    return MCommands.DroppedCount();
}

// Audio thread - command application
void DspGraph::DrainCommands() noexcept
{
    // Bounded drain: a producer that pushes faster than the callback renders
    AudioCommand ScopedCommand{};
    std::uint32_t ScopedDrained = 0;
    while (ScopedDrained < kMaxCommands && MCommands.Pop(&ScopedCommand))
    {
        ApplyCommand(ScopedCommand);
        ++ScopedDrained;
    }
}

void DspGraph::ApplyCommand(const AudioCommand& InCommand) noexcept
{
    switch (InCommand.type)
    {
        case AudioCommandType::None:
            break;
        case AudioCommandType::CreateBus:
        {
            if (InCommand.bus >= kMaxBuses)
            {
                break;
            }
            ResetBusSlot(InCommand.bus);
            MBuses[InCommand.bus].active = true;
            MBuses[InCommand.bus].parent = InCommand.parent;
            MBusAtomics[InCommand.bus].gain.store(ClampGain(InCommand.gain), std::memory_order_relaxed);
            if (InCommand.parent != kInvalidBus && InCommand.parent < kMaxBuses && MBuses[InCommand.parent].active)
            {
                AddChild(InCommand.parent, InCommand.bus);
            }
            MTopologyDirty = true;
            break;
        }
        case AudioCommandType::DestroyBus:
            DestroyBusOnAudioThread(InCommand.bus);
            break;
        case AudioCommandType::SetGain:
            if (InCommand.bus < kMaxBuses)
            {
                MBusAtomics[InCommand.bus].gain.store(ClampGain(InCommand.gain), std::memory_order_relaxed);
            }
            break;
        case AudioCommandType::SetRouting:
        {
            if (InCommand.bus >= kMaxBuses || InCommand.bus == kMasterBus || !MBuses[InCommand.bus].active)
            {
                break;
            }
            if (InCommand.parent != kInvalidBus && (InCommand.parent >= kMaxBuses || !MBuses[InCommand.parent].active))
            {
                break;
            }
            const BusId ScopedOldParent = MBuses[InCommand.bus].parent;
            if (ScopedOldParent != kInvalidBus)
            {
                RemoveChild(ScopedOldParent, InCommand.bus);
            }
            MBuses[InCommand.bus].parent = InCommand.parent;
            if (InCommand.parent != kInvalidBus)
            {
                AddChild(InCommand.parent, InCommand.bus);
            }
            MTopologyDirty = true;
            break;
        }
        case AudioCommandType::SetDucking:
        {
            if (InCommand.bus >= kMaxBuses || !MBuses[InCommand.bus].active)
            {
                break;
            }
            DuckingState& ScopedDucking = MBuses[InCommand.bus].ducking;
            ScopedDucking.sourceBus = InCommand.ducking.sourceBus;
            ScopedDucking.threshold = InCommand.ducking.threshold < 0.0f ? 0.0f : InCommand.ducking.threshold;
            ScopedDucking.ratio = InCommand.ducking.ratio < 0.0f ? 0.0f : InCommand.ducking.ratio;
            ScopedDucking.depth = Clamp01(InCommand.ducking.depth);
            ScopedDucking.attackCoeff = SmoothingCoefficient(InCommand.ducking.attackSeconds, MSampleRate);
            ScopedDucking.releaseCoeff = SmoothingCoefficient(InCommand.ducking.releaseSeconds, MSampleRate);
            if (ScopedDucking.sourceBus == kInvalidBus)
            {
                MBuses[InCommand.bus].flags &= ~kBusFlagDucking;
            }
            else
            {
                MBuses[InCommand.bus].flags |= kBusFlagDucking;
            }
            break;
        }
        case AudioCommandType::SetReverb:
        {
            if (InCommand.bus >= kMaxBuses || !MBuses[InCommand.bus].active)
            {
                break;
            }
            ReverbState& ScopedReverb = MBuses[InCommand.bus].reverb;
            ScopedReverb.roomSize = Clamp01(InCommand.reverb.roomSize) * 0.98f;
            ScopedReverb.damping = Clamp01(InCommand.reverb.damping);
            ScopedReverb.wet = Clamp01(InCommand.reverb.wet);
            ScopedReverb.enabled = InCommand.reverb.enabled;
            if (ScopedReverb.enabled)
            {
                MBuses[InCommand.bus].flags |= kBusFlagReverb;
            }
            else
            {
                MBuses[InCommand.bus].flags &= ~kBusFlagReverb;
            }
            break;
        }
        case AudioCommandType::SetFilter:
        {
            if (InCommand.bus >= kMaxBuses || !MBuses[InCommand.bus].active)
            {
                break;
            }
            FilterState& ScopedFilter = MBuses[InCommand.bus].filter;
            ScopedFilter.targetReverbGain = InCommand.filter.reverbGain;
            ScopedFilter.targetLowPassHz = InCommand.filter.lowPassHz;
            ScopedFilter.smoothingSeconds = InCommand.filter.smoothingSeconds;
            MBuses[InCommand.bus].flags |= kBusFlagFilter;
            break;
        }
        case AudioCommandType::PlayVoice:
        {
            if (InCommand.voice >= kMaxVoices)
            {
                break;
            }
            VoiceState& ScopedVoice = MVoiceStates[InCommand.voice];
            ScopedVoice.samples = InCommand.samples;
            ScopedVoice.stream = nullptr;
            ScopedVoice.cursor = 0.0;
            ScopedVoice.volume = InCommand.volume < 0.0f ? 0.0f : InCommand.volume;
            ScopedVoice.pan = InCommand.pan < -1.0f ? -1.0f : (InCommand.pan > 1.0f ? 1.0f : InCommand.pan);
            ScopedVoice.targetVolume = ScopedVoice.volume;
            ScopedVoice.targetPan = ScopedVoice.pan;
            ScopedVoice.bus = InCommand.bus;
            ScopedVoice.generation = InCommand.generation;
            ScopedVoice.frameCount = InCommand.frameCount;
            ScopedVoice.channels = InCommand.channels == 0 ? 1u : InCommand.channels;
            ScopedVoice.sampleRate = InCommand.sampleRate;
            ScopedVoice.format = InCommand.format;
            ScopedVoice.loop = (InCommand.flags & DspClipFlagLoop) != 0;
            ScopedVoice.increment = (static_cast<double>(ScopedVoice.sampleRate) / static_cast<double>(MSampleRate)) *
                                    static_cast<double>(InCommand.pitch);
            MVoicePlaybackActive[InCommand.voice].store(true, std::memory_order_release);
            break;
        }
        case AudioCommandType::PlayStream:
        {
            if (InCommand.voice >= kMaxVoices)
            {
                break;
            }
            VoiceState& ScopedVoice = MVoiceStates[InCommand.voice];
            ScopedVoice.samples = nullptr;
            ScopedVoice.stream = InCommand.stream;
            ScopedVoice.cursor = 0.0;
            ScopedVoice.volume = InCommand.volume < 0.0f ? 0.0f : InCommand.volume;
            ScopedVoice.pan = InCommand.pan < -1.0f ? -1.0f : (InCommand.pan > 1.0f ? 1.0f : InCommand.pan);
            ScopedVoice.targetVolume = ScopedVoice.volume;
            ScopedVoice.targetPan = ScopedVoice.pan;
            ScopedVoice.bus = InCommand.bus;
            ScopedVoice.generation = InCommand.generation;
            ScopedVoice.frameCount = 0;
            ScopedVoice.channels = 1;
            ScopedVoice.sampleRate = MSampleRate;
            ScopedVoice.format = static_cast<std::uint32_t>(Assets::PcmFormat::PcmF32);
            ScopedVoice.loop = false;
            ScopedVoice.increment = 1.0;
            MVoicePlaybackActive[InCommand.voice].store(true, std::memory_order_release);
            break;
        }
        case AudioCommandType::SetVoice:
        {
            if (InCommand.voice >= kMaxVoices)
            {
                break;
            }
            VoiceState& ScopedVoice = MVoiceStates[InCommand.voice];
            if (ScopedVoice.generation != InCommand.generation ||
                !MVoicePlaybackActive[InCommand.voice].load(std::memory_order_acquire))
            {
                break;
            }
            ScopedVoice.targetVolume = InCommand.volume < 0.0f ? 0.0f : InCommand.volume;
            ScopedVoice.targetPan = InCommand.pan < -1.0f ? -1.0f : (InCommand.pan > 1.0f ? 1.0f : InCommand.pan);
            break;
        }
        case AudioCommandType::StopVoice:
        {
            if (InCommand.voice >= kMaxVoices)
            {
                break;
            }
            if (MVoiceStates[InCommand.voice].generation != InCommand.generation)
            {
                // A stale stop for an already-recycled slot
                break;
            }
            MVoicePlaybackActive[InCommand.voice].store(false, std::memory_order_release);
            MVoiceClaimed[InCommand.voice].store(false, std::memory_order_release);
            break;
        }
    }
}

// Audio thread - topology
void DspGraph::AddChild(BusId InParent, BusId InChild) noexcept
{
    if (InParent >= kMaxBuses)
    {
        return;
    }
    BusState& ScopedBus = MBuses[InParent];
    if (ScopedBus.childCount >= kMaxBuses)
    {
        return;
    }
    for (std::uint8_t ScopedIndex = 0; ScopedIndex < ScopedBus.childCount; ++ScopedIndex)
    {
        if (ScopedBus.children[ScopedIndex] == InChild)
        {
            return;
        }
    }
    ScopedBus.children[ScopedBus.childCount++] = InChild;
}

void DspGraph::RemoveChild(BusId InParent, BusId InChild) noexcept
{
    if (InParent >= kMaxBuses)
    {
        return;
    }
    BusState& ScopedBus = MBuses[InParent];
    for (std::uint8_t ScopedIndex = 0; ScopedIndex < ScopedBus.childCount; ++ScopedIndex)
    {
        if (ScopedBus.children[ScopedIndex] != InChild)
        {
            continue;
        }
        // Order inside the child list is not meaningful except for determinism;
        ScopedBus.children[ScopedIndex] = ScopedBus.children[--ScopedBus.childCount];
        return;
    }
}

void DspGraph::RebuildOrder() noexcept
{
    MOrderCount = 0;
    MTopologyDirty = false;
    if (!MBuses[kMasterBus].active)
    {
        return;
    }

    // Iterative post-order DFS from Master. A tree has at most kMaxBuses nodes,
    struct Frame
    {
        BusId bus;
        std::uint8_t nextChild;
    };
    Frame ScopedStack[kMaxBuses]{};
    std::uint32_t ScopedDepth = 0;
    ScopedStack[ScopedDepth++] = Frame{kMasterBus, 0};
    while (ScopedDepth > 0)
    {
        Frame& ScopedFrame = ScopedStack[ScopedDepth - 1];
        const BusState& ScopedBus = MBuses[ScopedFrame.bus];
        if (ScopedFrame.nextChild < ScopedBus.childCount)
        {
            const BusId ScopedChild = ScopedBus.children[ScopedFrame.nextChild++];
            if (ScopedChild < kMaxBuses && MBuses[ScopedChild].active)
            {
                ScopedStack[ScopedDepth++] = Frame{ScopedChild, 0};
            }
        }
        else
        {
            MOrder[MOrderCount++] = ScopedFrame.bus;
            --ScopedDepth;
        }
    }
}

void DspGraph::ResetBusSlot(BusId InBus) noexcept
{
    MBuses[InBus] = BusState{};
    MBusAtomics[InBus].gain.store(1.0f, std::memory_order_relaxed);
    MBusAtomics[InBus].peak.store(0.0f, std::memory_order_relaxed);
    MBusAtomics[InBus].muted.store(0, std::memory_order_relaxed);
    MFilterReverbGain[InBus].store(1.0f, std::memory_order_relaxed);
    MFilterLowPassHz[InBus].store(20000.0f, std::memory_order_relaxed);
    MFilterSmoothing[InBus].store(0.25f, std::memory_order_relaxed);
    MLastBlockPeak[InBus] = 0.0f;
}

void DspGraph::DestroyBusOnAudioThread(BusId InBus) noexcept
{
    if (InBus >= kMaxBuses || InBus == kMasterBus || !MBuses[InBus].active)
    {
        return;
    }
    const BusId ScopedParent = MBuses[InBus].parent;
    if (ScopedParent != kInvalidBus)
    {
        RemoveChild(ScopedParent, InBus);
    }
    // Reparent the children to the destroyed bus's parent, matching the
    for (std::uint8_t ScopedIndex = 0; ScopedIndex < MBuses[InBus].childCount; ++ScopedIndex)
    {
        const BusId ScopedChild = MBuses[InBus].children[ScopedIndex];
        if (ScopedChild >= kMaxBuses || !MBuses[ScopedChild].active)
        {
            continue;
        }
        MBuses[ScopedChild].parent = ScopedParent;
        if (ScopedParent != kInvalidBus && MBuses[ScopedParent].active)
        {
            AddChild(ScopedParent, ScopedChild);
        }
    }
    ResetBusSlot(InBus);
    MTopologyDirty = true;
}

bool DspGraph::IsBusInGraph(BusId InBus) const noexcept
{
    return InBus < kMaxBuses && MBuses[InBus].active;
}

bool DspGraph::WouldCycle(BusId InParent, BusId InChild) const noexcept
{
    // Walk from the proposed parent to the root: if the child is on that path,
    BusId ScopedCursor = InParent;
    for (std::uint32_t ScopedGuard = 0; ScopedGuard < kMaxBuses && ScopedCursor != kInvalidBus; ++ScopedGuard)
    {
        if (ScopedCursor == InChild)
        {
            return true;
        }
        ScopedCursor = ScopedCursor < kMaxBuses ? MShadow[ScopedCursor].parent : kInvalidBus;
    }
    return false;
}

// Audio thread - rendering
void DspGraph::Process(float* InInterleavedStereo, std::uint32_t InFrameCount) noexcept
{
    if (InInterleavedStereo == nullptr)
    {
        return;
    }
    if (!MInitialized || InFrameCount == 0)
    {
        std::memset(InInterleavedStereo, 0, static_cast<std::size_t>(InFrameCount) * 2 * sizeof(float));
        return;
    }

    DrainCommands();
    if (MTopologyDirty)
    {
        RebuildOrder();
    }

    std::uint32_t ScopedOffset = 0;
    while (ScopedOffset < InFrameCount)
    {
        const std::uint32_t ScopedRemaining = InFrameCount - ScopedOffset;
        const std::uint32_t ScopedChunk = ScopedRemaining > kMaxBlockFrames ? kMaxBlockFrames : ScopedRemaining;
        ProcessBlock(InInterleavedStereo + static_cast<std::size_t>(ScopedOffset) * 2, ScopedChunk);
        ScopedOffset += ScopedChunk;
    }
}

void DspGraph::ProcessOffline(float* InInterleavedStereo, std::uint32_t InFrameCount) noexcept
{
    Process(InInterleavedStereo, InFrameCount);
}

void DspGraph::ProcessBlock(float* Output, std::uint32_t InFrameCount) noexcept
{
    const std::uint32_t ScopedFloats = InFrameCount * 2;
    std::memset(Output, 0, static_cast<std::size_t>(ScopedFloats) * sizeof(float));

    // The scratch bus buffers are the accumulation target for this block.
    for (std::uint32_t ScopedBus = 0; ScopedBus < kMaxBuses; ++ScopedBus)
    {
        std::memset(MBusOut[ScopedBus], 0, static_cast<std::size_t>(ScopedFloats) * sizeof(float));
    }
    bool ScopedRendered[kMaxBuses] = {};

    RenderVoices(InFrameCount);

    for (std::uint32_t ScopedIndex = 0; ScopedIndex < MOrderCount; ++ScopedIndex)
    {
        const BusId ScopedBus = MOrder[ScopedIndex];
        if (!MBuses[ScopedBus].active)
        {
            continue;
        }
        float* ScopedBuffer = MBusOut[ScopedBus];
        if ((MBuses[ScopedBus].flags & kBusFlagFilter) != 0)
        {
            ApplyFilter(ScopedBus, ScopedBuffer, InFrameCount);
        }
        if ((MBuses[ScopedBus].flags & kBusFlagReverb) != 0)
        {
            ApplyReverb(ScopedBus, ScopedBuffer, InFrameCount);
        }
        if ((MBuses[ScopedBus].flags & kBusFlagDucking) != 0)
        {
            ApplyDucking(ScopedBus, ScopedBuffer, InFrameCount, ScopedRendered);
        }
        ApplyBusGain(ScopedBus, ScopedBuffer, InFrameCount);

        float ScopedPeak = 0.0f;
        for (std::uint32_t ScopedSample = 0; ScopedSample < ScopedFloats; ++ScopedSample)
        {
            const float ScopedMagnitude = std::fabs(ScopedBuffer[ScopedSample]);
            if (ScopedMagnitude > ScopedPeak)
            {
                ScopedPeak = ScopedMagnitude;
            }
        }
        MBusAtomics[ScopedBus].peak.store(ScopedPeak, std::memory_order_relaxed);
        MLastBlockPeak[ScopedBus] = ScopedPeak;
        ScopedRendered[ScopedBus] = true;

        const BusId ScopedParent = MBuses[ScopedBus].parent;
        if (ScopedParent != kInvalidBus && ScopedParent < kMaxBuses && MBuses[ScopedParent].active)
        {
            float* ScopedDestination = MBusOut[ScopedParent];
            for (std::uint32_t ScopedSample = 0; ScopedSample < ScopedFloats; ++ScopedSample)
            {
                ScopedDestination[ScopedSample] += ScopedBuffer[ScopedSample];
            }
        }
        else if (ScopedBus == kMasterBus)
        {
            std::memcpy(Output, ScopedBuffer, static_cast<std::size_t>(ScopedFloats) * sizeof(float));
        }
    }
}

void DspGraph::RenderVoices(std::uint32_t InFrameCount) noexcept
{
    const float ScopedInvFrames = InFrameCount > 0 ? 1.0f / static_cast<float>(InFrameCount) : 0.0f;
    for (std::uint32_t ScopedSlot = 0; ScopedSlot < kMaxVoices; ++ScopedSlot)
    {
        if (!MVoicePlaybackActive[ScopedSlot].load(std::memory_order_acquire))
        {
            continue;
        }
        VoiceState& ScopedVoice = MVoiceStates[ScopedSlot];
        const bool ScopedStreamVoice = ScopedVoice.stream != nullptr;
        if (ScopedVoice.bus >= kMaxBuses)
        {
            MVoicePlaybackActive[ScopedSlot].store(false, std::memory_order_release);
            MVoiceClaimed[ScopedSlot].store(false, std::memory_order_release);
            continue;
        }
        if (!ScopedStreamVoice && (ScopedVoice.samples == nullptr || ScopedVoice.frameCount == 0))
        {
            MVoicePlaybackActive[ScopedSlot].store(false, std::memory_order_release);
            MVoiceClaimed[ScopedSlot].store(false, std::memory_order_release);
            continue;
        }

        const auto ScopedFormat = static_cast<Assets::PcmFormat>(ScopedVoice.format);
        const std::uint32_t ScopedChannels = ScopedVoice.channels == 0 ? 1u : ScopedVoice.channels;
        float* ScopedDestination = MBusOut[ScopedVoice.bus];
        float ScopedVolume = ScopedVoice.volume;
        float ScopedPan = ScopedVoice.pan;
        const float ScopedVolumeStep = (ScopedVoice.targetVolume - ScopedVolume) * ScopedInvFrames;
        const float ScopedPanStep = (ScopedVoice.targetPan - ScopedPan) * ScopedInvFrames;

        std::uint32_t ScopedStreamFrames = 0;
        if (ScopedStreamVoice)
        {
            ScopedStreamFrames = ScopedVoice.stream->Read(MStreamScratch, InFrameCount);
        }

        bool ScopedFinished = false;
        for (std::uint32_t ScopedFrame = 0; ScopedFrame < InFrameCount; ++ScopedFrame)
        {
            if (!ScopedStreamVoice && ScopedVoice.cursor >= static_cast<double>(ScopedVoice.frameCount))
            {
                if (ScopedVoice.loop)
                {
                    ScopedVoice.cursor = std::fmod(ScopedVoice.cursor, static_cast<double>(ScopedVoice.frameCount));
                }
                else
                {
                    ScopedFinished = true;
                    break;
                }
            }
            ScopedVolume += ScopedVolumeStep;
            ScopedPan += ScopedPanStep;
            const float ScopedLeftGain = ScopedVolume * (ScopedPan <= 0.0f ? 1.0f : 1.0f - ScopedPan);
            const float ScopedRightGain = ScopedVolume * (ScopedPan >= 0.0f ? 1.0f : 1.0f + ScopedPan);
            float ScopedLeft = 0.0f;
            float ScopedRight = 0.0f;
            if (ScopedStreamVoice)
            {
                ScopedLeft = ScopedFrame < ScopedStreamFrames ? MStreamScratch[ScopedFrame] : 0.0f;
                ScopedRight = ScopedLeft;
            }
            else
            {
                const auto ScopedSourceFrame = static_cast<std::uint64_t>(ScopedVoice.cursor);
                const std::uint64_t ScopedBase = ScopedSourceFrame * ScopedChannels;
                ScopedLeft = ReadPcmSample(ScopedVoice.samples, ScopedFormat, ScopedBase);
                ScopedRight = ReadPcmSample(ScopedVoice.samples, ScopedFormat,
                                            ScopedBase + (ScopedChannels > 1 ? 1u : 0u));
                ScopedVoice.cursor += ScopedVoice.increment;
            }
            ScopedDestination[ScopedFrame * 2 + 0] += ScopedLeft * ScopedLeftGain;
            ScopedDestination[ScopedFrame * 2 + 1] += ScopedRight * ScopedRightGain;
        }
        ScopedVoice.volume = ScopedVoice.targetVolume;
        ScopedVoice.pan = ScopedVoice.targetPan;
        if (ScopedFinished)
        {
            MVoicePlaybackActive[ScopedSlot].store(false, std::memory_order_release);
            MVoiceClaimed[ScopedSlot].store(false, std::memory_order_release);
        }
    }
}

void DspGraph::ApplyBusGain(BusId InBus, float* InBuffer, std::uint32_t InFrameCount) noexcept
{
    const bool ScopedMuted = MBusAtomics[InBus].muted.load(std::memory_order_relaxed) != 0;
    const float ScopedGain = ScopedMuted ? 0.0f : MBusAtomics[InBus].gain.load(std::memory_order_relaxed);
    if (ScopedGain == 1.0f)
    {
        return;
    }
    const std::uint32_t ScopedFloats = InFrameCount * 2;
    for (std::uint32_t ScopedSample = 0; ScopedSample < ScopedFloats; ++ScopedSample)
    {
        InBuffer[ScopedSample] *= ScopedGain;
    }
}

void DspGraph::ApplyFilter(BusId InBus, float* InBuffer, std::uint32_t InFrameCount) noexcept
{
    FilterState& ScopedFilter = MBuses[InBus].filter;
    const float ScopedNyquist = static_cast<float>(MSampleRate) * 0.45f;
    const float ScopedTarget = ScopedFilter.targetLowPassHz < ScopedNyquist ? ScopedFilter.targetLowPassHz
                                                                            : ScopedNyquist;
    const float ScopedCurrent = ScopedFilter.currentLowPassHz < ScopedNyquist ? ScopedFilter.currentLowPassHz
                                                                              : ScopedNyquist;
    const float ScopedSmoothing = ScopedFilter.smoothingSeconds > 0.0001f ? ScopedFilter.smoothingSeconds : 0.0001f;
    const float ScopedBlockAlpha =
        1.0f - std::exp(-static_cast<float>(InFrameCount) /
                        (ScopedSmoothing * static_cast<float>(MSampleRate)));
    const float ScopedEnd = ScopedCurrent + (ScopedTarget - ScopedCurrent) * ScopedBlockAlpha;
    const auto ScopedCoefficient = [this](float InCutoffHz) noexcept
    {
        if (InCutoffHz >= static_cast<float>(MSampleRate) * 0.449f)
        {
            return 1.0f;
        }
        return 1.0f - std::exp(-6.28318530718f * InCutoffHz / static_cast<float>(MSampleRate));
    };
    const float ScopedStartCoefficient = ScopedCoefficient(ScopedCurrent);
    const float ScopedEndCoefficient = ScopedCoefficient(ScopedEnd);
    if (ScopedStartCoefficient > 0.9999f && ScopedEndCoefficient > 0.9999f)
    {
        // Fully open: bypass the state update but still advance the smoothed
        ScopedFilter.currentLowPassHz = ScopedEnd;
        ScopedFilter.lowPassState[0] = InBuffer[(InFrameCount - 1u) * 2u];
        ScopedFilter.lowPassState[1] = InBuffer[(InFrameCount - 1u) * 2u + 1u];
        return;
    }
    const float ScopedInvFrames = InFrameCount > 1u ? 1.0f / static_cast<float>(InFrameCount - 1u) : 1.0f;
    for (std::uint32_t ScopedFrame = 0; ScopedFrame < InFrameCount; ++ScopedFrame)
    {
        const float ScopedT = static_cast<float>(ScopedFrame) * ScopedInvFrames;
        const float ScopedFrameCoefficient =
            ScopedStartCoefficient + (ScopedEndCoefficient - ScopedStartCoefficient) * ScopedT;
        const std::size_t ScopedPair = static_cast<std::size_t>(ScopedFrame) * 2;
        ScopedFilter.lowPassState[0] +=
            ScopedFrameCoefficient * (InBuffer[ScopedPair] - ScopedFilter.lowPassState[0]);
        ScopedFilter.lowPassState[1] +=
            ScopedFrameCoefficient * (InBuffer[ScopedPair + 1] - ScopedFilter.lowPassState[1]);
        InBuffer[ScopedPair] = ScopedFilter.lowPassState[0];
        InBuffer[ScopedPair + 1] = ScopedFilter.lowPassState[1];
    }
    ScopedFilter.currentLowPassHz = ScopedEnd;
}

void DspGraph::ApplyReverb(BusId InBus, float* InBuffer, std::uint32_t InFrameCount) noexcept
{
    ReverbState& ScopedReverb = MBuses[InBus].reverb;
    if (MReverbMemory == nullptr)
    {
        return;
    }
    FilterState& ScopedFilter = MBuses[InBus].filter;
    const float ScopedSmoothing = ScopedFilter.smoothingSeconds > 0.0001f ? ScopedFilter.smoothingSeconds : 0.0001f;
    const float ScopedBlockAlpha =
        1.0f - std::exp(-static_cast<float>(InFrameCount) /
                        (ScopedSmoothing * static_cast<float>(MSampleRate)));
    const float ScopedGainStart = ScopedFilter.currentReverbGain;
    const float ScopedGainEnd =
        ScopedFilter.currentReverbGain +
        (ScopedFilter.targetReverbGain - ScopedFilter.currentReverbGain) * ScopedBlockAlpha;
    const float ScopedInvFrames = InFrameCount > 1u ? 1.0f / static_cast<float>(InFrameCount - 1u) : 1.0f;
    float* ScopedMemory = MReverbMemory.get() + static_cast<std::size_t>(InBus) * MReverbSamplesPerBus;
    const float ScopedFeedback = 0.7f + ScopedReverb.roomSize * 0.28f;
    const float ScopedDamp1 = ScopedReverb.damping * 0.4f;
    const float ScopedDamp2 = 1.0f - ScopedDamp1;
    for (std::uint32_t ScopedFrame = 0; ScopedFrame < InFrameCount; ++ScopedFrame)
    {
        const std::size_t ScopedPair = static_cast<std::size_t>(ScopedFrame) * 2;
        const float ScopedMono = (InBuffer[ScopedPair] + InBuffer[ScopedPair + 1]) * 0.5f;

        float ScopedSum = 0.0f;
        for (std::uint32_t ScopedComb = 0; ScopedComb < 4; ++ScopedComb)
        {
            float* ScopedLine = ScopedMemory + ScopedReverb.combOffset[ScopedComb];
            const std::uint32_t ScopedCursor = ScopedReverb.combCursor[ScopedComb];
            const float ScopedDelayed = ScopedLine[ScopedCursor];
            ScopedReverb.combLowPass[ScopedComb] =
                ScopedDelayed * ScopedDamp2 + ScopedReverb.combLowPass[ScopedComb] * ScopedDamp1;
            ScopedLine[ScopedCursor] = ScopedMono + ScopedReverb.combLowPass[ScopedComb] * ScopedFeedback;
            ScopedReverb.combCursor[ScopedComb] =
                ScopedCursor + 1 >= ScopedReverb.combLength[ScopedComb] ? 0u : ScopedCursor + 1;
            ScopedSum += ScopedDelayed;
        }
        float ScopedWet = ScopedSum * 0.25f;
        for (std::uint32_t ScopedAllpass = 0; ScopedAllpass < 2; ++ScopedAllpass)
        {
            float* ScopedLine = ScopedMemory + ScopedReverb.allpassOffset[ScopedAllpass];
            const std::uint32_t ScopedCursor = ScopedReverb.allpassCursor[ScopedAllpass];
            const float ScopedDelayed = ScopedLine[ScopedCursor];
            const float ScopedOutput = ScopedDelayed - ScopedWet;
            ScopedLine[ScopedCursor] = ScopedWet + ScopedDelayed * 0.5f;
            ScopedReverb.allpassCursor[ScopedAllpass] =
                ScopedCursor + 1 >= ScopedReverb.allpassLength[ScopedAllpass] ? 0u : ScopedCursor + 1;
            ScopedWet = ScopedOutput;
        }
        const float ScopedGain =
            ScopedGainStart + (ScopedGainEnd - ScopedGainStart) * (static_cast<float>(ScopedFrame) * ScopedInvFrames);
        const float ScopedValue = ScopedWet * ScopedReverb.wet * ScopedGain;
        InBuffer[ScopedPair] += ScopedValue;
        InBuffer[ScopedPair + 1] += ScopedValue;
    }
    ScopedFilter.currentReverbGain = ScopedGainEnd;
}

void DspGraph::ApplyDucking(BusId InBus, float* InBuffer, std::uint32_t InFrameCount, const bool* InRendered) noexcept
{
    DuckingState& ScopedDucking = MBuses[InBus].ducking;
    if (ScopedDucking.sourceBus >= kMaxBuses || ScopedDucking.sourceBus == InBus)
    {
        return;
    }
    // The sidechain detector prefers the source bus's per-frame output of this
    const bool ScopedHaveSource = InRendered[ScopedDucking.sourceBus] && MBuses[ScopedDucking.sourceBus].active;
    const float* ScopedSource = ScopedHaveSource ? MBusOut[ScopedDucking.sourceBus] : nullptr;
    const float ScopedFallback = MLastBlockPeak[ScopedDucking.sourceBus];

    for (std::uint32_t ScopedFrame = 0; ScopedFrame < InFrameCount; ++ScopedFrame)
    {
        const std::size_t ScopedPair = static_cast<std::size_t>(ScopedFrame) * 2;
        float ScopedLevel = ScopedFallback;
        if (ScopedSource != nullptr)
        {
            const float ScopedLeft = std::fabs(ScopedSource[ScopedPair]);
            const float ScopedRight = std::fabs(ScopedSource[ScopedPair + 1]);
            ScopedLevel = ScopedLeft > ScopedRight ? ScopedLeft : ScopedRight;
        }
        const float ScopedDetectorCoefficient =
            ScopedLevel > ScopedDucking.envelope ? ScopedDucking.attackCoeff : ScopedDucking.releaseCoeff;
        ScopedDucking.envelope += ScopedDetectorCoefficient * (ScopedLevel - ScopedDucking.envelope);

        float ScopedReduction = ScopedDucking.envelope > ScopedDucking.threshold
                                    ? (ScopedDucking.envelope - ScopedDucking.threshold) * ScopedDucking.ratio
                                    : 0.0f;
        if (ScopedReduction > ScopedDucking.depth)
        {
            ScopedReduction = ScopedDucking.depth;
        }
        const float ScopedDesired = 1.0f - ScopedReduction;
        const float ScopedGainCoefficient =
            ScopedDesired < ScopedDucking.gain ? ScopedDucking.attackCoeff : ScopedDucking.releaseCoeff;
        ScopedDucking.gain += ScopedGainCoefficient * (ScopedDesired - ScopedDucking.gain);
        InBuffer[ScopedPair] *= ScopedDucking.gain;
        InBuffer[ScopedPair + 1] *= ScopedDucking.gain;
    }
}
}
