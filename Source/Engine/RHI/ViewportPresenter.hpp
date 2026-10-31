// FemboyEngine2D - viewport view-mode presenter: per-sprite diagnostic tints
// computed once per frame from the same ECS data the game renders.
#pragma once

#include "Engine/Core/Types.hpp"
#include "Engine/ECS/Schedule.hpp"
#include "Engine/Physics/SdfCollision.hpp"
#include "Engine/RHI/ViewportModes.hpp"

#include <array>
#include <cstdint>

namespace Fby::Rhi
{

// Frame-level diagnostics for the viewport HUD and the tests.
struct ViewportPresenterStats
{
    std::uint32_t VisibleSprites = 0;
    std::uint32_t TintedSprites = 0;
    std::uint32_t PeakOverdraw = 0;
    std::uint32_t MaxLightsPerSprite = 0;
    std::uint32_t ColliderCount = 0;
};

// The presenter owns no scene data: Prepare reads a registry into a fixed
// capacity table, TintForEntity answers the extract, and the table is replaced
// on the next Prepare. Nothing allocates.
class ViewportPresenter
{
public:
    static constexpr std::uint32_t kMaxPresentedSprites = 4096;
    static constexpr std::uint32_t kDefaultComplexityBudget = 8;

    void SetMode(ViewportMode InMode) noexcept
    {
        MMode = InMode;
    }
    [[nodiscard]] ViewportMode Mode() const noexcept
    {
        return MMode;
    }

    void SetSdfField(const SdfField* InField) noexcept
    {
        MSdfField = InField;
    }

    // Normalization reference for the complexity heat ramp: a sprite at or
    // above this cost is fully red.
    void SetComplexityBudget(std::uint32_t InBudget) noexcept
    {
        MComplexityBudget = InBudget > 0 ? InBudget : 1u;
    }

    // Recomputes the tint table for the current mode. Standard and Unlit only
    // count the visible sprites; the diagnostic modes sample colliders, lights
    // and the SDF field.
    void Prepare(Ecs::Registry& InRegistry) noexcept;

    // Writes the diagnostic colour for one entity. Returns false for Standard
    // and Unlit, where the extract keeps the authored/lit colour.
    [[nodiscard]] bool TintForEntity(Entity InEntity, std::uint32_t* OutColor) const noexcept;

    [[nodiscard]] const ViewportPresenterStats& Stats() const noexcept
    {
        return MStats;
    }

    // Colour the editor uses for the collision overlay of one entity. It reads
    // the same policy the physics bridge consumes.
    [[nodiscard]] static Color32 CollisionOverlayColor(const Ecs::Registry& InRegistry, Entity InEntity) noexcept;

    // Opaque diagnostic colour of one collision mode.
    [[nodiscard]] static Color32 CollisionModeColor(std::uint32_t InMode) noexcept;

private:
    [[nodiscard]] std::uint32_t CountReachingLights(Ecs::Registry& InRegistry, Entity InEntity,
                                                    Vector2D InPosition) const noexcept;

    struct TintEntry
    {
        Entity Owner = entt::null;
        std::uint32_t Color = 0xFFFFFFFFu;
    };

    struct VisibleSprite
    {
        Entity Owner = entt::null;
        Vector2D Center{};
        Vector2D HalfExtents{};
        std::uint32_t BaseColor = 0xFFFFFFFFu;
    };

    std::array<TintEntry, kMaxPresentedSprites> MTints{};
    std::array<VisibleSprite, kMaxPresentedSprites> MSprites{};
    ViewportPresenterStats MStats{};
    ViewportMode MMode = ViewportMode::Standard;
    const SdfField* MSdfField = nullptr;
    std::uint32_t MTintCount = 0;
    std::uint32_t MSpriteCount = 0;
    std::uint32_t MComplexityBudget = kDefaultComplexityBudget;
};
}
