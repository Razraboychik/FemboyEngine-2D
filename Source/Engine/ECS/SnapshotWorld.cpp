#include "Engine/ECS/SnapshotWorld.hpp"

#include <cstring>
#include <string_view>

namespace Fby::Ecs
{

bool EncodeWorldSnapshot(const WorldSnapshotV3& InState, std::span<std::byte> InDestination,
                         std::size_t* OutBytes) noexcept
{
    if (InDestination.size() < sizeof(WorldSnapshotV3) || InState.version != kWorldSnapshotVersion ||
        InState.chainCount > InState.chains.size() || InState.clothCount > InState.cloths.size() ||
        InState.emitterCount > InState.emitters.size() || InState.fluid.columns > kMaxWaterColumns ||
        InState.fluid.rows > kMaxWaterRows)
    {
        return false;
    }
    std::memcpy(InDestination.data(), &InState, sizeof(InState));
    if (OutBytes != nullptr)
    {
        *OutBytes = sizeof(InState);
    }
    return true;
}

bool DecodeWorldSnapshot(std::span<const std::byte> InBytes, WorldSnapshotV3& InState,
                         std::string_view* InError) noexcept
{
    if (InBytes.size() != sizeof(WorldSnapshotV3))
    {
        if (InError != nullptr)
        {
            *InError = "world extension size mismatch";
        }
        return false;
    }
    WorldSnapshotV3 ScopedCandidate{};
    std::memcpy(&ScopedCandidate, InBytes.data(), sizeof(ScopedCandidate));
    if (ScopedCandidate.version != kWorldSnapshotVersion)
    {
        if (InError != nullptr)
        {
            *InError = "unsupported world extension version";
        }
        return false;
    }
    if (ScopedCandidate.chainCount > ScopedCandidate.chains.size() ||
        ScopedCandidate.clothCount > ScopedCandidate.cloths.size() ||
        ScopedCandidate.emitterCount > ScopedCandidate.emitters.size() ||
        ScopedCandidate.fluid.columns > kMaxWaterColumns || ScopedCandidate.fluid.rows > kMaxWaterRows)
    {
        if (InError != nullptr)
        {
            *InError = "world extension count exceeds capacity";
        }
        return false;
    }
    InState = ScopedCandidate;
    return true;
}
}
