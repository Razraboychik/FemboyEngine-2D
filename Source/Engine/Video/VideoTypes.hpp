// FemboyEngine2D - types shared by the video stream component and its player.
#pragma once

#include <cstdint>

namespace Fby::Video
{

// Sentinel for "no player slot / no texture array". The values mirror
inline constexpr std::uint32_t kInvalidVideoPlayer = 0xFFFFFFFFu;
inline constexpr std::uint32_t kInvalidVideoTexture = 0xFFFFFFFFu;

// Fixed capacities. The frame pool is also the texture-array layer count, so a
inline constexpr std::uint32_t kMaxVideoPlayers = 8;
inline constexpr std::uint32_t kVideoFramePoolSize = 4;
// The ring is deeper than the pool: a couple of decoded frames can wait while
inline constexpr std::uint32_t kVideoRingCapacity = 8;

// VideoStreamComponent::flags. Valid and Finished are written by VideoSystem;
enum VideoStreamFlags : std::uint32_t
{
    kVideoFlagValid = 1u << 0,
    kVideoFlagFinished = 1u << 1,
    kVideoFlagAlpha = 1u << 2,
    kVideoFlagError = 1u << 3,
};

// One decode step result. The background thread treats PoolExhausted and
enum class DecodeStatus : std::uint8_t
{
    Decoded = 0,
    End,
    RingFull,
    PoolExhausted,
    Error,
};
}
