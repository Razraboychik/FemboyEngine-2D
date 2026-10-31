// FemboyEngine2D - allocation-free authenticated encryption.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

namespace Fby::Crypto
{

inline constexpr std::size_t kAeadKeyBytes = 32;
inline constexpr std::size_t kAeadNonceBytes = 12;
inline constexpr std::size_t kAeadTagBytes = 16;
inline constexpr std::size_t kAeadBlockBytes = 16;

enum class AeadAlgorithm : std::uint8_t
{
    None = 0,
    Aes256Gcm = 1,
    ChaCha20Poly1305 = 2,
};

[[nodiscard]] constexpr const char* AeadAlgorithmName(AeadAlgorithm InAlgorithm) noexcept
{
    switch (InAlgorithm)
    {
        case AeadAlgorithm::None:
            return "none";
        case AeadAlgorithm::Aes256Gcm:
            return "aes-256-gcm";
        case AeadAlgorithm::ChaCha20Poly1305:
            return "chacha20-poly1305";
    }
    return "none";
}

struct AeadKey
{
    std::array<std::byte, kAeadKeyBytes> bytes{};

    void Clear() noexcept;
};

struct AeadNonce
{
    std::array<std::byte, kAeadNonceBytes> bytes{};

    [[nodiscard]] std::uint64_t Counter() const noexcept;
    void SetCounter(std::uint64_t InCounter) noexcept;
};

struct AeadTag
{
    std::array<std::byte, kAeadTagBytes> bytes{};

    [[nodiscard]] bool Equals(const AeadTag& InOther) const noexcept;
};

static_assert(std::is_trivially_copyable_v<AeadKey>);
static_assert(std::is_trivially_copyable_v<AeadNonce>);
static_assert(sizeof(AeadTag) == 16);

// The tag comparison is constant-time: a launcher rejecting a modified archive
[[nodiscard]] bool ConstantTimeEquals(std::span<const std::byte> InLeft, std::span<const std::byte> InRight) noexcept;

// Writes `in.size()` ciphertext bytes. Encryption may run in place.
[[nodiscard]] bool Aes256GcmEncrypt(const AeadKey& InKey, const AeadNonce& InNonce, std::span<const std::byte> InAad,
                                    std::span<const std::byte> InPlaintext, std::span<std::byte> OutCiphertext,
                                    AeadTag& OutTag) noexcept;
[[nodiscard]] bool Aes256GcmDecrypt(const AeadKey& InKey, const AeadNonce& InNonce, std::span<const std::byte> InAad,
                                    std::span<const std::byte> InCiphertext, std::span<std::byte> OutPlaintext,
                                    const AeadTag& InTag) noexcept;

[[nodiscard]] bool ChaCha20Poly1305Encrypt(const AeadKey& InKey, const AeadNonce& InNonce,
                                           std::span<const std::byte> InAad, std::span<const std::byte> InPlaintext,
                                           std::span<std::byte> OutCiphertext, AeadTag& OutTag) noexcept;
[[nodiscard]] bool ChaCha20Poly1305Decrypt(const AeadKey& InKey, const AeadNonce& InNonce,
                                           std::span<const std::byte> InAad, std::span<const std::byte> InCiphertext,
                                           std::span<std::byte> OutPlaintext, const AeadTag& InTag) noexcept;

// Raw stream primitive, exposed for archive naming obfuscation and tests
[[nodiscard]] bool ChaCha20Xor(const AeadKey& InKey, const AeadNonce& InNonce, std::uint32_t InCounter,
                               std::span<const std::byte> InInput, std::span<std::byte> OutOutput) noexcept;

// Poly1305 over one contiguous message. The AEAD path uses it internally; the
[[nodiscard]] bool Poly1305Mac(const AeadKey& InKey, std::span<const std::byte> InMessage,
                               std::span<std::byte, kAeadTagBytes> OutTag) noexcept;

// CRC-64/ECMA. Not a security primitive: it detects a truncated or reordered
[[nodiscard]] std::uint64_t Crc64Ecma(std::span<const std::byte> InBytes, std::uint64_t InSeed = 0) noexcept;

// The AES-NI block path is used when the build enables FBY_CRYPTO_ENABLE_AESNI.
[[nodiscard]] bool HardwareAesAvailable() noexcept;

void SecureZero(std::span<std::byte> InBytes) noexcept;
}
