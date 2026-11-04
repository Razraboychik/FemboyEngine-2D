// FemboyEngine2D - paged navigation grid implementation.
#include "Engine/Navigation/PagedNavGrid.hpp"

#include "Engine/ECS/Tilemap.hpp"

#include <algorithm>

namespace Fby::Navigation
{

bool PagedNavGrid::Configure(const PagedNavGridDesc& InDesc) noexcept
{
    if (InDesc.PagesX == 0 || InDesc.PagesY == 0 || InDesc.PagesX > kMaxPagesX || InDesc.PagesY > kMaxPagesY ||
        InDesc.CellSize.X <= 0.0f || InDesc.CellSize.Y <= 0.0f)
    {
        return false;
    }
    MDesc = InDesc;
    MPages.clear();
    MPages.resize(static_cast<std::size_t>(InDesc.PagesX) * InDesc.PagesY);
    MAllocatedPages = 0;
    MWindowX = 0;
    MWindowY = 0;
    (void)MField.Configure(FlowFieldDesc{});
    return true;
}

void PagedNavGrid::Clear() noexcept
{
    for (std::unique_ptr<Page>& ScopedPage : MPages)
    {
        ScopedPage.reset();
    }
    MAllocatedPages = 0;
    MWindowX = 0;
    MWindowY = 0;
    (void)MField.Configure(FlowFieldDesc{});
}

PagedNavGrid::Page* PagedNavGrid::EnsurePage(std::uint32_t InPageX, std::uint32_t InPageY) noexcept
{
    if (InPageX >= MDesc.PagesX || InPageY >= MDesc.PagesY)
    {
        return nullptr;
    }
    std::unique_ptr<Page>& ScopedSlot = MPages[static_cast<std::size_t>(InPageY) * MDesc.PagesX + InPageX];
    if (!ScopedSlot)
    {
        ScopedSlot = std::make_unique<Page>();
        ScopedSlot->Cells.assign(static_cast<std::size_t>(kPageSide) * kPageSide, MDesc.DefaultCost);
        ScopedSlot->Allocated = true;
        ++MAllocatedPages;
    }
    return ScopedSlot.get();
}

const PagedNavGrid::Page* PagedNavGrid::FindPage(std::uint32_t InPageX, std::uint32_t InPageY) const noexcept
{
    if (InPageX >= MDesc.PagesX || InPageY >= MDesc.PagesY)
    {
        return nullptr;
    }
    const std::unique_ptr<Page>& ScopedSlot = MPages[static_cast<std::size_t>(InPageY) * MDesc.PagesX + InPageX];
    return ScopedSlot.get();
}

bool PagedNavGrid::SetCost(std::uint32_t InX, std::uint32_t InY, std::uint8_t InCost) noexcept
{
    if (!IsConfigured() || InX >= WidthCells() || InY >= HeightCells())
    {
        return false;
    }
    Page* ScopedPage = EnsurePage(InX / kPageSide, InY / kPageSide);
    if (ScopedPage == nullptr)
    {
        return false;
    }
    ScopedPage->Cells[static_cast<std::size_t>(InY % kPageSide) * kPageSide + (InX % kPageSide)] = InCost;
    return true;
}

std::uint8_t PagedNavGrid::CostAt(std::uint32_t InX, std::uint32_t InY) const noexcept
{
    if (!IsConfigured() || InX >= WidthCells() || InY >= HeightCells())
    {
        return 0;
    }
    const Page* ScopedPage = FindPage(InX / kPageSide, InY / kPageSide);
    if (ScopedPage == nullptr)
    {
        return 0;
    }
    return ScopedPage->Cells[static_cast<std::size_t>(InY % kPageSide) * kPageSide + (InX % kPageSide)];
}

bool PagedNavGrid::BakeFromWorld(const SdfWorld& InWorld, float InAgentRadius, std::uint8_t InNearCost) noexcept
{
    if (!IsConfigured() || !InWorld.IsConfigured())
    {
        return false;
    }
    const SdfField& ScopedField = InWorld.Field();
    const float ScopedNearBand = std::max(InAgentRadius, 0.0f) + std::max(MDesc.CellSize.X, MDesc.CellSize.Y);
    for (std::uint32_t ScopedY = 0; ScopedY < HeightCells(); ++ScopedY)
    {
        for (std::uint32_t ScopedX = 0; ScopedX < WidthCells(); ++ScopedX)
        {
            const Vector2D ScopedWorld{MDesc.Origin.X + (static_cast<float>(ScopedX) + 0.5f) * MDesc.CellSize.X,
                                       MDesc.Origin.Y + (static_cast<float>(ScopedY) + 0.5f) * MDesc.CellSize.Y};
            const float ScopedDistance = ScopedField.Sample(ScopedWorld);
            const std::uint8_t ScopedCost =
                ScopedDistance <= InAgentRadius ? 0 : (ScopedDistance <= ScopedNearBand ? InNearCost : 1);
            if (!SetCost(ScopedX, ScopedY, ScopedCost))
            {
                return false;
            }
        }
    }
    return true;
}

bool PagedNavGrid::BakeFromWorld(const SdfWorld& InWorld, const TilemapComponent& InTilemap,
                                 float InAgentRadius, std::uint8_t InNearCost) noexcept
{
    if (!BakeFromWorld(InWorld, InAgentRadius, InNearCost))
    {
        return false;
    }
    // Solid tiles override the SDF result: authored collision is authoritative.
    for (std::uint32_t ScopedY = 0; ScopedY < HeightCells(); ++ScopedY)
    {
        for (std::uint32_t ScopedX = 0; ScopedX < WidthCells(); ++ScopedX)
        {
            const Vector2D ScopedWorld{MDesc.Origin.X + (static_cast<float>(ScopedX) + 0.5f) * MDesc.CellSize.X,
                                       MDesc.Origin.Y + (static_cast<float>(ScopedY) + 0.5f) * MDesc.CellSize.Y};
            const Grid::Cell ScopedCell = Tilemap::WorldToCell(InTilemap, ScopedWorld);
            if (Tilemap::IsSolid(InTilemap, ScopedCell))
            {
                if (!SetCost(ScopedX, ScopedY, 0))
                {
                    return false;
                }
            }
        }
    }
    return true;
}

bool PagedNavGrid::BuildFlow(std::uint32_t InGoalX, std::uint32_t InGoalY) noexcept
{
    if (!IsConfigured() || InGoalX >= WidthCells() || InGoalY >= HeightCells())
    {
        return false;
    }
    const std::uint32_t ScopedWidth = WidthCells();
    const std::uint32_t ScopedHeight = HeightCells();
    std::uint32_t ScopedWindowWidth = std::min(ScopedWidth, kMaxWindowSide);
    std::uint32_t ScopedWindowHeight = std::min(ScopedHeight, kMaxWindowSide);
    // Centre the window on the goal and clamp it to the grid.
    const std::uint32_t ScopedHalfWidth = ScopedWindowWidth / 2u;
    const std::uint32_t ScopedHalfHeight = ScopedWindowHeight / 2u;
    std::uint32_t ScopedWindowX = InGoalX > ScopedHalfWidth ? InGoalX - ScopedHalfWidth : 0;
    std::uint32_t ScopedWindowY = InGoalY > ScopedHalfHeight ? InGoalY - ScopedHalfHeight : 0;
    if (ScopedWindowX + ScopedWindowWidth > ScopedWidth)
    {
        ScopedWindowX = ScopedWidth - ScopedWindowWidth;
    }
    if (ScopedWindowY + ScopedWindowHeight > ScopedHeight)
    {
        ScopedWindowY = ScopedHeight - ScopedWindowHeight;
    }
    MWindowX = ScopedWindowX;
    MWindowY = ScopedWindowY;

    FlowFieldDesc ScopedFieldDesc;
    ScopedFieldDesc.Width = ScopedWindowWidth;
    ScopedFieldDesc.Height = ScopedWindowHeight;
    if (!MField.Configure(ScopedFieldDesc))
    {
        return false;
    }
    for (std::uint32_t ScopedY = 0; ScopedY < ScopedWindowHeight; ++ScopedY)
    {
        for (std::uint32_t ScopedX = 0; ScopedX < ScopedWindowWidth; ++ScopedX)
        {
            (void)MField.SetCost(ScopedX, ScopedY, CostAt(MWindowX + ScopedX, MWindowY + ScopedY));
        }
    }
    const std::uint32_t ScopedGoalIndex = (InGoalY - MWindowY) * ScopedWindowWidth + (InGoalX - MWindowX);
    MField.Build(std::span<const std::uint32_t>(&ScopedGoalIndex, 1));
    return true;
}

bool PagedNavGrid::IsReachableCell(std::uint32_t InX, std::uint32_t InY) const noexcept
{
    const std::uint32_t ScopedWidth = MField.Width();
    if (ScopedWidth == 0 || InX < MWindowX || InY < MWindowY)
    {
        return false;
    }
    const std::uint32_t ScopedLocalX = InX - MWindowX;
    const std::uint32_t ScopedLocalY = InY - MWindowY;
    return MField.IsReachable(ScopedLocalX, ScopedLocalY);
}

std::uint32_t PagedNavGrid::PotentialAtCell(std::uint32_t InX, std::uint32_t InY) const noexcept
{
    if (!IsReachableCell(InX, InY))
    {
        return kFlowFieldInfinite;
    }
    return MField.PotentialAt(InX - MWindowX, InY - MWindowY);
}

Vector2D PagedNavGrid::FlowAtCell(std::uint32_t InX, std::uint32_t InY) const noexcept
{
    if (!IsReachableCell(InX, InY))
    {
        return Vector2D{0.0f, 0.0f};
    }
    return MField.FlowAt(InX - MWindowX, InY - MWindowY);
}

}
