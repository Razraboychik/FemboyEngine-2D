// FemboyEngine2D - destructible object implementation.
#include "Engine/Physics/SdfDestructible.hpp"

#include "Engine/Core/SimMath.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <utility>

namespace Fby
{
namespace
{

// Binary op-list container. The ops travel as trivially copyable records, so the
// file is only valid for the same compiler layout; the magic, version and byte
// count guard the parse.
inline constexpr std::uint32_t kOpsVersion = 1;
inline constexpr char kOpsMagic[8] = {'F', 'B', 'Y', 'S', 'D', 'O', 'P', '1'};

struct SdfOpsHeader
{
    char Magic[8] = {'F', 'B', 'Y', 'S', 'D', 'O', 'P', '1'};
    std::uint32_t Version = kOpsVersion;
    std::uint32_t OpCount = 0;
    std::uint32_t OpBytes = 0;
    std::uint32_t Reserved = 0;
};

static_assert(sizeof(SdfOpsHeader) == 24, "SdfOpsHeader must stay 24 bytes");

[[nodiscard]] Vector2D RotatePoint(Vector2D InPoint, float InRadians) noexcept
{
    const float ScopedCos = Sim::Cos(InRadians);
    const float ScopedSin = Sim::Sin(InRadians);
    return Vector2D{InPoint.X * ScopedCos - InPoint.Y * ScopedSin, InPoint.X * ScopedSin + InPoint.Y * ScopedCos};
}

[[nodiscard]] float BoxDistance(Vector2D InPoint, Vector2D InCenter, Vector2D InHalfExtents, float InAngle) noexcept
{
    const Vector2D ScopedLocal = RotatePoint(InPoint - InCenter, -InAngle);
    const float ScopedDx = std::abs(ScopedLocal.X) - InHalfExtents.X;
    const float ScopedDy = std::abs(ScopedLocal.Y) - InHalfExtents.Y;
    const float ScopedOutsideX = std::max(ScopedDx, 0.0f);
    const float ScopedOutsideY = std::max(ScopedDy, 0.0f);
    return std::sqrt(ScopedOutsideX * ScopedOutsideX + ScopedOutsideY * ScopedOutsideY) +
           std::min(std::max(ScopedDx, ScopedDy), 0.0f);
}

[[nodiscard]] float CircleDistance(Vector2D InPoint, Vector2D InCenter, float InRadius) noexcept
{
    const Vector2D ScopedDelta = InPoint - InCenter;
    return std::sqrt(ScopedDelta.LengthSquared()) - InRadius;
}

[[nodiscard]] float OpDistance(const SdfCarveOp& InOp, Vector2D InPoint) noexcept
{
    if (InOp.Shape == SdfCarveShape::Circle)
    {
        return CircleDistance(InPoint, InOp.Center, InOp.Radius);
    }
    return BoxDistance(InPoint, InOp.Center, InOp.HalfExtents, InOp.Angle);
}

}

bool SdfDestructible::Configure(const SdfDestructibleDesc& InDesc) noexcept
{
    if (InDesc.Width < 2 || InDesc.Height < 2 || InDesc.CellSize.X <= 0.0f || InDesc.CellSize.Y <= 0.0f)
    {
        return false;
    }
    MDesc = InDesc;
    MSamples.assign(static_cast<std::size_t>(InDesc.Width) * InDesc.Height, InDesc.OutsideDistance);
    MField.samples = MSamples.data();
    MField.width = InDesc.Width;
    MField.height = InDesc.Height;
    MField.origin = InDesc.Origin;
    MField.cellSize = InDesc.CellSize;
    MField.outsideDistance = InDesc.OutsideDistance;
    MOps.assign(kMaxOps, SdfCarveOp{});
    MOpCount = 0;
    MDirtyBricksRebaked = 0;
    MVersion = 0;
    Rebuild();
    return true;
}

float SdfDestructible::Evaluate(Vector2D InWorld) const noexcept
{
    // The intact object is centred inside its grid unless a fractured piece
    // pinned the parent centre.
    const Vector2D ScopedCenter{
        MDesc.HasBaseCenter
            ? MDesc.BaseCenter
            : Vector2D{MDesc.Origin.X + static_cast<float>(MDesc.Width) * MDesc.CellSize.X * 0.5f,
                       MDesc.Origin.Y + static_cast<float>(MDesc.Height) * MDesc.CellSize.Y * 0.5f}};
    float ScopedDistance = MDesc.BaseShape == SdfCarveShape::Circle
                               ? CircleDistance(InWorld, ScopedCenter, MDesc.Radius)
                               : BoxDistance(InWorld, ScopedCenter, MDesc.HalfExtents, 0.0f);
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < MOpCount; ++ScopedIndex)
    {
        // Boolean subtraction is max(base, -carve).
        ScopedDistance = std::max(ScopedDistance, -OpDistance(MOps[ScopedIndex], InWorld));
    }
    return ScopedDistance;
}

void SdfDestructible::RebuildBricks() noexcept
{
    SdfBrickMap ScopedMap;
    if (ScopedMap.Build(MField, MDesc.NarrowBand))
    {
        MBricks = std::move(ScopedMap);
    }
}

void SdfDestructible::Rebuild() noexcept
{
    if (!IsConfigured())
    {
        return;
    }
    for (std::uint32_t ScopedY = 0; ScopedY < MDesc.Height; ++ScopedY)
    {
        for (std::uint32_t ScopedX = 0; ScopedX < MDesc.Width; ++ScopedX)
        {
            const Vector2D ScopedWorld{MDesc.Origin.X + static_cast<float>(ScopedX) * MDesc.CellSize.X,
                                       MDesc.Origin.Y + static_cast<float>(ScopedY) * MDesc.CellSize.Y};
            MSamples[static_cast<std::size_t>(ScopedY) * MDesc.Width + ScopedX] = Evaluate(ScopedWorld);
        }
    }
    RebuildBricks();
    ++MVersion;
}

bool SdfDestructible::Subtract(const SdfCarveOp& InOp) noexcept
{
    if (!IsConfigured() || MOpCount >= kMaxOps)
    {
        return false;
    }
    MOps[MOpCount++] = InOp;

    // Only the bricks under the carve rect change; the rest keeps its samples.
    const std::uint32_t ScopedBricksX = (MDesc.Width + SdfBrickSize - 1u) / SdfBrickSize;
    const std::uint32_t ScopedBricksY = (MDesc.Height + SdfBrickSize - 1u) / SdfBrickSize;
    const Vector2D ScopedExtent = InOp.Shape == SdfCarveShape::Circle
                                     ? Vector2D{InOp.Radius, InOp.Radius}
                                     : Vector2D{InOp.HalfExtents.X, InOp.HalfExtents.Y};
    // Subtracting a shape changes the field wherever the carve is closer than
    // the intact surface, which can reach as deep as the object itself. The
    // dirty rect therefore grows by the object reach, not only by the carve.
    const float ScopedObjectReach =
        MDesc.BaseShape == SdfCarveShape::Circle
            ? MDesc.Radius
            : std::sqrt(MDesc.HalfExtents.X * MDesc.HalfExtents.X + MDesc.HalfExtents.Y * MDesc.HalfExtents.Y);
    const float ScopedMargin = MDesc.NarrowBand + ScopedObjectReach + std::max(MDesc.CellSize.X, MDesc.CellSize.Y);
    const float ScopedMinX = InOp.Center.X - ScopedExtent.X - ScopedMargin;
    const float ScopedMaxX = InOp.Center.X + ScopedExtent.X + ScopedMargin;
    const float ScopedMinY = InOp.Center.Y - ScopedExtent.Y - ScopedMargin;
    const float ScopedMaxY = InOp.Center.Y + ScopedExtent.Y + ScopedMargin;
    const auto ScopedBrickX = [this](float InWorldX)
    {
        const float ScopedCell = (InWorldX - MDesc.Origin.X) / MDesc.CellSize.X;
        return static_cast<std::int32_t>(std::floor(ScopedCell)) / static_cast<std::int32_t>(SdfBrickSize);
    };
    const auto ScopedBrickY = [this](float InWorldY)
    {
        const float ScopedCell = (InWorldY - MDesc.Origin.Y) / MDesc.CellSize.Y;
        return static_cast<std::int32_t>(std::floor(ScopedCell)) / static_cast<std::int32_t>(SdfBrickSize);
    };
    const std::int32_t ScopedFirstX = std::max<std::int32_t>(ScopedBrickX(ScopedMinX), 0);
    const std::int32_t ScopedFirstY = std::max<std::int32_t>(ScopedBrickY(ScopedMinY), 0);
    const std::int32_t ScopedLastX =
        std::min<std::int32_t>(ScopedBrickX(ScopedMaxX), static_cast<std::int32_t>(ScopedBricksX) - 1);
    const std::int32_t ScopedLastY =
        std::min<std::int32_t>(ScopedBrickY(ScopedMaxY), static_cast<std::int32_t>(ScopedBricksY) - 1);
    if (ScopedLastX < ScopedFirstX || ScopedLastY < ScopedFirstY)
    {
        RebuildBricks();
        ++MVersion;
        return true;
    }
    MDirtyBricksRebaked = 0;
    for (std::int32_t ScopedBrickYIndex = ScopedFirstY; ScopedBrickYIndex <= ScopedLastY; ++ScopedBrickYIndex)
    {
        for (std::int32_t ScopedBrickXIndex = ScopedFirstX; ScopedBrickXIndex <= ScopedLastX; ++ScopedBrickXIndex)
        {
            ++MDirtyBricksRebaked;
            const std::uint32_t ScopedBaseX = static_cast<std::uint32_t>(ScopedBrickXIndex) * SdfBrickSize;
            const std::uint32_t ScopedBaseY = static_cast<std::uint32_t>(ScopedBrickYIndex) * SdfBrickSize;
            const std::uint32_t ScopedEndX = std::min(ScopedBaseX + SdfBrickSize, MDesc.Width);
            const std::uint32_t ScopedEndY = std::min(ScopedBaseY + SdfBrickSize, MDesc.Height);
            for (std::uint32_t ScopedY = ScopedBaseY; ScopedY < ScopedEndY; ++ScopedY)
            {
                for (std::uint32_t ScopedX = ScopedBaseX; ScopedX < ScopedEndX; ++ScopedX)
                {
                    const Vector2D ScopedWorld{MDesc.Origin.X + static_cast<float>(ScopedX) * MDesc.CellSize.X,
                                               MDesc.Origin.Y + static_cast<float>(ScopedY) * MDesc.CellSize.Y};
                    MSamples[static_cast<std::size_t>(ScopedY) * MDesc.Width + ScopedX] = Evaluate(ScopedWorld);
                }
            }
        }
    }
    RebuildBricks();
    ++MVersion;
    return true;
}

float SdfDestructible::Sample(Vector2D InWorld) const noexcept
{
    return MField.Sample(InWorld);
}

bool SdfDestructible::SaveOps(std::vector<std::byte>* OutBytes) const
{
    if (OutBytes == nullptr || !IsConfigured())
    {
        return false;
    }
    SdfOpsHeader ScopedHeader;
    ScopedHeader.OpCount = MOpCount;
    ScopedHeader.OpBytes = static_cast<std::uint32_t>(MOpCount * sizeof(SdfCarveOp));
    const std::size_t ScopedTotal = sizeof(SdfOpsHeader) + ScopedHeader.OpBytes;
    OutBytes->assign(ScopedTotal, std::byte{0});
    std::memcpy(OutBytes->data(), &ScopedHeader, sizeof(ScopedHeader));
    if (ScopedHeader.OpBytes > 0)
    {
        std::memcpy(OutBytes->data() + sizeof(SdfOpsHeader), MOps.data(), ScopedHeader.OpBytes);
    }
    return true;
}

bool SdfDestructible::LoadOps(std::span<const std::byte> InBytes, std::string* InError) noexcept
{
    const auto ScopedFail = [InError](const char* InMessage)
    {
        if (InError != nullptr)
        {
            *InError = InMessage;
        }
        return false;
    };
    if (!IsConfigured())
    {
        return ScopedFail("sdf destructible: object is not configured");
    }
    if (InBytes.size() < sizeof(SdfOpsHeader))
    {
        return ScopedFail("sdf destructible: truncated op list");
    }
    SdfOpsHeader ScopedHeader{};
    std::memcpy(&ScopedHeader, InBytes.data(), sizeof(ScopedHeader));
    if (std::memcmp(ScopedHeader.Magic, kOpsMagic, sizeof(kOpsMagic)) != 0)
    {
        return ScopedFail("sdf destructible: wrong op-list magic");
    }
    if (ScopedHeader.Version != kOpsVersion)
    {
        return ScopedFail("sdf destructible: unsupported op-list version");
    }
    if (ScopedHeader.OpCount > kMaxOps)
    {
        return ScopedFail("sdf destructible: op count exceeds capacity");
    }
    if (ScopedHeader.OpBytes != ScopedHeader.OpCount * sizeof(SdfCarveOp))
    {
        return ScopedFail("sdf destructible: op byte count mismatch");
    }
    if (InBytes.size() != sizeof(SdfOpsHeader) + ScopedHeader.OpBytes)
    {
        return ScopedFail("sdf destructible: op-list size mismatch");
    }
    MOps.assign(kMaxOps, SdfCarveOp{});
    if (ScopedHeader.OpBytes > 0)
    {
        std::memcpy(MOps.data(), InBytes.data() + sizeof(SdfOpsHeader), ScopedHeader.OpBytes);
    }
    MOpCount = ScopedHeader.OpCount;
    Rebuild();
    return true;
}

}
