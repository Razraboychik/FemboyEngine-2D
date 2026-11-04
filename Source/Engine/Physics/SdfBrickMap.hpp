// FemboyEngine2D - sparse SDF brick map for CPU queries and GPU mirrors.
#pragma once

#include "Engine/Core/Types.hpp"
#include "Engine/Physics/SdfCollision.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <type_traits>
#include <vector>

namespace Fby
{

// One brick covers a square block of the source SDF grid.
inline constexpr std::uint32_t SdfBrickSize = 16u;
inline constexpr std::uint32_t SdfBrickTexels = SdfBrickSize * SdfBrickSize;

// Empty bricks are far outside, full bricks are fully solid, and narrow bricks
// carry a 16x16 distance payload.
enum class SdfBrickState : std::uint8_t
{
    Empty = 0,
    Full = 1,
    Narrow = 2,
};

// One table entry. PayloadOffset indexes the float pool in texel units.
struct SdfBrickHeader
{
    std::uint32_t PayloadOffset = 0xFFFFFFFFu;
    float MinDistance = 1.0e20f;
    float MaxDistance = -1.0e20f;
    std::uint32_t BrickX = 0;
    std::uint32_t BrickY = 0;
    SdfBrickState State = SdfBrickState::Empty;
    std::uint8_t Reserved0 = 0;
    std::uint16_t Reserved1 = 0;
};

static_assert(sizeof(SdfBrickHeader) == 24);

// GPU-facing mirror of one brick header. The state occupies the low two bits of
// StateAndFlags; the remaining bits are reserved.
struct SdfBrickGpu
{
    std::uint32_t PayloadOffset = 0xFFFFFFFFu;
    float MinDistance = 1.0e20f;
    float MaxDistance = -1.0e20f;
    std::uint32_t BrickX = 0;
    std::uint32_t BrickY = 0;
    std::uint32_t StateAndFlags = 0;
    std::uint64_t Reserved = 0;
};

static_assert(sizeof(SdfBrickGpu) == 32);

// Everything a serialized brick map needs to describe its source grid. The
// table and payload travel separately; this record is the header contract.
struct SdfBrickMapDesc
{
    Vector2D Origin{};
    Vector2D CellSize{1.0f, 1.0f};
    std::uint32_t SourceWidth = 0;
    std::uint32_t SourceHeight = 0;
    std::uint32_t BricksX = 0;
    std::uint32_t BricksY = 0;
    float OutsideDistance = 1.0e6f;
    float NarrowBand = 1.0f;
};

static_assert(std::is_trivially_copyable_v<SdfBrickMapDesc>);

// A sparse representation of a dense SdfField. The table is dense per brick
// coordinate; the payload pool is allocated only for narrow-band bricks.
class SdfBrickMap
{
public:
    [[nodiscard]] bool Build(const SdfField& InField, float InNarrowBand = 0.0f) noexcept
    {
        if (!InField.Valid())
        {
            return false;
        }
        MOrigin = InField.origin;
        MCellSize = InField.cellSize;
        MSourceWidth = InField.width;
        MSourceHeight = InField.height;
        MOutsideDistance = InField.outsideDistance;
        MBricksX = (InField.width + SdfBrickSize - 1u) / SdfBrickSize;
        MBricksY = (InField.height + SdfBrickSize - 1u) / SdfBrickSize;
        MNarrowBand = InNarrowBand > 0.0f
                          ? InNarrowBand
                          : std::max(MCellSize.X, MCellSize.Y) * static_cast<float>(SdfBrickSize);
        MBricks.assign(static_cast<std::size_t>(MBricksX) * MBricksY, SdfBrickHeader{});
        MPayload.clear();
        MPayload.reserve(static_cast<std::size_t>(InField.width) * InField.height);

        for (std::uint32_t ScopedBrickY = 0; ScopedBrickY < MBricksY; ++ScopedBrickY)
        {
            for (std::uint32_t ScopedBrickX = 0; ScopedBrickX < MBricksX; ++ScopedBrickX)
            {
                SdfBrickHeader& ScopedHeader = BrickAt(ScopedBrickX, ScopedBrickY);
                ScopedHeader.BrickX = ScopedBrickX;
                ScopedHeader.BrickY = ScopedBrickY;
                float ScopedMin = std::numeric_limits<float>::max();
                float ScopedMax = std::numeric_limits<float>::lowest();
                for (std::uint32_t ScopedY = 0; ScopedY < SdfBrickSize; ++ScopedY)
                {
                    for (std::uint32_t ScopedX = 0; ScopedX < SdfBrickSize; ++ScopedX)
                    {
                        const std::uint32_t ScopedSourceX = ScopedBrickX * SdfBrickSize + ScopedX;
                        const std::uint32_t ScopedSourceY = ScopedBrickY * SdfBrickSize + ScopedY;
                        const float ScopedDistance = SourceDistance(InField, ScopedSourceX, ScopedSourceY);
                        ScopedMin = std::min(ScopedMin, ScopedDistance);
                        ScopedMax = std::max(ScopedMax, ScopedDistance);
                    }
                }
                ScopedHeader.MinDistance = ScopedMin;
                ScopedHeader.MaxDistance = ScopedMax;
                if (ScopedMin > MNarrowBand)
                {
                    ScopedHeader.State = SdfBrickState::Empty;
                    continue;
                }
                if (ScopedMax < 0.0f)
                {
                    ScopedHeader.State = SdfBrickState::Full;
                    continue;
                }
                ScopedHeader.State = SdfBrickState::Narrow;
                ScopedHeader.PayloadOffset = static_cast<std::uint32_t>(MPayload.size());
                for (std::uint32_t ScopedY = 0; ScopedY < SdfBrickSize; ++ScopedY)
                {
                    for (std::uint32_t ScopedX = 0; ScopedX < SdfBrickSize; ++ScopedX)
                    {
                        const std::uint32_t ScopedSourceX = ScopedBrickX * SdfBrickSize + ScopedX;
                        const std::uint32_t ScopedSourceY = ScopedBrickY * SdfBrickSize + ScopedY;
                        MPayload.push_back(SourceDistance(InField, ScopedSourceX, ScopedSourceY));
                    }
                }
            }
        }
        return true;
    }

    [[nodiscard]] float Sample(Vector2D InWorld) const noexcept
    {
        if (MBricks.empty() || MCellSize.X <= 0.0f || MCellSize.Y <= 0.0f)
        {
            return MOutsideDistance;
        }
        const float ScopedX = (InWorld.X - MOrigin.X) / MCellSize.X;
        const float ScopedY = (InWorld.Y - MOrigin.Y) / MCellSize.Y;
        if (ScopedX < 0.0f || ScopedY < 0.0f || ScopedX >= static_cast<float>(MSourceWidth) ||
            ScopedY >= static_cast<float>(MSourceHeight))
        {
            return MOutsideDistance;
        }
        const std::uint32_t ScopedBrickX = static_cast<std::uint32_t>(ScopedX) / SdfBrickSize;
        const std::uint32_t ScopedBrickY = static_cast<std::uint32_t>(ScopedY) / SdfBrickSize;
        const SdfBrickHeader& ScopedHeader = BrickAt(ScopedBrickX, ScopedBrickY);
        if (ScopedHeader.State == SdfBrickState::Empty)
        {
            return ScopedHeader.MinDistance;
        }
        if (ScopedHeader.State == SdfBrickState::Full)
        {
            return ScopedHeader.MaxDistance;
        }
        const float ScopedLocalX = ScopedX - static_cast<float>(ScopedBrickX * SdfBrickSize);
        const float ScopedLocalY = ScopedY - static_cast<float>(ScopedBrickY * SdfBrickSize);
        const std::uint32_t ScopedX0 = std::min(static_cast<std::uint32_t>(ScopedLocalX), SdfBrickSize - 1u);
        const std::uint32_t ScopedY0 = std::min(static_cast<std::uint32_t>(ScopedLocalY), SdfBrickSize - 1u);
        const std::uint32_t ScopedX1 = std::min(ScopedX0 + 1u, SdfBrickSize - 1u);
        const std::uint32_t ScopedY1 = std::min(ScopedY0 + 1u, SdfBrickSize - 1u);
        const float ScopedFx = ScopedLocalX - static_cast<float>(ScopedX0);
        const float ScopedFy = ScopedLocalY - static_cast<float>(ScopedY0);
        const auto ScopedAt = [this, &ScopedHeader](std::uint32_t InX, std::uint32_t InY)
        {
            return MPayload[ScopedHeader.PayloadOffset + InY * SdfBrickSize + InX];
        };
        const float ScopedTop = ScopedAt(ScopedX0, ScopedY0) +
                                (ScopedAt(ScopedX1, ScopedY0) - ScopedAt(ScopedX0, ScopedY0)) * ScopedFx;
        const float ScopedBottom = ScopedAt(ScopedX0, ScopedY1) +
                                   (ScopedAt(ScopedX1, ScopedY1) - ScopedAt(ScopedX0, ScopedY1)) * ScopedFx;
        return ScopedTop + (ScopedBottom - ScopedTop) * ScopedFy;
    }

    // A bounded sphere trace with brick-level empty/full skipping. It is the
    // CPU reference for brick-map traversal.
    [[nodiscard]] float Trace(Vector2D InOrigin, Vector2D InDirection, float InMaxDistance,
                              std::uint32_t InMaxSteps) const noexcept
    {
        const float ScopedLength = std::sqrt(InDirection.LengthSquared());
        if (ScopedLength < 1.0e-6f)
        {
            return MOutsideDistance;
        }
        const Vector2D ScopedDirection = InDirection / ScopedLength;
        float ScopedTravel = 0.0f;
        for (std::uint32_t ScopedStep = 0; ScopedStep < InMaxSteps && ScopedTravel < InMaxDistance; ++ScopedStep)
        {
            const Vector2D ScopedPoint = InOrigin + ScopedDirection * ScopedTravel;
            const float ScopedDistance = Sample(ScopedPoint);
            if (ScopedDistance <= 0.0f)
            {
                return 0.0f;
            }
            ScopedTravel += std::max(ScopedDistance, std::max(MCellSize.X, MCellSize.Y));
        }
        // Zero means a hit. A ray that simply ran out of budget is still open,
        // so keep a small positive remainder instead of collapsing to hit.
        return std::max(InMaxDistance - ScopedTravel, 0.0001f);
    }

    [[nodiscard]] const SdfBrickHeader* Bricks() const noexcept
    {
        return MBricks.data();
    }

    [[nodiscard]] std::size_t BrickCount() const noexcept
    {
        return MBricks.size();
    }

    [[nodiscard]] std::size_t NarrowBrickCount() const noexcept
    {
        std::size_t ScopedCount = 0;
        for (const SdfBrickHeader& ScopedHeader : MBricks)
        {
            ScopedCount += ScopedHeader.State == SdfBrickState::Narrow ? 1u : 0u;
        }
        return ScopedCount;
    }

    [[nodiscard]] std::size_t PayloadCount() const noexcept
    {
        return MPayload.size();
    }

    [[nodiscard]] std::uint32_t BricksX() const noexcept
    {
        return MBricksX;
    }

    [[nodiscard]] std::uint32_t BricksY() const noexcept
    {
        return MBricksY;
    }

    [[nodiscard]] float NarrowBand() const noexcept
    {
        return MNarrowBand;
    }

    // Packs the dense brick table into the 32-byte GPU record.
    [[nodiscard]] std::vector<SdfBrickGpu> BuildGpuTable() const
    {
        std::vector<SdfBrickGpu> ScopedTable(MBricks.size());
        for (std::size_t ScopedIndex = 0; ScopedIndex < MBricks.size(); ++ScopedIndex)
        {
            const SdfBrickHeader& ScopedHeader = MBricks[ScopedIndex];
            SdfBrickGpu& ScopedGpu = ScopedTable[ScopedIndex];
            ScopedGpu.PayloadOffset = ScopedHeader.PayloadOffset;
            ScopedGpu.MinDistance = ScopedHeader.MinDistance;
            ScopedGpu.MaxDistance = ScopedHeader.MaxDistance;
            ScopedGpu.BrickX = ScopedHeader.BrickX;
            ScopedGpu.BrickY = ScopedHeader.BrickY;
            ScopedGpu.StateAndFlags = static_cast<std::uint32_t>(ScopedHeader.State);
        }
        return ScopedTable;
    }

    // The payload is stored as float32.
    [[nodiscard]] std::vector<float> BuildGpuPayload() const
    {
        return MPayload;
    }

    // Rebuilds the map from a serialized description. Every narrow brick must
    // point inside the payload pool; a table that would read past it is rejected.
    [[nodiscard]] bool Restore(const SdfBrickMapDesc& InDesc, std::span<const SdfBrickHeader> InBricks,
                               std::span<const float> InPayload) noexcept
    {
        const std::size_t ScopedExpected = static_cast<std::size_t>(InDesc.BricksX) * InDesc.BricksY;
        if (InDesc.BricksX == 0 || InDesc.BricksY == 0 || InDesc.SourceWidth == 0 || InDesc.SourceHeight == 0 ||
            InDesc.CellSize.X <= 0.0f || InDesc.CellSize.Y <= 0.0f || ScopedExpected != InBricks.size())
        {
            return false;
        }
        for (const SdfBrickHeader& ScopedHeader : InBricks)
        {
            if (ScopedHeader.BrickX >= InDesc.BricksX || ScopedHeader.BrickY >= InDesc.BricksY)
            {
                return false;
            }
            if (ScopedHeader.State == SdfBrickState::Narrow)
            {
                const std::size_t ScopedEnd =
                    static_cast<std::size_t>(ScopedHeader.PayloadOffset) + SdfBrickTexels;
                if (ScopedEnd > InPayload.size())
                {
                    return false;
                }
            }
        }
        MOrigin = InDesc.Origin;
        MCellSize = InDesc.CellSize;
        MSourceWidth = InDesc.SourceWidth;
        MSourceHeight = InDesc.SourceHeight;
        MBricksX = InDesc.BricksX;
        MBricksY = InDesc.BricksY;
        MOutsideDistance = InDesc.OutsideDistance;
        MNarrowBand = InDesc.NarrowBand;
        MBricks.assign(InBricks.begin(), InBricks.end());
        MPayload.assign(InPayload.begin(), InPayload.end());
        return true;
    }

    [[nodiscard]] SdfBrickMapDesc Desc() const noexcept
    {
        SdfBrickMapDesc ScopedDesc;
        ScopedDesc.Origin = MOrigin;
        ScopedDesc.CellSize = MCellSize;
        ScopedDesc.SourceWidth = MSourceWidth;
        ScopedDesc.SourceHeight = MSourceHeight;
        ScopedDesc.BricksX = MBricksX;
        ScopedDesc.BricksY = MBricksY;
        ScopedDesc.OutsideDistance = MOutsideDistance;
        ScopedDesc.NarrowBand = MNarrowBand;
        return ScopedDesc;
    }

    [[nodiscard]] std::span<const SdfBrickHeader> BrickView() const noexcept
    {
        return MBricks;
    }

    [[nodiscard]] std::span<const float> PayloadView() const noexcept
    {
        return MPayload;
    }

    // Header of the brick that contains the point, or null outside the grid.
    // Broadphase queries use it to skip bodies that a uniform brick cannot touch.
    [[nodiscard]] const SdfBrickHeader* HeaderAt(Vector2D InWorld) const noexcept
    {
        if (MBricks.empty() || MCellSize.X <= 0.0f || MCellSize.Y <= 0.0f)
        {
            return nullptr;
        }
        const float ScopedX = (InWorld.X - MOrigin.X) / MCellSize.X;
        const float ScopedY = (InWorld.Y - MOrigin.Y) / MCellSize.Y;
        if (ScopedX < 0.0f || ScopedY < 0.0f || ScopedX >= static_cast<float>(MSourceWidth) ||
            ScopedY >= static_cast<float>(MSourceHeight))
        {
            return nullptr;
        }
        const std::uint32_t ScopedBrickX = static_cast<std::uint32_t>(ScopedX) / SdfBrickSize;
        const std::uint32_t ScopedBrickY = static_cast<std::uint32_t>(ScopedY) / SdfBrickSize;
        if (ScopedBrickX >= MBricksX || ScopedBrickY >= MBricksY)
        {
            return nullptr;
        }
        return &BrickAt(ScopedBrickX, ScopedBrickY);
    }

private:
    [[nodiscard]] SdfBrickHeader& BrickAt(std::uint32_t InX, std::uint32_t InY) noexcept
    {
        return MBricks[static_cast<std::size_t>(InY) * MBricksX + InX];
    }

    [[nodiscard]] const SdfBrickHeader& BrickAt(std::uint32_t InX, std::uint32_t InY) const noexcept
    {
        return MBricks[static_cast<std::size_t>(InY) * MBricksX + InX];
    }

    [[nodiscard]] static float SourceDistance(const SdfField& InField, std::uint32_t InX,
                                              std::uint32_t InY) noexcept
    {
        if (InX >= InField.width || InY >= InField.height)
        {
            return InField.outsideDistance;
        }
        return InField.samples[static_cast<std::size_t>(InY) * InField.width + InX];
    }

    Vector2D MOrigin{};
    Vector2D MCellSize{1.0f, 1.0f};
    std::uint32_t MSourceWidth = 0;
    std::uint32_t MSourceHeight = 0;
    std::uint32_t MBricksX = 0;
    std::uint32_t MBricksY = 0;
    float MOutsideDistance = 1.0e6f;
    float MNarrowBand = 1.0f;
    std::vector<SdfBrickHeader> MBricks;
    std::vector<float> MPayload;
};
}
