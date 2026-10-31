// FemboyEngine2D - .fanim compressed animation clip asset.
#pragma once

#include "Engine/ECS/AnimationCompression.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace Fby::Animation::Asset
{

// "FANM0001" - self-describing magic with a version suffix.
inline constexpr std::array<char, 8> kFanmMagic{'F', 'A', 'N', 'M', '0', '0', '0', '1'};
inline constexpr std::uint32_t kFanmVersion = 1;
inline constexpr std::uint32_t kFanmHeaderSize = 120;
inline constexpr std::uint32_t kFanmMaxBytes = 256u * 1024u;

struct FanmHeader
{
    char magic[8];
    std::uint32_t version;
    std::uint32_t headerSize;
    std::uint32_t trackCount;
    std::uint32_t keyCount;
    float duration;
    float fps;
    std::uint32_t flags;
    std::uint32_t reserved;
    // FNV-1a of the track and key payload
    std::uint64_t contentHash;
    std::uint64_t sourceHash;
    // Authoring name, NUL padded
    char name[64];
};

static_assert(sizeof(FanmHeader) == kFanmHeaderSize);

// Serializes a compressed clip. The payload is the POD track/key arrays, so a
// loader validates the tables instead of trusting the counts.
[[nodiscard]] bool SaveClip(const CompressedClip& InClip, std::vector<std::byte>* OutBytes,
                            std::string* InError = nullptr);

// Parses and validates a `.fanim` payload. Rejects a magic/version mismatch, a
// table that overflows the payload, an out-of-range track range and a content
// hash mismatch.
[[nodiscard]] bool LoadClip(std::span<const std::byte> InBytes, CompressedClip* OutClip,
                            std::string* InError = nullptr);
}
