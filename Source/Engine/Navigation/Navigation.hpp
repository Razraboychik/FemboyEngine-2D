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

class NavMesh;

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

// How a raw A* cell polyline becomes a movement path. None is the shortest
// cell polyline; LineOfSight removes redundant corners; Chaikin and CatmullRom
// round the remaining corners with increasing strength.
enum class NavSmoothingMode : std::uint8_t
{
    None = 0,
    LineOfSight = 1,
    Chaikin = 2,
    CatmullRom = 3,
};

struct NavPathOptions
{
    NavSmoothingMode Smoothing = NavSmoothingMode::LineOfSight;
    // Chaikin/Catmull-Rom passes. 1..8; higher values cost more but stay inside
    // the fixed 128-waypoint budget.
    std::uint8_t SmoothingIterations = 2;
    // 0 keeps the raw polyline, 1 is the full curve. Intermediate values blend
    // towards the raw corners.
    float SmoothingStrength = 0.8f;
    // Deterministic route variation. RandomSeed makes the same query repeatable.
    std::uint32_t RandomSeed = 0;
    // Waypoint jitter in cells, 0 disables. Jitter stays on passable cells.
    float RandomWaypointCells = 0.0f;
    // Extra edge cost noise for route selection, 0 disables. Values around
    // 0.1..2.0 produce different but still sensible routes.
    float RandomCost = 0.0f;
    bool RandomizeRoute = false;
};

struct NavPath
{
    std::array<Grid::Cell, kMaxPathCells> Cells{};
    // Fractional movement points produced by smoothing/jitter. When HasPoints
    // is false the consumer uses cell centres from Cells.
    std::array<Vector2D, kMaxPathCells> Points{};
    std::uint16_t Count = 0;
    bool Truncated = false;
    bool HasPoints = false;
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

    // A* followed by the requested smoothing and deterministic route variation.
    // The raw A* result remains in OutPath->Cells; Points carries the final
    // movement positions.
    [[nodiscard]] bool FindWithOptions(const NavMap& InMap, Grid::Cell InStart, Grid::Cell InGoal,
                                       const NavPathOptions& InOptions, NavPath* OutPath,
                                       bool InAllowPartial = false) noexcept;

    // Path with up to `InMaxCells` cells; the rest is dropped and Truncated set.
    [[nodiscard]] bool FindLimited(const NavMap& InMap, Grid::Cell InStart, Grid::Cell InGoal, std::uint16_t InMaxCells,
                                   NavPath* OutPath) noexcept;

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
    std::uint32_t MRandomSeed = 0;
    float MRandomCost = 0.0f;
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
    // KNavAgentFlagActive and reserved authoring switches.
    std::uint32_t Flags = 0;
    std::uint32_t Reserved = 0;
};

static_assert(std::is_trivially_copyable_v<NavAgentSettings>);
static_assert(std::is_standard_layout_v<NavAgentSettings>);
static_assert(sizeof(NavAgentSettings) == 32);

// Optional per-agent path shaping. Keep this separate from NavAgentSettings so
// simple agents keep the compact 32-byte authoring record.
struct NavAgentPathSettings
{
    // Stored as 32-bit fields so Mirror/TOML can serialize them without size
    // special cases; ToOptions narrows them to the runtime enum.
    std::uint32_t Smoothing = static_cast<std::uint32_t>(NavSmoothingMode::LineOfSight);
    std::uint32_t SmoothingIterations = 2;
    float SmoothingStrength = 0.8f;
    std::uint32_t RandomSeed = 0;
    float RandomWaypointCells = 0.0f;
    float RandomCost = 0.0f;
    bool RandomizeRoute = false;
    std::uint8_t Reserved[3] = {};

    [[nodiscard]] NavPathOptions ToOptions() const noexcept
    {
        NavPathOptions ScopedOptions;
        ScopedOptions.Smoothing =
            static_cast<NavSmoothingMode>(Smoothing > static_cast<std::uint32_t>(NavSmoothingMode::CatmullRom)
                                              ? static_cast<std::uint32_t>(NavSmoothingMode::CatmullRom)
                                              : Smoothing);
        ScopedOptions.SmoothingIterations =
            static_cast<std::uint8_t>(SmoothingIterations > 8 ? 8 : SmoothingIterations);
        ScopedOptions.SmoothingStrength = SmoothingStrength;
        ScopedOptions.RandomSeed = RandomSeed;
        ScopedOptions.RandomWaypointCells = RandomWaypointCells;
        ScopedOptions.RandomCost = RandomCost;
        ScopedOptions.RandomizeRoute = RandomizeRoute;
        return ScopedOptions;
    }
};

static_assert(std::is_trivially_copyable_v<NavAgentPathSettings>);
static_assert(std::is_standard_layout_v<NavAgentPathSettings>);

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
    // Optional polygon navmesh. When ready it replaces the grid A* for movement
    // paths; the cost map stays the source of passability for both.
    void SetNavigationMesh(const NavMesh* InMesh) noexcept
    {
        MNavMesh = InMesh;
    }
    [[nodiscard]] const NavMesh* NavigationMesh() const noexcept
    {
        return MNavMesh;
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
    const NavMesh* MNavMesh = nullptr;
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

FBY_REFLECT_COMPONENT(Fby::Navigation::NavAgentPathSettings, "NavAgentPathSettings",
                      FBY_FIELD(Fby::Navigation::NavAgentPathSettings, Smoothing, UInt32),
                      FBY_FIELD(Fby::Navigation::NavAgentPathSettings, SmoothingIterations, UInt32),
                      FBY_FIELD(Fby::Navigation::NavAgentPathSettings, SmoothingStrength, Float),
                      FBY_FIELD(Fby::Navigation::NavAgentPathSettings, RandomSeed, UInt32),
                      FBY_FIELD(Fby::Navigation::NavAgentPathSettings, RandomWaypointCells, Float),
                      FBY_FIELD(Fby::Navigation::NavAgentPathSettings, RandomCost, Float),
                      FBY_FIELD(Fby::Navigation::NavAgentPathSettings, RandomizeRoute, Bool))
