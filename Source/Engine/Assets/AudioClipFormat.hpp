// FemboyEngine2D - cooked audio payload (.fpcm).
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace Fby::Assets
{

inline constexpr char kAudioClipMagic[8] = {'F', 'B', 'P', 'C', 'M', '0', '0', '1'};
inline constexpr std::uint32_t kAudioClipVersion = 1;
inline constexpr std::string_view kAudioClipExtension = ".fpcm";

// The runtime payload set is closed on purpose. A game that needs another codec
enum class PcmFormat : std::uint32_t
{
    // Signed little-endian PCM, exactly as the WAV data chunk stored it.
    PcmS16 = 0,
    PcmS24 = 1,
    // Float32 interleaved. Used by RegisterClip() callers and by decoded music.
    PcmF32 = 2,
    // A complete Ogg container. `frameCount` is 0: the number of frames is only
    OggOpus = 3,
    OggVorbis = 4,
};

inline constexpr std::uint32_t kAudioClipMaxChannels = 8;
inline constexpr std::uint32_t kAudioClipMinSampleRate = 4000;
inline constexpr std::uint32_t kAudioClipMaxSampleRate = 192000;
// A single cooked block is limited to 1 GiB of payload: bigger than any sound
inline constexpr std::uint64_t kAudioClipMaxPayloadBytes = 1ull << 30;

// The source was an Ogg container: the payload must be decoded before it can be
inline constexpr std::uint32_t kAudioFlagStream = 1u << 0;
// The source declares itself loopable (a loop hint a game may override).
inline constexpr std::uint32_t kAudioFlagLoop = 1u << 1;
// The source was Opus. Kept alongside kAudioFlagStream so an inspector can show
inline constexpr std::uint32_t kAudioFlagOpus = 1u << 2;

[[nodiscard]] constexpr bool IsCompressed(PcmFormat InFormat) noexcept
{
    return InFormat == PcmFormat::OggOpus || InFormat == PcmFormat::OggVorbis;
}

// Bytes per single sample of one channel. Compressed formats have no fixed
[[nodiscard]] constexpr std::uint32_t BytesPerSample(PcmFormat InFormat) noexcept
{
    switch (InFormat)
    {
        case PcmFormat::PcmS16:
            return 2;
        case PcmFormat::PcmS24:
            return 3;
        case PcmFormat::PcmF32:
            return 4;
        case PcmFormat::OggOpus:
        case PcmFormat::OggVorbis:
            return 0;
    }
    return 0;
}

[[nodiscard]] constexpr const char* PcmFormatName(PcmFormat InFormat) noexcept
{
    switch (InFormat)
    {
        case PcmFormat::PcmS16:
            return "pcm-s16";
        case PcmFormat::PcmS24:
            return "pcm-s24";
        case PcmFormat::PcmF32:
            return "pcm-f32";
        case PcmFormat::OggOpus:
            return "ogg-opus";
        case PcmFormat::OggVorbis:
            return "ogg-vorbis";
    }
    return "?";
}

struct AudioClipHeader
{
    char magic[8];
    std::uint32_t version;
    // PcmFormat
    std::uint32_t format;
    std::uint32_t channels;
    std::uint32_t sampleRate;
    // 0 for compressed containers
    std::uint32_t frameCount;
    // KAudioFlag*
    std::uint32_t flags;
    // Payload size, guards truncation
    std::uint32_t dataBytes;
    // Must be 0; keeps the header at 40 bytes
    std::uint32_t reserved;
};

static_assert(sizeof(AudioClipHeader) == 40, ".fpcm header is fixed at 40 bytes");
static_assert(std::is_trivially_copyable_v<AudioClipHeader>);

// Total payload size of a playable (non-container) block. Compressed blocks
[[nodiscard]] constexpr std::uint64_t AudioByteSize(PcmFormat InFormat, std::uint32_t InChannels,
                                                    std::uint32_t InFrameCount) noexcept
{
    return static_cast<std::uint64_t>(BytesPerSample(InFormat)) * InChannels * InFrameCount;
}

// A validated, zero-copy view of a cooked clip. `samples` points into the
struct CookedAudioView
{
    PcmFormat format = PcmFormat::PcmF32;
    std::uint32_t channels = 0;
    std::uint32_t sampleRate = 0;
    // 0 for compressed containers
    std::uint32_t frameCount = 0;
    std::uint32_t flags = 0;
    // Interleaved PCM, or the Ogg container
    std::span<const std::byte> samples;
    // Header + payload, for archive re-emit
    std::span<const std::byte> whole;
};

// Validates a .fpcm blob: magic, version, format, channel/sample-rate range,
[[nodiscard]] std::optional<CookedAudioView> ParseCookedAudio(std::span<const std::byte> InBytes,
                                                              std::string* InError = nullptr);

// Appends header + payload for the cooker. Cook-time only (it allocates); the
[[nodiscard]] bool AppendCookedAudio(std::vector<std::byte>& Out, PcmFormat InFormat, std::uint32_t InChannels,
                                     std::uint32_t InSampleRate, std::uint32_t InFrameCount, std::uint32_t InFlags,
                                     std::span<const std::byte> InPayload, std::string* InError = nullptr);
}
