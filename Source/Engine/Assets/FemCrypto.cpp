// FemboyEngine2D - AEAD implementation.
#include "Engine/Assets/FemCrypto.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>

#if defined(FBY_CRYPTO_ENABLE_AESNI) && (defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86))
#include <wmmintrin.h>
#define FBY_CRYPTO_HAS_AESNI 1
#else
#define FBY_CRYPTO_HAS_AESNI 0
#endif

namespace Fby::Crypto
{
namespace
{

[[nodiscard]] constexpr std::uint32_t Load32Le(const std::byte* InData) noexcept
{
    return static_cast<std::uint32_t>(InData[0]) | (static_cast<std::uint32_t>(InData[1]) << 8) |
           (static_cast<std::uint32_t>(InData[2]) << 16) | (static_cast<std::uint32_t>(InData[3]) << 24);
}

void Store32Le(std::byte* OutData, std::uint32_t InValue) noexcept
{
    OutData[0] = static_cast<std::byte>(InValue & 0xFFu);
    OutData[1] = static_cast<std::byte>((InValue >> 8) & 0xFFu);
    OutData[2] = static_cast<std::byte>((InValue >> 16) & 0xFFu);
    OutData[3] = static_cast<std::byte>((InValue >> 24) & 0xFFu);
}

[[nodiscard]] constexpr std::uint64_t Load64Le(const std::byte* InData) noexcept
{
    return static_cast<std::uint64_t>(Load32Le(InData)) | (static_cast<std::uint64_t>(Load32Le(InData + 4)) << 32);
}

[[nodiscard]] constexpr std::uint32_t Load32Be(const std::byte* InData) noexcept
{
    return (static_cast<std::uint32_t>(InData[0]) << 24) | (static_cast<std::uint32_t>(InData[1]) << 16) |
           (static_cast<std::uint32_t>(InData[2]) << 8) | static_cast<std::uint32_t>(InData[3]);
}

[[nodiscard]] constexpr std::uint64_t Load64Be(const std::byte* InData) noexcept
{
    return (static_cast<std::uint64_t>(Load32Be(InData)) << 32) | static_cast<std::uint64_t>(Load32Be(InData + 4));
}

void Store64Le(std::byte* OutData, std::uint64_t InValue) noexcept
{
    Store32Le(OutData, static_cast<std::uint32_t>(InValue));
    Store32Le(OutData + 4, static_cast<std::uint32_t>(InValue >> 32));
}

void Store32Be(std::byte* OutData, std::uint32_t InValue) noexcept
{
    OutData[0] = static_cast<std::byte>((InValue >> 24) & 0xFFu);
    OutData[1] = static_cast<std::byte>((InValue >> 16) & 0xFFu);
    OutData[2] = static_cast<std::byte>((InValue >> 8) & 0xFFu);
    OutData[3] = static_cast<std::byte>(InValue & 0xFFu);
}

[[nodiscard]] constexpr std::uint32_t RotateLeft(std::uint32_t InValue, int InBits) noexcept
{
    return (InValue << InBits) | (InValue >> (32 - InBits));
}

// AES-256 block cipher
constexpr std::array<std::uint8_t, 256> kSbox = {
    0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76, 0xca, 0x82,
    0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0, 0xb7, 0xfd, 0x93, 0x26,
    0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15, 0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96,
    0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75, 0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0,
    0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84, 0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb,
    0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf, 0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f,
    0x50, 0x3c, 0x9f, 0xa8, 0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff,
    0xf3, 0xd2, 0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
    0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb, 0xe0, 0x32,
    0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79, 0xe7, 0xc8, 0x37, 0x6d,
    0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08, 0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6,
    0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a, 0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e,
    0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e, 0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e,
    0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf, 0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f,
    0xb0, 0x54, 0xbb, 0x16};

constexpr std::array<std::uint8_t, 11> kRcon = {0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36};

struct Aes256Key
{
    std::array<std::uint8_t, 240> roundKeys{};
};

void AesExpandKey(const std::byte* InKey, Aes256Key& OutKey) noexcept
{
    std::array<std::uint8_t, 32> ScopedKey{};
    for (std::size_t ScopedI = 0; ScopedI < 32; ++ScopedI)
    {
        ScopedKey[ScopedI] = static_cast<std::uint8_t>(InKey[ScopedI]);
    }
    std::memcpy(OutKey.roundKeys.data(), ScopedKey.data(), 32);

    std::uint8_t ScopedTemp[4]{};
    std::uint32_t ScopedGenerated = 32;
    std::uint32_t ScopedRconIndex = 1;
    while (ScopedGenerated < 240)
    {
        std::memcpy(ScopedTemp, OutKey.roundKeys.data() + ScopedGenerated - 4, 4);
        if ((ScopedGenerated % 32) == 0)
        {
            const std::uint8_t ScopedFirst = ScopedTemp[0];
            ScopedTemp[0] = static_cast<std::uint8_t>(kSbox[ScopedTemp[1]] ^ kRcon[ScopedRconIndex]);
            ScopedTemp[1] = kSbox[ScopedTemp[2]];
            ScopedTemp[2] = kSbox[ScopedTemp[3]];
            ScopedTemp[3] = kSbox[ScopedFirst];
            ++ScopedRconIndex;
        }
        else if ((ScopedGenerated % 32) == 16)
        {
            for (std::uint8_t& ScopedByte : ScopedTemp)
            {
                ScopedByte = kSbox[ScopedByte];
            }
        }
        for (std::uint32_t ScopedI = 0; ScopedI < 4; ++ScopedI)
        {
            OutKey.roundKeys[ScopedGenerated] =
                static_cast<std::uint8_t>(OutKey.roundKeys[ScopedGenerated - 32] ^ ScopedTemp[ScopedI]);
            ++ScopedGenerated;
        }
    }
}

[[nodiscard]] std::uint8_t Multiply(std::uint8_t InA, std::uint8_t InB) noexcept
{
    std::uint8_t ScopedResult = 0;
    std::uint8_t ScopedA = InA;
    std::uint8_t ScopedB = InB;
    for (int ScopedI = 0; ScopedI < 8; ++ScopedI)
    {
        if ((ScopedB & 1u) != 0)
        {
            ScopedResult = static_cast<std::uint8_t>(ScopedResult ^ ScopedA);
        }
        const bool ScopedHigh = (ScopedA & 0x80u) != 0;
        ScopedA = static_cast<std::uint8_t>(ScopedA << 1);
        if (ScopedHigh)
        {
            ScopedA = static_cast<std::uint8_t>(ScopedA ^ 0x1bu);
        }
        ScopedB = static_cast<std::uint8_t>(ScopedB >> 1);
    }
    return ScopedResult;
}

void AesEncryptBlockPortable(const Aes256Key& InKey, const std::byte* InInput, std::byte* OutOutput) noexcept
{
    std::uint8_t ScopedState[16]{};
    for (std::size_t ScopedI = 0; ScopedI < 16; ++ScopedI)
    {
        ScopedState[ScopedI] = static_cast<std::uint8_t>(InInput[ScopedI]) ^ InKey.roundKeys[ScopedI];
    }

    for (std::uint32_t ScopedRound = 1; ScopedRound <= 14; ++ScopedRound)
    {
        // SubBytes
        for (std::uint8_t& ScopedByte : ScopedState)
        {
            ScopedByte = kSbox[ScopedByte];
        }
        // ShiftRows: row r rotates left by r columns
        std::uint8_t ScopedShifted[16]{};
        for (std::uint32_t ScopedColumn = 0; ScopedColumn < 4; ++ScopedColumn)
        {
            for (std::uint32_t ScopedRow = 0; ScopedRow < 4; ++ScopedRow)
            {
                ScopedShifted[ScopedColumn * 4 + ScopedRow] =
                    ScopedState[((ScopedColumn + ScopedRow) & 3u) * 4 + ScopedRow];
            }
        }
        std::memcpy(ScopedState, ScopedShifted, 16);

        if (ScopedRound != 14)
        {
            for (std::uint32_t ScopedColumn = 0; ScopedColumn < 4; ++ScopedColumn)
            {
                std::uint8_t* ScopedC = ScopedState + ScopedColumn * 4;
                const std::uint8_t ScopedA0 = ScopedC[0];
                const std::uint8_t ScopedA1 = ScopedC[1];
                const std::uint8_t ScopedA2 = ScopedC[2];
                const std::uint8_t ScopedA3 = ScopedC[3];
                ScopedC[0] = static_cast<std::uint8_t>(Multiply(ScopedA0, 2) ^ Multiply(ScopedA1, 3) ^ ScopedA2 ^ ScopedA3);
                ScopedC[1] = static_cast<std::uint8_t>(ScopedA0 ^ Multiply(ScopedA1, 2) ^ Multiply(ScopedA2, 3) ^ ScopedA3);
                ScopedC[2] = static_cast<std::uint8_t>(ScopedA0 ^ ScopedA1 ^ Multiply(ScopedA2, 2) ^ Multiply(ScopedA3, 3));
                ScopedC[3] = static_cast<std::uint8_t>(Multiply(ScopedA0, 3) ^ ScopedA1 ^ ScopedA2 ^ Multiply(ScopedA3, 2));
            }
        }
        const std::uint32_t ScopedKeyOffset = ScopedRound * 16;
        for (std::uint32_t ScopedI = 0; ScopedI < 16; ++ScopedI)
        {
            ScopedState[ScopedI] =
                static_cast<std::uint8_t>(ScopedState[ScopedI] ^ InKey.roundKeys[ScopedKeyOffset + ScopedI]);
        }
    }
    for (std::size_t ScopedI = 0; ScopedI < 16; ++ScopedI)
    {
        OutOutput[ScopedI] = static_cast<std::byte>(ScopedState[ScopedI]);
    }
}

void AesEncryptBlock(const Aes256Key& InKey, const std::byte* InInput, std::byte* OutOutput) noexcept
{
#if FBY_CRYPTO_HAS_AESNI
    __m128i ScopedBlock = _mm_loadu_si128(reinterpret_cast<const __m128i*>(InInput));
    ScopedBlock = _mm_xor_si128(ScopedBlock, _mm_loadu_si128(reinterpret_cast<const __m128i*>(InKey.roundKeys.data())));
    for (std::uint32_t ScopedRound = 1; ScopedRound < 14; ++ScopedRound)
    {
        ScopedBlock = _mm_aesenc_si128(
            ScopedBlock, _mm_loadu_si128(reinterpret_cast<const __m128i*>(InKey.roundKeys.data() + ScopedRound * 16)));
    }
    ScopedBlock = _mm_aesenclast_si128(
        ScopedBlock, _mm_loadu_si128(reinterpret_cast<const __m128i*>(InKey.roundKeys.data() + 14 * 16)));
    _mm_storeu_si128(reinterpret_cast<__m128i*>(OutOutput), ScopedBlock);
#else
    AesEncryptBlockPortable(InKey, InInput, OutOutput);
#endif
}

// GHASH / GCM
struct GHashState
{
    std::uint64_t high = 0;
    std::uint64_t low = 0;
};

[[nodiscard]] GHashState GfMultiply(GHashState InX, const GHashState& InY) noexcept
{
    GHashState ScopedZ{};
    GHashState ScopedV = InY;
    for (int ScopedBit = 127; ScopedBit >= 0; --ScopedBit)
    {
        const std::uint64_t ScopedMask = (ScopedBit >= 64) ? (InX.high >> (ScopedBit - 64)) & 1ull : (InX.low >> ScopedBit) & 1ull;
        if (ScopedMask != 0)
        {
            ScopedZ.high ^= ScopedV.high;
            ScopedZ.low ^= ScopedV.low;
        }
        const bool ScopedCarry = (ScopedV.low & 1ull) != 0;
        ScopedV.low = (ScopedV.low >> 1) | (ScopedV.high << 63);
        ScopedV.high >>= 1;
        if (ScopedCarry)
        {
            ScopedV.high ^= 0xE100000000000000ull;
        }
    }
    return ScopedZ;
}

void GHashBlock(GHashState& InState, const GHashState& InH, const std::byte* InBlock) noexcept
{
    std::byte ScopedSwapped[16]{};
    for (std::size_t ScopedI = 0; ScopedI < 16; ++ScopedI)
    {
        ScopedSwapped[ScopedI] = InBlock[15 - ScopedI];
    }
    // Load big-endian 128-bit into the pair: high is the first eight bytes.
    InState.high ^= Load64Le(ScopedSwapped + 8);
    InState.low ^= Load64Le(ScopedSwapped);
    InState = GfMultiply(InState, InH);
}

void GHashBytes(GHashState& InState, const GHashState& InH, std::span<const std::byte> InBytes) noexcept
{
    std::size_t ScopedOffset = 0;
    while (InBytes.size() - ScopedOffset >= 16)
    {
        GHashBlock(InState, InH, InBytes.data() + ScopedOffset);
        ScopedOffset += 16;
    }
    if (ScopedOffset < InBytes.size())
    {
        std::byte ScopedTail[16]{};
        std::memcpy(ScopedTail, InBytes.data() + ScopedOffset, InBytes.size() - ScopedOffset);
        GHashBlock(InState, InH, ScopedTail);
    }
}

void AesGcmTag(const Aes256Key& InKey, const std::byte* InJ0, std::span<const std::byte> InAad,
               std::span<const std::byte> InCiphertext, std::span<std::byte, 16> OutTag) noexcept
{
    std::byte ScopedHBlock[16]{};
    AesEncryptBlock(InKey, ScopedHBlock, ScopedHBlock);
    GHashState ScopedH{};
    ScopedH.high = Load64Be(ScopedHBlock);
    ScopedH.low = Load64Be(ScopedHBlock + 8);

    GHashState ScopedState{};
    GHashBytes(ScopedState, ScopedH, InAad);
    GHashBytes(ScopedState, ScopedH, InCiphertext);

    std::byte ScopedLengths[16]{};
    const std::uint64_t ScopedAadBits = static_cast<std::uint64_t>(InAad.size()) * 8ull;
    const std::uint64_t ScopedCipherBits = static_cast<std::uint64_t>(InCiphertext.size()) * 8ull;
    Store32Be(ScopedLengths + 0, static_cast<std::uint32_t>(ScopedAadBits >> 32));
    Store32Be(ScopedLengths + 4, static_cast<std::uint32_t>(ScopedAadBits));
    Store32Be(ScopedLengths + 8, static_cast<std::uint32_t>(ScopedCipherBits >> 32));
    Store32Be(ScopedLengths + 12, static_cast<std::uint32_t>(ScopedCipherBits));
    GHashBlock(ScopedState, ScopedH, ScopedLengths);

    std::byte ScopedTagMask[16]{};
    AesEncryptBlock(InKey, InJ0, ScopedTagMask);
    std::byte ScopedTagHigh[8]{};
    std::byte ScopedTagLow[8]{};
    Store64Le(ScopedTagHigh, ScopedState.high);
    Store64Le(ScopedTagLow, ScopedState.low);
    for (std::size_t ScopedI = 0; ScopedI < 8; ++ScopedI)
    {
        OutTag[ScopedI] = static_cast<std::byte>(ScopedTagMask[ScopedI] ^ ScopedTagHigh[7 - ScopedI]);
        OutTag[8 + ScopedI] = static_cast<std::byte>(ScopedTagMask[8 + ScopedI] ^ ScopedTagLow[7 - ScopedI]);
    }
}

void AesGcmCtr(const Aes256Key& InKey, const std::byte* InCounter, std::span<const std::byte> InInput,
               std::span<std::byte> OutOutput) noexcept
{
    std::byte ScopedCounter[16]{};
    std::memcpy(ScopedCounter, InCounter, 16);
    std::size_t ScopedOffset = 0;
    while (ScopedOffset < InInput.size())
    {
        std::byte ScopedKeyStream[16]{};
        AesEncryptBlock(InKey, ScopedCounter, ScopedKeyStream);
        const std::size_t ScopedCount = std::min<std::size_t>(16, InInput.size() - ScopedOffset);
        for (std::size_t ScopedI = 0; ScopedI < ScopedCount; ++ScopedI)
        {
            OutOutput[ScopedOffset + ScopedI] =
                static_cast<std::byte>(InInput[ScopedOffset + ScopedI] ^ ScopedKeyStream[ScopedI]);
        }
        for (int ScopedI = 15; ScopedI >= 12; --ScopedI)
        {
            ScopedCounter[ScopedI] = static_cast<std::byte>(static_cast<std::uint8_t>(ScopedCounter[ScopedI]) + 1u);
            if (ScopedCounter[ScopedI] != std::byte{0})
            {
                break;
            }
        }
        ScopedOffset += ScopedCount;
    }
}

void AesGcmJ0(const AeadNonce& InNonce, std::byte* OutJ0) noexcept
{
    std::memcpy(OutJ0, InNonce.bytes.data(), 12);
    OutJ0[12] = std::byte{0};
    OutJ0[13] = std::byte{0};
    OutJ0[14] = std::byte{0};
    OutJ0[15] = std::byte{1};
}

// ChaCha20
void ChaChaQuarterRound(std::uint32_t& InA, std::uint32_t& InB, std::uint32_t& InC, std::uint32_t& InD) noexcept
{
    InA += InB;
    InD ^= InA;
    InD = RotateLeft(InD, 16);
    InC += InD;
    InB ^= InC;
    InB = RotateLeft(InB, 12);
    InA += InB;
    InD ^= InA;
    InD = RotateLeft(InD, 8);
    InC += InD;
    InB ^= InC;
    InB = RotateLeft(InB, 7);
}

void ChaChaBlock(const AeadKey& InKey, const AeadNonce& InNonce, std::uint32_t InCounter,
                 std::byte* OutBlock) noexcept
{
    std::uint32_t ScopedState[16]{};
    ScopedState[0] = 0x61707865u;
    ScopedState[1] = 0x3320646eu;
    ScopedState[2] = 0x79622d32u;
    ScopedState[3] = 0x6b206574u;
    for (std::uint32_t ScopedI = 0; ScopedI < 8; ++ScopedI)
    {
        ScopedState[4 + ScopedI] = Load32Le(InKey.bytes.data() + ScopedI * 4);
    }
    ScopedState[12] = InCounter;
    ScopedState[13] = Load32Le(InNonce.bytes.data());
    ScopedState[14] = Load32Le(InNonce.bytes.data() + 4);
    ScopedState[15] = Load32Le(InNonce.bytes.data() + 8);

    std::uint32_t ScopedWorking[16]{};
    std::memcpy(ScopedWorking, ScopedState, sizeof(ScopedWorking));
    for (std::uint32_t ScopedRound = 0; ScopedRound < 10; ++ScopedRound)
    {
        ChaChaQuarterRound(ScopedWorking[0], ScopedWorking[4], ScopedWorking[8], ScopedWorking[12]);
        ChaChaQuarterRound(ScopedWorking[1], ScopedWorking[5], ScopedWorking[9], ScopedWorking[13]);
        ChaChaQuarterRound(ScopedWorking[2], ScopedWorking[6], ScopedWorking[10], ScopedWorking[14]);
        ChaChaQuarterRound(ScopedWorking[3], ScopedWorking[7], ScopedWorking[11], ScopedWorking[15]);
        ChaChaQuarterRound(ScopedWorking[0], ScopedWorking[5], ScopedWorking[10], ScopedWorking[15]);
        ChaChaQuarterRound(ScopedWorking[1], ScopedWorking[6], ScopedWorking[11], ScopedWorking[12]);
        ChaChaQuarterRound(ScopedWorking[2], ScopedWorking[7], ScopedWorking[8], ScopedWorking[13]);
        ChaChaQuarterRound(ScopedWorking[3], ScopedWorking[4], ScopedWorking[9], ScopedWorking[14]);
    }
    for (std::uint32_t ScopedI = 0; ScopedI < 16; ++ScopedI)
    {
        Store32Le(OutBlock + ScopedI * 4, ScopedWorking[ScopedI] + ScopedState[ScopedI]);
    }
}

// Poly1305 (26-bit limbs, constant-time arithmetic, no lookup tables)
struct Poly1305State
{
    std::uint32_t r0 = 0, r1 = 0, r2 = 0, r3 = 0, r4 = 0;
    std::uint32_t s1 = 0, s2 = 0, s3 = 0, s4 = 0;
    std::uint32_t h0 = 0, h1 = 0, h2 = 0, h3 = 0, h4 = 0;
    std::uint32_t pad0 = 0, pad1 = 0, pad2 = 0, pad3 = 0;
};

void PolyInit(Poly1305State& InState, const std::byte* InKey) noexcept
{
    InState.r0 = Load32Le(InKey + 0) & 0x3ffffffu;
    InState.r1 = (Load32Le(InKey + 3) >> 2) & 0x3ffff03u;
    InState.r2 = (Load32Le(InKey + 6) >> 4) & 0x3ffc0ffu;
    InState.r3 = (Load32Le(InKey + 9) >> 6) & 0x3f03fffu;
    InState.r4 = (Load32Le(InKey + 12) >> 8) & 0x00fffffu;
    InState.s1 = InState.r1 * 5u;
    InState.s2 = InState.r2 * 5u;
    InState.s3 = InState.r3 * 5u;
    InState.s4 = InState.r4 * 5u;
    InState.pad0 = Load32Le(InKey + 16);
    InState.pad1 = Load32Le(InKey + 20);
    InState.pad2 = Load32Le(InKey + 24);
    InState.pad3 = Load32Le(InKey + 28);
}

void PolyBlock(Poly1305State& InState, const std::byte* InBlock, std::uint32_t InHighBit) noexcept
{
    InState.h0 += Load32Le(InBlock + 0) & 0x3ffffffu;
    InState.h1 += (Load32Le(InBlock + 3) >> 2) & 0x3ffffffu;
    InState.h2 += (Load32Le(InBlock + 6) >> 4) & 0x3ffffffu;
    InState.h3 += (Load32Le(InBlock + 9) >> 6) & 0x3ffffffu;
    InState.h4 += (Load32Le(InBlock + 12) >> 8) | InHighBit;

    const std::uint64_t ScopedD0 = static_cast<std::uint64_t>(InState.h0) * InState.r0 +
                                   static_cast<std::uint64_t>(InState.h1) * InState.s4 +
                                   static_cast<std::uint64_t>(InState.h2) * InState.s3 +
                                   static_cast<std::uint64_t>(InState.h3) * InState.s2 +
                                   static_cast<std::uint64_t>(InState.h4) * InState.s1;
    const std::uint64_t ScopedD1 = static_cast<std::uint64_t>(InState.h0) * InState.r1 +
                                   static_cast<std::uint64_t>(InState.h1) * InState.r0 +
                                   static_cast<std::uint64_t>(InState.h2) * InState.s4 +
                                   static_cast<std::uint64_t>(InState.h3) * InState.s3 +
                                   static_cast<std::uint64_t>(InState.h4) * InState.s2;
    const std::uint64_t ScopedD2 = static_cast<std::uint64_t>(InState.h0) * InState.r2 +
                                   static_cast<std::uint64_t>(InState.h1) * InState.r1 +
                                   static_cast<std::uint64_t>(InState.h2) * InState.r0 +
                                   static_cast<std::uint64_t>(InState.h3) * InState.s4 +
                                   static_cast<std::uint64_t>(InState.h4) * InState.s3;
    const std::uint64_t ScopedD3 = static_cast<std::uint64_t>(InState.h0) * InState.r3 +
                                   static_cast<std::uint64_t>(InState.h1) * InState.r2 +
                                   static_cast<std::uint64_t>(InState.h2) * InState.r1 +
                                   static_cast<std::uint64_t>(InState.h3) * InState.r0 +
                                   static_cast<std::uint64_t>(InState.h4) * InState.s4;
    const std::uint64_t ScopedD4 = static_cast<std::uint64_t>(InState.h0) * InState.r4 +
                                   static_cast<std::uint64_t>(InState.h1) * InState.r3 +
                                   static_cast<std::uint64_t>(InState.h2) * InState.r2 +
                                   static_cast<std::uint64_t>(InState.h3) * InState.r1 +
                                   static_cast<std::uint64_t>(InState.h4) * InState.r0;

    std::uint32_t ScopedCarry = static_cast<std::uint32_t>(ScopedD0 >> 26);
    InState.h0 = static_cast<std::uint32_t>(ScopedD0) & 0x3ffffffu;
    const std::uint64_t ScopedD1Carry = ScopedD1 + ScopedCarry;
    ScopedCarry = static_cast<std::uint32_t>(ScopedD1Carry >> 26);
    InState.h1 = static_cast<std::uint32_t>(ScopedD1Carry) & 0x3ffffffu;
    const std::uint64_t ScopedD2Carry = ScopedD2 + ScopedCarry;
    ScopedCarry = static_cast<std::uint32_t>(ScopedD2Carry >> 26);
    InState.h2 = static_cast<std::uint32_t>(ScopedD2Carry) & 0x3ffffffu;
    const std::uint64_t ScopedD3Carry = ScopedD3 + ScopedCarry;
    ScopedCarry = static_cast<std::uint32_t>(ScopedD3Carry >> 26);
    InState.h3 = static_cast<std::uint32_t>(ScopedD3Carry) & 0x3ffffffu;
    const std::uint64_t ScopedD4Carry = ScopedD4 + ScopedCarry;
    ScopedCarry = static_cast<std::uint32_t>(ScopedD4Carry >> 26);
    InState.h4 = static_cast<std::uint32_t>(ScopedD4Carry) & 0x3ffffffu;
    InState.h0 += ScopedCarry * 5u;
    ScopedCarry = InState.h0 >> 26;
    InState.h0 &= 0x3ffffffu;
    InState.h1 += ScopedCarry;
}

void PolyFinish(Poly1305State& InState, std::byte* OutTag) noexcept
{
    std::uint32_t ScopedCarry = InState.h1 >> 26;
    InState.h1 &= 0x3ffffffu;
    InState.h2 += ScopedCarry;
    ScopedCarry = InState.h2 >> 26;
    InState.h2 &= 0x3ffffffu;
    InState.h3 += ScopedCarry;
    ScopedCarry = InState.h3 >> 26;
    InState.h3 &= 0x3ffffffu;
    InState.h4 += ScopedCarry;
    ScopedCarry = InState.h4 >> 26;
    InState.h4 &= 0x3ffffffu;
    InState.h0 += ScopedCarry * 5u;
    ScopedCarry = InState.h0 >> 26;
    InState.h0 &= 0x3ffffffu;
    InState.h1 += ScopedCarry;

    std::uint32_t ScopedG0 = InState.h0 + 5u;
    ScopedCarry = ScopedG0 >> 26;
    ScopedG0 &= 0x3ffffffu;
    std::uint32_t ScopedG1 = InState.h1 + ScopedCarry;
    ScopedCarry = ScopedG1 >> 26;
    ScopedG1 &= 0x3ffffffu;
    std::uint32_t ScopedG2 = InState.h2 + ScopedCarry;
    ScopedCarry = ScopedG2 >> 26;
    ScopedG2 &= 0x3ffffffu;
    std::uint32_t ScopedG3 = InState.h3 + ScopedCarry;
    ScopedCarry = ScopedG3 >> 26;
    ScopedG3 &= 0x3ffffffu;
    std::uint32_t ScopedG4 = InState.h4 + ScopedCarry - (1u << 26);

    std::uint32_t ScopedMask = (ScopedG4 >> 31) - 1u;
    ScopedG0 &= ScopedMask;
    ScopedG1 &= ScopedMask;
    ScopedG2 &= ScopedMask;
    ScopedG3 &= ScopedMask;
    ScopedG4 &= ScopedMask;
    ScopedMask = ~ScopedMask;
    InState.h0 = (InState.h0 & ScopedMask) | ScopedG0;
    InState.h1 = (InState.h1 & ScopedMask) | ScopedG1;
    InState.h2 = (InState.h2 & ScopedMask) | ScopedG2;
    InState.h3 = (InState.h3 & ScopedMask) | ScopedG3;
    InState.h4 = (InState.h4 & ScopedMask) | ScopedG4;

    std::uint32_t ScopedWord0 = (InState.h0 | (InState.h1 << 26)) + InState.pad0;
    std::uint32_t ScopedWord1 = ((InState.h1 >> 6) | (InState.h2 << 20)) + InState.pad1 +
                                (ScopedWord0 < InState.pad0 ? 1u : 0u);
    const std::uint32_t ScopedWord1Carry = ScopedWord1 < ((InState.h1 >> 6) | (InState.h2 << 20)) ? 1u : 0u;
    std::uint32_t ScopedWord2 = ((InState.h2 >> 12) | (InState.h3 << 14)) + InState.pad2 + ScopedWord1Carry;
    const std::uint32_t ScopedWord2Carry = ScopedWord2 < ((InState.h2 >> 12) | (InState.h3 << 14)) ? 1u : 0u;
    const std::uint32_t ScopedWord3 = ((InState.h3 >> 18) | (InState.h4 << 8)) + InState.pad3 + ScopedWord2Carry;
    Store32Le(OutTag + 0, ScopedWord0);
    Store32Le(OutTag + 4, ScopedWord1);
    Store32Le(OutTag + 8, ScopedWord2);
    Store32Le(OutTag + 12, ScopedWord3);
}

void PolyUpdate(Poly1305State& InState, std::span<const std::byte> InMessage) noexcept
{
    std::size_t ScopedOffset = 0;
    while (InMessage.size() - ScopedOffset >= 16)
    {
        PolyBlock(InState, InMessage.data() + ScopedOffset, 1u << 24);
        ScopedOffset += 16;
    }
    if (ScopedOffset < InMessage.size())
    {
        std::byte ScopedTail[16]{};
        const std::size_t ScopedRemaining = InMessage.size() - ScopedOffset;
        std::memcpy(ScopedTail, InMessage.data() + ScopedOffset, ScopedRemaining);
        ScopedTail[ScopedRemaining] = std::byte{1};
        PolyBlock(InState, ScopedTail, 0);
    }
}

// AEAD pads AAD and ciphertext with zero bytes to 16, then MACs a big-endian
void PolyAeadBytes(Poly1305State& InState, std::span<const std::byte> InBytes) noexcept
{
    std::size_t ScopedOffset = 0;
    while (InBytes.size() - ScopedOffset >= 16)
    {
        PolyBlock(InState, InBytes.data() + ScopedOffset, 1u << 24);
        ScopedOffset += 16;
    }
    if (ScopedOffset < InBytes.size())
    {
        std::byte ScopedTail[16]{};
        std::memcpy(ScopedTail, InBytes.data() + ScopedOffset, InBytes.size() - ScopedOffset);
        PolyBlock(InState, ScopedTail, 1u << 24);
    }
}

void ChaChaPolyTag(const AeadKey& InKey, const AeadNonce& InNonce, std::span<const std::byte> InAad,
                   std::span<const std::byte> InCiphertext, std::span<std::byte, 16> OutTag) noexcept
{
    std::byte ScopedPolyKey[64]{};
    ChaChaBlock(InKey, InNonce, 0, ScopedPolyKey);
    Poly1305State ScopedState{};
    PolyInit(ScopedState, ScopedPolyKey);
    PolyAeadBytes(ScopedState, InAad);
    PolyAeadBytes(ScopedState, InCiphertext);
    std::byte ScopedLengths[16]{};
    Store64Le(ScopedLengths, static_cast<std::uint64_t>(InAad.size()));
    Store64Le(ScopedLengths + 8, static_cast<std::uint64_t>(InCiphertext.size()));
    PolyAeadBytes(ScopedState, std::span<const std::byte>(ScopedLengths, 16));
    PolyFinish(ScopedState, OutTag.data());
}

[[nodiscard]] bool ValidateBuffers(std::span<const std::byte> InPlaintext, std::span<std::byte> OutCiphertext) noexcept
{
    return OutCiphertext.size() >= InPlaintext.size();
}
}

void AeadKey::Clear() noexcept
{
    SecureZero(bytes);
}

std::uint64_t AeadNonce::Counter() const noexcept
{
    return Load64Le(bytes.data() + 4);
}

void AeadNonce::SetCounter(std::uint64_t InCounter) noexcept
{
    // The first four nonce bytes stay free for a per-archive random prefix; the
    bytes[0] = std::byte{0};
    bytes[1] = std::byte{0};
    bytes[2] = std::byte{0};
    bytes[3] = std::byte{0};
    Store64Le(bytes.data() + 4, InCounter);
}

bool AeadTag::Equals(const AeadTag& InOther) const noexcept
{
    return ConstantTimeEquals(bytes, InOther.bytes);
}

bool ConstantTimeEquals(std::span<const std::byte> InLeft, std::span<const std::byte> InRight) noexcept
{
    if (InLeft.size() != InRight.size())
    {
        return false;
    }
    std::uint8_t ScopedDifference = 0;
    for (std::size_t ScopedI = 0; ScopedI < InLeft.size(); ++ScopedI)
    {
        ScopedDifference |= static_cast<std::uint8_t>(InLeft[ScopedI]) ^ static_cast<std::uint8_t>(InRight[ScopedI]);
    }
    return ScopedDifference == 0;
}

bool Aes256GcmEncrypt(const AeadKey& InKey, const AeadNonce& InNonce, std::span<const std::byte> InAad,
                      std::span<const std::byte> InPlaintext, std::span<std::byte> OutCiphertext,
                      AeadTag& OutTag) noexcept
{
    if (!ValidateBuffers(InPlaintext, OutCiphertext))
    {
        return false;
    }
    Aes256Key ScopedKey{};
    AesExpandKey(InKey.bytes.data(), ScopedKey);

    std::byte ScopedJ0[16]{};
    AesGcmJ0(InNonce, ScopedJ0);
    std::byte ScopedFirstCounter[16]{};
    std::memcpy(ScopedFirstCounter, ScopedJ0, 16);
    for (int ScopedI = 15; ScopedI >= 12; --ScopedI)
    {
        ScopedFirstCounter[ScopedI] = static_cast<std::byte>(static_cast<std::uint8_t>(ScopedFirstCounter[ScopedI]) + 1u);
        if (ScopedFirstCounter[ScopedI] != std::byte{0})
        {
            break;
        }
    }
    AesGcmCtr(ScopedKey, ScopedFirstCounter, InPlaintext, OutCiphertext);
    AesGcmTag(ScopedKey, ScopedJ0, InAad, OutCiphertext.first(InPlaintext.size()), OutTag.bytes);
    SecureZero(std::as_writable_bytes(std::span(ScopedKey.roundKeys)));
    return true;
}

bool Aes256GcmDecrypt(const AeadKey& InKey, const AeadNonce& InNonce, std::span<const std::byte> InAad,
                      std::span<const std::byte> InCiphertext, std::span<std::byte> OutPlaintext,
                      const AeadTag& InTag) noexcept
{
    if (!ValidateBuffers(InCiphertext, OutPlaintext))
    {
        return false;
    }
    Aes256Key ScopedKey{};
    AesExpandKey(InKey.bytes.data(), ScopedKey);

    std::byte ScopedJ0[16]{};
    AesGcmJ0(InNonce, ScopedJ0);
    AeadTag ScopedExpected{};
    AesGcmTag(ScopedKey, ScopedJ0, InAad, InCiphertext, ScopedExpected.bytes);
    if (!ConstantTimeEquals(ScopedExpected.bytes, InTag.bytes))
    {
        SecureZero(std::as_writable_bytes(std::span(ScopedKey.roundKeys)));
        return false;
    }
    std::byte ScopedFirstCounter[16]{};
    std::memcpy(ScopedFirstCounter, ScopedJ0, 16);
    for (int ScopedI = 15; ScopedI >= 12; --ScopedI)
    {
        ScopedFirstCounter[ScopedI] = static_cast<std::byte>(static_cast<std::uint8_t>(ScopedFirstCounter[ScopedI]) + 1u);
        if (ScopedFirstCounter[ScopedI] != std::byte{0})
        {
            break;
        }
    }
    AesGcmCtr(ScopedKey, ScopedFirstCounter, InCiphertext, OutPlaintext);
    SecureZero(std::as_writable_bytes(std::span(ScopedKey.roundKeys)));
    return true;
}

bool ChaCha20Xor(const AeadKey& InKey, const AeadNonce& InNonce, std::uint32_t InCounter,
                 std::span<const std::byte> InInput, std::span<std::byte> OutOutput) noexcept
{
    if (OutOutput.size() < InInput.size())
    {
        return false;
    }
    std::byte ScopedKeyStream[64]{};
    std::size_t ScopedOffset = 0;
    std::uint32_t ScopedCounter = InCounter;
    while (ScopedOffset < InInput.size())
    {
        ChaChaBlock(InKey, InNonce, ScopedCounter, ScopedKeyStream);
        const std::size_t ScopedCount = std::min<std::size_t>(64, InInput.size() - ScopedOffset);
        for (std::size_t ScopedI = 0; ScopedI < ScopedCount; ++ScopedI)
        {
            OutOutput[ScopedOffset + ScopedI] =
                static_cast<std::byte>(InInput[ScopedOffset + ScopedI] ^ ScopedKeyStream[ScopedI]);
        }
        ++ScopedCounter;
        ScopedOffset += ScopedCount;
    }
    SecureZero(std::span<std::byte>(ScopedKeyStream, sizeof(ScopedKeyStream)));
    return true;
}

bool ChaCha20Poly1305Encrypt(const AeadKey& InKey, const AeadNonce& InNonce, std::span<const std::byte> InAad,
                             std::span<const std::byte> InPlaintext, std::span<std::byte> OutCiphertext,
                             AeadTag& OutTag) noexcept
{
    if (!ValidateBuffers(InPlaintext, OutCiphertext))
    {
        return false;
    }
    if (!ChaCha20Xor(InKey, InNonce, 1, InPlaintext, OutCiphertext))
    {
        return false;
    }
    ChaChaPolyTag(InKey, InNonce, InAad, OutCiphertext.first(InPlaintext.size()), OutTag.bytes);
    return true;
}

bool ChaCha20Poly1305Decrypt(const AeadKey& InKey, const AeadNonce& InNonce, std::span<const std::byte> InAad,
                             std::span<const std::byte> InCiphertext, std::span<std::byte> OutPlaintext,
                             const AeadTag& InTag) noexcept
{
    if (!ValidateBuffers(InCiphertext, OutPlaintext))
    {
        return false;
    }
    AeadTag ScopedExpected{};
    ChaChaPolyTag(InKey, InNonce, InAad, InCiphertext, ScopedExpected.bytes);
    if (!ConstantTimeEquals(ScopedExpected.bytes, InTag.bytes))
    {
        return false;
    }
    return ChaCha20Xor(InKey, InNonce, 1, InCiphertext, OutPlaintext);
}

bool Poly1305Mac(const AeadKey& InKey, std::span<const std::byte> InMessage,
                 std::span<std::byte, kAeadTagBytes> OutTag) noexcept
{
    Poly1305State ScopedState{};
    PolyInit(ScopedState, InKey.bytes.data());
    PolyUpdate(ScopedState, InMessage);
    PolyFinish(ScopedState, OutTag.data());
    return true;
}

std::uint64_t Crc64Ecma(std::span<const std::byte> InBytes, std::uint64_t InSeed) noexcept
{
    constexpr std::uint64_t kPolynomial = 0xC96C5795D7870F42ull;
    std::uint64_t ScopedCrc = ~InSeed;
    for (const std::byte ScopedByte : InBytes)
    {
        ScopedCrc ^= static_cast<std::uint64_t>(ScopedByte);
        for (int ScopedBit = 0; ScopedBit < 8; ++ScopedBit)
        {
            const std::uint64_t ScopedMask = (ScopedCrc & 1ull) ? 0xFFFFFFFFFFFFFFFFull : 0ull;
            ScopedCrc = (ScopedCrc >> 1) ^ (kPolynomial & ScopedMask);
        }
    }
    return ~ScopedCrc;
}

bool HardwareAesAvailable() noexcept
{
    return FBY_CRYPTO_HAS_AESNI != 0;
}

void SecureZero(std::span<std::byte> InBytes) noexcept
{
    volatile std::byte* ScopedPointer = InBytes.data();
    for (std::size_t ScopedI = 0; ScopedI < InBytes.size(); ++ScopedI)
    {
        ScopedPointer[ScopedI] = std::byte{0};
    }
}
}
