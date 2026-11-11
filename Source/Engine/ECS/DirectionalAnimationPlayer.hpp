// FemboyEngine2D - fixed-capacity directional animation player.
#pragma once

#include "Engine/ECS/AnimationClipPose.hpp"
#include "Engine/ECS/DirectionalBlend.hpp"
#include "Engine/ECS/Skeleton.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace Fby
{
class VirtualFileSystem;
}

namespace Fby::Animation
{

inline constexpr std::uint32_t kMaxDirectionalPlayerClips = 8;
inline constexpr std::uint32_t kNoDirectionalClip = 0xFFFFFFFFu;

// Owns a fixed set of .fanim clips, their bone bindings for one skeleton and a
// shared phase. It is a flat value object, not an ECS component: a gameplay
// system keeps one per character and feeds it Facing2D. The table is sampled
// only when ApplyPose runs, so the player adds no tick of its own.
class DirectionalAnimationPlayer
{
public:
    DirectionalAnimationPlayer() = default;

    // Copies the clip into its fixed slot. Returns false when the pool is full
    // or the id is already present.
    [[nodiscard]] bool SetClip(std::uint32_t InClipId, const CompressedClip& InClip) noexcept;
    [[nodiscard]] bool SetClipFromBytes(std::uint32_t InClipId, std::span<const std::byte> InBytes,
                                        std::string* OutError = nullptr);
    [[nodiscard]] bool LoadClip(VirtualFileSystem& InVfs, std::uint32_t InClipId, std::string_view InPath,
                                std::string* OutError = nullptr);
    [[nodiscard]] bool SetBlendTable(const DirectionalBlendTable& InTable) noexcept;

    // Resolves every clip's bone hashes once. Call after the clips are set and
    // the skeleton rest pose is captured.
    [[nodiscard]] bool BuildBindings(const SkeletonComponent& InSkeleton) noexcept;

    // The phase is one cycle; every directional clip samples at phase * duration
    // so the eight cycles keep the same foot phase.
    void SetPhase(float InPhase) noexcept;
    [[nodiscard]] float Phase() const noexcept
    {
        return MPhase;
    }
    void AdvancePhase(float InDt, float InCycleDuration, bool InIsLooping) noexcept;

    // Samples the table from the facing vector, accumulates the weighted clips
    // and resolves the result into the skeleton. Returns false when the table or
    // bindings are not ready.
    [[nodiscard]] bool ApplyPose(SkeletonComponent& InSkeleton, Vector2D InFacing) noexcept;

    // Writes the dominant clip's SpriteClip track at the current phase.
    [[nodiscard]] bool ApplySprite(SpriteAnimationComponent& InSpriteAnimation) const noexcept;

    [[nodiscard]] std::uint32_t DominantClip() const noexcept
    {
        return MDominantClip;
    }
    [[nodiscard]] const DirectionalBlendSample& LastSample() const noexcept
    {
        return MSample;
    }
    [[nodiscard]] std::uint16_t ClipCount() const noexcept
    {
        return MClipCount;
    }

private:
    [[nodiscard]] std::int32_t FindClip(std::uint32_t InClipId) const noexcept;

    std::array<CompressedClip, kMaxDirectionalPlayerClips> MClips{};
    std::array<std::uint32_t, kMaxDirectionalPlayerClips> MClipIds{};
    std::array<AnimationClipBinding, kMaxDirectionalPlayerClips> MBindings{};
    DirectionalBlendTable MTable{};
    DirectionalBlendSample MSample{};
    std::uint32_t MDominantClip = kNoDirectionalClip;
    float MPhase = 0.0f;
    std::uint16_t MClipCount = 0;
    bool MHasTable = false;
    bool MBindingsValid = false;
};
}
