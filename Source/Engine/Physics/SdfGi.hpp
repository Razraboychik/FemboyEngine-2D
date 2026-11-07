// FemboyEngine2D - CPU SDF GI probe volume and one-bounce reference.
#pragma once

#include "Engine/Core/SimMath.hpp"
#include "Engine/Core/Types.hpp"
#include "Engine/Physics/SdfBrickMap.hpp"
#include "Engine/RHI/GpuLighting.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace Fby
{

inline constexpr std::uint32_t SdfGiLobes = 4u;

// Four directional lobes plus an occlusion term. The layout is deliberately
// compact for direct SSBO upload.
struct SdfGiProbe
{
    std::array<float, SdfGiLobes> Radiance{0.0f, 0.0f, 0.0f, 0.0f};
    float Occlusion = 1.0f;
    float Openness = 0.0f;
    float Reserved0 = 0.0f;
    float Reserved1 = 0.0f;
};

static_assert(sizeof(SdfGiProbe) == 32);

using GpuGiProbe = Rhi::GpuGiProbe;
using GpuGiVolumeHeader = Rhi::GpuGiVolumeHeader;

static_assert(sizeof(GpuGiProbe) == 48);

// A regular grid of probes baked from the sparse SDF. Rays are traced with the
// brick map's bounded trace; the result is a deterministic CPU reference for
// probe evaluation.
class SdfGiVolume
{
public:
    [[nodiscard]] bool Build(const SdfBrickMap& InMap, Vector2D InOrigin, Vector2D InCellSize, std::uint32_t InWidth,
                             std::uint32_t InHeight, float InMaxDistance = 256.0f,
                             std::uint32_t InRaysPerLobe = 2u)
    {
        if (InWidth == 0u || InHeight == 0u || InCellSize.X <= 0.0f || InCellSize.Y <= 0.0f)
        {
            return false;
        }
        MOrigin = InOrigin;
        MCellSize = InCellSize;
        MWidth = InWidth;
        MHeight = InHeight;
        MMaxDistance = std::max(InMaxDistance, 1.0f);
        MRaysPerLobe = std::clamp(InRaysPerLobe, 1u, 4u);
        MProbes.assign(static_cast<std::size_t>(MWidth) * MHeight, SdfGiProbe{});
        for (std::uint32_t ScopedY = 0; ScopedY < MHeight; ++ScopedY)
        {
            for (std::uint32_t ScopedX = 0; ScopedX < MWidth; ++ScopedX)
            {
                EvaluateProbe(InMap, ScopedX, ScopedY);
            }
        }
        return true;
    }

    // Recomputes one temporal slice. SliceCount = 8 means every eighth probe is
    // refreshed and the remaining probes keep their previous result.
    [[nodiscard]] bool Update(const SdfBrickMap& InMap, std::uint32_t InSlice,
                              std::uint32_t InSliceCount) noexcept
    {
        if (MProbes.empty() || InSliceCount == 0u || InSlice >= InSliceCount)
        {
            return false;
        }
        for (std::uint32_t ScopedY = 0; ScopedY < MHeight; ++ScopedY)
        {
            for (std::uint32_t ScopedX = 0; ScopedX < MWidth; ++ScopedX)
            {
                const std::size_t ScopedIndex = static_cast<std::size_t>(ScopedY) * MWidth + ScopedX;
                if (ScopedIndex % InSliceCount != InSlice)
                {
                    continue;
                }
                EvaluateProbe(InMap, ScopedX, ScopedY);
            }
        }
        return true;
    }

    [[nodiscard]] const SdfGiProbe* ProbeAt(Vector2D InWorld) const noexcept
    {
        if (MProbes.empty())
        {
            return nullptr;
        }
        const float ScopedX = (InWorld.X - MOrigin.X) / MCellSize.X;
        const float ScopedY = (InWorld.Y - MOrigin.Y) / MCellSize.Y;
        if (ScopedX < 0.0f || ScopedY < 0.0f || ScopedX >= static_cast<float>(MWidth) ||
            ScopedY >= static_cast<float>(MHeight))
        {
            return nullptr;
        }
        return &MProbes[static_cast<std::size_t>(ScopedY) * MWidth + static_cast<std::size_t>(ScopedX)];
    }

    [[nodiscard]] std::size_t ProbeCount() const noexcept
    {
        return MProbes.size();
    }

    // Overwrites one probe from an external bake (the editor feeds the traced GI
    // into the volume the lighting system consumes).
    [[nodiscard]] bool SetProbeRadiance(std::size_t InIndex, const std::array<float, SdfGiLobes>& InLobes,
                                        float InOcclusion, float InOpenness) noexcept
    {
        if (InIndex >= MProbes.size())
        {
            return false;
        }
        SdfGiProbe& ScopedProbe = MProbes[InIndex];
        ScopedProbe.Radiance = InLobes;
        ScopedProbe.Occlusion = InOcclusion;
        ScopedProbe.Openness = InOpenness;
        return true;
    }

    // Packs the current grid into the GPU layout. The caller owns the buffer
    // and uploads it as an SSBO when the renderer supports GI probes.
    [[nodiscard]] bool PackGpu(std::vector<GpuGiProbe>& OutProbes) const
    {
        if (MProbes.empty())
        {
            OutProbes.clear();
            return false;
        }
        OutProbes.resize(MProbes.size());
        for (std::uint32_t ScopedY = 0; ScopedY < MHeight; ++ScopedY)
        {
            for (std::uint32_t ScopedX = 0; ScopedX < MWidth; ++ScopedX)
            {
                const std::size_t ScopedIndex = static_cast<std::size_t>(ScopedY) * MWidth + ScopedX;
                const SdfGiProbe& ScopedProbe = MProbes[ScopedIndex];
                GpuGiProbe& ScopedGpu = OutProbes[ScopedIndex];
                ScopedGpu.PositionAndRadius =
                    Vector4D{MOrigin.X + (static_cast<float>(ScopedX) + 0.5f) * MCellSize.X,
                             MOrigin.Y + (static_cast<float>(ScopedY) + 0.5f) * MCellSize.Y,
                             std::max(MCellSize.X, MCellSize.Y) * 0.5f, 0.0f};
                ScopedGpu.Radiance = Vector4D{ScopedProbe.Radiance[0], ScopedProbe.Radiance[1],
                                              ScopedProbe.Radiance[2], ScopedProbe.Radiance[3]};
                ScopedGpu.Occlusion =
                    Vector4D{ScopedProbe.Occlusion, ScopedProbe.Openness, 0.0f, 0.0f};
            }
        }
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

    [[nodiscard]] Vector2D Origin() const noexcept
    {
        return MOrigin;
    }

    [[nodiscard]] Vector2D CellSize() const noexcept
    {
        return MCellSize;
    }

    [[nodiscard]] float MaxDistance() const noexcept
    {
        return MMaxDistance;
    }

private:
    void EvaluateProbe(const SdfBrickMap& InMap, std::uint32_t InX, std::uint32_t InY) noexcept
    {
        SdfGiProbe& ScopedProbe = MProbes[static_cast<std::size_t>(InY) * MWidth + InX];
        const Vector2D ScopedCenter{MOrigin.X + (static_cast<float>(InX) + 0.5f) * MCellSize.X,
                                   MOrigin.Y + (static_cast<float>(InY) + 0.5f) * MCellSize.Y};
        if (InMap.Sample(ScopedCenter) <= 0.0f)
        {
            ScopedProbe.Occlusion = 1.0f;
            ScopedProbe.Openness = 0.0f;
            return;
        }
        float ScopedOpenness = 0.0f;
        for (std::uint32_t ScopedLobe = 0; ScopedLobe < SdfGiLobes; ++ScopedLobe)
        {
            const float ScopedAngle = static_cast<float>(ScopedLobe) * 1.57079632679f;
            const Vector2D ScopedBase{Sim::Cos(ScopedAngle), Sim::Sin(ScopedAngle)};
            float ScopedLobeOpen = 0.0f;
            for (std::uint32_t ScopedRay = 0; ScopedRay < MRaysPerLobe; ++ScopedRay)
            {
                const float ScopedJitter = (static_cast<float>(ScopedRay) + 0.5f) /
                                           static_cast<float>(MRaysPerLobe);
                const float ScopedOffset = (ScopedJitter - 0.5f) * 0.35f;
                const Vector2D ScopedDirection{ScopedBase.X - ScopedBase.Y * ScopedOffset,
                                               ScopedBase.Y + ScopedBase.X * ScopedOffset};
                const float ScopedRemaining = InMap.Trace(ScopedCenter, ScopedDirection, MMaxDistance, 32u);
                ScopedLobeOpen += ScopedRemaining > 0.0f ? ScopedRemaining / MMaxDistance : 0.0f;
            }
            ScopedLobeOpen /= static_cast<float>(MRaysPerLobe);
            ScopedProbe.Radiance[ScopedLobe] = ScopedLobeOpen;
            ScopedOpenness += ScopedLobeOpen;
        }
        ScopedOpenness /= static_cast<float>(SdfGiLobes);
        ScopedProbe.Openness = ScopedOpenness;
        ScopedProbe.Occlusion = 1.0f - ScopedOpenness;
    }

    Vector2D MOrigin{};
    Vector2D MCellSize{1.0f, 1.0f};
    std::uint32_t MWidth = 0;
    std::uint32_t MHeight = 0;
    std::uint32_t MRaysPerLobe = 2;
    float MMaxDistance = 256.0f;
    std::vector<SdfGiProbe> MProbes;
};
}
