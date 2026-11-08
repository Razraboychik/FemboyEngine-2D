// FemboyEngine2D - Opus decoding for video audio tracks.
#include "Engine/Video/VideoAudioDecoder.hpp"

#include <opus.h>

#include <algorithm>

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

VideoAudioDecoder::~VideoAudioDecoder()
{
    Shutdown();
}

bool VideoAudioDecoder::Initialize(std::uint32_t InSampleRate, std::uint32_t InChannels, std::string* InError)
{
    Shutdown();
    SetError(InError, "");
    if (InSampleRate < 8000 || InSampleRate > 192000 || InChannels == 0 || InChannels > 2)
    {
        SetError(InError, "opus: sample rate or channel count is out of range");
        return false;
    }
    int ScopedError = OPUS_OK;
    MDecoder = opus_decoder_create(static_cast<opus_int32>(InSampleRate), static_cast<int>(InChannels), &ScopedError);
    if (MDecoder == nullptr || ScopedError != OPUS_OK)
    {
        MDecoder = nullptr;
        SetError(InError, "opus: decoder creation failed");
        return false;
    }
    MSampleRate = InSampleRate;
    MChannels = InChannels;
    return true;
}

void VideoAudioDecoder::Shutdown() noexcept
{
    if (MDecoder != nullptr)
    {
        opus_decoder_destroy(static_cast<OpusDecoder*>(MDecoder));
        MDecoder = nullptr;
    }
    MSampleRate = 0;
    MChannels = 0;
}

bool VideoAudioDecoder::Decode(std::span<const std::uint8_t> InPacket, std::span<float> OutPcm, std::uint32_t* OutFrames,
                               std::string* InError) noexcept
{
    if (OutFrames != nullptr)
    {
        *OutFrames = 0;
    }
    if (MDecoder == nullptr)
    {
        SetError(InError, "opus: decoder is not initialized");
        return false;
    }
    if (InPacket.empty() || OutPcm.size() < kMaxOpusFrameSamples * MChannels)
    {
        SetError(InError, "opus: empty packet or short destination");
        return false;
    }
    const int ScopedFrames = opus_decode_float(static_cast<OpusDecoder*>(MDecoder), InPacket.data(),
                                               static_cast<opus_int32>(InPacket.size()), OutPcm.data(),
                                               static_cast<int>(kMaxOpusFrameSamples), 0);
    if (ScopedFrames < 0)
    {
        SetError(InError, "opus: packet decode failed");
        return false;
    }
    if (OutFrames != nullptr)
    {
        *OutFrames = static_cast<std::uint32_t>(ScopedFrames);
    }
    return true;
}

}
