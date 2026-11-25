// FemboyEngine2D - stable, serializable subsystem identities.
#pragma once

#include "Engine/Core/Types.hpp"

#include <array>
#include <cstdint>

namespace Fby
{

struct ChainIdTag;
struct ClothIdTag;
struct EmitterIdTag;

using ChainId = Handle<ChainIdTag>;
using ClothId = Handle<ClothIdTag>;
using EmitterId = Handle<EmitterIdTag>;

static_assert(sizeof(ChainId) == 4);
static_assert(sizeof(ClothId) == 4);
static_assert(sizeof(EmitterId) == 4);

// A compact stable-id allocator. It has no free list and no allocation: slot
template <typename Id> class StableIdAllocator
{
public:
    [[nodiscard]] Id Allocate() noexcept
    {
        for (std::uint32_t ScopedAttempt = 0; ScopedAttempt < kCapacity; ++ScopedAttempt)
        {
            const std::uint32_t ScopedSlot = MNext++ % kCapacity;
            if (!MUsed[ScopedSlot])
            {
                MUsed[ScopedSlot] = true;
                ++MGeneration[ScopedSlot];
                return Id{Encode(ScopedSlot, MGeneration[ScopedSlot])};
            }
        }
        // All slots live: caller must destroy an object first
        return Id{};
    }

    void Release(Id InId) noexcept
    {
        const std::uint32_t ScopedSlot = SlotOf(InId.id);
        const std::uint32_t ScopedGeneration = GenerationOf(InId.id);
        if (ScopedSlot < kCapacity && MUsed[ScopedSlot] && MGeneration[ScopedSlot] == ScopedGeneration)
        {
            MUsed[ScopedSlot] = false;
            ++MGeneration[ScopedSlot];
        }
    }
    [[nodiscard]] bool IsLive(Id InId) const noexcept
    {
        const std::uint32_t ScopedSlot = SlotOf(InId.id);
        return ScopedSlot < kCapacity && MUsed[ScopedSlot] &&
               MGeneration[ScopedSlot] == GenerationOf(InId.id);
    }

private:
    static constexpr std::uint32_t kCapacity = 64;
    static constexpr std::uint32_t kSlotBits = 6;
    static constexpr std::uint32_t kSlotMask = kCapacity - 1;
    static constexpr std::uint32_t SlotOf(std::uint32_t InId) noexcept
    {
        return InId & kSlotMask;
    }
    static constexpr std::uint32_t GenerationOf(std::uint32_t InId) noexcept
    {
        return InId >> kSlotBits;
    }
    static constexpr std::uint32_t Encode(std::uint32_t InSlot, std::uint32_t InGeneration) noexcept
    {
        return ((InGeneration & 0x03FFFFFFu) << kSlotBits) | InSlot;
    }
    std::array<bool, kCapacity> MUsed{};
    std::array<std::uint32_t, kCapacity> MGeneration{};
    std::uint32_t MNext = 0;
};
}
