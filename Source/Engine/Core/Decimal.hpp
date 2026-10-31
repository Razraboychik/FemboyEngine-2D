// FemboyEngine2D - fixed-width signed decimal numbers.
#pragma once

#include "Engine/Core/BigInt.hpp"

#include <array>
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

inline constexpr std::uint32_t kDefaultDecimalPrecision = 6;
inline constexpr std::uint32_t kMaxDecimalAlignDigits = 64;

enum class RoundingMode : std::uint8_t
{
    TowardZero = 0,
    HalfUp,
    HalfEven,
    Floor,
    Ceil,
};

// A decimal value is sign * coefficient * 10^exponent. The coefficient is a
// non-negative fixed-width integer and the sign is kept separately so exact
// decimal fractions and signed deltas are both representable.
template <std::uint32_t Expansion = 1> struct DecimalT
{
    BigIntT<Expansion> Coefficient{};
    std::int32_t Exponent = 0;
    bool Negative = false;

    [[nodiscard]] constexpr bool IsZero() const noexcept
    {
        return Coefficient.IsZero();
    }

    [[nodiscard]] static DecimalT FromInt64(std::int64_t InValue) noexcept
    {
        DecimalT ScopedResult;
        const std::uint64_t ScopedMagnitude =
            InValue < 0 ? 0ull - static_cast<std::uint64_t>(InValue) : static_cast<std::uint64_t>(InValue);
        ScopedResult.Coefficient = BigIntT<Expansion>::FromUInt64(ScopedMagnitude);
        ScopedResult.Negative = InValue < 0;
        ScopedResult.Normalize();
        return ScopedResult;
    }

    [[nodiscard]] static DecimalT FromUInt64(std::uint64_t InValue) noexcept
    {
        DecimalT ScopedResult;
        ScopedResult.Coefficient = BigIntT<Expansion>::FromUInt64(InValue);
        ScopedResult.Negative = false;
        ScopedResult.Normalize();
        return ScopedResult;
    }

    template <typename T>
        requires(std::is_integral_v<T> && !std::is_same_v<std::remove_cv_t<T>, bool>)
    [[nodiscard]] static DecimalT FromIntegral(T InValue) noexcept
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

    [[nodiscard]] static DecimalT FromBigInt(const BigIntT<Expansion>& InValue) noexcept
    {
        DecimalT ScopedResult;
        if (FromString(InValue.ToString(), &ScopedResult) != Status::Ok)
        {
            return {};
        }
        return ScopedResult;
    }

    [[nodiscard]] static Status FromString(std::string_view InText, DecimalT* OutValue) noexcept
    {
        if (OutValue == nullptr || InText.empty())
        {
            return Status::InvalidArgument;
        }
        std::size_t ScopedCursor = 0;
        bool ScopedNegative = false;
        if (InText[ScopedCursor] == '-' || InText[ScopedCursor] == '+')
        {
            ScopedNegative = InText[ScopedCursor] == '-';
            ++ScopedCursor;
        }
        if (ScopedCursor >= InText.size())
        {
            return Status::InvalidArgument;
        }

        std::string ScopedDigits;
        std::uint32_t ScopedFractionDigits = 0;
        bool ScopedFraction = false;
        bool ScopedAnyDigit = false;
        for (; ScopedCursor < InText.size(); ++ScopedCursor)
        {
            const char ScopedCharacter = InText[ScopedCursor];
            if (ScopedCharacter >= '0' && ScopedCharacter <= '9')
            {
                ScopedDigits.push_back(ScopedCharacter);
                ScopedAnyDigit = true;
                if (ScopedFraction)
                {
                    ++ScopedFractionDigits;
                }
                continue;
            }
            if (ScopedCharacter == '.' && !ScopedFraction)
            {
                ScopedFraction = true;
                continue;
            }
            break;
        }
        if (!ScopedAnyDigit)
        {
            return Status::InvalidArgument;
        }

        std::int64_t ScopedExplicitExponent = 0;
        if (ScopedCursor < InText.size() && (InText[ScopedCursor] == 'e' || InText[ScopedCursor] == 'E'))
        {
            ++ScopedCursor;
            bool ScopedExponentNegative = false;
            if (ScopedCursor < InText.size() &&
                (InText[ScopedCursor] == '-' || InText[ScopedCursor] == '+'))
            {
                ScopedExponentNegative = InText[ScopedCursor] == '-';
                ++ScopedCursor;
            }
            if (ScopedCursor >= InText.size())
            {
                return Status::InvalidArgument;
            }
            std::uint64_t ScopedExponentValue = 0;
            for (; ScopedCursor < InText.size(); ++ScopedCursor)
            {
                const char ScopedCharacter = InText[ScopedCursor];
                if (ScopedCharacter < '0' || ScopedCharacter > '9')
                {
                    return Status::InvalidArgument;
                }
                const std::uint64_t ScopedDigit = static_cast<std::uint64_t>(ScopedCharacter - '0');
                if (ScopedExponentValue > (std::numeric_limits<std::uint64_t>::max() - ScopedDigit) / 10u)
                {
                    return Status::Overflow;
                }
                ScopedExponentValue = ScopedExponentValue * 10u + ScopedDigit;
            }
            if (ScopedExponentNegative)
            {
                if (ScopedExponentValue > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
                {
                    return Status::Overflow;
                }
                ScopedExplicitExponent = -static_cast<std::int64_t>(ScopedExponentValue);
            }
            else
            {
                if (ScopedExponentValue > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
                {
                    return Status::Overflow;
                }
                ScopedExplicitExponent = static_cast<std::int64_t>(ScopedExponentValue);
            }
        }
        if (ScopedCursor != InText.size())
        {
            return Status::InvalidArgument;
        }

        BigIntT<Expansion> ScopedCoefficient;
        const Status ScopedStatus = BigIntT<Expansion>::FromString(ScopedDigits, &ScopedCoefficient);
        if (ScopedStatus != Status::Ok)
        {
            return ScopedStatus;
        }
        const std::int64_t ScopedExponent = ScopedExplicitExponent - static_cast<std::int64_t>(ScopedFractionDigits);
        if (ScopedExponent < std::numeric_limits<std::int32_t>::min() ||
            ScopedExponent > std::numeric_limits<std::int32_t>::max())
        {
            return Status::Overflow;
        }
        DecimalT ScopedResult;
        ScopedResult.Coefficient = ScopedCoefficient;
        ScopedResult.Exponent = static_cast<std::int32_t>(ScopedExponent);
        ScopedResult.Negative = ScopedNegative && !ScopedCoefficient.IsZero();
        ScopedResult.Normalize();
        *OutValue = ScopedResult;
        return Status::Ok;
    }

    void Normalize() noexcept
    {
        if (Coefficient.IsZero())
        {
            Negative = false;
            Exponent = 0;
            return;
        }
        if (Coefficient.IsNegative())
        {
            Negative = true;
            Coefficient = Coefficient.Negated();
        }
        while (true)
        {
            BigIntT<Expansion> ScopedQuotient;
            BigIntT<Expansion> ScopedRemainder;
            if (Coefficient.DivMod(BigIntT<Expansion>::FromInt64(10), &ScopedQuotient, &ScopedRemainder) != Status::Ok ||
                !ScopedRemainder.IsZero())
            {
                break;
            }
            if (Exponent == std::numeric_limits<std::int32_t>::max())
            {
                break;
            }
            Coefficient = ScopedQuotient;
            ++Exponent;
        }
    }

    [[nodiscard]] DecimalT Normalized() const noexcept
    {
        DecimalT ScopedResult = *this;
        ScopedResult.Normalize();
        return ScopedResult;
    }

    [[nodiscard]] int Compare(const DecimalT& InOther) const noexcept
    {
        const DecimalT ScopedLeft = Normalized();
        const DecimalT ScopedRight = InOther.Normalized();
        if (ScopedLeft.IsZero() && ScopedRight.IsZero())
        {
            return 0;
        }
        if (ScopedLeft.Negative != ScopedRight.Negative)
        {
            return ScopedLeft.Negative ? -1 : 1;
        }
        int ScopedComparison = CompareMagnitude(ScopedLeft, ScopedRight);
        return ScopedLeft.Negative ? -ScopedComparison : ScopedComparison;
    }

    [[nodiscard]] Status Add(const DecimalT& InOther, DecimalT* OutResult) const noexcept
    {
        if (OutResult == nullptr)
        {
            return Status::InvalidArgument;
        }
        DecimalT ScopedLeft = Normalized();
        DecimalT ScopedRight = InOther.Normalized();
        if (ScopedLeft.IsZero())
        {
            *OutResult = ScopedRight;
            return Status::Ok;
        }
        if (ScopedRight.IsZero())
        {
            *OutResult = ScopedLeft;
            return Status::Ok;
        }

        const std::int64_t ScopedDifference =
            static_cast<std::int64_t>(ScopedLeft.Exponent) - static_cast<std::int64_t>(ScopedRight.Exponent);
        if (ScopedDifference > static_cast<std::int64_t>(kMaxDecimalAlignDigits) ||
            ScopedDifference < -static_cast<std::int64_t>(kMaxDecimalAlignDigits))
        {
            const int ScopedComparison = CompareMagnitude(ScopedLeft, ScopedRight);
            *OutResult = ScopedComparison >= 0 ? ScopedLeft : ScopedRight;
            return Status::Inexact;
        }

        if (ScopedDifference > 0)
        {
            if (!ScaleByPowerOfTen(ScopedLeft.Coefficient, static_cast<std::uint32_t>(ScopedDifference)))
            {
                const int ScopedComparison = CompareMagnitude(ScopedLeft, ScopedRight);
                *OutResult = ScopedComparison >= 0 ? ScopedLeft : ScopedRight;
                return Status::Inexact;
            }
            ScopedLeft.Exponent = ScopedRight.Exponent;
        }
        else if (ScopedDifference < 0)
        {
            if (!ScaleByPowerOfTen(ScopedRight.Coefficient, static_cast<std::uint32_t>(-ScopedDifference)))
            {
                const int ScopedComparison = CompareMagnitude(ScopedLeft, ScopedRight);
                *OutResult = ScopedComparison >= 0 ? ScopedLeft : ScopedRight;
                return Status::Inexact;
            }
            ScopedRight.Exponent = ScopedLeft.Exponent;
        }

        DecimalT ScopedResult;
        ScopedResult.Exponent = ScopedLeft.Exponent;
        if (ScopedLeft.Negative == ScopedRight.Negative)
        {
            if (ScopedLeft.Coefficient.Add(ScopedRight.Coefficient, &ScopedResult.Coefficient) != Status::Ok)
            {
                return Status::Overflow;
            }
            ScopedResult.Negative = ScopedLeft.Negative;
        }
        else
        {
            const int ScopedComparison = ScopedLeft.Coefficient.Compare(ScopedRight.Coefficient);
            if (ScopedComparison == 0)
            {
                *OutResult = {};
                return Status::Ok;
            }
            if (ScopedComparison > 0)
            {
                if (ScopedLeft.Coefficient.Sub(ScopedRight.Coefficient, &ScopedResult.Coefficient) != Status::Ok)
                {
                    return Status::Overflow;
                }
                ScopedResult.Negative = ScopedLeft.Negative;
            }
            else
            {
                if (ScopedRight.Coefficient.Sub(ScopedLeft.Coefficient, &ScopedResult.Coefficient) != Status::Ok)
                {
                    return Status::Overflow;
                }
                ScopedResult.Negative = ScopedRight.Negative;
            }
        }
        ScopedResult.Normalize();
        *OutResult = ScopedResult;
        return Status::Ok;
    }

    [[nodiscard]] Status Sub(const DecimalT& InOther, DecimalT* OutResult) const noexcept
    {
        DecimalT ScopedOther = InOther;
        if (!ScopedOther.IsZero())
        {
            ScopedOther.Negative = !ScopedOther.Negative;
        }
        return Add(ScopedOther, OutResult);
    }

    [[nodiscard]] Status Mul(const DecimalT& InOther, DecimalT* OutResult) const noexcept
    {
        if (OutResult == nullptr)
        {
            return Status::InvalidArgument;
        }
        DecimalT ScopedResult;
        if (Coefficient.Mul(InOther.Coefficient, &ScopedResult.Coefficient) != Status::Ok)
        {
            return Status::Overflow;
        }
        const std::int64_t ScopedExponent =
            static_cast<std::int64_t>(Exponent) + static_cast<std::int64_t>(InOther.Exponent);
        if (ScopedExponent < std::numeric_limits<std::int32_t>::min() ||
            ScopedExponent > std::numeric_limits<std::int32_t>::max())
        {
            return Status::Overflow;
        }
        ScopedResult.Exponent = static_cast<std::int32_t>(ScopedExponent);
        ScopedResult.Negative = Negative != InOther.Negative;
        ScopedResult.Normalize();
        *OutResult = ScopedResult;
        return Status::Ok;
    }

    [[nodiscard]] Status Div(const DecimalT& InOther, DecimalT* OutResult,
                             std::uint32_t InPrecision = kDefaultDecimalPrecision,
                             RoundingMode InRounding = RoundingMode::HalfEven) const noexcept
    {
        if (OutResult == nullptr)
        {
            return Status::InvalidArgument;
        }
        if (InOther.IsZero())
        {
            return Status::DivideByZero;
        }
        if (IsZero())
        {
            *OutResult = {};
            return Status::Ok;
        }
        const DecimalT ScopedLeft = Normalized();
        const DecimalT ScopedRight = InOther.Normalized();
        const std::uint32_t ScopedLeftDigits = DigitCount(ScopedLeft.Coefficient);
        const std::uint32_t ScopedRightDigits = DigitCount(ScopedRight.Coefficient);
        const std::int64_t ScopedWanted = static_cast<std::int64_t>(InPrecision) +
                                          static_cast<std::int64_t>(ScopedRightDigits) -
                                          static_cast<std::int64_t>(ScopedLeftDigits);
        const std::uint32_t ScopedScale =
            ScopedWanted <= 0 ? 0u : static_cast<std::uint32_t>(ScopedWanted);
        if (ScopedScale > kMaxDecimalAlignDigits)
        {
            return Status::Overflow;
        }
        BigIntT<Expansion> ScopedScaled = ScopedLeft.Coefficient;
        if (!ScaleByPowerOfTen(ScopedScaled, ScopedScale))
        {
            return Status::Overflow;
        }
        BigIntT<Expansion> ScopedQuotient;
        BigIntT<Expansion> ScopedRemainder;
        if (ScopedScaled.DivMod(ScopedRight.Coefficient, &ScopedQuotient, &ScopedRemainder) != Status::Ok)
        {
            return Status::Overflow;
        }
        const bool ScopedNegative = ScopedLeft.Negative != ScopedRight.Negative;
        if (!ScopedRemainder.IsZero())
        {
            int ScopedHalfComparison = 0;
            BigIntT<Expansion> ScopedDoubled;
            if (ScopedRemainder.ShiftLeft(1, &ScopedDoubled) == Status::Overflow)
            {
                ScopedHalfComparison = 1;
            }
            else
            {
                ScopedHalfComparison = ScopedDoubled.Compare(ScopedRight.Coefficient);
            }
            bool ScopedIncrement = false;
            switch (InRounding)
            {
                case RoundingMode::TowardZero:
                    break;
                case RoundingMode::HalfUp:
                    ScopedIncrement = ScopedHalfComparison >= 0;
                    break;
                case RoundingMode::HalfEven:
                    ScopedIncrement = ScopedHalfComparison > 0 ||
                                      (ScopedHalfComparison == 0 && (ScopedQuotient.Limbs[0] & 1u) != 0);
                    break;
                case RoundingMode::Floor:
                    ScopedIncrement = ScopedNegative;
                    break;
                case RoundingMode::Ceil:
                    ScopedIncrement = !ScopedNegative;
                    break;
            }
            if (ScopedIncrement)
            {
                BigIntT<Expansion> ScopedIncremented;
                if (ScopedQuotient.Add(BigIntT<Expansion>::FromInt64(1), &ScopedIncremented) != Status::Ok)
                {
                    return Status::Overflow;
                }
                ScopedQuotient = ScopedIncremented;
            }
        }
        const std::int64_t ScopedExponent = static_cast<std::int64_t>(ScopedLeft.Exponent) -
                                            static_cast<std::int64_t>(ScopedRight.Exponent) -
                                            static_cast<std::int64_t>(ScopedScale);
        if (ScopedExponent < std::numeric_limits<std::int32_t>::min() ||
            ScopedExponent > std::numeric_limits<std::int32_t>::max())
        {
            return Status::Overflow;
        }
        DecimalT ScopedResult;
        ScopedResult.Coefficient = ScopedQuotient;
        ScopedResult.Exponent = static_cast<std::int32_t>(ScopedExponent);
        ScopedResult.Negative = ScopedLeft.Negative != ScopedRight.Negative;
        ScopedResult.Normalize();
        *OutResult = ScopedResult;
        return ScopedRemainder.IsZero() ? Status::Ok : Status::Inexact;
    }

    [[nodiscard]] Status ToInt64(std::int64_t* OutValue) const noexcept
    {
        if (OutValue == nullptr)
        {
            return Status::InvalidArgument;
        }
        if (Exponent != 0)
        {
            return Status::InvalidArgument;
        }
        std::uint64_t ScopedMagnitude = 0;
        if (Coefficient.ToUInt64(&ScopedMagnitude) != Status::Ok)
        {
            return Status::Overflow;
        }
        if (Negative)
        {
            if (ScopedMagnitude > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) + 1ull)
            {
                return Status::Overflow;
            }
            if (ScopedMagnitude == static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) + 1ull)
            {
                *OutValue = std::numeric_limits<std::int64_t>::min();
                return Status::Ok;
            }
            *OutValue = -static_cast<std::int64_t>(ScopedMagnitude);
            return Status::Ok;
        }
        if (ScopedMagnitude > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
        {
            return Status::Overflow;
        }
        *OutValue = static_cast<std::int64_t>(ScopedMagnitude);
        return Status::Ok;
    }

    [[nodiscard]] Status FormatPlain(std::span<char> OutBuffer, std::size_t* OutSize = nullptr) const noexcept
    {
        const DecimalT ScopedValue = Normalized();
        std::array<char, BigIntT<Expansion>::kDecimalCapacity> ScopedDigits{};
        std::size_t ScopedDigitCount = 0;
        if (ScopedValue.Coefficient.FormatPlain(std::span<char>(ScopedDigits), &ScopedDigitCount) != Status::Ok)
        {
            return Status::InvalidArgument;
        }
        std::size_t ScopedCursor = 0;
        const auto ScopedPut = [&ScopedCursor, OutBuffer](char InCharacter) noexcept
        {
            if (ScopedCursor >= OutBuffer.size())
            {
                return false;
            }
            OutBuffer[ScopedCursor++] = InCharacter;
            return true;
        };
        if (ScopedValue.Negative && !ScopedPut('-'))
        {
            return Status::BufferTooSmall;
        }
        if (ScopedValue.IsZero())
        {
            if (!ScopedPut('0'))
            {
                return Status::BufferTooSmall;
            }
        }
        else if (ScopedValue.Exponent >= 0)
        {
            for (std::size_t ScopedIndex = 0; ScopedIndex < ScopedDigitCount; ++ScopedIndex)
            {
                if (!ScopedPut(ScopedDigits[ScopedIndex]))
                {
                    return Status::BufferTooSmall;
                }
            }
            for (std::int32_t ScopedIndex = 0; ScopedIndex < ScopedValue.Exponent; ++ScopedIndex)
            {
                if (!ScopedPut('0'))
                {
                    return Status::BufferTooSmall;
                }
            }
        }
        else
        {
            const std::int64_t ScopedPoint =
                static_cast<std::int64_t>(ScopedDigitCount) + ScopedValue.Exponent;
            if (ScopedPoint > 0)
            {
                for (std::int64_t ScopedIndex = 0; ScopedIndex < ScopedPoint; ++ScopedIndex)
                {
                    if (!ScopedPut(ScopedDigits[static_cast<std::size_t>(ScopedIndex)]))
                    {
                        return Status::BufferTooSmall;
                    }
                }
                if (!ScopedPut('.'))
                {
                    return Status::BufferTooSmall;
                }
                for (std::size_t ScopedIndex = static_cast<std::size_t>(ScopedPoint); ScopedIndex < ScopedDigitCount;
                     ++ScopedIndex)
                {
                    if (!ScopedPut(ScopedDigits[ScopedIndex]))
                    {
                        return Status::BufferTooSmall;
                    }
                }
            }
            else
            {
                if (!ScopedPut('0') || !ScopedPut('.'))
                {
                    return Status::BufferTooSmall;
                }
                for (std::int64_t ScopedIndex = 0; ScopedIndex < -ScopedPoint; ++ScopedIndex)
                {
                    if (!ScopedPut('0'))
                    {
                        return Status::BufferTooSmall;
                    }
                }
                for (std::size_t ScopedIndex = 0; ScopedIndex < ScopedDigitCount; ++ScopedIndex)
                {
                    if (!ScopedPut(ScopedDigits[ScopedIndex]))
                    {
                        return Status::BufferTooSmall;
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

    [[nodiscard]] Status Format(const FormatSpec& InSpec, std::span<char> OutBuffer,
                                std::size_t* OutSize = nullptr) const noexcept
    {
        if (InSpec.Style == Notation::Scientific)
        {
            return FormatScientific(InSpec, OutBuffer, OutSize);
        }
        return FormatPlain(OutBuffer, OutSize);
    }

    [[nodiscard]] std::string ToString() const
    {
        std::array<char, 4096> ScopedBuffer{};
        std::size_t ScopedSize = 0;
        if (FormatPlain(std::span<char>(ScopedBuffer), &ScopedSize) != Status::Ok)
        {
            return {};
        }
        return std::string(ScopedBuffer.data(), ScopedSize);
    }

    [[nodiscard]] Status FormatScientific(const FormatSpec& InSpec, std::span<char> OutBuffer,
                                          std::size_t* OutSize = nullptr) const noexcept
    {
        const DecimalT ScopedValue = Normalized();
        if (ScopedValue.IsZero())
        {
            if (OutBuffer.empty())
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
        std::array<char, BigIntT<Expansion>::kDecimalCapacity> ScopedDigits{};
        std::size_t ScopedDigitCount = 0;
        if (ScopedValue.Coefficient.FormatPlain(std::span<char>(ScopedDigits), &ScopedDigitCount) != Status::Ok)
        {
            return Status::InvalidArgument;
        }
        std::uint32_t ScopedSignificant = InSpec.SignificantDigits == 0 ? 6u : InSpec.SignificantDigits;
        if (ScopedSignificant > ScopedDigitCount)
        {
            ScopedSignificant = static_cast<std::uint32_t>(ScopedDigitCount);
        }
        std::size_t ScopedCursor = 0;
        const auto ScopedPut = [&ScopedCursor, OutBuffer](char InCharacter) noexcept
        {
            if (ScopedCursor >= OutBuffer.size())
            {
                return false;
            }
            OutBuffer[ScopedCursor++] = InCharacter;
            return true;
        };
        if ((ScopedValue.Negative && !ScopedPut('-')) || !ScopedPut(ScopedDigits[0]))
        {
            return Status::BufferTooSmall;
        }
        if (ScopedSignificant > 1)
        {
            if (!ScopedPut('.'))
            {
                return Status::BufferTooSmall;
            }
            for (std::uint32_t ScopedIndex = 1; ScopedIndex < ScopedSignificant; ++ScopedIndex)
            {
                if (!ScopedPut(ScopedDigits[ScopedIndex]))
                {
                    return Status::BufferTooSmall;
                }
            }
        }
        if (!ScopedPut(InSpec.ExponentMarker == '\0' ? 'E' : InSpec.ExponentMarker))
        {
            return Status::BufferTooSmall;
        }
        const std::int64_t ScopedExponent =
            static_cast<std::int64_t>(ScopedValue.Exponent) + static_cast<std::int64_t>(ScopedDigitCount) - 1;
        std::array<char, 24> ScopedExponentBuffer{};
        const auto ScopedConversion = std::to_chars(ScopedExponentBuffer.data(),
                                                    ScopedExponentBuffer.data() + ScopedExponentBuffer.size(),
                                                    ScopedExponent);
        if (ScopedConversion.ec != std::errc{})
        {
            return Status::InvalidArgument;
        }
        for (const char* ScopedPointer = ScopedExponentBuffer.data(); ScopedPointer < ScopedConversion.ptr;
             ++ScopedPointer)
        {
            if (!ScopedPut(*ScopedPointer))
            {
                return Status::BufferTooSmall;
            }
        }
        if (OutSize != nullptr)
        {
            *OutSize = ScopedCursor;
        }
        return Status::Ok;
    }

private:
    [[nodiscard]] static std::uint32_t DigitCount(const BigIntT<Expansion>& InValue) noexcept
    {
        if (InValue.IsZero())
        {
            return 1;
        }
        BigIntT<Expansion> ScopedValue = InValue;
        std::uint32_t ScopedCount = 0;
        while (!ScopedValue.IsZero())
        {
            BigIntT<Expansion> ScopedQuotient;
            BigIntT<Expansion> ScopedRemainder;
            if (ScopedValue.DivMod(BigIntT<Expansion>::FromInt64(10), &ScopedQuotient, &ScopedRemainder) !=
                Status::Ok)
            {
                break;
            }
            ScopedValue = ScopedQuotient;
            ++ScopedCount;
        }
        return ScopedCount == 0 ? 1u : ScopedCount;
    }

    [[nodiscard]] static bool ScaleByPowerOfTen(BigIntT<Expansion>& InValue, std::uint32_t InDigits) noexcept
    {
        BigIntT<Expansion> ScopedValue = InValue;
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < InDigits; ++ScopedIndex)
        {
            BigIntT<Expansion> ScopedProduct;
            if (ScopedValue.Mul(BigIntT<Expansion>::FromInt64(10), &ScopedProduct) != Status::Ok)
            {
                return false;
            }
            ScopedValue = ScopedProduct;
        }
        InValue = ScopedValue;
        return true;
    }

    [[nodiscard]] static int CompareMagnitude(const DecimalT& InLeft, const DecimalT& InRight) noexcept
    {
        const std::string ScopedLeftDigits = InLeft.Coefficient.ToString();
        const std::string ScopedRightDigits = InRight.Coefficient.ToString();
        const std::int64_t ScopedLeftOrder =
            static_cast<std::int64_t>(InLeft.Exponent) + static_cast<std::int64_t>(ScopedLeftDigits.size());
        const std::int64_t ScopedRightOrder =
            static_cast<std::int64_t>(InRight.Exponent) + static_cast<std::int64_t>(ScopedRightDigits.size());
        if (ScopedLeftOrder != ScopedRightOrder)
        {
            return ScopedLeftOrder < ScopedRightOrder ? -1 : 1;
        }
        const std::size_t ScopedCount =
            ScopedLeftDigits.size() > ScopedRightDigits.size() ? ScopedLeftDigits.size() : ScopedRightDigits.size();
        for (std::size_t ScopedIndex = 0; ScopedIndex < ScopedCount; ++ScopedIndex)
        {
            const char ScopedLeft =
                ScopedIndex < ScopedLeftDigits.size() ? ScopedLeftDigits[ScopedIndex] : '0';
            const char ScopedRight =
                ScopedIndex < ScopedRightDigits.size() ? ScopedRightDigits[ScopedIndex] : '0';
            if (ScopedLeft != ScopedRight)
            {
                return ScopedLeft < ScopedRight ? -1 : 1;
            }
        }
        return 0;
    }
};

template <std::uint32_t Expansion>
[[nodiscard]] inline DecimalT<Expansion> operator+(const DecimalT<Expansion>& InLeft,
                                                   const DecimalT<Expansion>& InRight) noexcept
{
    DecimalT<Expansion> ScopedResult;
    (void)InLeft.Add(InRight, &ScopedResult);
    return ScopedResult;
}

template <std::uint32_t Expansion>
[[nodiscard]] inline DecimalT<Expansion> operator-(const DecimalT<Expansion>& InLeft,
                                                   const DecimalT<Expansion>& InRight) noexcept
{
    DecimalT<Expansion> ScopedResult;
    (void)InLeft.Sub(InRight, &ScopedResult);
    return ScopedResult;
}

template <std::uint32_t Expansion>
[[nodiscard]] inline DecimalT<Expansion> operator*(const DecimalT<Expansion>& InLeft,
                                                   const DecimalT<Expansion>& InRight) noexcept
{
    DecimalT<Expansion> ScopedResult;
    (void)InLeft.Mul(InRight, &ScopedResult);
    return ScopedResult;
}

template <std::uint32_t Expansion> [[nodiscard]] inline DecimalT<Expansion> operator-(DecimalT<Expansion> InValue) noexcept
{
    if (!InValue.IsZero())
    {
        InValue.Negative = !InValue.Negative;
    }
    return InValue;
}

template <std::uint32_t Expansion, typename T>
    requires(std::is_integral_v<T> && !std::is_same_v<std::remove_cv_t<T>, bool>)
[[nodiscard]] inline DecimalT<Expansion> operator+(const DecimalT<Expansion>& InLeft, T InRight) noexcept
{
    return InLeft + DecimalT<Expansion>::FromIntegral(InRight);
}

template <std::uint32_t Expansion, typename T>
    requires(std::is_integral_v<T> && !std::is_same_v<std::remove_cv_t<T>, bool>)
[[nodiscard]] inline DecimalT<Expansion> operator+(T InLeft, const DecimalT<Expansion>& InRight) noexcept
{
    return DecimalT<Expansion>::FromIntegral(InLeft) + InRight;
}

template <std::uint32_t Expansion, typename T>
    requires(std::is_integral_v<T> && !std::is_same_v<std::remove_cv_t<T>, bool>)
[[nodiscard]] inline DecimalT<Expansion> operator-(const DecimalT<Expansion>& InLeft, T InRight) noexcept
{
    return InLeft - DecimalT<Expansion>::FromIntegral(InRight);
}

template <std::uint32_t Expansion, typename T>
    requires(std::is_integral_v<T> && !std::is_same_v<std::remove_cv_t<T>, bool>)
[[nodiscard]] inline DecimalT<Expansion> operator-(T InLeft, const DecimalT<Expansion>& InRight) noexcept
{
    return DecimalT<Expansion>::FromIntegral(InLeft) - InRight;
}

template <std::uint32_t Expansion, typename T>
    requires(std::is_integral_v<T> && !std::is_same_v<std::remove_cv_t<T>, bool>)
[[nodiscard]] inline DecimalT<Expansion> operator*(const DecimalT<Expansion>& InLeft, T InRight) noexcept
{
    return InLeft * DecimalT<Expansion>::FromIntegral(InRight);
}

template <std::uint32_t Expansion, typename T>
    requires(std::is_integral_v<T> && !std::is_same_v<std::remove_cv_t<T>, bool>)
[[nodiscard]] inline DecimalT<Expansion> operator*(T InLeft, const DecimalT<Expansion>& InRight) noexcept
{
    return DecimalT<Expansion>::FromIntegral(InLeft) * InRight;
}

template <std::uint32_t Expansion>
[[nodiscard]] inline DecimalT<Expansion> operator+(const DecimalT<Expansion>& InLeft,
                                                   const BigIntT<Expansion>& InRight) noexcept
{
    return InLeft + DecimalT<Expansion>::FromBigInt(InRight);
}

template <std::uint32_t Expansion>
[[nodiscard]] inline DecimalT<Expansion> operator+(const BigIntT<Expansion>& InLeft,
                                                   const DecimalT<Expansion>& InRight) noexcept
{
    return DecimalT<Expansion>::FromBigInt(InLeft) + InRight;
}

template <std::uint32_t Expansion>
[[nodiscard]] inline DecimalT<Expansion> operator-(const DecimalT<Expansion>& InLeft,
                                                   const BigIntT<Expansion>& InRight) noexcept
{
    return InLeft - DecimalT<Expansion>::FromBigInt(InRight);
}

template <std::uint32_t Expansion>
[[nodiscard]] inline DecimalT<Expansion> operator*(const DecimalT<Expansion>& InLeft,
                                                   const BigIntT<Expansion>& InRight) noexcept
{
    return InLeft * DecimalT<Expansion>::FromBigInt(InRight);
}

template <std::uint32_t Expansion>
[[nodiscard]] inline bool operator==(const DecimalT<Expansion>& InLeft, const DecimalT<Expansion>& InRight) noexcept
{
    return InLeft.Compare(InRight) == 0;
}

template <std::uint32_t Expansion>
[[nodiscard]] inline bool operator!=(const DecimalT<Expansion>& InLeft, const DecimalT<Expansion>& InRight) noexcept
{
    return InLeft.Compare(InRight) != 0;
}

template <std::uint32_t Expansion>
[[nodiscard]] inline bool operator<(const DecimalT<Expansion>& InLeft, const DecimalT<Expansion>& InRight) noexcept
{
    return InLeft.Compare(InRight) < 0;
}

template <std::uint32_t Expansion>
[[nodiscard]] inline bool operator>(const DecimalT<Expansion>& InLeft, const DecimalT<Expansion>& InRight) noexcept
{
    return InLeft.Compare(InRight) > 0;
}

using Decimal128 = DecimalT<1>;
using Decimal256 = DecimalT<2>;

static_assert(sizeof(Decimal128) == 24, "Decimal128 must stay at 24 bytes");
static_assert(sizeof(Decimal256) == 40, "Decimal256 must stay at 40 bytes");
static_assert(std::is_trivially_copyable_v<Decimal128>);
static_assert(std::is_standard_layout_v<Decimal128>);
}
