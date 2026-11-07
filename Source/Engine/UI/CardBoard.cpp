// FemboyEngine2D - scene-less card board layout.
#include "Engine/UI/CardBoard.hpp"

#include "Engine/ECS/SystemGraph.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace Fby::Ui
{

SlotLayout ComputeSlotLayout(const CardBoardComponent& InBoard, const SlotComponent& InSlot) noexcept
{
    // A corrupt board size must not overflow the cell product or index math.
    const std::uint32_t ScopedColumns = std::clamp(InBoard.Columns, 1u, 64u);
    const std::uint32_t ScopedRows = std::clamp(InBoard.Rows, 1u, 64u);
    const std::uint32_t ScopedIndex = std::min(InSlot.SlotIndex, ScopedColumns * ScopedRows - 1u);
    if ((InSlot.Flags & SlotAnchored) != 0)
    {
        return SlotLayout{InSlot.Position, InBoard.Rotation + InSlot.Rotation};
    }
    const std::uint32_t ScopedColumn = ScopedIndex % ScopedColumns;
    const std::uint32_t ScopedRow = ScopedIndex / ScopedColumns;
    const Vector2D ScopedLocal{
        static_cast<float>(ScopedColumn) * (InBoard.CardSize.X + InBoard.Spacing.X) + InBoard.CardSize.X * 0.5f,
        static_cast<float>(ScopedRow) * (InBoard.CardSize.Y + InBoard.Spacing.Y) + InBoard.CardSize.Y * 0.5f};
    const float ScopedCos = std::cos(InBoard.Rotation);
    const float ScopedSin = std::sin(InBoard.Rotation);
    const Vector2D ScopedRotated{ScopedLocal.X * ScopedCos - ScopedLocal.Y * ScopedSin,
                                 ScopedLocal.X * ScopedSin + ScopedLocal.Y * ScopedCos};
    SlotLayout ScopedResult{};
    ScopedResult.Position = InBoard.Origin + ScopedRotated + InSlot.Offset;
    ScopedResult.Rotation = InBoard.Rotation + InSlot.Rotation;
    return ScopedResult;
}

void CardBoardSystem::Update(const Ecs::CoreSystemView& InView) noexcept
{
    struct BoardRef
    {
        std::uint32_t id = 0;
        const CardBoardComponent* board = nullptr;
    };
    std::array<BoardRef, kMaxBoards> ScopedBoards{};
    std::uint32_t ScopedBoardCount = 0;
    auto ScopedBoardView = InView.registry.view<CardBoardComponent>();
    for (const Entity ScopedEntity : ScopedBoardView)
    {
        if (ScopedBoardCount >= ScopedBoards.size())
        {
            break;
        }
        const CardBoardComponent& ScopedBoard = ScopedBoardView.get<CardBoardComponent>(ScopedEntity);
        ScopedBoards[ScopedBoardCount].id = ScopedBoard.BoardId;
        ScopedBoards[ScopedBoardCount].board = &ScopedBoard;
        ++ScopedBoardCount;
    }
    auto ScopedSlotView = InView.registry.view<SlotComponent>();
    for (const Entity ScopedEntity : ScopedSlotView)
    {
        SlotComponent& ScopedSlot = ScopedSlotView.get<SlotComponent>(ScopedEntity);
        const CardBoardComponent* ScopedBoard = nullptr;
        for (std::uint32_t ScopedI = 0; ScopedI < ScopedBoardCount; ++ScopedI)
        {
            if (ScopedBoards[ScopedI].id == ScopedSlot.BoardId)
            {
                ScopedBoard = ScopedBoards[ScopedI].board;
                break;
            }
        }
        if (ScopedBoard == nullptr)
        {
            continue;
        }
        const SlotLayout ScopedLayout = ComputeSlotLayout(*ScopedBoard, ScopedSlot);
        ScopedSlot.Position = ScopedLayout.Position;
        ScopedSlot.IsOccupied = ScopedSlot.CardId != 0u;
        Transform2D& ScopedTransform = InView.registry.get_or_emplace<Transform2D>(ScopedEntity);
        ScopedTransform.Position = ScopedLayout.Position;
        ScopedTransform.Rotation = ScopedLayout.Rotation;
        if (auto* ScopedOrder = InView.registry.try_get<DrawOrder>(ScopedEntity); ScopedOrder == nullptr)
        {
            DrawOrder ScopedNewOrder{};
            ScopedNewOrder.Layer = ScopedSlot.Layer;
            InView.registry.emplace<DrawOrder>(ScopedEntity, ScopedNewOrder);
        }
        else if (ScopedOrder->Layer != ScopedSlot.Layer)
        {
            // The slot can change layer after the first frame; keep DrawOrder in
            // sync instead of freezing the initial value.
            ScopedOrder->Layer = ScopedSlot.Layer;
        }
    }
}

bool CardBoardSystem::Register(Ecs::SystemGraph& InGraph)
{
    return InGraph.Add(Ecs::Phase::PostUpdate, "CardBoardSystem", Ecs::BindCoreSystem<&CardBoardSystem::Tick>());
}

void CardBoardSystem::Tick(const Ecs::CoreSystemView& InView, void* InUser)
{
    (void)InUser;
    Update(InView);
}
}
