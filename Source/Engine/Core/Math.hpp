// FemboyEngine2D - finite-safe scalar helpers for values crossing a trust boundary.
//
// These are deliberately small and explicit. They are NOT a replacement for
// std::clamp/min/max: internal code that has already validated its inputs keeps
// using the standard algorithms. Use these where a NaN/Inf can arrive from
// serialized data, a script, a plugin or a network packet, and where silently
// propagating it would poison simulation state.
#pragma once

#include "Engine/Core/Types.hpp"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <cmath>
#include <concepts>
#include <limits>

namespace Fby::Math
{

// Returns Fallback when Value is not finite, otherwise std::clamp(Value, Low, High).
// Low > High is a precondition violation and is asserted in debug builds.
template <std::floating_point T>
[[nodiscard]] constexpr T ClampFinite(T Value, T Low, T High, T Fallback) noexcept
{
    if (!std::isfinite(Value))
    {
        return Fallback;
    }
    return std::clamp(Value, Low, High);
}

template <std::floating_point T>
[[nodiscard]] constexpr T FiniteOr(T Value, T Fallback) noexcept
{
    return std::isfinite(Value) ? Value : Fallback;
}

[[nodiscard]] constexpr bool IsFinite(Vector2D Value) noexcept
{
    return std::isfinite(Value.X) && std::isfinite(Value.Y);
}

[[nodiscard]] constexpr bool IsFinite(Vector3D Value) noexcept
{
    return std::isfinite(Value.X) && std::isfinite(Value.Y) && std::isfinite(Value.Z);
}

// Normalizes only when the vector has a finite, non-degenerate length.
// Returns false and leaves OutValue untouched otherwise.
[[nodiscard]] inline bool TryNormalize(Vector2D Value, Vector2D& OutValue, float Epsilon = 1.0e-12f) noexcept
{
    if (!IsFinite(Value) || !(Epsilon > 0.0f))
    {
        return false;
    }
    const float ScopedLengthSquared = Value.X * Value.X + Value.Y * Value.Y;
    if (!std::isfinite(ScopedLengthSquared) || ScopedLengthSquared <= Epsilon * Epsilon)
    {
        return false;
    }
    const float ScopedInverseLength = 1.0f / std::sqrt(ScopedLengthSquared);
    OutValue = Vector2D{Value.X * ScopedInverseLength, Value.Y * ScopedInverseLength};
    return true;
}

// Division with a non-finite/zero denominator guard. Zero by itself is a valid
// denominator for non-simulation maths, so callers that treat zero specially
// must pass Epsilon > 0 and use the overload below.
[[nodiscard]] constexpr float SafeDiv(float Numerator, float Denominator, float Fallback) noexcept
{
    if (!std::isfinite(Numerator) || !std::isfinite(Denominator) || Denominator == 0.0f)
    {
        return Fallback;
    }
    return Numerator / Denominator;
}

[[nodiscard]] constexpr float SafeDiv(float Numerator, float Denominator, float Fallback, float Epsilon) noexcept
{
    if (!std::isfinite(Numerator) || !std::isfinite(Denominator) || std::fabs(Denominator) <= Epsilon)
    {
        return Fallback;
    }
    return Numerator / Denominator;
}

// Exponent helpers that reject the standard algorithms' undefined/unspecified
// inputs instead of relying on the caller to remember the preconditions.
[[nodiscard]] constexpr std::uint32_t SafeBitCeil(std::uint32_t Value) noexcept
{
    if (Value == 0u || Value > (1u << 31))
    {
        return 0u;
    }
    return std::bit_ceil(Value);
}

}
