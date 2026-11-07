// FemboyEngine2D - scene-less card boards.
#pragma once

#include "Engine/Core/Types.hpp"
#include "Engine/ECS/Components.hpp"
#include "Engine/ECS/Schedule.hpp"

#include <cstdint>
#include <span>
#include <type_traits>

namespace Fby::Ui
{

enum SlotFlags : std::uint32_t
{
    SlotNone = 0,
    SlotFaceUp = 1u << 0,
    SlotSelected = 1u << 1,
    SlotHovered = 1u << 2,
    SlotLocked = 1u << 3,
    SlotHighlight = 1u << 4,
    // Use `position` instead of the computed board cell (hard slot anchor)
    SlotAnchored = 1u << 5,
};

struct SlotComponent
{
    std::uint32_t BoardId = 0;
    std::uint32_t SlotIndex = 0;
    std::uint32_t CardId = 0;
    std::uint32_t Flags = 0;
    // 0..3 for suit, 0..14 for rank; opaque to the layout system
    std::uint16_t Suit = 0;
    std::uint16_t Rank = 0;
    // Local nudge in board space, used for a lifted/selected card
    Vector2D Offset{};
    float Rotation = 0.0f;
    std::uint16_t Layer = 0;
    std::uint16_t Reserved = 0;
    // Resolved board-space position and occupancy. An anchored slot uses this
    Vector2D Position{};
    bool IsOccupied = false;
};

static_assert(std::is_trivially_copyable_v<SlotComponent>);
static_assert(std::is_standard_layout_v<SlotComponent>);

struct CardBoardComponent
{
    std::uint32_t BoardId = 0;
    std::uint32_t Columns = 8;
    std::uint32_t Rows = 2;
    Vector2D Origin{0.0f, 0.0f};
    Vector2D CardSize{72.0f, 104.0f};
    Vector2D Spacing{12.0f, 12.0f};
    float Rotation = 0.0f;
    std::uint32_t Flags = 0;
    // Colour used by the procedural SDF card back
    Color32 BackColor = Color32::White();
};

static_assert(std::is_trivially_copyable_v<CardBoardComponent>);

struct SlotLayout
{
    Vector2D Position{};
    float Rotation = 0.0f;
};

// Pure layout function shared by the ECS system and the UI packer. `slotIndex` is
[[nodiscard]] SlotLayout ComputeSlotLayout(const CardBoardComponent& InBoard, const SlotComponent& InSlot) noexcept;

class CardBoardSystem
{
public:
    static constexpr std::uint32_t kMaxBoards = 16;

    // PostUpdate. Materialises/updates Transform2D for every slot. A headless
    static void Update(const Ecs::CoreSystemView& InView) noexcept;
    [[nodiscard]] static bool Register(Ecs::SystemGraph& InGraph);

private:
    static void Tick(const Ecs::CoreSystemView& InView, void* InUser);
};
}
