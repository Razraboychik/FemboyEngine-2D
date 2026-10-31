// FemboyEngine2D - animation timeline model.
#pragma once

#include "Engine/Core/Types.hpp"
#include "Engine/ECS/Skeleton.hpp"
#include "Engine/ECS/SpriteAnimation.hpp"

#include <array>
#include <cstdint>
#include <type_traits>

namespace Fby::Animation
{

inline constexpr std::uint32_t kMaxTimelineTracks = 64;
inline constexpr std::uint32_t kMaxTimelineKeys = 512;
inline constexpr std::uint32_t kMaxTimelineEvents = 64;
inline constexpr std::uint32_t kMaxTimelineName = 64;

enum class TrackKind : std::uint8_t
{
    BonePosition = 0,
    BoneRotation,
    BoneScale,
    SpriteClip,
    Count
};

enum class Interpolation : std::uint8_t
{
    Step = 0,
    Linear,
    Bezier,
};

enum class TimelineEventKind : std::uint8_t
{
    Audio = 0,
    Script,
};

// One key. `value` is vec2 for every track kind: bone position uses both lanes,
struct TimelineKey
{
    float time = 0.0f;
    Vector2D value{};
    Vector2D inTangent{-0.1f, 0.0f};
    Vector2D outTangent{0.1f, 0.0f};
    Interpolation interpolation = Interpolation::Bezier;
    std::uint8_t flags = 0;
    std::uint16_t reserved = 0;
};

static_assert(sizeof(TimelineKey) == 32);

struct TimelineTrack
{
    TrackKind kind = TrackKind::BonePosition;
    std::uint16_t firstKey = 0;
    std::uint16_t keyCount = 0;
    std::int16_t boneIndex = -1;
    std::uint16_t flags = 0;
    std::uint64_t boneHash = 0;
    Color32 color = Color32::FromBytes(255, 180, 220);
};

struct TimelineEvent
{
    float time = 0.0f;
    TimelineEventKind kind = TimelineEventKind::Audio;
    std::uint8_t once = 1;
    std::uint8_t flags = 0;
    // AssetId value (audio clip)
    std::uint32_t audioAsset = 0;
    // ScriptRuntime handle
    std::uint32_t scriptProgram = 0;
    // Target entity for script events
    std::uint32_t entity = 0;
    float volume = 1.0f;
};

// Trigger adapter: the panel owns no engine singletons. A host installs plain
struct TimelineTriggerSink
{
    void* user = nullptr;
    void (*playAudio)(void* InUser, std::uint64_t InAssetId, float InVolume) noexcept = nullptr;
    void (*runScript)(void* InUser, std::uint32_t InProgram, std::uint32_t InEntity) noexcept = nullptr;
};

class AnimationTimeline
{
public:
    void Clear() noexcept;
    [[nodiscard]] bool Empty() const noexcept
    {
        return trackCount == 0;
    }

    [[nodiscard]] std::uint16_t AddTrack(TrackKind InKind, std::uint64_t InBoneHash,
                                         std::uint32_t InColorRgba) noexcept;
    [[nodiscard]] std::int16_t FindTrack(TrackKind InKind, std::uint64_t InBoneHash) const noexcept;
    [[nodiscard]] std::int16_t FindTrackByBone(TrackKind InKind, std::int16_t InBoneIndex) const noexcept;

    // Keys inside a track stay sorted by time. Returns the key's global index,
    [[nodiscard]] std::uint16_t AddKey(std::uint16_t InTrack, float InTime, Vector2D InValue,
                                       Interpolation InInterpolation = Interpolation::Bezier) noexcept;
    [[nodiscard]] bool RemoveKey(std::uint16_t InTrack, std::uint16_t InKeyIndex) noexcept;
    [[nodiscard]] bool SetKeyTangents(std::uint16_t InTrack, std::uint16_t InKeyIndex, Vector2D InInTangent,
                                      Vector2D OutTangent) noexcept;

    [[nodiscard]] std::uint16_t AddEvent(const TimelineEvent& InEvent) noexcept;
    [[nodiscard]] bool RemoveEvent(std::uint16_t InIndex) noexcept;

    // Evaluates one track at `time`. Empty tracks return `fallback`.
    [[nodiscard]] Vector2D EvaluateTrack(std::uint16_t InTrack, float InTime, Vector2D InFallback = {}) const noexcept;

    // Writes the authored local pose into `skeleton` and evaluates SkeletalAnimation. Bone
    void ApplyPose(SkeletonComponent& InSkeleton, float InTime) const noexcept;
    // Applies the active SpriteAnimation clip at `time` (startFrame/frameCount/currentFrame).
    void ApplySpriteClip(SpriteAnimationComponent& InSpriteAnimation, float InTime) const noexcept;

    // Fires events in (previousTime, time]; when looping, the caller also passes
    void DispatchEvents(float InPreviousTime, float InTime, bool InLooped,
                        const TimelineTriggerSink& InSink) const noexcept;

    [[nodiscard]] bool Validate() const noexcept;
    [[nodiscard]] float LastKeyTime() const noexcept;

    // Data (public on purpose: the editor serializes it as plain bytes).
    char name[kMaxTimelineName]{};
    float duration = 5.0f;
    float fps = 24.0f;
    std::uint32_t flags = 0;
    std::array<TimelineTrack, kMaxTimelineTracks> tracks{};
    std::uint32_t trackCount = 0;
    std::array<TimelineKey, kMaxTimelineKeys> keys{};
    std::uint32_t keyCount = 0;
    std::array<TimelineEvent, kMaxTimelineEvents> events{};
    std::uint32_t eventCount = 0;

private:
// (All evaluation helpers are free functions in the .cpp so the authored
};

static_assert(std::is_trivially_copyable_v<AnimationTimeline>);
static_assert(std::is_standard_layout_v<AnimationTimeline>);
}
