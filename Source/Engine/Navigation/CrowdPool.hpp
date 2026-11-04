// FemboyEngine2D - data-oriented crowd pool: SoA agents, spatial hash, boids.
#pragma once

#include "Engine/Core/Types.hpp"
#include "Engine/Navigation/FlowField.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace Fby::Navigation
{

// One simulated agent. The pool stores these in a flat array; entities reference
// them by index, the same contract ClothComponent and ParticleEmitterComponent use.
struct CrowdAgent
{
    Vector2D Position{};
    Vector2D Velocity{};
    Vector2D Target{};
    std::uint32_t Group = 0;
    float MaxSpeed = 80.0f;
    float WanderPhase = 0.0f;
    std::uint32_t Flags = 0;
};

struct CrowdSettings
{
    float PerceptionRadius = 24.0f;
    float SeparationWeight = 1.6f;
    float CohesionWeight = 0.2f;
    float AlignmentWeight = 0.3f;
    float TargetWeight = 1.0f;
    float Acceleration = 240.0f;
    // Hard cap on neighbours per agent; this is what keeps the cost predictable.
    std::uint32_t MaxNeighbours = 16;
    // Optional flow field the agents follow instead of steering straight at the
    // target. The mapping turns an agent position into a field cell.
    const FlowField* Field = nullptr;
    Vector2D FieldOrigin{};
    float FieldCellSize = 1.0f;
};

// Fixed-capacity crowd. Step() rebuilds the grid, then updates every agent from
// the previous state, so the result does not depend on thread scheduling.
class CrowdPool
{
public:
    inline static constexpr std::uint32_t kMaxAgents = 131072;

    [[nodiscard]] bool Configure(std::uint32_t InCapacity, float InCellSize = 0.0f) noexcept;
    void Clear() noexcept;
    [[nodiscard]] bool IsConfigured() const noexcept
    {
        return !MAgents.empty();
    }
    [[nodiscard]] std::uint32_t Count() const noexcept
    {
        return MCount;
    }
    [[nodiscard]] std::uint32_t Capacity() const noexcept
    {
        return static_cast<std::uint32_t>(MAgents.size());
    }

    // Returns the agent index, or 0xFFFFFFFF when the pool is full.
    [[nodiscard]] std::uint32_t Add(Vector2D InPosition, std::uint32_t InGroup, Vector2D InTarget,
                                    float InMaxSpeed = 80.0f) noexcept;
    [[nodiscard]] CrowdAgent* At(std::uint32_t InIndex) noexcept;
    [[nodiscard]] const CrowdAgent* At(std::uint32_t InIndex) const noexcept;
    void SetTarget(std::uint32_t InIndex, Vector2D InTarget) noexcept;
    // Updates every agent of the group and returns how many were touched. This is
    // the goal-group schedule: one order moves the whole squad.
    [[nodiscard]] std::uint32_t SetGroupTarget(std::uint32_t InGroup, Vector2D InTarget) noexcept;
    [[nodiscard]] std::span<CrowdAgent> Agents() noexcept
    {
        return {MAgents.data(), MCount};
    }

    void Step(float InDt, const CrowdSettings& InSettings) noexcept;

    // Number of neighbours the last Step looked at for the given agent. Tests use
    // it to prove the cap holds.
    [[nodiscard]] std::uint32_t NeighbourCount(std::uint32_t InIndex) const noexcept;

private:
    void RebuildGrid() noexcept;
    [[nodiscard]] std::uint32_t CellOf(std::uint32_t InX, std::uint32_t InY) const noexcept
    {
        return InY * MGridWidth + InX;
    }

    std::vector<CrowdAgent> MAgents;
    std::vector<std::uint32_t> MCellStart;
    std::vector<std::uint32_t> MCellCount;
    std::vector<std::uint32_t> MCellCursor;
    std::vector<std::uint32_t> MSorted;
    std::vector<std::uint32_t> MNeighbourCounts;
    std::vector<Vector2D> MSteer;
    std::uint32_t MCount = 0;
    std::uint32_t MGridWidth = 0;
    std::uint32_t MGridHeight = 0;
    float MCellSize = 24.0f;
    Vector2D MGridOrigin{};
};

}
