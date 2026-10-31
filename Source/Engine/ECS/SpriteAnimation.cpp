#include "Engine/ECS/SpriteAnimation.hpp"

#include "Engine/ECS/SystemGraph.hpp"

#include <algorithm>
#include <cmath>

namespace Fby
{
namespace
{
constexpr float kMinFrameDuration = 1.0f / 1000.0f;

[[nodiscard]] std::uint32_t SafeFrameCount(const SpriteAnimationComponent& InSpriteAnimation) noexcept
{
    return std::max(InSpriteAnimation.FrameCount, 1u);
}

[[nodiscard]] std::uint32_t SafeColumns(const SpriteAnimationComponent& InSpriteAnimation, const SpriteSheetComponent* InGrid) noexcept
{
    if (InGrid != nullptr && InGrid->Columns > 0)
    {
        return InGrid->Columns;
    }
    return SafeFrameCount(InSpriteAnimation);
}

[[nodiscard]] std::uint32_t SafeRows(const SpriteSheetComponent* InGrid) noexcept
{
    return InGrid != nullptr ? std::max(InGrid->Rows, 1u) : 1u;
}
}

SpriteFrameRect SpriteFrameUv(const SpriteAnimationComponent& InSpriteAnimation, const SpriteSheetComponent* InGrid) noexcept
{
    SpriteFrameRect ScopedResult;
    const std::uint32_t ScopedFrameCount = SafeFrameCount(InSpriteAnimation);
    const std::uint32_t ScopedColumns = SafeColumns(InSpriteAnimation, InGrid);
    const std::uint32_t ScopedRows = SafeRows(InGrid);
    // 64-bit product: a corrupt scene can carry UINT32_MAX columns and rows,
    const std::uint64_t ScopedTotalCells =
        static_cast<std::uint64_t>(ScopedColumns) * static_cast<std::uint64_t>(ScopedRows);

    // currentFrame is local to the clip; startFrame selects the first cell in
    const std::uint32_t ScopedLocal = InSpriteAnimation.CurrentFrame % ScopedFrameCount;
    const std::uint64_t ScopedAbsolute =
        ScopedTotalCells > 0 ? (static_cast<std::uint64_t>(InSpriteAnimation.StartFrame) + ScopedLocal) % ScopedTotalCells : 0;
    const std::uint64_t ScopedColumn = ScopedAbsolute % ScopedColumns;
    const std::uint64_t ScopedRow = (ScopedAbsolute / ScopedColumns) % ScopedRows;

    const Vector2D ScopedRegionMin = InGrid != nullptr ? InGrid->UVMin : Vector2D{0.0f, 0.0f};
    const Vector2D ScopedRegionMax = InGrid != nullptr ? InGrid->UVMax : Vector2D{1.0f, 1.0f};
    const Vector2D ScopedCellSize{(ScopedRegionMax.X - ScopedRegionMin.X) / static_cast<float>(ScopedColumns),
                                  (ScopedRegionMax.Y - ScopedRegionMin.Y) / static_cast<float>(ScopedRows)};

    ScopedResult.uvMin = Vector2D{ScopedRegionMin.X + ScopedCellSize.X * static_cast<float>(ScopedColumn),
                                  ScopedRegionMin.Y + ScopedCellSize.Y * static_cast<float>(ScopedRow)};
    ScopedResult.uvMax = Vector2D{ScopedResult.uvMin.X + ScopedCellSize.X, ScopedResult.uvMin.Y + ScopedCellSize.Y};
    ScopedResult.valid = true;
    return ScopedResult;
}

void ApplySpriteFrame(const SpriteAnimationComponent& InSpriteAnimation, const SpriteSheetComponent* InGrid, Sprite2D& InSprite) noexcept
{
    // A default-constructed SpriteAnimationComponent has no clip yet; it must not erase the
    if (InSpriteAnimation.FrameCount == 0)
    {
        return;
    }
    const SpriteFrameRect ScopedRect = SpriteFrameUv(InSpriteAnimation, InGrid);
    InSprite.UVMin = ScopedRect.uvMin;
    InSprite.UVMax = ScopedRect.uvMax;
    if (InGrid != nullptr)
    {
        // The array handle in SpriteAnimationComponent selects the texture array; the
        InSprite.Layer = InGrid->Layer;
    }
}

bool AdvanceSpriteAnimation(SpriteAnimationComponent& InSpriteAnimation, float InDt) noexcept
{
    const std::uint32_t ScopedFrameCount = SafeFrameCount(InSpriteAnimation);
    if (InSpriteAnimation.CurrentFrame >= ScopedFrameCount)
    {
        InSpriteAnimation.CurrentFrame = ScopedFrameCount - 1u;
    }
    if (!InSpriteAnimation.IsPlaying || ScopedFrameCount <= 1 || !(InDt > 0.0f) || !std::isfinite(InDt))
    {
        return false;
    }

    // A NaN duration from a corrupt scene must not turn into a NaN -> uint64
    const float ScopedDuration =
        std::isfinite(InSpriteAnimation.FrameDuration) ? std::max(InSpriteAnimation.FrameDuration, kMinFrameDuration) : kMinFrameDuration;
    const double ScopedElapsed = std::isfinite(InSpriteAnimation.ElapsedTime) ? static_cast<double>(InSpriteAnimation.ElapsedTime) : 0.0;
    const double ScopedTotal = ScopedElapsed + static_cast<double>(InDt);
    // 1e-6 of a frame is below what a display can show but above float rounding:
    const double ScopedStepCount =
        std::floor((ScopedTotal + static_cast<double>(ScopedDuration) * 1.0e-6) / static_cast<double>(ScopedDuration));
    if (ScopedStepCount < 1.0)
    {
        InSpriteAnimation.ElapsedTime = static_cast<float>(ScopedTotal);
        return false;
    }

    // Keep the fractional remainder so a frame does not drift when dt is not an
    const double ScopedClampedSteps = std::min(ScopedStepCount, 1.0e12);
    const auto ScopedSteps = static_cast<std::uint64_t>(ScopedClampedSteps);
    InSpriteAnimation.ElapsedTime = std::max(
        0.0f, static_cast<float>(ScopedTotal - static_cast<double>(ScopedSteps) * static_cast<double>(ScopedDuration)));

    const std::uint32_t ScopedPrevious = InSpriteAnimation.CurrentFrame;
    if (InSpriteAnimation.IsLooping)
    {
        InSpriteAnimation.CurrentFrame = static_cast<std::uint32_t>(
            (static_cast<std::uint64_t>(InSpriteAnimation.CurrentFrame) + ScopedSteps) % ScopedFrameCount);
    }
    else
    {
        const std::uint64_t ScopedRemaining = static_cast<std::uint64_t>(ScopedFrameCount - 1u - InSpriteAnimation.CurrentFrame);
        if (ScopedSteps >= ScopedRemaining)
        {
            InSpriteAnimation.CurrentFrame = ScopedFrameCount - 1u;
            InSpriteAnimation.IsPlaying = false;
            InSpriteAnimation.ElapsedTime = 0.0f;
        }
        else
        {
            InSpriteAnimation.CurrentFrame += static_cast<std::uint32_t>(ScopedSteps);
        }
    }
    return InSpriteAnimation.CurrentFrame != ScopedPrevious;
}

void SpriteAnimationSystem::Run(Ecs::Registry& InRegistry, float InDt, SpriteAnimationEventQueue* InEvents) const
{
    auto ScopedView = InRegistry.view<SpriteAnimationComponent, Sprite2D>();
    for (auto [ScopedEntity, ScopedSpriteAnimation, ScopedSprite] : ScopedView.each())
    {
        if (ScopedSpriteAnimation.FrameCount == 0)
        {
            continue;
        }
        const SpriteSheetComponent* ScopedGrid = InRegistry.try_get<SpriteSheetComponent>(ScopedEntity);
        const bool ScopedChanged = AdvanceSpriteAnimation(ScopedSpriteAnimation, InDt);
        // Apply unconditionally: an inspector scrub can change currentFrame
        ApplySpriteFrame(ScopedSpriteAnimation, ScopedGrid, ScopedSprite);
        if (ScopedChanged && InEvents != nullptr)
        {
            (void)InEvents->Push(ScopedEntity, ScopedSpriteAnimation.CurrentFrame, kSpriteAnimationEventFrame);
        }
    }
}

void SpriteAnimationSystem::Run(Ecs::Registry& InRegistry, float InDt) const
{
    Run(InRegistry, InDt, nullptr);
}

void SpriteAnimationSystem::Run(Ecs::SystemContext& InContext) const
{
    Run(InContext.registry, InContext.dt, nullptr);
}

void SpriteAnimationSystem::Run(Ecs::SystemContext& InContext, SpriteAnimationEventQueue& InEvents) const
{
    Run(InContext.registry, InContext.dt, &InEvents);
}

void RegisterSpriteAnimationSystem(Ecs::Schedule& InSchedule)
{
    (void)InSchedule.Add(
        Ecs::Phase::FixedUpdate, "spriteAnimation", +[](Ecs::SystemContext& InContext, void*) { SpriteAnimationSystem{}.Run(InContext); });
}

void RegisterSpriteAnimationSystem(Ecs::SystemGraph& InGraph)
{
    (void)InGraph.Add(
        Ecs::Phase::FixedUpdate, "spriteAnimation", +[](Ecs::SystemContext& InContext, void*) { SpriteAnimationSystem{}.Run(InContext); });
}
}
