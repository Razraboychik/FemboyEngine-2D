// FemboyEngine2D - X25519 key exchange and EdDSA signatures for the network layer.
#pragma once

#include <array>
#include <cstddef>
#include <span>
#include <type_traits>

namespace Fby::Crypto
{

inline constexpr std::size_t kX25519KeyBytes = 32;
inline constexpr std::size_t kEddsaSeedBytes = 32;
inline constexpr std::size_t kEddsaPublicBytes = 32;
// The secret key carries the scalar and the public half back to back, which is
// the representation the signature primitive consumes.
inline constexpr std::size_t kEddsaSecretBytes = 64;
inline constexpr std::size_t kEddsaSignatureBytes = 64;

struct X25519KeyPair
{
    std::array<std::byte, kX25519KeyBytes> Secret{};
    std::array<std::byte, kX25519KeyBytes> Public{};

    [[nodiscard]] friend bool operator==(const X25519KeyPair&, const X25519KeyPair&) = default;
};

static_assert(std::is_trivially_copyable_v<X25519KeyPair>);
static_assert(std::is_standard_layout_v<X25519KeyPair>);

struct EddsaKeyPair
{
    std::array<std::byte, kEddsaSecretBytes> Secret{};
    std::array<std::byte, kEddsaPublicBytes> Public{};

    [[nodiscard]] friend bool operator==(const EddsaKeyPair&, const EddsaKeyPair&) = default;
};

static_assert(std::is_trivially_copyable_v<EddsaKeyPair>);
static_assert(std::is_standard_layout_v<EddsaKeyPair>);

// Expands a 32-byte seed into the X25519 key pair. The caller keeps ownership of
// the seed and should wipe it after use.
[[nodiscard]] bool X25519KeyPairFromSeed(std::span<const std::byte> InSeed, X25519KeyPair* OutPair) noexcept;

// Derives a shared secret. The raw curve point is hashed with BLAKE2b because a
// raw X25519 output is not uniformly distributed. Returns false for a
// low-order peer point, whose raw output is all zeros.
[[nodiscard]] bool X25519SharedSecret(std::span<const std::byte> InSecret, std::span<const std::byte> InPeerPublic,
                                      std::span<std::byte> OutShared) noexcept;

// The RFC 7748 raw output, for the standard Noise DH() operation. Noise feeds
// the 32-byte curve point directly into HKDF, so this path must not post-hash.
// Returns false for a low-order peer point (all-zero output).
[[nodiscard]] bool X25519RawSharedSecret(std::span<const std::byte> InSecret,
                                         std::span<const std::byte> InPeerPublic,
                                         std::span<std::byte> OutShared) noexcept;

// Expands a 32-byte seed into an EdDSA key pair. The seed span is not modified;
// the implementation works on a local copy that it wipes.
[[nodiscard]] bool EddsaKeyPairFromSeed(std::span<const std::byte> InSeed, EddsaKeyPair* OutPair) noexcept;

// Signs one contiguous message. `OutSignature` is exactly 64 bytes; a rejected
// size leaves the buffer untouched and returns no signature.
void EddsaSign(std::span<const std::byte> InSecret, std::span<const std::byte> InMessage,
               std::span<std::byte> OutSignature) noexcept;

// Verifies one signature. Returns false for malformed inputs and for any
// tampered message, key or signature.
[[nodiscard]] bool EddsaVerify(std::span<const std::byte> InPublic, std::span<const std::byte> InMessage,
                               std::span<const std::byte> InSignature) noexcept;
}
