// FemboyEngine2D - 32-bit wire entity handle.
#pragma once

#include <cstdint>

namespace Fby
{

// Packed as 20 index bits and 12 generation bits. The value is stable across
// the ECS storage backend and is what scenes, snapshots and plugins exchange.
using Entity = std::uint32_t;

inline constexpr Entity kNullEntity = 0xFFFFFFFFu;

[[nodiscard]] constexpr std::uint32_t ToIntegral(Entity InEntity) noexcept
{
    return InEntity;
}

[[nodiscard]] constexpr Entity FromIntegral(std::uint32_t InValue) noexcept
{
    return InValue;
}

}
