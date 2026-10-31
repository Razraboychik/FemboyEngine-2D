// FemboyEngine2D - fixed-width big integers.
#pragma once

#include <array>
#include <bit>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>

namespace Fby::BigNum
{

enum class Status : std::uint8_t
{
    Ok = 0,
    Overflow,
    DivideByZero,
    InvalidArgument,
    TooLarge,
    BufferTooSmall,
    Inexact,
};

// Display notation used by the number formatter.
enum class Notation : std::uint8_t
{
    Plain = 0,
    Scientific,
};

struct FormatSpec
{
    Notation Style = Notation::Plain;
    // Significant digits used by scientific notation.
    std::uint8_t SignificantDigits = 6;
    // The exponent marker is a character so callers can choose E, e or x10^.
    char ExponentMarker = 'E';
};

inline constexpr std::uint32_t kMaxExpansion = 8;

namespace Detail
{

[[nodiscard]] inline int CompareUnsigned(std::span<const std::uint32_t> InLeft,
                                         std::span<const std::uint32_t> InRight) noexcept
{
    const std::size_t ScopedCount = InLeft.size() < InRight.size() ? InLeft.size() : InRight.size();
    for (std::size_t ScopedIndex = ScopedCount; ScopedIndex > 0; --ScopedIndex)
    {
        const std::uint32_t ScopedLimb = ScopedIndex - 1;
        if (InLeft[ScopedLimb] != InRight[ScopedLimb])
        {
            return InLeft[ScopedLimb] < InRight[ScopedLimb] ? -1 : 1;
        }
    }
    if (InLeft.size() == InRight.size())
    {
        return 0;
    }
    return InLeft.size() < InRight.size() ? -1 : 1;
}

[[nodiscard]] inline bool IsZero(std::span<const std::uint32_t> InValue) noexcept
{
    for (const std::uint32_t ScopedLimb : InValue)
    {
        if (ScopedLimb != 0)
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] inline std::uint32_t AddUnsigned(std::span<const std::uint32_t> InLeft,
                                               std::span<const std::uint32_t> InRight,
                                               std::span<std::uint32_t> Out) noexcept
{
    std::uint64_t ScopedCarry = 0;
    for (std::size_t ScopedIndex = 0; ScopedIndex < InLeft.size(); ++ScopedIndex)
    {
        const std::uint64_t ScopedSum =
            static_cast<std::uint64_t>(InLeft[ScopedIndex]) + InRight[ScopedIndex] + ScopedCarry;
        Out[ScopedIndex] = static_cast<std::uint32_t>(ScopedSum);
        ScopedCarry = ScopedSum >> 32;
    }
    return static_cast<std::uint32_t>(ScopedCarry);
}

[[nodiscard]] inline std::uint32_t SubUnsigned(std::span<const std::uint32_t> InLeft,
                                               std::span<const std::uint32_t> InRight,
                                               std::span<std::uint32_t> Out) noexcept
{
    std::uint64_t ScopedBorrow = 0;
    for (std::size_t ScopedIndex = 0; ScopedIndex < InLeft.size(); ++ScopedIndex)
    {
        const std::uint64_t ScopedSubtrahend = static_cast<std::uint64_t>(InRight[ScopedIndex]) + ScopedBorrow;
        const std::uint64_t ScopedMinuend = InLeft[ScopedIndex];
        Out[ScopedIndex] = static_cast<std::uint32_t>(ScopedMinuend - ScopedSubtrahend);
        ScopedBorrow = ScopedMinuend < ScopedSubtrahend ? 1u : 0u;
    }
    return static_cast<std::uint32_t>(ScopedBorrow);
}

inline void NegateUnsigned(std::span<std::uint32_t> InLimbs) noexcept
{
    std::uint64_t ScopedCarry = 1;
    for (std::uint32_t& ScopedLimb : InLimbs)
    {
        const std::uint64_t ScopedValue = static_cast<std::uint64_t>(~ScopedLimb) + ScopedCarry;
        ScopedLimb = static_cast<std::uint32_t>(ScopedValue);
        ScopedCarry = ScopedValue >> 32;
    }
}

inline void MultiplyUnsigned(std::span<const std::uint32_t> InLeft, std::span<const std::uint32_t> InRight,
                             std::span<std::uint32_t> Out) noexcept
{
    for (std::uint32_t& ScopedLimb : Out)
    {
        ScopedLimb = 0;
    }
    for (std::size_t ScopedLeft = 0; ScopedLeft < InLeft.size(); ++ScopedLeft)
    {
        std::uint64_t ScopedCarry = 0;
        for (std::size_t ScopedRight = 0; ScopedRight < InRight.size(); ++ScopedRight)
        {
            const std::uint64_t ScopedProduct = static_cast<std::uint64_t>(InLeft[ScopedLeft]) * InRight[ScopedRight] +
                                                Out[ScopedLeft + ScopedRight] + ScopedCarry;
            Out[ScopedLeft + ScopedRight] = static_cast<std::uint32_t>(ScopedProduct);
            ScopedCarry = ScopedProduct >> 32;
        }
        Out[ScopedLeft + InRight.size()] = static_cast<std::uint32_t>(ScopedCarry);
    }
}

// Binary shift-subtract division. The caller uses this on magnitudes, so no sign
// bits participate; the quotient and remainder are both canonical magnitudes.
inline void DivideUnsigned(std::span<const std::uint32_t> InDividend, std::span<const std::uint32_t> InDivisor,
                           std::span<std::uint32_t> OutQuotient, std::span<std::uint32_t> OutRemainder) noexcept
{
    for (std::uint32_t& ScopedLimb : OutQuotient)
    {
        ScopedLimb = 0;
    }
    for (std::uint32_t& ScopedLimb : OutRemainder)
    {
        ScopedLimb = 0;
    }
    const std::uint32_t ScopedBits = static_cast<std::uint32_t>(InDividend.size() * 32);
    for (std::uint32_t ScopedBit = ScopedBits; ScopedBit > 0; --ScopedBit)
    {
        const std::uint32_t ScopedIndex = ScopedBit - 1;
        std::uint32_t ScopedCarry = 0;
        for (std::size_t ScopedLimb = 0; ScopedLimb < OutRemainder.size(); ++ScopedLimb)
        {
            const std::uint32_t ScopedNext = OutRemainder[ScopedLimb] >> 31;
            OutRemainder[ScopedLimb] = (OutRemainder[ScopedLimb] << 1) | ScopedCarry;
            ScopedCarry = ScopedNext;
        }
        OutRemainder[0] |= (InDividend[ScopedIndex / 32] >> (ScopedIndex % 32)) & 1u;
        if (CompareUnsigned(OutRemainder, InDivisor) >= 0)
        {
            (void)SubUnsigned(OutRemainder, InDivisor, OutRemainder);
            OutQuotient[ScopedIndex / 32] |= 1u << (ScopedIndex % 32);
        }
    }
}
}

// Fixed-width signed integer. The value is stored in two's complement so the
// object is exactly 16 bytes per 128-bit block and has no separate sign field.
template <std::uint32_t Expansion> struct BigIntT
{
    static_assert(Expansion >= 1, "a big integer needs at least one 128-bit block");
    static_assert(Expansion <= kMaxExpansion, "big integer expansion exceeds the engine limit");

    static constexpr std::uint32_t kLimbs = Expansion * 4u;
    static constexpr std::uint32_t kBits = kLimbs * 32u;
    static constexpr std::uint32_t kDecimalChunks = (kBits + 28u) / 29u;
    static constexpr std::uint32_t kDecimalCapacity = 1u + kDecimalChunks * 9u + 1u;

    std::array<std::uint32_t, kLimbs> Limbs{};

    [[nodiscard]] constexpr bool IsNegative() const noexcept
    {
        return (Limbs[kLimbs - 1] >> 31) != 0;
    }

    [[nodiscard]] constexpr bool IsZero() const noexcept
    {
        for (const std::uint32_t ScopedLimb : Limbs)
        {
            if (ScopedLimb != 0)
            {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] constexpr bool IsMinusOne() const noexcept
    {
        for (const std::uint32_t ScopedLimb : Limbs)
        {
            if (ScopedLimb != 0xFFFFFFFFu)
            {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] constexpr bool IsMinimum() const noexcept
    {
        if (!IsNegative() || Limbs[kLimbs - 1] != 0x80000000u)
        {
            return false;
        }
        for (std::uint32_t ScopedIndex = 0; ScopedIndex + 1 < kLimbs; ++ScopedIndex)
        {
            if (Limbs[ScopedIndex] != 0)
            {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] static constexpr BigIntT FromInt64(std::int64_t InValue) noexcept
    {
        BigIntT ScopedResult;
        const std::uint64_t ScopedValue = static_cast<std::uint64_t>(InValue);
        ScopedResult.Limbs[0] = static_cast<std::uint32_t>(ScopedValue);
        ScopedResult.Limbs[1] = static_cast<std::uint32_t>(ScopedValue >> 32);
        const std::uint32_t ScopedFill = InValue < 0 ? 0xFFFFFFFFu : 0u;
        for (std::uint32_t ScopedIndex = 2; ScopedIndex < kLimbs; ++ScopedIndex)
        {
            ScopedResult.Limbs[ScopedIndex] = ScopedFill;
        }
        return ScopedResult;
    }

    [[nodiscard]] static constexpr BigIntT FromUInt64(std::uint64_t InValue) noexcept
    {
        BigIntT ScopedResult;
        ScopedResult.Limbs[0] = static_cast<std::uint32_t>(InValue);
        ScopedResult.Limbs[1] = static_cast<std::uint32_t>(InValue >> 32);
        return ScopedResult;
    }

    template <typename T>
        requires(std::is_integral_v<T> && !std::is_same_v<std::remove_cv_t<T>, bool>)
    [[nodiscard]] static constexpr BigIntT FromIntegral(T InValue) noexcept
    {
        if constexpr (std::is_signed_v<T>)
        {
            return FromInt64(static_cast<std::int64_t>(InValue));
        }
        else
        {
            return FromUInt64(static_cast<std::uint64_t>(InValue));
        }
    }

    [[nodiscard]] friend BigIntT operator+(const BigIntT& InLeft, const BigIntT& InRight) noexcept
    {
        BigIntT ScopedResult;
        (void)InLeft.Add(InRight, &ScopedResult);
        return ScopedResult;
    }

    [[nodiscard]] friend BigIntT operator-(const BigIntT& InLeft, const BigIntT& InRight) noexcept
    {
        BigIntT ScopedResult;
        (void)InLeft.Sub(InRight, &ScopedResult);
        return ScopedResult;
    }

    [[nodiscard]] friend BigIntT operator*(const BigIntT& InLeft, const BigIntT& InRight) noexcept
    {
        BigIntT ScopedResult;
        (void)InLeft.Mul(InRight, &ScopedResult);
        return ScopedResult;
    }

    [[nodiscard]] friend BigIntT operator-(const BigIntT& InValue) noexcept
    {
        return InValue.Negated();
    }

    template <typename T>
        requires(std::is_integral_v<T> && !std::is_same_v<std::remove_cv_t<T>, bool>)
    [[nodiscard]] friend BigIntT operator+(const BigIntT& InLeft, T InRight) noexcept
    {
        return InLeft + FromIntegral(InRight);
    }

    template <typename T>
        requires(std::is_integral_v<T> && !std::is_same_v<std::remove_cv_t<T>, bool>)
    [[nodiscard]] friend BigIntT operator+(T InLeft, const BigIntT& InRight) noexcept
    {
        return FromIntegral(InLeft) + InRight;
    }

    template <typename T>
        requires(std::is_integral_v<T> && !std::is_same_v<std::remove_cv_t<T>, bool>)
    [[nodiscard]] friend BigIntT operator-(const BigIntT& InLeft, T InRight) noexcept
    {
        return InLeft - FromIntegral(InRight);
    }

    template <typename T>
        requires(std::is_integral_v<T> && !std::is_same_v<std::remove_cv_t<T>, bool>)
    [[nodiscard]] friend BigIntT operator-(T InLeft, const BigIntT& InRight) noexcept
    {
        return FromIntegral(InLeft) - InRight;
    }

    template <typename T>
        requires(std::is_integral_v<T> && !std::is_same_v<std::remove_cv_t<T>, bool>)
    [[nodiscard]] friend BigIntT operator*(const BigIntT& InLeft, T InRight) noexcept
    {
        return InLeft * FromIntegral(InRight);
    }

    template <typename T>
        requires(std::is_integral_v<T> && !std::is_same_v<std::remove_cv_t<T>, bool>)
    [[nodiscard]] friend BigIntT operator*(T InLeft, const BigIntT& InRight) noexcept
    {
        return FromIntegral(InLeft) * InRight;
    }

    [[nodiscard]] friend bool operator==(const BigIntT& InLeft, const BigIntT& InRight) noexcept
    {
        return InLeft.Compare(InRight) == 0;
    }

    [[nodiscard]] friend bool operator!=(const BigIntT& InLeft, const BigIntT& InRight) noexcept
    {
        return InLeft.Compare(InRight) != 0;
    }

    [[nodiscard]] friend bool operator<(const BigIntT& InLeft, const BigIntT& InRight) noexcept
    {
        return InLeft.Compare(InRight) < 0;
    }

    [[nodiscard]] friend bool operator<=(const BigIntT& InLeft, const BigIntT& InRight) noexcept
    {
        return InLeft.Compare(InRight) <= 0;
    }

    [[nodiscard]] friend bool operator>(const BigIntT& InLeft, const BigIntT& InRight) noexcept
    {
        return InLeft.Compare(InRight) > 0;
    }

    [[nodiscard]] friend bool operator>=(const BigIntT& InLeft, const BigIntT& InRight) noexcept
    {
        return InLeft.Compare(InRight) >= 0;
    }

    template <typename T>
        requires(std::is_integral_v<T> && !std::is_same_v<std::remove_cv_t<T>, bool>)
    [[nodiscard]] friend bool operator==(const BigIntT& InLeft, T InRight) noexcept
    {
        return InLeft == FromIntegral(InRight);
    }

    template <typename T>
        requires(std::is_integral_v<T> && !std::is_same_v<std::remove_cv_t<T>, bool>)
    [[nodiscard]] friend bool operator!=(const BigIntT& InLeft, T InRight) noexcept
    {
        return InLeft != FromIntegral(InRight);
    }

    template <typename T>
        requires(std::is_integral_v<T> && !std::is_same_v<std::remove_cv_t<T>, bool>)
    [[nodiscard]] friend bool operator<(const BigIntT& InLeft, T InRight) noexcept
    {
        return InLeft < FromIntegral(InRight);
    }

    template <typename T>
        requires(std::is_integral_v<T> && !std::is_same_v<std::remove_cv_t<T>, bool>)
    [[nodiscard]] friend bool operator>(const BigIntT& InLeft, T InRight) noexcept
    {
        return InLeft > FromIntegral(InRight);
    }

    [[nodiscard]] Status ToInt64(std::int64_t* OutValue) const noexcept
    {
        if (OutValue == nullptr)
        {
            return Status::InvalidArgument;
        }
        const std::uint64_t ScopedLow = static_cast<std::uint64_t>(Limbs[0]) |
                                        (static_cast<std::uint64_t>(Limbs[1]) << 32);
        if (IsNegative())
        {
            for (std::uint32_t ScopedIndex = 2; ScopedIndex < kLimbs; ++ScopedIndex)
            {
                if (Limbs[ScopedIndex] != 0xFFFFFFFFu)
                {
                    return Status::Overflow;
                }
            }
            if ((ScopedLow >> 63) == 0)
            {
                return Status::Overflow;
            }
            *OutValue = static_cast<std::int64_t>(ScopedLow);
            return Status::Ok;
        }
        for (std::uint32_t ScopedIndex = 2; ScopedIndex < kLimbs; ++ScopedIndex)
        {
            if (Limbs[ScopedIndex] != 0)
            {
                return Status::Overflow;
            }
        }
        if (ScopedLow > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
        {
            return Status::Overflow;
        }
        *OutValue = static_cast<std::int64_t>(ScopedLow);
        return Status::Ok;
    }

    [[nodiscard]] Status ToUInt64(std::uint64_t* OutValue) const noexcept
    {
        if (OutValue == nullptr || IsNegative())
        {
            return Status::InvalidArgument;
        }
        for (std::uint32_t ScopedIndex = 2; ScopedIndex < kLimbs; ++ScopedIndex)
        {
            if (Limbs[ScopedIndex] != 0)
            {
                return Status::Overflow;
            }
        }
        *OutValue = static_cast<std::uint64_t>(Limbs[0]) | (static_cast<std::uint64_t>(Limbs[1]) << 32);
        return Status::Ok;
    }

    [[nodiscard]] int Compare(const BigIntT& InOther) const noexcept
    {
        const bool ScopedLeftNegative = IsNegative();
        const bool ScopedRightNegative = InOther.IsNegative();
        if (ScopedLeftNegative != ScopedRightNegative)
        {
            return ScopedLeftNegative ? -1 : 1;
        }
        return Detail::CompareUnsigned(Limbs, InOther.Limbs);
    }

    [[nodiscard]] BigIntT Negated() const noexcept
    {
        BigIntT ScopedResult = *this;
        Detail::NegateUnsigned(ScopedResult.Limbs);
        return ScopedResult;
    }

    [[nodiscard]] Status Add(const BigIntT& InOther, BigIntT* OutResult) const noexcept
    {
        if (OutResult == nullptr)
        {
            return Status::InvalidArgument;
        }
        BigIntT ScopedResult;
        std::uint64_t ScopedCarry = 0;
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < kLimbs; ++ScopedIndex)
        {
            const std::uint64_t ScopedSum =
                static_cast<std::uint64_t>(Limbs[ScopedIndex]) + InOther.Limbs[ScopedIndex] + ScopedCarry;
            ScopedResult.Limbs[ScopedIndex] = static_cast<std::uint32_t>(ScopedSum);
            ScopedCarry = ScopedSum >> 32;
        }
        const bool ScopedOverflow = !IsNegative() == !InOther.IsNegative() &&
                                    ScopedResult.IsNegative() != IsNegative();
        *OutResult = ScopedResult;
        return ScopedOverflow ? Status::Overflow : Status::Ok;
    }

    [[nodiscard]] Status Sub(const BigIntT& InOther, BigIntT* OutResult) const noexcept
    {
        if (OutResult == nullptr)
        {
            return Status::InvalidArgument;
        }
        BigIntT ScopedResult;
        const BigIntT ScopedNegated = InOther.Negated();
        std::uint64_t ScopedCarry = 0;
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < kLimbs; ++ScopedIndex)
        {
            const std::uint64_t ScopedSum =
                static_cast<std::uint64_t>(Limbs[ScopedIndex]) + ScopedNegated.Limbs[ScopedIndex] + ScopedCarry;
            ScopedResult.Limbs[ScopedIndex] = static_cast<std::uint32_t>(ScopedSum);
            ScopedCarry = ScopedSum >> 32;
        }
        const bool ScopedOverflow =
            IsNegative() != InOther.IsNegative() && ScopedResult.IsNegative() != IsNegative();
        *OutResult = ScopedResult;
        return ScopedOverflow ? Status::Overflow : Status::Ok;
    }

    [[nodiscard]] Status Mul(const BigIntT& InOther, BigIntT* OutResult) const noexcept
    {
        if (OutResult == nullptr)
        {
            return Status::InvalidArgument;
        }
        std::array<std::uint32_t, kLimbs> ScopedLeftMagnitude{};
        std::array<std::uint32_t, kLimbs> ScopedRightMagnitude{};
        FillMagnitude(*this, ScopedLeftMagnitude);
        FillMagnitude(InOther, ScopedRightMagnitude);
        std::array<std::uint32_t, kLimbs * 2> ScopedProduct{};
        Detail::MultiplyUnsigned(ScopedLeftMagnitude, ScopedRightMagnitude, ScopedProduct);
        for (std::uint32_t ScopedIndex = kLimbs; ScopedIndex < kLimbs * 2; ++ScopedIndex)
        {
            if (ScopedProduct[ScopedIndex] != 0)
            {
                return Status::Overflow;
            }
        }
        std::array<std::uint32_t, kLimbs> ScopedMagnitude{};
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < kLimbs; ++ScopedIndex)
        {
            ScopedMagnitude[ScopedIndex] = ScopedProduct[ScopedIndex];
        }
        const bool ScopedNegative = IsNegative() != InOther.IsNegative();
        if ((ScopedNegative && !FitsNegative(ScopedMagnitude)) ||
            (!ScopedNegative && !FitsPositive(ScopedMagnitude)))
        {
            return Status::Overflow;
        }
        *OutResult = FromMagnitude(ScopedMagnitude, ScopedNegative);
        return Status::Ok;
    }

    [[nodiscard]] Status DivMod(const BigIntT& InDivisor, BigIntT* OutQuotient, BigIntT* OutRemainder) const noexcept
    {
        if (OutQuotient == nullptr || OutRemainder == nullptr)
        {
            return Status::InvalidArgument;
        }
        if (InDivisor.IsZero())
        {
            return Status::DivideByZero;
        }
        if (IsMinimum() && InDivisor.IsMinusOne())
        {
            return Status::Overflow;
        }
        std::array<std::uint32_t, kLimbs> ScopedDividendMagnitude{};
        std::array<std::uint32_t, kLimbs> ScopedDivisorMagnitude{};
        FillMagnitude(*this, ScopedDividendMagnitude);
        FillMagnitude(InDivisor, ScopedDivisorMagnitude);
        std::array<std::uint32_t, kLimbs> ScopedQuotientMagnitude{};
        std::array<std::uint32_t, kLimbs> ScopedRemainderMagnitude{};
        Detail::DivideUnsigned(ScopedDividendMagnitude, ScopedDivisorMagnitude, ScopedQuotientMagnitude,
                               ScopedRemainderMagnitude);
        const bool ScopedQuotientNegative = IsNegative() != InDivisor.IsNegative();
        if ((ScopedQuotientNegative && !FitsNegative(ScopedQuotientMagnitude)) ||
            (!ScopedQuotientNegative && !FitsPositive(ScopedQuotientMagnitude)))
        {
            return Status::Overflow;
        }
        *OutQuotient = FromMagnitude(ScopedQuotientMagnitude, ScopedQuotientNegative);
        *OutRemainder = FromMagnitude(ScopedRemainderMagnitude, IsNegative());
        return Status::Ok;
    }

    [[nodiscard]] Status ShiftLeft(std::uint32_t InBits, BigIntT* OutResult) const noexcept
    {
        if (OutResult == nullptr)
        {
            return Status::InvalidArgument;
        }
        BigIntT ScopedResult;
        if (InBits >= kBits)
        {
            const bool ScopedLost = !IsZero();
            OutResult->Limbs.fill(0);
            return ScopedLost ? Status::Overflow : Status::Ok;
        }
        const std::uint32_t ScopedWords = InBits / 32;
        const std::uint32_t ScopedBits = InBits % 32;
        bool ScopedLost = false;
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < kLimbs; ++ScopedIndex)
        {
            const std::uint32_t ScopedDestination = ScopedIndex + ScopedWords;
            if (ScopedDestination >= kLimbs)
            {
                ScopedLost = ScopedLost || Limbs[ScopedIndex] != 0;
                continue;
            }
            const std::uint64_t ScopedValue = static_cast<std::uint64_t>(Limbs[ScopedIndex]) << ScopedBits;
            ScopedResult.Limbs[ScopedDestination] |= static_cast<std::uint32_t>(ScopedValue);
            if (ScopedBits != 0)
            {
                const std::uint32_t ScopedHigh = static_cast<std::uint32_t>(ScopedValue >> 32);
                if (ScopedDestination + 1 < kLimbs)
                {
                    ScopedResult.Limbs[ScopedDestination + 1] |= ScopedHigh;
                }
                else if (ScopedHigh != 0)
                {
                    ScopedLost = true;
                }
            }
        }
        *OutResult = ScopedResult;
        return ScopedLost ? Status::Overflow : Status::Ok;
    }

    [[nodiscard]] Status ShiftRight(std::uint32_t InBits, BigIntT* OutResult) const noexcept
    {
        if (OutResult == nullptr)
        {
            return Status::InvalidArgument;
        }
        if (InBits >= kBits)
        {
            OutResult->Limbs.fill(IsNegative() ? 0xFFFFFFFFu : 0u);
            return Status::Ok;
        }
        const std::uint32_t ScopedWords = InBits / 32;
        const std::uint32_t ScopedBits = InBits % 32;
        const std::uint32_t ScopedFill = IsNegative() ? 0xFFFFFFFFu : 0u;
        BigIntT ScopedResult;
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < kLimbs; ++ScopedIndex)
        {
            const std::uint32_t ScopedLowIndex = ScopedIndex + ScopedWords;
            const std::uint32_t ScopedHighIndex = ScopedLowIndex + 1;
            const std::uint32_t ScopedLow = ScopedLowIndex < kLimbs ? Limbs[ScopedLowIndex] : ScopedFill;
            const std::uint32_t ScopedHigh = ScopedHighIndex < kLimbs ? Limbs[ScopedHighIndex] : ScopedFill;
            ScopedResult.Limbs[ScopedIndex] =
                ScopedBits == 0 ? ScopedLow : (ScopedLow >> ScopedBits) | (ScopedHigh << (32 - ScopedBits));
        }
        *OutResult = ScopedResult;
        return Status::Ok;
    }

    [[nodiscard]] std::uint32_t BitLength() const noexcept
    {
        std::array<std::uint32_t, kLimbs> ScopedMagnitude{};
        FillMagnitude(*this, ScopedMagnitude);
        for (std::uint32_t ScopedIndex = kLimbs; ScopedIndex > 0; --ScopedIndex)
        {
            const std::uint32_t ScopedLimb = ScopedMagnitude[ScopedIndex - 1];
            if (ScopedLimb != 0)
            {
                return (ScopedIndex - 1) * 32 + (32u - static_cast<std::uint32_t>(std::countl_zero(ScopedLimb)));
            }
        }
        return 0;
    }

    [[nodiscard]] Status Format(const FormatSpec& InSpec, std::span<char> OutBuffer,
                                std::size_t* OutSize = nullptr) const noexcept
    {
        if (InSpec.Style == Notation::Scientific)
        {
            return FormatScientific(InSpec, OutBuffer, OutSize);
        }
        return FormatPlain(OutBuffer, OutSize);
    }

    [[nodiscard]] Status FormatPlain(std::span<char> OutBuffer, std::size_t* OutSize = nullptr) const noexcept
    {
        std::array<char, kDecimalCapacity> ScopedText{};
        std::size_t ScopedSize = 0;
        const Status ScopedStatus = FormatPlainInternal(ScopedText, &ScopedSize);
        if (ScopedStatus != Status::Ok)
        {
            return ScopedStatus;
        }
        if (OutBuffer.size() < ScopedSize)
        {
            return Status::BufferTooSmall;
        }
        for (std::size_t ScopedIndex = 0; ScopedIndex < ScopedSize; ++ScopedIndex)
        {
            OutBuffer[ScopedIndex] = ScopedText[ScopedIndex];
        }
        if (OutSize != nullptr)
        {
            *OutSize = ScopedSize;
        }
        return Status::Ok;
    }

    [[nodiscard]] Status FormatScientific(const FormatSpec& InSpec, std::span<char> OutBuffer,
                                          std::size_t* OutSize = nullptr) const noexcept
    {
        std::array<char, kDecimalCapacity> ScopedText{};
        std::size_t ScopedTextSize = 0;
        const Status ScopedStatus = FormatPlainInternal(ScopedText, &ScopedTextSize);
        if (ScopedStatus != Status::Ok)
        {
            return ScopedStatus;
        }
        const bool ScopedNegative = ScopedTextSize > 0 && ScopedText[0] == '-';
        const std::size_t ScopedDigitOffset = ScopedNegative ? 1u : 0u;
        const std::size_t ScopedDigitCount = ScopedTextSize - ScopedDigitOffset;
        if (ScopedDigitCount == 0 || (ScopedDigitCount == 1 && ScopedText[ScopedDigitOffset] == '0'))
        {
            if (OutBuffer.size() < 1)
            {
                return Status::BufferTooSmall;
            }
            OutBuffer[0] = '0';
            if (OutSize != nullptr)
            {
                *OutSize = 1;
            }
            return Status::Ok;
        }
        std::uint32_t ScopedSignificant = InSpec.SignificantDigits == 0 ? 6u : InSpec.SignificantDigits;
        if (ScopedSignificant > ScopedDigitCount)
        {
            ScopedSignificant = static_cast<std::uint32_t>(ScopedDigitCount);
        }
        std::array<char, kDecimalCapacity + 32> ScopedResult{};
        std::size_t ScopedCursor = 0;
        if (ScopedNegative)
        {
            ScopedResult[ScopedCursor++] = '-';
        }
        ScopedResult[ScopedCursor++] = ScopedText[ScopedDigitOffset];
        if (ScopedSignificant > 1)
        {
            ScopedResult[ScopedCursor++] = '.';
            for (std::uint32_t ScopedIndex = 1; ScopedIndex < ScopedSignificant; ++ScopedIndex)
            {
                ScopedResult[ScopedCursor++] = ScopedText[ScopedDigitOffset + ScopedIndex];
            }
        }
        ScopedResult[ScopedCursor++] = InSpec.ExponentMarker == '\0' ? 'E' : InSpec.ExponentMarker;
        std::array<char, 24> ScopedExponent{};
        const std::int64_t ScopedExponentValue = static_cast<std::int64_t>(ScopedDigitCount) - 1;
        const auto ScopedConversion =
            std::to_chars(ScopedExponent.data(), ScopedExponent.data() + ScopedExponent.size(), ScopedExponentValue);
        if (ScopedConversion.ec != std::errc{})
        {
            return Status::InvalidArgument;
        }
        const std::size_t ScopedExponentSize = static_cast<std::size_t>(ScopedConversion.ptr - ScopedExponent.data());
        if (ScopedCursor + ScopedExponentSize > ScopedResult.size())
        {
            return Status::TooLarge;
        }
        for (std::size_t ScopedIndex = 0; ScopedIndex < ScopedExponentSize; ++ScopedIndex)
        {
            ScopedResult[ScopedCursor++] = ScopedExponent[ScopedIndex];
        }
        if (OutBuffer.size() < ScopedCursor)
        {
            return Status::BufferTooSmall;
        }
        for (std::size_t ScopedIndex = 0; ScopedIndex < ScopedCursor; ++ScopedIndex)
        {
            OutBuffer[ScopedIndex] = ScopedResult[ScopedIndex];
        }
        if (OutSize != nullptr)
        {
            *OutSize = ScopedCursor;
        }
        return Status::Ok;
    }

    [[nodiscard]] std::string ToString() const
    {
        std::array<char, kDecimalCapacity> ScopedBuffer{};
        std::size_t ScopedSize = 0;
        const Status ScopedStatus = FormatPlain(std::span<char>(ScopedBuffer), &ScopedSize);
        if (ScopedStatus != Status::Ok)
        {
            return {};
        }
        return std::string(ScopedBuffer.data(), ScopedSize);
    }

    [[nodiscard]] static Status FromString(std::string_view InText, BigIntT* OutValue) noexcept
    {
        if (OutValue == nullptr || InText.empty())
        {
            return Status::InvalidArgument;
        }
        std::size_t ScopedCursor = 0;
        bool ScopedNegative = false;
        if (InText[0] == '-' || InText[0] == '+')
        {
            ScopedNegative = InText[0] == '-';
            ScopedCursor = 1;
        }
        if (ScopedCursor >= InText.size())
        {
            return Status::InvalidArgument;
        }
        std::array<std::uint32_t, kLimbs> ScopedMagnitude{};
        for (; ScopedCursor < InText.size(); ++ScopedCursor)
        {
            const char ScopedCharacter = InText[ScopedCursor];
            if (ScopedCharacter < '0' || ScopedCharacter > '9')
            {
                return Status::InvalidArgument;
            }
            std::uint64_t ScopedCarry = static_cast<std::uint64_t>(ScopedCharacter - '0');
            for (std::uint32_t ScopedLimb = 0; ScopedLimb < kLimbs; ++ScopedLimb)
            {
                const std::uint64_t ScopedValue =
                    static_cast<std::uint64_t>(ScopedMagnitude[ScopedLimb]) * 10u + ScopedCarry;
                ScopedMagnitude[ScopedLimb] = static_cast<std::uint32_t>(ScopedValue);
                ScopedCarry = ScopedValue >> 32;
            }
            if (ScopedCarry != 0)
            {
                return Status::Overflow;
            }
        }
        if ((ScopedNegative && !FitsNegative(ScopedMagnitude)) ||
            (!ScopedNegative && !FitsPositive(ScopedMagnitude)))
        {
            return Status::Overflow;
        }
        *OutValue = FromMagnitude(ScopedMagnitude, ScopedNegative);
        return Status::Ok;
    }

private:
    static void FillMagnitude(const BigIntT& InValue, std::array<std::uint32_t, kLimbs>& Out) noexcept
    {
        Out = InValue.Limbs;
        if (InValue.IsNegative())
        {
            Detail::NegateUnsigned(Out);
        }
    }

    [[nodiscard]] static constexpr bool FitsPositive(const std::array<std::uint32_t, kLimbs>& InMagnitude) noexcept
    {
        return (InMagnitude[kLimbs - 1] >> 31) == 0;
    }

    [[nodiscard]] static constexpr bool FitsNegative(const std::array<std::uint32_t, kLimbs>& InMagnitude) noexcept
    {
        if ((InMagnitude[kLimbs - 1] >> 31) == 0)
        {
            return true;
        }
        if (InMagnitude[kLimbs - 1] != 0x80000000u)
        {
            return false;
        }
        for (std::uint32_t ScopedIndex = 0; ScopedIndex + 1 < kLimbs; ++ScopedIndex)
        {
            if (InMagnitude[ScopedIndex] != 0)
            {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] static BigIntT FromMagnitude(const std::array<std::uint32_t, kLimbs>& InMagnitude,
                                               bool InNegative) noexcept
    {
        BigIntT ScopedResult;
        ScopedResult.Limbs = InMagnitude;
        if (InNegative)
        {
            Detail::NegateUnsigned(ScopedResult.Limbs);
        }
        return ScopedResult;
    }

    [[nodiscard]] Status FormatPlainInternal(std::span<char> OutBuffer, std::size_t* OutSize) const noexcept
    {
        std::array<std::uint32_t, kLimbs> ScopedMagnitude{};
        FillMagnitude(*this, ScopedMagnitude);
        std::array<std::uint32_t, kDecimalChunks> ScopedChunks{};
        std::uint32_t ScopedChunkCount = 0;
        while (!Detail::IsZero(ScopedMagnitude))
        {
            std::uint64_t ScopedRemainder = 0;
            for (std::uint32_t ScopedIndex = kLimbs; ScopedIndex > 0; --ScopedIndex)
            {
                const std::uint64_t ScopedValue = (ScopedRemainder << 32) | ScopedMagnitude[ScopedIndex - 1];
                ScopedMagnitude[ScopedIndex - 1] = static_cast<std::uint32_t>(ScopedValue / 1000000000u);
                ScopedRemainder = ScopedValue % 1000000000u;
            }
            if (ScopedChunkCount >= ScopedChunks.size())
            {
                return Status::TooLarge;
            }
            ScopedChunks[ScopedChunkCount++] = static_cast<std::uint32_t>(ScopedRemainder);
        }
        std::size_t ScopedCursor = 0;
        if (IsNegative())
        {
            if (ScopedCursor >= OutBuffer.size())
            {
                return Status::BufferTooSmall;
            }
            OutBuffer[ScopedCursor++] = '-';
        }
        if (ScopedChunkCount == 0)
        {
            if (ScopedCursor >= OutBuffer.size())
            {
                return Status::BufferTooSmall;
            }
            OutBuffer[ScopedCursor++] = '0';
        }
        else
        {
            for (std::uint32_t ScopedChunk = ScopedChunkCount; ScopedChunk > 0; --ScopedChunk)
            {
                const std::uint32_t ScopedValue = ScopedChunks[ScopedChunk - 1];
                if (ScopedChunk == ScopedChunkCount)
                {
                    std::array<char, 10> ScopedDigits{};
                    std::uint32_t ScopedDigitCount = 0;
                    std::uint32_t ScopedRemaining = ScopedValue;
                    do
                    {
                        ScopedDigits[ScopedDigitCount++] = static_cast<char>('0' + ScopedRemaining % 10u);
                        ScopedRemaining /= 10u;
                    } while (ScopedRemaining != 0);
                    for (std::uint32_t ScopedIndex = ScopedDigitCount; ScopedIndex > 0; --ScopedIndex)
                    {
                        if (ScopedCursor >= OutBuffer.size())
                        {
                            return Status::BufferTooSmall;
                        }
                        OutBuffer[ScopedCursor++] = ScopedDigits[ScopedIndex - 1];
                    }
                }
                else
                {
                    constexpr std::array<std::uint32_t, 9> kPowers{1u,       10u,      100u,     1000u,   10000u,
                                                                   100000u,  1000000u, 10000000u, 100000000u};
                    for (std::uint32_t ScopedDigit = 9; ScopedDigit > 0; --ScopedDigit)
                    {
                        if (ScopedCursor >= OutBuffer.size())
                        {
                            return Status::BufferTooSmall;
                        }
                        OutBuffer[ScopedCursor++] =
                            static_cast<char>('0' + (ScopedValue / kPowers[ScopedDigit - 1]) % 10u);
                    }
                }
            }
        }
        if (OutSize != nullptr)
        {
            *OutSize = ScopedCursor;
        }
        return Status::Ok;
    }
};

using Int128 = BigIntT<1>;
using Int256 = BigIntT<2>;
using Int512 = BigIntT<4>;

static_assert(sizeof(Int128) == 16, "Int128 must stay at 16 bytes");
static_assert(sizeof(Int256) == 32, "Int256 must stay at 32 bytes");
static_assert(std::is_trivially_copyable_v<Int128>);
static_assert(std::is_standard_layout_v<Int128>);
}
