// FemboyEngine2D - XChaCha20-Poly1305 AEAD over Monocypher.
#pragma once

#include <cstddef>
#include <span>

namespace Fby::Crypto
{

inline constexpr std::size_t kXChaChaKeyBytes = 32;
inline constexpr std::size_t kXChaChaNonceBytes = 24;
inline constexpr std::size_t kXChaChaTagBytes = 16;

// Encrypts one contiguous message. Every span size is part of the contract: a
// mismatched key, nonce, tag or ciphertext span is a silent no-op that zeroes
// the outputs, never a partial write.
void XChaCha20Poly1305Encrypt(std::span<const std::byte> InKey, std::span<const std::byte> InNonce,
                              std::span<const std::byte> InAad, std::span<const std::byte> InPlaintext,
                              std::span<std::byte> OutCiphertext, std::span<std::byte> OutTag) noexcept;

// Decrypts and authenticates. On failure the plaintext buffer is zeroed so a
// caller that ignores the result cannot read unauthenticated bytes.
[[nodiscard]] bool XChaCha20Poly1305Decrypt(std::span<const std::byte> InKey, std::span<const std::byte> InNonce,
                                            std::span<const std::byte> InAad,
                                            std::span<const std::byte> InCiphertext,
                                            std::span<const std::byte> InTag,
                                            std::span<std::byte> OutPlaintext) noexcept;
}
