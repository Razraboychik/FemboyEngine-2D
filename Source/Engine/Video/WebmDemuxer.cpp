// FemboyEngine2D - WebM/EBML demuxer.
#include "Engine/Video/WebmDemuxer.hpp"

#include <algorithm>
#include <bit>
#include <cstdio>
#include <cstring>
#include <format>
#include <limits>
#include <string>
#include <utility>

namespace Fby::Video
{
namespace
{

constexpr std::uint32_t kEbmlHeaderId = 0x1A45DFA3u;
constexpr std::uint32_t kEbmlReadVersionId = 0x42F7u;
constexpr std::uint32_t kEbmlMaxIdLengthId = 0x42F2u;
constexpr std::uint32_t kEbmlMaxSizeLengthId = 0x42F3u;
constexpr std::uint32_t kEbmlDocTypeId = 0x4282u;

constexpr std::uint32_t kSegmentId = 0x18538067u;
constexpr std::uint32_t kSeekHeadId = 0x114D9B74u;
constexpr std::uint32_t kInfoId = 0x1549A966u;
constexpr std::uint32_t kTimestampScaleId = 0x2AD7B1u;
constexpr std::uint32_t kDurationId = 0x4489u;
constexpr std::uint32_t kTracksId = 0x1654AE6Bu;
constexpr std::uint32_t kTrackEntryId = 0xAEu;
constexpr std::uint32_t kTrackNumberId = 0xD7u;
constexpr std::uint32_t kTrackTypeId = 0x83u;
constexpr std::uint32_t kCodecIdId = 0x86u;
constexpr std::uint32_t kCodecPrivateId = 0x63A2u;
constexpr std::uint32_t kDefaultDurationId = 0x23E383u;
constexpr std::uint32_t kVideoId = 0xE0u;
constexpr std::uint32_t kPixelWidthId = 0xB0u;
constexpr std::uint32_t kPixelHeightId = 0xBAu;
constexpr std::uint32_t kAlphaModeId = 0x53C0u;
constexpr std::uint32_t kClusterId = 0x1F43B675u;
constexpr std::uint32_t kClusterTimestampId = 0xE7u;
constexpr std::uint32_t kSimpleBlockId = 0xA3u;
constexpr std::uint32_t kBlockGroupId = 0xA0u;
constexpr std::uint32_t kBlockId = 0xA1u;
constexpr std::uint32_t kBlockDurationId = 0x9Bu;
constexpr std::uint32_t kBlockAdditionsId = 0x75A1u;
constexpr std::uint32_t kBlockMoreId = 0xA6u;
constexpr std::uint32_t kBlockAddIdId = 0xEEu;
constexpr std::uint32_t kBlockAdditionalId = 0xA5u;
constexpr std::uint32_t kCuesId = 0x1C53BB6Bu;
constexpr std::uint32_t kChaptersId = 0x1043A770u;
constexpr std::uint32_t kAttachmentsId = 0x1941A469u;
constexpr std::uint32_t kTagsId = 0x1254C367u;
constexpr std::uint32_t kVoidId = 0xECu;
constexpr std::uint32_t kCrc32Id = 0xBFu;

// WebM's default TimestampScale: one timestamp tick is one millisecond.
constexpr std::uint64_t kDefaultTimestampScaleNs = 1000000ull;

void SetError(std::string* InError, std::string InMessage)
{
    if (InError != nullptr)
    {
        *InError = std::move(InMessage);
    }
}

[[nodiscard]] bool IsTopLevelId(std::uint32_t InId) noexcept
{
    switch (InId)
    {
        case kSeekHeadId:
        case kInfoId:
        case kTracksId:
        case kClusterId:
        case kCuesId:
        case kChaptersId:
        case kAttachmentsId:
        case kTagsId:
            return true;
        default:
            return false;
    }
}

[[nodiscard]] bool IsSkippableAnywhere(std::uint32_t InId) noexcept
{
    return InId == kVoidId || InId == kCrc32Id;
}

// Reads a VINT whose value carries no id semantics (block track numbers).
[[nodiscard]] bool ReadSpanVint(std::span<const std::byte> InData, std::size_t& InPosition,
                                std::uint64_t& InValue) noexcept
{
    if (InPosition >= InData.size())
    {
        return false;
    }
    const auto ScopedFirst = static_cast<std::uint8_t>(InData[InPosition]);
    if (ScopedFirst == 0)
    {
        return false;
    }
    std::size_t ScopedLength = 0;
    std::uint8_t ScopedMask = 0x80;
    while ((ScopedFirst & ScopedMask) == 0)
    {
        ScopedMask >>= 1;
        ++ScopedLength;
    }
    // Marker bit position + 1 == byte length
    ++ScopedLength;
    if (ScopedLength > 8 || InPosition + ScopedLength > InData.size())
    {
        return false;
    }
    InValue = static_cast<std::uint64_t>(ScopedFirst & (ScopedMask - 1));
    for (std::size_t ScopedI = 1; ScopedI < ScopedLength; ++ScopedI)
    {
        InValue = (InValue << 8) | static_cast<std::uint8_t>(InData[InPosition + ScopedI]);
    }
    InPosition += ScopedLength;
    return true;
}
}

// Low-level EBML primitives
bool WebmDemuxer::RangeInside(std::uint64_t InOffset, std::uint64_t InSize) const noexcept
{
    return InOffset <= MBytes.size() && InSize <= static_cast<std::uint64_t>(MBytes.size()) - InOffset;
}

bool WebmDemuxer::ReadId(std::size_t InPosition, std::uint32_t& InId, std::size_t& InHeaderBytes) const
{
    if (InPosition >= MBytes.size())
    {
        return false;
    }
    const auto ScopedFirst = static_cast<std::uint8_t>(MBytes[InPosition]);
    std::size_t ScopedLength = 0;
    std::uint8_t ScopedMask = 0x80;
    // An EBML id is at most four bytes; a first byte with the marker below the
    while (ScopedLength < 4 && (ScopedFirst & ScopedMask) == 0)
    {
        ScopedMask >>= 1;
        ++ScopedLength;
    }
    if (ScopedLength >= 4)
    {
        return false;
    }
    ++ScopedLength;
    if (InPosition + ScopedLength > MBytes.size())
    {
        return false;
    }
    std::uint32_t ScopedValue = 0;
    for (std::size_t ScopedI = 0; ScopedI < ScopedLength; ++ScopedI)
    {
        ScopedValue = (ScopedValue << 8) | static_cast<std::uint8_t>(MBytes[InPosition + ScopedI]);
    }
    InId = ScopedValue;
    InHeaderBytes = ScopedLength;
    return true;
}

bool WebmDemuxer::ReadVintValue(std::size_t InPosition, std::uint64_t& InValue, std::size_t& InBytes) const
{
    if (InPosition >= MBytes.size())
    {
        return false;
    }
    const auto ScopedFirst = static_cast<std::uint8_t>(MBytes[InPosition]);
    if (ScopedFirst == 0)
    {
        return false;
    }
    std::size_t ScopedLength = 0;
    std::uint8_t ScopedMask = 0x80;
    while ((ScopedFirst & ScopedMask) == 0)
    {
        ScopedMask >>= 1;
        ++ScopedLength;
    }
    ++ScopedLength;
    if (ScopedLength > 8 || InPosition + ScopedLength > MBytes.size())
    {
        return false;
    }
    InValue = static_cast<std::uint64_t>(ScopedFirst & (ScopedMask - 1));
    for (std::size_t ScopedI = 1; ScopedI < ScopedLength; ++ScopedI)
    {
        InValue = (InValue << 8) | static_cast<std::uint8_t>(MBytes[InPosition + ScopedI]);
    }
    InBytes = ScopedLength;
    return true;
}

bool WebmDemuxer::ReadElement(std::size_t InPosition, Element& InElement) const
{
    std::size_t ScopedIdBytes = 0;
    if (!ReadId(InPosition, InElement.id, ScopedIdBytes))
    {
        return false;
    }
    std::size_t ScopedSizeBytes = 0;
    std::uint64_t ScopedSize = 0;
    if (!ReadVintValue(InPosition + ScopedIdBytes, ScopedSize, ScopedSizeBytes))
    {
        return false;
    }
    const std::uint64_t ScopedUnknownMarker = (1ull << (7 * static_cast<unsigned>(ScopedSizeBytes))) - 1ull;
    InElement.unknownSize = ScopedSize == ScopedUnknownMarker;
    InElement.size = ScopedSize;
    InElement.dataStart = InPosition + ScopedIdBytes + ScopedSizeBytes;
    if (!InElement.unknownSize && !RangeInside(InElement.dataStart, InElement.size))
    {
        return false;
    }
    return true;
}

bool WebmDemuxer::ReadUnsigned(const Element& InElement, std::uint64_t& InValue) const
{
    if (InElement.unknownSize || InElement.size > 8 || !RangeInside(InElement.dataStart, InElement.size))
    {
        return false;
    }
    std::uint64_t ScopedParsed = 0;
    for (std::uint64_t ScopedI = 0; ScopedI < InElement.size; ++ScopedI)
    {
        ScopedParsed = (ScopedParsed << 8) | static_cast<std::uint8_t>(MBytes[InElement.dataStart + ScopedI]);
    }
    InValue = ScopedParsed;
    return true;
}

bool WebmDemuxer::ReadFloat(const Element& InElement, double& InValue) const
{
    if (InElement.unknownSize || !RangeInside(InElement.dataStart, InElement.size))
    {
        return false;
    }
    if (InElement.size == 4)
    {
        std::uint32_t ScopedBits = 0;
        for (std::uint32_t ScopedI = 0; ScopedI < 4; ++ScopedI)
        {
            ScopedBits = (ScopedBits << 8) | static_cast<std::uint8_t>(MBytes[InElement.dataStart + ScopedI]);
        }
        InValue = static_cast<double>(std::bit_cast<float>(ScopedBits));
        return true;
    }
    if (InElement.size == 8)
    {
        std::uint64_t ScopedBits = 0;
        for (std::uint32_t ScopedI = 0; ScopedI < 8; ++ScopedI)
        {
            ScopedBits = (ScopedBits << 8) | static_cast<std::uint8_t>(MBytes[InElement.dataStart + ScopedI]);
        }
        InValue = std::bit_cast<double>(ScopedBits);
        return true;
    }
    return false;
}

std::string_view WebmDemuxer::ReadString(const Element& InElement) const
{
    if (InElement.unknownSize || !RangeInside(InElement.dataStart, InElement.size))
    {
        return {};
    }
    std::size_t ScopedLength = static_cast<std::size_t>(InElement.size);
    const auto* ScopedText = reinterpret_cast<const char*>(MBytes.data() + InElement.dataStart);
    // EBML strings are not NUL terminated; some muxers pad with NULs anyway.
    while (ScopedLength > 0 && ScopedText[ScopedLength - 1] == '\0')
    {
        --ScopedLength;
    }
    return std::string_view(ScopedText, ScopedLength);
}

std::uint64_t WebmDemuxer::TimestampToNs(std::uint64_t InTicks) const noexcept
{
    const std::uint64_t ScopedScale = MTrack.timestampScaleNs == 0 ? kDefaultTimestampScaleNs : MTrack.timestampScaleNs;
    if (InTicks > std::numeric_limits<std::uint64_t>::max() / ScopedScale)
    {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return InTicks * ScopedScale;
}

// Header parsing
bool WebmDemuxer::ParseEbmlHeader(std::string* InError)
{
    Element ScopedHeader;
    if (!ReadElement(0, ScopedHeader) || ScopedHeader.id != kEbmlHeaderId)
    {
        SetError(InError, "not an EBML/WebM stream (missing the EBML header element)");
        return false;
    }
    if (ScopedHeader.unknownSize)
    {
        SetError(InError, "EBML header has unknown size");
        return false;
    }

    bool ScopedHasDocType = false;
    std::string ScopedDocType;
    const std::size_t ScopedEnd = ScopedHeader.dataStart + static_cast<std::size_t>(ScopedHeader.size);
    std::size_t ScopedPosition = ScopedHeader.dataStart;
    while (ScopedPosition < ScopedEnd)
    {
        Element ScopedChild;
        if (!ReadElement(ScopedPosition, ScopedChild) || ScopedChild.unknownSize)
        {
            SetError(InError, "malformed EBML header");
            return false;
        }
        if (ScopedChild.id == kEbmlDocTypeId)
        {
            ScopedHasDocType = true;
            ScopedDocType = std::string(ReadString(ScopedChild));
        }
        else if (ScopedChild.id == kEbmlMaxIdLengthId)
        {
            std::uint64_t ScopedValue = 0;
            if (!ReadUnsigned(ScopedChild, ScopedValue) || ScopedValue > 4)
            {
                SetError(InError, "EBML header declares an unsupported maximum id length");
                return false;
            }
        }
        else if (ScopedChild.id == kEbmlMaxSizeLengthId)
        {
            std::uint64_t ScopedValue = 0;
            if (!ReadUnsigned(ScopedChild, ScopedValue) || ScopedValue > 8)
            {
                SetError(InError, "EBML header declares an unsupported maximum size length");
                return false;
            }
        }
        else if (ScopedChild.id == kEbmlReadVersionId)
        {
            std::uint64_t ScopedValue = 0;
            if (!ReadUnsigned(ScopedChild, ScopedValue) || ScopedValue > 1)
            {
                SetError(InError, "unsupported EBML read version");
                return false;
            }
        }
        ScopedPosition = ScopedChild.dataStart + static_cast<std::size_t>(ScopedChild.size);
    }

    if (!ScopedHasDocType)
    {
        SetError(InError, "EBML header has no DocType element");
        return false;
    }
    if (ScopedDocType != "webm")
    {
        SetError(InError, std::format("EBML DocType '{}' is not WebM: only WebM with a VP9 video track is supported",
                                      ScopedDocType));
        return false;
    }
    return true;
}

bool WebmDemuxer::ParseSegmentInfo(const Element& InSegment, std::string* InError)
{
    std::uint64_t ScopedTimestampScale = kDefaultTimestampScaleNs;
    double ScopedDurationTicks = -1.0;
    const std::size_t ScopedEnd = InSegment.dataStart + static_cast<std::size_t>(InSegment.size);
    std::size_t ScopedPosition = InSegment.dataStart;
    while (ScopedPosition < ScopedEnd)
    {
        Element ScopedChild;
        if (!ReadElement(ScopedPosition, ScopedChild) || ScopedChild.unknownSize)
        {
            SetError(InError, "malformed Segment/Info element");
            return false;
        }
        if (ScopedChild.id == kTimestampScaleId)
        {
            std::uint64_t ScopedValue = 0;
            if (!ReadUnsigned(ScopedChild, ScopedValue))
            {
                SetError(InError, "Info/TimestampScale is malformed");
                return false;
            }
            if (ScopedValue == 0 || ScopedValue > 1000000000ull)
            {
                SetError(InError,
                         std::format("Info/TimestampScale {} is out of range (1..1000000000 ns)", ScopedValue));
                return false;
            }
            ScopedTimestampScale = ScopedValue;
        }
        else if (ScopedChild.id == kDurationId)
        {
            double ScopedValue = 0.0;
            if (!ReadFloat(ScopedChild, ScopedValue) || !(ScopedValue >= 0.0))
            {
                SetError(InError, "Info/Duration is malformed");
                return false;
            }
            ScopedDurationTicks = ScopedValue;
        }
        ScopedPosition = ScopedChild.dataStart + static_cast<std::size_t>(ScopedChild.size);
    }

    MTrack.timestampScaleNs = ScopedTimestampScale;
    if (ScopedDurationTicks >= 0.0)
    {
        MTrack.durationNs = static_cast<std::uint64_t>(ScopedDurationTicks * static_cast<double>(ScopedTimestampScale));
    }
    return true;
}

bool WebmDemuxer::ParseTracks(const Element& InTracks, std::string* InError)
{
    const std::size_t ScopedEnd = InTracks.dataStart + static_cast<std::size_t>(InTracks.size);
    std::size_t ScopedPosition = InTracks.dataStart;
    while (ScopedPosition < ScopedEnd)
    {
        Element ScopedEntry;
        if (!ReadElement(ScopedPosition, ScopedEntry) || ScopedEntry.unknownSize)
        {
            SetError(InError, "malformed Segment/Tracks element");
            return false;
        }
        if (ScopedEntry.id == kTrackEntryId && !ParseTrackEntry(ScopedEntry, InError))
        {
            return false;
        }
        ScopedPosition = ScopedEntry.dataStart + static_cast<std::size_t>(ScopedEntry.size);
    }
    return true;
}

bool WebmDemuxer::ParseTrackEntry(const Element& InEntry, std::string* InError)
{
    std::uint64_t ScopedTrackNumber = 0;
    std::uint64_t ScopedTrackType = 0;
    std::string_view ScopedCodecId{};
    bool ScopedHasCodecPrivate = false;
    std::span<const std::byte> ScopedCodecPrivate{};
    std::uint32_t ScopedWidth = 0;
    std::uint32_t ScopedHeight = 0;
    std::uint64_t ScopedDefaultDuration = 0;
    std::uint64_t ScopedAlphaMode = 0;

    const std::size_t ScopedEnd = InEntry.dataStart + static_cast<std::size_t>(InEntry.size);
    std::size_t ScopedPosition = InEntry.dataStart;
    while (ScopedPosition < ScopedEnd)
    {
        Element ScopedChild;
        if (!ReadElement(ScopedPosition, ScopedChild) || ScopedChild.unknownSize)
        {
            SetError(InError, "malformed TrackEntry element");
            return false;
        }
        if (ScopedChild.id == kTrackNumberId)
        {
            (void)ReadUnsigned(ScopedChild, ScopedTrackNumber);
        }
        else if (ScopedChild.id == kTrackTypeId)
        {
            (void)ReadUnsigned(ScopedChild, ScopedTrackType);
        }
        else if (ScopedChild.id == kCodecIdId)
        {
            ScopedCodecId = ReadString(ScopedChild);
        }
        else if (ScopedChild.id == kCodecPrivateId)
        {
            ScopedHasCodecPrivate = true;
            if (RangeInside(ScopedChild.dataStart, ScopedChild.size))
            {
                ScopedCodecPrivate = MBytes.subspan(ScopedChild.dataStart, static_cast<std::size_t>(ScopedChild.size));
            }
        }
        else if (ScopedChild.id == kDefaultDurationId)
        {
            (void)ReadUnsigned(ScopedChild, ScopedDefaultDuration);
        }
        else if (ScopedChild.id == kVideoId)
        {
            const std::size_t ScopedVideoEnd = ScopedChild.dataStart + static_cast<std::size_t>(ScopedChild.size);
            std::size_t ScopedVideoPosition = ScopedChild.dataStart;
            while (ScopedVideoPosition < ScopedVideoEnd)
            {
                Element ScopedVideoChild;
                if (!ReadElement(ScopedVideoPosition, ScopedVideoChild) || ScopedVideoChild.unknownSize)
                {
                    SetError(InError, "malformed TrackEntry/Video element");
                    return false;
                }
                std::uint64_t ScopedValue = 0;
                if (ScopedVideoChild.id == kPixelWidthId && ReadUnsigned(ScopedVideoChild, ScopedValue))
                {
                    ScopedWidth = static_cast<std::uint32_t>(ScopedValue);
                }
                else if (ScopedVideoChild.id == kPixelHeightId && ReadUnsigned(ScopedVideoChild, ScopedValue))
                {
                    ScopedHeight = static_cast<std::uint32_t>(ScopedValue);
                }
                else if (ScopedVideoChild.id == kAlphaModeId && ReadUnsigned(ScopedVideoChild, ScopedValue))
                {
                    ScopedAlphaMode = ScopedValue;
                }
                ScopedVideoPosition = ScopedVideoChild.dataStart + static_cast<std::size_t>(ScopedVideoChild.size);
            }
        }
        ScopedPosition = ScopedChild.dataStart + static_cast<std::size_t>(ScopedChild.size);
    }

    // TrackType 1 is video; audio (2), subtitles (17) and everything else are
    if (ScopedTrackType != 1)
    {
        return true;
    }

    if (ScopedTrackNumber == 0 || ScopedTrackNumber > std::numeric_limits<std::uint32_t>::max())
    {
        SetError(InError, "video TrackEntry has an invalid TrackNumber");
        return false;
    }
    if (MTrack.trackNumber != 0)
    {
        SetError(InError, "container has more than one video track; VideoStream supports exactly one VP9 track");
        return false;
    }
    if (ScopedCodecId.empty())
    {
        SetError(InError, "video TrackEntry has no CodecID");
        return false;
    }
    if (!ValidateVideoCodec(ScopedCodecId, InError))
    {
        return false;
    }
    if (ScopedWidth == 0 || ScopedHeight == 0 || ScopedWidth > 16384 || ScopedHeight > 16384)
    {
        SetError(InError, "video track has invalid PixelWidth/PixelHeight");
        return false;
    }
    if (ScopedAlphaMode > 1)
    {
        SetError(InError, std::format("video track has invalid AlphaMode {}", ScopedAlphaMode));
        return false;
    }
    if (ScopedHasCodecPrivate)
    {
        if (ScopedCodecPrivate.empty())
        {
            SetError(InError, "V_VP9 CodecPrivate is empty");
            return false;
        }
        if (!ParseVp9CodecPrivate(ScopedCodecPrivate, InError))
        {
            return false;
        }
    }

    MTrack.trackNumber = static_cast<std::uint32_t>(ScopedTrackNumber);
    MTrack.width = ScopedWidth;
    MTrack.height = ScopedHeight;
    MTrack.defaultDurationNs = ScopedDefaultDuration;
    MTrack.alpha = ScopedAlphaMode == 1;
    MTrack.hasCodecPrivate = ScopedHasCodecPrivate;
    const std::size_t ScopedCodecCopy = std::min(ScopedCodecId.size(), sizeof(MTrack.codecId) - 1);
    std::memcpy(MTrack.codecId, ScopedCodecId.data(), ScopedCodecCopy);
    MTrack.codecId[ScopedCodecCopy] = '\0';
    return true;
}

bool WebmDemuxer::ParseVp9CodecPrivate(std::span<const std::byte> InData, std::string* InError)
{
    // VPCodecConfigurationRecord (ISO/IEC 14496-15 style, 8 bytes + init data):
    if (InData.size() < 8)
    {
        SetError(InError, "V_VP9 CodecPrivate is shorter than the 8-byte VPCodecConfigurationRecord");
        return false;
    }
    const auto ScopedByteAt = [&InData](std::size_t InIndex) { return static_cast<std::uint8_t>(InData[InIndex]); };

    const std::uint8_t ScopedProfile = ScopedByteAt(0);
    const std::uint8_t ScopedLevel = ScopedByteAt(1);
    const std::uint8_t ScopedBitField = ScopedByteAt(2);
    const std::uint8_t ScopedBitDepth = static_cast<std::uint8_t>(ScopedBitField >> 4);
    const std::uint8_t ScopedChromaSubsampling = static_cast<std::uint8_t>((ScopedBitField >> 1) & 0x7u);
    const bool ScopedFullRange = (ScopedBitField & 0x1u) != 0;
    const std::uint16_t ScopedInitializationBytes =
        static_cast<std::uint16_t>((static_cast<std::uint16_t>(ScopedByteAt(6)) << 8) | ScopedByteAt(7));

    if (ScopedProfile > 3)
    {
        SetError(InError, std::format("VP9 profile {} is invalid", ScopedProfile));
        return false;
    }
    if (ScopedBitDepth != 8)
    {
        SetError(InError,
                 std::format("VP9 bit depth {} is not supported: VideoStream decodes 8-bit I420 only", ScopedBitDepth));
        return false;
    }
    if (ScopedChromaSubsampling > 1)
    {
        SetError(InError, std::format("VP9 chroma subsampling mode {} is not 4:2:0; VideoStream requires 4:2:0",
                                      ScopedChromaSubsampling));
        return false;
    }
    if (ScopedInitializationBytes != InData.size() - 8)
    {
        SetError(InError, "V_VP9 CodecPrivate initialization-data size does not match the element");
        return false;
    }

    MTrack.profile = ScopedProfile;
    MTrack.level = ScopedLevel;
    MTrack.bitDepth = ScopedBitDepth;
    MTrack.fullRange = ScopedFullRange;
    return true;
}

bool WebmDemuxer::ValidateVideoCodec(std::string_view InCodecId, std::string* InError)
{
    if (InCodecId == "V_VP9")
    {
        return true;
    }
    if (InCodecId == "V_VP8")
    {
        SetError(InError, "codec 'V_VP8' is not supported: VideoStream requires VP9 (V_VP9)");
        return false;
    }
    if (InCodecId == "V_AV1")
    {
        SetError(InError, "codec 'V_AV1' (AV1) is not supported: VideoStream requires VP9 (V_VP9)");
        return false;
    }
    if (InCodecId == "V_MPEG4/ISO/AVC")
    {
        SetError(InError, "codec 'V_MPEG4/ISO/AVC' (H.264) is not supported: VideoStream requires VP9 (V_VP9)");
        return false;
    }
    if (InCodecId == "V_MPEGH/ISO/HEVC")
    {
        SetError(InError, "codec 'V_MPEGH/ISO/HEVC' (H.265) is not supported: VideoStream requires VP9 (V_VP9)");
        return false;
    }
    SetError(InError, std::format("video codec '{}' is not supported: VideoStream requires VP9 (V_VP9)", InCodecId));
    return false;
}

ContainerKind SniffContainer(std::span<const std::byte> InBytes) noexcept
{
    if (InBytes.size() >= 4 && static_cast<std::uint8_t>(InBytes[0]) == 0x1A &&
        static_cast<std::uint8_t>(InBytes[1]) == 0x45 && static_cast<std::uint8_t>(InBytes[2]) == 0xDF &&
        static_cast<std::uint8_t>(InBytes[3]) == 0xA3)
    {
        return ContainerKind::Ebml;
    }
    if (InBytes.size() >= 8 && static_cast<char>(InBytes[4]) == 'f' && static_cast<char>(InBytes[5]) == 't' &&
        static_cast<char>(InBytes[6]) == 'y' && static_cast<char>(InBytes[7]) == 'p')
    {
        return ContainerKind::Mp4;
    }
    return ContainerKind::Unknown;
}

// Open / Rewind / Close
bool WebmDemuxer::Open(std::span<const std::byte> InBytes, std::string* InError)
{
    Close();
    MBytes = InBytes;
    if (MBytes.empty())
    {
        SetError(InError, "video stream is empty");
        return false;
    }

    switch (SniffContainer(MBytes))
    {
        case ContainerKind::Mp4:
            SetError(InError,
                     "MP4/ISO-BMFF container (ftyp box) is not supported: VideoStream requires WebM with a VP9 video "
                     "track");
            return false;
        case ContainerKind::Ebml:
            break;
        case ContainerKind::Unknown:
        default:
            SetError(InError, "not an EBML/WebM stream (missing the EBML header magic)");
            return false;
    }

    if (!ParseEbmlHeader(InError))
    {
        return false;
    }

    // Locate the Segment. Void/CRC padding and a SeekHead may precede it.
    Element ScopedHeader;
    // ParseEbmlHeader already validated it
    (void)ReadElement(0, ScopedHeader);
    std::size_t ScopedPosition = ScopedHeader.dataStart + static_cast<std::size_t>(ScopedHeader.size);
    bool ScopedFoundSegment = false;
    while (ScopedPosition < MBytes.size())
    {
        Element ScopedElement;
        if (!ReadElement(ScopedPosition, ScopedElement))
        {
            SetError(InError, "truncated element before the Segment");
            return false;
        }
        if (ScopedElement.id == kSegmentId)
        {
            MSegmentStart = ScopedElement.dataStart;
            if (ScopedElement.unknownSize)
            {
                MSegmentEnd = MBytes.size();
            }
            else
            {
                MSegmentEnd = ScopedElement.dataStart + static_cast<std::size_t>(ScopedElement.size);
            }
            ScopedFoundSegment = true;
            break;
        }
        if (IsSkippableAnywhere(ScopedElement.id) && !ScopedElement.unknownSize)
        {
            ScopedPosition = ScopedElement.dataStart + static_cast<std::size_t>(ScopedElement.size);
            continue;
        }
        SetError(InError, "unexpected element before the Segment");
        return false;
    }
    if (!ScopedFoundSegment)
    {
        SetError(InError, "WebM stream has no Segment element");
        return false;
    }

    // Walk the Segment's level-1 children until the first Cluster: Info and
    bool ScopedSawTracks = false;
    MScanPosition = MSegmentStart;
    while (MScanPosition < MSegmentEnd)
    {
        Element ScopedElement;
        if (!ReadElement(MScanPosition, ScopedElement))
        {
            SetError(InError, "truncated level-1 element in the Segment");
            return false;
        }
        if (ScopedElement.id == kClusterId)
        {
            if (!ScopedSawTracks)
            {
                SetError(InError, "WebM Segment has a Cluster before the Tracks element");
                return false;
            }
            MFirstClusterOffset = MScanPosition;
            break;
        }
        if (ScopedElement.id == kInfoId)
        {
            if (ScopedElement.unknownSize || !ParseSegmentInfo(ScopedElement, InError))
            {
                SetError(InError, InError != nullptr && !InError->empty() ? *InError : "malformed Info element");
                return false;
            }
        }
        else if (ScopedElement.id == kTracksId)
        {
            if (ScopedElement.unknownSize || !ParseTracks(ScopedElement, InError))
            {
                SetError(InError, InError != nullptr && !InError->empty() ? *InError : "malformed Tracks element");
                return false;
            }
            ScopedSawTracks = true;
        }
        else if (!IsTopLevelId(ScopedElement.id) && !IsSkippableAnywhere(ScopedElement.id))
        {
            SetError(InError, std::format("unexpected level-1 element 0x{:X} in the Segment", ScopedElement.id));
            return false;
        }
        if (ScopedElement.unknownSize)
        {
            SetError(InError, std::format("level-1 element 0x{:X} has unknown size", ScopedElement.id));
            return false;
        }
        MScanPosition = ScopedElement.dataStart + static_cast<std::size_t>(ScopedElement.size);
    }
    if (MFirstClusterOffset == 0)
    {
        MFirstClusterOffset = MSegmentEnd;
    }

    if (!ScopedSawTracks)
    {
        SetError(InError, "WebM Segment has no Tracks element");
        return false;
    }
    if (MTrack.trackNumber == 0)
    {
        SetError(InError, "WebM file has no video track; VideoStream requires a V_VP9 video track");
        return false;
    }
    if (MTrack.timestampScaleNs == 0 || MTrack.timestampScaleNs > 1000000000ull)
    {
        SetError(InError, "WebM TimestampScale is out of range");
        return false;
    }

    MOpen = true;
    Rewind();
    return true;
}

void WebmDemuxer::Rewind() noexcept
{
    MScanPosition = MFirstClusterOffset != 0 ? MFirstClusterOffset : MSegmentEnd;
    MClusterEnd = MScanPosition;
    MClusterStart = MScanPosition;
    MClusterTimestampTicks = 0;
    MInCluster = false;
    MClusterUnknownSize = false;
    MFinishedClusters = MScanPosition >= MSegmentEnd;
    MFatal = false;
}

void WebmDemuxer::Close() noexcept
{
    MBytes = {};
    MSegmentStart = 0;
    MSegmentEnd = 0;
    MFirstClusterOffset = 0;
    MScanPosition = 0;
    MClusterEnd = 0;
    MClusterStart = 0;
    MClusterTimestampTicks = 0;
    MInCluster = false;
    MClusterUnknownSize = false;
    MFinishedClusters = false;
    MOpen = false;
    MFatal = false;
    MTrack = {};
}

// Frame walking
bool WebmDemuxer::ParseBlockHeader(std::span<const std::byte> InData, std::uint32_t& InTrackNumber,
                                   std::int16_t& InRelativeTimestamp, std::uint8_t& InFlags, std::uint8_t& InLacing,
                                   std::span<const std::byte>& InPayload) const
{
    std::size_t ScopedPosition = 0;
    std::uint64_t ScopedTrack = 0;
    if (!ReadSpanVint(InData, ScopedPosition, ScopedTrack) || ScopedTrack == 0 ||
        ScopedTrack > std::numeric_limits<std::uint32_t>::max())
    {
        return false;
    }
    if (ScopedPosition + 3 > InData.size())
    {
        return false;
    }
    const auto ScopedHigh = static_cast<std::uint8_t>(InData[ScopedPosition]);
    const auto ScopedLow = static_cast<std::uint8_t>(InData[ScopedPosition + 1]);
    const auto ScopedRawTimestamp =
        static_cast<std::uint16_t>((static_cast<std::uint16_t>(ScopedHigh) << 8) | ScopedLow);
    InRelativeTimestamp = static_cast<std::int16_t>(ScopedRawTimestamp);
    InFlags = static_cast<std::uint8_t>(InData[ScopedPosition + 2]);
    InLacing = static_cast<std::uint8_t>((InFlags >> 1) & 0x3u);
    InPayload = InData.subspan(ScopedPosition + 3);
    InTrackNumber = static_cast<std::uint32_t>(ScopedTrack);
    return true;
}

bool WebmDemuxer::ParseBlockGroup(const Element& InGroup, WebmFrame& InFrame, bool& InIsVideoTrack,
                                  std::string* InError)
{
    InIsVideoTrack = false;
    bool ScopedHaveBlock = false;
    bool ScopedHaveAlpha = false;
    std::span<const std::byte> ScopedPayload{};
    std::uint8_t ScopedBlockFlags = 0;
    std::int16_t ScopedRelativeTimestamp = 0;
    std::uint64_t ScopedDurationTicks = 0;
    bool ScopedHaveDuration = false;

    const std::size_t ScopedEnd = InGroup.dataStart + static_cast<std::size_t>(InGroup.size);
    std::size_t ScopedPosition = InGroup.dataStart;
    while (ScopedPosition < ScopedEnd)
    {
        Element ScopedChild;
        if (!ReadElement(ScopedPosition, ScopedChild) || ScopedChild.unknownSize)
        {
            SetError(InError, "malformed BlockGroup element");
            return false;
        }

        if (ScopedChild.id == kBlockId)
        {
            std::span<const std::byte> ScopedBlockPayload{};
            std::uint32_t ScopedTrackNumber = 0;
            std::uint8_t ScopedLacing = 0;
            if (!ParseBlockHeader(MBytes.subspan(ScopedChild.dataStart, static_cast<std::size_t>(ScopedChild.size)),
                                  ScopedTrackNumber, ScopedRelativeTimestamp, ScopedBlockFlags, ScopedLacing,
                                  ScopedBlockPayload))
            {
                SetError(InError, "malformed Block inside a BlockGroup");
                return false;
            }
            if (ScopedTrackNumber == MTrack.trackNumber)
            {
                if (ScopedLacing != 0)
                {
                    SetError(InError, "laced video blocks are not supported; a VP9 video track never laces");
                    return false;
                }
                ScopedPayload = ScopedBlockPayload;
                ScopedHaveBlock = true;
            }
        }
        else if (ScopedChild.id == kBlockDurationId)
        {
            if (!ReadUnsigned(ScopedChild, ScopedDurationTicks))
            {
                SetError(InError, "malformed BlockDuration inside a BlockGroup");
                return false;
            }
            ScopedHaveDuration = true;
        }
        else if (ScopedChild.id == kBlockAdditionsId)
        {
            const std::size_t ScopedAdditionsEnd = ScopedChild.dataStart + static_cast<std::size_t>(ScopedChild.size);
            std::size_t ScopedAdditionsPosition = ScopedChild.dataStart;
            while (ScopedAdditionsPosition < ScopedAdditionsEnd)
            {
                Element ScopedMore;
                if (!ReadElement(ScopedAdditionsPosition, ScopedMore) || ScopedMore.unknownSize)
                {
                    SetError(InError, "malformed BlockAdditions element");
                    return false;
                }
                if (ScopedMore.id != kBlockMoreId)
                {
                    ScopedAdditionsPosition = ScopedMore.dataStart + static_cast<std::size_t>(ScopedMore.size);
                    continue;
                }
                // BlockMore: optional BlockAddID (default 1) and the payload.
                std::uint64_t ScopedAddId = 1;
                std::span<const std::byte> ScopedAdditional{};
                const std::size_t ScopedMoreEnd = ScopedMore.dataStart + static_cast<std::size_t>(ScopedMore.size);
                std::size_t ScopedMorePosition = ScopedMore.dataStart;
                while (ScopedMorePosition < ScopedMoreEnd)
                {
                    Element ScopedField;
                    if (!ReadElement(ScopedMorePosition, ScopedField) || ScopedField.unknownSize)
                    {
                        SetError(InError, "malformed BlockMore element");
                        return false;
                    }
                    if (ScopedField.id == kBlockAddIdId)
                    {
                        if (!ReadUnsigned(ScopedField, ScopedAddId))
                        {
                            SetError(InError, "malformed BlockAddID");
                            return false;
                        }
                    }
                    else if (ScopedField.id == kBlockAdditionalId)
                    {
                        ScopedAdditional =
                            MBytes.subspan(ScopedField.dataStart, static_cast<std::size_t>(ScopedField.size));
                    }
                    ScopedMorePosition = ScopedField.dataStart + static_cast<std::size_t>(ScopedField.size);
                }
                // BlockAddID 1 is the WebM alpha plane: a complete second VP9
                if (ScopedAddId == 1 && !ScopedAdditional.empty())
                {
                    InFrame.alpha = ScopedAdditional;
                    ScopedHaveAlpha = true;
                }
                ScopedAdditionsPosition = ScopedMore.dataStart + static_cast<std::size_t>(ScopedMore.size);
            }
        }
        ScopedPosition = ScopedChild.dataStart + static_cast<std::size_t>(ScopedChild.size);
    }

    if (!ScopedHaveBlock)
    {
        // A BlockGroup of another track; the caller skips it
        return true;
    }

    const std::int64_t ScopedTicks = static_cast<std::int64_t>(MClusterTimestampTicks) + ScopedRelativeTimestamp;
    const std::uint64_t ScopedClampedTicks = ScopedTicks < 0 ? 0ull : static_cast<std::uint64_t>(ScopedTicks);
    InFrame.payload = ScopedPayload;
    InFrame.timestampNs = TimestampToNs(ScopedClampedTicks);
    InFrame.keyframe = (ScopedBlockFlags & 0x80u) != 0;
    InFrame.invisible = (ScopedBlockFlags & 0x08u) != 0;
    InFrame.hasAlpha = ScopedHaveAlpha;
    InFrame.durationNs = ScopedHaveDuration ? TimestampToNs(ScopedDurationTicks) : MTrack.defaultDurationNs;
    InIsVideoTrack = true;
    return true;
}

WebmDemuxer::ClusterScan WebmDemuxer::ReadClusterChild(const Element& InElement, std::string* InError,
                                                       WebmFrame& OutFrame)
{
    switch (InElement.id)
    {
        case kClusterTimestampId:
        {
            std::uint64_t ScopedTicks = 0;
            if (!ReadUnsigned(InElement, ScopedTicks))
            {
                SetError(InError, "malformed Cluster/Timestamp");
                return ClusterScan::Error;
            }
            MClusterTimestampTicks = ScopedTicks;
            MScanPosition = InElement.dataStart + static_cast<std::size_t>(InElement.size);
            return ClusterScan::Skip;
        }
        case kSimpleBlockId:
        {
            std::span<const std::byte> ScopedPayload{};
            std::uint32_t ScopedTrackNumber = 0;
            std::int16_t ScopedRelativeTimestamp = 0;
            std::uint8_t ScopedFlags = 0;
            std::uint8_t ScopedLacing = 0;
            if (!ParseBlockHeader(MBytes.subspan(InElement.dataStart, static_cast<std::size_t>(InElement.size)),
                                  ScopedTrackNumber, ScopedRelativeTimestamp, ScopedFlags, ScopedLacing, ScopedPayload))
            {
                SetError(InError, "malformed SimpleBlock");
                return ClusterScan::Error;
            }
            MScanPosition = InElement.dataStart + static_cast<std::size_t>(InElement.size);
            if (ScopedTrackNumber != MTrack.trackNumber)
            {
                // Another track in the same cluster (audio)
                return ClusterScan::Skip;
            }
            if (ScopedLacing != 0)
            {
                SetError(InError, "laced video blocks are not supported; a VP9 video track never laces");
                return ClusterScan::Error;
            }
            const std::int64_t ScopedTicks =
                static_cast<std::int64_t>(MClusterTimestampTicks) + ScopedRelativeTimestamp;
            const std::uint64_t ScopedClampedTicks = ScopedTicks < 0 ? 0ull : static_cast<std::uint64_t>(ScopedTicks);
            OutFrame.payload = ScopedPayload;
            OutFrame.alpha = {};
            OutFrame.timestampNs = TimestampToNs(ScopedClampedTicks);
            OutFrame.durationNs = MTrack.defaultDurationNs;
            OutFrame.keyframe = (ScopedFlags & 0x80u) != 0;
            OutFrame.invisible = (ScopedFlags & 0x08u) != 0;
            OutFrame.hasAlpha = false;
            return ClusterScan::Frame;
        }
        case kBlockGroupId:
        {
            WebmFrame ScopedFrame;
            bool ScopedIsVideoTrack = false;
            if (!ParseBlockGroup(InElement, ScopedFrame, ScopedIsVideoTrack, InError))
            {
                return ClusterScan::Error;
            }
            MScanPosition = InElement.dataStart + static_cast<std::size_t>(InElement.size);
            if (!ScopedIsVideoTrack)
            {
                return ClusterScan::Skip;
            }
            OutFrame = ScopedFrame;
            return ClusterScan::Frame;
        }
        case kVoidId:
        case kCrc32Id:
            if (InElement.unknownSize)
            {
                SetError(InError, "Void/CRC32 element has unknown size");
                return ClusterScan::Error;
            }
            MScanPosition = InElement.dataStart + static_cast<std::size_t>(InElement.size);
            return ClusterScan::Skip;
        default:
            break;
    }

    // A level-1 element after cluster children ends the cluster list. The
    if (IsTopLevelId(InElement.id))
    {
        MInCluster = false;
        MFinishedClusters = true;
        return ClusterScan::End;
    }
    if (InElement.unknownSize)
    {
        SetError(InError, std::format("cluster child element 0x{:X} has unknown size", InElement.id));
        return ClusterScan::Error;
    }
    MScanPosition = InElement.dataStart + static_cast<std::size_t>(InElement.size);
    return ClusterScan::Skip;
}

DemuxStatus WebmDemuxer::NextFrame(WebmFrame& InFrame, std::string* InError)
{
    if (!MOpen || MFatal)
    {
        SetError(InError, MFatal ? "WebM demuxer is in a failed state" : "WebM demuxer is not open");
        return DemuxStatus::Error;
    }

    for (;;)
    {
        if (MFinishedClusters)
        {
            return DemuxStatus::End;
        }

        if (!MInCluster)
        {
            if (MScanPosition >= MSegmentEnd)
            {
                MFinishedClusters = true;
                return DemuxStatus::End;
            }
            Element ScopedElement;
            if (!ReadElement(MScanPosition, ScopedElement))
            {
                MFatal = true;
                SetError(InError, "truncated element after the clusters");
                return DemuxStatus::Error;
            }
            if (ScopedElement.id == kClusterId)
            {
                MInCluster = true;
                MClusterStart = MScanPosition;
                MClusterUnknownSize = ScopedElement.unknownSize;
                MClusterEnd = ScopedElement.unknownSize
                                  ? MSegmentEnd
                                  : ScopedElement.dataStart + static_cast<std::size_t>(ScopedElement.size);
                MClusterTimestampTicks = 0;
                MScanPosition = ScopedElement.dataStart;
                continue;
            }
            if (ScopedElement.unknownSize)
            {
                MFatal = true;
                SetError(InError,
                         std::format("level-1 element 0x{:X} after clusters has unknown size", ScopedElement.id));
                return DemuxStatus::Error;
            }
            if (IsTopLevelId(ScopedElement.id) || IsSkippableAnywhere(ScopedElement.id))
            {
                MScanPosition = ScopedElement.dataStart + static_cast<std::size_t>(ScopedElement.size);
                continue;
            }
            MFatal = true;
            SetError(InError, std::format("unexpected element 0x{:X} after the clusters", ScopedElement.id));
            return DemuxStatus::Error;
        }

        if (MScanPosition >= MClusterEnd)
        {
            MInCluster = false;
            continue;
        }
        Element ScopedElement;
        if (!ReadElement(MScanPosition, ScopedElement))
        {
            MFatal = true;
            SetError(InError, "truncated element inside a Cluster");
            return DemuxStatus::Error;
        }
        // An unknown-size Cluster ends when the next Cluster starts.
        if (ScopedElement.id == kClusterId)
        {
            MInCluster = false;
            continue;
        }
        switch (ReadClusterChild(ScopedElement, InError, InFrame))
        {
            case ClusterScan::Frame:
                return DemuxStatus::Frame;
            case ClusterScan::Skip:
                continue;
            case ClusterScan::End:
                return DemuxStatus::End;
            case ClusterScan::Error:
                MFatal = true;
                return DemuxStatus::Error;
        }
    }
}
}
