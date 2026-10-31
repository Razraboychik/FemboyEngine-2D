// FemboyEngine2D - private scene-IO helpers.
#pragma once

#include "Engine/Core/BigInt.hpp"
#include "Engine/Core/Decimal.hpp"
#include "Engine/Core/Mirror.hpp"
#include "Engine/ECS/ComponentRegistry.hpp"

#include <bit>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>

namespace Fby::Scene::Detail
{

[[nodiscard]] inline const std::byte* FieldAddress(const void* InComponent, const Reflect::FieldDesc& InField)
{
    return static_cast<const std::byte*>(InComponent) + InField.offset;
}

[[nodiscard]] inline std::byte* FieldAddress(void* InComponent, const Reflect::FieldDesc& InField)
{
    return static_cast<std::byte*>(InComponent) + InField.offset;
}

[[nodiscard]] inline std::uint64_t ReadUnsigned(const void* InComponent, const Reflect::FieldDesc& InField)
{
    const std::byte* ScopedAddress = FieldAddress(InComponent, InField);
    switch (InField.size)
    {
        case 1:
        {
            std::uint8_t ScopedValue = 0;
            std::memcpy(&ScopedValue, ScopedAddress, sizeof(ScopedValue));
            return ScopedValue;
        }
        case 2:
        {
            std::uint16_t ScopedValue = 0;
            std::memcpy(&ScopedValue, ScopedAddress, sizeof(ScopedValue));
            return ScopedValue;
        }
        case 4:
        {
            std::uint32_t ScopedValue = 0;
            std::memcpy(&ScopedValue, ScopedAddress, sizeof(ScopedValue));
            return ScopedValue;
        }
        case 8:
        {
            std::uint64_t ScopedValue = 0;
            std::memcpy(&ScopedValue, ScopedAddress, sizeof(ScopedValue));
            return ScopedValue;
        }
        default:
            return 0;
    }
}

[[nodiscard]] inline std::int64_t ReadSigned(const void* InComponent, const Reflect::FieldDesc& InField)
{
    const std::byte* ScopedAddress = FieldAddress(InComponent, InField);
    switch (InField.size)
    {
        case 1:
        {
            std::int8_t ScopedValue = 0;
            std::memcpy(&ScopedValue, ScopedAddress, sizeof(ScopedValue));
            return ScopedValue;
        }
        case 2:
        {
            std::int16_t ScopedValue = 0;
            std::memcpy(&ScopedValue, ScopedAddress, sizeof(ScopedValue));
            return ScopedValue;
        }
        case 4:
        {
            std::int32_t ScopedValue = 0;
            std::memcpy(&ScopedValue, ScopedAddress, sizeof(ScopedValue));
            return ScopedValue;
        }
        case 8:
        {
            std::int64_t ScopedValue = 0;
            std::memcpy(&ScopedValue, ScopedAddress, sizeof(ScopedValue));
            return ScopedValue;
        }
        default:
            return 0;
    }
}

[[nodiscard]] inline std::uint64_t ReadU64(const void* InComponent, const Reflect::FieldDesc& InField)
{
    std::uint64_t ScopedValue = 0;
    if (InField.size == sizeof(ScopedValue))
    {
        std::memcpy(&ScopedValue, FieldAddress(InComponent, InField), sizeof(ScopedValue));
    }
    return ScopedValue;
}

[[nodiscard]] inline bool WriteU64(void* InComponent, const Reflect::FieldDesc& InField, std::uint64_t InValue)
{
    if (InField.size != sizeof(InValue))
    {
        return false;
    }
    std::memcpy(FieldAddress(InComponent, InField), &InValue, sizeof(InValue));
    return true;
}

[[nodiscard]] inline bool WriteUnsigned(void* InComponent, const Reflect::FieldDesc& InField, std::uint64_t InValue)
{
    const std::uint64_t ScopedMaximum =
        InField.size >= 8 ? std::numeric_limits<std::uint64_t>::max() : (std::uint64_t{1} << (InField.size * 8)) - 1;
    if (InValue > ScopedMaximum)
    {
        return false;
    }
    std::byte* ScopedAddress = FieldAddress(InComponent, InField);
    switch (InField.size)
    {
        case 1:
        {
            const auto ScopedNarrowed = static_cast<std::uint8_t>(InValue);
            std::memcpy(ScopedAddress, &ScopedNarrowed, sizeof(ScopedNarrowed));
            return true;
        }
        case 2:
        {
            const auto ScopedNarrowed = static_cast<std::uint16_t>(InValue);
            std::memcpy(ScopedAddress, &ScopedNarrowed, sizeof(ScopedNarrowed));
            return true;
        }
        case 4:
        {
            const auto ScopedNarrowed = static_cast<std::uint32_t>(InValue);
            std::memcpy(ScopedAddress, &ScopedNarrowed, sizeof(ScopedNarrowed));
            return true;
        }
        case 8:
            std::memcpy(ScopedAddress, &InValue, sizeof(InValue));
            return true;
        default:
            return false;
    }
}

[[nodiscard]] inline bool WriteSigned(void* InComponent, const Reflect::FieldDesc& InField, std::int64_t InValue)
{
    const int ScopedBits = static_cast<int>(InField.size) * 8;
    if (ScopedBits < 64)
    {
        const std::int64_t ScopedMinimum = -(std::int64_t{1} << (ScopedBits - 1));
        const std::int64_t ScopedMaximum = (std::int64_t{1} << (ScopedBits - 1)) - 1;
        if (InValue < ScopedMinimum || InValue > ScopedMaximum)
        {
            return false;
        }
    }
    std::byte* ScopedAddress = FieldAddress(InComponent, InField);
    switch (InField.size)
    {
        case 1:
        {
            const auto ScopedNarrowed = static_cast<std::int8_t>(InValue);
            std::memcpy(ScopedAddress, &ScopedNarrowed, sizeof(ScopedNarrowed));
            return true;
        }
        case 2:
        {
            const auto ScopedNarrowed = static_cast<std::int16_t>(InValue);
            std::memcpy(ScopedAddress, &ScopedNarrowed, sizeof(ScopedNarrowed));
            return true;
        }
        case 4:
        {
            const auto ScopedNarrowed = static_cast<std::int32_t>(InValue);
            std::memcpy(ScopedAddress, &ScopedNarrowed, sizeof(ScopedNarrowed));
            return true;
        }
        case 8:
            std::memcpy(ScopedAddress, &InValue, sizeof(InValue));
            return true;
        default:
            return false;
    }
}

[[nodiscard]] inline float ReadFloat(const void* InComponent, const Reflect::FieldDesc& InField)
{
    float ScopedValue = 0.0f;
    std::memcpy(&ScopedValue, FieldAddress(InComponent, InField), sizeof(ScopedValue));
    return ScopedValue;
}

[[nodiscard]] inline bool WriteFloat(void* InComponent, const Reflect::FieldDesc& InField, float InValue)
{
    if (InField.size != sizeof(float))
    {
        return false;
    }
    std::memcpy(FieldAddress(InComponent, InField), &InValue, sizeof(InValue));
    return true;
}

[[nodiscard]] inline Vector2D ReadVec2(const void* InComponent, const Reflect::FieldDesc& InField)
{
    Vector2D ScopedValue{};
    std::memcpy(&ScopedValue, FieldAddress(InComponent, InField), sizeof(ScopedValue));
    return ScopedValue;
}

[[nodiscard]] inline bool WriteVec2(void* InComponent, const Reflect::FieldDesc& InField, Vector2D InValue)
{
    if (InField.size != sizeof(Vector2D))
    {
        return false;
    }
    std::memcpy(FieldAddress(InComponent, InField), &InValue, sizeof(InValue));
    return true;
}

[[nodiscard]] inline Color32 ReadColor(const void* InComponent, const Reflect::FieldDesc& InField)
{
    std::uint32_t ScopedValue = 0;
    std::memcpy(&ScopedValue, FieldAddress(InComponent, InField), sizeof(ScopedValue));
    return Color32{ScopedValue};
}

[[nodiscard]] inline bool WriteColor(void* InComponent, const Reflect::FieldDesc& InField, Color32 InValue)
{
    if (InField.size != sizeof(std::uint32_t))
    {
        return false;
    }
    std::memcpy(FieldAddress(InComponent, InField), &InValue.rgba, sizeof(InValue.rgba));
    return true;
}

// Fixed-capacity strings: the field is char[N] or std::array<char, N>; the
[[nodiscard]] inline std::string_view ReadString(const void* InComponent, const Reflect::FieldDesc& InField)
{
    const char* ScopedText = reinterpret_cast<const char*>(FieldAddress(InComponent, InField));
    std::size_t ScopedLength = 0;
    while (ScopedLength < InField.size && ScopedText[ScopedLength] != '\0')
    {
        ++ScopedLength;
    }
    return std::string_view(ScopedText, ScopedLength);
}

[[nodiscard]] inline bool WriteString(void* InComponent, const Reflect::FieldDesc& InField, std::string_view InValue)
{
    if (InField.size == 0 || InValue.size() >= InField.size || InValue.find('\0') != std::string_view::npos)
    {
        return false;
    }
    char* ScopedText = reinterpret_cast<char*>(FieldAddress(InComponent, InField));
    std::memset(ScopedText, 0, InField.size);
    std::memcpy(ScopedText, InValue.data(), InValue.size());
    return true;
}

[[nodiscard]] inline bool ReadInt128(const void* InComponent, const Reflect::FieldDesc& InField,
                                     BigNum::Int128* OutValue)
{
    if (OutValue == nullptr || InField.size != sizeof(BigNum::Int128))
    {
        return false;
    }
    std::memcpy(OutValue, FieldAddress(InComponent, InField), sizeof(BigNum::Int128));
    return true;
}

[[nodiscard]] inline bool WriteInt128(void* InComponent, const Reflect::FieldDesc& InField,
                                      const BigNum::Int128& InValue)
{
    if (InField.size != sizeof(BigNum::Int128))
    {
        return false;
    }
    std::memcpy(FieldAddress(InComponent, InField), &InValue, sizeof(BigNum::Int128));
    return true;
}

[[nodiscard]] inline bool ReadDecimal(const void* InComponent, const Reflect::FieldDesc& InField,
                                      BigNum::Decimal128* OutValue)
{
    if (OutValue == nullptr || InField.size != sizeof(BigNum::Decimal128))
    {
        return false;
    }
    std::memcpy(OutValue, FieldAddress(InComponent, InField), sizeof(BigNum::Decimal128));
    return true;
}

[[nodiscard]] inline bool WriteDecimal(void* InComponent, const Reflect::FieldDesc& InField,
                                       const BigNum::Decimal128& InValue)
{
    if (InField.size != sizeof(BigNum::Decimal128))
    {
        return false;
    }
    std::memcpy(FieldAddress(InComponent, InField), &InValue, sizeof(BigNum::Decimal128));
    return true;
}

[[nodiscard]] inline Entity ReadEntity(const void* InComponent, const Reflect::FieldDesc& InField)
{
    std::uint32_t ScopedValue = 0;
    std::memcpy(&ScopedValue, FieldAddress(InComponent, InField), sizeof(ScopedValue));
    return static_cast<Entity>(ScopedValue);
}

[[nodiscard]] inline std::uint32_t ReadEntityIndex(const void* InComponent, const Reflect::FieldDesc& InField)
{
    std::uint32_t ScopedValue = 0;
    std::memcpy(&ScopedValue, FieldAddress(InComponent, InField), sizeof(ScopedValue));
    return ScopedValue;
}

[[nodiscard]] inline bool WriteEntity(void* InComponent, const Reflect::FieldDesc& InField, Entity InEntity)
{
    if (InField.size != sizeof(std::uint32_t))
    {
        return false;
    }
    const auto ScopedValue = static_cast<std::uint32_t>(InEntity);
    std::memcpy(FieldAddress(InComponent, InField), &ScopedValue, sizeof(ScopedValue));
    return true;
}

[[nodiscard]] inline bool WriteEntityIndex(void* InComponent, const Reflect::FieldDesc& InField, std::uint32_t InIndex)
{
    if (InField.size != sizeof(std::uint32_t))
    {
        return false;
    }
    std::memcpy(FieldAddress(InComponent, InField), &InIndex, sizeof(InIndex));
    return true;
}

// Human-readable, round-trippable float. TOML requires a decimal point or an
[[nodiscard]] inline std::string FormatFloat(float InValue)
{
    if (std::isnan(InValue))
    {
        return "nan";
    }
    if (std::isinf(InValue))
    {
        return InValue < 0.0f ? "-inf" : "inf";
    }
    char ScopedBuffer[64]{};
    const auto ScopedResult =
        std::to_chars(ScopedBuffer, ScopedBuffer + sizeof(ScopedBuffer), InValue, std::chars_format::general, 9);
    if (ScopedResult.ec != std::errc{})
    {
        return "0.0";
    }
    std::string ScopedText(ScopedBuffer, ScopedResult.ptr);
    if (ScopedText.find_first_of(".eE") == std::string::npos)
    {
        ScopedText += ".0";
    }
    return ScopedText;
}

[[nodiscard]] inline std::string EscapeTomlString(std::string_view InText)
{
    std::string ScopedOut;
    ScopedOut.reserve(InText.size() + 8);
    for (const char ScopedCharacter : InText)
    {
        switch (ScopedCharacter)
        {
            case '"':
                ScopedOut += "\\\"";
                break;
            case '\\':
                ScopedOut += "\\\\";
                break;
            case '\n':
                ScopedOut += "\\n";
                break;
            case '\r':
                ScopedOut += "\\r";
                break;
            case '\t':
                ScopedOut += "\\t";
                break;
            case '\b':
                ScopedOut += "\\b";
                break;
            case '\f':
                ScopedOut += "\\f";
                break;
            default:
                if (static_cast<unsigned char>(ScopedCharacter) < 0x20)
                {
                    constexpr char ScopedKHex[] = "0123456789ABCDEF";
                    ScopedOut += "\\u00";
                    ScopedOut += ScopedKHex[(static_cast<unsigned char>(ScopedCharacter) >> 4) & 0xF];
                    ScopedOut += ScopedKHex[static_cast<unsigned char>(ScopedCharacter) & 0xF];
                }
                else
                {
                    ScopedOut.push_back(ScopedCharacter);
                }
                break;
        }
    }
    return ScopedOut;
}

[[nodiscard]] inline std::string FormatColor(Color32 InColor)
{
    constexpr char ScopedKHex[] = "0123456789ABCDEF";
    std::string ScopedOut = "\"#";
    const std::uint8_t ScopedChannels[4] = {InColor.R(), InColor.G(), InColor.B(), InColor.A()};
    for (const std::uint8_t ScopedChannel : ScopedChannels)
    {
        ScopedOut += ScopedKHex[(ScopedChannel >> 4) & 0xF];
        ScopedOut += ScopedKHex[ScopedChannel & 0xF];
    }
    ScopedOut += '"';
    return ScopedOut;
}

[[nodiscard]] inline bool ParseHexByte(char InHigh, char InLow, std::uint8_t& InValue)
{
    const auto ScopedDigit = [](char InCharacter) -> int
    {
        if (InCharacter >= '0' && InCharacter <= '9')
        {
            return InCharacter - '0';
        }
        if (InCharacter >= 'a' && InCharacter <= 'f')
        {
            return InCharacter - 'a' + 10;
        }
        if (InCharacter >= 'A' && InCharacter <= 'F')
        {
            return InCharacter - 'A' + 10;
        }
        return -1;
    };
    const int ScopedHi = ScopedDigit(InHigh);
    const int ScopedLo = ScopedDigit(InLow);
    if (ScopedHi < 0 || ScopedLo < 0)
    {
        return false;
    }
    InValue = static_cast<std::uint8_t>((ScopedHi << 4) | ScopedLo);
    return true;
}

// Accepts "#RRGGBBAA" (the writer's form) and "RRGGBBAA".
[[nodiscard]] inline bool ParseColorString(std::string_view InText, Color32& InColor)
{
    if (!InText.empty() && InText.front() == '#')
    {
        InText.remove_prefix(1);
    }
    if (InText.size() != 8)
    {
        return false;
    }
    std::uint8_t ScopedChannels[4]{};
    for (std::size_t ScopedI = 0; ScopedI < 4; ++ScopedI)
    {
        if (!ParseHexByte(InText[ScopedI * 2], InText[ScopedI * 2 + 1], ScopedChannels[ScopedI]))
        {
            return false;
        }
    }
    InColor = Color32::FromBytes(ScopedChannels[0], ScopedChannels[1], ScopedChannels[2], ScopedChannels[3]);
    return true;
}
}
