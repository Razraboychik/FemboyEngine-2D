// FemboyEngine2D - BLAKE2b hash, HMAC and HKDF for the session layer.
#pragma once

#include "Engine/Core/Sha256.hpp"

#include <cstddef>
#include <span>

namespace Fby::Crypto
{

inline constexpr std::size_t kBlake2bBytes = 64;
inline constexpr std::size_t kBlake2bBlockBytes = 128;
// Maximum HMAC key size accepted by the keyed BLAKE2b wrapper.
inline constexpr std::size_t kBlake2bMaxKeyBytes = 64;
// HKDF expand is defined over at most 255 blocks of the 64-byte digest.
inline constexpr std::size_t kHkdfMaxOutputBytes = 255 * kBlake2bBytes;
// The SHA-256 variants use 32-byte blocks, so the same 255-block limit applies.
inline constexpr std::size_t kHkdfSha256MaxOutputBytes = 255 * kSha256Bytes;

// One-shot BLAKE2b. `OutDigest` accepts 1..64 bytes; an empty or oversized span
// is rejected without touching memory.
void Blake2b(std::span<const std::byte> InData, std::span<std::byte> OutDigest) noexcept;

// Keyed BLAKE2b (1..64 key bytes). A rejected key zeroes the digest so a caller
// that ignores the error can never encrypt with a half-initialised key.
void Blake2bKeyed(std::span<const std::byte> InKey, std::span<const std::byte> InData,
                  std::span<std::byte> OutDigest) noexcept;

// Standard HMAC over unkeyed BLAKE2b, so the construction composes with HKDF
// below and matches any HMAC-BLAKE2b reference implementation. `OutMac` is
// exactly 64 bytes.
void HmacBlake2b(std::span<const std::byte> InKey, std::span<const std::byte> InData,
                 std::span<std::byte> OutMac) noexcept;

// RFC 5869 extract-and-expand with HMAC-BLAKE2b. An empty salt or info is
// legal. `OutKey` accepts 1..kHkdfMaxOutputBytes bytes; a rejected size zeroes
// the output.
void HkdfBlake2b(std::span<const std::byte> InSecret, std::span<const std::byte> InSalt,
                 std::span<const std::byte> InInfo, std::span<std::byte> OutKey) noexcept;

// HMAC-SHA-256 (RFC 2104 over FIPS 180-4). `OutMac` is exactly 32 bytes.
void HmacSha256(std::span<const std::byte> InKey, std::span<const std::byte> InData,
                std::span<std::byte> OutMac) noexcept;

// RFC 5869 extract-and-expand with HMAC-SHA-256. `OutKey` accepts
// 1..255 * 32 bytes; a rejected size zeroes the output.
void HkdfSha256(std::span<const std::byte> InSecret, std::span<const std::byte> InSalt,
                std::span<const std::byte> InInfo, std::span<std::byte> OutKey) noexcept;

// Wipes a byte range in place. Wraps the platform-safe wipe of the crypto
// backend, so compiler dead-store elimination cannot keep a key alive.
void WipeBytes(std::span<std::byte> InBytes) noexcept;
}
