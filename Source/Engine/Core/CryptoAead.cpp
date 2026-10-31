// FemboyEngine2D - XChaCha20-Poly1305 implementation over Monocypher.
#include "Engine/Core/CryptoAead.hpp"

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
}

void XChaCha20Poly1305Encrypt(std::span<const std::byte> InKey, std::span<const std::byte> InNonce,
                              std::span<const std::byte> InAad, std::span<const std::byte> InPlaintext,
                              std::span<std::byte> OutCiphertext, std::span<std::byte> OutTag) noexcept
{
    if (InKey.size() != kXChaChaKeyBytes || InNonce.size() != kXChaChaNonceBytes ||
        OutTag.size() != kXChaChaTagBytes || OutCiphertext.size() != InPlaintext.size())
    {
        if (!OutCiphertext.empty())
        {
            std::memset(OutCiphertext.data(), 0, OutCiphertext.size());
        }
        if (!OutTag.empty())
        {
            std::memset(OutTag.data(), 0, OutTag.size());
        }
        return;
    }
    crypto_aead_lock(BytesOf(OutCiphertext), BytesOf(OutTag), BytesOf(InKey), BytesOf(InNonce), DataOf(InAad),
                     InAad.size(), DataOf(InPlaintext), InPlaintext.size());
}

bool XChaCha20Poly1305Decrypt(std::span<const std::byte> InKey, std::span<const std::byte> InNonce,
                              std::span<const std::byte> InAad, std::span<const std::byte> InCiphertext,
                              std::span<const std::byte> InTag, std::span<std::byte> OutPlaintext) noexcept
{
    if (InKey.size() != kXChaChaKeyBytes || InNonce.size() != kXChaChaNonceBytes ||
        InTag.size() != kXChaChaTagBytes || OutPlaintext.size() != InCiphertext.size())
    {
        if (!OutPlaintext.empty())
        {
            std::memset(OutPlaintext.data(), 0, OutPlaintext.size());
        }
        return false;
    }

    const int ScopedStatus = crypto_aead_unlock(BytesOf(OutPlaintext), BytesOf(InTag), BytesOf(InKey),
                                                BytesOf(InNonce), DataOf(InAad), InAad.size(),
                                                DataOf(InCiphertext), InCiphertext.size());
    if (ScopedStatus != 0)
    {
        if (!OutPlaintext.empty())
        {
            std::memset(OutPlaintext.data(), 0, OutPlaintext.size());
        }
        return false;
    }
    return true;
}
}
