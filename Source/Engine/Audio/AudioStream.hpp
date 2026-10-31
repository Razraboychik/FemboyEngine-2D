// FemboyEngine2D - lock-free sample ring and microphone capture into it.
#pragma once

#include "Engine/Audio/AudioDevices.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>

namespace Fby
{

// Single-producer / single-consumer ring of interleaved f32 frames. The storage
// is caller-owned, so capture and mixer paths never allocate.
class FloatRing
{
public:
    FloatRing() = default;

    void Configure(float* InBuffer, std::uint32_t InCapacityFrames, std::uint32_t InChannels) noexcept;

    // Producer side. Returns the frames actually accepted; the rest are dropped
    // and counted, which is the drift-overflow signal.
    [[nodiscard]] std::uint32_t Write(const float* InFrames, std::uint32_t InFrameCount) noexcept;
    // Consumer side. Returns the frames actually read; a short read is an
    // underrun and is counted.
    [[nodiscard]] std::uint32_t Read(float* OutFrames, std::uint32_t InFrameCount) noexcept;

    [[nodiscard]] std::uint32_t AvailableRead() const noexcept;
    [[nodiscard]] std::uint32_t AvailableWrite() const noexcept;
    [[nodiscard]] std::uint32_t CapacityFrames() const noexcept
    {
        return MCapacityFrames;
    }
    [[nodiscard]] std::uint32_t Channels() const noexcept
    {
        return MChannels;
    }
    [[nodiscard]] std::uint64_t DroppedFrames() const noexcept
    {
        return MDropped.load(std::memory_order_relaxed);
    }
    [[nodiscard]] std::uint64_t UnderrunFrames() const noexcept
    {
        return MUnderrun.load(std::memory_order_relaxed);
    }
    void ResetCounters() noexcept;

protected:
    float* MBuffer = nullptr;
    std::uint32_t MCapacityFrames = 0;
    std::uint32_t MChannels = 1;
    // Producer index and consumer index, both modulo the capacity.
    alignas(64) std::atomic<std::uint32_t> MWriteIndex{0};
    alignas(64) std::atomic<std::uint32_t> MReadIndex{0};
    std::atomic<std::uint64_t> MDropped{0};
    std::atomic<std::uint64_t> MUnderrun{0};
};

// Opens one capture device and pushes mono f32 frames into a ring. Drift is
// handled by the ring: a full ring drops the newest block, an empty one is an
// underrun reported by the consumer.
class VoiceCapture
{
public:
    VoiceCapture() = default;
    ~VoiceCapture();

    VoiceCapture(const VoiceCapture&) = delete;
    VoiceCapture& operator=(const VoiceCapture&) = delete;

    [[nodiscard]] bool Start(const AudioDeviceEnumerator& InDevices, const char* InDeviceId, FloatRing* InRing,
                             std::uint32_t InSampleRate = 48000, std::string* InError = nullptr) noexcept;
    void Stop() noexcept;
    [[nodiscard]] bool Active() const noexcept
    {
        return MDevice != nullptr;
    }
    [[nodiscard]] AudioDeviceStatus Status() const noexcept
    {
        return MStatus;
    }
    [[nodiscard]] std::uint32_t SampleRate() const noexcept
    {
        return MSampleRate;
    }

    // Called by the backend callback on the capture thread.
    void PushFrames(const float* InFrames, std::uint32_t InFrameCount) noexcept
    {
        if (MRing != nullptr && InFrames != nullptr)
        {
            (void)MRing->Write(InFrames, InFrameCount);
        }
    }
    // Metering for the same frames; published by the capture callback.
    void PublishLevel(float InPeak, float InRms) noexcept
    {
        MPeak.store(InPeak, std::memory_order_relaxed);
        MRms.store(InRms, std::memory_order_relaxed);
    }
    [[nodiscard]] float Peak() const noexcept
    {
        return MPeak.load(std::memory_order_relaxed);
    }
    [[nodiscard]] float Rms() const noexcept
    {
        return MRms.load(std::memory_order_relaxed);
    }

protected:
    void* MDevice = nullptr;
    FloatRing* MRing = nullptr;
    AudioDeviceStatus MStatus = AudioDeviceStatus::Unavailable;
    std::uint32_t MSampleRate = 48000;
    std::atomic<float> MPeak{0.0f};
    std::atomic<float> MRms{0.0f};
};
}
