// FemboyEngine2D - WebM/EBML demuxer for VP9 (with optional alpha).
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace Fby::Video
{

// What the tracks element told us about the single video track we accept.
struct WebmTrackInfo
{
    std::uint32_t trackNumber = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    // TimestampScale in nanoseconds per timestamp tick (WebM default: 1 ms).
    std::uint64_t timestampScaleNs = 1000000ull;
    // DefaultDuration in nanoseconds (0 = not present; the container carries no
    std::uint64_t defaultDurationNs = 0;
    // Whole-stream duration in nanoseconds when the Info element carries it.
    std::uint64_t durationNs = 0;
    // True when the Video element's AlphaMode flag is 1: every frame may carry
    bool alpha = false;
    bool hasCodecPrivate = false;
    // VpcC videoFullRangeFlag
    bool fullRange = false;
    std::uint8_t profile = 0;
    std::uint8_t level = 0;
    std::uint8_t bitDepth = 8;
    // The accepted codec id; always "V_VP9" after a successful Open().
    char codecId[32] = {};
};

// One demuxed frame. Both spans point into the mapped container and stay valid
struct WebmFrame
{
    // Compressed VP9 frame
    std::span<const std::byte> payload;
    // Compressed alpha VP9 frame (may be empty)
    std::span<const std::byte> alpha;
    std::uint64_t timestampNs = 0;
    std::uint64_t durationNs = 0;
    bool keyframe = false;
    bool hasAlpha = false;
    // Matroska's "invisible" SimpleBlock flag: the frame must be decoded (it
    bool invisible = false;
};

enum class DemuxStatus : std::uint8_t
{
    Frame = 0,
    End,
    Error,
};

// Streaming demuxer. Open() parses the EBML header, Segment/Info and
class WebmDemuxer
{
public:
    WebmDemuxer() = default;

    // Parses the container header and the video track. `bytes` must outlive the
    [[nodiscard]] bool Open(std::span<const std::byte> InBytes, std::string* InError = nullptr);
    void Close() noexcept;

    // Returns Frame and fills `frame`; returns End at the end of the stream;
    [[nodiscard]] DemuxStatus NextFrame(WebmFrame& InFrame, std::string* InError = nullptr);

    // Restarts at the first cluster. Used by the looping player; no allocation.
    void Rewind() noexcept;

    [[nodiscard]] bool IsOpen() const noexcept
    {
        return MOpen;
    }
    [[nodiscard]] const WebmTrackInfo& Track() const noexcept
    {
        return MTrack;
    }

private:
    struct Element
    {
        std::uint32_t id = 0;
        std::uint64_t size = 0;
        std::size_t dataStart = 0;
        bool unknownSize = false;
    };

    // Low-level EBML primitives. Every one bounds-checks and leaves a message
    [[nodiscard]] bool ReadElement(std::size_t InPosition, Element& InElement) const;
    [[nodiscard]] bool ReadId(std::size_t InPosition, std::uint32_t& InId, std::size_t& InHeaderBytes) const;
    [[nodiscard]] bool ReadVintValue(std::size_t InPosition, std::uint64_t& InValue, std::size_t& InBytes) const;
    [[nodiscard]] bool ReadUnsigned(const Element& InElement, std::uint64_t& InValue) const;
    [[nodiscard]] bool ReadFloat(const Element& InElement, double& InValue) const;
    [[nodiscard]] std::string_view ReadString(const Element& InElement) const;

    [[nodiscard]] bool ParseEbmlHeader(std::string* InError);
    [[nodiscard]] bool ParseSegmentInfo(const Element& InSegment, std::string* InError);
    [[nodiscard]] bool ParseTracks(const Element& InTracks, std::string* InError);
    [[nodiscard]] bool ParseTrackEntry(const Element& InEntry, std::string* InError);
    [[nodiscard]] bool ParseVp9CodecPrivate(std::span<const std::byte> InData, std::string* InError);

    // Cluster walking. ReadClusterChild() returns Frame and fills `outFrame`,
    enum class ClusterScan : std::uint8_t
    {
        Skip = 0,
        Frame,
        End,
        Error,
    };
    [[nodiscard]] ClusterScan ReadClusterChild(const Element& InElement, std::string* InError, WebmFrame& OutFrame);
    [[nodiscard]] bool ParseBlockHeader(std::span<const std::byte> InData, std::uint32_t& InTrackNumber,
                                        std::int16_t& InRelativeTimestamp, std::uint8_t& InFlags,
                                        std::uint8_t& InLacing, std::span<const std::byte>& InPayload) const;
    [[nodiscard]] bool ParseBlockGroup(const Element& InGroup, WebmFrame& InFrame, bool& InIsVideoTrack,
                                       std::string* InError);

    // Applies the video-track checks to a codec id; this is where VP8/AV1/H.264
    [[nodiscard]] bool ValidateVideoCodec(std::string_view InCodecId, std::string* InError);

    [[nodiscard]] bool RangeInside(std::uint64_t InOffset, std::uint64_t InSize) const noexcept;
    [[nodiscard]] std::uint64_t TimestampToNs(std::uint64_t InTicks) const noexcept;

    std::span<const std::byte> MBytes{};
    // First byte of Segment payload
    std::size_t MSegmentStart = 0;
    // One past the Segment payload
    std::size_t MSegmentEnd = 0;
    std::size_t MFirstClusterOffset = 0;
    // Next element to consider
    std::size_t MScanPosition = 0;
    // End of the current cluster's children
    std::size_t MClusterEnd = 0;
    // For a Rewind() to the first cluster
    std::size_t MClusterStart = 0;
    std::uint64_t MClusterTimestampTicks = 0;
    bool MInCluster = false;
    bool MClusterUnknownSize = false;
    bool MFinishedClusters = false;
    bool MOpen = false;
    bool MFatal = false;
    WebmTrackInfo MTrack{};
};

// Detects the container from the first bytes without parsing anything. Used by
enum class ContainerKind : std::uint8_t
{
    Unknown = 0,
    // Starts with the EBML magic (WebM or Matroska; DocType decides)
    Ebml,
    // ISO base media file format: 'ftyp' at offset 4
    Mp4,
};

[[nodiscard]] ContainerKind SniffContainer(std::span<const std::byte> InBytes) noexcept;
}
