// FemboyEngine2D - paged navigation storage with a windowed flow build.
#pragma once

#include "Engine/Core/Types.hpp"
#include "Engine/Navigation/FlowField.hpp"
#include "Engine/Physics/SdfWorld.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace Fby
{
struct TilemapComponent;
}

namespace Fby::Navigation
{

// A navigation grid stored as 64x64 pages. Pages are allocated on the first write,
// so a large world only pays for the ground that has been authored or touched.
// Flow is built over a window that covers the goal, which keeps the search bounded.
struct PagedNavGridDesc
{
    Vector2D Origin{};
    Vector2D CellSize{8.0f, 8.0f};
    std::uint32_t PagesX = 0;
    std::uint32_t PagesY = 0;
    std::uint8_t DefaultCost = 1;
};

class PagedNavGrid
{
public:
    inline static constexpr std::uint32_t kPageSide = 64;
    inline static constexpr std::uint32_t kMaxPagesX = 32;
    inline static constexpr std::uint32_t kMaxPagesY = 32;
    // A window wider than this still builds, because FlowField caps itself.
    inline static constexpr std::uint32_t kMaxWindowSide = kFlowFieldMaxSide;

    [[nodiscard]] bool Configure(const PagedNavGridDesc& InDesc) noexcept;
    [[nodiscard]] bool IsConfigured() const noexcept
    {
        return MDesc.PagesX > 0 && MDesc.PagesY > 0;
    }
    [[nodiscard]] std::uint32_t WidthCells() const noexcept
    {
        return MDesc.PagesX * kPageSide;
    }
    [[nodiscard]] std::uint32_t HeightCells() const noexcept
    {
        return MDesc.PagesY * kPageSide;
    }
    [[nodiscard]] std::uint32_t AllocatedPageCount() const noexcept
    {
        return MAllocatedPages;
    }
    [[nodiscard]] const PagedNavGridDesc& Desc() const noexcept
    {
        return MDesc;
    }

    void Clear() noexcept;
    [[nodiscard]] bool SetCost(std::uint32_t InX, std::uint32_t InY, std::uint8_t InCost) noexcept;
    // Missing pages read as blocked, so an unauthored region never invites a path.
    [[nodiscard]] std::uint8_t CostAt(std::uint32_t InX, std::uint32_t InY) const noexcept;

    // Bakes the page costs from the world SDF: cells closer than the agent radius
    // are walls, the narrow band costs more and open ground keeps cost 1.
    [[nodiscard]] bool BakeFromWorld(const SdfWorld& InWorld, float InAgentRadius,
                                     std::uint8_t InNearCost = 4) noexcept;
    // Same, but solid tilemap tiles also become walls.
    [[nodiscard]] bool BakeFromWorld(const SdfWorld& InWorld, const TilemapComponent& InTilemap,
                                     float InAgentRadius, std::uint8_t InNearCost = 4) noexcept;

    // Builds the flow over the window that covers the whole grid, or a window of
    // kMaxWindowSide cells centred on the goal when the grid is larger.
    [[nodiscard]] bool BuildFlow(std::uint32_t InGoalX, std::uint32_t InGoalY) noexcept;

    [[nodiscard]] std::uint32_t WindowOriginX() const noexcept
    {
        return MWindowX;
    }
    [[nodiscard]] std::uint32_t WindowOriginY() const noexcept
    {
        return MWindowY;
    }
    [[nodiscard]] std::uint32_t WindowWidth() const noexcept
    {
        return MField.Width();
    }
    [[nodiscard]] std::uint32_t WindowHeight() const noexcept
    {
        return MField.Height();
    }
    [[nodiscard]] const FlowField& Field() const noexcept
    {
        return MField;
    }
    [[nodiscard]] bool IsReachableCell(std::uint32_t InX, std::uint32_t InY) const noexcept;
    [[nodiscard]] std::uint32_t PotentialAtCell(std::uint32_t InX, std::uint32_t InY) const noexcept;
    [[nodiscard]] Vector2D FlowAtCell(std::uint32_t InX, std::uint32_t InY) const noexcept;

private:
    struct Page
    {
        std::vector<std::uint8_t> Cells = std::vector<std::uint8_t>(kPageSide * kPageSide, 1);
        bool Allocated = false;
    };

    [[nodiscard]] Page* EnsurePage(std::uint32_t InPageX, std::uint32_t InPageY) noexcept;
    [[nodiscard]] const Page* FindPage(std::uint32_t InPageX, std::uint32_t InPageY) const noexcept;

    PagedNavGridDesc MDesc{};
    std::vector<std::unique_ptr<Page>> MPages;
    std::uint32_t MAllocatedPages = 0;
    FlowField MField;
    std::uint32_t MWindowX = 0;
    std::uint32_t MWindowY = 0;
};

}
