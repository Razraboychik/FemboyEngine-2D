// FemboyEngine2D - opt-in transform hierarchy.
#pragma once

#include "Engine/ECS/Components.hpp"
#include "Engine/ECS/Schedule.hpp"

#include <cstdint>

namespace Fby::Ecs
{

// Composes LocalTransform2D through Parent2D into Transform2D. Only entities
// that own LocalTransform2D are touched, so scenes that use plain Transform2D
// are unchanged. Returns false on a cycle or a missing parent; the affected
// entities keep their previous world transform.
[[nodiscard]] bool UpdateHierarchy(Registry& InRegistry) noexcept;

// Maximum parent chain the pass follows; deeper chains are reported as broken.
inline constexpr std::uint32_t kMaxHierarchyDepth = 64;
}
