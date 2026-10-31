// FemboyEngine2D - little-endian helpers for the fixed network records.
#pragma once

#include <cstdint>

namespace Fby::Net::Detail
{

constexpr void StoreU16(std::uint8_t* OutData, std::uint16_t InValue) noexcept
{
    OutData[0] = static_cast<std::uint8_t>(InValue);
    OutData[1] = static_cast<std::uint8_t>(InValue >> 8);
}

constexpr void StoreU32(std::uint8_t* OutData, std::uint32_t InValue) noexcept
{
    OutData[0] = static_cast<std::uint8_t>(InValue);
    OutData[1] = static_cast<std::uint8_t>(InValue >> 8);
    OutData[2] = static_cast<std::uint8_t>(InValue >> 16);
    OutData[3] = static_cast<std::uint8_t>(InValue >> 24);
}

constexpr void StoreU64(std::uint8_t* OutData, std::uint64_t InValue) noexcept
{
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < 8; ++ScopedIndex)
    {
        OutData[ScopedIndex] = static_cast<std::uint8_t>(InValue >> (ScopedIndex * 8u));
    }
}

[[nodiscard]] constexpr std::uint16_t LoadU16(const std::uint8_t* InData) noexcept
{
    return static_cast<std::uint16_t>(InData[0] | (static_cast<std::uint16_t>(InData[1]) << 8));
}

[[nodiscard]] constexpr std::uint32_t LoadU32(const std::uint8_t* InData) noexcept
{
    return static_cast<std::uint32_t>(InData[0]) | (static_cast<std::uint32_t>(InData[1]) << 8) |
           (static_cast<std::uint32_t>(InData[2]) << 16) | (static_cast<std::uint32_t>(InData[3]) << 24);
}

[[nodiscard]] constexpr std::uint64_t LoadU64(const std::uint8_t* InData) noexcept
{
    std::uint64_t ScopedValue = 0;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < 8; ++ScopedIndex)
    {
        ScopedValue |= static_cast<std::uint64_t>(InData[ScopedIndex]) << (ScopedIndex * 8u);
    }
    return ScopedValue;
}
}
