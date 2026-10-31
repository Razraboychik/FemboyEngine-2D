// FemboyEngine2D - column-major 4x4 matrix.
#pragma once

#include "Engine/Core/Types.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <type_traits>

namespace Fby
{

struct Mat4
{
    // column-major: m[column * 4 + row]
    std::array<float, 16> m{1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                            0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};

    [[nodiscard]] static constexpr Mat4 Identity() noexcept
    {
        return Mat4{};
    }

    [[nodiscard]] static constexpr Mat4 Translation(Vector2D InTranslation) noexcept
    {
        Mat4 ScopedResult;
        ScopedResult.m[12] = InTranslation.X;
        ScopedResult.m[13] = InTranslation.Y;
        return ScopedResult;
    }

    [[nodiscard]] static constexpr Mat4 Scale(Vector2D InScale) noexcept
    {
        Mat4 ScopedResult;
        ScopedResult.m[0] = InScale.X;
        ScopedResult.m[5] = InScale.Y;
        return ScopedResult;
    }

    [[nodiscard]] static Mat4 Rotation(float InRadians) noexcept
    {
        const float ScopedCosine = std::cos(InRadians);
        const float ScopedSine = std::sin(InRadians);
        Mat4 ScopedResult;
        ScopedResult.m[0] = ScopedCosine;
        ScopedResult.m[1] = ScopedSine;
        ScopedResult.m[4] = -ScopedSine;
        ScopedResult.m[5] = ScopedCosine;
        return ScopedResult;
    }

    [[nodiscard]] friend constexpr Mat4 operator*(const Mat4& InLeft, const Mat4& InRight) noexcept
    {
        Mat4 ScopedResult;
        for (std::size_t ScopedColumn = 0; ScopedColumn < 4; ++ScopedColumn)
        {
            for (std::size_t ScopedRow = 0; ScopedRow < 4; ++ScopedRow)
            {
                float ScopedValue = 0.0f;
                for (std::size_t ScopedK = 0; ScopedK < 4; ++ScopedK)
                {
                    ScopedValue += InLeft.m[ScopedK * 4 + ScopedRow] * InRight.m[ScopedColumn * 4 + ScopedK];
                }
                ScopedResult.m[ScopedColumn * 4 + ScopedRow] = ScopedValue;
            }
        }
        return ScopedResult;
    }

    [[nodiscard]] friend constexpr bool operator==(const Mat4&, const Mat4&) = default;
};

static_assert(sizeof(Mat4) == 64);
static_assert(alignof(Mat4) == 4);
static_assert(std::is_trivially_copyable_v<Mat4>);
static_assert(std::is_standard_layout_v<Mat4>);
}
