// FemboyEngine2D - Ogg page/packet parsing and Opus decoding.
#include "Engine/Core/Time.hpp"
#include "Engine/Audio/OggOpus.hpp"

#include "Engine/Assets/AudioClipFormat.hpp"

#include <opus.h>

#include <algorithm>
#include <cstring>
#include <new>

static_assert((Fby::Audio::kOggRingFrames & (Fby::Audio::kOggRingFrames - 1)) == 0,
              "the decode ring mask needs a power-of-two capacity");

namespace Fby::Audio
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

[[nodiscard]] std::uint64_t ReadU64(const std::byte* InBytes, std::size_t InIndex) noexcept
{
    return static_cast<std::uint64_t>(ReadU32(InBytes, InIndex)) |
           (static_cast<std::uint64_t>(ReadU32(InBytes, InIndex + 4)) << 32);
}

// Ogg's checksum is a plain, non-reflected CRC-32 with polynomial 0x04C11DB7.
[[nodiscard]] std::uint32_t CrcUpdate(std::uint32_t InCrc, std::uint8_t InByte) noexcept
{
    InCrc ^= static_cast<std::uint32_t>(InByte) << 24;
    for (std::uint32_t ScopedBit = 0; ScopedBit < 8; ++ScopedBit)
    {
        InCrc = (InCrc & 0x80000000u) != 0 ? (InCrc << 1) ^ 0x04C11DB7u : InCrc << 1;
    }
    return InCrc;
}

constexpr std::uint32_t kOggChecksumOffset = 22;
}

std::uint32_t OggCrc32(std::span<const std::byte> InBytes) noexcept
{
    std::uint32_t ScopedCrc = 0;
    for (const std::byte ScopedByte : InBytes)
    {
        ScopedCrc = CrcUpdate(ScopedCrc, std::to_integer<std::uint8_t>(ScopedByte));
    }
    return ScopedCrc;
}

std::uint32_t OggPageCrc(std::span<const std::byte> InPage) noexcept
{
    std::uint32_t ScopedCrc = 0;
    for (std::size_t ScopedIndex = 0; ScopedIndex < InPage.size(); ++ScopedIndex)
    {
        const bool ScopedInsideChecksum = ScopedIndex >= kOggChecksumOffset && ScopedIndex < kOggChecksumOffset + 4;
        ScopedCrc =
            CrcUpdate(ScopedCrc, ScopedInsideChecksum ? 0u : std::to_integer<std::uint8_t>(InPage[ScopedIndex]));
    }
    return ScopedCrc;
}

// OggPageReader
void OggPageReader::Reset(std::span<const std::byte> InBytes) noexcept
{
    MData = InBytes;
    MOffset = 0;
}

bool OggPageReader::Next(OggPage* InPage, std::string* InError) noexcept
{
    if (MData.empty() || MOffset >= MData.size())
    {
        return false;
    }
    if (MData.size() - MOffset < kOggPageHeaderBytes)
    {
        SetError(InError, "Ogg page header is truncated");
        MOffset = MData.size();
        return false;
    }

    const std::byte* ScopedHeader = MData.data() + MOffset;
    if (std::memcmp(ScopedHeader, "OggS", 4) != 0)
    {
        SetError(InError, "Ogg stream has a bad capture pattern");
        MOffset = MData.size();
        return false;
    }
    if (U8(ScopedHeader, 4) != 0)
    {
        SetError(InError, "Ogg page uses an unsupported version");
        MOffset = MData.size();
        return false;
    }

    const std::uint8_t ScopedSegmentCount = U8(ScopedHeader, 26);
    const std::size_t ScopedSegmentTableOffset = MOffset + kOggPageHeaderBytes;
    if (MData.size() - ScopedSegmentTableOffset < ScopedSegmentCount)
    {
        SetError(InError, "Ogg segment table is truncated");
        MOffset = MData.size();
        return false;
    }
    std::size_t ScopedBodySize = 0;
    for (std::uint8_t ScopedSegment = 0; ScopedSegment < ScopedSegmentCount; ++ScopedSegment)
    {
        ScopedBodySize += U8(MData.data(), ScopedSegmentTableOffset + ScopedSegment);
    }
    const std::size_t ScopedBodyOffset = ScopedSegmentTableOffset + ScopedSegmentCount;
    if (MData.size() - ScopedBodyOffset < ScopedBodySize)
    {
        SetError(InError, "Ogg page body is truncated");
        MOffset = MData.size();
        return false;
    }

    const std::size_t ScopedPageSize = kOggPageHeaderBytes + ScopedSegmentCount + ScopedBodySize;
    const auto ScopedWhole = MData.subspan(MOffset, ScopedPageSize);
    const std::uint32_t ScopedStoredChecksum = ReadU32(ScopedHeader, kOggChecksumOffset);
    if (OggPageCrc(ScopedWhole) != ScopedStoredChecksum)
    {
        SetError(InError, "Ogg page checksum does not match its contents");
        MOffset = MData.size();
        return false;
    }

    InPage->version = 0;
    InPage->headerType = U8(ScopedHeader, 5);
    InPage->granulePosition = ReadU64(ScopedHeader, 6);
    InPage->serialNumber = ReadU32(ScopedHeader, 14);
    InPage->sequence = ReadU32(ScopedHeader, 18);
    InPage->checksum = ScopedStoredChecksum;
    InPage->segmentCount = ScopedSegmentCount;
    for (std::uint8_t ScopedSegment = 0; ScopedSegment < ScopedSegmentCount; ++ScopedSegment)
    {
        InPage->lace[ScopedSegment] = U8(MData.data(), ScopedSegmentTableOffset + ScopedSegment);
    }
    InPage->body = MData.subspan(ScopedBodyOffset, ScopedBodySize);
    InPage->whole = ScopedWhole;
    MOffset = ScopedBodyOffset + ScopedBodySize;
    return true;
}

// OggPacketReader
void OggPacketReader::Reset(std::span<const std::byte> InBytes) noexcept
{
    MPages.Reset(InBytes);
    MPage = OggPage{};
    MHavePage = false;
    MFirstPacket = true;
    MAssembling = false;
    MSegmentIndex = 0;
    MPageBodyOffset = 0;
    MBufferSize = 0;
}

bool OggPacketReader::AtEnd() const noexcept
{
    return !MHavePage && MBufferSize == 0 && MPages.AtEnd();
}

bool OggPacketReader::Next(OggPacket* InPacket, std::string* InError) noexcept
{
    if (!MAssembling)
    {
        MBufferSize = 0;
    }

    for (;;)
    {
        if (!MHavePage)
        {
            if (!MPages.Next(&MPage, InError))
            {
                if (MBufferSize != 0 && InError != nullptr && InError->empty())
                {
                    *InError = "Ogg stream ends inside a packet";
                }
                return false;
            }
            MHavePage = true;
            MSegmentIndex = 0;
            MPageBodyOffset = 0;
            // A stream whose very first page is a continuation has no first
            if (MFirstPacket && !MAssembling && (MPage.headerType & kOggFlagContinued) != 0)
            {
                SetError(InError, "Ogg stream begins with a continued packet");
                return false;
            }
        }

        while (MSegmentIndex < MPage.segmentCount)
        {
            const std::size_t ScopedLace = MPage.lace[MSegmentIndex];
            ++MSegmentIndex;
            if (MBufferSize + ScopedLace > MBuffer.size())
            {
                SetError(InError, "Ogg packet is larger than the assembly buffer");
                return false;
            }
            if (ScopedLace != 0)
            {
                std::memcpy(MBuffer.data() + MBufferSize, MPage.body.data() + MPageBodyOffset, ScopedLace);
                MPageBodyOffset += ScopedLace;
                MBufferSize += ScopedLace;
            }
            if (ScopedLace < 255)
            {
                InPacket->bytes = std::span<const std::byte>(MBuffer.data(), MBufferSize);
                InPacket->granulePosition = MPage.granulePosition;
                InPacket->serialNumber = MPage.serialNumber;
                InPacket->beginningOfStream = MFirstPacket && (MPage.headerType & kOggFlagBeginningOfStream) != 0;
                InPacket->endOfStream = (MPage.headerType & kOggFlagEndOfStream) != 0;
                MFirstPacket = false;
                MAssembling = false;
                MBufferSize = 0;
                if (MSegmentIndex >= MPage.segmentCount)
                {
                    MHavePage = false;
                }
                return true;
            }
        }

        // The page ended on a 255 lace: this packet continues on the next page.
        MHavePage = false;
        MAssembling = true;
    }
}

// Codec headers
bool ParseOpusHead(std::span<const std::byte> InPacket, OpusHeadInfo* InInfo, std::string* InError)
{
    if (InInfo == nullptr)
    {
        SetError(InError, "OpusHead output is null");
        return false;
    }
    if (InPacket.size() < 19)
    {
        SetError(InError, "OpusHead packet is truncated");
        return false;
    }
    if (std::memcmp(InPacket.data(), "OpusHead", 8) != 0)
    {
        SetError(InError, "packet does not start with OpusHead");
        return false;
    }
    InInfo->version = U8(InPacket.data(), 8);
    if (InInfo->version > 1)
    {
        SetError(InError, "OpusHead version is not supported");
        return false;
    }
    InInfo->channelCount = U8(InPacket.data(), 9);
    if (InInfo->channelCount == 0 || InInfo->channelCount > kOggMaxStreamChannels)
    {
        SetError(InError, "OpusHead channel count is not supported (1 or 2 only)");
        return false;
    }
    InInfo->preSkip = ReadU16(InPacket.data(), 10);
    InInfo->inputSampleRate = ReadU32(InPacket.data(), 12);
    InInfo->outputGain = static_cast<std::int16_t>(ReadU16(InPacket.data(), 16));
    InInfo->mappingFamily = U8(InPacket.data(), 18);
    return true;
}

bool ValidateOpusTags(std::span<const std::byte> InPacket, std::string* InError)
{
    constexpr std::size_t ScopedKHeaderBytes = 8;
    constexpr std::size_t ScopedKLengthBytes = 4;
    if (InPacket.size() < ScopedKHeaderBytes + ScopedKLengthBytes)
    {
        SetError(InError, "OpusTags packet is truncated");
        return false;
    }
    if (std::memcmp(InPacket.data(), "OpusTags", ScopedKHeaderBytes) != 0)
    {
        SetError(InError, "packet does not start with OpusTags");
        return false;
    }
    const std::uint32_t ScopedVendorLength = ReadU32(InPacket.data(), ScopedKHeaderBytes);
    std::size_t ScopedOffset = ScopedKHeaderBytes + ScopedKLengthBytes;
    if (ScopedOffset + ScopedVendorLength + ScopedKLengthBytes > InPacket.size())
    {
        SetError(InError, "OpusTags vendor string is out of bounds");
        return false;
    }
    ScopedOffset += ScopedVendorLength;
    const std::uint32_t ScopedCommentCount = ReadU32(InPacket.data(), ScopedOffset);
    ScopedOffset += ScopedKLengthBytes;
    for (std::uint32_t ScopedComment = 0; ScopedComment < ScopedCommentCount; ++ScopedComment)
    {
        if (ScopedOffset + ScopedKLengthBytes > InPacket.size())
        {
            SetError(InError, "OpusTags comment table is truncated");
            return false;
        }
        const std::uint32_t ScopedLength = ReadU32(InPacket.data(), ScopedOffset);
        ScopedOffset += ScopedKLengthBytes;
        if (ScopedOffset + ScopedLength > InPacket.size())
        {
            SetError(InError, "OpusTags comment is out of bounds");
            return false;
        }
        ScopedOffset += ScopedLength;
    }
    return true;
}

bool ParseVorbisIdent(std::span<const std::byte> InPacket, VorbisIdentInfo* InInfo, std::string* InError)
{
    // 1 byte type + 6 "vorbis" + 4 version + 1 channels + 4 rate + 12 bitrates
    constexpr std::size_t ScopedKMinimumBytes = 30;
    if (InInfo == nullptr)
    {
        SetError(InError, "Vorbis identification output is null");
        return false;
    }
    if (InPacket.size() < ScopedKMinimumBytes)
    {
        SetError(InError, "Vorbis identification header is truncated");
        return false;
    }
    if (U8(InPacket.data(), 0) != 0x01 || std::memcmp(InPacket.data() + 1, "vorbis", 6) != 0)
    {
        SetError(InError, "packet is not a Vorbis identification header");
        return false;
    }
    if (ReadU32(InPacket.data(), 7) != 0)
    {
        SetError(InError, "Vorbis version is not supported");
        return false;
    }
    InInfo->channels = U8(InPacket.data(), 11);
    InInfo->sampleRate = ReadU32(InPacket.data(), 12);
    if (InInfo->channels == 0 || InInfo->channels > Assets::kAudioClipMaxChannels || InInfo->sampleRate == 0)
    {
        SetError(InError, "Vorbis identification header has invalid channels or sample rate");
        return false;
    }
    return true;
}

// OggOpusStream
OggOpusStream::~OggOpusStream()
{
    Stop();
    DestroyDecoder();
}

void OggOpusStream::DestroyDecoder() noexcept
{
    if (MDecoder != nullptr)
    {
        opus_decoder_destroy(static_cast<OpusDecoder*>(MDecoder));
        MDecoder = nullptr;
    }
}

bool OggOpusStream::Open(std::span<const std::byte> InOggBytes, std::string* InError)
{
    Stop();
    DestroyDecoder();
    MPackets.Reset(InOggBytes);
    MOgg = InOggBytes;
    MOpen = false;

    OggPacket ScopedHeadPacket;
    if (!MPackets.Next(&ScopedHeadPacket, InError))
    {
        if (InError != nullptr && InError->empty())
        {
            *InError = "Ogg Opus stream is empty";
        }
        return false;
    }
    OpusHeadInfo ScopedHead;
    if (!ParseOpusHead(ScopedHeadPacket.bytes, &ScopedHead, InError))
    {
        return false;
    }
    OggPacket ScopedTagsPacket;
    if (!MPackets.Next(&ScopedTagsPacket, InError))
    {
        if (InError != nullptr && InError->empty())
        {
            *InError = "Ogg Opus stream has no OpusTags packet";
        }
        return false;
    }
    if (!ValidateOpusTags(ScopedTagsPacket.bytes, InError))
    {
        return false;
    }

    MChannels = ScopedHead.channelCount;
    MPreSkipRemaining = ScopedHead.preSkip;
    int ScopedOpusError = OPUS_OK;
    MDecoder =
        opus_decoder_create(static_cast<opus_int32>(kOggOpusSampleRate), static_cast<int>(MChannels), &ScopedOpusError);
    if (MDecoder == nullptr || ScopedOpusError != OPUS_OK)
    {
        DestroyDecoder();
        SetError(InError, "Opus decoder could not be created");
        return false;
    }
    MRing.reset(new (std::nothrow) float[kOggRingFrames * 2]);
    MScratch.reset(new (std::nothrow) float[kOggMaxDecodeFrameSamples * 2]);
    if (MRing == nullptr || MScratch == nullptr)
    {
        MRing.reset();
        MScratch.reset();
        DestroyDecoder();
        SetError(InError, "Ogg Opus stream could not allocate its decode buffers");
        return false;
    }
    MRingHead.store(0, std::memory_order_relaxed);
    MRingTail.store(0, std::memory_order_relaxed);
    MStop.store(false, std::memory_order_relaxed);
    MFinished.store(false, std::memory_order_relaxed);
    MDecodedFrames.store(0, std::memory_order_relaxed);
    MOpen = true;
    return true;
}

bool OggOpusStream::Start()
{
    if (!MOpen || MStarted)
    {
        return false;
    }
    MStop.store(false, std::memory_order_relaxed);
    try
    {
        MDecodeThread = std::thread([this] { DecodeLoop(); });
    }
    catch (...)
    {
        return false;
    }
    MStarted = true;
    return true;
}

void OggOpusStream::Stop() noexcept
{
    MStop.store(true, std::memory_order_release);
    if (MDecodeThread.joinable())
    {
        MDecodeThread.join();
    }
    MStarted = false;
}

void OggOpusStream::DecodeLoop()
{
    constexpr std::uint32_t ScopedKRingMask = kOggRingFrames - 1;
    constexpr std::uint32_t ScopedKMaxFrameSamples = kOggMaxDecodeFrameSamples;
    while (!MStop.load(std::memory_order_acquire))
    {
        // Backpressure at packet granularity: wait for room rather than block
        const std::uint32_t ScopedQueued =
            MRingTail.load(std::memory_order_relaxed) - MRingHead.load(std::memory_order_acquire);
        if (kOggRingFrames - ScopedQueued < ScopedKMaxFrameSamples)
        {
            Core::SleepMicroseconds(200.0);
            continue;
        }

        OggPacket ScopedPacket;
        if (!MPackets.Next(&ScopedPacket, nullptr))
        {
            MFinished.store(true, std::memory_order_release);
            break;
        }
        const auto* ScopedData = reinterpret_cast<const unsigned char*>(ScopedPacket.bytes.data());
        const int ScopedDecoded = opus_decode_float(static_cast<OpusDecoder*>(MDecoder), ScopedData,
                                                    static_cast<opus_int32>(ScopedPacket.bytes.size()), MScratch.get(),
                                                    static_cast<int>(ScopedKMaxFrameSamples), 0);
        if (ScopedDecoded < 0)
        {
            MFinished.store(true, std::memory_order_release);
            break;
        }
        if (ScopedDecoded == 0)
        {
            continue;
        }

        std::uint32_t ScopedStart = 0;
        if (MPreSkipRemaining > 0)
        {
            const auto ScopedSkip = std::min(MPreSkipRemaining, static_cast<std::uint32_t>(ScopedDecoded));
            ScopedStart = ScopedSkip;
            MPreSkipRemaining -= ScopedSkip;
        }

        bool ScopedStopped = false;
        for (std::uint32_t ScopedSample = ScopedStart; ScopedSample < static_cast<std::uint32_t>(ScopedDecoded);
             ++ScopedSample)
        {
            std::uint32_t ScopedTail = MRingTail.load(std::memory_order_relaxed);
            for (;;)
            {
                const std::uint32_t ScopedHead = MRingHead.load(std::memory_order_acquire);
                if (ScopedTail - ScopedHead < kOggRingFrames)
                {
                    break;
                }
                if (MStop.load(std::memory_order_acquire))
                {
                    ScopedStopped = true;
                    break;
                }
                Core::SleepMicroseconds(200.0);
            }
            if (ScopedStopped)
            {
                return;
            }
            const std::uint32_t ScopedSlot = (ScopedTail & ScopedKRingMask) * 2;
            if (MChannels == 1)
            {
                const float ScopedValue = MScratch[ScopedSample];
                MRing[ScopedSlot] = ScopedValue;
                MRing[ScopedSlot + 1] = ScopedValue;
            }
            else
            {
                MRing[ScopedSlot] = MScratch[static_cast<std::size_t>(ScopedSample) * MChannels];
                MRing[ScopedSlot + 1] = MScratch[static_cast<std::size_t>(ScopedSample) * MChannels + 1];
            }
            MRingTail.store(ScopedTail + 1, std::memory_order_release);
        }
        MDecodedFrames.fetch_add(static_cast<std::uint64_t>(ScopedDecoded) - ScopedStart, std::memory_order_relaxed);

        if (ScopedPacket.endOfStream)
        {
            MFinished.store(true, std::memory_order_release);
            break;
        }
    }
}

std::uint32_t OggOpusStream::Read(float* InInterleavedStereo, std::uint32_t InFrameCount) noexcept
{
    if (InInterleavedStereo == nullptr || InFrameCount == 0)
    {
        return 0;
    }
    constexpr std::uint32_t ScopedKRingMask = kOggRingFrames - 1;
    const std::uint32_t ScopedHead = MRingHead.load(std::memory_order_relaxed);
    const std::uint32_t ScopedTail = MRingTail.load(std::memory_order_acquire);
    const std::uint32_t ScopedAvailable = ScopedTail - ScopedHead;
    const std::uint32_t ScopedCount = InFrameCount > ScopedAvailable ? ScopedAvailable : InFrameCount;
    for (std::uint32_t ScopedFrame = 0; ScopedFrame < ScopedCount; ++ScopedFrame)
    {
        const std::uint32_t ScopedSlot = ((ScopedHead + ScopedFrame) & ScopedKRingMask) * 2;
        InInterleavedStereo[static_cast<std::size_t>(ScopedFrame) * 2] = MRing[ScopedSlot];
        InInterleavedStereo[static_cast<std::size_t>(ScopedFrame) * 2 + 1] = MRing[ScopedSlot + 1];
    }
    MRingHead.store(ScopedHead + ScopedCount, std::memory_order_release);
    return ScopedCount;
}

std::uint32_t OggOpusStream::AvailableFrames() const noexcept
{
    const std::uint32_t ScopedHead = MRingHead.load(std::memory_order_acquire);
    const std::uint32_t ScopedTail = MRingTail.load(std::memory_order_acquire);
    return ScopedTail - ScopedHead;
}

// Whole-track decode (load-time fallback)
bool DecodeOggOpusToStereo(std::span<const std::byte> InOggBytes, std::uint32_t InMaxFrames,
                           std::vector<float>* InInterleavedStereo, std::uint32_t* OutFrames, std::string* InError)
{
    if (InInterleavedStereo == nullptr || OutFrames == nullptr)
    {
        SetError(InError, "whole-track decode received a null output");
        return false;
    }
    InInterleavedStereo->clear();
    *OutFrames = 0;

    OggPacketReader ScopedPackets;
    ScopedPackets.Reset(InOggBytes);
    OggPacket ScopedHeadPacket;
    if (!ScopedPackets.Next(&ScopedHeadPacket, InError))
    {
        if (InError != nullptr && InError->empty())
        {
            *InError = "Ogg Opus stream is empty";
        }
        return false;
    }
    OpusHeadInfo ScopedHead;
    if (!ParseOpusHead(ScopedHeadPacket.bytes, &ScopedHead, InError))
    {
        return false;
    }
    const std::uint32_t ScopedChannels = ScopedHead.channelCount;

    int ScopedOpusError = OPUS_OK;
    OpusDecoder* ScopedDecoder = opus_decoder_create(static_cast<opus_int32>(kOggOpusSampleRate),
                                                     static_cast<int>(ScopedChannels), &ScopedOpusError);
    if (ScopedDecoder == nullptr || ScopedOpusError != OPUS_OK)
    {
        SetError(InError, "Opus decoder could not be created");
        return false;
    }

    std::vector<float> ScopedScratch(kOggMaxDecodeFrameSamples * 2);
    std::uint32_t ScopedPreSkipRemaining = ScopedHead.preSkip;
    bool ScopedHaveTags = false;
    bool ScopedOk = true;
    OggPacket ScopedPacket;
    while (ScopedPackets.Next(&ScopedPacket, nullptr))
    {
        if (!ScopedHaveTags)
        {
            if (!ValidateOpusTags(ScopedPacket.bytes, InError))
            {
                ScopedOk = false;
                break;
            }
            ScopedHaveTags = true;
            continue;
        }
        const auto* ScopedData = reinterpret_cast<const unsigned char*>(ScopedPacket.bytes.data());
        const int ScopedDecoded =
            opus_decode_float(ScopedDecoder, ScopedData, static_cast<opus_int32>(ScopedPacket.bytes.size()),
                              ScopedScratch.data(), static_cast<int>(kOggMaxDecodeFrameSamples), 0);
        if (ScopedDecoded < 0)
        {
            SetError(InError, "Opus packet failed to decode");
            ScopedOk = false;
            break;
        }
        if (ScopedDecoded == 0)
        {
            continue;
        }
        std::uint32_t ScopedStart = 0;
        if (ScopedPreSkipRemaining > 0)
        {
            const auto ScopedSkip = std::min(ScopedPreSkipRemaining, static_cast<std::uint32_t>(ScopedDecoded));
            ScopedStart = ScopedSkip;
            ScopedPreSkipRemaining -= ScopedSkip;
        }
        const std::uint32_t ScopedUsable = static_cast<std::uint32_t>(ScopedDecoded) - ScopedStart;
        if (static_cast<std::uint64_t>(*OutFrames) + ScopedUsable > InMaxFrames)
        {
            SetError(InError, "Ogg Opus track exceeds the whole-track decode cap; use OggOpusStream");
            ScopedOk = false;
            break;
        }
        InInterleavedStereo->resize(static_cast<std::size_t>(*OutFrames + ScopedUsable) * 2);
        for (std::uint32_t ScopedSample = ScopedStart; ScopedSample < static_cast<std::uint32_t>(ScopedDecoded);
             ++ScopedSample)
        {
            const std::size_t ScopedDestination =
                static_cast<std::size_t>(*OutFrames + (ScopedSample - ScopedStart)) * 2;
            if (ScopedChannels == 1)
            {
                const float ScopedValue = ScopedScratch[ScopedSample];
                (*InInterleavedStereo)[ScopedDestination] = ScopedValue;
                (*InInterleavedStereo)[ScopedDestination + 1] = ScopedValue;
            }
            else
            {
                (*InInterleavedStereo)[ScopedDestination] =
                    ScopedScratch[static_cast<std::size_t>(ScopedSample) * ScopedChannels];
                (*InInterleavedStereo)[ScopedDestination + 1] =
                    ScopedScratch[static_cast<std::size_t>(ScopedSample) * ScopedChannels + 1];
            }
        }
        *OutFrames += ScopedUsable;
        if (ScopedPacket.endOfStream)
        {
            break;
        }
    }

    opus_decoder_destroy(ScopedDecoder);
    if (!ScopedHaveTags && ScopedOk)
    {
        SetError(InError, "Ogg Opus stream has no OpusTags packet");
        ScopedOk = false;
    }
    if (!ScopedOk)
    {
        InInterleavedStereo->clear();
        *OutFrames = 0;
    }
    return ScopedOk;
}
}
