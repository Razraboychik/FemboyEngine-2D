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
                return Id{ScopedSlot};
            }
        }
        // All slots live: caller must destroy an object first
        return Id{};
    }

    void Release(Id InId) noexcept
    {
        if (InId.id < kCapacity)
        {
            MUsed[InId.id] = false;
        }
    }
    [[nodiscard]] bool IsLive(Id InId) const noexcept
    {
        return InId.id < kCapacity && MUsed[InId.id];
    }

private:
    static constexpr std::uint32_t kCapacity = 64;
    std::array<bool, kCapacity> MUsed{};
    std::uint32_t MNext = 0;
};
}
