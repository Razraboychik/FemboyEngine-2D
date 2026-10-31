// FemboyEngine2D - Gerstner background waves and the mdspan shallow-water grid.
#include "Engine/Physics/Fluid.hpp"

#include "Engine/Physics/Physics.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Fby
{
namespace
{

constexpr float kPi = 3.14159265358979323846f;

[[nodiscard]] Vector2D Normalize(Vector2D InValue) noexcept
{
    const float ScopedLengthSquared = InValue.LengthSquared();
    if (ScopedLengthSquared <= 1.0e-8f)
    {
        return Vector2D{1.0f, 0.0f};
    }
    return InValue * (1.0f / std::sqrt(ScopedLengthSquared));
}
}

// GerstnerField
void GerstnerField::Clear() noexcept
{
    MWaveCount = 0;
    MWaves.fill(GerstnerWave{});
}

std::uint32_t GerstnerField::SetWaves(std::span<const GerstnerWave> InWaves) noexcept
{
    MWaveCount = static_cast<std::uint32_t>(std::min<std::size_t>(InWaves.size(), kMaxWaves));
    for (std::uint32_t ScopedI = 0; ScopedI < MWaveCount; ++ScopedI)
    {
        GerstnerWave ScopedWave = InWaves[ScopedI];
        ScopedWave.direction = Normalize(ScopedWave.direction);
        ScopedWave.steepness = std::clamp(ScopedWave.steepness, 0.0f, 0.9f);
        ScopedWave.wavelength = std::max(ScopedWave.wavelength, 1.0f);
        ScopedWave.speed = std::max(ScopedWave.speed, 0.01f);
        MWaves[ScopedI] = ScopedWave;
    }
    return MWaveCount;
}

GerstnerSample GerstnerField::Sample(Vector2D InBase, float InTimeSeconds) const noexcept
{
    GerstnerSample ScopedResult{};
    float ScopedJxx = 1.0f;
    float ScopedJyy = 1.0f;
    float ScopedJxy = 0.0f;

    for (std::uint32_t ScopedI = 0; ScopedI < MWaveCount; ++ScopedI)
    {
        const GerstnerWave& ScopedWave = MWaves[ScopedI];
        const float ScopedWaveNumber = 2.0f * kPi / ScopedWave.wavelength;
        const float ScopedAngularSpeed = std::sqrt(kGravity * ScopedWaveNumber) * ScopedWave.speed;
        const float ScopedPhase =
            ScopedWaveNumber * ScopedWave.direction.Dot(InBase) - ScopedAngularSpeed * InTimeSeconds + ScopedWave.phase;
        const float ScopedSine = std::sin(ScopedPhase);
        const float ScopedCosine = std::cos(ScopedPhase);
        // Q/k is the horizontal particle radius. Q is exposed as steepness so
        const float ScopedAmplitude = ScopedWave.steepness / ScopedWaveNumber;

        ScopedResult.displacement.X += ScopedWave.direction.X * ScopedAmplitude * ScopedCosine;
        ScopedResult.displacement.Y += ScopedWave.direction.Y * ScopedAmplitude * ScopedCosine;
        ScopedResult.height += ScopedAmplitude * ScopedSine;

        const float ScopedQ = ScopedWave.steepness * ScopedSine;
        ScopedJxx -= ScopedQ * ScopedWave.direction.X * ScopedWave.direction.X;
        ScopedJyy -= ScopedQ * ScopedWave.direction.Y * ScopedWave.direction.Y;
        ScopedJxy -= ScopedQ * ScopedWave.direction.X * ScopedWave.direction.Y;
    }

    // The Jacobian of the horizontal displacement. A value below ~0.65 is a
    ScopedResult.jacobian = ScopedJxx * ScopedJyy - ScopedJxy * ScopedJxy;
    return ScopedResult;
}

float GerstnerField::Height(Vector2D InBase, float InTimeSeconds) const noexcept
{
    float ScopedHeight = 0.0f;
    for (std::uint32_t ScopedI = 0; ScopedI < MWaveCount; ++ScopedI)
    {
        const GerstnerWave& ScopedWave = MWaves[ScopedI];
        const float ScopedWaveNumber = 2.0f * kPi / ScopedWave.wavelength;
        const float ScopedAngularSpeed = std::sqrt(kGravity * ScopedWaveNumber) * ScopedWave.speed;
        const float ScopedPhase =
            ScopedWaveNumber * ScopedWave.direction.Dot(InBase) - ScopedAngularSpeed * InTimeSeconds + ScopedWave.phase;
        ScopedHeight += (ScopedWave.steepness / ScopedWaveNumber) * std::sin(ScopedPhase);
    }
    return ScopedHeight;
}

// ShallowWaterGrid
bool ShallowWaterGrid::Configure(const FluidDesc& InDesc) noexcept
{
    if (InDesc.columns < 2 || InDesc.columns > kMaxWaterColumns || InDesc.rows < 2 || InDesc.rows > kMaxWaterRows ||
        InDesc.cellSize.X <= 0.0f || InDesc.cellSize.Y <= 0.0f || InDesc.baseDepth <= 0.0f || InDesc.gravity <= 0.0f)
    {
        MColumns = 0;
        MRows = 0;
        return false;
    }
    MDesc = InDesc;
    MDesc.maxHeight = std::max(MDesc.maxHeight, 1.0f);
    MDesc.dropRadius = std::max(MDesc.dropRadius, 0.5f);
    MDesc.damping = std::clamp(MDesc.damping, 0.5f, 0.9999f);
    MColumns = InDesc.columns;
    MRows = InDesc.rows;
    Clear();
    return true;
}

void ShallowWaterGrid::Clear() noexcept
{
    MHeight.fill(0.0f);
    MHeightNext.fill(0.0f);
    MVelocityX.fill(0.0f);
    MVelocityY.fill(0.0f);
}

float ShallowWaterGrid::CellHeight(std::uint32_t InX, std::uint32_t InY) const noexcept
{
    return MHeight[static_cast<std::size_t>(InY) * MColumns + InX];
}

void ShallowWaterGrid::Step(float InDt) noexcept
{
    if (!IsConfigured() || !(InDt > 0.0f))
    {
        return;
    }

    const float ScopedDx = MDesc.cellSize.X;
    const float ScopedDy = MDesc.cellSize.Y;
    const float ScopedWaveSpeed = std::sqrt(MDesc.gravity * MDesc.baseDepth);
    const float ScopedMaxDt = 0.45f * std::min(ScopedDx, ScopedDy) / std::max(ScopedWaveSpeed, 0.001f);
    const float ScopedStep = std::min(InDt, ScopedMaxDt);
    const float ScopedDamping = std::pow(MDesc.damping, ScopedStep * 60.0f);
    const float ScopedVelocityLimit = std::max(4.0f * ScopedWaveSpeed, 100.0f);

    for (std::uint32_t ScopedY = 1; ScopedY + 1 < MRows; ++ScopedY)
    {
        for (std::uint32_t ScopedX = 1; ScopedX + 1 < MColumns; ++ScopedX)
        {
            const std::size_t ScopedIndex = static_cast<std::size_t>(ScopedY) * MColumns + ScopedX;
            const float ScopedHeightLeft = MHeight[ScopedIndex - 1];
            const float ScopedHeightRight = MHeight[ScopedIndex + 1];
            const float ScopedHeightDown = MHeight[ScopedIndex - MColumns];
            const float ScopedHeightUp = MHeight[ScopedIndex + MColumns];
            float ScopedVx = MVelocityX[ScopedIndex] -
                             MDesc.gravity * ScopedStep * (ScopedHeightRight - ScopedHeightLeft) / (2.0f * ScopedDx);
            float ScopedVy = MVelocityY[ScopedIndex] -
                             MDesc.gravity * ScopedStep * (ScopedHeightUp - ScopedHeightDown) / (2.0f * ScopedDy);
            ScopedVx *= ScopedDamping;
            ScopedVy *= ScopedDamping;
            ScopedVx = std::clamp(ScopedVx, -ScopedVelocityLimit, ScopedVelocityLimit);
            ScopedVy = std::clamp(ScopedVy, -ScopedVelocityLimit, ScopedVelocityLimit);
            MVelocityX[ScopedIndex] = ScopedVx;
            MVelocityY[ScopedIndex] = ScopedVy;
        }
    }

    const float ScopedMinHeight = -MDesc.baseDepth * 0.9f;
    const float ScopedMaxHeight = MDesc.maxHeight;
    for (std::uint32_t ScopedY = 0; ScopedY < MRows; ++ScopedY)
    {
        for (std::uint32_t ScopedX = 0; ScopedX < MColumns; ++ScopedX)
        {
            const std::size_t ScopedIndex = static_cast<std::size_t>(ScopedY) * MColumns + ScopedX;
            if (ScopedX == 0 || ScopedY == 0 || ScopedX + 1 >= MColumns || ScopedY + 1 >= MRows)
            {
                // Reflective wall: freeze the boundary height and zero normal
                MHeightNext[ScopedIndex] = MHeight[ScopedIndex];
                MVelocityX[ScopedIndex] = 0.0f;
                MVelocityY[ScopedIndex] = 0.0f;
                continue;
            }

            const float ScopedVxLeft = MVelocityX[ScopedIndex - 1];
            const float ScopedVxRight = MVelocityX[ScopedIndex + 1];
            const float ScopedVyDown = MVelocityY[ScopedIndex - MColumns];
            const float ScopedVyUp = MVelocityY[ScopedIndex + MColumns];
            const float ScopedHeightLeft = MHeight[ScopedIndex - 1];
            const float ScopedHeightRight = MHeight[ScopedIndex + 1];
            const float ScopedHeightDown = MHeight[ScopedIndex - MColumns];
            const float ScopedHeightUp = MHeight[ScopedIndex + MColumns];

            const float ScopedDivergence =
                (ScopedVxRight - ScopedVxLeft) / (2.0f * ScopedDx) + (ScopedVyUp - ScopedVyDown) / (2.0f * ScopedDy);
            const float ScopedAdvection =
                MVelocityX[ScopedIndex] * (ScopedHeightRight - ScopedHeightLeft) / (2.0f * ScopedDx) +
                MVelocityY[ScopedIndex] * (ScopedHeightUp - ScopedHeightDown) / (2.0f * ScopedDy);
            float ScopedNext =
                MHeight[ScopedIndex] - ScopedStep * (MDesc.baseDepth * ScopedDivergence + ScopedAdvection);
            MHeightNext[ScopedIndex] = std::clamp(ScopedNext, ScopedMinHeight, ScopedMaxHeight);
        }
    }
    MHeight.swap(MHeightNext);
}

void ShallowWaterGrid::AddDrop(Vector2D InWorldPosition, float InRadiusCells, float InStrength) noexcept
{
    if (!IsConfigured() || !(InStrength > 0.0f))
    {
        return;
    }
    const Vector2D ScopedCell = WorldToCell(InWorldPosition);
    const float ScopedRadius = std::max(InRadiusCells, 0.5f);
    const float ScopedSigma = std::max(ScopedRadius * 0.5f, 0.25f);
    const float ScopedTwoSigmaSquared = 2.0f * ScopedSigma * ScopedSigma;
    const int ScopedMinX = std::max(0, static_cast<int>(std::floor(ScopedCell.X - 3.0f * ScopedSigma)));
    const int ScopedMaxX =
        std::min(static_cast<int>(MColumns) - 1, static_cast<int>(std::ceil(ScopedCell.X + 3.0f * ScopedSigma)));
    const int ScopedMinY = std::max(0, static_cast<int>(std::floor(ScopedCell.Y - 3.0f * ScopedSigma)));
    const int ScopedMaxY =
        std::min(static_cast<int>(MRows) - 1, static_cast<int>(std::ceil(ScopedCell.Y + 3.0f * ScopedSigma)));
    if (ScopedMinX > ScopedMaxX || ScopedMinY > ScopedMaxY)
    {
        return;
    }

    for (int ScopedY = ScopedMinY; ScopedY <= ScopedMaxY; ++ScopedY)
    {
        for (int ScopedX = ScopedMinX; ScopedX <= ScopedMaxX; ++ScopedX)
        {
            const float ScopedDx = static_cast<float>(ScopedX) - ScopedCell.X;
            const float ScopedDy = static_cast<float>(ScopedY) - ScopedCell.Y;
            const float ScopedWeight = std::exp(-(ScopedDx * ScopedDx + ScopedDy * ScopedDy) / ScopedTwoSigmaSquared);
            const std::size_t ScopedIndex =
                static_cast<std::size_t>(ScopedY) * MColumns + static_cast<std::size_t>(ScopedX);
            MHeight[ScopedIndex] =
                std::clamp(MHeight[ScopedIndex] - InStrength * ScopedWeight, -MDesc.baseDepth * 0.9f, MDesc.maxHeight);
            MVelocityY[ScopedIndex] =
                std::clamp(MVelocityY[ScopedIndex] + InStrength * ScopedWeight * 18.0f, -4000.0f, 4000.0f);
        }
    }
}

float ShallowWaterGrid::SampleHeight(Vector2D InWorldPosition) const noexcept
{
    if (!IsConfigured())
    {
        return 0.0f;
    }
    const Vector2D ScopedCell = WorldToCell(InWorldPosition);
    const float ScopedX = std::clamp(ScopedCell.X, 0.0f, static_cast<float>(MColumns - 1));
    const float ScopedY = std::clamp(ScopedCell.Y, 0.0f, static_cast<float>(MRows - 1));
    const std::uint32_t ScopedX0 = static_cast<std::uint32_t>(ScopedX);
    const std::uint32_t ScopedY0 = static_cast<std::uint32_t>(ScopedY);
    const std::uint32_t ScopedX1 = std::min(ScopedX0 + 1, MColumns - 1);
    const std::uint32_t ScopedY1 = std::min(ScopedY0 + 1, MRows - 1);
    const float ScopedTx = ScopedX - static_cast<float>(ScopedX0);
    const float ScopedTy = ScopedY - static_cast<float>(ScopedY0);
    const float ScopedTop =
        CellHeight(ScopedX0, ScopedY0) + (CellHeight(ScopedX1, ScopedY0) - CellHeight(ScopedX0, ScopedY0)) * ScopedTx;
    const float ScopedBottom =
        CellHeight(ScopedX0, ScopedY1) + (CellHeight(ScopedX1, ScopedY1) - CellHeight(ScopedX0, ScopedY1)) * ScopedTx;
    return ScopedTop + (ScopedBottom - ScopedTop) * ScopedTy;
}

float ShallowWaterGrid::SampleVelocityY(Vector2D InWorldPosition) const noexcept
{
    if (!IsConfigured())
    {
        return 0.0f;
    }
    const Vector2D ScopedCell = WorldToCell(InWorldPosition);
    const std::uint32_t ScopedX =
        static_cast<std::uint32_t>(std::clamp(ScopedCell.X, 0.0f, static_cast<float>(MColumns - 1)));
    const std::uint32_t ScopedY =
        static_cast<std::uint32_t>(std::clamp(ScopedCell.Y, 0.0f, static_cast<float>(MRows - 1)));
    return MVelocityY[static_cast<std::size_t>(ScopedY) * MColumns + ScopedX];
}

std::uint32_t ShallowWaterGrid::CopyHeightField(std::span<float> InDestination) const noexcept
{
    if (!IsConfigured() || InDestination.empty())
    {
        return 0;
    }
    const std::size_t ScopedCount = std::min(InDestination.size(), static_cast<std::size_t>(MColumns) * MRows);
    std::copy_n(MHeight.data(), ScopedCount, InDestination.data());
    return static_cast<std::uint32_t>(ScopedCount);
}

Vector2D ShallowWaterGrid::WorldToCell(Vector2D InWorldPosition) const noexcept
{
    return Vector2D{(InWorldPosition.X - MDesc.origin.X) / MDesc.cellSize.X,
                    (InWorldPosition.Y - MDesc.origin.Y) / MDesc.cellSize.Y};
}

// FluidWaveSystem
void FluidWaveSystem::Initialize(const FluidDesc& InDesc) noexcept
{
    (void)MWater.Configure(InDesc);
    MTracked.fill(Tracked{});
    MTrackedCount = 0;
}

void FluidWaveSystem::Shutdown() noexcept
{
    MWater.Clear();
    MGerstner.Clear();
    MTracked.fill(Tracked{});
    MTrackedCount = 0;
}

FluidWaveSystem::Tracked* FluidWaveSystem::FindTracked(Entity InEntity) noexcept
{
    for (std::uint32_t ScopedI = 0; ScopedI < MTrackedCount; ++ScopedI)
    {
        if (MTracked[ScopedI].entity == InEntity)
        {
            return &MTracked[ScopedI];
        }
    }
    return nullptr;
}

void FluidWaveSystem::AddTracked(Entity InEntity) noexcept
{
    if (MTrackedCount < kMaxTrackedInteractors)
    {
        MTracked[MTrackedCount++] = Tracked{InEntity, false};
    }
}

void FluidWaveSystem::Update(Ecs::Registry& InRegistry, float InDt) noexcept
{
    if (!MWater.IsConfigured())
    {
        return;
    }

    // Drop tracking for entities gameplay destroyed. Swap-remove keeps the
    for (std::uint32_t ScopedI = 0; ScopedI < MTrackedCount;)
    {
        if (!InRegistry.valid(MTracked[ScopedI].entity))
        {
            MTracked[ScopedI] = MTracked[MTrackedCount - 1];
            --MTrackedCount;
            continue;
        }
        ++ScopedI;
    }

    const FluidDesc& ScopedDesc = MWater.Desc();
    const Vector2D ScopedWaterMin = ScopedDesc.origin;
    const Vector2D ScopedWaterMax{ScopedDesc.origin.X + ScopedDesc.cellSize.X * static_cast<float>(MWater.Columns()),
                                  ScopedDesc.origin.Y + ScopedDesc.cellSize.Y * static_cast<float>(MWater.Rows())};

    auto ScopedView = InRegistry.view<FluidInteractorComponent, Transform2D>();
    for (auto [ScopedEntity, ScopedInteractor, ScopedTransform] : ScopedView.each())
    {
        (void)ScopedInteractor;
        const bool ScopedInside =
            ScopedTransform.Position.X >= ScopedWaterMin.X && ScopedTransform.Position.X < ScopedWaterMax.X &&
            ScopedTransform.Position.Y >= ScopedWaterMin.Y && ScopedTransform.Position.Y < ScopedWaterMax.Y;
        Tracked* ScopedTracked = FindTracked(ScopedEntity);
        if (ScopedTracked == nullptr)
        {
            AddTracked(ScopedEntity);
            ScopedTracked = FindTracked(ScopedEntity);
            if (ScopedTracked == nullptr)
            {
                // Tracking table full; skip rather than allocate
                continue;
            }
        }

        if (ScopedInside && !ScopedTracked->inside)
        {
            float ScopedVelocityY = 0.0f;
            if (const auto* ScopedBody = InRegistry.try_get<RigidBody2D>(ScopedEntity); ScopedBody != nullptr)
            {
                ScopedVelocityY = ScopedBody->LinearVelocity.Y;
            }
            const float ScopedImpact = 1.0f + std::max(ScopedVelocityY, 0.0f) / 500.0f;
            MWater.AddDrop(ScopedTransform.Position, ScopedInteractor.RadiusCells,
                           ScopedInteractor.SplashStrength * ScopedInteractor.Mass * ScopedImpact *
                               ScopedDesc.dropStrength);
        }
        ScopedTracked->inside = ScopedInside;
    }

    MWater.Step(InDt);
}
}
