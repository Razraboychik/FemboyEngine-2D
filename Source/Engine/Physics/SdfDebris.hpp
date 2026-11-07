// FemboyEngine2D - deterministic debris spawn for a carve.
#pragma once

#include "Engine/Core/Pcg32.hpp"
#include "Engine/Core/SimMath.hpp"
#include "Engine/Core/Types.hpp"
#include "Engine/Physics/SdfDestructible.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>

namespace Fby
{

// One flying fragment. The renderer and the physics both consume this POD; the
// simulation keeps it deterministic by deriving it only from the carve and the
// seed.
struct SdfDebrisPiece
{
    Vector2D Position{};
    Vector2D Velocity{};
    Vector2D HalfExtents{0.5f, 0.5f};
    float Rotation = 0.0f;
    float Lifetime = 0.0f;
};

static_assert(std::is_trivially_copyable_v<SdfDebrisPiece>);

struct SdfDebrisSettings
{
    std::uint32_t MaxPieces = 24;
    // Sample stride in cells; 1 makes a fragment per inside cell.
    std::uint32_t Stride = 2;
    float SpeedMin = 40.0f;
    float SpeedMax = 160.0f;
    float Lifetime = 1.5f;
    float HalfExtent = 1.0f;
    std::uint64_t Seed = 1;
};

[[nodiscard]] inline float DebrisCarveDistance(const SdfCarveOp& InOp, Vector2D InPoint) noexcept
{
    if (InOp.Shape == SdfCarveShape::Circle)
    {
        const Vector2D ScopedDelta = InPoint - InOp.Center;
        return std::sqrt(ScopedDelta.LengthSquared()) - InOp.Radius;
    }
    const Vector2D ScopedDelta = InPoint - InOp.Center;
    const float ScopedCos = Sim::Cos(InOp.Angle);
    const float ScopedSin = Sim::Sin(InOp.Angle);
    const Vector2D ScopedLocal{ScopedDelta.X * ScopedCos + ScopedDelta.Y * ScopedSin,
                               -ScopedDelta.X * ScopedSin + ScopedDelta.Y * ScopedCos};
    const float ScopedDx = std::abs(ScopedLocal.X) - InOp.HalfExtents.X;
    const float ScopedDy = std::abs(ScopedLocal.Y) - InOp.HalfExtents.Y;
    const float ScopedOutsideX = std::max(ScopedDx, 0.0f);
    const float ScopedOutsideY = std::max(ScopedDy, 0.0f);
    return std::sqrt(ScopedOutsideX * ScopedOutsideX + ScopedOutsideY * ScopedOutsideY) +
           std::min(std::max(ScopedDx, ScopedDy), 0.0f);
}

// Spawns fragments for the material a carve removes. Points inside the carve shape
// on a fixed stride become pieces, so the same carve and seed always produce the
// same array: no unordered container and no wall clock in the path.
[[nodiscard]] inline std::uint32_t SpawnDebris(const SdfDestructible& InObject, const SdfCarveOp& InOp,
                                               const SdfDebrisSettings& InSettings,
                                               std::span<SdfDebrisPiece> OutPieces) noexcept
{
    if (!InObject.IsConfigured() || OutPieces.empty() || InSettings.MaxPieces == 0)
    {
        return 0;
    }
    const SdfField& ScopedField = InObject.Field();
    const float ScopedExtentX = InOp.Shape == SdfCarveShape::Circle ? InOp.Radius : InOp.HalfExtents.X;
    const float ScopedExtentY = InOp.Shape == SdfCarveShape::Circle ? InOp.Radius : InOp.HalfExtents.Y;
    const float ScopedMinX = std::max(InOp.Center.X - ScopedExtentX, ScopedField.origin.X);
    const float ScopedMaxX = std::min(InOp.Center.X + ScopedExtentX,
                                      ScopedField.origin.X + static_cast<float>(ScopedField.width) * ScopedField.cellSize.X);
    const float ScopedMinY = std::max(InOp.Center.Y - ScopedExtentY, ScopedField.origin.Y);
    const float ScopedMaxY = std::min(InOp.Center.Y + ScopedExtentY,
                                      ScopedField.origin.Y + static_cast<float>(ScopedField.height) * ScopedField.cellSize.Y);
    const float ScopedStepX = ScopedField.cellSize.X * static_cast<float>(std::max(InSettings.Stride, 1u));
    const float ScopedStepY = ScopedField.cellSize.Y * static_cast<float>(std::max(InSettings.Stride, 1u));

    // First pass counts the candidates so the cap can subsample deterministically.
    std::uint32_t ScopedCandidates = 0;
    for (float ScopedY = ScopedMinY; ScopedY <= ScopedMaxY; ScopedY += ScopedStepY)
    {
        for (float ScopedX = ScopedMinX; ScopedX <= ScopedMaxX; ScopedX += ScopedStepX)
        {
            if (DebrisCarveDistance(InOp, Vector2D{ScopedX, ScopedY}) <= 0.0f)
            {
                ++ScopedCandidates;
            }
        }
    }
    if (ScopedCandidates == 0)
    {
        return 0;
    }
    const std::uint32_t ScopedLimit = std::min({ScopedCandidates, InSettings.MaxPieces,
                                                static_cast<std::uint32_t>(OutPieces.size())});
    const std::uint32_t ScopedSkip = std::max(ScopedCandidates / ScopedLimit, 1u);
    const float ScopedSpeedRange = std::max(InSettings.SpeedMax - InSettings.SpeedMin, 0.0f);

    std::uint32_t ScopedWritten = 0;
    std::uint32_t ScopedIndex = 0;
    Pcg32 ScopedRng = Pcg32::FromSeed(InSettings.Seed);
    for (float ScopedY = ScopedMinY; ScopedY <= ScopedMaxY && ScopedWritten < ScopedLimit; ScopedY += ScopedStepY)
    {
        for (float ScopedX = ScopedMinX; ScopedX <= ScopedMaxX && ScopedWritten < ScopedLimit; ScopedX += ScopedStepX)
        {
            if (DebrisCarveDistance(InOp, Vector2D{ScopedX, ScopedY}) > 0.0f)
            {
                continue;
            }
            const std::uint32_t ScopedOrdinal = ScopedIndex++;
            if (ScopedOrdinal % ScopedSkip != 0u)
            {
                continue;
            }
            const Vector2D ScopedPoint{ScopedX, ScopedY};
            Vector2D ScopedDirection = ScopedPoint - InOp.Center;
            if (ScopedDirection.LengthSquared() <= 1.0e-6f)
            {
                ScopedDirection = Vector2D{1.0f, 0.0f};
            }
            const float ScopedLength = std::sqrt(ScopedDirection.LengthSquared());
            ScopedDirection = ScopedDirection / ScopedLength;
            const float ScopedSpeed =
                InSettings.SpeedMin + ScopedSpeedRange * static_cast<float>(ScopedRng.NextU32() % 1000u) / 1000.0f;
            const float ScopedRotation =
                6.28318530718f * static_cast<float>(ScopedRng.NextU32() % 1000u) / 1000.0f;
            SdfDebrisPiece& ScopedPiece = OutPieces[ScopedWritten++];
            ScopedPiece.Position = ScopedPoint;
            ScopedPiece.Velocity = ScopedDirection * ScopedSpeed;
            ScopedPiece.HalfExtents = Vector2D{InSettings.HalfExtent, InSettings.HalfExtent};
            ScopedPiece.Rotation = ScopedRotation;
            ScopedPiece.Lifetime = InSettings.Lifetime;
        }
    }
    return ScopedWritten;
}

}
