// FemboyEngine2D - Opus voice codec wrappers.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace Fby
{

inline constexpr std::uint32_t kVoiceSampleRate = 48000;
inline constexpr std::uint32_t kVoiceFrameSamples = 960;
inline constexpr std::uint32_t kVoiceMaxPacketBytes = 512;

struct VoiceCodecConfig
{
    std::uint32_t Bitrate = 24000;
    int Complexity = 6;
    bool InbandFec = true;
    bool Dtx = false;
    // Loss estimate passed to the encoder; Opus embeds FEC only when this is
    // non-zero, so a decoder can reconstruct one lost frame from the next one.
    int PacketLossPercent = 10;
};

// One encoder per outgoing stream. Allocation happens in Initialize (cold path).
class VoiceEncoder
{
public:
    VoiceEncoder() = default;
    ~VoiceEncoder();

    VoiceEncoder(const VoiceEncoder&) = delete;
    VoiceEncoder& operator=(const VoiceEncoder&) = delete;

    [[nodiscard]] bool Initialize(const VoiceCodecConfig& InConfig = {}) noexcept;
    void Shutdown() noexcept;
    [[nodiscard]] bool Ready() const noexcept
    {
        return MEncoder != nullptr;
    }
    // Encodes exactly kVoiceFrameSamples mono f32 samples. Returns the packet
    // size, or 0 when the encoder is unavailable or the input is the wrong size.
    [[nodiscard]] std::uint32_t Encode(std::span<const float> InSamples, std::span<std::uint8_t> OutPacket) noexcept;

protected:
    void* MEncoder = nullptr;
};

// One decoder per incoming stream; loss concealment uses the codec's PLC.
class VoiceDecoder
{
public:
    VoiceDecoder() = default;
    ~VoiceDecoder();

    VoiceDecoder(const VoiceDecoder&) = delete;
    VoiceDecoder& operator=(const VoiceDecoder&) = delete;

    [[nodiscard]] bool Initialize() noexcept;
    void Shutdown() noexcept;
    [[nodiscard]] bool Ready() const noexcept
    {
        return MDecoder != nullptr;
    }
    [[nodiscard]] std::uint32_t Decode(std::span<const std::uint8_t> InPacket,
                                       std::span<float> OutSamples) noexcept;
    // Reconstructs the frame that was lost before `InPacket` using Opus in-band
    // FEC (decode_fec = 1). The packet itself is not consumed: the caller decodes
    // it normally afterwards. Returns 0 when FEC is not present.
    [[nodiscard]] std::uint32_t DecodeFec(std::span<const std::uint8_t> InPacket,
                                          std::span<float> OutSamples) noexcept;
    [[nodiscard]] std::uint32_t Conceal(std::span<float> OutSamples) noexcept;

protected:
    void* MDecoder = nullptr;
};
}
