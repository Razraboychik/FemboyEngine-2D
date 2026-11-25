// FemboyEngine2D - smoke regions that attenuate the baked GI probes.
#pragma once

#include "Engine/Core/SimMath.hpp"
#include "Engine/Core/Types.hpp"
#include "Engine/Physics/SdfGiBake.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace Fby
{

struct SdfSmokeDesc
{
    Vector2D Origin{};
    Vector2D CellSize{8.0f, 8.0f};
    std::uint32_t Width = 0;
    std::uint32_t Height = 0;
    // Per-second density retention.
    float Dissipation = 0.97f;
    // Wind in world units per second.
    Vector2D Wind{};
    // Optical depth per density unit per world unit.
    float Absorption = 0.25f;
};

// A deterministic density grid. Step advects by the wind with a bilinear
// backtrace and dissipates; the result feeds Transmittance, which dims the baked
// probes along the sun direction. No random terms, so a replay reproduces it.
class SdfSmokeField
{
public:
    [[nodiscard]] bool Configure(const SdfSmokeDesc& InDesc) noexcept
    {
        if (InDesc.Width == 0 || InDesc.Height == 0 || !std::isfinite(InDesc.CellSize.X) ||
            !std::isfinite(InDesc.CellSize.Y) || !std::isfinite(InDesc.Origin.X) ||
            !std::isfinite(InDesc.Origin.Y) || InDesc.CellSize.X <= 0.0f || InDesc.CellSize.Y <= 0.0f)
        {
            return false;
        }
        constexpr std::uint64_t kMaxCells = 1ull << 22u;
        if (static_cast<std::uint64_t>(InDesc.Width) * InDesc.Height > kMaxCells)
        {
            return false;
        }
        try
        {
            MDensity.assign(static_cast<std::size_t>(InDesc.Width) * InDesc.Height, 0.0f);
            MScratch.assign(MDensity.size(), 0.0f);
        }
        catch (...)
        {
            MDensity.clear();
            MScratch.clear();
            return false;
        }
        MDesc = InDesc;
        return true;
    }

    void Clear() noexcept
    {
        std::fill(MDensity.begin(), MDensity.end(), 0.0f);
    }

    [[nodiscard]] bool AddDensityWorld(Vector2D InCenter, float InRadius, float InAmount) noexcept
    {
        if (MDensity.empty() || !std::isfinite(InCenter.X) || !std::isfinite(InCenter.Y) ||
            !std::isfinite(InRadius) || !std::isfinite(InAmount) || InRadius <= 0.0f || InAmount == 0.0f)
        {
            return false;
        }
        const auto ScopedCell = [](double InValue, std::uint32_t InLimit)
        {
            if (!std::isfinite(InValue))
            {
                return 0.0;
            }
            return std::clamp(InValue, 0.0, static_cast<double>(InLimit));
        };
        const std::int32_t ScopedMinX = static_cast<std::int32_t>(ScopedCell(
            std::floor((InCenter.X - InRadius - MDesc.Origin.X) / MDesc.CellSize.X), MDesc.Width - 1u));
        const std::int32_t ScopedMaxX = static_cast<std::int32_t>(ScopedCell(
            std::ceil((InCenter.X + InRadius - MDesc.Origin.X) / MDesc.CellSize.X), MDesc.Width - 1u));
        const std::int32_t ScopedMinY = static_cast<std::int32_t>(ScopedCell(
            std::floor((InCenter.Y - InRadius - MDesc.Origin.Y) / MDesc.CellSize.Y), MDesc.Height - 1u));
        const std::int32_t ScopedMaxY = static_cast<std::int32_t>(ScopedCell(
            std::ceil((InCenter.Y + InRadius - MDesc.Origin.Y) / MDesc.CellSize.Y), MDesc.Height - 1u));
        for (std::int32_t ScopedY = std::max(ScopedMinY, 0); ScopedY <= ScopedMaxY &&
                                                                 ScopedY < static_cast<std::int32_t>(MDesc.Height);
             ++ScopedY)
        {
            for (std::int32_t ScopedX = std::max(ScopedMinX, 0);
                 ScopedX <= ScopedMaxX && ScopedX < static_cast<std::int32_t>(MDesc.Width); ++ScopedX)
            {
                const Vector2D ScopedWorld{
                    MDesc.Origin.X + (static_cast<float>(ScopedX) + 0.5f) * MDesc.CellSize.X,
                    MDesc.Origin.Y + (static_cast<float>(ScopedY) + 0.5f) * MDesc.CellSize.Y};
                const float ScopedDistance = (ScopedWorld - InCenter).LengthSquared();
                if (ScopedDistance <= InRadius * InRadius)
                {
                    MDensity[static_cast<std::size_t>(ScopedY) * MDesc.Width + static_cast<std::uint32_t>(ScopedX)] +=
                        InAmount;
                }
            }
        }
        return true;
    }

    [[nodiscard]] float DensityAt(std::uint32_t InX, std::uint32_t InY) const noexcept
    {
        if (InX >= MDesc.Width || InY >= MDesc.Height)
        {
            return 0.0f;
        }
        return MDensity[static_cast<std::size_t>(InY) * MDesc.Width + InX];
    }

    [[nodiscard]] float SampleWorld(Vector2D InWorld) const noexcept
    {
        if (MDensity.empty())
        {
            return 0.0f;
        }
        const float ScopedX = (InWorld.X - MDesc.Origin.X) / MDesc.CellSize.X - 0.5f;
        const float ScopedY = (InWorld.Y - MDesc.Origin.Y) / MDesc.CellSize.Y - 0.5f;
        if (!(ScopedX >= 0.0f && ScopedY >= 0.0f && ScopedX < static_cast<float>(MDesc.Width) - 1.0f &&
              ScopedY < static_cast<float>(MDesc.Height) - 1.0f))
        {
            return 0.0f;
        }
        const auto ScopedX0 = static_cast<std::uint32_t>(ScopedX);
        const auto ScopedY0 = static_cast<std::uint32_t>(ScopedY);
        const float ScopedFx = ScopedX - static_cast<float>(ScopedX0);
        const float ScopedFy = ScopedY - static_cast<float>(ScopedY0);
        const auto ScopedAt = [this](std::uint32_t InX, std::uint32_t InY)
        {
            return MDensity[static_cast<std::size_t>(InY) * MDesc.Width + InX];
        };
        const float ScopedTop =
            ScopedAt(ScopedX0, ScopedY0) + (ScopedAt(ScopedX0 + 1, ScopedY0) - ScopedAt(ScopedX0, ScopedY0)) * ScopedFx;
        const float ScopedBottom = ScopedAt(ScopedX0, ScopedY0 + 1) +
                                   (ScopedAt(ScopedX0 + 1, ScopedY0 + 1) - ScopedAt(ScopedX0, ScopedY0 + 1)) * ScopedFx;
        return ScopedTop + (ScopedBottom - ScopedTop) * ScopedFy;
    }

    void Step(float InDt) noexcept
    {
        if (MDensity.empty() || !(InDt > 0.0f) || !std::isfinite(InDt))
        {
            return;
        }
        const float ScopedCellX = MDesc.CellSize.X;
        const float ScopedCellY = MDesc.CellSize.Y;
        const float ScopedRetain = std::pow(std::clamp(MDesc.Dissipation, 0.0f, 1.0f), InDt);
        // Semi-Lagrangian backtrace, deterministic and allocation free.
        for (std::uint32_t ScopedY = 0; ScopedY < MDesc.Height; ++ScopedY)
        {
            for (std::uint32_t ScopedX = 0; ScopedX < MDesc.Width; ++ScopedX)
            {
                const Vector2D ScopedWorld{MDesc.Origin.X + (static_cast<float>(ScopedX) + 0.5f) * ScopedCellX -
                                               MDesc.Wind.X * InDt,
                                           MDesc.Origin.Y + (static_cast<float>(ScopedY) + 0.5f) * ScopedCellY -
                                               MDesc.Wind.Y * InDt};
                MScratch[static_cast<std::size_t>(ScopedY) * MDesc.Width + ScopedX] =
                    SampleWorld(ScopedWorld) * ScopedRetain;
            }
        }
        std::swap(MDensity, MScratch);
    }

    // Beer-Lambert transmittance along a ray: 1 means clear air, 0 fully opaque.
    [[nodiscard]] float Transmittance(Vector2D InFrom, Vector2D InDirection, float InDistance,
                                      std::uint32_t InSteps = 16) const noexcept
    {
        if (MDensity.empty() || InDistance <= 0.0f || InSteps == 0)
        {
            return 1.0f;
        }
        const float ScopedLength = std::sqrt(InDirection.LengthSquared());
        if (ScopedLength <= 1.0e-6f)
        {
            return 1.0f;
        }
        const Vector2D ScopedDirection = InDirection / ScopedLength;
        const float ScopedStepLength = InDistance / static_cast<float>(InSteps);
        float ScopedOptical = 0.0f;
        for (std::uint32_t ScopedStep = 0; ScopedStep < InSteps; ++ScopedStep)
        {
            const Vector2D ScopedPoint =
                InFrom + ScopedDirection * (ScopedStepLength * (static_cast<float>(ScopedStep) + 0.5f));
            ScopedOptical += SampleWorld(ScopedPoint) * ScopedStepLength;
        }
        return Sim::Exp(-MDesc.Absorption * ScopedOptical);
    }

    // Dims the baked direct light by the smoke along the sun ray. The indirect
    // term is only halved, because a bounce keeps some energy in a smoky room.
    void AttenuateGi(SdfGiBaked& InBake, Vector2D InSunDirection, float InDistance) const noexcept
    {
        for (std::uint32_t ScopedY = 0; ScopedY < InBake.Height(); ++ScopedY)
        {
            for (std::uint32_t ScopedX = 0; ScopedX < InBake.Width(); ++ScopedX)
            {
                SdfGiBakedProbe* ScopedProbe = InBake.ProbeMutable(ScopedX, ScopedY);
                if (ScopedProbe == nullptr)
                {
                    continue;
                }
                const float ScopedTransmittance =
                    Transmittance(InBake.ProbeCenter(ScopedX, ScopedY), InSunDirection, InDistance);
                for (std::size_t ScopedChannel = 0; ScopedChannel < 3; ++ScopedChannel)
                {
                    ScopedProbe->Direct[ScopedChannel] *= ScopedTransmittance;
                    ScopedProbe->Indirect[ScopedChannel] *= 0.5f + 0.5f * ScopedTransmittance;
                }
            }
        }
    }

    [[nodiscard]] std::uint32_t Width() const noexcept
    {
        return MDesc.Width;
    }
    [[nodiscard]] std::uint32_t Height() const noexcept
    {
        return MDesc.Height;
    }

private:
    SdfSmokeDesc MDesc{};
    std::vector<float> MDensity;
    std::vector<float> MScratch;
};

}
