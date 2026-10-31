// FemboyEngine2D - AudioSystem implementation.
#include "Engine/Audio/Audio.hpp"

#include "Engine/Audio/AudioStream.hpp"
#include "Engine/Audio/OggOpus.hpp"

#include "miniaudio.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <new>
#include <utility>

namespace Fby
{
namespace
{
void AudioCallback(ma_device* InDevice, void* Output, const void* InInput, ma_uint32 InFrameCount)
{
    (void)InInput;
    auto* ScopedSystem = static_cast<AudioSystem*>(InDevice->pUserData);
    if (ScopedSystem != nullptr && Output != nullptr)
    {
        ScopedSystem->Mix(static_cast<float*>(Output), InFrameCount);
    }
}

// AudioBus was designed to mirror the three built-in graph bus ids, so the
[[nodiscard]] BusId ToBusId(AudioBus InBus) noexcept
{
    const auto ScopedIndex = static_cast<std::uint32_t>(InBus);
    return ScopedIndex < static_cast<std::uint32_t>(AudioBus::Count) ? static_cast<BusId>(ScopedIndex) : kSfxBus;
}

// Distance attenuation shared by both authoring paths. The component flavour has
[[nodiscard]] float Attenuate(Vector2D InSourcePosition, Vector2D InListenerPosition, float InMaxDistance) noexcept
{
    const Vector2D ScopedDelta = InSourcePosition - InListenerPosition;
    const float ScopedDistance = std::sqrt(ScopedDelta.LengthSquared());
    const float ScopedFalloff = 1.0f - ScopedDistance / std::max(InMaxDistance, 1.0f);
    return std::clamp(ScopedFalloff, 0.0f, 1.0f);
}

// Stereo position from the listener-relative X. 0 is dead centre and +1 is
// fully right; the offset is normalised by half the audible radius.
[[nodiscard]] float PanPosition(Vector2D InSourcePosition, Vector2D InListenerPosition, float InMaxDistance) noexcept
{
    const float ScopedDelta = InSourcePosition.X - InListenerPosition.X;
    const float ScopedScale = std::max(InMaxDistance * 0.5f, 1.0f);
    return std::clamp(ScopedDelta / ScopedScale, -1.0f, 1.0f);
}
}

AudioSystem::~AudioSystem()
{
    Shutdown();
}

bool AudioSystem::Initialize(std::uint32_t InSampleRate)
{
    Shutdown();
    MSampleRate = InSampleRate;

    // The graph comes up even when the device does not: a headless run can still
    if (!MMixer.Initialize(InSampleRate))
    {
        return false;
    }
    // The limiter is part of the device boundary, not an optional effect: it
    MLimiter.Configure(InSampleRate);

    auto* ScopedDevice = new (std::nothrow) ma_device{};
    if (ScopedDevice == nullptr)
    {
        return false;
    }
    ma_device_config ScopedConfig = ma_device_config_init(ma_device_type_playback);
    ScopedConfig.playback.format = ma_format_f32;
    ScopedConfig.playback.channels = 2;
    ScopedConfig.sampleRate = InSampleRate;
    ScopedConfig.dataCallback = AudioCallback;
    ScopedConfig.pUserData = this;
    ScopedConfig.periodSizeInFrames = kMaxBlockFrames;

    if (ma_device_init(nullptr, &ScopedConfig, ScopedDevice) != MA_SUCCESS)
    {
        delete ScopedDevice;
        return false;
    }
    if (ma_device_start(ScopedDevice) != MA_SUCCESS)
    {
        ma_device_uninit(ScopedDevice);
        delete ScopedDevice;
        return false;
    }
    MDevice = ScopedDevice;
    MInitialized = true;
    return true;
}

bool AudioSystem::ReopenOutput(const AudioDeviceEnumerator& InDevices, AudioDeviceKind InKind,
                               std::uint32_t InDeviceIndex) noexcept
{
    (void)InKind;
    if (!MInitialized)
    {
        return false;
    }
    const std::uint8_t* ScopedRaw = InDevices.RawDeviceId(AudioDeviceKind::Output, InDeviceIndex);
    if (ScopedRaw == nullptr)
    {
        return false;
    }

    if (MDevice != nullptr)
    {
        auto* ScopedOld = static_cast<ma_device*>(MDevice);
        ma_device_uninit(ScopedOld);
        delete ScopedOld;
        MDevice = nullptr;
    }
    if (MContext != nullptr)
    {
        auto* ScopedOldContext = static_cast<ma_context*>(MContext);
        ma_context_uninit(ScopedOldContext);
        delete ScopedOldContext;
        MContext = nullptr;
    }

    auto* ScopedContext = new (std::nothrow) ma_context{};
    auto* ScopedDevice = new (std::nothrow) ma_device{};
    if (ScopedContext == nullptr || ScopedDevice == nullptr)
    {
        delete ScopedContext;
        delete ScopedDevice;
        return false;
    }
    if (ma_context_init(nullptr, 0, nullptr, ScopedContext) != MA_SUCCESS)
    {
        delete ScopedContext;
        delete ScopedDevice;
        return false;
    }

    ma_device_info* ScopedPlayback = nullptr;
    ma_uint32 ScopedCount = 0;
    if (ma_context_get_devices(ScopedContext, &ScopedPlayback, &ScopedCount, nullptr, nullptr) != MA_SUCCESS)
    {
        ma_context_uninit(ScopedContext);
        delete ScopedContext;
        delete ScopedDevice;
        return false;
    }

    const ma_device_id* ScopedChosen = nullptr;
    for (ma_uint32 ScopedIndex = 0; ScopedIndex < ScopedCount; ++ScopedIndex)
    {
        if (std::memcmp(&ScopedPlayback[ScopedIndex].id, ScopedRaw, sizeof(ma_device_id)) == 0)
        {
            ScopedChosen = &ScopedPlayback[ScopedIndex].id;
            break;
        }
    }
    if (ScopedChosen == nullptr)
    {
        ma_context_uninit(ScopedContext);
        delete ScopedContext;
        delete ScopedDevice;
        return false;
    }

    ma_device_config ScopedConfig = ma_device_config_init(ma_device_type_playback);
    ScopedConfig.playback.format = ma_format_f32;
    ScopedConfig.playback.channels = 2;
    ScopedConfig.sampleRate = MSampleRate;
    ScopedConfig.dataCallback = AudioCallback;
    ScopedConfig.pUserData = this;
    ScopedConfig.periodSizeInFrames = kMaxBlockFrames;
    ScopedConfig.playback.pDeviceID = ScopedChosen;

    if (ma_device_init(ScopedContext, &ScopedConfig, ScopedDevice) != MA_SUCCESS)
    {
        ma_context_uninit(ScopedContext);
        delete ScopedContext;
        delete ScopedDevice;
        return false;
    }
    if (ma_device_start(ScopedDevice) != MA_SUCCESS)
    {
        ma_device_uninit(ScopedDevice);
        ma_context_uninit(ScopedContext);
        delete ScopedContext;
        delete ScopedDevice;
        return false;
    }
    MContext = ScopedContext;
    MDevice = ScopedDevice;
    return true;
}

void AudioSystem::Shutdown()
{
    if (MDevice != nullptr)
    {
        auto* ScopedDevice = static_cast<ma_device*>(MDevice);
        ma_device_stop(ScopedDevice);
        ma_device_uninit(ScopedDevice);
        delete ScopedDevice;
        MDevice = nullptr;
    }
    MMixer.Shutdown();
    ResetRuntimeState();
    MInitialized = false;
}

void AudioSystem::ResetRuntimeState()
{
    for (SourceRuntime& ScopedRuntime : MSources)
    {
        ScopedRuntime = SourceRuntime{};
    }
    for (BusRuntime& ScopedRuntime : MBusRuntime)
    {
        ScopedRuntime = BusRuntime{};
    }
    MUpdateStamp = 0;
}

const AudioClip* AudioSystem::FindClip(AssetId InId) const noexcept
{
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < MClipCount; ++ScopedIndex)
    {
        if (MClips[ScopedIndex].id == InId)
        {
            return &MClips[ScopedIndex];
        }
    }
    return nullptr;
}

const AudioSystem::CookedClipRecord* AudioSystem::FindCookedClip(AssetId InId) const noexcept
{
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < MCookedClipCount; ++ScopedIndex)
    {
        if (MCookedClips[ScopedIndex].id == InId)
        {
            return &MCookedClips[ScopedIndex];
        }
    }
    return nullptr;
}

const AudioSystem::OwnedPcmRecord* AudioSystem::FindOwnedPcm(AssetId InId) const noexcept
{
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < MOwnedPcmCount; ++ScopedIndex)
    {
        if (MOwnedPcm[ScopedIndex].id == InId)
        {
            return &MOwnedPcm[ScopedIndex];
        }
    }
    return nullptr;
}

bool AudioSystem::RegisterClip(AssetId InId, std::span<const float> InInterleavedSamples, std::uint32_t InChannels,
                               std::uint32_t InSampleRate)
{
    if (!InId.IsValid() || InInterleavedSamples.empty() || InChannels == 0 || InChannels > 2 || InSampleRate == 0)
    {
        return false;
    }
    AudioClip* ScopedClip = nullptr;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < MClipCount; ++ScopedIndex)
    {
        if (MClips[ScopedIndex].id == InId)
        {
            ScopedClip = &MClips[ScopedIndex];
            break;
        }
    }
    if (ScopedClip == nullptr)
    {
        if (MClipCount >= kMaxAudioClips)
        {
            return false;
        }
        ScopedClip = &MClips[MClipCount++];
    }
    ScopedClip->id = InId;
    ScopedClip->samples = InInterleavedSamples.data();
    ScopedClip->frameCount = static_cast<std::uint32_t>(InInterleavedSamples.size() / InChannels);
    ScopedClip->channels = InChannels;
    ScopedClip->sampleRate = InSampleRate;
    return true;
}

bool AudioSystem::RegisterCookedClip(AssetId InId, std::span<const std::byte> InCookedBlock, std::string* InError)
{
    if (!InId.IsValid())
    {
        if (InError != nullptr)
        {
            *InError = "audio clip id is invalid";
        }
        return false;
    }
    const auto ScopedView = Assets::ParseCookedAudio(InCookedBlock, InError);
    if (!ScopedView)
    {
        return false;
    }

    CookedClipRecord* ScopedRecord = nullptr;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < MCookedClipCount; ++ScopedIndex)
    {
        if (MCookedClips[ScopedIndex].id == InId)
        {
            ScopedRecord = &MCookedClips[ScopedIndex];
            break;
        }
    }
    if (ScopedRecord == nullptr)
    {
        if (MCookedClipCount >= kMaxAudioClips)
        {
            if (InError != nullptr)
            {
                *InError = "cooked audio clip table is full";
            }
            return false;
        }
        ScopedRecord = &MCookedClips[MCookedClipCount++];
    }
    ScopedRecord->id = InId;
    ScopedRecord->view = *ScopedView;
    return true;
}

bool AudioSystem::RegisterOggMusic(AssetId InId, std::span<const std::byte> InCookedBlock, std::string* InError)
{
    if (!InId.IsValid())
    {
        if (InError != nullptr)
        {
            *InError = "audio clip id is invalid";
        }
        return false;
    }
    const auto ScopedView = Assets::ParseCookedAudio(InCookedBlock, InError);
    if (!ScopedView)
    {
        return false;
    }
    if (ScopedView->format != Assets::PcmFormat::OggOpus)
    {
        if (InError != nullptr)
        {
            *InError = "RegisterOggMusic expects an Ogg Opus .fpcm block";
        }
        return false;
    }

    OwnedPcmRecord* ScopedRecord = nullptr;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < MOwnedPcmCount; ++ScopedIndex)
    {
        if (MOwnedPcm[ScopedIndex].id == InId)
        {
            ScopedRecord = &MOwnedPcm[ScopedIndex];
            break;
        }
    }
    if (ScopedRecord == nullptr)
    {
        if (MOwnedPcmCount >= kMaxAudioClips)
        {
            if (InError != nullptr)
            {
                *InError = "decoded music table is full";
            }
            return false;
        }
        ScopedRecord = &MOwnedPcm[MOwnedPcmCount];
    }

    std::vector<float> ScopedDecoded;
    std::uint32_t ScopedFrames = 0;
    if (!Audio::DecodeOggOpusToStereo(ScopedView->samples, Audio::kOggMaxWholeTrackFrames, &ScopedDecoded,
                                      &ScopedFrames, InError))
    {
        return false;
    }
    if (ScopedRecord == &MOwnedPcm[MOwnedPcmCount])
    {
        ++MOwnedPcmCount;
    }
    ScopedRecord->id = InId;
    ScopedRecord->samples = std::move(ScopedDecoded);
    ScopedRecord->frameCount = ScopedFrames;
    ScopedRecord->channels = 2;
    return true;
}

void AudioSystem::SetBusVolume(AudioBus InBus, float InVolume)
{
    (void)MMixer.SetBusGain(ToBusId(InBus), InVolume);
}

float AudioSystem::BusVolume(AudioBus InBus) const noexcept
{
    return MMixer.BusGain(ToBusId(InBus));
}

bool AudioSystem::SetBusZoneParameters(AudioBus InBus, float InReverbGain, float InLowPassHz,
                                       float InSmoothingSeconds) noexcept
{
    if (!MInitialized)
    {
        return false;
    }
    BusFilterParams ScopedParams;
    ScopedParams.reverbGain = InReverbGain;
    ScopedParams.lowPassHz = InLowPassHz;
    ScopedParams.smoothingSeconds = InSmoothingSeconds;
    return MMixer.SetBusFilter(ToBusId(InBus), ScopedParams) == DspResult::Ok;
}

void AudioSystem::SetMasterMuted(bool InMuted) noexcept
{
    (void)MMixer.SetBusMuted(kMasterBus, InMuted);
}

bool AudioSystem::MasterMuted() const noexcept
{
    return MMixer.BusMuted(kMasterBus);
}

void AudioSystem::ConfigureLimiter(const LimiterDesc& InDesc) noexcept
{
    MLimiter.Configure(MSampleRate == 0 ? 48000 : MSampleRate, InDesc);
}

std::uint32_t AudioSystem::StartClip(AssetId InId, BusId InBus, float InVolume, float InPitch, bool InLoop) noexcept
{
    DspClipView ScopedClip{};
    if (const AudioClip* ScopedFloatClip = FindClip(InId))
    {
        if (ScopedFloatClip->samples == nullptr || ScopedFloatClip->frameCount == 0)
        {
            return kInvalidAudioVoice;
        }
        ScopedClip.samples = reinterpret_cast<const std::byte*>(ScopedFloatClip->samples);
        ScopedClip.frameCount = ScopedFloatClip->frameCount;
        ScopedClip.channels = ScopedFloatClip->channels;
        ScopedClip.sampleRate = ScopedFloatClip->sampleRate;
        ScopedClip.format = Assets::PcmFormat::PcmF32;
    }
    else if (const OwnedPcmRecord* ScopedOwned = FindOwnedPcm(InId))
    {
        if (ScopedOwned->samples.empty() || ScopedOwned->frameCount == 0)
        {
            return kInvalidAudioVoice;
        }
        ScopedClip.samples = reinterpret_cast<const std::byte*>(ScopedOwned->samples.data());
        ScopedClip.frameCount = ScopedOwned->frameCount;
        ScopedClip.channels = ScopedOwned->channels;
        ScopedClip.sampleRate = Audio::kOggOpusSampleRate;
        ScopedClip.format = Assets::PcmFormat::PcmF32;
    }
    else if (const CookedClipRecord* ScopedCooked = FindCookedClip(InId))
    {
        // A compressed block is metadata for RegisterOggMusic(); the mixer can
        if (Assets::IsCompressed(ScopedCooked->view.format))
        {
            return kInvalidAudioVoice;
        }
        ScopedClip.samples = ScopedCooked->view.samples.data();
        ScopedClip.frameCount = ScopedCooked->view.frameCount;
        ScopedClip.channels = ScopedCooked->view.channels;
        ScopedClip.sampleRate = ScopedCooked->view.sampleRate;
        ScopedClip.format = ScopedCooked->view.format;
    }
    else
    {
        return kInvalidAudioVoice;
    }

    std::uint32_t ScopedVoice = kInvalidVoice;
    if (MMixer.PlayOneShot(ScopedClip, InBus, InVolume, InPitch, InLoop, &ScopedVoice) != DspResult::Ok)
    {
        return kInvalidAudioVoice;
    }
    return ScopedVoice;
}

std::uint32_t AudioSystem::Play(AssetId InId, AudioBus InBus, float InVolume, float InPitch, bool InLoop)
{
    return StartClip(InId, ToBusId(InBus), InVolume, InPitch, InLoop);
}

void AudioSystem::Stop(std::uint32_t InVoice)
{
    if (InVoice < kMaxAudioVoices)
    {
        (void)MMixer.StopVoice(InVoice);
    }
}

void AudioSystem::StopAll()
{
    MMixer.StopAllVoices();
}

bool AudioSystem::SetVoiceSpatial(std::uint32_t InVoice, float InVolume, float InPan) noexcept
{
    if (InVoice == kInvalidAudioVoice || InVoice >= kMaxAudioVoices)
    {
        return false;
    }
    return MMixer.SetVoiceSpatial(InVoice, InVolume, InPan) == DspResult::Ok;
}

void AudioSystem::Mix(float* Output, std::uint32_t InFrameCount) noexcept
{
    // The one audio-thread entry point. DspGraph::Process() drains commands,
    MMixer.Process(Output, InFrameCount);
    // Safety stage. The device format this engine opens is stereo; a future
    MLimiter.Process(Output, InFrameCount, 2);

    if (MReferenceRing != nullptr && Output != nullptr)
    {
        std::array<float, 256> ScopedMono{};
        std::uint32_t ScopedOffset = 0;
        while (ScopedOffset < InFrameCount)
        {
            const std::uint32_t ScopedChunk =
                InFrameCount - ScopedOffset < ScopedMono.size() ? InFrameCount - ScopedOffset
                                                               : static_cast<std::uint32_t>(ScopedMono.size());
            for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedChunk; ++ScopedIndex)
            {
                const std::uint32_t ScopedSource = (ScopedOffset + ScopedIndex) * 2;
                ScopedMono[ScopedIndex] = 0.5f * (Output[ScopedSource] + Output[ScopedSource + 1]);
            }
            (void)MReferenceRing->Write(ScopedMono.data(), ScopedChunk);
            ScopedOffset += ScopedChunk;
        }
    }
}

void AudioSystem::Update(Ecs::Registry& InRegistry)
{
    const AudioListener* ScopedListener = nullptr;
    auto ScopedListenerView = InRegistry.view<AudioListener>();
    for (auto [ScopedEntity, ScopedCandidate] : ScopedListenerView.each())
    {
        (void)ScopedEntity;
        if (ScopedCandidate.active)
        {
            ScopedListener = &ScopedCandidate;
            break;
        }
    }

    // Legacy authoring path: AudioSource keeps its runtime handle in the
    auto ScopedLegacyView = InRegistry.view<AudioSource>();
    for (auto [ScopedEntity, ScopedSource] : ScopedLegacyView.each())
    {
        (void)ScopedEntity;
        if (ScopedSource.playing)
        {
            if (ScopedSource.voice == kInvalidAudioVoice)
            {
                float ScopedVolume = ScopedSource.volume;
                if (ScopedSource.spatial && ScopedListener != nullptr)
                {
                    ScopedVolume *=
                        Attenuate(ScopedSource.position, ScopedListener->position, ScopedSource.maxDistance);
                }
                ScopedSource.voice =
                    Play(ScopedSource.clip, ScopedSource.bus, ScopedVolume, ScopedSource.pitch, ScopedSource.loop);
            }
            if (ScopedSource.voice != kInvalidAudioVoice)
            {
                float ScopedVolume = ScopedSource.volume;
                float ScopedPan = 0.0f;
                if (ScopedSource.spatial && ScopedListener != nullptr)
                {
                    ScopedVolume *=
                        Attenuate(ScopedSource.position, ScopedListener->position, ScopedSource.maxDistance);
                    ScopedPan = PanPosition(ScopedSource.position, ScopedListener->position, ScopedSource.maxDistance);
                }
                (void)SetVoiceSpatial(ScopedSource.voice, ScopedVolume, ScopedPan);
            }
        }
        else if (ScopedSource.voice != kInvalidAudioVoice)
        {
            Stop(ScopedSource.voice);
            ScopedSource.voice = kInvalidAudioVoice;
        }
    }

    SyncBusComponents(InRegistry);
    SyncSourceComponents(InRegistry, ScopedListener);
}

void AudioSystem::SyncBusComponents(Ecs::Registry& InRegistry)
{
    ++MUpdateStamp;
    if (MUpdateStamp == 0)
    {
        MUpdateStamp = 1;
    }

    auto ScopedView = InRegistry.view<AudioBusComponent>();
    for (auto [ScopedEntity, ScopedBus] : ScopedView.each())
    {
        (void)ScopedEntity;
        if (ScopedBus.BusID == kInvalidBus)
        {
            // The component asked for a bus the graph does not know yet; create
            const BusId ScopedParent = ScopedBus.ParentBus < kMaxBuses ? ScopedBus.ParentBus : kMasterBus;
            BusId ScopedCreated = kInvalidBus;
            if (MMixer.CreateBus(ScopedParent, ScopedBus.Volume, &ScopedCreated) != DspResult::Ok)
            {
                continue;
            }
            ScopedBus.BusID = ScopedCreated;
        }
        if (ScopedBus.BusID >= kMaxBuses || !MMixer.IsBusActive(ScopedBus.BusID))
        {
            continue;
        }

        BusRuntime& ScopedRuntime = MBusRuntime[ScopedBus.BusID];
        ScopedRuntime.busId = ScopedBus.BusID;
        ScopedRuntime.stamp = MUpdateStamp;

        const float ScopedVolume = std::clamp(ScopedBus.Volume, 0.0f, 8.0f);
        if (!ScopedRuntime.applied || ScopedVolume != ScopedRuntime.volume)
        {
            // Component volume goes through the command ring (the ECS path is a
            AudioCommand ScopedCommand{};
            ScopedCommand.type = AudioCommandType::SetGain;
            ScopedCommand.bus = ScopedBus.BusID;
            ScopedCommand.gain = ScopedVolume;
            (void)MMixer.PushCommand(ScopedCommand);
            ScopedRuntime.volume = ScopedVolume;
        }
        if (!ScopedRuntime.applied || ScopedRuntime.parent != ScopedBus.ParentBus)
        {
            const BusId ScopedParent = ScopedBus.ParentBus < kMaxBuses ? ScopedBus.ParentBus : kMasterBus;
            if (MMixer.SetBusRouting(ScopedBus.BusID, ScopedParent) == DspResult::Ok)
            {
                ScopedRuntime.parent = ScopedBus.ParentBus;
            }
        }
        (void)MMixer.SetBusMuted(ScopedBus.BusID, (ScopedBus.Flags & kAudioBusFlagMuted) != 0);
        ScopedRuntime.flags = ScopedBus.Flags;
        ScopedRuntime.applied = true;
    }
}

void AudioSystem::SyncSourceComponents(Ecs::Registry& InRegistry, const AudioListener* InListener)
{
    auto ScopedView = InRegistry.view<AudioSourceComponent>();
    for (auto [ScopedEntity, ScopedSource] : ScopedView.each())
    {
        const std::uint32_t ScopedRawEntity = static_cast<std::uint32_t>(ScopedEntity);
        SourceRuntime* ScopedRuntime = nullptr;
        for (SourceRuntime& ScopedCandidate : MSources)
        {
            if (ScopedCandidate.entity == ScopedRawEntity)
            {
                ScopedRuntime = &ScopedCandidate;
                break;
            }
        }

        if (!ScopedSource.Playing)
        {
            if (ScopedRuntime != nullptr)
            {
                if (ScopedRuntime->voice != kInvalidAudioVoice)
                {
                    Stop(ScopedRuntime->voice);
                }
                *ScopedRuntime = SourceRuntime{};
            }
            continue;
        }

        if (ScopedRuntime == nullptr)
        {
            for (SourceRuntime& ScopedCandidate : MSources)
            {
                if (ScopedCandidate.entity == 0xFFFFFFFFu)
                {
                    ScopedRuntime = &ScopedCandidate;
                    ScopedRuntime->entity = ScopedRawEntity;
                    ScopedRuntime->voice = kInvalidAudioVoice;
                    break;
                }
            }
        }
        if (ScopedRuntime == nullptr)
        {
            // More concurrently playing component sources than voices
            continue;
        }
        ScopedRuntime->stamp = MUpdateStamp;

        if (ScopedRuntime->voice == kInvalidAudioVoice)
        {
            float ScopedVolume = ScopedSource.Volume;
            if (ScopedSource.Spatial && InListener != nullptr)
            {
                ScopedVolume *= Attenuate(ScopedSource.Position, InListener->position, 4096.0f);
            }
            const BusId ScopedBus = ScopedSource.BusID < kMaxBuses ? ScopedSource.BusID : kSfxBus;
            ScopedRuntime->voice =
                StartClip(ScopedSource.Clip, ScopedBus, ScopedVolume, ScopedSource.Pitch, ScopedSource.Loop);
        }
        if (ScopedRuntime->voice != kInvalidAudioVoice)
        {
            float ScopedVolume = ScopedSource.Volume;
            float ScopedPan = 0.0f;
            if (ScopedSource.Spatial && InListener != nullptr)
            {
                ScopedVolume *= Attenuate(ScopedSource.Position, InListener->position, 4096.0f);
                ScopedPan = PanPosition(ScopedSource.Position, InListener->position, 4096.0f);
            }
            (void)SetVoiceSpatial(ScopedRuntime->voice, ScopedVolume, ScopedPan);
        }
    }

    // A component that was removed (or stopped playing) without our seeing it
    for (SourceRuntime& ScopedRuntime : MSources)
    {
        if (ScopedRuntime.entity == 0xFFFFFFFFu || ScopedRuntime.stamp == MUpdateStamp)
        {
            continue;
        }
        if (ScopedRuntime.voice != kInvalidAudioVoice)
        {
            Stop(ScopedRuntime.voice);
        }
        ScopedRuntime = SourceRuntime{};
    }
}
}
