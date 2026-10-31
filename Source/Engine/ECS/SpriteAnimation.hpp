// FemboyEngine2D - "SpriteAnimation", the fixed-step flipbook system.
#pragma once

#include "Engine/Assets/AssetMeta.hpp"
#include "Engine/Core/Types.hpp"
#include "Engine/ECS/Components.hpp"
#include "Engine/Core/AssetId.hpp"
#include "Engine/ECS/Schedule.hpp"
#include "Engine/Core/Mirror.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include <type_traits>

namespace Fby
{

// The public layout is fixed. Do not reorder:
struct SpriteAnimationComponent
{
    // Transactional metadata handle: the .femeta row owns the atlas path,
    Assets::AssetHandle TextureAtlas{};
    // First absolute cell inside the sheet
    std::uint32_t StartFrame = 0;
    // Cells in this clip
    std::uint32_t FrameCount = 0;
    // 0..frameCount-1, local to the clip
    std::uint32_t CurrentFrame = 0;
    // Seconds per cell
    float FrameDuration = 0.1f;
    // Accumulator inside the current cell
    float ElapsedTime = 0.0f;
    bool IsLooping = true;
    bool IsPlaying = true;
};

// Optional layout metadata. `columns == 0` means "infer from frameCount"
struct SpriteSheetComponent
{
    std::uint32_t Columns = 0;
    std::uint32_t Rows = 1;
    std::uint32_t Layer = 0;
    std::uint32_t Flags = 0;
    Vector2D UVMin{0.0f, 0.0f};
    Vector2D UVMax{1.0f, 1.0f};
};

static_assert(std::is_trivially_copyable_v<SpriteAnimationComponent>);
static_assert(std::is_standard_layout_v<SpriteAnimationComponent>);
static_assert(std::is_trivially_copyable_v<SpriteSheetComponent>);
static_assert(std::is_standard_layout_v<SpriteSheetComponent>);

struct SpriteFrameRect
{
    Vector2D uvMin{0.0f, 0.0f};
    Vector2D uvMax{1.0f, 1.0f};
    bool valid = false;
};

// Fixed-capacity animation event queue. SpriteAnimationSystem emits a frame-crossing event
inline constexpr std::uint32_t kSpriteAnimationEventFrame = 0;
inline constexpr std::uint32_t kSpriteAnimationEventContact = 1;

struct SpriteFrameEvent
{
    Entity entity = entt::null;
    std::uint32_t frame = 0;
    std::uint32_t eventId = kSpriteAnimationEventFrame;
    std::uint32_t sequence = 0;
};

class SpriteAnimationEventQueue
{
public:
    static constexpr std::uint32_t kCapacity = 512;

    void Clear() noexcept
    {
        MCount = 0;
    }
    [[nodiscard]] bool Push(Entity InEntity, std::uint32_t InFrame, std::uint32_t InEventId) noexcept
    {
        if (MCount >= kCapacity)
        {
            return false;
        }
        MEvents[MCount++] = SpriteFrameEvent{InEntity, InFrame, InEventId, MSequence++};
        return true;
    }
    [[nodiscard]] std::span<const SpriteFrameEvent> Events() const noexcept
    {
        return {MEvents.data(), MCount};
    }
    [[nodiscard]] std::uint32_t Count() const noexcept
    {
        return MCount;
    }

private:
    std::array<SpriteFrameEvent, kCapacity> MEvents{};
    std::uint32_t MCount = 0;
    std::uint32_t MSequence = 0;
};

// Pure function: no registry, no allocation, safe to call from the editor while
[[nodiscard]] SpriteFrameRect SpriteFrameUv(const SpriteAnimationComponent& InSpriteAnimation,
                                          const SpriteSheetComponent* InGrid = nullptr) noexcept;

// Writes the computed rect (and, when a grid is present, the array layer) into
void ApplySpriteFrame(const SpriteAnimationComponent& InSpriteAnimation, const SpriteSheetComponent* InGrid, Sprite2D& InSprite) noexcept;

// Advances the accumulator by a fixed dt. Exposed for unit tests and for tools
[[nodiscard]] bool AdvanceSpriteAnimation(SpriteAnimationComponent& InSpriteAnimation, float InDt) noexcept;

// FixedUpdate system. Reads ctx.dt, never allocates, processes every
class SpriteAnimationSystem
{
public:
    void Run(Ecs::SystemContext& InContext) const;
    void Run(Ecs::SystemContext& InContext, SpriteAnimationEventQueue& InEvents) const;
    void Run(Ecs::Registry& InRegistry, float InDt) const;
    void Run(Ecs::Registry& InRegistry, float InDt, SpriteAnimationEventQueue* InEvents) const;
};

// Adds "spriteAnimation" to Phase::FixedUpdate in the flat Schedule or the SystemGraph.
void RegisterSpriteAnimationSystem(Ecs::Schedule& InSchedule);
void RegisterSpriteAnimationSystem(Ecs::SystemGraph& InGraph);
}

FBY_REFLECT_COMPONENT(Fby::SpriteAnimationComponent, "SpriteAnimationComponent", FBY_FIELD(Fby::SpriteAnimationComponent, TextureAtlas, UInt64),
                      FBY_FIELD(Fby::SpriteAnimationComponent, StartFrame, UInt32),
                      FBY_FIELD(Fby::SpriteAnimationComponent, FrameCount, UInt32),
                      FBY_FIELD(Fby::SpriteAnimationComponent, CurrentFrame, UInt32),
                      FBY_FIELD(Fby::SpriteAnimationComponent, FrameDuration, Float),
                      FBY_FIELD(Fby::SpriteAnimationComponent, ElapsedTime, Float),
                      FBY_FIELD(Fby::SpriteAnimationComponent, IsLooping, Bool), FBY_FIELD(Fby::SpriteAnimationComponent, IsPlaying, Bool))

FBY_REFLECT_COMPONENT(Fby::SpriteSheetComponent, "SpriteSheetComponent",
                      FBY_FIELD(Fby::SpriteSheetComponent, Columns, UInt32),
                      FBY_FIELD(Fby::SpriteSheetComponent, Rows, UInt32),
                      FBY_FIELD(Fby::SpriteSheetComponent, Layer, UInt32),
                      FBY_FIELD(Fby::SpriteSheetComponent, Flags, UInt32),
                      FBY_FIELD(Fby::SpriteSheetComponent, UVMin, Vector2D),
                      FBY_FIELD(Fby::SpriteSheetComponent, UVMax, Vector2D))
