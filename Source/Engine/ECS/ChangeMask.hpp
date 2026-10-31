// FemboyEngine2D - frame-local component change masks.
#pragma once

#include "Engine/Core/Definitions.hpp"
#include "Engine/Core/Mirror.hpp"
#include "Engine/Core/Types.hpp"
#include "Engine/ECS/Schedule.hpp"

#include <atomic>
#include <cstdint>
#include <type_traits>

namespace Fby
{

// The public spelling used by gameplay code and by the extract. A typedef -
using ChangeMask = std::uint64_t;

enum class ChangeMaskBit : ChangeMask
{
    None = 0,
    Transform2D = 1ull << 0,
    Sprite2D = 1ull << 1,
    DrawOrder = 1ull << 2,
    Velocity = 1ull << 3,
    GameplayState = 1ull << 4,
    Animation = 1ull << 5,
    Audio = 1ull << 6,
    Physics = 1ull << 7,
    Lifetime = 1ull << 8,
    Custom = 1ull << 9,
    // Bits 16..63 are reserved for game-side aliases. The low 16 bits are the
    GameBitBase = 1ull << 16,
};

[[nodiscard]] constexpr ChangeMask ChangeMaskOf(ChangeMaskBit InBit) noexcept
{
    return static_cast<ChangeMask>(InBit);
}

[[nodiscard]] constexpr ChangeMask operator|(ChangeMaskBit InLeft, ChangeMaskBit InRight) noexcept
{
    return ChangeMaskOf(InLeft) | ChangeMaskOf(InRight);
}

[[nodiscard]] constexpr ChangeMask operator|(ChangeMask InLeft, ChangeMaskBit InRight) noexcept
{
    return InLeft | ChangeMaskOf(InRight);
}

// Stored in the registry next to Transform2D/Sprite2D. `Bits` is the only
struct ComponentChangeMask
{
    ChangeMask Bits = 0;
    std::uint32_t LastFrame = 0;
    std::uint32_t Reserved = 0;

    [[nodiscard]] constexpr bool Empty() const noexcept
    {
        return Bits == 0;
    }
    [[nodiscard]] constexpr bool Has(ChangeMaskBit InBit) const noexcept
    {
        return (Bits & ChangeMaskOf(InBit)) != 0;
    }
    constexpr void Set(ChangeMaskBit InBit) noexcept
    {
        Bits |= ChangeMaskOf(InBit);
    }
    constexpr void Clear(ChangeMaskBit InBit) noexcept
    {
        Bits &= ~ChangeMaskOf(InBit);
    }
};

static_assert(std::is_trivially_copyable_v<ComponentChangeMask>);
static_assert(std::is_standard_layout_v<ComponentChangeMask>);
static_assert(sizeof(ComponentChangeMask) == 16);
static_assert(alignof(ComponentChangeMask) == 8);

namespace Change
{

// Atomic mark used by systems that can run in parallel over disjoint entities.
FORCEINLINE void Mark(ComponentChangeMask& InMask, ChangeMask InBits, std::uint32_t InFrame = 0) noexcept
{
    std::atomic_ref<ChangeMask> ScopedBits(InMask.Bits);
    ScopedBits.fetch_or(InBits, std::memory_order_release);
    // LastFrame is diagnostic only. A plain store is enough; a reader that
    InMask.LastFrame = InFrame;
}

FORCEINLINE void Mark(ComponentChangeMask& InMask, ChangeMaskBit InBit, std::uint32_t InFrame = 0) noexcept
{
    Mark(InMask, ChangeMaskOf(InBit), InFrame);
}

[[nodiscard]] FORCEINLINE ChangeMask Consume(ComponentChangeMask& InMask) noexcept
{
    std::atomic_ref<ChangeMask> ScopedBits(InMask.Bits);
    return ScopedBits.fetch_and(static_cast<ChangeMask>(0), std::memory_order_acquire);
}

[[nodiscard]] FORCEINLINE ChangeMask Peek(const ComponentChangeMask& InMask) noexcept
{
    // atomic_ref has no const-qualified specialization on every standard
    std::atomic_ref<ChangeMask> ScopedBits(const_cast<ChangeMask&>(InMask.Bits));
    return ScopedBits.load(std::memory_order_acquire);
}

// Ensures the entity has a mask and marks the bits in one call. The component is
FORCEINLINE void Mark(Ecs::Registry& InRegistry, Entity InEntity, ChangeMask InBits, std::uint32_t InFrame = 0) noexcept
{
    ComponentChangeMask* ScopedMask = InRegistry.try_get<ComponentChangeMask>(InEntity);
    if (ScopedMask == nullptr)
    {
        ScopedMask = &InRegistry.emplace<ComponentChangeMask>(InEntity);
    }
    Mark(*ScopedMask, InBits, InFrame);
}

FORCEINLINE void Mark(Ecs::Registry& InRegistry, Entity InEntity, ChangeMaskBit InBit,
                      std::uint32_t InFrame = 0) noexcept
{
    Mark(InRegistry, InEntity, ChangeMaskOf(InBit), InFrame);
}

// System-facing convenience wrappers. A system that writes Transform2D calls
FORCEINLINE void MarkTransform(Ecs::Registry& InRegistry, Entity InEntity, std::uint32_t InFrame = 0) noexcept
{
    Mark(InRegistry, InEntity, ChangeMaskBit::Transform2D, InFrame);
}

FORCEINLINE void MarkSprite(Ecs::Registry& InRegistry, Entity InEntity, std::uint32_t InFrame = 0) noexcept
{
    Mark(InRegistry, InEntity, ChangeMaskBit::Sprite2D, InFrame);
}

FORCEINLINE void MarkDrawOrder(Ecs::Registry& InRegistry, Entity InEntity, std::uint32_t InFrame = 0) noexcept
{
    Mark(InRegistry, InEntity, ChangeMaskBit::DrawOrder, InFrame);
}

// Attaches an empty mask. Spawners call this once; an entity without a mask is
FORCEINLINE ComponentChangeMask& Ensure(Ecs::Registry& InRegistry, Entity InEntity) noexcept
{
    ComponentChangeMask* ScopedMask = InRegistry.try_get<ComponentChangeMask>(InEntity);
    return ScopedMask != nullptr ? *ScopedMask : InRegistry.emplace<ComponentChangeMask>(InEntity);
}

// Clears every mask in the registry. Used by a full render extract rebuild or by
inline void ClearAll(Ecs::Registry& InRegistry) noexcept
{
    auto ScopedView = InRegistry.view<ComponentChangeMask>();
    for (auto [ScopedEntity, ScopedMask] : ScopedView.each())
    {
        (void)ScopedEntity;
        ScopedMask.Bits = 0;
    }
}
}
}

// Mirror registration: the editor inspector can show the mask and the layout
FBY_REFLECT_COMPONENT(Fby::ComponentChangeMask, "ComponentChangeMask",
                      FBY_FIELD(Fby::ComponentChangeMask, Bits, UInt64),
                      FBY_FIELD(Fby::ComponentChangeMask, LastFrame, UInt32))
