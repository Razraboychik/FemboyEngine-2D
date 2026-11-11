// FemboyEngine2D - one VP9 video player (demux -> decode -> RGBA -> ring).
#include "Engine/Core/Time.hpp"
#include "Engine/Video/VideoPlayer.hpp"

#include "Engine/Core/Log.hpp"
#include "Engine/Core/ThreadAffinity.hpp"
#include "Engine/RHI/RHI.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <utility>

namespace Fby::Video
{
namespace
{

void SetError(std::string* InError, std::string InMessage)
{
    if (InError != nullptr)
    {
        *InError = std::move(InMessage);
    }
}

[[nodiscard]] std::uint8_t ClampChannel(std::int32_t InValue) noexcept
{
    if (InValue <= 0)
    {
        return 0;
    }
    if (InValue >= 255)
    {
        return 255;
    }
    return static_cast<std::uint8_t>(InValue);
}

// Fixed-point BT.709 coefficients, 16.16. The limited-range set is the classic
constexpr std::int32_t kLimitedLumaScale = 76284;
// 1.793 * 65536
constexpr std::int32_t kLimitedCrToR = 117507;
// 0.213 * 65536
constexpr std::int32_t kLimitedCbToG = 13959;
// 0.533 * 65536
constexpr std::int32_t kLimitedCrToG = 34931;
// 1.112 * 65536
constexpr std::int32_t kLimitedCbToB = 138412;
// 1.5748 * 65536
constexpr std::int32_t kFullCrToR = 103183;
// 0.1873 * 65536
constexpr std::int32_t kFullCbToG = 12276;
// 0.4681 * 65536
constexpr std::int32_t kFullCrToG = 30677;
// 1.8556 * 65536
constexpr std::int32_t kFullCbToB = 121598;
constexpr std::int32_t kRound = 1 << 15;

// Ring index arithmetic helper: kVideoRingCapacity is a power of two, so the
constexpr std::uint32_t kRingMask = kVideoRingCapacity - 1;
constexpr std::uint32_t kPoolInvalid = 0xFFFFFFFFu;
}

VideoPlayer::~VideoPlayer()
{
    Close();
}

// Open / Close
bool VideoPlayer::Open(std::span<const std::byte> InWebm, const Vp9Backend* InBackend, std::string* InError,
                       std::shared_ptr<const Fby::MappedFile> InOwner, const Av1Backend* InAv1Backend)
{
    Close();
    // Pin the mapping before the demuxer records any span into it. The decoder
    MSourcePin = std::move(InOwner);

    if (!MDemuxer.Open(InWebm, InError))
    {
        return false;
    }
    MTrack = MDemuxer.Track();
    MAv1SequenceSent = false;
    MAudioTrack = MDemuxer.AudioTrack();
    if (MAudioTrack.trackNumber != 0)
    {
        std::string ScopedAudioError;
        if (!MAudio.Open(InWebm, &ScopedAudioError))
        {
            // Video stays usable without audio; the component reports the error
            // through its normal flags and the host can inspect AudioTrack().
            MAudioTrack = {};
        }
    }
    if (MTrack.width == 0 || MTrack.height == 0 || MTrack.width > 16384 || MTrack.height > 16384)
    {
        SetError(InError, "video track dimensions are out of range");
        MDemuxer.Close();
        return false;
    }

    MAv1 = std::string_view(MTrack.codecId) == "V_AV1";
    const Vp9Backend* ScopedSelected =
        MAv1 ? (InAv1Backend != nullptr ? InAv1Backend : Libdav1dBackend()) : InBackend;
    if (ScopedSelected == nullptr || ScopedSelected->createDecoder == nullptr ||
        ScopedSelected->decodeFrame == nullptr)
    {
        SetError(InError, MAv1 ? "no AV1 decoder backend is available: supply an external dav1d plugin"
                               : "no VP9 decoder backend is available: install libvpx");
        Close();
        return false;
    }
    MBackend = *ScopedSelected;
    MWidth = MTrack.width;
    MHeight = MTrack.height;
    MMainDecoder = MBackend.createDecoder(MBackend.user, MWidth, MHeight);
    if (MMainDecoder == nullptr)
    {
        const char* ScopedDetail = MBackend.lastError != nullptr ? MBackend.lastError(MBackend.user) : nullptr;
        SetError(InError, ScopedDetail != nullptr ? ScopedDetail : "video backend could not create a decoder");
        Close();
        return false;
    }
    if (!MAv1 && MTrack.alpha)
    {
        MAlphaDecoder = MBackend.createDecoder(MBackend.user, MWidth, MHeight);
        if (MAlphaDecoder == nullptr)
        {
            SetError(InError, "VP9 backend could not create the alpha-plane decoder");
            Close();
            return false;
        }
    }

    // Planar storage for one decoded frame, main stream and alpha stream.
    const std::size_t ScopedLumaBytes = static_cast<std::size_t>(MWidth) * MHeight;
    const std::uint32_t ScopedChromaWidth = (MWidth + 1) / 2;
    const std::uint32_t ScopedChromaHeight = (MHeight + 1) / 2;
    const std::size_t ScopedChromaBytes = static_cast<std::size_t>(ScopedChromaWidth) * ScopedChromaHeight;
    MMainLuma.resize(ScopedLumaBytes);
    MMainChromaU.resize(ScopedChromaBytes);
    MMainChromaV.resize(ScopedChromaBytes);
    if (!MAv1 && MTrack.alpha)
    {
        MAlphaLuma.resize(ScopedLumaBytes);
    }
    else
    {
        MAlphaLuma.clear();
    }

    MMainView = YuvFrameView{};
    MMainView.luma = MMainLuma.data();
    MMainView.chromaU = MMainChromaU.data();
    MMainView.chromaV = MMainChromaV.data();
    MMainView.lumaStride = MWidth;
    MMainView.chromaStride = ScopedChromaWidth;
    MMainView.width = MWidth;
    MMainView.height = MHeight;

    MAlphaView = YuvFrameView{};
    MAlphaView.luma = MAlphaLuma.empty() ? nullptr : MAlphaLuma.data();
    MAlphaView.lumaStride = MWidth;
    MAlphaView.width = MWidth;
    MAlphaView.height = MHeight;

    // The RGBA pool is also the texture-array layer set: one upload target per
    const std::size_t ScopedRgbaBytes = ScopedLumaBytes * 4;
    for (FrameBuffer& ScopedBuffer : MBuffers)
    {
        ScopedBuffer.rgba.resize(ScopedRgbaBytes);
        ScopedBuffer.busy.store(false, std::memory_order_relaxed);
    }

    BuildColorTables(MTrack.fullRange);

    MRingHead.store(0, std::memory_order_relaxed);
    MRingTail.store(0, std::memory_order_relaxed);
    MEpoch.store(0, std::memory_order_relaxed);
    MRewindRequested.store(0, std::memory_order_relaxed);
    MFinished.store(false, std::memory_order_relaxed);
    MFailed.store(false, std::memory_order_relaxed);
    MDecoded.store(0, std::memory_order_relaxed);
    MPresented.store(0, std::memory_order_relaxed);
    MDropped.store(0, std::memory_order_relaxed);
    MLastPresentedTimestampNs.store(0, std::memory_order_relaxed);
    MClockNs = 0;
    MCurrentLayer = 0;
    MErrorText.clear();

    if (MGpu != nullptr && !CreateTexture(InError))
    {
        Close();
        return false;
    }

    MSeekTargetNs.store(-1, std::memory_order_relaxed);
    MOpen = true;
    LogInfo("video", "opened WebM {} {}x{} ({} alpha), {} ns/tick{}", MTrack.codecId, MWidth, MHeight,
            MTrack.alpha ? "with" : "without", MTrack.timestampScaleNs, MTrack.hasCodecPrivate ? ", vpcC present" : "");
    return true;
}

void VideoPlayer::Close()
{
    StopDecoderThread();
    DestroyTexture();
    if (MBackend.destroyDecoder != nullptr)
    {
        if (MMainDecoder != nullptr)
        {
            MBackend.destroyDecoder(MBackend.user, MMainDecoder);
        }
        if (MAlphaDecoder != nullptr)
        {
            MBackend.destroyDecoder(MBackend.user, MAlphaDecoder);
        }
    }
    MMainDecoder = nullptr;
    MAlphaDecoder = nullptr;
    MDemuxer.Close();
    // Release the mmap only after the demuxer and the decoder thread are gone.
    MSourcePin.reset();
    MBackend = {};
    MTrack = {};
    MAudioTrack = {};
    MAv1 = false;
    MAv1SequenceSent = false;
    MAv1Scratch.clear();
    MAudio.Close();

    MMainLuma.clear();
    MMainChromaU.clear();
    MMainChromaV.clear();
    MAlphaLuma.clear();
    MMainView = {};
    MAlphaView = {};
    for (FrameBuffer& ScopedBuffer : MBuffers)
    {
        ScopedBuffer.rgba.clear();
        ScopedBuffer.busy.store(false, std::memory_order_relaxed);
    }

    MRingHead.store(0, std::memory_order_relaxed);
    MRingTail.store(0, std::memory_order_relaxed);
    MEpoch.store(0, std::memory_order_relaxed);
    MRewindRequested.store(0, std::memory_order_relaxed);
    MFinished.store(false, std::memory_order_relaxed);
    MFailed.store(false, std::memory_order_relaxed);
    MClockNs = 0;
    MCurrentLayer = 0;
    MWidth = 0;
    MHeight = 0;
    MOpen = false;
}

bool VideoPlayer::CreateTexture(std::string* InError)
{
    if (MGpu == nullptr)
    {
        return true;
    }
    Rhi::TextureArrayDesc ScopedDesc;
    ScopedDesc.size = {MWidth, MHeight};
    ScopedDesc.layers = kVideoFramePoolSize;
    ScopedDesc.mipLevels = 1;
    // Video frames carry display-referred colour already; the sprite pipeline
    ScopedDesc.srgb = false;
    ScopedDesc.format = Assets::TextureFormat::Rgba8;

    // Seed the array once so every layer is defined before the first upload.
    std::vector<std::byte> ScopedInitial(static_cast<std::size_t>(kVideoFramePoolSize) * MWidth * MHeight * 4,
                                         std::byte{0});
    MTexture = MGpu->CreateTextureArray(ScopedDesc, ScopedInitial);
    if (!MTexture.IsValid())
    {
        SetError(InError, "RHI refused the video texture array");
        return false;
    }
    return true;
}

void VideoPlayer::DestroyTexture() noexcept
{
    if (MGpu != nullptr && MTexture.IsValid())
    {
        MGpu->DestroyTextureArray(MTexture);
    }
    MTexture = {};
}

// Colour conversion
void VideoPlayer::BuildColorTables(bool InFullRange) noexcept
{
    const std::int32_t ScopedLumaScale = InFullRange ? 65536 : kLimitedLumaScale;
    const std::int32_t ScopedCrToR = InFullRange ? kFullCrToR : kLimitedCrToR;
    const std::int32_t ScopedCbToG = InFullRange ? kFullCbToG : kLimitedCbToG;
    const std::int32_t ScopedCrToG = InFullRange ? kFullCrToG : kLimitedCrToG;
    const std::int32_t ScopedCbToB = InFullRange ? kFullCbToB : kLimitedCbToB;
    for (std::int32_t ScopedValue = 0; ScopedValue < 256; ++ScopedValue)
    {
        const std::int32_t ScopedLuma = InFullRange ? ScopedValue : ScopedValue - 16;
        const std::int32_t ScopedChroma = ScopedValue - 128;
        MLumaTerm[static_cast<std::size_t>(ScopedValue)] = ScopedLumaScale * ScopedLuma + kRound;
        MCrToR[static_cast<std::size_t>(ScopedValue)] = ScopedCrToR * ScopedChroma;
        MCbToG[static_cast<std::size_t>(ScopedValue)] = -ScopedCbToG * ScopedChroma;
        MCrToG[static_cast<std::size_t>(ScopedValue)] = -ScopedCrToG * ScopedChroma;
        MCbToB[static_cast<std::size_t>(ScopedValue)] = ScopedCbToB * ScopedChroma;
    }
}

void VideoPlayer::ConvertRgba(std::uint32_t InBufferIndex) noexcept
{
    if (InBufferIndex >= kVideoFramePoolSize || MMainView.luma == nullptr)
    {
        return;
    }
    std::byte* ScopedOutput = MBuffers[InBufferIndex].rgba.data();
    const std::uint8_t* ScopedAlpha = MTrack.alpha && MAlphaView.luma != nullptr ? MAlphaView.luma : nullptr;
    const std::uint32_t ScopedChromaWidth = (MWidth + 1) / 2;
    (void)ScopedChromaWidth;

    for (std::uint32_t ScopedY = 0; ScopedY < MHeight; ++ScopedY)
    {
        const std::uint8_t* ScopedLumaRow = MMainView.luma + static_cast<std::size_t>(ScopedY) * MMainView.lumaStride;
        const std::uint8_t* ScopedChromaURow =
            MMainView.chromaU + static_cast<std::size_t>(ScopedY / 2) * MMainView.chromaStride;
        const std::uint8_t* ScopedChromaVRow =
            MMainView.chromaV + static_cast<std::size_t>(ScopedY / 2) * MMainView.chromaStride;
        const std::uint8_t* ScopedAlphaRow =
            ScopedAlpha != nullptr ? ScopedAlpha + static_cast<std::size_t>(ScopedY) * MMainView.lumaStride : nullptr;
        std::byte* ScopedOutRow = ScopedOutput + static_cast<std::size_t>(ScopedY) * MWidth * 4;
        for (std::uint32_t ScopedX = 0; ScopedX < MWidth; ++ScopedX)
        {
            const std::int32_t ScopedLuma = MLumaTerm[ScopedLumaRow[ScopedX]];
            const std::uint8_t ScopedU = ScopedChromaURow[ScopedX / 2];
            const std::uint8_t ScopedV = ScopedChromaVRow[ScopedX / 2];
            const std::int32_t ScopedR = (ScopedLuma + MCrToR[ScopedV]) >> 16;
            const std::int32_t ScopedG = (ScopedLuma + MCbToG[ScopedU] + MCrToG[ScopedV]) >> 16;
            const std::int32_t ScopedB = (ScopedLuma + MCbToB[ScopedU]) >> 16;
            // WebM alpha: the alpha plane's luma is the alpha value directly
            const std::uint8_t ScopedA = ScopedAlphaRow != nullptr ? ScopedAlphaRow[ScopedX] : 255u;
            ScopedOutRow[ScopedX * 4 + 0] = static_cast<std::byte>(ClampChannel(ScopedR));
            ScopedOutRow[ScopedX * 4 + 1] = static_cast<std::byte>(ClampChannel(ScopedG));
            ScopedOutRow[ScopedX * 4 + 2] = static_cast<std::byte>(ClampChannel(ScopedB));
            ScopedOutRow[ScopedX * 4 + 3] = static_cast<std::byte>(ScopedA);
        }
    }
}

// Pool + ring
std::uint32_t VideoPlayer::AcquireBuffer() noexcept
{
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < kVideoFramePoolSize; ++ScopedIndex)
    {
        bool ScopedExpected = false;
        if (MBuffers[ScopedIndex].busy.compare_exchange_strong(ScopedExpected, true, std::memory_order_acq_rel,
                                                               std::memory_order_relaxed))
        {
            return ScopedIndex;
        }
    }
    return kPoolInvalid;
}

void VideoPlayer::ReleaseBuffer(std::uint32_t InIndex) noexcept
{
    if (InIndex < kVideoFramePoolSize)
    {
        MBuffers[InIndex].busy.store(false, std::memory_order_release);
    }
}

bool VideoPlayer::PushSlot(const FrameSlot& InSlot) noexcept
{
    const std::uint32_t ScopedHead = MRingHead.load(std::memory_order_relaxed);
    const std::uint32_t ScopedNext = (ScopedHead + 1) & kRingMask;
    if (ScopedNext == MRingTail.load(std::memory_order_acquire))
    {
        return false;
    }
    MRing[ScopedHead] = InSlot;
    MRingHead.store(ScopedNext, std::memory_order_release);
    return true;
}

bool VideoPlayer::PopSlot(FrameSlot& InSlot) noexcept
{
    const std::uint32_t ScopedTail = MRingTail.load(std::memory_order_relaxed);
    if (ScopedTail == MRingHead.load(std::memory_order_acquire))
    {
        return false;
    }
    InSlot = MRing[ScopedTail];
    MRingTail.store((ScopedTail + 1) & kRingMask, std::memory_order_release);
    return true;
}

bool VideoPlayer::PeekSlot(FrameSlot& InSlot) const noexcept
{
    const std::uint32_t ScopedTail = MRingTail.load(std::memory_order_relaxed);
    if (ScopedTail == MRingHead.load(std::memory_order_acquire))
    {
        return false;
    }
    InSlot = MRing[ScopedTail];
    return true;
}

bool VideoPlayer::RingEmpty() const noexcept
{
    return MRingTail.load(std::memory_order_acquire) == MRingHead.load(std::memory_order_acquire);
}

std::uint32_t VideoPlayer::QueuedFrames() const noexcept
{
    const std::uint32_t ScopedHead = MRingHead.load(std::memory_order_acquire);
    const std::uint32_t ScopedTail = MRingTail.load(std::memory_order_acquire);
    return (ScopedHead - ScopedTail) & kRingMask;
}

// Decode
DecodeStatus VideoPlayer::PumpFrame()
{
    if (!MOpen)
    {
        return DecodeStatus::Error;
    }

    const std::int64_t ScopedSeekTarget = MSeekTargetNs.exchange(-1, std::memory_order_acq_rel);
    if (ScopedSeekTarget != -1)
    {
        if (ScopedSeekTarget < 0)
        {
            MDemuxer.Rewind();
        }
        else
        {
            if (!MDemuxer.SeekToNs(static_cast<std::uint64_t>(ScopedSeekTarget)))
            {
                PublishError("video seek is not supported by this container");
                MFinished.store(true, std::memory_order_release);
                return DecodeStatus::Error;
            }
        }
        MFinished.store(false, std::memory_order_release);
    }

    // A looped stream that reached its end restarts here, on the pump thread.
    if (MFinished.load(std::memory_order_acquire))
    {
        if (MRewindRequested.exchange(0, std::memory_order_acq_rel) == 0)
        {
            return DecodeStatus::End;
        }
        MDemuxer.Rewind();
        MFinished.store(false, std::memory_order_release);
        MEpoch.fetch_add(1, std::memory_order_acq_rel);
    }

    // Back-pressure: never spin inside the pump. The caller (thread loop or
    const std::uint32_t ScopedHead = MRingHead.load(std::memory_order_relaxed);
    if (((ScopedHead + 1) & kRingMask) == MRingTail.load(std::memory_order_acquire))
    {
        return DecodeStatus::RingFull;
    }
    const std::uint32_t ScopedBufferIndex = AcquireBuffer();
    if (ScopedBufferIndex == kPoolInvalid)
    {
        return DecodeStatus::PoolExhausted;
    }

    WebmFrame ScopedFrame;
    const DemuxStatus ScopedDemuxStatus = MDemuxer.NextFrame(ScopedFrame, &MDemuxError);
    if (ScopedDemuxStatus != DemuxStatus::Frame)
    {
        ReleaseBuffer(ScopedBufferIndex);
        if (ScopedDemuxStatus == DemuxStatus::End)
        {
            MFinished.store(true, std::memory_order_release);
            return DecodeStatus::End;
        }
        PublishError(MDemuxError.empty() ? "WebM demux failed" : MDemuxError);
        return DecodeStatus::Error;
    }

    MDecoded.fetch_add(1, std::memory_order_relaxed);
    std::span<const std::byte> ScopedPayload = ScopedFrame.payload;
    if (MAv1 && !MAv1SequenceSent && MTrack.codecPrivate.size() > 4)
    {
        // AV1CodecConfigurationRecord: 4 header bytes, then configOBUs.
        MAv1Scratch.assign(MTrack.codecPrivate.begin() + 4, MTrack.codecPrivate.end());
        MAv1Scratch.insert(MAv1Scratch.end(), ScopedFrame.payload.begin(), ScopedFrame.payload.end());
        ScopedPayload = std::span<const std::byte>(MAv1Scratch.data(), MAv1Scratch.size());
    }
    if (!MBackend.decodeFrame(MBackend.user, MMainDecoder, ScopedPayload, MMainView))
    {
        const char* ScopedDetail = MBackend.lastError != nullptr ? MBackend.lastError(MBackend.user) : nullptr;
        PublishError(ScopedDetail != nullptr ? ScopedDetail : "video decode failed");
        ReleaseBuffer(ScopedBufferIndex);
        return DecodeStatus::Error;
    }
    // Only a successfully decoded frame proves that the sequence header is in the
    // decoder; a failed attempt must prepend the AV1 configuration again.
    MAv1SequenceSent = true;
    if (ScopedFrame.hasAlpha && MAlphaDecoder != nullptr)
    {
        if (!MBackend.decodeFrame(MBackend.user, MAlphaDecoder, ScopedFrame.alpha, MAlphaView))
        {
            const char* ScopedDetail = MBackend.lastError != nullptr ? MBackend.lastError(MBackend.user) : nullptr;
            PublishError(ScopedDetail != nullptr ? ScopedDetail : "VP9 alpha-plane decode failed");
            ReleaseBuffer(ScopedBufferIndex);
            return DecodeStatus::Error;
        }
    }

    // Invisible frames exist to update the decoder's reference state; they are
    if (ScopedFrame.invisible)
    {
        ReleaseBuffer(ScopedBufferIndex);
        return DecodeStatus::Decoded;
    }

    ConvertRgba(ScopedBufferIndex);

    FrameSlot ScopedSlot;
    ScopedSlot.timestampNs = ScopedFrame.timestampNs;
    ScopedSlot.durationNs = ScopedFrame.durationNs;
    ScopedSlot.poolIndex = ScopedBufferIndex;
    ScopedSlot.epoch = MEpoch.load(std::memory_order_acquire);
    ScopedSlot.keyframe = ScopedFrame.keyframe;
    ScopedSlot.hasAlpha = ScopedFrame.hasAlpha;
    if (!PushSlot(ScopedSlot))
    {
        ReleaseBuffer(ScopedBufferIndex);
        return DecodeStatus::RingFull;
    }
    return DecodeStatus::Decoded;
}

void VideoPlayer::PublishError(std::string InMessage)
{
    MFailed.store(true, std::memory_order_release);
    MErrorText = std::move(InMessage);
}

void VideoPlayer::ThreadMain(std::stop_token InStopToken)
{
    while (!InStopToken.stop_requested())
    {
        switch (PumpFrame())
        {
            case DecodeStatus::Decoded:
                // Keep decoding while the pool has room: that is what keeps the
                continue;
            case DecodeStatus::RingFull:
            case DecodeStatus::PoolExhausted:
                // Back-pressure: the render thread will release a buffer. A short
                Core::SleepMilliseconds(1.0);
                continue;
            case DecodeStatus::End:
                // A loop request arrives through rewindRequested_; Poll it without
                Core::SleepMilliseconds(2.0);
                continue;
            case DecodeStatus::Error:
                // failed_ is sticky; keep the thread alive but idle so the owner
                Core::SleepMilliseconds(10.0);
                continue;
        }
    }
}

void VideoPlayer::StartDecoderThread()
{
    if (!MOpen || MThreadRunning)
    {
        return;
    }
    MDecoderThread = std::jthread(
        [this](std::stop_token InStopToken)
        {
            BindThreadRole(ThreadRole::Video);
            ThreadMain(InStopToken);
        });
    MThreadRunning = true;
}

void VideoPlayer::StopDecoderThread()
{
    if (MDecoderThread.joinable())
    {
        MDecoderThread.request_stop();
        MDecoderThread.join();
    }
    MThreadRunning = false;
}

// Presentation
void VideoPlayer::SeekToStart() noexcept
{
    if (!MOpen)
    {
        return;
    }
    MSeekTargetNs.store(-2, std::memory_order_release);
    MEpoch.fetch_add(1, std::memory_order_acq_rel);
    MClockNs = 0;
    MAudio.Rewind();
}

bool VideoPlayer::SeekToNs(std::uint64_t InTimestampNs) noexcept
{
    if (!MOpen || InTimestampNs > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) ||
        MDemuxer.ClusterCount() == 0)
    {
        return false;
    }
    MSeekTargetNs.store(static_cast<std::int64_t>(InTimestampNs), std::memory_order_release);
    MEpoch.fetch_add(1, std::memory_order_acq_rel);
    MClockNs = static_cast<std::int64_t>(InTimestampNs);
    if (!MAudio.SeekToNs(InTimestampNs))
    {
        MAudio.Rewind();
    }
    return true;
}

void VideoPlayer::Advance(float InDtSeconds, float InSpeed, bool InPaused, bool InLoop)
{
    if (!MOpen)
    {
        return;
    }
    if (MAudio.IsOpen())
    {
        MAudio.SetPlaybackRate(InPaused ? 1.0f : (InSpeed > 0.0f ? InSpeed : 1.0f));
    }
    if (MAudio.IsOpen() && !InPaused)
    {
        for (std::uint32_t ScopedPump = 0; ScopedPump < 2; ++ScopedPump)
        {
            const AudioPumpStatus ScopedStatus = MAudio.PumpOne();
            if (ScopedStatus == AudioPumpStatus::End || ScopedStatus == AudioPumpStatus::Error ||
                ScopedStatus == AudioPumpStatus::Buffered)
            {
                break;
            }
        }
    }
    const std::uint64_t ScopedAudioClock = MAudio.IsOpen() ? MAudio.AudioClockNs() : 0;
    if (MAudioMaster && ScopedAudioClock > 0)
    {
        MClockNs = static_cast<std::int64_t>(ScopedAudioClock);
    }
    else if (!InPaused && InSpeed > 0.0f && !MFailed.load(std::memory_order_relaxed))
    {
        const double ScopedDelta = static_cast<double>(InDtSeconds) * static_cast<double>(InSpeed) * 1.0e9;
        if (ScopedDelta > 0.0)
        {
            const auto ScopedDeltaNs = static_cast<std::int64_t>(ScopedDelta);
            if (MClockNs > std::numeric_limits<std::int64_t>::max() - ScopedDeltaNs)
            {
                MClockNs = std::numeric_limits<std::int64_t>::max();
            }
            else
            {
                MClockNs += ScopedDeltaNs;
            }
        }
    }

    const std::uint64_t ScopedClock = MClockNs < 0 ? 0ull : static_cast<std::uint64_t>(MClockNs);
    FrameSlot ScopedNewest{};
    bool ScopedHaveNewest = false;
    for (;;)
    {
        FrameSlot ScopedSlot;
        if (!PeekSlot(ScopedSlot))
        {
            break;
        }
        if (ScopedSlot.epoch != MEpoch.load(std::memory_order_acquire))
        {
            // A leftover frame from before a loop restart: retire it without
            (void)PopSlot(ScopedSlot);
            ReleaseBuffer(ScopedSlot.poolIndex);
            MDropped.fetch_add(1, std::memory_order_relaxed);
            continue;
        }
        if (ScopedSlot.timestampNs > ScopedClock)
        {
            // Not due yet; stays at the head of the ring
            break;
        }
        (void)PopSlot(ScopedSlot);
        if (ScopedHaveNewest)
        {
            // A newer frame is already due, so this one is late by definition.
            ReleaseBuffer(ScopedNewest.poolIndex);
            MDropped.fetch_add(1, std::memory_order_relaxed);
        }
        ScopedNewest = ScopedSlot;
        ScopedHaveNewest = true;
    }

    if (ScopedHaveNewest)
    {
        if (MGpu != nullptr && MTexture.IsValid())
        {
            const std::span<const std::byte> ScopedLayerData(MBuffers[ScopedNewest.poolIndex].rgba.data(),
                                                             MBuffers[ScopedNewest.poolIndex].rgba.size());
            (void)MGpu->UpdateTextureArrayLayer(MTexture, ScopedNewest.poolIndex, 0, ScopedLayerData);
        }
        MCurrentLayer = ScopedNewest.poolIndex;
        MLastPresentedTimestampNs.store(ScopedNewest.timestampNs, std::memory_order_relaxed);
        MPresented.fetch_add(1, std::memory_order_relaxed);
        ReleaseBuffer(ScopedNewest.poolIndex);
    }

    if (MFinished.load(std::memory_order_acquire) && RingEmpty())
    {
        if (InLoop && !MFailed.load(std::memory_order_relaxed))
        {
            // Publish the rewind request once; the pump consumes it and bumps
            if (MRewindRequested.exchange(1, std::memory_order_acq_rel) == 0)
            {
                MEpoch.fetch_add(1, std::memory_order_acq_rel);
                MClockNs = 0;
                MAudio.Rewind();
            }
        }
    }
}

// Observations
double VideoPlayer::ClockSeconds() const noexcept
{
    return static_cast<double>(MClockNs) / 1.0e9;
}

bool VideoPlayer::AtEnd() const noexcept
{
    return MOpen && MFinished.load(std::memory_order_acquire) && RingEmpty();
}

std::uint32_t VideoPlayer::StateFlags() const noexcept
{
    std::uint32_t ScopedFlags = 0;
    if (MOpen)
    {
        ScopedFlags |= kVideoFlagValid;
    }
    if (MTrack.alpha)
    {
        ScopedFlags |= kVideoFlagAlpha;
    }
    if (MFailed.load(std::memory_order_relaxed))
    {
        ScopedFlags |= kVideoFlagError;
    }
    if (AtEnd())
    {
        ScopedFlags |= kVideoFlagFinished;
    }
    return ScopedFlags;
}
}
