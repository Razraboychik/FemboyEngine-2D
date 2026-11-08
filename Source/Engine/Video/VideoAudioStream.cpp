// FemboyEngine2D - decoded video audio stream and playback clock.
#include "Engine/Video/VideoAudioStream.hpp"

#include <algorithm>
#include <cstring>

namespace Fby::Video
{
namespace
{
void SetError(std::string* OutError, const char* InMessage)
{
    if (OutError != nullptr)
    {
        *OutError = InMessage;
    }
}
}

VideoAudioStream::~VideoAudioStream()
{
    Close();
}

bool VideoAudioStream::Open(std::span<const std::byte> InWebm, std::string* InError)
{
    Close();
    if (!MDemuxer.Open(InWebm, InError))
    {
        return false;
    }
    if (!MDemuxer.HasAudio())
    {
        SetError(InError, "video audio: WebM has no A_OPUS track");
        MDemuxer.Close();
        return false;
    }
    const WebmAudioTrackInfo& ScopedTrack = MDemuxer.AudioTrack();
    if (!MDecoder.Initialize(ScopedTrack.sampleRate, ScopedTrack.channels, InError))
    {
        MDemuxer.Close();
        return false;
    }
    MSampleRate = ScopedTrack.sampleRate;
    MChannels = ScopedTrack.channels;
    MOpened = true;
    return true;
}

bool VideoAudioStream::Initialize(std::uint32_t InSampleRate, std::uint32_t InChannels, std::string* InError)
{
    Close();
    if (InSampleRate < 8000 || InSampleRate > 192000 || InChannels == 0 || InChannels > kMaxChannels)
    {
        SetError(InError, "video audio: sample rate or channel count is out of range");
        return false;
    }
    ResetState();
    MSampleRate = InSampleRate;
    MChannels = InChannels;
    MOpened = true;
    return true;
}

void VideoAudioStream::Close() noexcept
{
    MDemuxer.Close();
    MDecoder.Shutdown();
    ResetState();
    MOpened = false;
}

void VideoAudioStream::Rewind() noexcept
{
    if (!MOpened)
    {
        return;
    }
    MDemuxer.Rewind();
    MWriteFrame = 0;
    MReadFrame = 0;
    MFrameCount = 0;
    MPendingFrames = 0;
    MPendingTimestampNs = 0;
    MFirstTimestampNs = 0;
    MPlayedFrames = 0;
    MLastPushedTimestampNs = 0;
    MSeekTargetNs = 0;
    MClockSeeded = false;
}

bool VideoAudioStream::SeekToNs(std::uint64_t InTimestampNs) noexcept
{
    if (!MOpened || !MDemuxer.SeekToNs(InTimestampNs))
    {
        return false;
    }
    MWriteFrame = 0;
    MReadFrame = 0;
    MFrameCount = 0;
    MPendingFrames = 0;
    MPendingTimestampNs = 0;
    MFirstTimestampNs = InTimestampNs;
    MPlayedFrames = 0;
    MLastPushedTimestampNs = InTimestampNs;
    MSeekTargetNs = InTimestampNs;
    MClockSeeded = true;
    return true;
}

void VideoAudioStream::ResetState() noexcept
{
    MWriteFrame = 0;
    MReadFrame = 0;
    MFrameCount = 0;
    MPendingFrames = 0;
    MPendingTimestampNs = 0;
    MFirstTimestampNs = 0;
    MPlayedFrames = 0;
    MLastPushedTimestampNs = 0;
    MSeekTargetNs = 0;
    MClockSeeded = false;
    MRate = 1.0f;
    MSampleRate = 0;
    MChannels = 0;
}

std::uint64_t VideoAudioStream::AudioClockNs() const noexcept
{
    if (!MClockSeeded || MSampleRate == 0)
    {
        return 0;
    }
    return MFirstTimestampNs + (MPlayedFrames * 1000000000ull) / MSampleRate;
}

bool VideoAudioStream::PushPcm(std::span<const float> InInterleaved, std::uint64_t InTimestampNs)
{
    if (!MOpened || MChannels == 0 || InInterleaved.empty())
    {
        return false;
    }
    const std::uint32_t ScopedFrames = static_cast<std::uint32_t>(InInterleaved.size() / MChannels);
    if (ScopedFrames == 0 || ScopedFrames > kMaxRingFrames - MFrameCount)
    {
        return false;
    }
    const std::uint32_t ScopedFirst = std::min(ScopedFrames, kMaxRingFrames - MWriteFrame);
    std::memcpy(MRing.data() + static_cast<std::size_t>(MWriteFrame) * MChannels, InInterleaved.data(),
                static_cast<std::size_t>(ScopedFirst) * MChannels * sizeof(float));
    if (ScopedFirst < ScopedFrames)
    {
        std::memcpy(MRing.data(), InInterleaved.data() + static_cast<std::size_t>(ScopedFirst) * MChannels,
                    static_cast<std::size_t>(ScopedFrames - ScopedFirst) * MChannels * sizeof(float));
    }
    MWriteFrame = (MWriteFrame + ScopedFrames) % kMaxRingFrames;
    MFrameCount += ScopedFrames;
    if (!MClockSeeded)
    {
        MFirstTimestampNs = InTimestampNs;
        MClockSeeded = true;
    }
    MLastPushedTimestampNs = InTimestampNs;
    return true;
}

void VideoAudioStream::SetPlaybackRate(float InRate) noexcept
{
    MRate = std::clamp(InRate, 0.25f, 4.0f);
}

std::uint32_t VideoAudioStream::CopyFrames(std::span<float> OutInterleaved, std::uint32_t InFrames) noexcept
{
    if (MChannels == 0 || InFrames == 0)
    {
        return 0;
    }
    const std::uint32_t ScopedFrames = std::min(InFrames, MFrameCount);
    if (ScopedFrames == 0)
    {
        return 0;
    }
    const std::uint32_t ScopedFirst = std::min(ScopedFrames, kMaxRingFrames - MReadFrame);
    std::memcpy(OutInterleaved.data(), MRing.data() + static_cast<std::size_t>(MReadFrame) * MChannels,
                static_cast<std::size_t>(ScopedFirst) * MChannels * sizeof(float));
    if (ScopedFirst < ScopedFrames)
    {
        std::memcpy(OutInterleaved.data() + static_cast<std::size_t>(ScopedFirst) * MChannels, MRing.data(),
                    static_cast<std::size_t>(ScopedFrames - ScopedFirst) * MChannels * sizeof(float));
    }
    MReadFrame = (MReadFrame + ScopedFrames) % kMaxRingFrames;
    MFrameCount -= ScopedFrames;
    MPlayedFrames += ScopedFrames;
    return ScopedFrames;
}

std::uint32_t VideoAudioStream::Read(std::span<float> OutInterleaved) noexcept
{
    if (MChannels == 0 || OutInterleaved.empty())
    {
        return 0;
    }
    const std::uint32_t ScopedRequested = static_cast<std::uint32_t>(OutInterleaved.size() / MChannels);
    if (MRate == 1.0f)
    {
        return CopyFrames(OutInterleaved, ScopedRequested);
    }
    const std::uint32_t ScopedWanted =
        static_cast<std::uint32_t>(static_cast<double>(ScopedRequested) * static_cast<double>(MRate) + 0.5);
    const std::uint32_t ScopedSource = std::min(ScopedWanted, MFrameCount);
    if (ScopedSource == 0)
    {
        return 0;
    }
    const std::span<float> ScopedScratch(MResampleScratch.data(), static_cast<std::size_t>(ScopedSource) * MChannels);
    const std::uint32_t ScopedCopied = CopyFrames(ScopedScratch, ScopedSource);
    if (ScopedCopied == 0)
    {
        return 0;
    }
    const std::uint32_t ScopedOutput =
        std::min(ScopedRequested, static_cast<std::uint32_t>(static_cast<double>(ScopedCopied) / MRate));
    for (std::uint32_t ScopedI = 0; ScopedI < ScopedOutput; ++ScopedI)
    {
        const std::uint32_t ScopedSourceIndex =
            std::min(static_cast<std::uint32_t>(static_cast<double>(ScopedI) * MRate), ScopedCopied - 1);
        for (std::uint32_t ScopedChannel = 0; ScopedChannel < MChannels; ++ScopedChannel)
        {
            OutInterleaved[static_cast<std::size_t>(ScopedI) * MChannels + ScopedChannel] =
                ScopedScratch[static_cast<std::size_t>(ScopedSourceIndex) * MChannels + ScopedChannel];
        }
    }
    return ScopedOutput;
}

bool VideoAudioStream::PushPending() noexcept
{
    if (MPendingFrames == 0 || MPendingFrames > kMaxRingFrames - MFrameCount)
    {
        return false;
    }
    const std::uint32_t ScopedFirst = std::min(MPendingFrames, kMaxRingFrames - MWriteFrame);
    std::memcpy(MRing.data() + static_cast<std::size_t>(MWriteFrame) * MChannels, MPending.data(),
                static_cast<std::size_t>(ScopedFirst) * MChannels * sizeof(float));
    if (ScopedFirst < MPendingFrames)
    {
        std::memcpy(MRing.data(), MPending.data() + static_cast<std::size_t>(ScopedFirst) * MChannels,
                    static_cast<std::size_t>(MPendingFrames - ScopedFirst) * MChannels * sizeof(float));
    }
    MWriteFrame = (MWriteFrame + MPendingFrames) % kMaxRingFrames;
    MFrameCount += MPendingFrames;
    if (!MClockSeeded)
    {
        MFirstTimestampNs = MPendingTimestampNs;
        MClockSeeded = true;
    }
    MLastPushedTimestampNs = MPendingTimestampNs;
    MPendingFrames = 0;
    return true;
}

AudioPumpStatus VideoAudioStream::PumpOne(std::string* InError)
{
    if (!MOpened)
    {
        SetError(InError, "video audio: stream is not open");
        return AudioPumpStatus::Error;
    }
    if (MPendingFrames != 0)
    {
        return PushPending() ? AudioPumpStatus::Decoded : AudioPumpStatus::Buffered;
    }
    WebmAudioPacket ScopedPacket;
    DemuxStatus ScopedStatus = DemuxStatus::End;
    for (std::uint32_t ScopedAttempt = 0; ScopedAttempt < 512; ++ScopedAttempt)
    {
        ScopedStatus = MDemuxer.NextAudioPacket(ScopedPacket, InError);
        if (ScopedStatus != DemuxStatus::Frame || MSeekTargetNs == 0 || ScopedPacket.timestampNs >= MSeekTargetNs)
        {
            break;
        }
    }
    if (ScopedStatus == DemuxStatus::End)
    {
        return AudioPumpStatus::End;
    }
    if (ScopedStatus == DemuxStatus::Error)
    {
        return AudioPumpStatus::Error;
    }
    if (MSeekTargetNs != 0 && ScopedPacket.timestampNs >= MSeekTargetNs)
    {
        MSeekTargetNs = 0;
    }
    const std::span<std::uint8_t> ScopedPacketBytes(
        const_cast<std::uint8_t*>(reinterpret_cast<const std::uint8_t*>(ScopedPacket.payload.data())),
        ScopedPacket.payload.size());
    std::uint32_t ScopedFrames = 0;
    std::span<float> ScopedPcm(MPending.data(), static_cast<std::size_t>(kMaxOpusFrameSamples) * MChannels);
    if (!MDecoder.Decode(ScopedPacketBytes, ScopedPcm, &ScopedFrames, InError))
    {
        return AudioPumpStatus::Error;
    }
    MPendingFrames = ScopedFrames;
    MPendingTimestampNs = ScopedPacket.timestampNs;
    return PushPending() ? AudioPumpStatus::Decoded : AudioPumpStatus::Buffered;
}

}
