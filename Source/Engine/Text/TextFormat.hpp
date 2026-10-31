// FemboyEngine2D - placeholder formatting for text blocks.
#pragma once

#include "Engine/Core/BigInt.hpp"
#include "Engine/Core/Decimal.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace Fby::Text
{

// Packed representation for ECS-backed format arguments: two style bits, eight
// significant-digit bits and an eight-bit exponent marker.
[[nodiscard]] constexpr std::uint32_t PackFormatSpec(const BigNum::FormatSpec& InSpec) noexcept
{
    const std::uint32_t ScopedStyle = static_cast<std::uint32_t>(InSpec.Style) & 0x3u;
    return ScopedStyle | (static_cast<std::uint32_t>(InSpec.SignificantDigits) << 2) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(InSpec.ExponentMarker)) << 10);
}

[[nodiscard]] constexpr BigNum::FormatSpec UnpackFormatSpec(std::uint32_t InPacked) noexcept
{
    BigNum::FormatSpec ScopedSpec;
    ScopedSpec.Style = static_cast<BigNum::Notation>(InPacked & 0x3u);
    ScopedSpec.SignificantDigits = static_cast<std::uint8_t>((InPacked >> 2) & 0xFFu);
    ScopedSpec.ExponentMarker = static_cast<char>((InPacked >> 10) & 0xFFu);
    return ScopedSpec;
}

enum class TextFormatStatus : std::uint8_t
{
    Ok = 0,
    InvalidTemplate,
    MissingArgument,
    ArgumentError,
    BufferTooSmall,
};

using TextArgFormatFn = BigNum::Status (*)(const void* InValue, const BigNum::FormatSpec& InSpec,
                                           std::span<char> OutBuffer, std::size_t* OutSize);

struct TextFormatArg
{
    const void* Value = nullptr;
    TextArgFormatFn Format = nullptr;
    BigNum::FormatSpec Spec{};
};

template <std::uint32_t Expansion>
[[nodiscard]] inline BigNum::Status FormatBigIntArg(const void* InValue, const BigNum::FormatSpec& InSpec,
                                                    std::span<char> OutBuffer, std::size_t* OutSize) noexcept
{
    if (InValue == nullptr)
    {
        return BigNum::Status::InvalidArgument;
    }
    return static_cast<const BigNum::BigIntT<Expansion>*>(InValue)->Format(InSpec, OutBuffer, OutSize);
}

template <std::uint32_t Expansion>
[[nodiscard]] inline BigNum::Status FormatDecimalArg(const void* InValue, const BigNum::FormatSpec& InSpec,
                                                     std::span<char> OutBuffer, std::size_t* OutSize) noexcept
{
    if (InValue == nullptr)
    {
        return BigNum::Status::InvalidArgument;
    }
    return static_cast<const BigNum::DecimalT<Expansion>*>(InValue)->Format(InSpec, OutBuffer, OutSize);
}

template <std::uint32_t Expansion>
[[nodiscard]] inline TextFormatArg MakeBigIntArg(const BigNum::BigIntT<Expansion>* InValue,
                                                 BigNum::FormatSpec InSpec = {}) noexcept
{
    TextFormatArg ScopedArg;
    ScopedArg.Value = InValue;
    ScopedArg.Format = &FormatBigIntArg<Expansion>;
    ScopedArg.Spec = InSpec;
    return ScopedArg;
}

template <std::uint32_t Expansion>
[[nodiscard]] inline TextFormatArg MakeDecimalArg(const BigNum::DecimalT<Expansion>* InValue,
                                                  BigNum::FormatSpec InSpec = {}) noexcept
{
    TextFormatArg ScopedArg;
    ScopedArg.Value = InValue;
    ScopedArg.Format = &FormatDecimalArg<Expansion>;
    ScopedArg.Spec = InSpec;
    return ScopedArg;
}

// Formats `{0}`, `{1}`, ... into the caller's buffer. `{{` and `}}` are literal
// braces. The argument table is borrowed, so no allocation and no copy occur.
[[nodiscard]] inline TextFormatStatus FormatTextTemplate(std::string_view InTemplate,
                                                         std::span<const TextFormatArg> InArgs,
                                                         std::span<char> OutBuffer,
                                                         std::size_t* OutSize = nullptr) noexcept
{
    std::size_t ScopedCursor = 0;
    std::size_t ScopedWritten = 0;
    const auto ScopedPut = [&ScopedWritten, OutBuffer](char InCharacter) noexcept
    {
        if (ScopedWritten >= OutBuffer.size())
        {
            return false;
        }
        OutBuffer[ScopedWritten++] = InCharacter;
        return true;
    };
    while (ScopedCursor < InTemplate.size())
    {
        const char ScopedCharacter = InTemplate[ScopedCursor];
        if (ScopedCharacter == '{')
        {
            if (ScopedCursor + 1 < InTemplate.size() && InTemplate[ScopedCursor + 1] == '{')
            {
                if (!ScopedPut('{'))
                {
                    return TextFormatStatus::BufferTooSmall;
                }
                ScopedCursor += 2;
                continue;
            }
            const std::size_t ScopedClose = InTemplate.find('}', ScopedCursor + 1);
            if (ScopedClose == std::string_view::npos || ScopedClose == ScopedCursor + 1)
            {
                return TextFormatStatus::InvalidTemplate;
            }
            std::uint32_t ScopedIndex = 0;
            for (std::size_t ScopedDigit = ScopedCursor + 1; ScopedDigit < ScopedClose; ++ScopedDigit)
            {
                const char ScopedValue = InTemplate[ScopedDigit];
                if (ScopedValue < '0' || ScopedValue > '9')
                {
                    return TextFormatStatus::InvalidTemplate;
                }
                ScopedIndex = ScopedIndex * 10 + static_cast<std::uint32_t>(ScopedValue - '0');
            }
            if (ScopedIndex >= InArgs.size() || InArgs[ScopedIndex].Format == nullptr)
            {
                return TextFormatStatus::MissingArgument;
            }
            std::size_t ScopedArgumentSize = 0;
            const BigNum::Status ScopedStatus =
                InArgs[ScopedIndex].Format(InArgs[ScopedIndex].Value, InArgs[ScopedIndex].Spec,
                                           OutBuffer.subspan(ScopedWritten), &ScopedArgumentSize);
            if (ScopedStatus != BigNum::Status::Ok)
            {
                return ScopedStatus == BigNum::Status::BufferTooSmall ? TextFormatStatus::BufferTooSmall
                                                                      : TextFormatStatus::ArgumentError;
            }
            ScopedWritten += ScopedArgumentSize;
            ScopedCursor = ScopedClose + 1;
            continue;
        }
        if (ScopedCharacter == '}')
        {
            if (ScopedCursor + 1 < InTemplate.size() && InTemplate[ScopedCursor + 1] == '}')
            {
                if (!ScopedPut('}'))
                {
                    return TextFormatStatus::BufferTooSmall;
                }
                ScopedCursor += 2;
                continue;
            }
            return TextFormatStatus::InvalidTemplate;
        }
        if (!ScopedPut(ScopedCharacter))
        {
            return TextFormatStatus::BufferTooSmall;
        }
        ++ScopedCursor;
    }
    if (OutSize != nullptr)
    {
        *OutSize = ScopedWritten;
    }
    return TextFormatStatus::Ok;
}
}
