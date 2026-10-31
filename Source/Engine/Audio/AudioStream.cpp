// FemboyEngine2D - sample ring and microphone capture implementation.
#include "Engine/Audio/AudioStream.hpp"

#include "miniaudio.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <memory>
#include <new>
#include <string>

namespace Fby
{
namespace
{
constexpr std::uint32_t kCaptureBlockFrames = 480;

void ConfigureIndices(std::atomic<std::uint32_t>& InIndex, std::uint32_t InValue) noexcept
{
    InIndex.store(InValue, std::memory_order_release);
}

AudioDeviceStatus MapCaptureResult(ma_result InResult) noexcept
{
    if (InResult == MA_SUCCESS)
    {
        return AudioDeviceStatus::Ok;
    }
    const char* ScopedDescription = ma_result_description(InResult);
    if (ScopedDescription == nullptr)
    {
        return AudioDeviceStatus::Unavailable;
    }
    if (std::strstr(ScopedDescription, "denied") != nullptr ||
        std::strstr(ScopedDescription, "permission") != nullptr)
    {
        return AudioDeviceStatus::PermissionDenied;
    }
    if (std::strstr(ScopedDescription, "busy") != nullptr || std::strstr(ScopedDescription, "in use") != nullptr)
    {
        return AudioDeviceStatus::Busy;
    }
    return AudioDeviceStatus::Unavailable;
}

void CaptureCallback(ma_device* InDevice, void* InPlayback, const void* InInput, ma_uint32 InFrames) noexcept
{
    (void)InPlayback;
    auto* ScopedCapture = static_cast<VoiceCapture*>(InDevice->pUserData);
    if (ScopedCapture == nullptr || InInput == nullptr || InFrames == 0)
    {
        return;
    }
    const auto* ScopedSamples = static_cast<const float*>(InInput);
    float ScopedPeak = 0.0f;
    double ScopedSum = 0.0;
    for (ma_uint32 ScopedIndex = 0; ScopedIndex < InFrames; ++ScopedIndex)
    {
        const float ScopedSample = std::fabs(ScopedSamples[ScopedIndex]);
        ScopedPeak = ScopedSample > ScopedPeak ? ScopedSample : ScopedPeak;
        ScopedSum += static_cast<double>(ScopedSamples[ScopedIndex]) * ScopedSamples[ScopedIndex];
    }
    ScopedCapture->PublishLevel(ScopedPeak, static_cast<float>(std::sqrt(ScopedSum / InFrames)));
    ScopedCapture->PushFrames(ScopedSamples, InFrames);
}
}

void FloatRing::Configure(float* InBuffer, std::uint32_t InCapacityFrames, std::uint32_t InChannels) noexcept
{
    MBuffer = InBuffer;
    MCapacityFrames = InBuffer != nullptr ? InCapacityFrames : 0;
    MChannels = InChannels == 0 ? 1 : InChannels;
    ConfigureIndices(MWriteIndex, 0);
    ConfigureIndices(MReadIndex, 0);
    MDropped.store(0, std::memory_order_relaxed);
    MUnderrun.store(0, std::memory_order_relaxed);
}

std::uint32_t FloatRing::AvailableRead() const noexcept
{
    if (MCapacityFrames == 0)
    {
        return 0;
    }
    const std::uint32_t ScopedWrite = MWriteIndex.load(std::memory_order_acquire);
    const std::uint32_t ScopedRead = MReadIndex.load(std::memory_order_acquire);
    return (ScopedWrite + MCapacityFrames - ScopedRead) % MCapacityFrames;
}

std::uint32_t FloatRing::AvailableWrite() const noexcept
{
    if (MCapacityFrames == 0)
    {
        return 0;
    }
    return MCapacityFrames - 1 - AvailableRead();
}

std::uint32_t FloatRing::Write(const float* InFrames, std::uint32_t InFrameCount) noexcept
{
    if (MBuffer == nullptr || InFrames == nullptr || InFrameCount == 0)
    {
        return 0;
    }
    const std::uint32_t ScopedSpace = AvailableWrite();
    const std::uint32_t ScopedAccepted = InFrameCount < ScopedSpace ? InFrameCount : ScopedSpace;
    if (ScopedAccepted < InFrameCount)
    {
        MDropped.fetch_add(InFrameCount - ScopedAccepted, std::memory_order_relaxed);
    }
    const std::uint32_t ScopedWrite = MWriteIndex.load(std::memory_order_relaxed);
    const std::uint32_t ScopedFirst = ScopedAccepted < MCapacityFrames - ScopedWrite ? ScopedAccepted
                                                                                     : MCapacityFrames - ScopedWrite;
    std::memcpy(MBuffer + ScopedWrite * MChannels, InFrames,
                static_cast<std::size_t>(ScopedFirst) * MChannels * sizeof(float));
    if (ScopedAccepted > ScopedFirst)
    {
        std::memcpy(MBuffer, InFrames + ScopedFirst * MChannels,
                    static_cast<std::size_t>(ScopedAccepted - ScopedFirst) * MChannels * sizeof(float));
    }
    MWriteIndex.store((ScopedWrite + ScopedAccepted) % MCapacityFrames, std::memory_order_release);
    return ScopedAccepted;
}

std::uint32_t FloatRing::Read(float* OutFrames, std::uint32_t InFrameCount) noexcept
{
    if (MBuffer == nullptr || OutFrames == nullptr || InFrameCount == 0)
    {
        return 0;
    }
    const std::uint32_t ScopedAvailable = AvailableRead();
    const std::uint32_t ScopedReadFrames = InFrameCount < ScopedAvailable ? InFrameCount : ScopedAvailable;
    if (ScopedReadFrames < InFrameCount)
    {
        MUnderrun.fetch_add(InFrameCount - ScopedReadFrames, std::memory_order_relaxed);
    }
    const std::uint32_t ScopedRead = MReadIndex.load(std::memory_order_relaxed);
    const std::uint32_t ScopedFirst =
        ScopedReadFrames < MCapacityFrames - ScopedRead ? ScopedReadFrames : MCapacityFrames - ScopedRead;
    std::memcpy(OutFrames, MBuffer + ScopedRead * MChannels,
                static_cast<std::size_t>(ScopedFirst) * MChannels * sizeof(float));
    if (ScopedReadFrames > ScopedFirst)
    {
        std::memcpy(OutFrames + ScopedFirst * MChannels, MBuffer,
                    static_cast<std::size_t>(ScopedReadFrames - ScopedFirst) * MChannels * sizeof(float));
    }
    MReadIndex.store((ScopedRead + ScopedReadFrames) % MCapacityFrames, std::memory_order_release);
    return ScopedReadFrames;
}

void FloatRing::ResetCounters() noexcept
{
    MDropped.store(0, std::memory_order_relaxed);
    MUnderrun.store(0, std::memory_order_relaxed);
}

VoiceCapture::~VoiceCapture()
{
    Stop();
}

bool VoiceCapture::Start(const AudioDeviceEnumerator& InDevices, const char* InDeviceId, FloatRing* InRing,
                         std::uint32_t InSampleRate, std::string* InError) noexcept
{
    Stop();
    if (!InDevices.Ready() || InDevices.Context() == nullptr || InRing == nullptr)
    {
        MStatus = AudioDeviceStatus::BackendMissing;
        if (InError != nullptr)
        {
            *InError = "capture: backend or ring is not ready";
        }
        return false;
    }
    const std::int32_t ScopedIndex = InDevices.Resolve(AudioDeviceKind::Input, InDeviceId);
    if (ScopedIndex < 0)
    {
        MStatus = AudioDeviceStatus::Unavailable;
        if (InError != nullptr)
        {
            *InError = "capture: no input device";
        }
        return false;
    }
    // The persisted key is a 64-bit hash; if two devices collide, opening the
    // first one would silently capture the wrong microphone.
    if (AudioDeviceIdCollisions(InDevices.Devices(AudioDeviceKind::Input), InDeviceId) > 1)
    {
        MStatus = AudioDeviceStatus::Unavailable;
        if (InError != nullptr)
        {
            *InError = "capture: the device id is ambiguous; select the device again";
        }
        return false;
    }
    const auto* ScopedRawId =
        reinterpret_cast<const ma_device_id*>(InDevices.RawDeviceId(AudioDeviceKind::Input,
                                                                    static_cast<std::uint32_t>(ScopedIndex)));

    ma_device_config ScopedConfig = ma_device_config_init(ma_device_type_capture);
    ScopedConfig.capture.format = ma_format_f32;
    ScopedConfig.capture.channels = 1;
    ScopedConfig.capture.pDeviceID = ScopedRawId;
    ScopedConfig.sampleRate = InSampleRate;
    ScopedConfig.periodSizeInFrames = kCaptureBlockFrames;
    ScopedConfig.dataCallback = &CaptureCallback;
    ScopedConfig.pUserData = this;

    auto* ScopedDevice = new (std::nothrow) ma_device{};
    if (ScopedDevice == nullptr)
    {
        MStatus = AudioDeviceStatus::Unavailable;
        return false;
    }
    const ma_result ScopedInit =
        ma_device_init(static_cast<ma_context*>(InDevices.Context()), &ScopedConfig, ScopedDevice);
    if (ScopedInit != MA_SUCCESS)
    {
        MStatus = MapCaptureResult(ScopedInit);
        if (InError != nullptr)
        {
            *InError = ma_result_description(ScopedInit) != nullptr ? ma_result_description(ScopedInit)
                                                                    : "capture: init failed";
        }
        ma_device_uninit(ScopedDevice);
        delete ScopedDevice;
        return false;
    }
    if (ma_device_start(ScopedDevice) != MA_SUCCESS)
    {
        MStatus = AudioDeviceStatus::Unavailable;
        if (InError != nullptr)
        {
            *InError = "capture: device did not start";
        }
        ma_device_uninit(ScopedDevice);
        delete ScopedDevice;
        return false;
    }

    MDevice = ScopedDevice;
    MRing = InRing;
    MSampleRate = InSampleRate;
    MStatus = AudioDeviceStatus::Ok;
    MPeak.store(0.0f, std::memory_order_relaxed);
    MRms.store(0.0f, std::memory_order_relaxed);
    return true;
}

void VoiceCapture::Stop() noexcept
{
    if (MDevice != nullptr)
    {
        auto* ScopedDevice = static_cast<ma_device*>(MDevice);
        ma_device_stop(ScopedDevice);
        ma_device_uninit(ScopedDevice);
        delete ScopedDevice;
    }
    MDevice = nullptr;
    MRing = nullptr;
    MStatus = AudioDeviceStatus::Unavailable;
    MPeak.store(0.0f, std::memory_order_relaxed);
    MRms.store(0.0f, std::memory_order_relaxed);
}

// The monitor is a thin wrapper over VoiceCapture: one miniaudio capture path,
// one callback, one place that computes peak/RMS and ring writes.
struct AudioCaptureMonitor::State
{
    VoiceCapture Capture{};
    std::array<float, 8192> Storage{};
    FloatRing Ring{};

    State()
    {
        Ring.Configure(Storage.data(), static_cast<std::uint32_t>(Storage.size()), 1);
    }
};

AudioCaptureMonitor::AudioCaptureMonitor() = default;

AudioCaptureMonitor::~AudioCaptureMonitor()
{
    Stop();
}

bool AudioCaptureMonitor::Start(const AudioDeviceEnumerator& InDevices, const char* InDeviceId,
                                std::uint32_t InSampleRate, std::string* InError) noexcept
{
    Stop();
    if (!MState)
    {
        MState = std::make_unique<State>();
    }
    if (!MState)
    {
        if (InError != nullptr)
        {
            *InError = "capture: cannot allocate the monitor state";
        }
        return false;
    }
    return MState->Capture.Start(InDevices, InDeviceId, &MState->Ring, InSampleRate, InError);
}

void AudioCaptureMonitor::Stop() noexcept
{
    if (MState)
    {
        MState->Capture.Stop();
        MState->Ring.ResetCounters();
    }
}

bool AudioCaptureMonitor::Active() const noexcept
{
    return MState && MState->Capture.Active();
}

float AudioCaptureMonitor::Peak() const noexcept
{
    return MState ? MState->Capture.Peak() : 0.0f;
}

float AudioCaptureMonitor::Rms() const noexcept
{
    return MState ? MState->Capture.Rms() : 0.0f;
}

AudioDeviceStatus AudioCaptureMonitor::Status() const noexcept
{
    return MState ? MState->Capture.Status() : AudioDeviceStatus::Unavailable;
}

std::uint64_t AudioCaptureMonitor::DroppedFrames() const noexcept
{
    return MState ? MState->Ring.DroppedFrames() : 0;
}
}
