// FemboyEngine2D - BLAKE2b, HMAC and HKDF implementation.
#include "Engine/Core/CryptoKdf.hpp"

#include "monocypher.h"

#include <array>
#include <cstdint>
#include <cstring>

namespace Fby::Crypto
{
namespace
{
constexpr std::uint8_t kInnerPad = 0x36;
constexpr std::uint8_t kOuterPad = 0x5C;

[[nodiscard]] const std::uint8_t* BytesOf(std::span<const std::byte> InBytes) noexcept
{
    return reinterpret_cast<const std::uint8_t*>(InBytes.data());
}

[[nodiscard]] std::uint8_t* BytesOf(std::span<std::byte> InBytes) noexcept
{
    return reinterpret_cast<std::uint8_t*>(InBytes.data());
}

// Points at a stable one-byte object when a span is empty: the backend still
// takes a pointer even for a zero-length message.
[[nodiscard]] const std::uint8_t* DataOf(std::span<const std::byte> InBytes) noexcept
{
    static const std::uint8_t EmptyByte = 0;
    return InBytes.empty() ? &EmptyByte : BytesOf(InBytes);
}

// Derives the HMAC key block: a key longer than the block is hashed first and
// the remainder is zero-padded.
void PrepareKeyBlock(std::span<const std::byte> InKey, std::array<std::uint8_t, kBlake2bBlockBytes>* OutBlock) noexcept
{
    OutBlock->fill(0);
    if (InKey.size() > kBlake2bBlockBytes)
    {
        crypto_blake2b(OutBlock->data(), kBlake2bBytes, BytesOf(InKey), InKey.size());
        return;
    }
    if (!InKey.empty())
    {
        std::memcpy(OutBlock->data(), InKey.data(), InKey.size());
    }
}

// HMAC over the concatenation of up to three spans. Keeping the parts separate
// means HKDF never copies a long info string onto the stack.
void HmacParts(std::span<const std::byte> InKey, std::span<const std::byte> InFirst,
               std::span<const std::byte> InSecond, std::span<const std::byte> InThird,
               std::span<std::byte> OutMac) noexcept
{
    std::array<std::uint8_t, kBlake2bBlockBytes> ScopedKeyBlock{};
    PrepareKeyBlock(InKey, &ScopedKeyBlock);

    std::array<std::uint8_t, kBlake2bBytes> ScopedInner{};
    crypto_blake2b_ctx ScopedContext;
    crypto_blake2b_init(&ScopedContext, kBlake2bBytes);
    for (std::uint8_t& ScopedByte : ScopedKeyBlock)
    {
        ScopedByte ^= kInnerPad;
    }
    crypto_blake2b_update(&ScopedContext, ScopedKeyBlock.data(), ScopedKeyBlock.size());
    if (!InFirst.empty())
    {
        crypto_blake2b_update(&ScopedContext, BytesOf(InFirst), InFirst.size());
    }
    if (!InSecond.empty())
    {
        crypto_blake2b_update(&ScopedContext, BytesOf(InSecond), InSecond.size());
    }
    if (!InThird.empty())
    {
        crypto_blake2b_update(&ScopedContext, BytesOf(InThird), InThird.size());
    }
    crypto_blake2b_final(&ScopedContext, ScopedInner.data());

    // Restore the key block and turn it into the outer padding.
    for (std::size_t ScopedIndex = 0; ScopedIndex < ScopedKeyBlock.size(); ++ScopedIndex)
    {
        ScopedKeyBlock[ScopedIndex] ^= static_cast<std::uint8_t>(kInnerPad ^ kOuterPad);
    }
    crypto_blake2b_init(&ScopedContext, kBlake2bBytes);
    crypto_blake2b_update(&ScopedContext, ScopedKeyBlock.data(), ScopedKeyBlock.size());
    crypto_blake2b_update(&ScopedContext, ScopedInner.data(), ScopedInner.size());
    crypto_blake2b_final(&ScopedContext, BytesOf(OutMac));

    crypto_wipe(ScopedKeyBlock.data(), ScopedKeyBlock.size());
    crypto_wipe(ScopedInner.data(), ScopedInner.size());
    crypto_wipe(&ScopedContext, sizeof(ScopedContext));
}
}

void Blake2b(std::span<const std::byte> InData, std::span<std::byte> OutDigest) noexcept
{
    if (OutDigest.empty() || OutDigest.size() > kBlake2bBytes)
    {
        return;
    }
    crypto_blake2b(BytesOf(OutDigest), OutDigest.size(), DataOf(InData), InData.size());
}

void Blake2bKeyed(std::span<const std::byte> InKey, std::span<const std::byte> InData,
                  std::span<std::byte> OutDigest) noexcept
{
    if (OutDigest.empty() || OutDigest.size() > kBlake2bBytes || InKey.empty() ||
        InKey.size() > kBlake2bMaxKeyBytes)
    {
        if (!OutDigest.empty())
        {
            std::memset(OutDigest.data(), 0, OutDigest.size());
        }
        return;
    }
    crypto_blake2b_keyed(BytesOf(OutDigest), OutDigest.size(), BytesOf(InKey), InKey.size(), DataOf(InData),
                         InData.size());
}

void HmacBlake2b(std::span<const std::byte> InKey, std::span<const std::byte> InData,
                 std::span<std::byte> OutMac) noexcept
{
    if (OutMac.size() != kBlake2bBytes)
    {
        if (!OutMac.empty())
        {
            std::memset(OutMac.data(), 0, OutMac.size());
        }
        return;
    }
    HmacParts(InKey, InData, {}, {}, OutMac);
}

void HkdfBlake2b(std::span<const std::byte> InSecret, std::span<const std::byte> InSalt,
                 std::span<const std::byte> InInfo, std::span<std::byte> OutKey) noexcept
{
    if (OutKey.empty() || OutKey.size() > kHkdfMaxOutputBytes)
    {
        if (!OutKey.empty())
        {
            std::memset(OutKey.data(), 0, OutKey.size());
        }
        return;
    }

    // Extract: PRK = HMAC(salt, IKM). An empty salt is the RFC default of
    // HashLen zero bytes, which HMAC represents as a zero key block.
    std::array<std::byte, kBlake2bBytes> ScopedPrk{};
    HmacParts(InSalt, InSecret, {}, {}, ScopedPrk);

    std::array<std::byte, kBlake2bBytes> ScopedBlock{};
    std::size_t ScopedWritten = 0;
    std::uint8_t ScopedCounter = 1;
    while (ScopedWritten < OutKey.size())
    {
        const std::span<const std::byte> ScopedPrevious =
            ScopedWritten == 0 ? std::span<const std::byte>{} : std::span<const std::byte>(ScopedBlock);
        const std::span<const std::byte> ScopedCounterBytes(reinterpret_cast<const std::byte*>(&ScopedCounter), 1);
        HmacParts(ScopedPrk, ScopedPrevious, InInfo, ScopedCounterBytes, ScopedBlock);

        const std::size_t ScopedRemaining = OutKey.size() - ScopedWritten;
        const std::size_t ScopedChunk = ScopedRemaining < kBlake2bBytes ? ScopedRemaining : kBlake2bBytes;
        std::memcpy(OutKey.data() + ScopedWritten, ScopedBlock.data(), ScopedChunk);
        ScopedWritten += ScopedChunk;
        ++ScopedCounter;
    }

    crypto_wipe(ScopedPrk.data(), ScopedPrk.size());
    crypto_wipe(ScopedBlock.data(), ScopedBlock.size());
}

void WipeBytes(std::span<std::byte> InBytes) noexcept
{
    if (!InBytes.empty())
    {
        crypto_wipe(InBytes.data(), InBytes.size());
    }
}

namespace
{
constexpr std::uint8_t kShaInnerPad = 0x36;
constexpr std::uint8_t kShaOuterPad = 0x5C;

[[nodiscard]] std::span<const std::byte> AsBytes(std::span<const std::uint8_t> InBytes) noexcept
{
    return std::span<const std::byte>(reinterpret_cast<const std::byte*>(InBytes.data()), InBytes.size());
}

// HMAC-SHA-256 over up to three ranges. Splitting the parts keeps HKDF from
// copying a long info string.
void HmacSha256Parts(std::span<const std::byte> InKey, std::span<const std::byte> InFirst,
                     std::span<const std::byte> InSecond, std::span<const std::byte> InThird,
                     std::span<std::byte> OutMac) noexcept
{
    std::array<std::uint8_t, kSha256BlockBytes> ScopedKeyBlock{};
    if (InKey.size() > kSha256BlockBytes)
    {
        std::array<std::byte, kSha256Bytes> ScopedHashedKey{};
        Sha256(InKey, ScopedHashedKey);
        std::memcpy(ScopedKeyBlock.data(), ScopedHashedKey.data(), ScopedHashedKey.size());
    }
    else if (!InKey.empty())
    {
        std::memcpy(ScopedKeyBlock.data(), InKey.data(), InKey.size());
    }

    std::array<std::uint8_t, kSha256BlockBytes> ScopedPad{};
    for (std::size_t ScopedIndex = 0; ScopedIndex < ScopedPad.size(); ++ScopedIndex)
    {
        ScopedPad[ScopedIndex] = static_cast<std::uint8_t>(ScopedKeyBlock[ScopedIndex] ^ kShaInnerPad);
    }

    std::array<std::byte, kSha256Bytes> ScopedInner{};
    Sha256Context ScopedContext;
    Sha256Init(&ScopedContext);
    Sha256Update(&ScopedContext, AsBytes(ScopedPad));
    if (!InFirst.empty())
    {
        Sha256Update(&ScopedContext, InFirst);
    }
    if (!InSecond.empty())
    {
        Sha256Update(&ScopedContext, InSecond);
    }
    if (!InThird.empty())
    {
        Sha256Update(&ScopedContext, InThird);
    }
    Sha256Final(&ScopedContext, ScopedInner);

    for (std::size_t ScopedIndex = 0; ScopedIndex < ScopedPad.size(); ++ScopedIndex)
    {
        ScopedPad[ScopedIndex] = static_cast<std::uint8_t>(ScopedKeyBlock[ScopedIndex] ^ kShaOuterPad);
    }
    Sha256Init(&ScopedContext);
    Sha256Update(&ScopedContext, AsBytes(ScopedPad));
    Sha256Update(&ScopedContext, ScopedInner);
    Sha256Final(&ScopedContext, OutMac);

    crypto_wipe(ScopedKeyBlock.data(), ScopedKeyBlock.size());
    crypto_wipe(ScopedPad.data(), ScopedPad.size());
    crypto_wipe(ScopedInner.data(), ScopedInner.size());
    crypto_wipe(&ScopedContext, sizeof(ScopedContext));
}
}

void HmacSha256(std::span<const std::byte> InKey, std::span<const std::byte> InData,
                std::span<std::byte> OutMac) noexcept
{
    if (OutMac.size() != kSha256Bytes)
    {
        if (!OutMac.empty())
        {
            std::memset(OutMac.data(), 0, OutMac.size());
        }
        return;
    }
    HmacSha256Parts(InKey, InData, {}, {}, OutMac);
}

void HkdfSha256(std::span<const std::byte> InSecret, std::span<const std::byte> InSalt,
                std::span<const std::byte> InInfo, std::span<std::byte> OutKey) noexcept
{
    if (OutKey.empty() || OutKey.size() > kHkdfSha256MaxOutputBytes)
    {
        if (!OutKey.empty())
        {
            std::memset(OutKey.data(), 0, OutKey.size());
        }
        return;
    }

    std::array<std::byte, kSha256Bytes> ScopedPrk{};
    HmacSha256(InSalt, InSecret, ScopedPrk);

    std::array<std::byte, kSha256Bytes> ScopedBlock{};
    std::size_t ScopedWritten = 0;
    std::uint8_t ScopedCounter = 1;
    while (ScopedWritten < OutKey.size())
    {
        const std::span<const std::byte> ScopedPrevious =
            ScopedWritten == 0 ? std::span<const std::byte>{} : std::span<const std::byte>(ScopedBlock);
        const std::span<const std::byte> ScopedCounterBytes(reinterpret_cast<const std::byte*>(&ScopedCounter), 1);
        HmacSha256Parts(ScopedPrk, ScopedPrevious, InInfo, ScopedCounterBytes, ScopedBlock);

        const std::size_t ScopedRemaining = OutKey.size() - ScopedWritten;
        const std::size_t ScopedChunk = ScopedRemaining < kSha256Bytes ? ScopedRemaining : kSha256Bytes;
        std::memcpy(OutKey.data() + ScopedWritten, ScopedBlock.data(), ScopedChunk);
        ScopedWritten += ScopedChunk;
        ++ScopedCounter;
    }

    crypto_wipe(ScopedPrk.data(), ScopedPrk.size());
    crypto_wipe(ScopedBlock.data(), ScopedBlock.size());
}
}
