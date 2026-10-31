// FemboyEngine2D - Ogg container + Opus decode helpers.
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace Fby::Audio
{

// A packet that does not fit here is not a music packet: Opus audio packets are
inline constexpr std::uint32_t kOggMaxPacketBytes = 16384;
// Every page header is 27 bytes: capture pattern, version, flags, granule,
inline constexpr std::uint32_t kOggPageHeaderBytes = 27;
inline constexpr std::uint32_t kOggOpusSampleRate = 48000;
// The mixer is stereo, so the decode path accepts mono and stereo Opus only.
inline constexpr std::uint32_t kOggMaxStreamChannels = 2;
// Opus can emit 120 ms per packet: 5760 samples at 48 kHz.
inline constexpr std::uint32_t kOggMaxDecodeFrameSamples = 5760;
// Decode-ahead ring: 16384 stereo frames = ~341 ms at 48 kHz.
inline constexpr std::uint32_t kOggRingFrames = 16384;
// Whole-track fallback cap: one minute of 48 kHz stereo f32 (~11.5 MiB).
inline constexpr std::uint32_t kOggMaxWholeTrackFrames = kOggOpusSampleRate * 60u;

inline constexpr std::uint8_t kOggFlagContinued = 0x01;
inline constexpr std::uint8_t kOggFlagBeginningOfStream = 0x02;
inline constexpr std::uint8_t kOggFlagEndOfStream = 0x04;

// A validated view of one page. `body` and `whole` point into the caller's
struct OggPage
{
    std::uint8_t version = 0;
    std::uint8_t headerType = 0;
    std::uint64_t granulePosition = 0;
    std::uint32_t serialNumber = 0;
    std::uint32_t sequence = 0;
    std::uint32_t checksum = 0;
    std::uint8_t segmentCount = 0;
    std::array<std::uint8_t, 255> lace{};
    std::span<const std::byte> body;
    // Header + body, used for the CRC
    std::span<const std::byte> whole;
};

// Ogg's CRC-32: polynomial 0x04C11DB7, initial value 0, no reflection, no final
[[nodiscard]] std::uint32_t OggCrc32(std::span<const std::byte> InBytes) noexcept;
// CRC over a complete page with the checksum field (bytes 22..25) treated as
[[nodiscard]] std::uint32_t OggPageCrc(std::span<const std::byte> InPage) noexcept;

// Walks the page sequence. Every page is validated: capture pattern, version,
class OggPageReader
{
public:
    void Reset(std::span<const std::byte> InBytes) noexcept;
    [[nodiscard]] bool Next(OggPage* InPage, std::string* InError = nullptr) noexcept;
    [[nodiscard]] bool AtEnd() const noexcept
    {
        return MOffset >= MData.size();
    }
    [[nodiscard]] std::size_t Offset() const noexcept
    {
        return MOffset;
    }

private:
    std::span<const std::byte> MData{};
    std::size_t MOffset = 0;
};

struct OggPacket
{
    std::span<const std::byte> bytes;
    std::uint64_t granulePosition = 0;
    std::uint32_t serialNumber = 0;
    bool beginningOfStream = false;
    bool endOfStream = false;
};

// Reassembles packets from pages. The assembled packet is copied into an
class OggPacketReader
{
public:
    void Reset(std::span<const std::byte> InBytes) noexcept;
    [[nodiscard]] bool Next(OggPacket* InPacket, std::string* InError = nullptr) noexcept;
    [[nodiscard]] bool AtEnd() const noexcept;

private:
    OggPageReader MPages{};
    OggPage MPage{};
    bool MHavePage = false;
    bool MFirstPacket = true;
    bool MAssembling = false;
    std::uint16_t MSegmentIndex = 0;
    std::size_t MPageBodyOffset = 0;
    std::size_t MBufferSize = 0;
    std::array<std::byte, kOggMaxPacketBytes> MBuffer{};
};

// The Opus ID header. `preSkip` samples at 48 kHz must be discarded after
struct OpusHeadInfo
{
    std::uint8_t version = 0;
    std::uint8_t channelCount = 0;
    std::uint16_t preSkip = 0;
    std::uint32_t inputSampleRate = 0;
    std::int16_t outputGain = 0;
    std::uint8_t mappingFamily = 0;
};

[[nodiscard]] bool ParseOpusHead(std::span<const std::byte> InPacket, OpusHeadInfo* InInfo,
                                 std::string* InError = nullptr);
[[nodiscard]] bool ValidateOpusTags(std::span<const std::byte> InPacket, std::string* InError = nullptr);

// Vorbis identification header: enough to cook the metadata (channels, rate).
struct VorbisIdentInfo
{
    std::uint32_t sampleRate = 0;
    std::uint8_t channels = 0;
};

[[nodiscard]] bool ParseVorbisIdent(std::span<const std::byte> InPacket, VorbisIdentInfo* InInfo,
                                    std::string* InError = nullptr);

// OggOpusStream - real-time Opus decode ahead of the mixer.
class OggOpusStream
{
public:
    static constexpr std::uint32_t kRingFrameCapacity = kOggRingFrames;

    OggOpusStream() = default;
    ~OggOpusStream();

    OggOpusStream(const OggOpusStream&) = delete;
    OggOpusStream& operator=(const OggOpusStream&) = delete;

    [[nodiscard]] bool Open(std::span<const std::byte> InOggBytes, std::string* InError = nullptr);
    [[nodiscard]] bool Start();
    void Stop() noexcept;

    // Pops up to `frameCount` interleaved stereo frames. Returns how many were
    [[nodiscard]] std::uint32_t Read(float* InInterleavedStereo, std::uint32_t InFrameCount) noexcept;

    [[nodiscard]] std::uint32_t AvailableFrames() const noexcept;
    [[nodiscard]] std::uint32_t Channels() const noexcept
    {
        return MChannels;
    }
    [[nodiscard]] std::uint32_t SampleRate() const noexcept
    {
        return kOggOpusSampleRate;
    }
    [[nodiscard]] bool IsOpen() const noexcept
    {
        return MOpen;
    }
    [[nodiscard]] bool IsFinished() const noexcept
    {
        return MFinished.load(std::memory_order_acquire);
    }
    [[nodiscard]] std::uint64_t DecodedFrames() const noexcept
    {
        return MDecodedFrames.load(std::memory_order_relaxed);
    }

private:
    void DecodeLoop();
    void DestroyDecoder() noexcept;

    // OpusDecoder*, opus.h stays out of this header
    void* MDecoder = nullptr;
    std::span<const std::byte> MOgg{};
    OggPacketReader MPackets{};
    bool MOpen = false;
    bool MStarted = false;
    std::uint32_t MChannels = 2;
    std::uint32_t MPreSkipRemaining = 0;

    std::unique_ptr<float[]> MRing{};
    // One decoded Opus packet, stereo
    std::unique_ptr<float[]> MScratch{};
    // Consumer
    alignas(64) std::atomic<std::uint32_t> MRingHead{0};
    // Producer
    alignas(64) std::atomic<std::uint32_t> MRingTail{0};
    std::atomic<bool> MStop{false};
    std::atomic<bool> MFinished{false};
    std::atomic<std::uint64_t> MDecodedFrames{0};
    std::thread MDecodeThread{};
};

// Whole-track decode into a stable f32 stereo buffer. Frames past `maxFrames`
[[nodiscard]] bool DecodeOggOpusToStereo(std::span<const std::byte> InOggBytes, std::uint32_t InMaxFrames,
                                         std::vector<float>* InInterleavedStereo, std::uint32_t* OutFrames,
                                         std::string* InError = nullptr);
}
