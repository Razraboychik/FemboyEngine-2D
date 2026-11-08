// FemboyEngine2D - deterministic scalar math for the simulation.
//
// The standard library is free to pick a different libm per platform, and the
// transcendental results then differ in the last bits. Rollback compares state
// by value, so the simulation uses these routines instead: range reduction is
// exact IEEE arithmetic and every polynomial step is an explicit fused
// multiply-add, which is correctly rounded on every supported target.
#pragma once

#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>

namespace Fby::Sim
{

inline constexpr float kPi = 3.14159265358979323846f;
inline constexpr float kHalfPi = 0.5f * kPi;
inline constexpr float kQuarterPi = 0.25f * kPi;
inline constexpr float kTwoPi = 2.0f * kPi;
// tan(pi / 8): above this ratio the atan polynomial is evaluated on the reduced
// argument (x - 1) / (x + 1) instead, which keeps |r| inside the fast range.
inline constexpr float kTanEighth = 0.41421356f;

namespace Detail
{

struct ReducedAngle
{
    float R = 0.0f;
    std::uint32_t Quadrant = 0;
};

// Reduces the angle to [-pi/4, pi/4] and returns the quadrant. std::remainder
// is an exact operation, unlike a sin/cos call, so it is safe to use here.
[[nodiscard]] inline ReducedAngle ReduceAngle(float InRadians) noexcept
{
    constexpr double kTwoPiDouble = 6.283185307179586476925286766559;
    constexpr double kHalfPiDouble = 1.5707963267948966192313216916398;
    const double ScopedAngle = std::remainder(static_cast<double>(InRadians), kTwoPiDouble);
    const double ScopedQuadrant = std::nearbyint(ScopedAngle / kHalfPiDouble);
    const double ScopedRemainder = ScopedAngle - ScopedQuadrant * kHalfPiDouble;
    return ReducedAngle{static_cast<float>(ScopedRemainder),
                        static_cast<std::uint32_t>(static_cast<std::int64_t>(ScopedQuadrant) & 3)};
}

[[nodiscard]] inline float SinPolynomial(float InR) noexcept
{
    const float ScopedSquare = InR * InR;
    float ScopedPoly = 1.0f / 362880.0f;
    ScopedPoly = std::fma(ScopedPoly, ScopedSquare, -1.0f / 5040.0f);
    ScopedPoly = std::fma(ScopedPoly, ScopedSquare, 1.0f / 120.0f);
    ScopedPoly = std::fma(ScopedPoly, ScopedSquare, -1.0f / 6.0f);
    return std::fma(ScopedPoly, InR * ScopedSquare, InR);
}

[[nodiscard]] inline float CosPolynomial(float InR) noexcept
{
    const float ScopedSquare = InR * InR;
    float ScopedPoly = 1.0f / 40320.0f;
    ScopedPoly = std::fma(ScopedPoly, ScopedSquare, -1.0f / 720.0f);
    ScopedPoly = std::fma(ScopedPoly, ScopedSquare, 1.0f / 24.0f);
    ScopedPoly = std::fma(ScopedPoly, ScopedSquare, -0.5f);
    return std::fma(ScopedPoly, ScopedSquare, 1.0f);
}

[[nodiscard]] inline float AtanSmall(float InValue) noexcept
{
    const float ScopedSquare = InValue * InValue;
    float ScopedPoly = 1.0f / 13.0f;
    ScopedPoly = std::fma(ScopedPoly, ScopedSquare, -1.0f / 11.0f);
    ScopedPoly = std::fma(ScopedPoly, ScopedSquare, 1.0f / 9.0f);
    ScopedPoly = std::fma(ScopedPoly, ScopedSquare, -1.0f / 7.0f);
    ScopedPoly = std::fma(ScopedPoly, ScopedSquare, 1.0f / 5.0f);
    ScopedPoly = std::fma(ScopedPoly, ScopedSquare, -1.0f / 3.0f);
    ScopedPoly = std::fma(ScopedPoly, ScopedSquare, 1.0f);
    return InValue * ScopedPoly;
}

// Non-negative atan: |x| > 1 is folded through pi/2 - atan(1/x), and values
// above tan(pi/8) through pi/4 + atan((x - 1) / (x + 1)).
[[nodiscard]] inline float AtanPositive(float InValue) noexcept
{
    if (InValue > 1.0f)
    {
        return kHalfPi - AtanPositive(1.0f / InValue);
    }
    if (InValue > kTanEighth)
    {
        return kQuarterPi + AtanSmall((InValue - 1.0f) / (InValue + 1.0f));
    }
    return AtanSmall(InValue);
}
}

// Writes sin and cos of one angle. The caller passes pointers so a single range
// reduction serves both values.
inline void SinCos(float InRadians, float* OutSin, float* OutCos) noexcept
{
    const Detail::ReducedAngle ScopedAngle = Detail::ReduceAngle(InRadians);
    const float ScopedSin = Detail::SinPolynomial(ScopedAngle.R);
    const float ScopedCos = Detail::CosPolynomial(ScopedAngle.R);
    switch (ScopedAngle.Quadrant)
    {
        case 0:
            *OutSin = ScopedSin;
            *OutCos = ScopedCos;
            break;
        case 1:
            *OutSin = ScopedCos;
            *OutCos = -ScopedSin;
            break;
        case 2:
            *OutSin = -ScopedSin;
            *OutCos = -ScopedCos;
            break;
        default:
            *OutSin = -ScopedCos;
            *OutCos = ScopedSin;
            break;
    }
}

[[nodiscard]] inline float Sin(float InRadians) noexcept
{
    float ScopedSin = 0.0f;
    float ScopedCos = 0.0f;
    SinCos(InRadians, &ScopedSin, &ScopedCos);
    return ScopedSin;
}

[[nodiscard]] inline float Cos(float InRadians) noexcept
{
    float ScopedSin = 0.0f;
    float ScopedCos = 0.0f;
    SinCos(InRadians, &ScopedSin, &ScopedCos);
    return ScopedCos;
}

[[nodiscard]] inline float Atan2(float InY, float InX) noexcept
{
    if (InX == 0.0f && InY == 0.0f)
    {
        return 0.0f;
    }
    const float ScopedAbsX = std::fabs(InX);
    const float ScopedAbsY = std::fabs(InY);
    float ScopedAngle = 0.0f;
    if (ScopedAbsX >= ScopedAbsY)
    {
        ScopedAngle = Detail::AtanPositive(ScopedAbsY / ScopedAbsX);
    }
    else
    {
        ScopedAngle = kHalfPi - Detail::AtanPositive(ScopedAbsX / ScopedAbsY);
    }
    if (InX < 0.0f)
    {
        ScopedAngle = kPi - ScopedAngle;
    }
    // signbit keeps the signed-zero case identical to the reference function.
    return std::signbit(InY) ? -ScopedAngle : ScopedAngle;
}

// Wraps an angle to [-pi, pi] without a libm trigonometric call.
[[nodiscard]] inline float WrapPi(float InRadians) noexcept
{
    constexpr double kTwoPiDouble = 6.283185307179586476925286766559;
    return static_cast<float>(std::remainder(static_cast<double>(InRadians), kTwoPiDouble));
}

// acos through the deterministic atan2 and an exactly rounded square root. The
// product form keeps precision close to +/-1, where 1 - x * x cancels.
[[nodiscard]] inline float Acos(float InValue) noexcept
{
    const float ScopedClamped = !(InValue >= -1.0f) ? -1.0f : (InValue > 1.0f ? 1.0f : InValue);
    const float ScopedSquared = (1.0f - ScopedClamped) * (1.0f + ScopedClamped);
    const float ScopedRoot = std::sqrt(ScopedSquared > 0.0f ? ScopedSquared : 0.0f);
    return Atan2(ScopedRoot, ScopedClamped);
}

// Cube root through exponent splitting and a fixed Newton chain: no libm call,
// so the iteration count and the rounding are identical on every target.
[[nodiscard]] inline float Cbrt(float InValue) noexcept
{
    if (!std::isfinite(InValue))
    {
        return InValue;
    }
    if (InValue == 0.0f)
    {
        return InValue;
    }
    const bool ScopedNegative = InValue < 0.0f;
    const float ScopedValue = ScopedNegative ? -InValue : InValue;
    std::uint32_t ScopedBits = std::bit_cast<std::uint32_t>(ScopedValue);
    const int ScopedExponent = static_cast<int>(ScopedBits >> 23u) - 127;
    ScopedBits = (ScopedBits & 0x007FFFFFu) | 0x3F800000u;
    float ScopedMantissa = std::bit_cast<float>(ScopedBits);
    // Move the mantissa into [1, 8) so the remaining exponent is divisible by 3.
    int ScopedRemainder = ScopedExponent % 3;
    if (ScopedRemainder < 0)
    {
        ScopedRemainder += 3;
    }
    const int ScopedQuotient = (ScopedExponent - ScopedRemainder) / 3;
    if (ScopedRemainder == 1)
    {
        ScopedMantissa *= 2.0f;
    }
    else if (ScopedRemainder == 2)
    {
        ScopedMantissa *= 4.0f;
    }
    float ScopedGuess = 1.0f + (ScopedMantissa - 1.0f) * (1.0f / 3.0f);
    for (int ScopedStep = 0; ScopedStep < 5; ++ScopedStep)
    {
        ScopedGuess = (2.0f * ScopedGuess + ScopedMantissa / (ScopedGuess * ScopedGuess)) * (1.0f / 3.0f);
    }
    // ldexp is an exact power-of-two scaling, so it adds no rounding of its own.
    const float ScopedResult = std::ldexp(ScopedGuess, ScopedQuotient);
    return ScopedNegative ? -ScopedResult : ScopedResult;
}

// exp(x) = 2^k * exp(r), with |r| <= ln2 / 2 and a seven-term Taylor sum. The
// exponent split is exact and the polynomial steps are fused, so the result is
// bit-identical wherever the hardware implements the same IEEE operations.
[[nodiscard]] inline float Exp(float InValue) noexcept
{
    if (!std::isfinite(InValue))
    {
        if (InValue > 0.0f)
        {
            return std::numeric_limits<float>::infinity();
        }
        return InValue < 0.0f ? 0.0f : InValue;
    }
    // Saturate before rounding to an int: 2^128 is the last representable step.
    if (InValue > 88.722839f)
    {
        return std::numeric_limits<float>::infinity();
    }
    if (InValue < -103.0f)
    {
        return 0.0f;
    }
    constexpr float kLn2 = 0.6931471805599453f;
    const float ScopedK = std::nearbyint(InValue * (1.0f / kLn2));
    const float ScopedR = std::fma(-ScopedK, kLn2, InValue);
    float ScopedPoly = 1.0f / 5040.0f;
    ScopedPoly = std::fma(ScopedPoly, ScopedR, 1.0f / 720.0f);
    ScopedPoly = std::fma(ScopedPoly, ScopedR, 1.0f / 120.0f);
    ScopedPoly = std::fma(ScopedPoly, ScopedR, 1.0f / 24.0f);
    ScopedPoly = std::fma(ScopedPoly, ScopedR, 1.0f / 6.0f);
    ScopedPoly = std::fma(ScopedPoly, ScopedR, 0.5f);
    ScopedPoly = std::fma(ScopedPoly, ScopedR, 1.0f);
    const float ScopedUnit = std::fma(ScopedPoly, ScopedR, 1.0f);
    return std::ldexp(ScopedUnit, static_cast<int>(ScopedK));
}

// Natural log through exponent splitting and the atanh series on
// z = (m - 1) / (m + 1), which converges fast because |z| <= 0.172 on [1, 2).
[[nodiscard]] inline float Log(float InValue) noexcept
{
    if (!(InValue > 0.0f))
    {
        if (InValue == 0.0f)
        {
            return -std::numeric_limits<float>::infinity();
        }
        return std::numeric_limits<float>::quiet_NaN();
    }
    if (InValue == std::numeric_limits<float>::infinity())
    {
        return InValue;
    }
    constexpr float kLn2 = 0.6931471805599453f;
    std::uint32_t ScopedBits = std::bit_cast<std::uint32_t>(InValue);
    int ScopedExponent = static_cast<int>(ScopedBits >> 23u) - 127;
    ScopedBits = (ScopedBits & 0x007FFFFFu) | 0x3F800000u;
    float ScopedMantissa = std::bit_cast<float>(ScopedBits);
    // Keep the mantissa near 1: outside [1, sqrt(2)) half it and bump exponent.
    if (ScopedMantissa > 1.41421356f)
    {
        ScopedMantissa *= 0.5f;
        ++ScopedExponent;
    }
    const float ScopedZ = (ScopedMantissa - 1.0f) / (ScopedMantissa + 1.0f);
    const float ScopedSquare = ScopedZ * ScopedZ;
    float ScopedPoly = 1.0f / 11.0f;
    ScopedPoly = std::fma(ScopedPoly, ScopedSquare, 1.0f / 9.0f);
    ScopedPoly = std::fma(ScopedPoly, ScopedSquare, 1.0f / 7.0f);
    ScopedPoly = std::fma(ScopedPoly, ScopedSquare, 1.0f / 5.0f);
    ScopedPoly = std::fma(ScopedPoly, ScopedSquare, 1.0f / 3.0f);
    ScopedPoly = std::fma(ScopedPoly, ScopedSquare, 1.0f);
    const float ScopedMantissaLog = 2.0f * ScopedZ * ScopedPoly;
    return std::fma(static_cast<float>(ScopedExponent), kLn2, ScopedMantissaLog);
}

// pow through the deterministic exp/log pair. Negative bases are defined only
// for integral exponents, matching the reference function's useful domain.
[[nodiscard]] inline float Pow(float InBase, float InExponent) noexcept
{
    if (InExponent == 0.0f)
    {
        return 1.0f;
    }
    if (InBase == 0.0f)
    {
        return InExponent > 0.0f ? 0.0f : std::numeric_limits<float>::infinity();
    }
    if (!std::isfinite(InBase) || !std::isfinite(InExponent))
    {
        return std::pow(InBase, InExponent);
    }
    if (InBase < 0.0f)
    {
        const float ScopedWhole = std::trunc(InExponent);
        if (ScopedWhole != InExponent)
        {
            return std::numeric_limits<float>::quiet_NaN();
        }
        const float ScopedMagnitude = Exp(InExponent * Log(-InBase));
        const bool ScopedOdd = std::fmod(std::fabs(ScopedWhole), 2.0f) == 1.0f;
        return ScopedOdd ? -ScopedMagnitude : ScopedMagnitude;
    }
    return Exp(InExponent * Log(InBase));
}
}
