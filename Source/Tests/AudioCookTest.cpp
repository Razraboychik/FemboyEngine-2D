// FemboyEngine2D - audio cooking tests.
#include "Tools/FempakCooker/AudioCook.hpp"

#include "Engine/Assets/AudioClipFormat.hpp"
#include "Engine/Audio/OggOpus.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <vector>

namespace
{
namespace fs = std::filesystem;
using Fby::Assets::AudioClipHeader;
using Fby::Assets::PcmFormat;

int GFailures = 0;

#define FBY_CHECK(condition)                                                                                           \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);                                  \
            ++GFailures;                                                                                              \
        }                                                                                                              \
    } while (false)

fs::path GTempRoot;

void AppendU16(std::vector<std::byte>& Out, std::uint16_t InValue)
{
    Out.push_back(static_cast<std::byte>(InValue & 0xFFu));
    Out.push_back(static_cast<std::byte>((InValue >> 8) & 0xFFu));
}

void AppendU32(std::vector<std::byte>& Out, std::uint32_t InValue)
{
    Out.push_back(static_cast<std::byte>(InValue & 0xFFu));
    Out.push_back(static_cast<std::byte>((InValue >> 8) & 0xFFu));
    Out.push_back(static_cast<std::byte>((InValue >> 16) & 0xFFu));
    Out.push_back(static_cast<std::byte>((InValue >> 24) & 0xFFu));
}

void AppendU64(std::vector<std::byte>& Out, std::uint64_t InValue)
{
    AppendU32(Out, static_cast<std::uint32_t>(InValue & 0xFFFFFFFFull));
    AppendU32(Out, static_cast<std::uint32_t>((InValue >> 32) & 0xFFFFFFFFull));
}

void AppendText(std::vector<std::byte>& Out, std::string_view InText)
{
    for (const char ScopedCharacter : InText)
    {
        Out.push_back(static_cast<std::byte>(static_cast<unsigned char>(ScopedCharacter)));
    }
}

[[nodiscard]] bool WriteFile(const fs::path& InPath, std::span<const std::byte> InBytes)
{
    std::ofstream ScopedStream(InPath, std::ios::binary | std::ios::trunc);
    if (!ScopedStream)
    {
        return false;
    }
    if (!InBytes.empty())
    {
        ScopedStream.write(reinterpret_cast<const char*>(InBytes.data()), static_cast<std::streamsize>(InBytes.size()));
    }
    return static_cast<bool>(ScopedStream);
}

// Synthetic WAV
struct WavFile
{
    std::vector<std::byte> bytes;
    std::vector<std::byte> pcm;
};

[[nodiscard]] WavFile BuildWav(std::uint16_t InBitsPerSample, std::uint16_t InChannels, std::uint32_t InSampleRate,
                               std::uint32_t InFrameCount)
{
    const std::uint32_t ScopedBytesPerSample = InBitsPerSample / 8u;
    const std::uint32_t ScopedBlockAlign = ScopedBytesPerSample * InChannels;
    const std::uint32_t ScopedDataBytes = ScopedBlockAlign * InFrameCount;

    WavFile ScopedWav;
    ScopedWav.pcm.resize(ScopedDataBytes);
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedDataBytes; ++ScopedIndex)
    {
        ScopedWav.pcm[ScopedIndex] = static_cast<std::byte>((ScopedIndex * 37u + 11u) & 0xFFu);
    }

    std::vector<std::byte>& ScopedOut = ScopedWav.bytes;
    AppendText(ScopedOut, "RIFF");
    AppendU32(ScopedOut, 4u + (8u + 16u) + (8u + ScopedDataBytes + (ScopedDataBytes & 1u)));
    AppendText(ScopedOut, "WAVE");
    AppendText(ScopedOut, "fmt ");
    AppendU32(ScopedOut, 16u);
    // WAVE_FORMAT_PCM
    AppendU16(ScopedOut, 1u);
    AppendU16(ScopedOut, InChannels);
    AppendU32(ScopedOut, InSampleRate);
    AppendU32(ScopedOut, InSampleRate * ScopedBlockAlign);
    AppendU16(ScopedOut, static_cast<std::uint16_t>(ScopedBlockAlign));
    AppendU16(ScopedOut, InBitsPerSample);
    AppendText(ScopedOut, "data");
    AppendU32(ScopedOut, ScopedDataBytes);
    ScopedOut.insert(ScopedOut.end(), ScopedWav.pcm.begin(), ScopedWav.pcm.end());
    if ((ScopedDataBytes & 1u) != 0)
    {
        ScopedOut.push_back(std::byte{0});
    }
    return ScopedWav;
}

// Synthetic Ogg
[[nodiscard]] std::vector<std::byte> BuildOggPage(std::uint8_t InHeaderType, std::uint64_t InGranule,
                                                  std::uint32_t InSerial, std::uint32_t InSequence,
                                                  const std::vector<std::uint8_t>& InLaces,
                                                  const std::vector<std::byte>& InBody)
{
    std::vector<std::byte> ScopedPage;
    ScopedPage.reserve(27 + InLaces.size() + InBody.size());
    AppendText(ScopedPage, "OggS");
    // Stream structure version
    ScopedPage.push_back(std::byte{0});
    ScopedPage.push_back(static_cast<std::byte>(InHeaderType));
    AppendU64(ScopedPage, InGranule);
    AppendU32(ScopedPage, InSerial);
    AppendU32(ScopedPage, InSequence);
    // Checksum placeholder
    AppendU32(ScopedPage, 0);
    ScopedPage.push_back(static_cast<std::byte>(InLaces.size()));
    for (const std::uint8_t ScopedLace : InLaces)
    {
        ScopedPage.push_back(static_cast<std::byte>(ScopedLace));
    }
    ScopedPage.insert(ScopedPage.end(), InBody.begin(), InBody.end());

    const std::uint32_t ScopedCrc =
        Fby::Audio::OggPageCrc(std::span<const std::byte>(ScopedPage.data(), ScopedPage.size()));
    ScopedPage[22] = static_cast<std::byte>(ScopedCrc & 0xFFu);
    ScopedPage[23] = static_cast<std::byte>((ScopedCrc >> 8) & 0xFFu);
    ScopedPage[24] = static_cast<std::byte>((ScopedCrc >> 16) & 0xFFu);
    ScopedPage[25] = static_cast<std::byte>((ScopedCrc >> 24) & 0xFFu);
    return ScopedPage;
}

[[nodiscard]] std::vector<std::byte> BuildOpusHead(std::uint8_t InChannels = 2, std::uint16_t InPreSkip = 312)
{
    std::vector<std::byte> ScopedPacket;
    AppendText(ScopedPacket, "OpusHead");
    // Version
    ScopedPacket.push_back(std::byte{1});
    ScopedPacket.push_back(static_cast<std::byte>(InChannels));
    AppendU16(ScopedPacket, InPreSkip);
    // Original input sample rate
    AppendU32(ScopedPacket, 44100);
    // Output gain
    AppendU16(ScopedPacket, 0);
    // Mapping family
    ScopedPacket.push_back(std::byte{0});
    return ScopedPacket;
}

[[nodiscard]] std::vector<std::byte> BuildOpusTags()
{
    std::vector<std::byte> ScopedPacket;
    AppendText(ScopedPacket, "OpusTags");
    // Vendor string length
    AppendU32(ScopedPacket, 4);
    AppendText(ScopedPacket, "fby!");
    // Comment count
    AppendU32(ScopedPacket, 0);
    return ScopedPacket;
}

[[nodiscard]] std::vector<std::byte> BuildVorbisIdent(std::uint32_t InSampleRate = 44100, std::uint8_t InChannels = 2)
{
    std::vector<std::byte> ScopedPacket;
    ScopedPacket.push_back(std::byte{0x01});
    AppendText(ScopedPacket, "vorbis");
    // Version
    AppendU32(ScopedPacket, 0);
    ScopedPacket.push_back(static_cast<std::byte>(InChannels));
    AppendU32(ScopedPacket, InSampleRate);
    // Bitrate maximum
    AppendU32(ScopedPacket, 0);
    // Bitrate nominal
    AppendU32(ScopedPacket, 0);
    // Bitrate minimum
    AppendU32(ScopedPacket, 0);
    // Block sizes
    ScopedPacket.push_back(std::byte{0xB8});
    // Framing flag
    ScopedPacket.push_back(std::byte{0x01});
    return ScopedPacket;
}

[[nodiscard]] std::vector<std::byte> BuildOggOpusStream()
{
    const std::vector<std::byte> ScopedHead = BuildOpusHead();
    const std::vector<std::byte> ScopedTags = BuildOpusTags();
    std::vector<std::byte> ScopedStream = BuildOggPage(Fby::Audio::kOggFlagBeginningOfStream, 312, 0x12345678u, 0,
                                                       {static_cast<std::uint8_t>(ScopedHead.size())}, ScopedHead);
    const std::vector<std::byte> ScopedTagsPage =
        BuildOggPage(0, 312, 0x12345678u, 1, {static_cast<std::uint8_t>(ScopedTags.size())}, ScopedTags);
    ScopedStream.insert(ScopedStream.end(), ScopedTagsPage.begin(), ScopedTagsPage.end());
    return ScopedStream;
}

// 1: WAV round-trip
void TestWavRoundTrip()
{
    const std::uint32_t ScopedFrames = 32;

    {
        const WavFile ScopedWav = BuildWav(16, 2, 48000, ScopedFrames);
        const fs::path ScopedPath = GTempRoot / "roundtrip16.wav";
        FBY_CHECK(WriteFile(ScopedPath, ScopedWav.bytes));

        std::vector<std::byte> ScopedCooked;
        std::string ScopedError;
        FBY_CHECK(Fby::Cook::CookAudioFile(ScopedPath, &ScopedCooked, &ScopedError));
        FBY_CHECK(ScopedError.empty());
        FBY_CHECK(ScopedCooked.size() == sizeof(AudioClipHeader) + ScopedWav.pcm.size());

        const auto ScopedView = Fby::Assets::ParseCookedAudio(ScopedCooked, &ScopedError);
        FBY_CHECK(ScopedView.has_value());
        FBY_CHECK(ScopedView->format == PcmFormat::PcmS16);
        FBY_CHECK(ScopedView->channels == 2);
        FBY_CHECK(ScopedView->sampleRate == 48000);
        FBY_CHECK(ScopedView->frameCount == ScopedFrames);
        FBY_CHECK(ScopedView->samples.size() == ScopedWav.pcm.size());
        // The cooked PCM is the source data chunk, byte for byte: no float
        FBY_CHECK(std::memcmp(ScopedView->samples.data(), ScopedWav.pcm.data(), ScopedWav.pcm.size()) == 0);
    }

    {
        const WavFile ScopedWav = BuildWav(24, 1, 44100, ScopedFrames);
        const fs::path ScopedPath = GTempRoot / "roundtrip24.wav";
        FBY_CHECK(WriteFile(ScopedPath, ScopedWav.bytes));

        std::vector<std::byte> ScopedCooked;
        std::string ScopedError;
        FBY_CHECK(Fby::Cook::CookAudioFile(ScopedPath, &ScopedCooked, &ScopedError));
        FBY_CHECK(ScopedCooked.size() == sizeof(AudioClipHeader) + ScopedWav.pcm.size());

        const auto ScopedView = Fby::Assets::ParseCookedAudio(ScopedCooked, &ScopedError);
        FBY_CHECK(ScopedView.has_value());
        FBY_CHECK(ScopedView->format == PcmFormat::PcmS24);
        FBY_CHECK(ScopedView->channels == 1);
        FBY_CHECK(ScopedView->sampleRate == 44100);
        FBY_CHECK(ScopedView->frameCount == ScopedFrames);
        FBY_CHECK(std::memcmp(ScopedView->samples.data(), ScopedWav.pcm.data(), ScopedWav.pcm.size()) == 0);
    }

    // An unsupported WAV variant (8-bit) must be refused.
    {
        WavFile ScopedWav = BuildWav(16, 1, 48000, 8);
        // Patch the bit depth field in the fmt chunk: offset 12 (id) + 8 (size)
        const std::uint16_t ScopedEightBits = 8;
        ScopedWav.bytes[34] = static_cast<std::byte>(ScopedEightBits & 0xFFu);
        ScopedWav.bytes[35] = static_cast<std::byte>((ScopedEightBits >> 8) & 0xFFu);
        const fs::path ScopedPath = GTempRoot / "eightbit.wav";
        FBY_CHECK(WriteFile(ScopedPath, ScopedWav.bytes));
        std::vector<std::byte> ScopedCooked;
        std::string ScopedError;
        FBY_CHECK(!Fby::Cook::CookAudioFile(ScopedPath, &ScopedCooked, &ScopedError));
        FBY_CHECK(!ScopedError.empty());
    }
}

// 2: .fpcm validation
void TestCookedValidation()
{
    const WavFile ScopedWav = BuildWav(16, 2, 48000, 8);
    const fs::path ScopedPath = GTempRoot / "validate.wav";
    FBY_CHECK(WriteFile(ScopedPath, ScopedWav.bytes));
    std::vector<std::byte> ScopedCooked;
    std::string ScopedError;
    FBY_CHECK(Fby::Cook::CookAudioFile(ScopedPath, &ScopedCooked, &ScopedError));
    FBY_CHECK(Fby::Assets::ParseCookedAudio(ScopedCooked, &ScopedError).has_value());

    // Truncated header.
    {
        std::vector<std::byte> ScopedTruncated(ScopedCooked.begin(), ScopedCooked.begin() + 8);
        FBY_CHECK(!Fby::Assets::ParseCookedAudio(ScopedTruncated, &ScopedError).has_value());
        FBY_CHECK(!ScopedError.empty());
    }
    // Bad magic.
    {
        std::vector<std::byte> ScopedCorrupt = ScopedCooked;
        ScopedCorrupt[0] = static_cast<std::byte>('X');
        FBY_CHECK(!Fby::Assets::ParseCookedAudio(ScopedCorrupt, &ScopedError).has_value());
    }
    // Unsupported version.
    {
        std::vector<std::byte> ScopedCorrupt = ScopedCooked;
        ScopedCorrupt[8] = static_cast<std::byte>(9);
        FBY_CHECK(!Fby::Assets::ParseCookedAudio(ScopedCorrupt, &ScopedError).has_value());
    }
    // Unknown format enum.
    {
        std::vector<std::byte> ScopedCorrupt = ScopedCooked;
        ScopedCorrupt[12] = static_cast<std::byte>(99);
        FBY_CHECK(!Fby::Assets::ParseCookedAudio(ScopedCorrupt, &ScopedError).has_value());
    }
    // Non-zero reserved field.
    {
        std::vector<std::byte> ScopedCorrupt = ScopedCooked;
        ScopedCorrupt[36] = static_cast<std::byte>(1);
        FBY_CHECK(!Fby::Assets::ParseCookedAudio(ScopedCorrupt, &ScopedError).has_value());
    }
    // Payload one byte short: dataBytes no longer matches the span.
    {
        std::vector<std::byte> ScopedCorrupt(ScopedCooked.begin(), ScopedCooked.end() - 1);
        FBY_CHECK(!Fby::Assets::ParseCookedAudio(ScopedCorrupt, &ScopedError).has_value());
    }
    // Header says more frames than the payload contains.
    {
        std::vector<std::byte> ScopedCorrupt = ScopedCooked;
        const std::uint32_t ScopedFrameCount = 9;
        ScopedCorrupt[24] = static_cast<std::byte>(ScopedFrameCount & 0xFFu);
        ScopedCorrupt[25] = static_cast<std::byte>((ScopedFrameCount >> 8) & 0xFFu);
        ScopedCorrupt[26] = static_cast<std::byte>((ScopedFrameCount >> 16) & 0xFFu);
        ScopedCorrupt[27] = static_cast<std::byte>((ScopedFrameCount >> 24) & 0xFFu);
        FBY_CHECK(!Fby::Assets::ParseCookedAudio(ScopedCorrupt, &ScopedError).has_value());
    }
    // Zero channels.
    {
        std::vector<std::byte> ScopedCorrupt = ScopedCooked;
        ScopedCorrupt[16] = std::byte{0};
        ScopedCorrupt[17] = std::byte{0};
        ScopedCorrupt[18] = std::byte{0};
        ScopedCorrupt[19] = std::byte{0};
        FBY_CHECK(!Fby::Assets::ParseCookedAudio(ScopedCorrupt, &ScopedError).has_value());
    }
    // A compressed block must carry the stream flag.
    {
        std::vector<std::byte> ScopedBlock;
        const std::vector<std::byte> ScopedPayload = {std::byte{1}, std::byte{2}};
        FBY_CHECK(Fby::Assets::AppendCookedAudio(ScopedBlock, PcmFormat::OggOpus, 2, 48000, 0, 0, ScopedPayload,
                                                 &ScopedError) == false);
        FBY_CHECK(Fby::Assets::AppendCookedAudio(ScopedBlock, PcmFormat::OggOpus, 2, 48000, 0,
                                                 Fby::Assets::kAudioFlagStream, ScopedPayload, &ScopedError));
        FBY_CHECK(Fby::Assets::ParseCookedAudio(ScopedBlock, &ScopedError).has_value());
    }
    // A frame count that does not divide the payload.
    {
        std::vector<std::byte> ScopedBlock;
        // 3 bytes, 2ch s16
        const std::vector<std::byte> ScopedPayload = {std::byte{0}, std::byte{0}, std::byte{0}};
        FBY_CHECK(!Fby::Assets::AppendCookedAudio(ScopedBlock, PcmFormat::PcmS16, 2, 48000, 1, 0, ScopedPayload,
                                                  &ScopedError));
    }
}

// 3: MP3 rejection
void TestMp3Rejection()
{
    const WavFile ScopedWav = BuildWav(16, 2, 48000, 8);
    std::vector<std::byte> ScopedCooked;
    std::string ScopedError;

    // Extension alone is enough, even with WAV bytes inside.
    {
        const fs::path ScopedPath = GTempRoot / "song.mp3";
        FBY_CHECK(WriteFile(ScopedPath, ScopedWav.bytes));
        FBY_CHECK(!Fby::Cook::CookAudioFile(ScopedPath, &ScopedCooked, &ScopedError));
        FBY_CHECK(ScopedError.find("MP3") != std::string::npos);
    }
    // ID3 tag inside a .wav: renamed MP3.
    {
        std::vector<std::byte> ScopedBytes;
        AppendText(ScopedBytes, "ID3");
        ScopedBytes.push_back(std::byte{0x04});
        ScopedBytes.push_back(std::byte{0x00});
        ScopedBytes.push_back(std::byte{0x00});
        const fs::path ScopedPath = GTempRoot / "renamed_id3.wav";
        FBY_CHECK(WriteFile(ScopedPath, ScopedBytes));
        FBY_CHECK(!Fby::Cook::CookAudioFile(ScopedPath, &ScopedCooked, &ScopedError));
        FBY_CHECK(ScopedError.find("MP3") != std::string::npos);
    }
    // MPEG frame sync inside a .wav: renamed MP3.
    {
        std::vector<std::byte> ScopedBytes;
        ScopedBytes.push_back(std::byte{0xFF});
        ScopedBytes.push_back(std::byte{0xFB});
        ScopedBytes.push_back(std::byte{0x90});
        ScopedBytes.push_back(std::byte{0x00});
        const fs::path ScopedPath = GTempRoot / "renamed_frame.wav";
        FBY_CHECK(WriteFile(ScopedPath, ScopedBytes));
        FBY_CHECK(!Fby::Cook::CookAudioFile(ScopedPath, &ScopedCooked, &ScopedError));
        FBY_CHECK(ScopedError.find("MP3") != std::string::npos);
    }
    // Unsupported but not MP3: FLAC bytes are rejected with a format error.
    {
        std::vector<std::byte> ScopedBytes;
        AppendText(ScopedBytes, "fLaC");
        ScopedBytes.push_back(std::byte{0});
        const fs::path ScopedPath = GTempRoot / "song.flac";
        FBY_CHECK(WriteFile(ScopedPath, ScopedBytes));
        FBY_CHECK(!Fby::Cook::CookAudioFile(ScopedPath, &ScopedCooked, &ScopedError));
        FBY_CHECK(ScopedError.find("unsupported") != std::string::npos);
    }
}

// 4: Ogg page parser and packet reassembly
void TestOggPageParser()
{
    const std::vector<std::byte> ScopedHead = BuildOpusHead();
    const std::vector<std::byte> ScopedTags = BuildOpusTags();
    std::vector<std::byte> ScopedStream = BuildOggPage(Fby::Audio::kOggFlagBeginningOfStream, 312, 0xCAFEBABEu, 0,
                                                       {static_cast<std::uint8_t>(ScopedHead.size())}, ScopedHead);
    const std::vector<std::byte> ScopedSecond =
        BuildOggPage(0, 312, 0xCAFEBABEu, 1, {static_cast<std::uint8_t>(ScopedTags.size())}, ScopedTags);
    ScopedStream.insert(ScopedStream.end(), ScopedSecond.begin(), ScopedSecond.end());

    // Page level.
    {
        Fby::Audio::OggPageReader ScopedReader;
        ScopedReader.Reset(ScopedStream);
        Fby::Audio::OggPage ScopedPage;
        std::string ScopedError;
        FBY_CHECK(ScopedReader.Next(&ScopedPage, &ScopedError));
        FBY_CHECK(ScopedPage.version == 0);
        FBY_CHECK((ScopedPage.headerType & Fby::Audio::kOggFlagBeginningOfStream) != 0);
        FBY_CHECK(ScopedPage.granulePosition == 312u);
        FBY_CHECK(ScopedPage.serialNumber == 0xCAFEBABEu);
        FBY_CHECK(ScopedPage.sequence == 0u);
        FBY_CHECK(ScopedPage.segmentCount == 1);
        FBY_CHECK(ScopedPage.lace[0] == ScopedHead.size());
        FBY_CHECK(ScopedPage.body.size() == ScopedHead.size());
        FBY_CHECK(std::memcmp(ScopedPage.body.data(), ScopedHead.data(), ScopedHead.size()) == 0);

        FBY_CHECK(ScopedReader.Next(&ScopedPage, &ScopedError));
        FBY_CHECK(ScopedPage.sequence == 1u);
        FBY_CHECK(ScopedPage.lace[0] == ScopedTags.size());
        FBY_CHECK(ScopedPage.body.size() == ScopedTags.size());
        FBY_CHECK(!ScopedReader.Next(&ScopedPage, &ScopedError));
        FBY_CHECK(ScopedReader.AtEnd());
    }

    // Packet level.
    {
        Fby::Audio::OggPacketReader ScopedReader;
        ScopedReader.Reset(ScopedStream);
        Fby::Audio::OggPacket ScopedPacket;
        std::string ScopedError;
        FBY_CHECK(ScopedReader.Next(&ScopedPacket, &ScopedError));
        FBY_CHECK(ScopedPacket.beginningOfStream);
        FBY_CHECK(ScopedPacket.bytes.size() == ScopedHead.size());
        FBY_CHECK(std::memcmp(ScopedPacket.bytes.data(), ScopedHead.data(), ScopedHead.size()) == 0);
        FBY_CHECK(ScopedReader.Next(&ScopedPacket, &ScopedError));
        FBY_CHECK(ScopedPacket.bytes.size() == ScopedTags.size());
        FBY_CHECK(std::memcmp(ScopedPacket.bytes.data(), ScopedTags.data(), ScopedTags.size()) == 0);
        FBY_CHECK(!ScopedReader.Next(&ScopedPacket, &ScopedError));
    }

    // Two packets on one page.
    {
        std::vector<std::uint8_t> ScopedLaces = {static_cast<std::uint8_t>(ScopedHead.size()),
                                                 static_cast<std::uint8_t>(ScopedTags.size())};
        std::vector<std::byte> ScopedBody = ScopedHead;
        ScopedBody.insert(ScopedBody.end(), ScopedTags.begin(), ScopedTags.end());
        std::vector<std::byte> ScopedPage =
            BuildOggPage(Fby::Audio::kOggFlagBeginningOfStream, 0, 7, 0, ScopedLaces, ScopedBody);
        Fby::Audio::OggPacketReader ScopedReader;
        ScopedReader.Reset(ScopedPage);
        Fby::Audio::OggPacket ScopedPacket;
        FBY_CHECK(ScopedReader.Next(&ScopedPacket, nullptr));
        FBY_CHECK(ScopedPacket.bytes.size() == ScopedHead.size());
        FBY_CHECK(ScopedReader.Next(&ScopedPacket, nullptr));
        FBY_CHECK(ScopedPacket.bytes.size() == ScopedTags.size());
        FBY_CHECK(!ScopedReader.Next(&ScopedPacket, nullptr));
    }

    // A packet split across two pages (255-byte lace then a continuation).
    {
        std::vector<std::byte> ScopedBody(255);
        for (std::size_t ScopedIndex = 0; ScopedIndex < ScopedBody.size(); ++ScopedIndex)
        {
            ScopedBody[ScopedIndex] = static_cast<std::byte>(ScopedIndex & 0xFFu);
        }
        const std::vector<std::byte> ScopedTail = {std::byte{0xAA}, std::byte{0xBB}, std::byte{0xCC}, std::byte{0xDD},
                                                   std::byte{0xEE}};
        std::vector<std::byte> ScopedSplit =
            BuildOggPage(Fby::Audio::kOggFlagBeginningOfStream, 0, 9, 0, {255}, ScopedBody);
        const std::vector<std::byte> ScopedContinuation = BuildOggPage(
            Fby::Audio::kOggFlagContinued, 0, 9, 1, {static_cast<std::uint8_t>(ScopedTail.size())}, ScopedTail);
        ScopedSplit.insert(ScopedSplit.end(), ScopedContinuation.begin(), ScopedContinuation.end());

        Fby::Audio::OggPacketReader ScopedReader;
        ScopedReader.Reset(ScopedSplit);
        Fby::Audio::OggPacket ScopedPacket;
        FBY_CHECK(ScopedReader.Next(&ScopedPacket, nullptr));
        FBY_CHECK(ScopedPacket.bytes.size() == ScopedBody.size() + ScopedTail.size());
        FBY_CHECK(std::memcmp(ScopedPacket.bytes.data(), ScopedBody.data(), ScopedBody.size()) == 0);
        FBY_CHECK(std::memcmp(ScopedPacket.bytes.data() + ScopedBody.size(), ScopedTail.data(), ScopedTail.size()) ==
                  0);
    }

    // CRC corruption is detected.
    {
        std::vector<std::byte> ScopedCorrupt = ScopedStream;
        ScopedCorrupt[27 + 1 + 5] = static_cast<std::byte>(ScopedCorrupt[27 + 1 + 5] ^ std::byte{0xFF});
        Fby::Audio::OggPageReader ScopedReader;
        ScopedReader.Reset(ScopedCorrupt);
        Fby::Audio::OggPage ScopedPage;
        std::string ScopedError;
        FBY_CHECK(!ScopedReader.Next(&ScopedPage, &ScopedError));
        FBY_CHECK(!ScopedError.empty());
    }

    // Codec headers.
    {
        Fby::Audio::OpusHeadInfo ScopedInfo;
        std::string ScopedError;
        FBY_CHECK(Fby::Audio::ParseOpusHead(ScopedHead, &ScopedInfo, &ScopedError));
        FBY_CHECK(ScopedInfo.channelCount == 2);
        FBY_CHECK(ScopedInfo.preSkip == 312);
        FBY_CHECK(ScopedInfo.inputSampleRate == 44100);
        FBY_CHECK(ScopedInfo.mappingFamily == 0);
        FBY_CHECK(Fby::Audio::ValidateOpusTags(ScopedTags, &ScopedError));

        Fby::Audio::VorbisIdentInfo ScopedVorbis;
        FBY_CHECK(Fby::Audio::ParseVorbisIdent(BuildVorbisIdent(22050, 1), &ScopedVorbis, &ScopedError));
        FBY_CHECK(ScopedVorbis.channels == 1);
        FBY_CHECK(ScopedVorbis.sampleRate == 22050);

        FBY_CHECK(!Fby::Audio::ValidateOpusTags(ScopedHead, &ScopedError));
    }
}

// 5 + 6: Ogg cooking and cooked payload sizes
void TestOggCooking()
{
    const std::vector<std::byte> ScopedOpusStream = BuildOggOpusStream();
    {
        const fs::path ScopedPath = GTempRoot / "music.ogg";
        FBY_CHECK(WriteFile(ScopedPath, ScopedOpusStream));
        std::vector<std::byte> ScopedCooked;
        std::string ScopedError;
        FBY_CHECK(Fby::Cook::CookAudioFile(ScopedPath, &ScopedCooked, &ScopedError));
        FBY_CHECK(ScopedCooked.size() == sizeof(AudioClipHeader) + ScopedOpusStream.size());

        const auto ScopedView = Fby::Assets::ParseCookedAudio(ScopedCooked, &ScopedError);
        FBY_CHECK(ScopedView.has_value());
        FBY_CHECK(ScopedView->format == PcmFormat::OggOpus);
        FBY_CHECK(ScopedView->frameCount == 0u);
        FBY_CHECK(ScopedView->channels == 2);
        FBY_CHECK(ScopedView->sampleRate == 48000);
        FBY_CHECK((ScopedView->flags & Fby::Assets::kAudioFlagStream) != 0);
        FBY_CHECK((ScopedView->flags & Fby::Assets::kAudioFlagOpus) != 0);
        FBY_CHECK(ScopedView->samples.size() == ScopedOpusStream.size());
        FBY_CHECK(std::memcmp(ScopedView->samples.data(), ScopedOpusStream.data(), ScopedOpusStream.size()) == 0);
    }

    // Same container under the .opus extension.
    {
        const fs::path ScopedPath = GTempRoot / "music.opus";
        FBY_CHECK(WriteFile(ScopedPath, ScopedOpusStream));
        std::vector<std::byte> ScopedCooked;
        std::string ScopedError;
        FBY_CHECK(Fby::Cook::CookAudioFile(ScopedPath, &ScopedCooked, &ScopedError));
        const auto ScopedView = Fby::Assets::ParseCookedAudio(ScopedCooked, &ScopedError);
        FBY_CHECK(ScopedView.has_value());
        FBY_CHECK(ScopedView->format == PcmFormat::OggOpus);
    }

    // Vorbis identification header -> Ogg Vorbis metadata.
    {
        const std::vector<std::byte> ScopedIdent = BuildVorbisIdent(44100, 2);
        std::vector<std::byte> ScopedStream =
            BuildOggPage(Fby::Audio::kOggFlagBeginningOfStream, 0, 0x51, 0,
                         {static_cast<std::uint8_t>(ScopedIdent.size())}, ScopedIdent);
        const fs::path ScopedPath = GTempRoot / "music_vorbis.ogg";
        FBY_CHECK(WriteFile(ScopedPath, ScopedStream));
        std::vector<std::byte> ScopedCooked;
        std::string ScopedError;
        FBY_CHECK(Fby::Cook::CookAudioFile(ScopedPath, &ScopedCooked, &ScopedError));
        const auto ScopedView = Fby::Assets::ParseCookedAudio(ScopedCooked, &ScopedError);
        FBY_CHECK(ScopedView.has_value());
        FBY_CHECK(ScopedView->format == PcmFormat::OggVorbis);
        FBY_CHECK(ScopedView->sampleRate == 44100);
        FBY_CHECK(ScopedView->channels == 2);
    }

    // Size arithmetic for every playable format.
    {
        for (const std::uint16_t ScopedBits : {static_cast<std::uint16_t>(16), static_cast<std::uint16_t>(24)})
        {
            const WavFile ScopedWav = BuildWav(ScopedBits, 2, 48000, 17);
            const fs::path ScopedPath = GTempRoot / (ScopedBits == 16 ? "size16.wav" : "size24.wav");
            FBY_CHECK(WriteFile(ScopedPath, ScopedWav.bytes));
            std::vector<std::byte> ScopedCooked;
            std::string ScopedError;
            FBY_CHECK(Fby::Cook::CookAudioFile(ScopedPath, &ScopedCooked, &ScopedError));
            const std::uint32_t ScopedBytesPerSample = ScopedBits / 8u;
            const std::uint64_t ScopedExpected = sizeof(AudioClipHeader) + 17ull * 2ull * ScopedBytesPerSample;
            FBY_CHECK(ScopedCooked.size() == ScopedExpected);
            const auto ScopedView = Fby::Assets::ParseCookedAudio(ScopedCooked, &ScopedError);
            FBY_CHECK(ScopedView.has_value());
            FBY_CHECK(ScopedView->samples.size() == 17ull * 2ull * ScopedBytesPerSample);
        }
    }
}
}

int main()
{
    std::error_code ScopedCode;
    // Prefer the OS temp directory, but fall back to a scratch directory next to
    const auto ScopedPrepare = [](const fs::path& InCandidate) -> bool
    {
        std::error_code ScopedError;
        fs::remove_all(InCandidate, ScopedError);
        fs::create_directories(InCandidate, ScopedError);
        return !ScopedError && fs::is_directory(InCandidate);
    };

    const fs::path ScopedTempCandidate = fs::temp_directory_path(ScopedCode) / "fby_audio_cook_test";
    if (ScopedCode || !ScopedPrepare(ScopedTempCandidate))
    {
        const fs::path ScopedLocalCandidate = fs::current_path(ScopedCode) / "fby_audio_cook_test_scratch";
        if (ScopedCode || !ScopedPrepare(ScopedLocalCandidate))
        {
            std::fprintf(stderr, "audio_cook_test: cannot create a temporary directory\n");
            return 1;
        }
        GTempRoot = ScopedLocalCandidate;
    }
    else
    {
        GTempRoot = ScopedTempCandidate;
    }

    TestWavRoundTrip();
    TestCookedValidation();
    TestMp3Rejection();
    TestOggPageParser();
    TestOggCooking();

    fs::remove_all(GTempRoot, ScopedCode);

    if (GFailures == 0)
    {
        std::printf("audio_cook_test: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "audio_cook_test: %d check(s) failed\n", GFailures);
    return 1;
}
