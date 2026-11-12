// FemboyEngine2D - .fempak on-disk format, shared by the runtime.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace Fby::Assets
{

inline constexpr char kPakMagic[8] = {'F', 'B', 'P', 'A', 'K', '0', '0', '1'};
inline constexpr std::uint32_t kPakVersion = 1;
inline constexpr std::uint32_t kPakAlignment = 16;
inline constexpr std::string_view kPakExtension = ".fempak";

struct PakHeader
{
    char magic[8];
    std::uint32_t version;
    std::uint32_t entryCount;
    std::uint64_t entryTableOffset;
    std::uint64_t stringTableOffset;
    std::uint64_t dataOffset;
};

struct PakEntry
{
    std::uint32_t pathOffset;
    std::uint32_t pathSize;
    std::uint64_t offset;
    std::uint64_t size;
};

static_assert(sizeof(PakHeader) == 40, ".fempak header is fixed at 40 bytes");
static_assert(sizeof(PakEntry) == 24, ".fempak entry is fixed at 24 bytes");

// The canonical path used by the cooker, the VFS and every tool.
[[nodiscard]] inline std::string NormalizePakPath(std::string_view InPath)
{
    std::string ScopedNormalized;
    ScopedNormalized.reserve(InPath.size());
    for (const char ScopedCharacter : InPath)
    {
        if (ScopedCharacter == '\\')
        {
            ScopedNormalized.push_back('/');
        }
        else if (ScopedCharacter != '\0')
        {
            ScopedNormalized.push_back(ScopedCharacter);
        }
    }
    if (ScopedNormalized.size() >= 2 &&
        ((ScopedNormalized[0] >= 'A' && ScopedNormalized[0] <= 'Z') ||
         (ScopedNormalized[0] >= 'a' && ScopedNormalized[0] <= 'z')) &&
        ScopedNormalized[1] == ':')
    {
        return {};
    }
    if (ScopedNormalized.starts_with("//"))
    {
        return {};
    }

    std::string ScopedResult;
    ScopedResult.reserve(ScopedNormalized.size());
    std::size_t ScopedRead = 0;
    while (ScopedRead < ScopedNormalized.size() && ScopedNormalized[ScopedRead] == '/')
    {
        ++ScopedRead;
    }
    while (ScopedRead < ScopedNormalized.size())
    {
        const std::size_t ScopedEnd = ScopedNormalized.find('/', ScopedRead);
        const std::size_t ScopedStop = ScopedEnd == std::string::npos ? ScopedNormalized.size() : ScopedEnd;
        const std::string_view ScopedComponent(ScopedNormalized.data() + ScopedRead, ScopedStop - ScopedRead);
        if (ScopedComponent == "..")
        {
            if (ScopedResult.empty())
            {
                return {};
            }
            const std::size_t ScopedSlash = ScopedResult.rfind('/');
            if (ScopedSlash == std::string::npos)
            {
                ScopedResult.clear();
            }
            else
            {
                ScopedResult.resize(ScopedSlash);
            }
        }
        else if (!ScopedComponent.empty() && ScopedComponent != ".")
        {
            if (!ScopedResult.empty())
            {
                ScopedResult.push_back('/');
            }
            ScopedResult.append(ScopedComponent);
        }
        if (ScopedEnd == std::string::npos)
        {
            break;
        }
        ScopedRead = ScopedEnd + 1;
    }
    return ScopedResult;
}

[[nodiscard]] inline std::uint64_t AlignUp(std::uint64_t InValue, std::uint64_t InAlignment)
{
    return (InValue + InAlignment - 1) & ~(InAlignment - 1);
}

[[nodiscard]] inline bool IsRangeInside(std::uint64_t InOffset, std::uint64_t InSize, std::uint64_t InTotal)
{
    return InOffset <= InTotal && InSize <= InTotal - InOffset;
}
}
