// FemboyEngine2D - canonical SHA-256 implementation.
#include "Engine/Core/Sha256.hpp"

#include <cstring>

namespace Fby::Crypto
{
namespace
{
// Round constants: the first 32 bits of the fractional parts of the cube roots
// of the first 64 primes.
constexpr std::uint32_t kRoundConstants[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};

// Initial hash values: the first 32 bits of the fractional parts of the square
// roots of the first eight primes.
constexpr std::uint32_t kInitialState[8] = {
    0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au, 0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u,
};

[[nodiscard]] constexpr std::uint32_t RotateRight(std::uint32_t InValue, std::uint32_t InBits) noexcept
{
    return (InValue >> InBits) | (InValue << (32u - InBits));
}

[[nodiscard]] std::uint32_t LoadBigEndian(const std::uint8_t* InData) noexcept
{
    return (static_cast<std::uint32_t>(InData[0]) << 24) | (static_cast<std::uint32_t>(InData[1]) << 16) |
           (static_cast<std::uint32_t>(InData[2]) << 8) | static_cast<std::uint32_t>(InData[3]);
}

void StoreBigEndian(std::uint32_t InValue, std::uint8_t* OutData) noexcept
{
    OutData[0] = static_cast<std::uint8_t>(InValue >> 24);
    OutData[1] = static_cast<std::uint8_t>(InValue >> 16);
    OutData[2] = static_cast<std::uint8_t>(InValue >> 8);
    OutData[3] = static_cast<std::uint8_t>(InValue);
}

// Compresses one 64-byte block into the chaining state.
void Transform(Sha256Context* InContext, const std::uint8_t* InBlock) noexcept
{
    std::uint32_t ScopedSchedule[64]{};
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < 16; ++ScopedIndex)
    {
        ScopedSchedule[ScopedIndex] = LoadBigEndian(InBlock + ScopedIndex * 4u);
    }
    for (std::uint32_t ScopedIndex = 16; ScopedIndex < 64; ++ScopedIndex)
    {
        const std::uint32_t ScopedS0 = RotateRight(ScopedSchedule[ScopedIndex - 15], 7) ^
                                       RotateRight(ScopedSchedule[ScopedIndex - 15], 18) ^
                                       (ScopedSchedule[ScopedIndex - 15] >> 3);
        const std::uint32_t ScopedS1 = RotateRight(ScopedSchedule[ScopedIndex - 2], 17) ^
                                       RotateRight(ScopedSchedule[ScopedIndex - 2], 19) ^
                                       (ScopedSchedule[ScopedIndex - 2] >> 10);
        ScopedSchedule[ScopedIndex] = ScopedSchedule[ScopedIndex - 16] + ScopedS0 +
                                      ScopedSchedule[ScopedIndex - 7] + ScopedS1;
    }

    std::array<std::uint32_t, 8> ScopedWorking = InContext->State;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < 64; ++ScopedIndex)
    {
        const std::uint32_t ScopedS1 = RotateRight(ScopedWorking[4], 6) ^ RotateRight(ScopedWorking[4], 11) ^
                                       RotateRight(ScopedWorking[4], 25);
        const std::uint32_t ScopedChoice =
            (ScopedWorking[4] & ScopedWorking[5]) ^ (~ScopedWorking[4] & ScopedWorking[6]);
        const std::uint32_t ScopedTemp1 =
            ScopedWorking[7] + ScopedS1 + ScopedChoice + kRoundConstants[ScopedIndex] + ScopedSchedule[ScopedIndex];
        const std::uint32_t ScopedS0 = RotateRight(ScopedWorking[0], 2) ^ RotateRight(ScopedWorking[0], 13) ^
                                       RotateRight(ScopedWorking[0], 22);
        const std::uint32_t ScopedMajority =
            (ScopedWorking[0] & ScopedWorking[1]) ^ (ScopedWorking[0] & ScopedWorking[2]) ^
            (ScopedWorking[1] & ScopedWorking[2]);
        const std::uint32_t ScopedTemp2 = ScopedS0 + ScopedMajority;

        ScopedWorking[7] = ScopedWorking[6];
        ScopedWorking[6] = ScopedWorking[5];
        ScopedWorking[5] = ScopedWorking[4];
        ScopedWorking[4] = ScopedWorking[3] + ScopedTemp1;
        ScopedWorking[3] = ScopedWorking[2];
        ScopedWorking[2] = ScopedWorking[1];
        ScopedWorking[1] = ScopedWorking[0];
        ScopedWorking[0] = ScopedTemp1 + ScopedTemp2;
    }

    for (std::size_t ScopedIndex = 0; ScopedIndex < InContext->State.size(); ++ScopedIndex)
    {
        InContext->State[ScopedIndex] += ScopedWorking[ScopedIndex];
    }
}

// Appends the one-bit terminator and the 64-bit big-endian bit length.
void WritePadding(Sha256Context* InContext) noexcept
{
    const std::uint64_t ScopedBits = InContext->TotalBytes * 8u;
    InContext->Buffer[InContext->BufferedBytes++] = 0x80u;
    if (InContext->BufferedBytes > 56u)
    {
        std::memset(InContext->Buffer.data() + InContext->BufferedBytes, 0,
                    kSha256BlockBytes - InContext->BufferedBytes);
        Transform(InContext, InContext->Buffer.data());
        InContext->BufferedBytes = 0;
    }
    std::memset(InContext->Buffer.data() + InContext->BufferedBytes, 0, 56u - InContext->BufferedBytes);
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < 8u; ++ScopedIndex)
    {
        InContext->Buffer[56u + ScopedIndex] = static_cast<std::uint8_t>(ScopedBits >> (56u - ScopedIndex * 8u));
    }
    Transform(InContext, InContext->Buffer.data());
    InContext->BufferedBytes = 0;
}
}

void Sha256Init(Sha256Context* OutContext) noexcept
{
    if (OutContext == nullptr)
    {
        return;
    }
    *OutContext = Sha256Context{};
    std::memcpy(OutContext->State.data(), kInitialState, sizeof(kInitialState));
}

void Sha256Update(Sha256Context* InContext, std::span<const std::byte> InData) noexcept
{
    if (InContext == nullptr)
    {
        return;
    }
    std::size_t ScopedOffset = 0;
    while (ScopedOffset < InData.size())
    {
        const std::size_t ScopedSpace = kSha256BlockBytes - InContext->BufferedBytes;
        const std::size_t ScopedRemaining = InData.size() - ScopedOffset;
        const std::size_t ScopedChunk = ScopedSpace < ScopedRemaining ? ScopedSpace : ScopedRemaining;
        std::memcpy(InContext->Buffer.data() + InContext->BufferedBytes, InData.data() + ScopedOffset, ScopedChunk);
        InContext->BufferedBytes += static_cast<std::uint32_t>(ScopedChunk);
        InContext->TotalBytes += ScopedChunk;
        ScopedOffset += ScopedChunk;
        if (InContext->BufferedBytes == kSha256BlockBytes)
        {
            Transform(InContext, InContext->Buffer.data());
            InContext->BufferedBytes = 0;
        }
    }
}

void Sha256Final(Sha256Context* InContext, std::span<std::byte> OutDigest) noexcept
{
    if (OutDigest.size() != kSha256Bytes)
    {
        if (!OutDigest.empty())
        {
            std::memset(OutDigest.data(), 0, OutDigest.size());
        }
        return;
    }
    if (InContext == nullptr)
    {
        std::memset(OutDigest.data(), 0, OutDigest.size());
        return;
    }

    WritePadding(InContext);
    for (std::size_t ScopedIndex = 0; ScopedIndex < 8; ++ScopedIndex)
    {
        StoreBigEndian(InContext->State[ScopedIndex],
                       reinterpret_cast<std::uint8_t*>(OutDigest.data()) + ScopedIndex * 4u);
    }
    InContext->Buffer.fill(0);
    InContext->TotalBytes = 0;
}

void Sha256(std::span<const std::byte> InData, std::span<std::byte> OutDigest) noexcept
{
    Sha256Context ScopedContext;
    Sha256Init(&ScopedContext);
    Sha256Update(&ScopedContext, InData);
    Sha256Final(&ScopedContext, OutDigest);
    std::memset(&ScopedContext, 0, sizeof(ScopedContext));
}
}
