// FemboyEngine2D - ECS bridge for the replication channel.
#pragma once

#include "Engine/ECS/Snapshot.hpp"
#include "Engine/Network/NetRollback.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace Fby::Net
{

// Captures the state an authority publishes as a baseline. `OutBytes` must be at
// least Ecs::SnapshotSize(registry, schema) bytes.
[[nodiscard]] bool CaptureReplicationBaseline(const Ecs::Registry& InRegistry, const Ecs::SnapshotSchema& InSchema,
                                              std::uint64_t InTick, const DeterministicRng& InRng,
                                              std::span<std::byte> OutState,
                                              std::size_t* OutBytes = nullptr) noexcept;

// Delta against the baseline the client is known to have.
[[nodiscard]] bool CaptureReplicationDelta(const Ecs::Registry& InRegistry, const Ecs::SnapshotSchema& InSchema,
                                           std::span<const std::byte> InBaseline, std::uint64_t InTick,
                                           const DeterministicRng& InRng, std::span<std::byte> OutDelta,
                                           std::size_t* OutBytes = nullptr) noexcept;

// Client side: a baseline recreates the world; a delta restores the baseline and
// applies the changed records.
[[nodiscard]] bool ApplyReplicationBaseline(Ecs::Registry& InRegistry, const Ecs::SnapshotSchema& InSchema,
                                            std::span<const std::byte> InState,
                                            DeterministicRng* OutRng = nullptr) noexcept;
[[nodiscard]] bool ApplyReplicationDelta(Ecs::Registry& InRegistry, const Ecs::SnapshotSchema& InSchema,
                                         std::span<const std::byte> InBaseline, std::span<const std::byte> InDelta,
                                         DeterministicRng* OutRng = nullptr) noexcept;

// Relevance filter: keeps only delta records whose entity is in the plan. The
// plan comes from NetRollbackSession::BuildRelevancePlan (the relevance grid
// lives inside the session, so this helper stays a thin wrapper).
// Incremental client update: keeps existing entity handles valid (no baseline
// restore), so gameplay references survive every replicated tick.
[[nodiscard]] bool ApplyReplicationDeltaInPlace(Ecs::Registry& InRegistry, const Ecs::SnapshotSchema& InSchema,
                                                std::span<const std::byte> InDelta) noexcept;

[[nodiscard]] bool FilterReplicationDelta(const NetRollbackSession& InSession, const RelevancePlan& InPlan,
                                          std::span<const std::byte> InDelta, std::span<std::byte> OutFiltered,
                                          std::size_t* OutBytes = nullptr) noexcept;
}
