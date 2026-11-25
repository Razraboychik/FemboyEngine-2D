// FemboyEngine2D - Opus voice codec implementation.
#include "Engine/Audio/VoiceCodec.hpp"

#include <algorithm>

#include <cstring>

#if defined(FBY_HAS_OPUS)
#include "opus.h"
#endif

namespace Fby
{

VoiceEncoder::~VoiceEncoder()
{
    Shutdown();
}

bool VoiceEncoder::Initialize(const VoiceCodecConfig& InConfig) noexcept
{
    Shutdown();
#if defined(FBY_HAS_OPUS)
    int ScopedError = OPUS_OK;
    auto* ScopedEncoder = opus_encoder_create(static_cast<opus_int32>(kVoiceSampleRate), 1, OPUS_APPLICATION_VOIP,
                                              &ScopedError);
    if (ScopedEncoder == nullptr || ScopedError != OPUS_OK)
    {
        if (ScopedEncoder != nullptr)
        {
            opus_encoder_destroy(ScopedEncoder);
        }
        return false;
    }
    const opus_int32 ScopedBitrate = static_cast<opus_int32>(std::clamp(InConfig.Bitrate, 6000u, 510000u));
    const int ScopedComplexity = std::clamp(InConfig.Complexity, 0, 10);
    const int ScopedLoss = std::clamp(InConfig.PacketLossPercent, 0, 100);
    const int ScopedResults[] = {
        opus_encoder_ctl(ScopedEncoder, OPUS_SET_BITRATE(ScopedBitrate)),
        opus_encoder_ctl(ScopedEncoder, OPUS_SET_COMPLEXITY(ScopedComplexity)),
        opus_encoder_ctl(ScopedEncoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE)),
        opus_encoder_ctl(ScopedEncoder, OPUS_SET_INBAND_FEC(InConfig.InbandFec ? 1 : 0)),
        opus_encoder_ctl(ScopedEncoder, OPUS_SET_PACKET_LOSS_PERC(ScopedLoss)),
        opus_encoder_ctl(ScopedEncoder, OPUS_SET_DTX(InConfig.Dtx ? 1 : 0)),
    };
    for (const int ScopedResult : ScopedResults)
    {
        if (ScopedResult != OPUS_OK)
        {
            opus_encoder_destroy(ScopedEncoder);
            return false;
        }
    }
    MEncoder = ScopedEncoder;
    return true;
#else
    (void)InConfig;
    return false;
#endif
}

void VoiceEncoder::Shutdown() noexcept
{
#if defined(FBY_HAS_OPUS)
    if (MEncoder != nullptr)
    {
        opus_encoder_destroy(static_cast<OpusEncoder*>(MEncoder));
    }
#endif
    MEncoder = nullptr;
}

std::uint32_t VoiceEncoder::Encode(std::span<const float> InSamples, std::span<std::uint8_t> OutPacket,
                                   std::int32_t* OutError) noexcept
{
    if (OutError != nullptr)
    {
        *OutError = 0;
    }
#if defined(FBY_HAS_OPUS)
    if (MEncoder == nullptr || InSamples.size() != kVoiceFrameSamples || OutPacket.empty())
    {
        if (OutError != nullptr)
        {
            *OutError = OPUS_BAD_ARG;
        }
        return 0;
    }
    const opus_int32 ScopedBytes =
        opus_encode_float(static_cast<OpusEncoder*>(MEncoder), InSamples.data(),
                    static_cast<int>(kVoiceFrameSamples), OutPacket.data(),
                    static_cast<opus_int32>(OutPacket.size()));
    if (ScopedBytes < 0 && OutError != nullptr)
    {
        *OutError = ScopedBytes;
    }
    return ScopedBytes > 0 ? static_cast<std::uint32_t>(ScopedBytes) : 0;
#else
    (void)InSamples;
    (void)OutPacket;
    return 0;
#endif
}

VoiceDecoder::~VoiceDecoder()
{
    Shutdown();
}

bool VoiceDecoder::Initialize() noexcept
{
    Shutdown();
#if defined(FBY_HAS_OPUS)
    int ScopedError = OPUS_OK;
    auto* ScopedDecoder = opus_decoder_create(static_cast<opus_int32>(kVoiceSampleRate), 1, &ScopedError);
    if (ScopedDecoder == nullptr || ScopedError != OPUS_OK)
    {
        if (ScopedDecoder != nullptr)
        {
            opus_decoder_destroy(ScopedDecoder);
        }
        return false;
    }
    MDecoder = ScopedDecoder;
    return true;
#else
    return false;
#endif
}

void VoiceDecoder::Shutdown() noexcept
{
#if defined(FBY_HAS_OPUS)
    if (MDecoder != nullptr)
    {
        opus_decoder_destroy(static_cast<OpusDecoder*>(MDecoder));
    }
#endif
    MDecoder = nullptr;
}

std::uint32_t VoiceDecoder::Decode(std::span<const std::uint8_t> InPacket, std::span<float> OutSamples,
                                   std::int32_t* OutError) noexcept
{
    if (OutError != nullptr)
    {
        *OutError = 0;
    }
#if defined(FBY_HAS_OPUS)
    if (MDecoder == nullptr || OutSamples.size() < kVoiceFrameSamples || InPacket.empty())
    {
        if (OutError != nullptr)
        {
            *OutError = OPUS_BAD_ARG;
        }
        return 0;
    }
    const int ScopedFrames = opus_decode_float(static_cast<OpusDecoder*>(MDecoder), InPacket.data(),
                                         static_cast<opus_int32>(InPacket.size()), OutSamples.data(),
                                         static_cast<int>(kVoiceFrameSamples), 0);
    if (ScopedFrames < 0 && OutError != nullptr)
    {
        *OutError = ScopedFrames;
    }
    return ScopedFrames > 0 ? static_cast<std::uint32_t>(ScopedFrames) : 0;
#else
    (void)InPacket;
    (void)OutSamples;
    return 0;
#endif
}

std::uint32_t VoiceDecoder::DecodeFec(std::span<const std::uint8_t> InPacket, std::span<float> OutSamples,
                                      std::int32_t* OutError) noexcept
{
    if (OutError != nullptr)
    {
        *OutError = 0;
    }
#if defined(FBY_HAS_OPUS)
    if (MDecoder == nullptr || OutSamples.size() < kVoiceFrameSamples || InPacket.empty())
    {
        if (OutError != nullptr)
        {
            *OutError = OPUS_BAD_ARG;
        }
        return 0;
    }
    const int ScopedFrames = opus_decode_float(static_cast<OpusDecoder*>(MDecoder), InPacket.data(),
                                               static_cast<opus_int32>(InPacket.size()), OutSamples.data(),
                                               static_cast<int>(kVoiceFrameSamples), 1);
    if (ScopedFrames < 0 && OutError != nullptr)
    {
        *OutError = ScopedFrames;
    }
    return ScopedFrames > 0 ? static_cast<std::uint32_t>(ScopedFrames) : 0;
#else
    (void)InPacket;
    (void)OutSamples;
    return 0;
#endif
}

std::uint32_t VoiceDecoder::Conceal(std::span<float> OutSamples, std::int32_t* OutError) noexcept
{
    if (OutError != nullptr)
    {
        *OutError = 0;
    }
#if defined(FBY_HAS_OPUS)
    if (MDecoder == nullptr || OutSamples.size() < kVoiceFrameSamples)
    {
        if (OutError != nullptr)
        {
            *OutError = OPUS_BAD_ARG;
        }
        return 0;
    }
    const int ScopedFrames = opus_decode_float(static_cast<OpusDecoder*>(MDecoder), nullptr, 0, OutSamples.data(),
                                         static_cast<int>(kVoiceFrameSamples), 0);
    if (ScopedFrames < 0 && OutError != nullptr)
    {
        *OutError = ScopedFrames;
    }
    return ScopedFrames > 0 ? static_cast<std::uint32_t>(ScopedFrames) : 0;
#else
    (void)OutSamples;
    return 0;
#endif
}
}
