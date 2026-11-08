// FemboyEngine2D - decoded video audio stream: PCM ring and playback clock.
#pragma once

#include "Engine/Video/VideoAudioDecoder.hpp"
#include "Engine/Video/WebmDemuxer.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <string>

namespace Fby::Video
{

enum class AudioPumpStatus : std::uint8_t
{
    Decoded = 0,
    Buffered,
    End,
    Error,
};

// One decoded audio stream for a video player. The ring is fixed-size and the
// class is single-threaded: PumpOne()/Read() belong to the audio owner, while a
// caller may hand the PCM to the mixer. AudioClockNs() is the presentation time
// of the next sample the mixer has not consumed yet.
class VideoAudioStream
{
public:
    static constexpr std::uint32_t kMaxRingFrames = 8192;
    static constexpr std::uint32_t kMaxChannels = 2;

    VideoAudioStream() = default;
    ~VideoAudioStream();

    VideoAudioStream(const VideoAudioStream&) = delete;
    VideoAudioStream& operator=(const VideoAudioStream&) = delete;

    // Binds a WebM mapping whose lifetime must exceed the stream.
    [[nodiscard]] bool Open(std::span<const std::byte> InWebm, std::string* InError = nullptr);
    // Standalone ring, used when the host decodes audio itself.
    [[nodiscard]] bool Initialize(std::uint32_t InSampleRate, std::uint32_t InChannels, std::string* InError = nullptr);
    void Close() noexcept;
    // Restarts the audio cursor at the first cluster and clears the ring.
    void Rewind() noexcept;
    // Restarts at the cluster containing InTimestampNs and skips packets before
    // the target. The ring is cleared and the audio clock is seeded at the
    // target so the video and audio share the same seek position.
    [[nodiscard]] bool SeekToNs(std::uint64_t InTimestampNs) noexcept;

    [[nodiscard]] bool IsOpen() const noexcept
    {
        return MOpened;
    }
    [[nodiscard]] std::uint32_t SampleRate() const noexcept
    {
        return MSampleRate;
    }
    [[nodiscard]] std::uint32_t Channels() const noexcept
    {
        return MChannels;
    }
    void SetPlaybackRate(float InRate) noexcept;
    [[nodiscard]] float PlaybackRate() const noexcept
    {
        return MRate;
    }
    [[nodiscard]] std::uint32_t AvailableFrames() const noexcept
    {
        return MFrameCount;
    }
    [[nodiscard]] std::uint64_t AudioClockNs() const noexcept;
    [[nodiscard]] std::uint64_t LastPushedTimestampNs() const noexcept
    {
        return MLastPushedTimestampNs;
    }

    // Decodes the next Opus packet. When the ring cannot hold the whole frame,
    // the decoded frame stays in the internal pending buffer and Buffered is
    // returned; the next call flushes it before decoding more.
    [[nodiscard]] AudioPumpStatus PumpOne(std::string* InError = nullptr);

    // Injects interleaved PCM (host-side decoder or tests). The timestamp is the
    // packet's presentation time; the first accepted push seeds the clock.
    [[nodiscard]] bool PushPcm(std::span<const float> InInterleaved, std::uint64_t InTimestampNs);

    // Copies up to InMaxFrames interleaved frames. Returns the number consumed.
    [[nodiscard]] std::uint32_t Read(std::span<float> OutInterleaved) noexcept;

private:
    [[nodiscard]] bool PushPending() noexcept;
    [[nodiscard]] std::uint32_t CopyFrames(std::span<float> OutInterleaved, std::uint32_t InFrames) noexcept;
    void ResetState() noexcept;

    WebmDemuxer MDemuxer{};
    VideoAudioDecoder MDecoder{};
    std::array<float, kMaxRingFrames * kMaxChannels> MRing{};
    std::array<float, kMaxOpusFrameSamples * kMaxChannels> MPending{};
    std::array<float, kMaxRingFrames * kMaxChannels> MResampleScratch{};
    std::uint32_t MWriteFrame = 0;
    std::uint32_t MReadFrame = 0;
    std::uint32_t MFrameCount = 0;
    std::uint32_t MPendingFrames = 0;
    std::uint64_t MPendingTimestampNs = 0;
    std::uint64_t MFirstTimestampNs = 0;
    std::uint64_t MPlayedFrames = 0;
    std::uint64_t MLastPushedTimestampNs = 0;
    std::uint64_t MSeekTargetNs = 0;
    std::uint32_t MSampleRate = 0;
    std::uint32_t MChannels = 0;
    float MRate = 1.0f;
    bool MOpened = false;
    bool MClockSeeded = false;
};

}
