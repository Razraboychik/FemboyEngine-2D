#include "Engine/ECS/SnapshotWorld.hpp"

#include <algorithm>

#include <cstring>
#include <string_view>

namespace Fby::Ecs
{

bool EncodeWorldSnapshot(const WorldSnapshotV3& InState, std::span<std::byte> InDestination,
                         std::size_t* OutBytes) noexcept
{
    if (InDestination.size() != sizeof(WorldSnapshotV3) || InState.version != kWorldSnapshotVersion ||
        InState.chainCount > InState.chains.size() || InState.clothCount > InState.cloths.size() ||
        InState.emitterCount > InState.emitters.size() || InState.fluid.columns > kMaxWaterColumns ||
        InState.fluid.rows > kMaxWaterRows)
    {
        return false;
    }
    // Start from a value-initialised object so padding and unused slots are
    // canonical; assignment then copies the live members without stale bytes.
    WorldSnapshotV3 ScopedStaged{};
    ScopedStaged = InState;
    std::fill(ScopedStaged.chains.begin() + ScopedStaged.chainCount, ScopedStaged.chains.end(), ChainSaveV3{});
    std::fill(ScopedStaged.cloths.begin() + ScopedStaged.clothCount, ScopedStaged.cloths.end(), ClothSaveV3{});
    std::fill(ScopedStaged.emitters.begin() + ScopedStaged.emitterCount, ScopedStaged.emitters.end(),
              EmitterSaveV3{});
    const std::size_t ScopedFluidCells =
        static_cast<std::size_t>(ScopedStaged.fluid.desc.columns) * ScopedStaged.fluid.desc.rows;
    for (std::size_t ScopedIndex = ScopedFluidCells; ScopedIndex < ScopedStaged.fluid.height.size(); ++ScopedIndex)
    {
        ScopedStaged.fluid.height[ScopedIndex] = 0.0f;
    }
    std::memcpy(InDestination.data(), &ScopedStaged, sizeof(ScopedStaged));
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
