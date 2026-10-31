// FemboyEngine2D - .fempak on-disk format, shared by the runtime and the
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

// The canonical path used by the cooker, the VFS and every tool. Public so all
[[nodiscard]] inline std::string NormalizePakPath(std::string_view InPath)
{
    std::string ScopedResult;
    ScopedResult.reserve(InPath.size());
    for (const char ScopedCharacter : InPath)
    {
        if (ScopedCharacter == '\\')
        {
            ScopedResult.push_back('/');
        }
        else if (ScopedCharacter != '\0')
        {
            ScopedResult.push_back(ScopedCharacter);
        }
    }
    // Strip a leading "./" and any leading slashes; a path inside a pack is
    while (ScopedResult.size() >= 2 && ScopedResult[0] == '.' && ScopedResult[1] == '/')
    {
        ScopedResult.erase(0, 2);
    }
    while (!ScopedResult.empty() && ScopedResult.front() == '/')
    {
        ScopedResult.erase(ScopedResult.begin());
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
