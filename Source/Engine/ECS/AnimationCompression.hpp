// FemboyEngine2D - animation clip compression (tolerance reduction + quantization).
#pragma once

#include "Engine/ECS/AnimationTimeline.hpp"

#include <array>
#include <cstdint>
#include <type_traits>

namespace Fby::Animation
{

// A compressed clip is a runtime playback format: keys keep their interpolation
// mode, times are normalized to 16 bits over the clip duration and values are
// int16 with a per-track scale. Authored curves stay in the timeline; baking
// writes this structure into a `.fanim` asset.
inline constexpr std::uint32_t kMaxCompressedKeys = kMaxTimelineKeys;

struct CompressedKey
{
    // Time in [0, 65535] over the clip duration.
    std::uint16_t Time = 0;
    std::int16_t ValueX = 0;
    std::int16_t ValueY = 0;
    std::uint8_t Interpolation = static_cast<std::uint8_t>(Interpolation::Linear);
    std::uint8_t Reserved = 0;
};

static_assert(sizeof(CompressedKey) == 8);

struct CompressedTrack
{
    TrackKind Kind = TrackKind::BonePosition;
    std::uint16_t FirstKey = 0;
    std::uint16_t KeyCount = 0;
    std::int16_t BoneIndex = -1;
    std::uint16_t Flags = 0;
    std::uint64_t BoneHash = 0;
    Color32 Color = Color32::FromBytes(255, 180, 220);
    // int16 value 32767 maps to +ScaleX / +ScaleY.
    float ScaleX = 1.0f;
    float ScaleY = 1.0f;
};

static_assert(sizeof(CompressedTrack) == 40);

struct CompressedClip
{
    char Name[kMaxTimelineName]{};
    float Duration = 5.0f;
    float Fps = 24.0f;
    std::uint32_t Flags = 0;
    std::uint32_t SourceHash = 0;
    std::uint16_t TrackCount = 0;
    std::uint16_t KeyCount = 0;
    std::array<CompressedTrack, kMaxTimelineTracks> Tracks{};
    std::array<CompressedKey, kMaxCompressedKeys> Keys{};
};

static_assert(std::is_trivially_copyable_v<CompressedClip>);
static_assert(std::is_standard_layout_v<CompressedClip>);

struct CompressionReport
{
    std::uint32_t SourceKeys = 0;
    std::uint32_t KeptKeys = 0;
    std::uint32_t MaxErrorX1000 = 0;
    std::uint32_t Tracks = 0;
    bool Truncated = false;
};

// Drops keys that a linear segment reproduces within `InTolerance` (in value
// units) and quantizes the survivors. Returns false only for an invalid input.
[[nodiscard]] bool CompressTimeline(const AnimationTimeline& InTimeline, float InTolerance,
                                    CompressedClip* OutClip, CompressionReport* OutReport = nullptr) noexcept;

// Rebuilds a playable timeline. Interpolation modes are preserved; tangents are
// regenerated as flat, so a Bezier curve is approximated by its kept keys.
[[nodiscard]] bool DecompressTimeline(const CompressedClip& InClip, AnimationTimeline* OutTimeline) noexcept;

// Evaluates a compressed clip at `InTime` without decompressing it.
[[nodiscard]] Vector2D EvaluateCompressedTrack(const CompressedClip& InClip, std::uint16_t InTrack, float InTime,
                                              Vector2D InFallback = {}) noexcept;
}
