// FemboyEngine2D - public API and data-format version contract.
//
// A breaking change to a stable C ABI, a serialized scene or a snapshot bumps
// the matching constant and the changelog. The values here are the ones a host,
// plugin or mod probes at load time; internal headers are not part of this
// contract.
#pragma once

#include <cstdint>

namespace Fby
{

// Stable host API surface used by editor extensions and embedders.
inline constexpr std::uint32_t kEngineApiVersion = 1;

// Safe data ABI in Engine/Modding/ModAPI.h. Kept in sync by a compile-time
// assertion in the API contract test.
inline constexpr std::uint32_t kModAbiVersion = 1;

// On-disk scene format in Engine/ECS/Scene.hpp.
inline constexpr std::uint32_t kSceneFormatVersion = 2;

// ECS snapshot format in Engine/ECS/Snapshot.hpp.
inline constexpr std::uint32_t kSnapshotFormatVersion = 5;

}

// Marks a public declaration that remains available for one minor line while
// callers migrate. The message must name the replacement.
#define FBY_DEPRECATED(Message) [[deprecated(Message)]]
