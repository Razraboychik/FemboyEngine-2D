// FemboyEngine2D - persistent transient ring buffer.
#pragma once

#include "Engine/Core/Assert.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <type_traits>

namespace Fby::Rhi
{

class TransientRing
{
public:
    // `mapped` is persistent host-writable memory of at least
    TransientRing(std::byte* InMapped, std::size_t InSliceBytes, std::uint32_t InFramesInFlight) noexcept
        : MMapped(InMapped), MSliceBytes(InSliceBytes), MFrames(InFramesInFlight)
    {
        FBY_ASSERT(MMapped != nullptr || MSliceBytes == 0);
        FBY_ASSERT(MFrames > 0);
    }

    TransientRing(const TransientRing&) = delete;
    TransientRing& operator=(const TransientRing&) = delete;

    // Points the ring at the slice belonging to this frame and rewinds it.
    void BeginFrame(std::uint64_t InFrameIndex) noexcept
    {
        MCurrentSlice = static_cast<std::uint32_t>(InFrameIndex % MFrames);
        MOffset = 0;
        MHighWater = 0;
    }

    // Allocates `bytes` with `alignment` (power of two) inside the current
    [[nodiscard]] std::span<std::byte> Allocate(std::size_t InBytes, std::size_t InAlignment = 16) noexcept
    {
        FBY_ASSERT((InAlignment & (InAlignment - 1)) == 0);
        const std::size_t ScopedAligned = (MOffset + InAlignment - 1) & ~(InAlignment - 1);
        if (ScopedAligned + InBytes > MSliceBytes)
        {
            return {};
        }
        MOffset = ScopedAligned + InBytes;
        MHighWater = MOffset > MHighWater ? MOffset : MHighWater;
        return {MMapped + static_cast<std::size_t>(MCurrentSlice) * MSliceBytes + ScopedAligned, InBytes};
    }

    // Copies a POD range into the ring and hands back the GPU offset.
    template <typename T> [[nodiscard]] std::uint64_t Upload(std::span<const T> InValues) noexcept
    {
        static_assert(std::is_trivially_copyable_v<T>);
        const std::size_t ScopedBytes = InValues.size_bytes();
        const std::size_t ScopedAligned = (MOffset + 15) & ~static_cast<std::size_t>(15);
        if (ScopedAligned + ScopedBytes > MSliceBytes)
        {
            return kInvalidOffset;
        }
        std::byte* ScopedDestination = MMapped + static_cast<std::size_t>(MCurrentSlice) * MSliceBytes + ScopedAligned;
        if (ScopedBytes != 0)
        {
            // memcpy is the only correct tool here: the destination is GPU
            std::memcpy(ScopedDestination, InValues.data(), ScopedBytes);
        }
        MOffset = ScopedAligned + ScopedBytes;
        MHighWater = MOffset > MHighWater ? MOffset : MHighWater;
        return static_cast<std::uint64_t>(MCurrentSlice) * MSliceBytes + ScopedAligned;
    }

    [[nodiscard]] std::size_t SliceBytes() const noexcept
    {
        return MSliceBytes;
    }
    [[nodiscard]] std::size_t Used() const noexcept
    {
        return MOffset;
    }
    [[nodiscard]] std::size_t HighWater() const noexcept
    {
        return MHighWater;
    }
    [[nodiscard]] std::uint32_t FramesInFlight() const noexcept
    {
        return MFrames;
    }

    static constexpr std::uint64_t kInvalidOffset = ~0ull;

private:
    std::byte* MMapped = nullptr;
    std::size_t MSliceBytes = 0;
    std::uint32_t MFrames = 1;
    std::uint32_t MCurrentSlice = 0;
    std::size_t MOffset = 0;
    std::size_t MHighWater = 0;
};
}
