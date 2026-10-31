// FemboyEngine2D - audio cooking: WAV/OGG validation and .fpcm assembly.
#include "AudioCook.hpp"

#include "Engine/Assets/AudioClipFormat.hpp"
#include "Engine/Audio/OggOpus.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace Fby::Cook
{
namespace
{
namespace fs = std::filesystem;

void SetError(std::string* InError, std::string InMessage)
{
    if (InError != nullptr)
    {
        *InError = std::move(InMessage);
    }
}

[[nodiscard]] std::string LowerExtension(const fs::path& InPath)
{
    std::string ScopedExtension = InPath.extension().string();
    std::transform(ScopedExtension.begin(), ScopedExtension.end(), ScopedExtension.begin(),
                   [](unsigned char InCharacter) { return static_cast<char>(std::tolower(InCharacter)); });
    return ScopedExtension;
}

[[nodiscard]] bool ReadFileBytes(const fs::path& InPath, std::vector<std::byte>* InBytes)
{
    std::ifstream ScopedStream(InPath, std::ios::binary);
    if (!ScopedStream)
    {
        return false;
    }
    ScopedStream.seekg(0, std::ios::end);
    const std::streamoff ScopedSize = ScopedStream.tellg();
    if (ScopedSize < 0)
    {
        return false;
    }
    ScopedStream.seekg(0, std::ios::beg);
    InBytes->resize(static_cast<std::size_t>(ScopedSize));
    if (ScopedSize > 0)
    {
        ScopedStream.read(reinterpret_cast<char*>(InBytes->data()), static_cast<std::streamsize>(ScopedSize));
    }
    return static_cast<bool>(ScopedStream);
}

[[nodiscard]] std::uint8_t U8(const std::byte* InBytes, std::size_t InIndex) noexcept
{
    return std::to_integer<std::uint8_t>(InBytes[InIndex]);
}

[[nodiscard]] std::uint16_t ReadU16(const std::byte* InBytes, std::size_t InIndex) noexcept
{
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(U8(InBytes, InIndex)) |
        static_cast<std::uint16_t>(static_cast<std::uint16_t>(U8(InBytes, InIndex + 1)) << 8));
}

[[nodiscard]] std::uint32_t ReadU32(const std::byte* InBytes, std::size_t InIndex) noexcept
{
    return static_cast<std::uint32_t>(U8(InBytes, InIndex)) |
           (static_cast<std::uint32_t>(U8(InBytes, InIndex + 1)) << 8) |
           (static_cast<std::uint32_t>(U8(InBytes, InIndex + 2)) << 16) |
           (static_cast<std::uint32_t>(U8(InBytes, InIndex + 3)) << 24);
}

[[nodiscard]] bool HasExtension(const std::string& InExtension,
                                std::initializer_list<const char*> InCandidates) noexcept
{
    for (const char* ScopedCandidate : InCandidates)
    {
        if (InExtension == ScopedCandidate)
        {
            return true;
        }
    }
    return false;
}

// MP3 detection is a whitelist's second line of defence: ID3v2 tag or an MPEG
[[nodiscard]] bool LooksLikeMp3(std::span<const std::byte> InBytes) noexcept
{
    if (InBytes.size() >= 3 && std::memcmp(InBytes.data(), "ID3", 3) == 0)
    {
        return true;
    }
    if (InBytes.size() >= 2)
    {
        const std::uint8_t ScopedFirst = U8(InBytes.data(), 0);
        const std::uint8_t ScopedSecond = U8(InBytes.data(), 1);
        if (ScopedFirst == 0xFF && (ScopedSecond & 0xE0u) == 0xE0u)
        {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool IsRiffWave(std::span<const std::byte> InBytes) noexcept
{
    return InBytes.size() >= 12 && std::memcmp(InBytes.data(), "RIFF", 4) == 0 &&
           std::memcmp(InBytes.data() + 8, "WAVE", 4) == 0;
}

[[nodiscard]] bool IsOgg(std::span<const std::byte> InBytes) noexcept
{
    return InBytes.size() >= 4 && std::memcmp(InBytes.data(), "OggS", 4) == 0;
}

// WAV
struct WavDescription
{
    Assets::PcmFormat format = Assets::PcmFormat::PcmS16;
    std::uint16_t channels = 0;
    std::uint32_t sampleRate = 0;
    std::uint32_t frameCount = 0;
    std::span<const std::byte> samples;
};

[[nodiscard]] bool ParseWav(std::span<const std::byte> InBytes, WavDescription* InDescription, std::string* InError)
{
    if (!IsRiffWave(InBytes))
    {
        SetError(InError, "WAV source is not a RIFF/WAVE file");
        return false;
    }

    bool ScopedHaveFormat = false;
    bool ScopedHaveData = false;
    std::uint16_t ScopedAudioFormat = 0;
    std::uint16_t ScopedChannels = 0;
    std::uint16_t ScopedBlockAlign = 0;
    std::uint16_t ScopedBitsPerSample = 0;
    std::uint32_t ScopedSampleRate = 0;
    std::span<const std::byte> ScopedData;

    std::size_t ScopedOffset = 12;
    while (ScopedOffset + 8 <= InBytes.size())
    {
        const std::byte* ScopedChunk = InBytes.data() + ScopedOffset;
        const std::uint64_t ScopedChunkSize = ReadU32(ScopedChunk, 4);
        const std::size_t ScopedDataOffset = ScopedOffset + 8;
        if (ScopedChunkSize > InBytes.size() - ScopedDataOffset)
        {
            SetError(InError, "WAV chunk points past the end of the file");
            return false;
        }
        const std::string_view ScopedId(reinterpret_cast<const char*>(ScopedChunk), 4);

        if (ScopedId == "fmt ")
        {
            if (ScopedChunkSize < 16)
            {
                SetError(InError, "WAV fmt chunk is truncated");
                return false;
            }
            ScopedAudioFormat = ReadU16(InBytes.data(), ScopedDataOffset + 0);
            ScopedChannels = ReadU16(InBytes.data(), ScopedDataOffset + 2);
            ScopedSampleRate = ReadU32(InBytes.data(), ScopedDataOffset + 4);
            ScopedBlockAlign = ReadU16(InBytes.data(), ScopedDataOffset + 12);
            ScopedBitsPerSample = ReadU16(InBytes.data(), ScopedDataOffset + 14);
            ScopedHaveFormat = true;
        }
        else if (ScopedId == "data")
        {
            ScopedData = InBytes.subspan(ScopedDataOffset, static_cast<std::size_t>(ScopedChunkSize));
            ScopedHaveData = true;
            if (ScopedHaveFormat)
            {
                // The fmt chunk already described the data
                break;
            }
        }

        // Chunks are word-aligned: an odd size is followed by one pad byte.
        ScopedOffset = ScopedDataOffset + static_cast<std::size_t>(ScopedChunkSize) + (ScopedChunkSize & 1u);
    }

    if (!ScopedHaveFormat)
    {
        SetError(InError, "WAV file has no fmt chunk");
        return false;
    }
    if (ScopedAudioFormat != 1)
    {
        SetError(InError, "WAV is not uncompressed PCM (format tag " + std::to_string(ScopedAudioFormat) +
                              "); only PCM 16/24-bit is supported");
        return false;
    }
    if (ScopedBitsPerSample != 16 && ScopedBitsPerSample != 24)
    {
        SetError(InError, "WAV must be 16 or 24-bit PCM");
        return false;
    }
    if (ScopedChannels == 0 || ScopedChannels > Assets::kAudioClipMaxChannels)
    {
        SetError(InError, "WAV channel count is out of range");
        return false;
    }
    if (ScopedSampleRate < Assets::kAudioClipMinSampleRate || ScopedSampleRate > Assets::kAudioClipMaxSampleRate)
    {
        SetError(InError, "WAV sample rate is out of range");
        return false;
    }
    const std::uint32_t ScopedExpectedBlockAlign =
        static_cast<std::uint32_t>(ScopedChannels) * (ScopedBitsPerSample / 8u);
    if (ScopedBlockAlign != ScopedExpectedBlockAlign)
    {
        SetError(InError, "WAV block alignment does not match its channel/bit depth");
        return false;
    }
    if (!ScopedHaveData)
    {
        SetError(InError, "WAV file has no data chunk");
        return false;
    }
    if (ScopedData.empty() || ScopedData.size() % ScopedExpectedBlockAlign != 0)
    {
        SetError(InError, "WAV data chunk is empty or not a whole number of frames");
        return false;
    }

    InDescription->format = ScopedBitsPerSample == 16 ? Assets::PcmFormat::PcmS16 : Assets::PcmFormat::PcmS24;
    InDescription->channels = ScopedChannels;
    InDescription->sampleRate = ScopedSampleRate;
    InDescription->frameCount = static_cast<std::uint32_t>(ScopedData.size() / ScopedExpectedBlockAlign);
    InDescription->samples = ScopedData;
    return true;
}

[[nodiscard]] bool CookWav(std::span<const std::byte> InBytes, std::vector<std::byte>* Output, std::string* InError)
{
    WavDescription ScopedDescription;
    if (!ParseWav(InBytes, &ScopedDescription, InError))
    {
        return false;
    }
    // Samples are copied verbatim; the runtime reads int16/int24 directly with
    return Assets::AppendCookedAudio(*Output, ScopedDescription.format, ScopedDescription.channels,
                                     ScopedDescription.sampleRate, ScopedDescription.frameCount, 0,
                                     ScopedDescription.samples, InError);
}

// Ogg (Opus or Vorbis)
[[nodiscard]] bool CookOgg(std::span<const std::byte> InBytes, std::vector<std::byte>* Output, std::string* InError)
{
    Audio::OggPacketReader ScopedPackets;
    ScopedPackets.Reset(InBytes);
    Audio::OggPacket ScopedIdentification;
    if (!ScopedPackets.Next(&ScopedIdentification, InError))
    {
        if (InError != nullptr && InError->empty())
        {
            *InError = "Ogg source has no packets";
        }
        return false;
    }

    Assets::PcmFormat ScopedFormat = Assets::PcmFormat::OggOpus;
    std::uint32_t ScopedChannels = 0;
    std::uint32_t ScopedSampleRate = 0;
    std::uint32_t ScopedFlags = Assets::kAudioFlagStream;

    if (ScopedIdentification.bytes.size() >= 8 && std::memcmp(ScopedIdentification.bytes.data(), "OpusHead", 8) == 0)
    {
        Audio::OpusHeadInfo ScopedHead;
        if (!Audio::ParseOpusHead(ScopedIdentification.bytes, &ScopedHead, InError))
        {
            return false;
        }
        ScopedFormat = Assets::PcmFormat::OggOpus;
        ScopedChannels = ScopedHead.channelCount;
        // Opus always decodes at 48 kHz; the OpusHead input rate is the source's
        ScopedSampleRate = Audio::kOggOpusSampleRate;
        ScopedFlags |= Assets::kAudioFlagOpus;
    }
    else if (ScopedIdentification.bytes.size() >= 7 && U8(ScopedIdentification.bytes.data(), 0) == 0x01 &&
             std::memcmp(ScopedIdentification.bytes.data() + 1, "vorbis", 6) == 0)
    {
        Audio::VorbisIdentInfo ScopedIdent;
        if (!Audio::ParseVorbisIdent(ScopedIdentification.bytes, &ScopedIdent, InError))
        {
            return false;
        }
        ScopedFormat = Assets::PcmFormat::OggVorbis;
        ScopedChannels = ScopedIdent.channels;
        ScopedSampleRate = ScopedIdent.sampleRate;
    }
    else
    {
        SetError(InError, "Ogg source is neither Opus nor Vorbis");
        return false;
    }

    // Validate the whole page stream, not just the first page: a truncated or
    Audio::OggPageReader ScopedPages;
    ScopedPages.Reset(InBytes);
    Audio::OggPage ScopedPage;
    std::uint32_t ScopedPageCount = 0;
    while (ScopedPages.Next(&ScopedPage, nullptr))
    {
        ++ScopedPageCount;
    }
    if (ScopedPageCount == 0)
    {
        SetError(InError, "Ogg source has no valid pages");
        return false;
    }

    // The container is carried verbatim: decode happens at load/stream time.
    return Assets::AppendCookedAudio(*Output, ScopedFormat, ScopedChannels, ScopedSampleRate, 0, ScopedFlags, InBytes,
                                     InError);
}
}

bool CookAudioFile(const fs::path& InSource, std::vector<std::byte>* Output, std::string* InError)
{
    if (Output == nullptr)
    {
        SetError(InError, "audio cook received a null output buffer");
        return false;
    }
    Output->clear();
    if (InError != nullptr)
    {
        InError->clear();
    }

    const std::string ScopedExtension = LowerExtension(InSource);
    if (HasExtension(ScopedExtension, {".mp3", ".mp2", ".mpga", ".m4a", ".aac"}))
    {
        SetError(InError, "MP3 is not supported by FemboyEngine; cook WAV 16/24-bit, Ogg Opus or Ogg Vorbis instead");
        return false;
    }

    std::vector<std::byte> ScopedBytes;
    if (!ReadFileBytes(InSource, &ScopedBytes) || ScopedBytes.empty())
    {
        SetError(InError, "cannot read audio source " + InSource.string());
        return false;
    }
    if (LooksLikeMp3(ScopedBytes))
    {
        SetError(InError,
                 "audio source looks like MP3 (ID3 tag or MPEG frame sync); MP3 is not supported even when renamed");
        return false;
    }

    if (IsRiffWave(ScopedBytes))
    {
        return CookWav(ScopedBytes, Output, InError);
    }
    if (IsOgg(ScopedBytes))
    {
        return CookOgg(ScopedBytes, Output, InError);
    }
    SetError(InError, "unsupported audio source: only WAV 16/24-bit PCM and Ogg Opus/Vorbis are cooked (" +
                          InSource.filename().string() + ")");
    return false;
}
}
