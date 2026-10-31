// FemboyEngine2D - video module tests.
#include "Engine/Video/VideoStream.hpp"

#include "Engine/Assets/TextureFormat.hpp"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace
{

int GFailures = 0;

#define FBY_CHECK(condition)                                                                                           \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #condition);                                  \
            ++GFailures;                                                                                              \
        }                                                                                                              \
    } while (0)

// Allocation counter: every `new` in the process goes through these. The test
std::atomic<std::size_t> GAllocations{0};
}

void* operator new(std::size_t InSize)
{
    GAllocations.fetch_add(1, std::memory_order_relaxed);
    void* ScopedMemory = std::malloc(InSize == 0 ? 1 : InSize);
    if (ScopedMemory == nullptr)
    {
        throw std::bad_alloc();
    }
    return ScopedMemory;
}

void* operator new[](std::size_t InSize)
{
    return ::operator new(InSize);
}

void operator delete(void* InMemory) noexcept
{
    std::free(InMemory);
}

void operator delete[](void* InMemory) noexcept
{
    std::free(InMemory);
}

void operator delete(void* InMemory, std::size_t) noexcept
{
    std::free(InMemory);
}

void operator delete[](void* InMemory, std::size_t) noexcept
{
    std::free(InMemory);
}

namespace
{

using Fby::Video::DecodeStatus;
using Fby::Video::VideoPlayer;
using Fby::Video::VideoStreamComponent;
using Fby::Video::VideoSystem;
using Bytes = std::vector<std::byte>;

// Minimal EBML writer for synthetic WebM files
constexpr std::uint32_t kEbmlHeaderId = 0x1A45DFA3u;
constexpr std::uint32_t kEbmlDocTypeId = 0x4282u;
constexpr std::uint32_t kEbmlReadVersionId = 0x42F7u;
constexpr std::uint32_t kSegmentId = 0x18538067u;
constexpr std::uint32_t kInfoId = 0x1549A966u;
constexpr std::uint32_t kTimestampScaleId = 0x2AD7B1u;
constexpr std::uint32_t kTracksId = 0x1654AE6Bu;
constexpr std::uint32_t kTrackEntryId = 0xAEu;
constexpr std::uint32_t kTrackNumberId = 0xD7u;
constexpr std::uint32_t kTrackTypeId = 0x83u;
constexpr std::uint32_t kCodecIdId = 0x86u;
constexpr std::uint32_t kCodecPrivateId = 0x63A2u;
constexpr std::uint32_t kVideoId = 0xE0u;
constexpr std::uint32_t kPixelWidthId = 0xB0u;
constexpr std::uint32_t kPixelHeightId = 0xBAu;
constexpr std::uint32_t kAlphaModeId = 0x53C0u;
constexpr std::uint32_t kClusterId = 0x1F43B675u;
constexpr std::uint32_t kClusterTimestampId = 0xE7u;
constexpr std::uint32_t kSimpleBlockId = 0xA3u;
constexpr std::uint32_t kBlockGroupId = 0xA0u;
constexpr std::uint32_t kBlockId = 0xA1u;
constexpr std::uint32_t kBlockAdditionsId = 0x75A1u;
constexpr std::uint32_t kBlockMoreId = 0xA6u;
constexpr std::uint32_t kBlockAddIdId = 0xEEu;
constexpr std::uint32_t kBlockAdditionalId = 0xA5u;

[[nodiscard]] Bytes BytesOf(std::initializer_list<std::uint8_t> InValues)
{
    Bytes ScopedOut;
    ScopedOut.reserve(InValues.size());
    for (const std::uint8_t ScopedValue : InValues)
    {
        ScopedOut.push_back(static_cast<std::byte>(ScopedValue));
    }
    return ScopedOut;
}

[[nodiscard]] Bytes EncodeId(std::uint32_t InId)
{
    std::uint32_t ScopedRemaining = InId;
    std::uint32_t ScopedLength = 1;
    while (ScopedRemaining > 0xFFu)
    {
        ScopedRemaining >>= 8;
        ++ScopedLength;
    }
    Bytes ScopedOut(ScopedLength);
    for (std::uint32_t ScopedIndex = ScopedLength; ScopedIndex > 0; --ScopedIndex)
    {
        ScopedOut[ScopedIndex - 1] = static_cast<std::byte>(InId & 0xFFu);
        InId >>= 8;
    }
    return ScopedOut;
}

[[nodiscard]] Bytes EncodeSize(std::uint64_t InValue)
{
    // All-ones is the "unknown size" marker, so a known size always needs a
    for (std::uint32_t ScopedLength = 1; ScopedLength <= 8; ++ScopedLength)
    {
        const std::uint64_t ScopedCapacity = (1ull << (7 * ScopedLength)) - 1ull;
        if (ScopedLength == 8 || InValue < ScopedCapacity)
        {
            Bytes ScopedOut(ScopedLength, std::byte{0});
            std::uint64_t ScopedRemaining = InValue;
            for (std::uint32_t ScopedIndex = ScopedLength; ScopedIndex > 0; --ScopedIndex)
            {
                ScopedOut[ScopedIndex - 1] = static_cast<std::byte>(ScopedRemaining & 0xFFu);
                ScopedRemaining >>= 8;
            }
            ScopedOut[0] =
                static_cast<std::byte>(static_cast<std::uint8_t>(ScopedOut[0]) | (0x80u >> (ScopedLength - 1)));
            return ScopedOut;
        }
    }
    return BytesOf({0xFF});
}

[[nodiscard]] Bytes Element(std::uint32_t InId, const Bytes& InPayload)
{
    Bytes ScopedOut = EncodeId(InId);
    const Bytes ScopedSize = EncodeSize(InPayload.size());
    ScopedOut.insert(ScopedOut.end(), ScopedSize.begin(), ScopedSize.end());
    ScopedOut.insert(ScopedOut.end(), InPayload.begin(), InPayload.end());
    return ScopedOut;
}

[[nodiscard]] Bytes UintElement(std::uint32_t InId, std::uint64_t InValue)
{
    Bytes ScopedPayload;
    do
    {
        ScopedPayload.insert(ScopedPayload.begin(), static_cast<std::byte>(InValue & 0xFFu));
        InValue >>= 8;
    } while (InValue != 0);
    return Element(InId, ScopedPayload);
}

[[nodiscard]] Bytes StringElement(std::uint32_t InId, std::string_view InText)
{
    Bytes ScopedPayload(InText.size());
    std::memcpy(ScopedPayload.data(), InText.data(), InText.size());
    return Element(InId, ScopedPayload);
}

[[nodiscard]] Bytes BlockPayload(const Bytes& InFrame, std::int16_t InRelativeTimestamp)
{
    // Track number 1 as a one-byte VINT (0x80 | 1), then a big-endian int16
    Bytes ScopedPayload = BytesOf({0x81, static_cast<std::uint8_t>((InRelativeTimestamp >> 8) & 0xFF),
                                   static_cast<std::uint8_t>(InRelativeTimestamp & 0xFF), 0x80});
    ScopedPayload.insert(ScopedPayload.end(), InFrame.begin(), InFrame.end());
    return ScopedPayload;
}

struct WebmBuilder
{
    std::string codecId = "V_VP9";
    std::string docType = "webm";
    bool alpha = false;
    bool includeVpcC = true;
    bool audioTrack = false;
    // 8-bit, 4:2:0, limited range
    std::uint8_t vpcCBitField = 0x80;
    std::uint64_t timestampScale = 1000000ull;
    std::uint32_t frameCount = 3;
    std::uint32_t width = 4;
    std::uint32_t height = 4;
    std::uint32_t frameSpacingMs = 20;
    // Use BlockGroup even without alpha (edge case)
    bool blockGroups = false;
};

// Builds a complete, valid-enough WebM file. Every frame payload is
[[nodiscard]] Bytes BuildWebm(const WebmBuilder& InBuilder)
{
    Bytes ScopedEbmlHeader;
    ScopedEbmlHeader =
        Element(kEbmlHeaderId,
                [&]
                {
                    Bytes ScopedChildren = StringElement(kEbmlDocTypeId, InBuilder.docType);
                    const Bytes ScopedReadVersion = UintElement(kEbmlReadVersionId, 1);
                    ScopedChildren.insert(ScopedChildren.end(), ScopedReadVersion.begin(), ScopedReadVersion.end());
                    return ScopedChildren;
                }());

    Bytes ScopedInfo = UintElement(kTimestampScaleId, InBuilder.timestampScale);
    ScopedInfo = Element(kInfoId, ScopedInfo);

    Bytes ScopedVideo = UintElement(kPixelWidthId, InBuilder.width);
    const Bytes ScopedHeight = UintElement(kPixelHeightId, InBuilder.height);
    ScopedVideo.insert(ScopedVideo.end(), ScopedHeight.begin(), ScopedHeight.end());
    if (InBuilder.alpha)
    {
        const Bytes ScopedAlphaMode = UintElement(kAlphaModeId, 1);
        ScopedVideo.insert(ScopedVideo.end(), ScopedAlphaMode.begin(), ScopedAlphaMode.end());
    }

    Bytes ScopedVideoTrack = UintElement(kTrackNumberId, 1);
    const Bytes ScopedTrackType = UintElement(kTrackTypeId, 1);
    ScopedVideoTrack.insert(ScopedVideoTrack.end(), ScopedTrackType.begin(), ScopedTrackType.end());
    const Bytes ScopedCodec = StringElement(kCodecIdId, InBuilder.codecId);
    ScopedVideoTrack.insert(ScopedVideoTrack.end(), ScopedCodec.begin(), ScopedCodec.end());
    if (InBuilder.includeVpcC)
    {
        const Bytes ScopedCodecPrivate =
            Element(kCodecPrivateId, BytesOf({0x00, 0x0A, InBuilder.vpcCBitField, 0x01, 0x01, 0x01, 0x00, 0x00}));
        ScopedVideoTrack.insert(ScopedVideoTrack.end(), ScopedCodecPrivate.begin(), ScopedCodecPrivate.end());
    }
    const Bytes ScopedVideoElement = Element(kVideoId, ScopedVideo);
    ScopedVideoTrack.insert(ScopedVideoTrack.end(), ScopedVideoElement.begin(), ScopedVideoElement.end());

    Bytes ScopedTracks;
    if (InBuilder.audioTrack)
    {
        Bytes ScopedAudioTrack = UintElement(kTrackNumberId, 2);
        const Bytes ScopedAudioType = UintElement(kTrackTypeId, 2);
        ScopedAudioTrack.insert(ScopedAudioTrack.end(), ScopedAudioType.begin(), ScopedAudioType.end());
        const Bytes ScopedAudioCodec = StringElement(kCodecIdId, "A_OPUS");
        ScopedAudioTrack.insert(ScopedAudioTrack.end(), ScopedAudioCodec.begin(), ScopedAudioCodec.end());
        const Bytes ScopedAudioEntry = Element(kTrackEntryId, ScopedAudioTrack);
        ScopedTracks.insert(ScopedTracks.end(), ScopedAudioEntry.begin(), ScopedAudioEntry.end());
    }
    const Bytes ScopedVideoEntry = Element(kTrackEntryId, ScopedVideoTrack);
    ScopedTracks.insert(ScopedTracks.end(), ScopedVideoEntry.begin(), ScopedVideoEntry.end());
    const Bytes ScopedTracksElement = Element(kTracksId, ScopedTracks);

    Bytes ScopedClusterChildren = UintElement(kClusterTimestampId, 0);
    for (std::uint32_t ScopedFrame = 0; ScopedFrame < InBuilder.frameCount; ++ScopedFrame)
    {
        const auto ScopedSerial = static_cast<std::uint8_t>(ScopedFrame + 1);
        Bytes ScopedMainFrame = BytesOf({ScopedSerial, 0x00, 0x42});
        const std::int16_t ScopedRelativeTimestamp = static_cast<std::int16_t>(ScopedFrame * InBuilder.frameSpacingMs);
        if (InBuilder.alpha && ScopedFrame < 3)
        {
            ScopedMainFrame[1] = static_cast<std::byte>(ScopedSerial * 10);
        }
        if (InBuilder.alpha || InBuilder.blockGroups)
        {
            Bytes ScopedBlock = Element(kBlockId, BlockPayload(ScopedMainFrame, ScopedRelativeTimestamp));
            if (InBuilder.alpha)
            {
                // BlockAddID = 1 is the WebM alpha plane: a second VP9 frame
                const Bytes ScopedAlphaFrame = BytesOf({0x00, static_cast<std::uint8_t>(64 + ScopedFrame * 32), 0x00});
                Bytes ScopedBlockMore = UintElement(kBlockAddIdId, 1);
                const Bytes ScopedAdditional = Element(kBlockAdditionalId, ScopedAlphaFrame);
                ScopedBlockMore.insert(ScopedBlockMore.end(), ScopedAdditional.begin(), ScopedAdditional.end());
                const Bytes ScopedMore = Element(kBlockMoreId, ScopedBlockMore);
                const Bytes ScopedAdditions = Element(kBlockAdditionsId, ScopedMore);
                ScopedBlock.insert(ScopedBlock.end(), ScopedAdditions.begin(), ScopedAdditions.end());
            }
            const Bytes ScopedGroup = Element(kBlockGroupId, ScopedBlock);
            ScopedClusterChildren.insert(ScopedClusterChildren.end(), ScopedGroup.begin(), ScopedGroup.end());
        }
        else
        {
            const Bytes ScopedSimpleBlock =
                Element(kSimpleBlockId, BlockPayload(ScopedMainFrame, ScopedRelativeTimestamp));
            ScopedClusterChildren.insert(ScopedClusterChildren.end(), ScopedSimpleBlock.begin(),
                                         ScopedSimpleBlock.end());
        }
    }
    const Bytes ScopedCluster = Element(kClusterId, ScopedClusterChildren);

    Bytes ScopedSegment = ScopedInfo;
    ScopedSegment.insert(ScopedSegment.end(), ScopedTracksElement.begin(), ScopedTracksElement.end());
    ScopedSegment.insert(ScopedSegment.end(), ScopedCluster.begin(), ScopedCluster.end());
    const Bytes ScopedSegmentElement = Element(kSegmentId, ScopedSegment);

    Bytes ScopedFile = ScopedEbmlHeader;
    ScopedFile.insert(ScopedFile.end(), ScopedSegmentElement.begin(), ScopedSegmentElement.end());
    return ScopedFile;
}

// Builds an MP4-shaped buffer: a leading 'ftyp' box at offset 4.
[[nodiscard]] Bytes BuildMp4()
{
    return BytesOf({0x00, 0x00, 0x00, 0x18, 'f', 't', 'y', 'p', 'i', 's', 'o', 'm', 0x00, 0x00, 0x02, 0x00});
}

// Fake VP9 backend: deterministic, stateless, allocation free.
struct FakeVp9
{
    std::uint32_t createdDecoders = 0;

    static std::uint8_t FrameByte(std::span<const std::byte> InCompressed, std::size_t InIndex, std::uint8_t InFallback)
    {
        return InCompressed.size() > InIndex ? static_cast<std::uint8_t>(InCompressed[InIndex]) : InFallback;
    }

    static void* Create(void* InUser, std::uint32_t InWidth, std::uint32_t InHeight)
    {
        (void)InWidth;
        (void)InHeight;
        auto* ScopedSelf = static_cast<FakeVp9*>(InUser);
        ++ScopedSelf->createdDecoders;
        // The fake decoder has no state of its own
        return ScopedSelf;
    }

    static void Destroy(void* InUser, void* InDecoder)
    {
        (void)InUser;
        (void)InDecoder;
    }

    static bool Decode(void* InUser, void* InDecoder, std::span<const std::byte> InCompressed,
                       const Fby::Video::YuvFrameView& InTarget)
    {
        (void)InUser;
        (void)InDecoder;
        if (InDecoder == nullptr || InCompressed.empty() || !InTarget.IsValid())
        {
            return false;
        }
        const bool ScopedAlphaPlane = InTarget.chromaU == nullptr;
        // Main luma is mapped into the legal 8-bit video range so the BT.709
        const std::uint8_t ScopedValue = ScopedAlphaPlane
                                             ? FrameByte(InCompressed, 1, 0)
                                             : static_cast<std::uint8_t>(16 + FrameByte(InCompressed, 0, 1) * 60);
        for (std::uint32_t ScopedY = 0; ScopedY < InTarget.height; ++ScopedY)
        {
            std::memset(InTarget.luma + static_cast<std::size_t>(ScopedY) * InTarget.lumaStride, ScopedValue,
                        InTarget.width);
        }
        if (!ScopedAlphaPlane)
        {
            const std::uint32_t ScopedChromaWidth = (InTarget.width + 1) / 2;
            const std::uint32_t ScopedChromaHeight = (InTarget.height + 1) / 2;
            for (std::uint32_t ScopedY = 0; ScopedY < ScopedChromaHeight; ++ScopedY)
            {
                std::memset(InTarget.chromaU + static_cast<std::size_t>(ScopedY) * InTarget.chromaStride, 128,
                            ScopedChromaWidth);
                std::memset(InTarget.chromaV + static_cast<std::size_t>(ScopedY) * InTarget.chromaStride, 128,
                            ScopedChromaWidth);
            }
        }
        return true;
    }

    static const char* LastError(void*)
    {
        return "";
    }

    Fby::Video::Vp9Backend Table()
    {
        Fby::Video::Vp9Backend ScopedTable;
        ScopedTable.name = "fake-vp9";
        ScopedTable.user = this;
        ScopedTable.createDecoder = &FakeVp9::Create;
        ScopedTable.destroyDecoder = &FakeVp9::Destroy;
        ScopedTable.decodeFrame = &FakeVp9::Decode;
        ScopedTable.lastError = &FakeVp9::LastError;
        return ScopedTable;
    }
};

// Fake RHI: texture-array storage only, everything else inert.
class FakeRhi final : public Fby::Rhi::RHI
{
public:
    struct Array
    {
        Fby::Rhi::TextureArrayDesc desc{};
        std::vector<std::vector<std::byte>> layers;
    };

    std::vector<Array> arrays;
    std::uint32_t uploadCount = 0;

    bool Initialize(const Fby::Rhi::DeviceDesc&) override
    {
        return true;
    }
    void Shutdown() override
    {
    }
    void Resize(Fby::Rhi::Extent2D) override
    {
    }
    void WaitIdle() override
    {
    }
    bool BeginFrame() override
    {
        return true;
    }
    void Submit(const Fby::Rhi::FramePacket&) override
    {
    }
    void EndFrame() override
    {
    }

    Fby::Rhi::TextureArrayHandle CreateTextureArray(const Fby::Rhi::TextureArrayDesc& InDesc,
                                                    std::span<const std::byte> InPixels) override
    {
        Array ScopedArray;
        ScopedArray.desc = InDesc;
        const std::uint64_t ScopedLayerBytes =
            Fby::Assets::LayerByteSize(InDesc.format, InDesc.size.width, InDesc.size.height, InDesc.mipLevels);
        ScopedArray.layers.resize(InDesc.layers);
        for (std::vector<std::byte>& ScopedLayer : ScopedArray.layers)
        {
            ScopedLayer.resize(static_cast<std::size_t>(ScopedLayerBytes));
        }
        if (!InPixels.empty() && InPixels.size() == ScopedLayerBytes * InDesc.layers)
        {
            for (std::uint32_t ScopedLayer = 0; ScopedLayer < InDesc.layers; ++ScopedLayer)
            {
                std::memcpy(ScopedArray.layers[ScopedLayer].data(), InPixels.data() + ScopedLayerBytes * ScopedLayer,
                            static_cast<std::size_t>(ScopedLayerBytes));
            }
        }
        arrays.push_back(std::move(ScopedArray));
        Fby::Rhi::TextureArrayHandle ScopedHandle;
        ScopedHandle.id = static_cast<std::uint32_t>(arrays.size() - 1);
        return ScopedHandle;
    }

    void DestroyTextureArray(Fby::Rhi::TextureArrayHandle) override
    {
    }

    bool UpdateTextureArrayLayer(Fby::Rhi::TextureArrayHandle InHandle, std::uint32_t InLayer, std::uint32_t InMipLevel,
                                 std::span<const std::byte> InData) override
    {
        if (InHandle.id >= arrays.size() || InLayer >= arrays[InHandle.id].layers.size())
        {
            return false;
        }
        std::vector<std::byte>& ScopedDestination = arrays[InHandle.id].layers[InLayer];
        if (InMipLevel != 0 || InData.size() > ScopedDestination.size())
        {
            return false;
        }
        std::memcpy(ScopedDestination.data(), InData.data(), InData.size());
        ++uploadCount;
        return true;
    }

    const Fby::Rhi::DeviceInfo& Info() const noexcept override
    {
        return MInfo;
    }
    Fby::Rhi::FrameStats Stats() const noexcept override
    {
        return {};
    }
    Fby::Rhi::Backend GetBackend() const noexcept override
    {
        return Fby::Rhi::Backend::Null;
    }
    std::size_t TransientCapacity() const noexcept override
    {
        return 0;
    }

private:
    Fby::Rhi::DeviceInfo MInfo{};
};

// Tests

void TestDemuxAccept()
{
    const Bytes ScopedWebm = BuildWebm(WebmBuilder{});

    Fby::Video::WebmDemuxer ScopedDemuxer;
    std::string ScopedError;
    FBY_CHECK(ScopedDemuxer.Open(ScopedWebm, &ScopedError));
    FBY_CHECK(ScopedError.empty());
    FBY_CHECK(ScopedDemuxer.Track().trackNumber == 1);
    FBY_CHECK(ScopedDemuxer.Track().width == 4 && ScopedDemuxer.Track().height == 4);
    FBY_CHECK(ScopedDemuxer.Track().timestampScaleNs == 1000000ull);
    FBY_CHECK(!ScopedDemuxer.Track().alpha);
    FBY_CHECK(ScopedDemuxer.Track().bitDepth == 8);
    FBY_CHECK(std::string_view(ScopedDemuxer.Track().codecId) == "V_VP9");

    std::uint32_t ScopedFrames = 0;
    Fby::Video::WebmFrame ScopedFrame;
    while (ScopedDemuxer.NextFrame(ScopedFrame, &ScopedError) == Fby::Video::DemuxStatus::Frame)
    {
        FBY_CHECK(!ScopedFrame.payload.empty());
        FBY_CHECK(!ScopedFrame.hasAlpha);
        FBY_CHECK(ScopedFrame.timestampNs == static_cast<std::uint64_t>(ScopedFrames) * 20ull * 1000000ull);
        FBY_CHECK(static_cast<std::uint8_t>(ScopedFrame.payload[0]) == ScopedFrames + 1);
        ++ScopedFrames;
    }
    FBY_CHECK(ScopedFrames == 3);

    // An audio track in the same Tracks element is legal and ignored.
    WebmBuilder ScopedWithAudio;
    ScopedWithAudio.audioTrack = true;
    const Bytes ScopedWebmWithAudio = BuildWebm(ScopedWithAudio);
    Fby::Video::WebmDemuxer ScopedDemuxer2;
    ScopedError.clear();
    FBY_CHECK(ScopedDemuxer2.Open(ScopedWebmWithAudio, &ScopedError));
    FBY_CHECK(ScopedDemuxer2.Track().trackNumber == 1);
    FBY_CHECK(ScopedDemuxer2.NextFrame(ScopedFrame, &ScopedError) == Fby::Video::DemuxStatus::Frame);
}

void TestDemuxAlpha()
{
    WebmBuilder ScopedBuilder;
    ScopedBuilder.alpha = true;
    const Bytes ScopedWebm = BuildWebm(ScopedBuilder);

    Fby::Video::WebmDemuxer ScopedDemuxer;
    std::string ScopedError;
    FBY_CHECK(ScopedDemuxer.Open(ScopedWebm, &ScopedError));
    FBY_CHECK(ScopedDemuxer.Track().alpha);

    Fby::Video::WebmFrame ScopedFrame;
    std::uint32_t ScopedFrames = 0;
    while (ScopedDemuxer.NextFrame(ScopedFrame, &ScopedError) == Fby::Video::DemuxStatus::Frame)
    {
        FBY_CHECK(ScopedFrame.hasAlpha);
        FBY_CHECK(!ScopedFrame.alpha.empty());
        FBY_CHECK(static_cast<std::uint8_t>(ScopedFrame.alpha[1]) == 64 + ScopedFrames * 32);
        ++ScopedFrames;
    }
    FBY_CHECK(ScopedFrames == 3);
}

void TestRejections()
{
    std::string ScopedError;
    Fby::Video::WebmDemuxer ScopedDemuxer;

    // MP4 / ISO-BMFF: ftyp box at offset 4.
    const Bytes ScopedMp4 = BuildMp4();
    FBY_CHECK(!ScopedDemuxer.Open(ScopedMp4, &ScopedError));
    FBY_CHECK(ScopedError.find("MP4") != std::string::npos);
    FBY_CHECK(Fby::Video::SniffContainer(ScopedMp4) == Fby::Video::ContainerKind::Mp4);

    // Not EBML at all.
    ScopedError.clear();
    const Bytes ScopedJunk = BytesOf({0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01});
    FBY_CHECK(!ScopedDemuxer.Open(ScopedJunk, &ScopedError));
    FBY_CHECK(ScopedError.find("EBML") != std::string::npos);

    // Matroska is EBML but not WebM.
    {
        WebmBuilder ScopedBuilder;
        ScopedBuilder.docType = "matroska";
        ScopedError.clear();
        FBY_CHECK(!ScopedDemuxer.Open(BuildWebm(ScopedBuilder), &ScopedError));
        FBY_CHECK(ScopedError.find("not WebM") != std::string::npos);
        FBY_CHECK(ScopedError.find("matroska") != std::string::npos);
    }

    // Codec rejections, each with a precise message.
    const std::pair<const char*, const char*> ScopedCodecs[] = {
        {"V_VP8", "VP8"},
        {"V_AV1", "AV1"},
        {"V_MPEG4/ISO/AVC", "H.264"},
        {"V_MPEGH/ISO/HEVC", "H.265"},
        {"V_THEORA", "not supported"},
    };
    for (const auto& [ScopedCodecId, ScopedNeedle] : ScopedCodecs)
    {
        WebmBuilder ScopedBuilder;
        ScopedBuilder.codecId = ScopedCodecId;
        ScopedError.clear();
        FBY_CHECK(!ScopedDemuxer.Open(BuildWebm(ScopedBuilder), &ScopedError));
        FBY_CHECK(ScopedError.find(ScopedNeedle) != std::string::npos);
        FBY_CHECK(ScopedError.find("VP9") != std::string::npos);
    }

    // 10-bit VP9 in the codec configuration record.
    {
        WebmBuilder ScopedBuilder;
        // Bit depth 10
        ScopedBuilder.vpcCBitField = 0xA0;
        ScopedError.clear();
        FBY_CHECK(!ScopedDemuxer.Open(BuildWebm(ScopedBuilder), &ScopedError));
        FBY_CHECK(ScopedError.find("bit depth") != std::string::npos);
    }

    // A zero TimestampScale is invalid (it would collapse every timestamp).
    {
        WebmBuilder ScopedBuilder;
        ScopedBuilder.timestampScale = 0;
        ScopedError.clear();
        FBY_CHECK(!ScopedDemuxer.Open(BuildWebm(ScopedBuilder), &ScopedError));
        FBY_CHECK(ScopedError.find("TimestampScale") != std::string::npos);
    }
}

void TestPlayerRingOrderingAndAlpha()
{
    WebmBuilder ScopedBuilder;
    ScopedBuilder.alpha = true;
    const Bytes ScopedWebm = BuildWebm(ScopedBuilder);

    FakeVp9 ScopedFake;
    const auto ScopedBackend = ScopedFake.Table();
    FakeRhi ScopedRhi;
    Fby::Video::VideoPlayer ScopedPlayer;
    ScopedPlayer.SetRenderer(&ScopedRhi);
    std::string ScopedError;
    FBY_CHECK(ScopedPlayer.Open(ScopedWebm, &ScopedBackend, &ScopedError));
    FBY_CHECK(ScopedPlayer.TextureId() != Fby::Video::kInvalidVideoTexture);
    // Main + alpha decoder
    FBY_CHECK(ScopedFake.createdDecoders == 2);

    // Fill the ring: four pooled buffers, so four decodes fit before back-pressure.
    for (std::uint32_t ScopedFrame = 0; ScopedFrame < 3; ++ScopedFrame)
    {
        FBY_CHECK(ScopedPlayer.PumpFrame() == DecodeStatus::Decoded);
    }
    FBY_CHECK(ScopedPlayer.DecodedFrames() == 3);

    // Present frame 0 at its timestamp (0 ns), then frame 1 at 20 ms and frame 2
    ScopedPlayer.Advance(0.0f, 1.0f, false, false);
    FBY_CHECK(ScopedPlayer.PresentedFrames() == 1);
    FBY_CHECK(ScopedPlayer.LastPresentedTimestampNs() == 0);
    const std::uint32_t ScopedFirstLayer = ScopedPlayer.CurrentLayer();

    ScopedPlayer.Advance(0.021f, 1.0f, false, false);
    FBY_CHECK(ScopedPlayer.PresentedFrames() == 2);
    FBY_CHECK(ScopedPlayer.LastPresentedTimestampNs() == 20ull * 1000000ull);

    ScopedPlayer.Advance(0.021f, 1.0f, false, false);
    FBY_CHECK(ScopedPlayer.PresentedFrames() == 3);
    FBY_CHECK(ScopedPlayer.LastPresentedTimestampNs() == 40ull * 1000000ull);

    // Uploaded layers are in presentation order: the first uploaded layer is
    FBY_CHECK(ScopedRhi.uploadCount == 3);
    FBY_CHECK(ScopedRhi.arrays.size() == 1);
    FBY_CHECK(ScopedRhi.arrays[0].desc.layers == Fby::Video::kVideoFramePoolSize);
    FBY_CHECK(ScopedRhi.arrays[0].desc.format == Fby::Assets::TextureFormat::Rgba8);
    FBY_CHECK(ScopedRhi.arrays[0].desc.srgb == false);
    FBY_CHECK(ScopedRhi.arrays[0].desc.mipLevels == 1);
    FBY_CHECK(ScopedRhi.arrays[0].desc.size.width == 4 && ScopedRhi.arrays[0].desc.size.height == 4);

    // Alpha composition: the fake alpha decoder writes payload byte 1 into the
    const auto& ScopedFirstFrame = ScopedRhi.arrays[0].layers[ScopedFirstLayer];
    FBY_CHECK(static_cast<std::uint8_t>(ScopedFirstFrame[3]) == 64);
    const auto& ScopedThirdLayer = ScopedRhi.arrays[0].layers[ScopedPlayer.CurrentLayer()];
    FBY_CHECK(static_cast<std::uint8_t>(ScopedThirdLayer[3]) == 64 + 2 * 32);

    // Luma mapping: frame 0 decodes to Y=76, frame 2 to Y=196. U=V=128, so the
    const std::uint8_t ScopedFirstRed = static_cast<std::uint8_t>(ScopedFirstFrame[0]);
    const std::uint8_t ScopedThirdRed = static_cast<std::uint8_t>(ScopedThirdLayer[0]);
    FBY_CHECK(ScopedFirstRed >= 68 && ScopedFirstRed <= 72);
    FBY_CHECK(ScopedThirdRed > ScopedFirstRed);
    // U=V=128 -> grey
    FBY_CHECK(static_cast<std::uint8_t>(ScopedFirstFrame[1]) == ScopedFirstRed);
    FBY_CHECK(static_cast<std::uint8_t>(ScopedFirstFrame[2]) == ScopedFirstRed);
}

void TestSystemDropsAndLoops()
{
    WebmBuilder ScopedBuilder;
    ScopedBuilder.frameCount = 3;
    const Bytes ScopedWebm = BuildWebm(ScopedBuilder);

    FakeVp9 ScopedFake;
    const auto ScopedBackend = ScopedFake.Table();
    FakeRhi ScopedRhi;
    VideoSystem ScopedSystem;
    std::string ScopedError;
    FBY_CHECK(ScopedSystem.Initialize(&ScopedRhi, nullptr, &ScopedBackend, &ScopedError));
    ScopedSystem.SetThreadingEnabled(false);

    Fby::Ecs::Registry ScopedRegistry;
    Fby::JobSystem ScopedJobs(1);
    Fby::Ecs::SystemContext ScopedContext{ScopedRegistry, ScopedJobs};
    ScopedContext.renderer = &ScopedRhi;

    const entt::entity ScopedEntity = ScopedRegistry.create();
    VideoStreamComponent ScopedComponent;
    ScopedComponent.Loop = false;
    ScopedComponent.Speed = 1.0f;
    FBY_CHECK(ScopedSystem.OpenMemory(ScopedComponent, ScopedWebm, &ScopedError));
    FBY_CHECK(ScopedComponent.Player != Fby::Video::kInvalidVideoPlayer);
    ScopedRegistry.emplace<VideoStreamComponent>(ScopedEntity, ScopedComponent);

    // Prime the ring.
    FBY_CHECK(ScopedSystem.Player(ScopedComponent.Player)->PumpFrame() == DecodeStatus::Decoded);
    FBY_CHECK(ScopedSystem.Player(ScopedComponent.Player)->PumpFrame() == DecodeStatus::Decoded);
    FBY_CHECK(ScopedSystem.Player(ScopedComponent.Player)->PumpFrame() == DecodeStatus::Decoded);

    // Frame 0 is due at t=0.
    ScopedContext.dt = 0.0f;
    ScopedSystem.Update(ScopedContext);
    const VideoStreamComponent& ScopedTracked = ScopedRegistry.get<VideoStreamComponent>(ScopedEntity);
    FBY_CHECK((ScopedTracked.Flags & Fby::Video::kVideoFlagValid) != 0);
    FBY_CHECK((ScopedTracked.Flags & Fby::Video::kVideoFlagFinished) == 0);
    FBY_CHECK(ScopedSystem.Player(ScopedComponent.Player)->PresentedFrames() == 1);

    // Jump 100 ms: both remaining frames are late. The newest (frame 2) is
    ScopedContext.dt = 0.1f;
    ScopedSystem.Update(ScopedContext);
    FBY_CHECK(ScopedSystem.Player(ScopedComponent.Player)->PresentedFrames() == 2);
    FBY_CHECK(ScopedSystem.Player(ScopedComponent.Player)->DroppedFrames() == 1);
    FBY_CHECK(ScopedSystem.Player(ScopedComponent.Player)->LastPresentedTimestampNs() == 40ull * 1000000ull);

    // End of stream without loop: flag stays Finished.
    FBY_CHECK(ScopedSystem.Player(ScopedComponent.Player)->PumpFrame() == DecodeStatus::End);
    ScopedContext.dt = 0.0f;
    ScopedSystem.Update(ScopedContext);
    FBY_CHECK((ScopedRegistry.get<VideoStreamComponent>(ScopedEntity).Flags & Fby::Video::kVideoFlagFinished) != 0);

    // Loop: the finished stream rewinds, decodes frame 0 again and reports no
    ScopedRegistry.get<VideoStreamComponent>(ScopedEntity).Loop = true;
    // Publishes the rewind request
    ScopedSystem.Update(ScopedContext);
    FBY_CHECK(ScopedSystem.Player(ScopedComponent.Player)->PumpFrame() == DecodeStatus::Decoded);
    ScopedSystem.Update(ScopedContext);
    const VideoStreamComponent& ScopedLooped = ScopedRegistry.get<VideoStreamComponent>(ScopedEntity);
    FBY_CHECK((ScopedLooped.Flags & Fby::Video::kVideoFlagFinished) == 0);
    FBY_CHECK(ScopedSystem.Player(ScopedComponent.Player)->PresentedFrames() == 3);
    FBY_CHECK(ScopedSystem.Player(ScopedComponent.Player)->LastPresentedTimestampNs() == 0);

    FBY_CHECK(ScopedSystem.Close(*ScopedRegistry.try_get<VideoStreamComponent>(ScopedEntity)));
    ScopedRegistry.destroy(ScopedEntity);
    FBY_CHECK(ScopedSystem.OpenPlayers() == 0);
}

void TestSteadyStateAllocations()
{
    // Twelve frames means the pump hits both ring back-pressure and the mip of
    WebmBuilder ScopedBuilder;
    ScopedBuilder.frameCount = 12;
    ScopedBuilder.frameSpacingMs = 20;
    const Bytes ScopedWebm = BuildWebm(ScopedBuilder);

    FakeVp9 ScopedFake;
    const auto ScopedBackend = ScopedFake.Table();
    FakeRhi ScopedRhi;
    VideoSystem ScopedSystem;
    std::string ScopedError;
    FBY_CHECK(ScopedSystem.Initialize(&ScopedRhi, nullptr, &ScopedBackend, &ScopedError));
    ScopedSystem.SetThreadingEnabled(false);

    Fby::Ecs::Registry ScopedRegistry;
    Fby::JobSystem ScopedJobs(1);
    Fby::Ecs::SystemContext ScopedContext{ScopedRegistry, ScopedJobs};

    const entt::entity ScopedEntity = ScopedRegistry.create();
    VideoStreamComponent ScopedComponent;
    ScopedComponent.Loop = true;
    FBY_CHECK(ScopedSystem.OpenMemory(ScopedComponent, ScopedWebm, &ScopedError));
    ScopedRegistry.emplace<VideoStreamComponent>(ScopedEntity, ScopedComponent);
    VideoPlayer* ScopedPlayer = ScopedSystem.Player(ScopedComponent.Player);
    FBY_CHECK(ScopedPlayer != nullptr);

    // Warm up everything that allocates: registry storage, RHI array, demuxer
    for (std::uint32_t ScopedFrame = 0; ScopedFrame < 2; ++ScopedFrame)
    {
        FBY_CHECK(ScopedPlayer->PumpFrame() == DecodeStatus::Decoded);
        ScopedContext.dt = 0.020f;
        ScopedSystem.Update(ScopedContext);
    }

    const std::size_t ScopedBefore = GAllocations.load(std::memory_order_relaxed);
    for (std::uint32_t ScopedIteration = 0; ScopedIteration < 40; ++ScopedIteration)
    {
        for (std::uint32_t ScopedPump = 0; ScopedPump < 3; ++ScopedPump)
        {
            (void)ScopedPlayer->PumpFrame();
        }
        ScopedContext.dt = 0.020f;
        ScopedSystem.Update(ScopedContext);
    }
    const std::size_t ScopedAfter = GAllocations.load(std::memory_order_relaxed);
    if (ScopedAfter != ScopedBefore)
    {
        std::fprintf(stderr, "  steady-state allocations: %zu\n", ScopedAfter - ScopedBefore);
    }
    FBY_CHECK(ScopedAfter == ScopedBefore);
    FBY_CHECK(ScopedPlayer->PresentedFrames() > 40);
    FBY_CHECK(ScopedRhi.uploadCount > 40);
}
}

int main()
{
    TestDemuxAccept();
    TestDemuxAlpha();
    TestRejections();
    TestPlayerRingOrderingAndAlpha();
    TestSystemDropsAndLoops();
    TestSteadyStateAllocations();

    if (GFailures != 0)
    {
        std::fprintf(stderr, "video_test: %d failure(s)\n", GFailures);
        return 1;
    }
    std::printf("video_test: OK\n");
    return 0;
}
