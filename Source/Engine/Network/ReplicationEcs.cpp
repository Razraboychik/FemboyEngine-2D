// FemboyEngine2D - ECS bridge for the replication channel implementation.
#include "Engine/Network/ReplicationEcs.hpp"

namespace Fby::Net
{

bool CaptureReplicationBaseline(const Ecs::Registry& InRegistry, const Ecs::SnapshotSchema& InSchema,
                                std::uint64_t InTick, const DeterministicRng& InRng, std::span<std::byte> OutState,
                                std::size_t* OutBytes) noexcept
{
    return Ecs::CaptureSnapshot(InRegistry, InSchema, OutState, InTick, InRng, OutBytes);
}

bool CaptureReplicationDelta(const Ecs::Registry& InRegistry, const Ecs::SnapshotSchema& InSchema,
                             std::span<const std::byte> InBaseline, std::uint64_t InTick,
                             const DeterministicRng& InRng, std::span<std::byte> OutDelta,
                             std::size_t* OutBytes) noexcept
{
    return Ecs::CaptureDelta(InRegistry, InSchema, InBaseline, OutDelta, InTick, InRng, OutBytes);
}

bool ApplyReplicationBaseline(Ecs::Registry& InRegistry, const Ecs::SnapshotSchema& InSchema,
                              std::span<const std::byte> InState, DeterministicRng* OutRng) noexcept
{
    return Ecs::RestoreSnapshot(InRegistry, InSchema, InState, OutRng);
}

bool ApplyReplicationDelta(Ecs::Registry& InRegistry, const Ecs::SnapshotSchema& InSchema,
                           std::span<const std::byte> InBaseline, std::span<const std::byte> InDelta,
                           DeterministicRng* OutRng) noexcept
{
    return Ecs::RestoreDelta(InRegistry, InSchema, InBaseline, InDelta, OutRng);
}

bool ApplyReplicationDeltaInPlace(Ecs::Registry& InRegistry, const Ecs::SnapshotSchema& InSchema,
                                  std::span<const std::byte> InDelta) noexcept
{
    return Ecs::ApplyDelta(InRegistry, InSchema, InDelta);
}

bool FilterReplicationDelta(const NetRollbackSession& InSession, const RelevancePlan& InPlan,
                            std::span<const std::byte> InDelta, std::span<std::byte> OutFiltered,
                            std::size_t* OutBytes) noexcept
{
    return InSession.FilterDeltaForPlayer(InPlan, InDelta, OutFiltered, OutBytes) ==
           NetRollbackSession::Result::Ok;
}
}
