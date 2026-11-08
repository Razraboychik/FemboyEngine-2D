// FemboyEngine2D - Opus audio decoding for video tracks.
#pragma once

#include <cstdint>
#include <span>
#include <string>

namespace Fby::Video
{

inline constexpr std::uint32_t kMaxOpusFrameSamples = 5760;

// Decodes the A_OPUS packets extracted from WebM. One instance per stream; the
// decoder is created on the cold path and only Decode() runs on the audio clock.
class VideoAudioDecoder
{
public:
    VideoAudioDecoder() = default;
    ~VideoAudioDecoder();

    VideoAudioDecoder(const VideoAudioDecoder&) = delete;
    VideoAudioDecoder& operator=(const VideoAudioDecoder&) = delete;

    [[nodiscard]] bool Initialize(std::uint32_t InSampleRate, std::uint32_t InChannels, std::string* InError = nullptr);
    void Shutdown() noexcept;
    [[nodiscard]] bool Ready() const noexcept
    {
        return MDecoder != nullptr;
    }
    [[nodiscard]] std::uint32_t SampleRate() const noexcept
    {
        return MSampleRate;
    }
    [[nodiscard]] std::uint32_t Channels() const noexcept
    {
        return MChannels;
    }

    // Decodes one packet into interleaved float PCM. OutFrames receives the
    // decoded frame count; the destination must hold at least
    // kMaxOpusFrameSamples * channels floats. Returns false for a malformed
    // packet; the error text is optional.
    [[nodiscard]] bool Decode(std::span<const std::uint8_t> InPacket, std::span<float> OutPcm, std::uint32_t* OutFrames,
                              std::string* InError = nullptr) noexcept;

private:
    void* MDecoder = nullptr;
    std::uint32_t MSampleRate = 0;
    std::uint32_t MChannels = 0;
};

}
