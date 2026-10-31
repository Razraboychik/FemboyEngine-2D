// FemboyEngine2D - canonical SHA-256 (FIPS 180-4).
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace Fby::Crypto
{

inline constexpr std::size_t kSha256Bytes = 32;
inline constexpr std::size_t kSha256BlockBytes = 64;

// Streaming state. Every field is plain data, so a caller can copy the context
// to fork a digest without touching the heap.
struct Sha256Context
{
    std::array<std::uint32_t, 8> State{};
    std::array<std::uint8_t, kSha256BlockBytes> Buffer{};
    std::uint64_t TotalBytes = 0;
    std::uint32_t BufferedBytes = 0;
};

void Sha256Init(Sha256Context* OutContext) noexcept;

// Absorbs one contiguous range. Repeated calls are equivalent to a single call
// over the concatenation.
void Sha256Update(Sha256Context* InContext, std::span<const std::byte> InData) noexcept;

// Writes exactly kSha256Bytes bytes into `OutDigest`. A span of any other size
// is zeroed instead.
void Sha256Final(Sha256Context* InContext, std::span<std::byte> OutDigest) noexcept;

// One-shot digest over one contiguous range.
void Sha256(std::span<const std::byte> InData, std::span<std::byte> OutDigest) noexcept;
}
