// FemboyEngine2D - frame-linear allocator.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

namespace Fby
{

class FrameArena
{
public:
    FrameArena() = default;
    explicit FrameArena(std::span<std::byte> InStorage) noexcept : MBase(InStorage.data()), MCapacity(InStorage.size())
    {
    }

    void Initialize(std::span<std::byte> InStorage) noexcept
    {
        MBase = InStorage.data();
        MCapacity = InStorage.size();
        MOffset = 0;
    }

    void Reset() noexcept
    {
        MOffset = 0;
    }

    [[nodiscard]] void* Allocate(std::size_t InSize, std::size_t InAlignment = alignof(std::max_align_t)) noexcept
    {
        if (InSize == 0 || InAlignment == 0 || MBase == nullptr)
        {
            return nullptr;
        }
        const std::size_t ScopedAligned = (MOffset + InAlignment - 1u) & ~(InAlignment - 1u);
        if (ScopedAligned > MCapacity || InSize > MCapacity - ScopedAligned)
        {
            return nullptr;
        }
        void* ScopedResult = MBase + ScopedAligned;
        MOffset = ScopedAligned + InSize;
        return ScopedResult;
    }

    template <typename T> [[nodiscard]] T* AllocateArray(std::size_t InCount) noexcept
    {
        static_assert(std::is_trivially_copyable_v<T>, "FrameArena only stores POD payloads");
        if (InCount > static_cast<std::size_t>(-1) / sizeof(T))
        {
            return nullptr;
        }
        return static_cast<T*>(Allocate(sizeof(T) * InCount, alignof(T)));
    }

    [[nodiscard]] std::size_t Used() const noexcept
    {
        return MOffset;
    }
    [[nodiscard]] std::size_t Capacity() const noexcept
    {
        return MCapacity;
    }
    [[nodiscard]] bool Empty() const noexcept
    {
        return MOffset == 0;
    }

private:
    std::byte* MBase = nullptr;
    std::size_t MCapacity = 0;
    std::size_t MOffset = 0;
};
}
