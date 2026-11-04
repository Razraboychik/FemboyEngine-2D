// FemboyEngine2D - scene TOML codec for the joint chain arrays.
#include "Engine/Physics/PhysicsJointsScene.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace Fby
{
namespace
{
constexpr char kHexDigits[] = "0123456789ABCDEF";

[[nodiscard]] std::string ToHex(const std::byte* InData, std::size_t InSize)
{
    std::string ScopedOut;
    ScopedOut.resize(InSize * 2);
    for (std::size_t ScopedI = 0; ScopedI < InSize; ++ScopedI)
    {
        const std::uint8_t ScopedByte = std::to_integer<std::uint8_t>(InData[ScopedI]);
        ScopedOut[ScopedI * 2] = kHexDigits[ScopedByte >> 4];
        ScopedOut[ScopedI * 2 + 1] = kHexDigits[ScopedByte & 0x0Fu];
    }
    return ScopedOut;
}

[[nodiscard]] std::uint8_t HexValue(char InChar) noexcept
{
    if (InChar >= '0' && InChar <= '9')
    {
        return static_cast<std::uint8_t>(InChar - '0');
    }
    if (InChar >= 'a' && InChar <= 'f')
    {
        return static_cast<std::uint8_t>(InChar - 'a' + 10);
    }
    if (InChar >= 'A' && InChar <= 'F')
    {
        return static_cast<std::uint8_t>(InChar - 'A' + 10);
    }
    return 0xFF;
}

[[nodiscard]] bool FromHex(std::string_view InText, std::byte* OutData, std::size_t InSize) noexcept
{
    if (InText.size() != InSize * 2)
    {
        return false;
    }
    for (std::size_t ScopedI = 0; ScopedI < InSize; ++ScopedI)
    {
        const std::uint8_t ScopedHigh = HexValue(InText[ScopedI * 2]);
        const std::uint8_t ScopedLow = HexValue(InText[ScopedI * 2 + 1]);
        if (ScopedHigh == 0xFF || ScopedLow == 0xFF)
        {
            return false;
        }
        OutData[ScopedI] = static_cast<std::byte>((ScopedHigh << 4) | ScopedLow);
    }
    return true;
}

// Copies a section field by field so the padding of the temporary stays zero and
// the emitted bytes are deterministic across compilers.
[[nodiscard]] JointChainSection CanonicalSection(const JointChainSection& InSection) noexcept
{
    JointChainSection ScopedSection{};
    ScopedSection.Kind = InSection.Kind;
    ScopedSection.Flags = InSection.Flags;
    ScopedSection.LinkCount = InSection.LinkCount;
    ScopedSection.Radius = InSection.Radius;
    ScopedSection.LinkLengthScale = InSection.LinkLengthScale;
    ScopedSection.BreakForce = InSection.BreakForce;
    ScopedSection.BreakTorque = InSection.BreakTorque;
    ScopedSection.LowerSpringForce = InSection.LowerSpringForce;
    ScopedSection.UpperSpringForce = InSection.UpperSpringForce;
    ScopedSection.DamageRate = InSection.DamageRate;
    ScopedSection.DamageThreshold = InSection.DamageThreshold;
    return ScopedSection;
}
}

std::string CaptureJointChainToml(const void* InComponent)
{
    const auto* ScopedComponent = static_cast<const JointChainComponent*>(InComponent);
    std::string ScopedOut;
    if (ScopedComponent->SectionCount > 0)
    {
        const std::uint32_t ScopedCount = std::min<std::uint32_t>(ScopedComponent->SectionCount, 8);
        std::array<JointChainSection, 8> ScopedSections{};
        for (std::uint32_t ScopedI = 0; ScopedI < ScopedCount; ++ScopedI)
        {
            ScopedSections[ScopedI] = CanonicalSection(ScopedComponent->Sections[ScopedI]);
        }
        ScopedOut += "SectionsHex = \"";
        ScopedOut +=
            ToHex(reinterpret_cast<const std::byte*>(ScopedSections.data()), ScopedCount * sizeof(JointChainSection));
        ScopedOut += "\"\n";
    }
    if (ScopedComponent->PathCount > 0)
    {
        const std::uint32_t ScopedCount = std::min<std::uint32_t>(ScopedComponent->PathCount, 16);
        std::array<Vector2D, 16> ScopedPath{};
        for (std::uint32_t ScopedI = 0; ScopedI < ScopedCount; ++ScopedI)
        {
            ScopedPath[ScopedI] = ScopedComponent->Path[ScopedI];
        }
        ScopedOut += "PathHex = \"";
        ScopedOut += ToHex(reinterpret_cast<const std::byte*>(ScopedPath.data()), ScopedCount * sizeof(Vector2D));
        ScopedOut += "\"\n";
    }
    return ScopedOut;
}

bool RestoreJointChainToml(void* InComponent, std::string_view InKey, std::string_view InValue, std::string* OutError)
{
    auto* ScopedComponent = static_cast<JointChainComponent*>(InComponent);
    if (InKey == "SectionsHex")
    {
        const std::uint32_t ScopedCount = std::min<std::uint32_t>(ScopedComponent->SectionCount, 8);
        std::array<JointChainSection, 8> ScopedSections{};
        if (!FromHex(InValue, reinterpret_cast<std::byte*>(ScopedSections.data()),
                     ScopedCount * sizeof(JointChainSection)))
        {
            *OutError = "SectionsHex does not match SectionCount";
            return false;
        }
        for (std::uint32_t ScopedI = 0; ScopedI < ScopedCount; ++ScopedI)
        {
            ScopedComponent->Sections[ScopedI] = ScopedSections[ScopedI];
        }
        return true;
    }
    if (InKey == "PathHex")
    {
        const std::uint32_t ScopedCount = std::min<std::uint32_t>(ScopedComponent->PathCount, 16);
        std::array<Vector2D, 16> ScopedPath{};
        if (!FromHex(InValue, reinterpret_cast<std::byte*>(ScopedPath.data()), ScopedCount * sizeof(Vector2D)))
        {
            *OutError = "PathHex does not match PathCount";
            return false;
        }
        for (std::uint32_t ScopedI = 0; ScopedI < ScopedCount; ++ScopedI)
        {
            ScopedComponent->Path[ScopedI] = ScopedPath[ScopedI];
        }
        return true;
    }
    *OutError = "unknown JointChainComponent field '" + std::string(InKey) + "'";
    return false;
}
}
