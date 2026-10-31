// FemboyEngine2D - X25519 and EdDSA implementation over Monocypher.
#include "Engine/Core/CryptoAsymmetric.hpp"

#include "Engine/Core/CryptoKdf.hpp"

#include "monocypher.h"

#include <cstdint>
#include <cstring>

namespace Fby::Crypto
{
namespace
{
[[nodiscard]] const std::uint8_t* BytesOf(std::span<const std::byte> InBytes) noexcept
{
    return reinterpret_cast<const std::uint8_t*>(InBytes.data());
}

[[nodiscard]] std::uint8_t* BytesOf(std::span<std::byte> InBytes) noexcept
{
    return reinterpret_cast<std::uint8_t*>(InBytes.data());
}

[[nodiscard]] const std::uint8_t* DataOf(std::span<const std::byte> InBytes) noexcept
{
    static const std::uint8_t EmptyByte = 0;
    return InBytes.empty() ? &EmptyByte : BytesOf(InBytes);
}

[[nodiscard]] bool AllZero(std::span<const std::uint8_t> InBytes) noexcept
{
    std::uint8_t ScopedAccumulator = 0;
    for (const std::uint8_t ScopedByte : InBytes)
    {
        ScopedAccumulator |= ScopedByte;
    }
    return ScopedAccumulator == 0;
}
}

bool X25519KeyPairFromSeed(std::span<const std::byte> InSeed, X25519KeyPair* OutPair) noexcept
{
    if (OutPair == nullptr || InSeed.size() != kX25519KeyBytes)
    {
        return false;
    }
    crypto_x25519_public_key(BytesOf(std::span<std::byte>(OutPair->Public)), BytesOf(InSeed));
    std::memcpy(OutPair->Secret.data(), InSeed.data(), kX25519KeyBytes);
    return true;
}

bool X25519SharedSecret(std::span<const std::byte> InSecret, std::span<const std::byte> InPeerPublic,
                        std::span<std::byte> OutShared) noexcept
{
    if (InSecret.size() != kX25519KeyBytes || InPeerPublic.size() != kX25519KeyBytes ||
        OutShared.size() != kX25519KeyBytes)
    {
        return false;
    }

    std::array<std::uint8_t, kX25519KeyBytes> ScopedRaw{};
    crypto_x25519(ScopedRaw.data(), BytesOf(InSecret), BytesOf(InPeerPublic));

    // A low-order peer point yields an all-zero raw output. Hashing it would
    // produce a well-defined but attacker-chosen secret, so reject it here.
    const bool ScopedAcceptable = !AllZero(ScopedRaw);
    crypto_blake2b(BytesOf(OutShared), kX25519KeyBytes, ScopedRaw.data(), ScopedRaw.size());
    crypto_wipe(ScopedRaw.data(), ScopedRaw.size());
    return ScopedAcceptable;
}

bool X25519RawSharedSecret(std::span<const std::byte> InSecret, std::span<const std::byte> InPeerPublic,
                           std::span<std::byte> OutShared) noexcept
{
    if (InSecret.size() != kX25519KeyBytes || InPeerPublic.size() != kX25519KeyBytes ||
        OutShared.size() != kX25519KeyBytes)
    {
        return false;
    }

    std::array<std::uint8_t, kX25519KeyBytes> ScopedRaw{};
    crypto_x25519(ScopedRaw.data(), BytesOf(InSecret), BytesOf(InPeerPublic));
    const bool ScopedAcceptable = !AllZero(ScopedRaw);
    std::memcpy(OutShared.data(), ScopedRaw.data(), ScopedRaw.size());
    crypto_wipe(ScopedRaw.data(), ScopedRaw.size());
    return ScopedAcceptable;
}

bool EddsaKeyPairFromSeed(std::span<const std::byte> InSeed, EddsaKeyPair* OutPair) noexcept
{
    if (OutPair == nullptr || InSeed.size() != kEddsaSeedBytes)
    {
        return false;
    }

    // The primitive consumes and wipes its seed argument, so it works on a
    // local copy and never mutates memory the caller still owns.
    std::array<std::uint8_t, kEddsaSeedBytes> ScopedSeed{};
    std::memcpy(ScopedSeed.data(), InSeed.data(), kEddsaSeedBytes);
    crypto_eddsa_key_pair(BytesOf(std::span<std::byte>(OutPair->Secret)), BytesOf(std::span<std::byte>(OutPair->Public)),
                          ScopedSeed.data());
    crypto_wipe(ScopedSeed.data(), ScopedSeed.size());
    return true;
}

void EddsaSign(std::span<const std::byte> InSecret, std::span<const std::byte> InMessage,
               std::span<std::byte> OutSignature) noexcept
{
    if (InSecret.size() != kEddsaSecretBytes || OutSignature.size() != kEddsaSignatureBytes)
    {
        return;
    }
    crypto_eddsa_sign(BytesOf(OutSignature), BytesOf(InSecret), DataOf(InMessage), InMessage.size());
}

bool EddsaVerify(std::span<const std::byte> InPublic, std::span<const std::byte> InMessage,
                 std::span<const std::byte> InSignature) noexcept
{
    if (InPublic.size() != kEddsaPublicBytes || InSignature.size() != kEddsaSignatureBytes)
    {
        return false;
    }
    return crypto_eddsa_check(BytesOf(InSignature), BytesOf(InPublic), DataOf(InMessage), InMessage.size()) == 0;
}
}
