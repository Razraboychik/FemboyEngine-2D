// FemboyEngine2D - network relevance grid.
#pragma once

#include "Engine/Core/Types.hpp"
#include "Engine/ECS/Schedule.hpp"
#include "Engine/Network/NetRollback.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>
#include <vector>

#if defined(__cpp_lib_mdspan) && __cpp_lib_mdspan >= 202207L
#include <mdspan>
#define FBY_RELEVANCE_HAS_MDSPAN 1
#else
#define FBY_RELEVANCE_HAS_MDSPAN 0
#endif

namespace Fby::Net
{

inline constexpr std::uint32_t kMaxRelevanceCells = 4096;
inline constexpr std::uint32_t kMaxRelevanceEntitiesPerCell = 128;
inline constexpr std::uint32_t kMaxRelevanceOutput = kMaxNetEntities;
inline constexpr std::uint32_t kRelevanceBitsetWords = (kMaxNetEntities + 63u) / 64u;

enum class RelevanceBand : std::uint8_t
{
    None = 0,
    Immediate = 1,
    Near = 2,
    Far = 3,
};

[[nodiscard]] constexpr const char* RelevanceBandName(RelevanceBand InBand) noexcept
{
    switch (InBand)
    {
        case RelevanceBand::None:
            return "none";
        case RelevanceBand::Immediate:
            return "immediate";
        case RelevanceBand::Near:
            return "near";
        case RelevanceBand::Far:
            return "far";
    }
    return "?";
}

struct RelevanceConfig
{
    Vector2D origin{0.0f, 0.0f};
    float cellSize = 512.0f;
    std::uint32_t cellsX = 32;
    std::uint32_t cellsY = 32;
    // Chebyshev radius treated as "current plus neighbours"
    std::uint32_t neighborRadius = 1;
    std::uint32_t nearRadius = 3;
    std::uint32_t farRadius = 8;
    // Send cadence in ticks for the two distant bands
    std::uint32_t nearInterval = 2;
    std::uint32_t farInterval = 4;
};

struct RelevanceCell
{
    std::uint32_t count = 0;
    std::uint32_t dropped = 0;
    std::array<NetEntityId, kMaxRelevanceEntitiesPerCell> entities{};
};

static_assert(std::is_trivially_copyable_v<RelevanceCell>);
static_assert(sizeof(RelevanceCell) == kMaxRelevanceEntitiesPerCell * sizeof(NetEntityId) + 8u);

struct RelevanceEntity
{
    NetEntityId id{};
    std::uint16_t cellX = 0;
    std::uint16_t cellY = 0;
    RelevanceBand band = RelevanceBand::None;
    std::uint8_t interval = 1;
    std::uint32_t phase = 0;
};

static_assert(sizeof(RelevanceEntity) == 16);

struct RelevancePlan
{
    NetPlayerId player{};
    std::uint64_t tick = 0;
    std::uint32_t count = 0;
    std::uint32_t immediateCount = 0;
    std::uint32_t nearCount = 0;
    std::uint32_t farCount = 0;
    bool truncated = false;
    std::array<RelevanceEntity, kMaxRelevanceOutput> items{};
    // One bit per NetEntityId; the delta filter reads it without searching.
    std::array<std::uint64_t, kRelevanceBitsetWords> bits{};

    [[nodiscard]] bool Contains(NetEntityId InId) const noexcept
    {
        return InId.IsValid() && (bits[InId.value >> 6u] & (1ull << (InId.value & 63u))) != 0;
    }
    void Set(NetEntityId InId) noexcept
    {
        if (InId.IsValid())
        {
            bits[InId.value >> 6u] |= 1ull << (InId.value & 63u);
        }
    }
};

static_assert(std::is_trivially_copyable_v<RelevancePlan>);

struct RelevanceGridStats
{
    std::uint32_t mappedEntities = 0;
    std::uint32_t placedEntities = 0;
    std::uint32_t outOfBounds = 0;
    std::uint32_t droppedEntities = 0;
    std::uint32_t occupiedCells = 0;
};

// NetEntityId -> Vector2D is supplied by the session's Transform2D view. The grid
class RelevanceGrid
{
public:
    RelevanceGrid() = default;

    [[nodiscard]] bool Initialize(const RelevanceConfig& InConfig, std::string_view* InError = nullptr);

    void Rebuild(Ecs::Registry& InRegistry, const NetEntityMap& InEntities) noexcept;

    void SetPlayerPosition(NetPlayerId InPlayer, Vector2D InPosition) noexcept
    {
        if (InPlayer.IsValid())
        {
            MPlayerPositions[InPlayer.value] = InPosition;
            MHasPlayer[InPlayer.value] = true;
        }
    }
    [[nodiscard]] Vector2D PlayerPosition(NetPlayerId InPlayer) const noexcept
    {
        return InPlayer.IsValid() ? MPlayerPositions[InPlayer.value] : Vector2D{};
    }
    [[nodiscard]] bool HasPlayer(NetPlayerId InPlayer) const noexcept
    {
        return InPlayer.IsValid() && MHasPlayer[InPlayer.value];
    }

    [[nodiscard]] bool BuildPlan(NetPlayerId InPlayer, std::uint64_t InTick, RelevancePlan& OutPlan) const noexcept;
    [[nodiscard]] RelevanceBand BandFor(std::uint32_t InDx, std::uint32_t InDy) const noexcept;
    [[nodiscard]] bool IsDue(std::uint32_t InInterval, NetEntityId InId, std::uint64_t InTick) const noexcept
    {
        if (InInterval <= 1)
        {
            return true;
        }
        return (InTick % InInterval) == (static_cast<std::uint64_t>(InId.value) % InInterval);
    }

    struct CellCoordinate
    {
        std::int32_t x = 0;
        std::int32_t y = 0;
        bool valid = false;
    };
    [[nodiscard]] CellCoordinate CellOf(Vector2D InPosition) const noexcept;
    [[nodiscard]] const RelevanceCell* CellAt(std::uint32_t InX, std::uint32_t InY) const noexcept;

    [[nodiscard]] const RelevanceConfig& Config() const noexcept
    {
        return MConfig;
    }
    [[nodiscard]] const RelevanceGridStats& Stats() const noexcept
    {
        return MStats;
    }
    [[nodiscard]] bool Initialized() const noexcept
    {
        return MInitialized;
    }
    [[nodiscard]] std::uint32_t CellCount() const noexcept
    {
        return MConfig.cellsX * MConfig.cellsY;
    }

private:
    struct SortableEntity
    {
        std::uint32_t id = kInvalidNetEntityId;
        Entity local = entt::null;
    };

    [[nodiscard]] RelevanceCell& CellRef(std::uint32_t InX, std::uint32_t InY) noexcept
    {
#if FBY_RELEVANCE_HAS_MDSPAN
        const std::mdspan<RelevanceCell, std::dextents<std::size_t, 2>> ScopedGrid(MCells.data(), MConfig.cellsY,
                                                                                   MConfig.cellsX);
        return ScopedGrid[InY, InX];
#else
        return MCells[static_cast<std::size_t>(InY) * MConfig.cellsX + InX];
#endif
    }
    [[nodiscard]] const RelevanceCell& CellRef(std::uint32_t InX, std::uint32_t InY) const noexcept
    {
#if FBY_RELEVANCE_HAS_MDSPAN
        const std::mdspan<const RelevanceCell, std::dextents<std::size_t, 2>> ScopedGrid(MCells.data(), MConfig.cellsY,
                                                                                         MConfig.cellsX);
        return ScopedGrid[InY, InX];
#else
        return MCells[static_cast<std::size_t>(InY) * MConfig.cellsX + InX];
#endif
    }

    RelevanceConfig MConfig{};
    std::vector<RelevanceCell> MCells;
    std::array<SortableEntity, kMaxNetEntities> MSortScratch{};
    std::array<Vector2D, kMaxNetPlayers> MPlayerPositions{};
    std::array<bool, kMaxNetPlayers> MHasPlayer{};
    RelevanceGridStats MStats{};
    bool MInitialized = false;
};
}
