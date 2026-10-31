// FemboyEngine2D - .fpcm validation and header writing.
#include "Engine/Assets/AudioClipFormat.hpp"

#include <cstring>
#include <limits>

namespace Fby::Assets
{
namespace
{
void SetError(std::string* InError, const char* InMessage)
{
    if (InError != nullptr)
    {
        *InError = InMessage;
    }
}

[[nodiscard]] bool ValidFormat(std::uint32_t InValue) noexcept
{
    return InValue <= static_cast<std::uint32_t>(PcmFormat::OggVorbis);
}

[[nodiscard]] bool ValidChannels(std::uint32_t InValue) noexcept
{
    return InValue > 0 && InValue <= kAudioClipMaxChannels;
}

[[nodiscard]] bool ValidSampleRate(std::uint32_t InValue) noexcept
{
    return InValue >= kAudioClipMinSampleRate && InValue <= kAudioClipMaxSampleRate;
}

// The descriptor-level validation shared by parsing and appending: everything
[[nodiscard]] bool ValidateDescriptor(PcmFormat InFormat, std::uint32_t InChannels, std::uint32_t InSampleRate,
                                      std::uint32_t InFrameCount, std::uint32_t InFlags, std::uint64_t InPayloadBytes,
                                      std::string* InError)
{
    if (!ValidChannels(InChannels))
    {
        SetError(InError, "audio clip channel count is out of range");
        return false;
    }
    if (!ValidSampleRate(InSampleRate))
    {
        SetError(InError, "audio clip sample rate is out of range");
        return false;
    }
    const bool ScopedCompressed = IsCompressed(InFormat);
    if (!ScopedCompressed && InFrameCount == 0)
    {
        SetError(InError, "audio clip has no frames");
        return false;
    }
    if (ScopedCompressed && (InFlags & kAudioFlagStream) == 0)
    {
        SetError(InError, "compressed audio clip is missing the stream flag");
        return false;
    }
    const std::uint64_t ScopedExpected =
        ScopedCompressed ? InPayloadBytes : AudioByteSize(InFormat, InChannels, InFrameCount);
    if (ScopedExpected != InPayloadBytes || ScopedExpected > kAudioClipMaxPayloadBytes)
    {
        SetError(InError, "audio clip payload size does not match its header");
        return false;
    }
    return true;
}
}

std::optional<CookedAudioView> ParseCookedAudio(std::span<const std::byte> InBytes, std::string* InError)
{
    if (InBytes.size() < sizeof(AudioClipHeader))
    {
        SetError(InError, "audio clip payload is truncated (no complete header)");
        return std::nullopt;
    }

    AudioClipHeader ScopedHeader{};
    std::memcpy(&ScopedHeader, InBytes.data(), sizeof(ScopedHeader));
    if (std::memcmp(ScopedHeader.magic, kAudioClipMagic, sizeof(kAudioClipMagic)) != 0)
    {
        SetError(InError, "audio clip payload has a bad magic (not a cooked .fpcm)");
        return std::nullopt;
    }
    if (ScopedHeader.version != kAudioClipVersion)
    {
        SetError(InError, "audio clip payload uses an unsupported .fpcm version");
        return std::nullopt;
    }
    if (!ValidFormat(ScopedHeader.format))
    {
        SetError(InError, "audio clip payload uses an unknown PcmFormat");
        return std::nullopt;
    }
    if (ScopedHeader.reserved != 0)
    {
        SetError(InError, "audio clip payload has a non-zero reserved field");
        return std::nullopt;
    }

    const auto ScopedFormat = static_cast<PcmFormat>(ScopedHeader.format);
    const std::uint64_t ScopedPayloadSize =
        static_cast<std::uint64_t>(InBytes.size()) - static_cast<std::uint64_t>(sizeof(AudioClipHeader));
    if (ScopedPayloadSize > std::numeric_limits<std::uint32_t>::max() ||
        ScopedHeader.dataBytes != static_cast<std::uint32_t>(ScopedPayloadSize))
    {
        SetError(InError, "audio clip payload size does not match its header");
        return std::nullopt;
    }
    if (!ValidateDescriptor(ScopedFormat, ScopedHeader.channels, ScopedHeader.sampleRate, ScopedHeader.frameCount,
                            ScopedHeader.flags, ScopedPayloadSize, InError))
    {
        return std::nullopt;
    }
    if (!IsCompressed(ScopedFormat) && ScopedHeader.frameCount > 0 && ScopedHeader.channels > 0)
    {
        // A truncated interleaved block would let the mixer read past the span;
        const std::uint64_t ScopedFrameBytes =
            static_cast<std::uint64_t>(BytesPerSample(ScopedFormat)) * ScopedHeader.channels;
        if (ScopedFrameBytes == 0 || ScopedPayloadSize % ScopedFrameBytes != 0 ||
            ScopedPayloadSize / ScopedFrameBytes != ScopedHeader.frameCount)
        {
            SetError(InError, "audio clip payload is not a whole number of frames");
            return std::nullopt;
        }
    }

    CookedAudioView ScopedView;
    ScopedView.format = ScopedFormat;
    ScopedView.channels = ScopedHeader.channels;
    ScopedView.sampleRate = ScopedHeader.sampleRate;
    ScopedView.frameCount = ScopedHeader.frameCount;
    ScopedView.flags = ScopedHeader.flags;
    ScopedView.samples = InBytes.subspan(sizeof(AudioClipHeader));
    ScopedView.whole = InBytes;
    return ScopedView;
}

bool AppendCookedAudio(std::vector<std::byte>& Out, PcmFormat InFormat, std::uint32_t InChannels,
                       std::uint32_t InSampleRate, std::uint32_t InFrameCount, std::uint32_t InFlags,
                       std::span<const std::byte> InPayload, std::string* InError)
{
    if (!ValidFormat(static_cast<std::uint32_t>(InFormat)))
    {
        SetError(InError, "cooked audio clip uses an unknown PcmFormat");
        return false;
    }
    if (InPayload.size() > std::numeric_limits<std::uint32_t>::max())
    {
        SetError(InError, "cooked audio clip payload exceeds the 4 GiB .fpcm limit");
        return false;
    }
    if (!ValidateDescriptor(InFormat, InChannels, InSampleRate, InFrameCount, InFlags,
                            static_cast<std::uint64_t>(InPayload.size()), InError))
    {
        return false;
    }

    AudioClipHeader ScopedHeader{};
    std::memcpy(ScopedHeader.magic, kAudioClipMagic, sizeof(kAudioClipMagic));
    ScopedHeader.version = kAudioClipVersion;
    ScopedHeader.format = static_cast<std::uint32_t>(InFormat);
    ScopedHeader.channels = InChannels;
    ScopedHeader.sampleRate = InSampleRate;
    ScopedHeader.frameCount = InFrameCount;
    ScopedHeader.flags = InFlags;
    ScopedHeader.dataBytes = static_cast<std::uint32_t>(InPayload.size());
    ScopedHeader.reserved = 0;

    Out.resize(sizeof(AudioClipHeader) + InPayload.size());
    std::memcpy(Out.data(), &ScopedHeader, sizeof(ScopedHeader));
    if (!InPayload.empty())
    {
        std::memcpy(Out.data() + sizeof(AudioClipHeader), InPayload.data(), InPayload.size());
    }
    return true;
}
}
