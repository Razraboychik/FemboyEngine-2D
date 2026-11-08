// FemboyEngine2D - world distance field owner implementation.
#include "Engine/Physics/SdfWorld.hpp"

#include "Engine/Core/SimMath.hpp"
#include "Engine/ECS/Components.hpp"
#include "Engine/ECS/Schedule.hpp"
#include "Engine/ECS/Tilemap.hpp"
#include "Engine/Physics/SdfDestructible.hpp"
#include "Engine/Physics/SdfPrimitives.hpp"
#include "Engine/Physics/SdfVolumeAsset.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace Fby
{
namespace
{

[[nodiscard]] float BoxDistance(Vector2D InPoint, Vector2D InCenter, Vector2D InHalfExtents) noexcept
{
    const float ScopedDx = std::abs(InPoint.X - InCenter.X) - InHalfExtents.X;
    const float ScopedDy = std::abs(InPoint.Y - InCenter.Y) - InHalfExtents.Y;
    const float ScopedOutsideX = std::max(ScopedDx, 0.0f);
    const float ScopedOutsideY = std::max(ScopedDy, 0.0f);
    const float ScopedOutside = std::sqrt(ScopedOutsideX * ScopedOutsideX + ScopedOutsideY * ScopedOutsideY);
    return ScopedOutside + std::min(std::max(ScopedDx, ScopedDy), 0.0f);
}

[[nodiscard]] Vector2D RotatePoint(Vector2D InPoint, float InRadians) noexcept
{
    const float ScopedCos = Sim::Cos(InRadians);
    const float ScopedSin = Sim::Sin(InRadians);
    return Vector2D{InPoint.X * ScopedCos - InPoint.Y * ScopedSin, InPoint.X * ScopedSin + InPoint.Y * ScopedCos};
}

}

bool SdfWorld::Configure(const SdfWorldDesc& InDesc) noexcept
{
    if (InDesc.Width < 2 || InDesc.Height < 2 || InDesc.CellSize.X <= 0.0f || InDesc.CellSize.Y <= 0.0f)
    {
        return false;
    }
    MDesc = InDesc;
    const std::size_t ScopedCells = static_cast<std::size_t>(InDesc.Width) * InDesc.Height;
    MSamples.assign(ScopedCells, InDesc.OutsideDistance);
    MField.samples = MSamples.data();
    MField.width = InDesc.Width;
    MField.height = InDesc.Height;
    MField.origin = InDesc.Origin;
    MField.cellSize = InDesc.CellSize;
    MField.outsideDistance = InDesc.OutsideDistance;

    MBoxes.clear();
    MCircles.clear();
    const std::uint32_t ScopedBricksX = (InDesc.Width + SdfBrickSize - 1u) / SdfBrickSize;
    const std::uint32_t ScopedBricksY = (InDesc.Height + SdfBrickSize - 1u) / SdfBrickSize;
    MDirtyFlags.assign(static_cast<std::size_t>(ScopedBricksX) * ScopedBricksY, 0);
    MDirtyBricks.clear();
    MBricks = SdfBrickMap{};
    MFieldVersion = 0;
    return true;
}

void SdfWorld::ClearSources() noexcept
{
    MBoxes.clear();
    MCircles.clear();
    MImprints.clear();
}

bool SdfWorld::AddBox(Vector2D InCenter, Vector2D InHalfExtents) noexcept
{
    if (MBoxes.size() >= kMaxBoxes)
    {
        return false;
    }
    MBoxes.push_back(SdfWorldBox{InCenter, InHalfExtents});
    const float ScopedMargin = MDesc.NarrowBand + std::max(MDesc.CellSize.X, MDesc.CellSize.Y);
    const Vector2D ScopedExtent{InHalfExtents.X + ScopedMargin, InHalfExtents.Y + ScopedMargin};
    MarkDirtyRect(InCenter - ScopedExtent, InCenter + ScopedExtent);
    return true;
}

bool SdfWorld::AddCircle(Vector2D InCenter, float InRadius) noexcept
{
    if (MCircles.size() >= kMaxCircles || InRadius <= 0.0f)
    {
        return false;
    }
    MCircles.push_back(SdfWorldCircle{InCenter, InRadius});
    const float ScopedMargin = InRadius + MDesc.NarrowBand + std::max(MDesc.CellSize.X, MDesc.CellSize.Y);
    MarkDirtyRect(Vector2D{InCenter.X - ScopedMargin, InCenter.Y - ScopedMargin},
                  Vector2D{InCenter.X + ScopedMargin, InCenter.Y + ScopedMargin});
    return true;
}

bool SdfWorld::BrickHasSources(Vector2D InMin, Vector2D InMax) const noexcept
{
    const float ScopedMargin = MDesc.NarrowBand;
    for (const SdfWorldBox& ScopedBox : MBoxes)
    {
        const float ScopedMinX = ScopedBox.Center.X - ScopedBox.HalfExtents.X - ScopedMargin;
        const float ScopedMaxX = ScopedBox.Center.X + ScopedBox.HalfExtents.X + ScopedMargin;
        const float ScopedMinY = ScopedBox.Center.Y - ScopedBox.HalfExtents.Y - ScopedMargin;
        const float ScopedMaxY = ScopedBox.Center.Y + ScopedBox.HalfExtents.Y + ScopedMargin;
        if (ScopedMaxX >= InMin.X && ScopedMinX <= InMax.X && ScopedMaxY >= InMin.Y && ScopedMinY <= InMax.Y)
        {
            return true;
        }
    }
    for (const SdfWorldCircle& ScopedCircle : MCircles)
    {
        const float ScopedRadius = ScopedCircle.Radius + ScopedMargin;
        const float ScopedMinX = ScopedCircle.Center.X - ScopedRadius;
        const float ScopedMaxX = ScopedCircle.Center.X + ScopedRadius;
        const float ScopedMinY = ScopedCircle.Center.Y - ScopedRadius;
        const float ScopedMaxY = ScopedCircle.Center.Y + ScopedRadius;
        if (ScopedMaxX >= InMin.X && ScopedMinX <= InMax.X && ScopedMaxY >= InMin.Y && ScopedMinY <= InMax.Y)
        {
            return true;
        }
    }
    return false;
}

float SdfWorld::EvaluateSources(Vector2D InWorld) const noexcept
{
    float ScopedDistance = MDesc.OutsideDistance;
    for (const SdfWorldBox& ScopedBox : MBoxes)
    {
        ScopedDistance = std::min(ScopedDistance, BoxDistance(InWorld, ScopedBox.Center, ScopedBox.HalfExtents));
    }
    for (const SdfWorldCircle& ScopedCircle : MCircles)
    {
        const Vector2D ScopedDelta = InWorld - ScopedCircle.Center;
        ScopedDistance = std::min(ScopedDistance, std::sqrt(ScopedDelta.LengthSquared()) - ScopedCircle.Radius);
    }
    for (const SdfWorldImprint& ScopedImprint : MImprints)
    {
        const float ScopedX = (InWorld.X - ScopedImprint.Origin.X) / ScopedImprint.CellSize.X;
        const float ScopedY = (InWorld.Y - ScopedImprint.Origin.Y) / ScopedImprint.CellSize.Y;
        if (ScopedX < 0.0f || ScopedY < 0.0f || ScopedX >= static_cast<float>(ScopedImprint.Width) ||
            ScopedY >= static_cast<float>(ScopedImprint.Height))
        {
            continue;
        }
        const std::uint32_t ScopedX0 = static_cast<std::uint32_t>(ScopedX);
        const std::uint32_t ScopedY0 = static_cast<std::uint32_t>(ScopedY);
        const std::uint32_t ScopedX1 = std::min(ScopedX0 + 1u, ScopedImprint.Width - 1u);
        const std::uint32_t ScopedY1 = std::min(ScopedY0 + 1u, ScopedImprint.Height - 1u);
        const float ScopedFx = ScopedX - static_cast<float>(ScopedX0);
        const float ScopedFy = ScopedY - static_cast<float>(ScopedY0);
        const auto ScopedAt = [&ScopedImprint](std::uint32_t InX, std::uint32_t InY)
        {
            return ScopedImprint.Samples[static_cast<std::size_t>(InY) * ScopedImprint.Width + InX];
        };
        const float ScopedTop = ScopedAt(ScopedX0, ScopedY0) +
                                (ScopedAt(ScopedX1, ScopedY0) - ScopedAt(ScopedX0, ScopedY0)) * ScopedFx;
        const float ScopedBottom = ScopedAt(ScopedX0, ScopedY1) +
                                   (ScopedAt(ScopedX1, ScopedY1) - ScopedAt(ScopedX0, ScopedY1)) * ScopedFx;
        ScopedDistance = std::min(ScopedDistance, ScopedTop + (ScopedBottom - ScopedTop) * ScopedFy);
    }
    return ScopedDistance;
}

bool SdfWorld::Imprint(const SdfDestructible& InObject) noexcept
{
    if (!IsConfigured() || !InObject.IsConfigured())
    {
        return false;
    }
    const SdfField& ScopedSource = InObject.Field();
    SdfWorldImprint ScopedImprint;
    ScopedImprint.Origin = ScopedSource.origin;
    ScopedImprint.CellSize = ScopedSource.cellSize;
    ScopedImprint.Width = ScopedSource.width;
    ScopedImprint.Height = ScopedSource.height;
    const std::size_t ScopedCount = static_cast<std::size_t>(ScopedSource.width) * ScopedSource.height;
    ScopedImprint.Samples.assign(ScopedSource.samples, ScopedSource.samples + ScopedCount);

    const Vector2D ScopedMax{ScopedImprint.Origin.X + static_cast<float>(ScopedImprint.Width) * ScopedImprint.CellSize.X,
                             ScopedImprint.Origin.Y + static_cast<float>(ScopedImprint.Height) * ScopedImprint.CellSize.Y};
    MImprints.push_back(std::move(ScopedImprint));
    MarkDirtyRect(ScopedSource.origin, ScopedMax);
    // The dirty bake recomputes the covered bricks from every source, so the new
    // imprint is part of the evaluation.
    (void)ApplyDirtyBake(kMaxBoxes);
    return true;
}

void SdfWorld::ClearImprints() noexcept
{
    if (MImprints.empty())
    {
        return;
    }
    MImprints.clear();
    MarkAllBricksDirty();
    (void)ApplyDirtyBake(kMaxBoxes);
}

void SdfWorld::RebuildBrickMap() noexcept
{
    SdfBrickMap ScopedMap;
    if (ScopedMap.Build(MField, MDesc.NarrowBand))
    {
        MBricks = std::move(ScopedMap);
    }
}

void SdfWorld::MarkAllBricksDirty() noexcept
{
    MDirtyBricks.clear();
    std::fill(MDirtyFlags.begin(), MDirtyFlags.end(), static_cast<std::uint8_t>(1));
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < MDirtyFlags.size(); ++ScopedIndex)
    {
        MDirtyBricks.push_back(ScopedIndex);
    }
}

void SdfWorld::MarkDirtyRect(Vector2D InMin, Vector2D InMax) noexcept
{
    if (!IsConfigured())
    {
        return;
    }
    const std::uint32_t ScopedBricksX = (MDesc.Width + SdfBrickSize - 1u) / SdfBrickSize;
    const std::uint32_t ScopedBricksY = (MDesc.Height + SdfBrickSize - 1u) / SdfBrickSize;
    if (InMax.X <= InMin.X || InMax.Y <= InMin.Y)
    {
        return;
    }
    const auto ScopedCellX = [this](float InWorldX)
    {
        const float ScopedCell = (InWorldX - MDesc.Origin.X) / MDesc.CellSize.X;
        return static_cast<std::int32_t>(std::floor(ScopedCell));
    };
    const auto ScopedCellY = [this](float InWorldY)
    {
        const float ScopedCell = (InWorldY - MDesc.Origin.Y) / MDesc.CellSize.Y;
        return static_cast<std::int32_t>(std::floor(ScopedCell));
    };
    std::int32_t ScopedMinX = ScopedCellX(InMin.X);
    std::int32_t ScopedMinY = ScopedCellY(InMin.Y);
    std::int32_t ScopedMaxX = ScopedCellX(InMax.X);
    std::int32_t ScopedMaxY = ScopedCellY(InMax.Y);
    if (ScopedMaxX < 0 || ScopedMaxY < 0 || ScopedMinX >= static_cast<std::int32_t>(MDesc.Width) ||
        ScopedMinY >= static_cast<std::int32_t>(MDesc.Height))
    {
        return;
    }
    ScopedMinX = std::max<std::int32_t>(ScopedMinX, 0);
    ScopedMinY = std::max<std::int32_t>(ScopedMinY, 0);
    ScopedMaxX = std::min<std::int32_t>(ScopedMaxX, static_cast<std::int32_t>(MDesc.Width) - 1);
    ScopedMaxY = std::min<std::int32_t>(ScopedMaxY, static_cast<std::int32_t>(MDesc.Height) - 1);
    const std::uint32_t ScopedFirstX = static_cast<std::uint32_t>(ScopedMinX) / SdfBrickSize;
    const std::uint32_t ScopedFirstY = static_cast<std::uint32_t>(ScopedMinY) / SdfBrickSize;
    const std::uint32_t ScopedLastX = static_cast<std::uint32_t>(ScopedMaxX) / SdfBrickSize;
    const std::uint32_t ScopedLastY = static_cast<std::uint32_t>(ScopedMaxY) / SdfBrickSize;
    for (std::uint32_t ScopedY = ScopedFirstY; ScopedY <= ScopedLastY && ScopedY < ScopedBricksY; ++ScopedY)
    {
        for (std::uint32_t ScopedX = ScopedFirstX; ScopedX <= ScopedLastX && ScopedX < ScopedBricksX; ++ScopedX)
        {
            const std::uint32_t ScopedIndex = ScopedY * ScopedBricksX + ScopedX;
            if (ScopedIndex < MDirtyFlags.size() && MDirtyFlags[ScopedIndex] == 0)
            {
                MDirtyFlags[ScopedIndex] = 1;
                MDirtyBricks.push_back(ScopedIndex);
            }
        }
    }
}

bool SdfWorld::BakeAll() noexcept
{
    if (!IsConfigured())
    {
        return false;
    }
    const std::uint32_t ScopedBricksX = (MDesc.Width + SdfBrickSize - 1u) / SdfBrickSize;
    const std::uint32_t ScopedBricksY = (MDesc.Height + SdfBrickSize - 1u) / SdfBrickSize;
    for (std::uint32_t ScopedBrickY = 0; ScopedBrickY < ScopedBricksY; ++ScopedBrickY)
    {
        for (std::uint32_t ScopedBrickX = 0; ScopedBrickX < ScopedBricksX; ++ScopedBrickX)
        {
            const std::uint32_t ScopedBaseX = ScopedBrickX * SdfBrickSize;
            const std::uint32_t ScopedBaseY = ScopedBrickY * SdfBrickSize;
            const std::uint32_t ScopedEndX = std::min(ScopedBaseX + SdfBrickSize, MDesc.Width);
            const std::uint32_t ScopedEndY = std::min(ScopedBaseY + SdfBrickSize, MDesc.Height);
            // A brick that no source reaches is uniform; skipping its cell loop
            // is what keeps a full bake of a large world inside the budget.
            const Vector2D ScopedMin{MDesc.Origin.X + static_cast<float>(ScopedBaseX) * MDesc.CellSize.X,
                                     MDesc.Origin.Y + static_cast<float>(ScopedBaseY) * MDesc.CellSize.Y};
            const Vector2D ScopedMax{MDesc.Origin.X + static_cast<float>(ScopedEndX) * MDesc.CellSize.X,
                                     MDesc.Origin.Y + static_cast<float>(ScopedEndY) * MDesc.CellSize.Y};
            const bool ScopedHasSources = BrickHasSources(ScopedMin, ScopedMax);
            for (std::uint32_t ScopedY = ScopedBaseY; ScopedY < ScopedEndY; ++ScopedY)
            {
                for (std::uint32_t ScopedX = ScopedBaseX; ScopedX < ScopedEndX; ++ScopedX)
                {
                    const std::size_t ScopedIndex = static_cast<std::size_t>(ScopedY) * MDesc.Width + ScopedX;
                    if (!ScopedHasSources)
                    {
                        MSamples[ScopedIndex] = MDesc.OutsideDistance;
                        continue;
                    }
                    const Vector2D ScopedWorld{
                        MDesc.Origin.X + static_cast<float>(ScopedX) * MDesc.CellSize.X,
                        MDesc.Origin.Y + static_cast<float>(ScopedY) * MDesc.CellSize.Y};
                    MSamples[ScopedIndex] = EvaluateSources(ScopedWorld);
                }
            }
        }
    }
    RebuildBrickMap();
    std::fill(MDirtyFlags.begin(), MDirtyFlags.end(), static_cast<std::uint8_t>(0));
    MDirtyBricks.clear();
    ++MFieldVersion;
    return true;
}

bool SdfWorld::BakeRegistry(Ecs::Registry& InRegistry) noexcept
{
    if (!IsConfigured())
    {
        return false;
    }
    auto ScopedView = InRegistry.view<Transform2D, SdfShapeComponent>();
    for (std::uint32_t ScopedY = 0; ScopedY < MDesc.Height; ++ScopedY)
    {
        for (std::uint32_t ScopedX = 0; ScopedX < MDesc.Width; ++ScopedX)
        {
            const Vector2D ScopedWorld{MDesc.Origin.X + static_cast<float>(ScopedX) * MDesc.CellSize.X,
                                       MDesc.Origin.Y + static_cast<float>(ScopedY) * MDesc.CellSize.Y};
            float ScopedDistance = MDesc.OutsideDistance;
            for (auto [ScopedEntity, ScopedTransform, ScopedShape] : ScopedView.each())
            {
                (void)ScopedEntity;
                const float ScopedScaleX = std::max(std::abs(ScopedTransform.Scale.X), 1.0e-4f);
                const float ScopedScaleY = std::max(std::abs(ScopedTransform.Scale.Y), 1.0e-4f);
                const Vector2D ScopedLocal = RotatePoint(ScopedWorld - ScopedTransform.Position, -ScopedTransform.Rotation);
                const Vector2D ScopedShapePoint{ScopedLocal.X / ScopedScaleX, ScopedLocal.Y / ScopedScaleY};
                // Exact per-cell distance in shape space, scaled back by the
                // smaller axis; non-uniform scale is an approximation by design.
                ScopedDistance =
                    std::min(ScopedDistance, SdfDistance(ScopedShape.Shape, ScopedShapePoint) *
                                                 std::min(ScopedScaleX, ScopedScaleY));
            }
            MSamples[static_cast<std::size_t>(ScopedY) * MDesc.Width + ScopedX] = ScopedDistance;
        }
    }
    RebuildBrickMap();
    std::fill(MDirtyFlags.begin(), MDirtyFlags.end(), static_cast<std::uint8_t>(0));
    MDirtyBricks.clear();
    ++MFieldVersion;
    return true;
}

bool SdfWorld::ApplyDirtyBake(std::uint32_t InMaxBricks) noexcept
{
    if (!IsConfigured())
    {
        return false;
    }
    if (MDirtyBricks.empty())
    {
        return true;
    }
    const std::uint32_t ScopedBricksX = (MDesc.Width + SdfBrickSize - 1u) / SdfBrickSize;
    const std::uint32_t ScopedCount = std::min<std::uint32_t>(InMaxBricks, static_cast<std::uint32_t>(MDirtyBricks.size()));
    for (std::uint32_t ScopedOrder = 0; ScopedOrder < ScopedCount; ++ScopedOrder)
    {
        const std::uint32_t ScopedIndex = MDirtyBricks[ScopedOrder];
        const std::uint32_t ScopedBrickX = ScopedIndex % ScopedBricksX;
        const std::uint32_t ScopedBrickY = ScopedIndex / ScopedBricksX;
        const std::uint32_t ScopedBaseX = ScopedBrickX * SdfBrickSize;
        const std::uint32_t ScopedBaseY = ScopedBrickY * SdfBrickSize;
        for (std::uint32_t ScopedY = 0; ScopedY < SdfBrickSize; ++ScopedY)
        {
            const std::uint32_t ScopedTargetY = ScopedBaseY + ScopedY;
            if (ScopedTargetY >= MDesc.Height)
            {
                break;
            }
            for (std::uint32_t ScopedX = 0; ScopedX < SdfBrickSize; ++ScopedX)
            {
                const std::uint32_t ScopedTargetX = ScopedBaseX + ScopedX;
                if (ScopedTargetX >= MDesc.Width)
                {
                    break;
                }
                const Vector2D ScopedWorld{
                    MDesc.Origin.X + static_cast<float>(ScopedTargetX) * MDesc.CellSize.X,
                    MDesc.Origin.Y + static_cast<float>(ScopedTargetY) * MDesc.CellSize.Y};
                MSamples[static_cast<std::size_t>(ScopedTargetY) * MDesc.Width + ScopedTargetX] =
                    EvaluateSources(ScopedWorld);
            }
        }
        if (ScopedIndex < MDirtyFlags.size())
        {
            MDirtyFlags[ScopedIndex] = 0;
        }
    }
    MDirtyBricks.erase(MDirtyBricks.begin(), MDirtyBricks.begin() + ScopedCount);
    // The sparse table is rebuilt from the updated dense field. Distance
    // evaluation is the incremental part; the table pass is linear and cheap.
    RebuildBrickMap();
    ++MFieldVersion;
    return MDirtyBricks.empty();
}

bool SdfWorld::AddTilemap(const TilemapComponent& InTilemap) noexcept
{
    if (!IsConfigured() || InTilemap.Width == 0 || InTilemap.Height == 0)
    {
        return false;
    }
    const Vector2D ScopedExtent = Grid::CellExtent(InTilemap.Grid);
    for (std::uint32_t ScopedY = 0; ScopedY < InTilemap.Height; ++ScopedY)
    {
        std::uint32_t ScopedX = 0;
        while (ScopedX < InTilemap.Width)
        {
            const Grid::Cell ScopedCell{static_cast<std::int32_t>(ScopedX), static_cast<std::int32_t>(ScopedY)};
            if (!Tilemap::IsSolid(InTilemap, ScopedCell))
            {
                ++ScopedX;
                continue;
            }
            const std::uint32_t ScopedFirst = ScopedX;
            while (ScopedX < InTilemap.Width &&
                   Tilemap::IsSolid(InTilemap,
                                    Grid::Cell{static_cast<std::int32_t>(ScopedX), static_cast<std::int32_t>(ScopedY)}))
            {
                ++ScopedX;
            }
            const Vector2D ScopedFirstCenter =
                Tilemap::CellToWorld(InTilemap, Grid::Cell{static_cast<std::int32_t>(ScopedFirst),
                                                           static_cast<std::int32_t>(ScopedY)});
            const Vector2D ScopedLastCenter =
                Tilemap::CellToWorld(InTilemap, Grid::Cell{static_cast<std::int32_t>(ScopedX - 1u),
                                                           static_cast<std::int32_t>(ScopedY)});
            const Vector2D ScopedCenter{(ScopedFirstCenter.X + ScopedLastCenter.X) * 0.5f, ScopedFirstCenter.Y};
            const Vector2D ScopedHalf{(ScopedLastCenter.X - ScopedFirstCenter.X + ScopedExtent.X) * 0.5f,
                                      ScopedExtent.Y * 0.5f};
            if (!AddBox(ScopedCenter, ScopedHalf))
            {
                return false;
            }
        }
    }
    return true;
}

bool SdfWorld::SaveAsset(std::vector<std::byte>* OutBytes) const
{
    SdfVolumeAsset ScopedAsset;
    if (!ScopedAsset.Build(MField, MDesc.NarrowBand))
    {
        return false;
    }
    return ScopedAsset.Save(OutBytes);
}

}
