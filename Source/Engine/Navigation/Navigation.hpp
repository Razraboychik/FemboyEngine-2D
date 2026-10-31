// FemboyEngine2D - grid navigation: cost maps, A* and agent steering.
#pragma once

#include "Engine/Core/Grid.hpp"
#include "Engine/Core/Mirror.hpp"
#include "Engine/Core/Types.hpp"
#include "Engine/ECS/Schedule.hpp"
#include "Engine/ECS/Tilemap.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <type_traits>

namespace Fby::Navigation
{

inline constexpr std::uint32_t kNavMaxWidth = 128;
inline constexpr std::uint32_t kNavMaxHeight = 128;
inline constexpr std::uint32_t kNavMaxCells = kNavMaxWidth * kNavMaxHeight;
inline constexpr std::uint32_t kMaxPathCells = 128;

// Cost 0 blocks a cell; 1 is normal ground; higher values are slow terrain. The
// map is a plain fixed array so a path query never allocates.
struct NavMap
{
    Grid::GridDesc Grid{};
    std::uint32_t Width = 0;
    std::uint32_t Height = 0;
    std::array<std::uint8_t, kNavMaxCells> Cost{};

    [[nodiscard]] bool Contains(Grid::Cell InCell) const noexcept
    {
        return InCell.X >= 0 && InCell.Y >= 0 && static_cast<std::uint32_t>(InCell.X) < Width &&
               static_cast<std::uint32_t>(InCell.Y) < Height;
    }
    [[nodiscard]] std::uint8_t CostAt(Grid::Cell InCell) const noexcept
    {
        if (!Contains(InCell))
        {
            return 0;
        }
        return Cost[static_cast<std::size_t>(InCell.Y) * Width + static_cast<std::size_t>(InCell.X)];
    }
    [[nodiscard]] bool SetCost(Grid::Cell InCell, std::uint8_t InCost) noexcept
    {
        if (!Contains(InCell))
        {
            return false;
        }
        Cost[static_cast<std::size_t>(InCell.Y) * Width + static_cast<std::size_t>(InCell.X)] = InCost;
        return true;
    }
    [[nodiscard]] bool Passable(Grid::Cell InCell) const noexcept
    {
        return CostAt(InCell) > 0;
    }
};

static_assert(std::is_trivially_copyable_v<NavMap>);

void ClearMap(NavMap& InMap, std::uint32_t InWidth, std::uint32_t InHeight, const Grid::GridDesc& InGrid) noexcept;

// Copies solid tiles as blocked cells and visible floor as cost 1.
[[nodiscard]] bool BuildFromTilemap(const TilemapComponent& InTilemap, NavMap* OutMap) noexcept;

struct NavPath
{
    std::array<Grid::Cell, kMaxPathCells> Cells{};
    std::uint16_t Count = 0;
    bool Truncated = false;
    std::uint32_t QueryId = 0;

    [[nodiscard]] bool Empty() const noexcept
    {
        return Count == 0;
    }
};

struct PathStats
{
    std::uint32_t Expanded = 0;
    std::uint32_t Pushed = 0;
    std::uint32_t Queries = 0;
    std::uint32_t Failed = 0;
    std::uint32_t MaxExpanded = 0;
};

// A* over a NavMap. One instance owns the scratch memory (about 200 KiB) and
// must not be shared between threads.
class PathFinder
{
public:
    [[nodiscard]] bool Find(const NavMap& InMap, Grid::Cell InStart, Grid::Cell InGoal, NavPath* OutPath,
                            bool InAllowPartial = false) noexcept;

    // Path with up to `InMaxCells` cells; the rest is dropped and Truncated set.
    [[nodiscard]] bool FindLimited(const NavMap& InMap, Grid::Cell InStart, Grid::Cell InGoal,
                                   std::uint16_t InMaxCells, NavPath* OutPath) noexcept;

    [[nodiscard]] const PathStats& Stats() const noexcept
    {
        return MStats;
    }
    void ResetStats() noexcept
    {
        MStats = PathStats{};
    }

private:
    [[nodiscard]] std::uint32_t NodeIndex(Grid::Cell InCell) const noexcept
    {
        return static_cast<std::uint32_t>(InCell.Y) * MWidth + static_cast<std::uint32_t>(InCell.X);
    }
    [[nodiscard]] Grid::Cell NodeCell(std::uint32_t InIndex) const noexcept
    {
        return Grid::Cell{static_cast<std::int32_t>(InIndex % MWidth), static_cast<std::int32_t>(InIndex / MWidth)};
    }
    void ClearScratch(std::uint32_t InCellCount) noexcept;
    void Push(std::uint32_t InNode) noexcept;
    [[nodiscard]] std::uint32_t Pop() noexcept;
    void UpdateOpen(std::uint32_t InNode) noexcept;

    std::array<float, kNavMaxCells> MGScore{};
    std::array<float, kNavMaxCells> MFScore{};
    std::array<std::uint32_t, kNavMaxCells> MParent{};
    std::array<std::uint32_t, kNavMaxCells> MHeap{};
    std::array<std::uint8_t, kNavMaxCells> MClosed{};
    std::array<std::uint8_t, kNavMaxCells> MInOpen{};
    std::uint32_t MHeapSize = 0;
    std::uint32_t MWidth = 0;
    std::uint32_t MHeight = 0;
    PathStats MStats{};
};

// Authored agent parameters. This is the component the scene serializes and the
// editor shows; it contains no path state, so re-saving a scene never snapshots
// transient pathfinding data.
struct NavAgentSettings
{
    // World-space destination. The system repathes when it moves further than
    // `RepathDistance` or when `RepathInterval` elapses.
    Vector2D Target{};
    float Speed = 96.0f;
    // Stop distance to the destination.
    float ArrivalRadius = 4.0f;
    float RepathInterval = 0.5f;
    // Repath when the target moved further than this.
    float RepathDistance = 8.0f;
    // KNavAgentFlagActive and future authoring switches.
    std::uint32_t Flags = 0;
    std::uint32_t Reserved = 0;
};

static_assert(std::is_trivially_copyable_v<NavAgentSettings>);
static_assert(std::is_standard_layout_v<NavAgentSettings>);
static_assert(sizeof(NavAgentSettings) == 32);

// Runtime path state. Fixed capacity, no allocation; owned by the system's
// Update and deliberately not reflected into the scene schema.
struct NavAgentComponent
{
    NavPath Path{};
    Vector2D LastTarget{};
    std::uint16_t PathIndex = 0;
    // Accumulator towards the next repath, in seconds.
    float RepathTimer = 0.0f;
    // KNavAgentFlagArrived / KNavAgentFlagBlocked, plus the per-frame repath
    // deferral bit.
    std::uint32_t Flags = 0;
};

static_assert(std::is_trivially_copyable_v<NavAgentComponent>);

inline constexpr std::uint32_t kNavAgentFlagActive = 1u << 0;
inline constexpr std::uint32_t kNavAgentFlagArrived = 1u << 1;
inline constexpr std::uint32_t kNavAgentFlagBlocked = 1u << 2;
// Set by the system when the per-frame repath budget postponed this agent.
inline constexpr std::uint32_t kNavAgentFlagRepathDeferred = 1u << 3;

// Moves agents along their path. Requires Transform2D, NavAgentSettings and
// NavAgentComponent on the agent entity; the system creates the runtime
// component for a settings-only entity before iterating.
class NavigationSystem
{
public:
    void SetMap(const NavMap* InMap) noexcept
    {
        MMap = InMap;
    }
    [[nodiscard]] const NavMap* Map() const noexcept
    {
        return MMap;
    }
    [[nodiscard]] PathFinder& Finder() noexcept
    {
        return MFinder;
    }

    // FixedUpdate. Writes Transform2D::Position of every agent.
    void Update(Ecs::Registry& InRegistry, float InDeltaSeconds) noexcept;

    // Per-frame cap on A* solves. Agents that miss the budget keep their current
    // path and are marked with kNavAgentFlagRepathDeferred, so one frame with a
    // crowd of new targets cannot spike the fixed step.
    std::uint32_t MaxRepathsPerFrame = 8;
    [[nodiscard]] std::uint32_t RepathsThisFrame() const noexcept
    {
        return MRepathsThisFrame;
    }

    [[nodiscard]] static bool Register(Ecs::SystemGraph& InGraph, NavigationSystem& InSystem);

private:
    static void Tick(Ecs::SystemContext& InContext, void* InUser);
    // Structural pass: gives every settings-only agent a runtime state component.
    void EnsureRuntimeState(Ecs::Registry& InRegistry) noexcept;

    const NavMap* MMap = nullptr;
    PathFinder MFinder;
    std::uint32_t MRepathsThisFrame = 0;
};

// Follows `InPath` from world position `InPosition` and returns the new
// position. `OutIndex` advances as cells are consumed.
[[nodiscard]] Vector2D FollowPath(const NavMap& InMap, const NavPath& InPath, std::uint16_t* OutIndex,
                                  Vector2D InPosition, float InSpeed, float InDeltaSeconds, bool* OutArrived) noexcept;
}

FBY_REFLECT_COMPONENT(Fby::Navigation::NavAgentSettings, "NavAgentSettings",
                      FBY_FIELD(Fby::Navigation::NavAgentSettings, Target, Vector2D),
                      FBY_FIELD(Fby::Navigation::NavAgentSettings, Speed, Float),
                      FBY_FIELD(Fby::Navigation::NavAgentSettings, ArrivalRadius, Float),
                      FBY_FIELD(Fby::Navigation::NavAgentSettings, RepathInterval, Float),
                      FBY_FIELD(Fby::Navigation::NavAgentSettings, RepathDistance, Float),
                      FBY_FIELD(Fby::Navigation::NavAgentSettings, Flags, UInt32))
