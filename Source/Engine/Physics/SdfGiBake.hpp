// FemboyEngine2D - baked SDF GI: direct sun visibility plus one bounce.
#pragma once

#include "Engine/Core/SimMath.hpp"
#include "Engine/Core/Types.hpp"
#include "Engine/Physics/SdfBrickMap.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace Fby
{

struct SdfGiBakeSettings
{
    // Direction from the probe toward the sun; normalised by the bake.
    Vector2D SunDirection{0.0f, 1.0f};
    float SunIntensity = 1.0f;
    float BounceIntensity = 0.35f;
    // Ring of neighbour probes sampled for the one-bounce term.
    std::uint32_t BounceSamples = 8;
    float BounceRadius = 48.0f;
    std::uint32_t MaxSteps = 32;
    // Trace length for the direct sun term; independent of the bounce ring.
    float SunDistance = 256.0f;
};

// Baked per probe: direct sun colour, one-bounce colour, and the two terms that
// drive the shader lerp.
struct SdfGiBakedProbe
{
    std::array<float, 3> Direct{0.0f, 0.0f, 0.0f};
    std::array<float, 3> Indirect{0.0f, 0.0f, 0.0f};
    float SunVisibility = 0.0f;
    float Openness = 0.0f;
};

static_assert(sizeof(SdfGiBakedProbe) == 32);

// Bakes a regular probe grid from the sparse SDF. Deterministic by construction:
// probes are visited in row-major order and the bounce ring uses fixed angles.
class SdfGiBaked
{
public:
    [[nodiscard]] bool Build(const SdfBrickMap& InMap, Vector2D InOrigin, Vector2D InCellSize, std::uint32_t InWidth,
                             std::uint32_t InHeight, const SdfGiBakeSettings& InSettings = {}) noexcept
    {
        if (InWidth == 0 || InHeight == 0 || InCellSize.X <= 0.0f || InCellSize.Y <= 0.0f)
        {
            return false;
        }
        MSettings = InSettings;
        MOrigin = InOrigin;
        MCellSize = InCellSize;
        MWidth = InWidth;
        MHeight = InHeight;
        const Vector2D ScopedSun = Normalize(MSettings.SunDirection);
        const float ScopedBounceDistance = std::max(MSettings.BounceRadius, 1.0f);
        const float ScopedSunDistance = std::max(MSettings.SunDistance, ScopedBounceDistance);
        MProbes.assign(static_cast<std::size_t>(MWidth) * MHeight, SdfGiBakedProbe{});
        const std::uint32_t ScopedBounceSamples = std::clamp(MSettings.BounceSamples, 0u, 16u);
        for (std::uint32_t ScopedY = 0; ScopedY < MHeight; ++ScopedY)
        {
            for (std::uint32_t ScopedX = 0; ScopedX < MWidth; ++ScopedX)
            {
                const Vector2D ScopedCenter = ProbeCenter(ScopedX, ScopedY);
                SdfGiBakedProbe& ScopedProbe = MProbes[static_cast<std::size_t>(ScopedY) * MWidth + ScopedX];
                if (InMap.Sample(ScopedCenter) <= 0.0f)
                {
                    continue;
                }
                const float ScopedTrace = InMap.Trace(ScopedCenter, ScopedSun, ScopedSunDistance,
                                                      std::max(MSettings.MaxSteps, 1u));
                // Sun visibility is binary: a zero trace is a hit, any positive
                // remainder means the ray escaped.
                ScopedProbe.SunVisibility = ScopedTrace > 0.0f ? 1.0f : 0.0f;
                ScopedProbe.Direct = {MSettings.SunIntensity * ScopedProbe.SunVisibility,
                                      MSettings.SunIntensity * ScopedProbe.SunVisibility,
                                      MSettings.SunIntensity * ScopedProbe.SunVisibility};
                ScopedProbe.Openness = ScopedProbe.SunVisibility;
            }
        }
        // One bounce: gather the direct sun of visible neighbouring probes. This
        // runs for shadowed probes too, which is where the bounce term matters.
        if (ScopedBounceSamples > 0 && MSettings.BounceIntensity > 0.0f)
        {
            for (std::uint32_t ScopedY = 0; ScopedY < MHeight; ++ScopedY)
            {
                for (std::uint32_t ScopedX = 0; ScopedX < MWidth; ++ScopedX)
                {
                    const std::size_t ScopedIndex = static_cast<std::size_t>(ScopedY) * MWidth + ScopedX;
                    const Vector2D ScopedCenter = ProbeCenter(ScopedX, ScopedY);
                    if (InMap.Sample(ScopedCenter) <= 0.0f)
                    {
                        continue;
                    }
                    std::array<float, 3> ScopedIndirect{0.0f, 0.0f, 0.0f};
                    std::uint32_t ScopedContributors = 0;
                    for (std::uint32_t ScopedSample = 0; ScopedSample < ScopedBounceSamples; ++ScopedSample)
                    {
                        const float ScopedAngle =
                            (static_cast<float>(ScopedSample) + 0.5f) * 6.28318530718f /
                            static_cast<float>(ScopedBounceSamples);
                        const Vector2D ScopedOffset{Sim::Cos(ScopedAngle), Sim::Sin(ScopedAngle)};
                        const Vector2D ScopedNeighbor = ScopedCenter + ScopedOffset * ScopedBounceDistance;
                        if (InMap.Sample(ScopedNeighbor) <= 0.0f)
                        {
                            continue;
                        }
                        const float ScopedRemaining =
                            InMap.Trace(ScopedCenter, ScopedOffset, ScopedBounceDistance,
                                        std::max(MSettings.MaxSteps, 1u));
                        if (ScopedRemaining <= 0.0f)
                        {
                            continue;
                        }
                        const float ScopedNeighborVisibility = SunAt(InMap, ScopedNeighbor, ScopedSun, ScopedSunDistance);
                        if (ScopedNeighborVisibility <= 0.0f)
                        {
                            continue;
                        }
                        ScopedIndirect[0] += ScopedNeighborVisibility;
                        ScopedIndirect[1] += ScopedNeighborVisibility;
                        ScopedIndirect[2] += ScopedNeighborVisibility;
                        ++ScopedContributors;
                    }
                    if (ScopedContributors > 0)
                    {
                        const float ScopedScale =
                            MSettings.BounceIntensity * MSettings.SunIntensity / static_cast<float>(ScopedContributors);
                        SdfGiBakedProbe& ScopedTarget = MProbes[ScopedIndex];
                        ScopedTarget.Indirect = {ScopedIndirect[0] * ScopedScale, ScopedIndirect[1] * ScopedScale,
                                                 ScopedIndirect[2] * ScopedScale};
                    }
                }
            }
        }
        return true;
    }

    [[nodiscard]] const SdfGiBakedProbe* ProbeAt(std::uint32_t InX, std::uint32_t InY) const noexcept
    {
        if (InX >= MWidth || InY >= MHeight)
        {
            return nullptr;
        }
        return &MProbes[static_cast<std::size_t>(InY) * MWidth + InX];
    }

    [[nodiscard]] SdfGiBakedProbe* ProbeMutable(std::uint32_t InX, std::uint32_t InY) noexcept
    {
        if (InX >= MWidth || InY >= MHeight)
        {
            return nullptr;
        }
        return &MProbes[static_cast<std::size_t>(InY) * MWidth + InX];
    }

    // Restores a bake that was serialized earlier, without re-tracing.
    [[nodiscard]] bool Restore(Vector2D InOrigin, Vector2D InCellSize, std::uint32_t InWidth,
                               std::uint32_t InHeight, std::span<const SdfGiBakedProbe> InProbes) noexcept
    {
        if (InWidth == 0 || InHeight == 0 || InCellSize.X <= 0.0f || InCellSize.Y <= 0.0f ||
            InProbes.size() != static_cast<std::size_t>(InWidth) * InHeight)
        {
            return false;
        }
        MOrigin = InOrigin;
        MCellSize = InCellSize;
        MWidth = InWidth;
        MHeight = InHeight;
        MProbes.assign(InProbes.begin(), InProbes.end());
        return true;
    }

    [[nodiscard]] std::uint32_t Width() const noexcept
    {
        return MWidth;
    }
    [[nodiscard]] std::uint32_t Height() const noexcept
    {
        return MHeight;
    }
    [[nodiscard]] std::size_t ProbeCount() const noexcept
    {
        return MProbes.size();
    }
    [[nodiscard]] Vector2D ProbeCenter(std::uint32_t InX, std::uint32_t InY) const noexcept
    {
        return Vector2D{MOrigin.X + (static_cast<float>(InX) + 0.5f) * MCellSize.X,
                        MOrigin.Y + (static_cast<float>(InY) + 0.5f) * MCellSize.Y};
    }
    [[nodiscard]] Vector2D Origin() const noexcept
    {
        return MOrigin;
    }
    [[nodiscard]] Vector2D CellSize() const noexcept
    {
        return MCellSize;
    }

private:
    [[nodiscard]] static Vector2D Normalize(Vector2D InValue) noexcept
    {
        const float ScopedLength = std::sqrt(InValue.LengthSquared());
        if (ScopedLength <= 1.0e-6f)
        {
            return Vector2D{0.0f, 1.0f};
        }
        return Vector2D{InValue.X / ScopedLength, InValue.Y / ScopedLength};
    }

    [[nodiscard]] static float SunAt(const SdfBrickMap& InMap, Vector2D InPoint, Vector2D InSun,
                                     float InMaxDistance) noexcept
    {
        const float ScopedTrace = InMap.Trace(InPoint, InSun, InMaxDistance, 32u);
        return ScopedTrace > 0.0f ? std::clamp(ScopedTrace / InMaxDistance, 0.0f, 1.0f) : 0.0f;
    }

    SdfGiBakeSettings MSettings{};
    Vector2D MOrigin{};
    Vector2D MCellSize{1.0f, 1.0f};
    std::uint32_t MWidth = 0;
    std::uint32_t MHeight = 0;
    std::vector<SdfGiBakedProbe> MProbes;
};

}
