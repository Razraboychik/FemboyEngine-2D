// FemboyEngine2D - one VP9 video player.
#pragma once

#include "Engine/Video/Vp9Backend.hpp"
#include "Engine/Video/VideoTypes.hpp"
#include "Engine/Video/Av1Backend.hpp"
#include "Engine/Video/VideoAudioStream.hpp"
#include "Engine/Video/WebmDemuxer.hpp"

#include "Engine/RHI/RHI.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace Fby
{
class MappedFile;
}

namespace Fby::Rhi
{
class RHI;
}

namespace Fby::Video
{

class VideoPlayer
{
public:
    VideoPlayer() = default;
    ~VideoPlayer();

    VideoPlayer(const VideoPlayer&) = delete;
    VideoPlayer& operator=(const VideoPlayer&) = delete;
    VideoPlayer(VideoPlayer&&) = delete;
    VideoPlayer& operator=(VideoPlayer&&) = delete;

    // Binds the device that streaming uploads go through.
    void SetRenderer(Rhi::RHI* InGpu) noexcept
    {
        MGpu = InGpu;
    }

    // Opens a WebM/VP9 stream over a mapping that must outlive the player.
    [[nodiscard]] bool Open(std::span<const std::byte> InWebm, const Vp9Backend* InBackend,
                            std::string* InError = nullptr, std::shared_ptr<const Fby::MappedFile> InOwner = nullptr,
                            const Av1Backend* InAv1Backend = nullptr);

    // Stops the decoder thread (if any) and releases every resource.
    void Close();

    [[nodiscard]] bool IsOpen() const noexcept
    {
        return MOpen;
    }

    // Starts the background decoder. Safe to call more than once; a second call
    void StartDecoderThread();
    void StopDecoderThread();

    // One decode step. Produces at most one frame; never blocks.
    [[nodiscard]] DecodeStatus PumpFrame();

    // Render-side per-frame update: advance the clock, drop late frames, upload
    void Advance(float InDtSeconds, float InSpeed, bool InPaused, bool InLoop);
    // Restarts video and audio at the first cluster. Frames already queued are
    // retired by the epoch check in Advance().
    void SeekToStart() noexcept;
    // Seeks video to the last cluster at or before InTimestampNs. Audio restarts
    // at that cluster; the caller should drop decoded PCM until the clocks align.
    [[nodiscard]] bool SeekToNs(std::uint64_t InTimestampNs) noexcept;
    // Opt-in: use the decoded audio clock as the video master clock. The host
    // must call ReadAudio() each frame; when the audio clock has not advanced
    // yet, the wall-clock delta is used as a fallback.
    void SetAudioMasterClock(bool InEnabled) noexcept
    {
        MAudioMaster = InEnabled;
    }
    [[nodiscard]] bool AudioMasterClock() const noexcept
    {
        return MAudioMaster;
    }

    [[nodiscard]] std::uint32_t TextureId() const noexcept
    {
        return MTexture.id;
    }
    [[nodiscard]] std::uint32_t CurrentLayer() const noexcept
    {
        return MCurrentLayer;
    }
    [[nodiscard]] const WebmTrackInfo& Track() const noexcept
    {
        return MTrack;
    }
    [[nodiscard]] bool HasAudio() const noexcept
    {
        return MAudioTrack.trackNumber != 0;
    }
    [[nodiscard]] const WebmAudioTrackInfo& AudioTrack() const noexcept
    {
        return MAudioTrack;
    }
    // Decoded PCM is pumped from Advance() on the owner thread; the host reads
    // it with ReadAudio() after Advance().
    [[nodiscard]] std::uint32_t ReadAudio(std::span<float> OutInterleaved) noexcept
    {
        return MAudio.Read(OutInterleaved);
    }
    [[nodiscard]] std::uint64_t AudioClockNs() const noexcept
    {
        return MAudio.AudioClockNs();
    }
    [[nodiscard]] std::uint32_t QueuedAudioFrames() const noexcept
    {
        return MAudio.AvailableFrames();
    }
    [[nodiscard]] std::uint64_t PresentedFrames() const noexcept
    {
        return MPresented.load(std::memory_order_relaxed);
    }
    [[nodiscard]] std::uint64_t DroppedFrames() const noexcept
    {
        return MDropped.load(std::memory_order_relaxed);
    }
    [[nodiscard]] std::uint64_t DecodedFrames() const noexcept
    {
        return MDecoded.load(std::memory_order_relaxed);
    }
    [[nodiscard]] std::uint64_t LastPresentedTimestampNs() const noexcept
    {
        return MLastPresentedTimestampNs.load(std::memory_order_relaxed);
    }
    [[nodiscard]] double ClockSeconds() const noexcept;
    // True once the demuxer reached the end AND the ring is drained.
    [[nodiscard]] bool AtEnd() const noexcept;
    // Sticky: a structural error or a decoder failure happened.
    [[nodiscard]] bool HasError() const noexcept
    {
        return MFailed.load(std::memory_order_relaxed);
    }
    [[nodiscard]] std::string_view ErrorText() const noexcept
    {
        return MErrorText;
    }
    // kVideoFlag* mask for the owning component.
    [[nodiscard]] std::uint32_t StateFlags() const noexcept;
    // Frames queued in the ring (observability / tests).
    [[nodiscard]] std::uint32_t QueuedFrames() const noexcept;

private:
    struct FrameBuffer
    {
        // Width*height*4, allocated in Open()
        std::vector<std::byte> rgba;
        std::atomic<bool> busy{false};
    };

    struct FrameSlot
    {
        std::uint64_t timestampNs = 0;
        std::uint64_t durationNs = 0;
        std::uint32_t poolIndex = 0;
        std::uint32_t epoch = 0;
        bool keyframe = false;
        bool hasAlpha = false;
    };

    [[nodiscard]] std::uint32_t AcquireBuffer() noexcept;
    void ReleaseBuffer(std::uint32_t InIndex) noexcept;

    [[nodiscard]] bool PushSlot(const FrameSlot& InSlot) noexcept;
    [[nodiscard]] bool PopSlot(FrameSlot& InSlot) noexcept;
    [[nodiscard]] bool PeekSlot(FrameSlot& InSlot) const noexcept;
    [[nodiscard]] bool RingEmpty() const noexcept;

    [[nodiscard]] bool CreateTexture(std::string* InError);
    void DestroyTexture() noexcept;
    // Converts the YUV planes of the just-decoded frame into the pool buffer,
    void ConvertRgba(std::uint32_t InBufferIndex) noexcept;
    void BuildColorTables(bool InFullRange) noexcept;
    void ThreadMain(std::stop_token InStopToken);
    void PublishError(std::string InMessage);

    Rhi::RHI* MGpu = nullptr;

    // Owner of the bytes MDemuxer points into. Null for a caller-owned span
    std::shared_ptr<const Fby::MappedFile> MSourcePin{};

    WebmDemuxer MDemuxer{};
    WebmTrackInfo MTrack{};
    WebmAudioTrackInfo MAudioTrack{};
    VideoAudioStream MAudio{};
    std::atomic<std::int64_t> MSeekTargetNs{-1};
    bool MAudioMaster = false;
    Vp9Backend MBackend{};
    bool MAv1 = false;
    bool MAv1SequenceSent = false;
    std::vector<std::byte> MAv1Scratch;
    void* MMainDecoder = nullptr;
    void* MAlphaDecoder = nullptr;

    // Decoder-owned planar storage, sized once in Open().
    std::vector<std::uint8_t> MMainLuma;
    std::vector<std::uint8_t> MMainChromaU;
    std::vector<std::uint8_t> MMainChromaV;
    std::vector<std::uint8_t> MAlphaLuma;
    YuvFrameView MMainView{};
    YuvFrameView MAlphaView{};

    // BT.709 (limited range) lookup tables, filled in Open().
    std::array<std::int32_t, 256> MLumaTerm{};
    std::array<std::int32_t, 256> MCrToR{};
    std::array<std::int32_t, 256> MCbToG{};
    std::array<std::int32_t, 256> MCrToG{};
    std::array<std::int32_t, 256> MCbToB{};

    std::array<FrameBuffer, kVideoFramePoolSize> MBuffers{};
    std::array<FrameSlot, kVideoRingCapacity> MRing{};
    // Producer (decoder)
    std::atomic<std::uint32_t> MRingHead{0};
    // Consumer (render)
    std::atomic<std::uint32_t> MRingTail{0};

    Rhi::TextureArrayHandle MTexture{};
    std::atomic<std::uint32_t> MEpoch{0};
    std::atomic<std::uint32_t> MRewindRequested{0};
    std::atomic<bool> MFinished{false};
    std::atomic<bool> MFailed{false};

    std::atomic<std::uint64_t> MDecoded{0};
    std::atomic<std::uint64_t> MPresented{0};
    std::atomic<std::uint64_t> MDropped{0};
    std::atomic<std::uint64_t> MLastPresentedTimestampNs{0};

    std::int64_t MClockNs = 0;
    std::uint32_t MCurrentLayer = 0;
    std::uint32_t MWidth = 0;
    std::uint32_t MHeight = 0;
    bool MOpen = false;

    std::jthread MDecoderThread{};
    bool MThreadRunning = false;
    std::string MErrorText{};
    // Demuxer error scratch. A local std::string in PumpFrame allocates a debug
    // container proxy on every call, which breaks the no-allocation frame rule.
    std::string MDemuxError{};
};
}
