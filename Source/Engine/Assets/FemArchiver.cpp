// FemboyEngine2D - .femarc implementation.
#include "Engine/Assets/FemArchiver.hpp"

#if defined(FBY_HAS_LZMA_SDK)
#include <LzmaLib.h>
#endif

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>

namespace Fby::Assets
{
namespace
{

constexpr std::uint32_t kHeaderOffsetVersion = 8;
constexpr std::uint32_t kHeaderOffsetHeaderBytes = 12;
constexpr std::uint32_t kHeaderOffsetFlags = 16;
constexpr std::uint32_t kHeaderOffsetEntryCount = 20;
constexpr std::uint32_t kHeaderOffsetBodyOffset = 24;
constexpr std::uint32_t kHeaderOffsetBodySize = 32;
constexpr std::uint32_t kHeaderOffsetRawSize = 40;
constexpr std::uint32_t kHeaderOffsetPayloadCrc = 48;
constexpr std::uint32_t kHeaderOffsetNonce = 56;
constexpr std::uint32_t kHeaderOffsetTag = 68;
constexpr std::uint32_t kHeaderOffsetReserved = 84;
constexpr std::uint32_t kBodyCrcOffset = 0;

[[nodiscard]] std::uint32_t Load32(const std::byte* InData) noexcept
{
    return static_cast<std::uint32_t>(InData[0]) | (static_cast<std::uint32_t>(InData[1]) << 8) |
           (static_cast<std::uint32_t>(InData[2]) << 16) | (static_cast<std::uint32_t>(InData[3]) << 24);
}

[[nodiscard]] std::uint64_t Load64(const std::byte* InData) noexcept
{
    return static_cast<std::uint64_t>(Load32(InData)) | (static_cast<std::uint64_t>(Load32(InData + 4)) << 32);
}

void Store32(std::byte* OutData, std::uint32_t InValue) noexcept
{
    OutData[0] = static_cast<std::byte>(InValue & 0xFFu);
    OutData[1] = static_cast<std::byte>((InValue >> 8) & 0xFFu);
    OutData[2] = static_cast<std::byte>((InValue >> 16) & 0xFFu);
    OutData[3] = static_cast<std::byte>((InValue >> 24) & 0xFFu);
}

void Store64(std::byte* OutData, std::uint64_t InValue) noexcept
{
    Store32(OutData, static_cast<std::uint32_t>(InValue));
    Store32(OutData + 4, static_cast<std::uint32_t>(InValue >> 32));
}

[[nodiscard]] constexpr std::uint64_t AlignUp(std::uint64_t InValue, std::uint64_t InAlignment) noexcept
{
    return (InValue + InAlignment - 1) & ~(InAlignment - 1);
}

[[nodiscard]] constexpr std::uint32_t CodecBits(ArchiveCodec InCodec) noexcept
{
    return static_cast<std::uint32_t>(InCodec) & 0xFFu;
}

[[nodiscard]] constexpr std::uint32_t CipherBits(ArchiveCipher InCipher) noexcept
{
    return static_cast<std::uint32_t>(InCipher) & 0xFFu;
}

[[nodiscard]] constexpr bool Fits32(std::uint64_t InValue) noexcept
{
    return InValue <= 0xFFFFFFFFull;
}

[[nodiscard]] std::size_t MaxEncodedBytes(std::size_t InRaw, ArchiveCodec InCodec) noexcept
{
    if (InCodec == ArchiveCodec::Store)
    {
        return InRaw;
    }
    if (InCodec == ArchiveCodec::FbyLz)
    {
        return InRaw + InRaw / 128u + 64u;
    }
    return InRaw + InRaw / 3u + 4096u;
}

void WriteEntry(std::span<std::byte> InBody, std::uint32_t InIndex, std::uint32_t InPathOffset,
                std::uint32_t InPathSize, std::uint32_t InDataOffset, std::uint32_t InStoredSize,
                std::uint64_t InRawSize, std::uint64_t InCrc, ArchiveCodec InCodec, std::span<const std::byte> InProps)
{
    std::byte* ScopedEntry = InBody.data() + InIndex * kFemArcEntryBytes;
    std::memset(ScopedEntry, 0, kFemArcEntryBytes);
    Store32(ScopedEntry + 0, InPathOffset);
    Store32(ScopedEntry + 4, InPathSize);
    Store32(ScopedEntry + 8, InDataOffset);
    Store32(ScopedEntry + 12, InStoredSize);
    Store64(ScopedEntry + 16, InRawSize);
    Store64(ScopedEntry + 24, InCrc);
    ScopedEntry[32] = static_cast<std::byte>(CodecBits(InCodec));
    ScopedEntry[33] = static_cast<std::byte>(std::min<std::size_t>(InProps.size(), 5));
    for (std::size_t ScopedI = 0; ScopedI < std::min<std::size_t>(InProps.size(), 5); ++ScopedI)
    {
        ScopedEntry[34 + ScopedI] = InProps[ScopedI];
    }
}

[[nodiscard]] bool ValidPath(std::string_view InPath) noexcept
{
    if (InPath.empty() || InPath.size() > kFemArcMaxPathBytes)
    {
        return false;
    }
    if (InPath.front() == '/' || InPath.front() == '\\')
    {
        return false;
    }
    if (InPath.find("..") != std::string_view::npos)
    {
        return false;
    }
    return InPath.find('\\') == std::string_view::npos && InPath.find('\0') == std::string_view::npos;
}

// FbyLz: a deterministic LZ4-block-compatible codec.
struct LzTable
{
    std::span<std::uint32_t> slots;
};

[[nodiscard]] bool PrepareLzTable(std::span<std::byte> InScratch, LzTable& OutTable) noexcept
{
    if (InScratch.size() < 65536u * sizeof(std::uint32_t))
    {
        return false;
    }
    const auto ScopedAddress = reinterpret_cast<std::uintptr_t>(InScratch.data());
    if ((ScopedAddress % alignof(std::uint32_t)) != 0)
    {
        return false;
    }
    OutTable.slots = {reinterpret_cast<std::uint32_t*>(InScratch.data()), 65536u};
    std::fill(OutTable.slots.begin(), OutTable.slots.end(), 0xFFFFFFFFu);
    return true;
}

[[nodiscard]] bool WriteLengthExtension(std::span<std::byte> InDestination, std::size_t& InOffset,
                                        std::size_t InLength) noexcept
{
    std::size_t ScopedRemaining = InLength - 15u;
    while (ScopedRemaining >= 255u)
    {
        if (InOffset >= InDestination.size())
        {
            return false;
        }
        InDestination[InOffset++] = std::byte{255};
        ScopedRemaining -= 255u;
    }
    if (InOffset >= InDestination.size())
    {
        return false;
    }
    InDestination[InOffset++] = static_cast<std::byte>(ScopedRemaining);
    return true;
}

[[nodiscard]] bool EmitSequence(std::span<std::byte> InDestination, std::size_t& InOffset,
                                std::span<const std::byte> InPlain, std::size_t InLiteralStart,
                                std::size_t InLiteralLength, std::size_t InMatchLength,
                                std::size_t InMatchOffset) noexcept
{
    const std::size_t ScopedToken = InOffset;
    if (InOffset + 1 > InDestination.size())
    {
        return false;
    }
    ++InOffset;
    const std::uint8_t ScopedLiteralNibble = static_cast<std::uint8_t>(InLiteralLength >= 15 ? 15u : InLiteralLength);
    const std::uint8_t ScopedMatchNibble =
        static_cast<std::uint8_t>(InMatchLength == 0 ? 0u : (InMatchLength >= 19 ? 15u : InMatchLength - 4u));
    InDestination[ScopedToken] = static_cast<std::byte>((ScopedLiteralNibble << 4) | ScopedMatchNibble);
    if (InLiteralLength >= 15)
    {
        if (!WriteLengthExtension(InDestination, InOffset, InLiteralLength))
        {
            return false;
        }
    }
    if (InOffset + InLiteralLength > InDestination.size())
    {
        return false;
    }
    std::memcpy(InDestination.data() + InOffset, InPlain.data() + InLiteralStart, InLiteralLength);
    InOffset += InLiteralLength;
    if (InMatchLength != 0)
    {
        if (InOffset + 2 > InDestination.size())
        {
            return false;
        }
        InDestination[InOffset++] = static_cast<std::byte>(InMatchOffset & 0xFFu);
        InDestination[InOffset++] = static_cast<std::byte>((InMatchOffset >> 8) & 0xFFu);
        if (InMatchLength - 4u >= 15u)
        {
            if (!WriteLengthExtension(InDestination, InOffset, InMatchLength - 4u))
            {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] std::uint32_t HashSequence(std::uint32_t InValue) noexcept
{
    return (InValue * 2654435761u) >> 16;
}

[[nodiscard]] std::uint32_t LoadSequence(std::span<const std::byte> InInput, std::size_t InOffset) noexcept
{
    return Load32(InInput.data() + InOffset);
}
}

bool LzmaSdkAvailable() noexcept
{
#if defined(FBY_HAS_LZMA_SDK)
    return true;
#else
    return false;
#endif
}

bool FbyLzCompress(std::span<const std::byte> InPlain, std::span<std::byte> OutCompressed,
                   std::span<std::byte> InScratch, std::size_t* OutBytes) noexcept
{
    if (OutBytes == nullptr || OutCompressed.empty())
    {
        return false;
    }
    if (InPlain.empty())
    {
        *OutBytes = 0;
        return true;
    }
    if (InPlain.size() < 13)
    {
        if (OutCompressed.size() < InPlain.size() + 1)
        {
            return false;
        }
        OutCompressed[0] = static_cast<std::byte>(InPlain.size() << 4);
        std::memcpy(OutCompressed.data() + 1, InPlain.data(), InPlain.size());
        *OutBytes = InPlain.size() + 1;
        return true;
    }

    LzTable ScopedTable{};
    if (!PrepareLzTable(InScratch, ScopedTable))
    {
        return false;
    }

    std::size_t ScopedInput = 0;
    std::size_t ScopedAnchor = 0;
    std::size_t ScopedOutput = 0;
    while (ScopedInput + 12 < InPlain.size())
    {
        const std::uint32_t ScopedSequence = LoadSequence(InPlain, ScopedInput);
        const std::uint32_t ScopedHash = HashSequence(ScopedSequence) & 0xFFFFu;
        const std::uint32_t ScopedReference = ScopedTable.slots[ScopedHash];
        ScopedTable.slots[ScopedHash] = static_cast<std::uint32_t>(ScopedInput);
        const bool ScopedCandidate = ScopedReference != 0xFFFFFFFFu && ScopedInput > ScopedReference &&
                                     (ScopedInput - ScopedReference) <= 65535u &&
                                     LoadSequence(InPlain, ScopedReference) == ScopedSequence;
        if (!ScopedCandidate)
        {
            ++ScopedInput;
            continue;
        }
        std::size_t ScopedMatchLength = 4;
        while (ScopedInput + ScopedMatchLength < InPlain.size() &&
               InPlain[ScopedReference + ScopedMatchLength] == InPlain[ScopedInput + ScopedMatchLength])
        {
            ++ScopedMatchLength;
        }
        if (!EmitSequence(OutCompressed, ScopedOutput, InPlain, ScopedAnchor, ScopedInput - ScopedAnchor,
                          ScopedMatchLength, ScopedInput - ScopedReference))
        {
            return false;
        }
        ScopedInput += ScopedMatchLength;
        ScopedAnchor = ScopedInput;
    }

    if (!EmitSequence(OutCompressed, ScopedOutput, InPlain, ScopedAnchor, InPlain.size() - ScopedAnchor, 0, 0))
    {
        return false;
    }
    *OutBytes = ScopedOutput;
    return true;
}

bool FbyLzDecompress(std::span<const std::byte> InCompressed, std::span<std::byte> OutPlain) noexcept
{
    if (InCompressed.empty())
    {
        return OutPlain.empty();
    }
    std::size_t ScopedInput = 0;
    std::size_t ScopedOutput = 0;
    while (ScopedInput < InCompressed.size())
    {
        const std::uint8_t ScopedToken = static_cast<std::uint8_t>(InCompressed[ScopedInput++]);
        std::size_t ScopedLiteralLength = ScopedToken >> 4;
        if (ScopedLiteralLength == 15)
        {
            while (true)
            {
                if (ScopedInput >= InCompressed.size())
                {
                    return false;
                }
                const std::uint8_t ScopedByte = static_cast<std::uint8_t>(InCompressed[ScopedInput++]);
                ScopedLiteralLength += ScopedByte;
                if (ScopedByte != 255)
                {
                    break;
                }
            }
        }
        if (ScopedOutput + ScopedLiteralLength > OutPlain.size() ||
            ScopedInput + ScopedLiteralLength > InCompressed.size())
        {
            return false;
        }
        std::memcpy(OutPlain.data() + ScopedOutput, InCompressed.data() + ScopedInput, ScopedLiteralLength);
        ScopedOutput += ScopedLiteralLength;
        ScopedInput += ScopedLiteralLength;
        if (ScopedInput >= InCompressed.size())
        {
            break;
        }
        if (ScopedInput + 2 > InCompressed.size())
        {
            return false;
        }
        const std::size_t ScopedOffset = static_cast<std::size_t>(InCompressed[ScopedInput]) |
                                         (static_cast<std::size_t>(InCompressed[ScopedInput + 1]) << 8);
        ScopedInput += 2;
        if (ScopedOffset == 0 || ScopedOffset > ScopedOutput)
        {
            return false;
        }
        std::size_t ScopedMatchLength = ScopedToken & 0x0Fu;
        if (ScopedMatchLength == 15)
        {
            while (true)
            {
                if (ScopedInput >= InCompressed.size())
                {
                    return false;
                }
                const std::uint8_t ScopedByte = static_cast<std::uint8_t>(InCompressed[ScopedInput++]);
                ScopedMatchLength += ScopedByte;
                if (ScopedByte != 255)
                {
                    break;
                }
            }
        }
        ScopedMatchLength += 4;
        if (ScopedOutput + ScopedMatchLength > OutPlain.size())
        {
            return false;
        }
        for (std::size_t ScopedI = 0; ScopedI < ScopedMatchLength; ++ScopedI)
        {
            OutPlain[ScopedOutput + ScopedI] = OutPlain[ScopedOutput - ScopedOffset + ScopedI];
        }
        ScopedOutput += ScopedMatchLength;
    }
    return ScopedOutput == OutPlain.size();
}

std::size_t FemArchiver::MaxArchiveBytes(std::span<const ArchiveInput> InEntries, ArchiveCodec InCodec) noexcept
{
    std::uint64_t ScopedTotal = kFemArcHeaderBytes + static_cast<std::uint64_t>(InEntries.size()) * kFemArcEntryBytes;
    for (const ArchiveInput& ScopedEntry : InEntries)
    {
        ScopedTotal += ScopedEntry.path.size() + 8u;
        ScopedTotal += MaxEncodedBytes(ScopedEntry.bytes.size(), InCodec) + kFemArcAlignment;
    }
    return static_cast<std::size_t>(ScopedTotal + 4096u);
}

ArchiveStatus FemArchiver::Build(std::span<const ArchiveInput> InEntries, std::span<std::byte> OutArchive,
                                 const ArchiveBuildOptions& InOptions, std::size_t* OutBytes) noexcept
{
    if (OutBytes == nullptr)
    {
        return ArchiveStatus::InvalidArgument;
    }
    *OutBytes = 0;
    if (InEntries.size() > kFemArcMaxEntries)
    {
        return ArchiveStatus::TooManyEntries;
    }
    if (OutArchive.size() < kFemArcHeaderBytes)
    {
        return ArchiveStatus::BufferTooSmall;
    }
    if (InOptions.cipher != ArchiveCipher::None && InOptions.key == nullptr)
    {
        return ArchiveStatus::InvalidArgument;
    }

    std::array<std::uint32_t, kFemArcMaxEntries> ScopedOrder{};
    for (std::uint32_t ScopedI = 0; ScopedI < InEntries.size(); ++ScopedI)
    {
        ScopedOrder[ScopedI] = ScopedI;
        if (!ValidPath(InEntries[ScopedI].path))
        {
            return ArchiveStatus::InvalidArgument;
        }
    }
    // Insertion sort keeps the output deterministic without an allocation and
    for (std::uint32_t ScopedI = 1; ScopedI < InEntries.size(); ++ScopedI)
    {
        const std::uint32_t ScopedValue = ScopedOrder[ScopedI];
        std::uint32_t ScopedJ = ScopedI;
        while (ScopedJ > 0 && InEntries[ScopedOrder[ScopedJ - 1]].path > InEntries[ScopedValue].path)
        {
            ScopedOrder[ScopedJ] = ScopedOrder[ScopedJ - 1];
            --ScopedJ;
        }
        ScopedOrder[ScopedJ] = ScopedValue;
    }
    for (std::uint32_t ScopedI = 1; ScopedI < InEntries.size(); ++ScopedI)
    {
        if (InEntries[ScopedOrder[ScopedI - 1]].path == InEntries[ScopedOrder[ScopedI]].path)
        {
            return ArchiveStatus::InvalidArgument;
        }
    }

    std::uint64_t ScopedStringBytes = 0;
    std::uint64_t ScopedRawBytes = 0;
    for (std::size_t ScopedI = 0; ScopedI < InEntries.size(); ++ScopedI)
    {
        const std::uint32_t ScopedIndex = ScopedOrder[ScopedI];
        ScopedStringBytes += InEntries[ScopedIndex].path.size();
        ScopedRawBytes += InEntries[ScopedIndex].bytes.size();
    }
    const std::uint64_t ScopedTableBytes = static_cast<std::uint64_t>(InEntries.size()) * kFemArcEntryBytes;
    const std::uint64_t ScopedDataStart = AlignUp(ScopedTableBytes + ScopedStringBytes, kFemArcAlignment);
    if (!Fits32(ScopedDataStart) || !Fits32(ScopedStringBytes))
    {
        return ArchiveStatus::BufferTooSmall;
    }

    std::memset(OutArchive.data(), 0, kFemArcHeaderBytes);
    std::memcpy(OutArchive.data(), kFemArcMagic, 8);
    Store32(OutArchive.data() + kHeaderOffsetVersion, kFemArcVersion);
    Store32(OutArchive.data() + kHeaderOffsetHeaderBytes, kFemArcHeaderBytes);
    Store32(OutArchive.data() + kHeaderOffsetFlags,
            CodecBits(InOptions.codec) | (CipherBits(InOptions.cipher) << 8) | ((InOptions.level & 0xFFu) << 16));
    Store32(OutArchive.data() + kHeaderOffsetEntryCount, static_cast<std::uint32_t>(InEntries.size()));
    Store64(OutArchive.data() + kHeaderOffsetBodyOffset, kFemArcHeaderBytes);
    std::memcpy(OutArchive.data() + kHeaderOffsetNonce, InOptions.nonce.bytes.data(), 12);

    std::span<std::byte> ScopedBody = OutArchive.subspan(kFemArcHeaderBytes);
    if (ScopedBody.size() < ScopedDataStart)
    {
        return ArchiveStatus::BufferTooSmall;
    }
    std::memset(ScopedBody.data(), 0, ScopedDataStart);

    std::uint64_t ScopedStringCursor = ScopedTableBytes;
    std::uint64_t ScopedDataCursor = ScopedDataStart;
    std::uint64_t ScopedPayloadCrc = 0;
    for (std::uint32_t ScopedI = 0; ScopedI < InEntries.size(); ++ScopedI)
    {
        const ArchiveInput& ScopedEntry = InEntries[ScopedOrder[ScopedI]];
        const std::size_t ScopedPathSize = ScopedEntry.path.size();
        std::memcpy(ScopedBody.data() + ScopedStringCursor, ScopedEntry.path.data(), ScopedPathSize);

        if (InOptions.progress != nullptr)
        {
            ArchiveProgress ScopedProgress;
            ScopedProgress.stage = ArchiveProgressStage::Compressing;
            ScopedProgress.index = ScopedI + 1;
            ScopedProgress.total = static_cast<std::uint32_t>(InEntries.size());
            ScopedProgress.path = ScopedEntry.path;
            ScopedProgress.rawBytes = ScopedEntry.bytes.size();
            InOptions.progress(InOptions.progressUser, ScopedProgress);
        }

        const ArchiveCodec ScopedRequested = InOptions.codec;
        ArchiveCodec ScopedStoredCodec = ArchiveCodec::Store;
        std::size_t ScopedStoredSize = ScopedEntry.bytes.size();
        std::array<std::byte, 5> ScopedProperties{};
        std::size_t ScopedPropertyBytes = 0;

        if (ScopedRequested != ArchiveCodec::Store && !ScopedEntry.bytes.empty())
        {
            const std::size_t ScopedLimit = MaxEncodedBytes(ScopedEntry.bytes.size(), ScopedRequested);
            if (ScopedDataCursor + ScopedLimit > ScopedBody.size())
            {
                return ArchiveStatus::BufferTooSmall;
            }
            std::span<std::byte> ScopedDestination = ScopedBody.subspan(ScopedDataCursor, ScopedLimit);
            std::size_t ScopedCompressed = 0;
            bool ScopedCompressedOk = false;
            if (ScopedRequested == ArchiveCodec::FbyLz)
            {
                ScopedCompressedOk =
                    FbyLzCompress(ScopedEntry.bytes, ScopedDestination, InOptions.scratch, &ScopedCompressed);
            }
            else if (ScopedRequested == ArchiveCodec::Lzma)
            {
#if defined(FBY_HAS_LZMA_SDK)
                std::size_t ScopedDestinationSize = ScopedDestination.size();
                std::size_t ScopedPropsSize = ScopedProperties.size();
                const int ScopedResult = LzmaCompress(
                    reinterpret_cast<unsigned char*>(ScopedDestination.data()), &ScopedDestinationSize,
                    reinterpret_cast<const unsigned char*>(ScopedEntry.bytes.data()), ScopedEntry.bytes.size(),
                    reinterpret_cast<unsigned char*>(ScopedProperties.data()), &ScopedPropsSize,
                    static_cast<int>(std::min<std::uint32_t>(InOptions.level, 9u)), 1 << 24, 3, 0, 2, 64, 1);
                if (ScopedResult == 0)
                {
                    ScopedCompressed = ScopedDestinationSize;
                    ScopedPropertyBytes = ScopedPropsSize;
                    ScopedCompressedOk = ScopedCompressed < ScopedEntry.bytes.size();
                }
#else
                return ArchiveStatus::CodecUnavailable;
#endif
            }
            if (ScopedCompressedOk && ScopedCompressed < ScopedEntry.bytes.size())
            {
                ScopedStoredCodec = ScopedRequested;
                ScopedStoredSize = ScopedCompressed;
            }
        }
        if (ScopedStoredCodec == ArchiveCodec::Store)
        {
            if (ScopedDataCursor + ScopedEntry.bytes.size() > ScopedBody.size())
            {
                return ArchiveStatus::BufferTooSmall;
            }
            std::memcpy(ScopedBody.data() + ScopedDataCursor, ScopedEntry.bytes.data(), ScopedEntry.bytes.size());
        }
        if (!Fits32(ScopedStringCursor) || !Fits32(ScopedDataCursor) || !Fits32(ScopedStoredSize) ||
            !Fits32(ScopedPathSize))
        {
            return ArchiveStatus::BufferTooSmall;
        }
        if (InOptions.progress != nullptr)
        {
            ArchiveProgress ScopedProgress;
            ScopedProgress.stage = ArchiveProgressStage::Compressing;
            ScopedProgress.index = ScopedI + 1;
            ScopedProgress.total = static_cast<std::uint32_t>(InEntries.size());
            ScopedProgress.path = ScopedEntry.path;
            ScopedProgress.rawBytes = ScopedEntry.bytes.size();
            ScopedProgress.storedBytes = ScopedStoredSize;
            InOptions.progress(InOptions.progressUser, ScopedProgress);
        }
        ScopedPayloadCrc = Crypto::Crc64Ecma(ScopedEntry.bytes, ScopedPayloadCrc);
        WriteEntry(ScopedBody, ScopedI, static_cast<std::uint32_t>(ScopedStringCursor),
                   static_cast<std::uint32_t>(ScopedPathSize), static_cast<std::uint32_t>(ScopedDataCursor),
                   static_cast<std::uint32_t>(ScopedStoredSize), ScopedEntry.bytes.size(),
                   Crypto::Crc64Ecma(ScopedEntry.bytes), ScopedStoredCodec,
                   std::span<const std::byte>(ScopedProperties.data(), ScopedPropertyBytes));
        ScopedStringCursor += ScopedPathSize;
        ScopedDataCursor = AlignUp(ScopedDataCursor + ScopedStoredSize, kFemArcAlignment);
    }

    if (ScopedDataCursor > ScopedBody.size())
    {
        return ArchiveStatus::BufferTooSmall;
    }
    Store64(OutArchive.data() + kHeaderOffsetBodySize, ScopedDataCursor);
    Store64(OutArchive.data() + kHeaderOffsetRawSize, ScopedRawBytes);
    Store64(OutArchive.data() + kHeaderOffsetPayloadCrc, ScopedPayloadCrc);
    Store64(OutArchive.data() + kHeaderOffsetReserved + kBodyCrcOffset,
            Crypto::Crc64Ecma(ScopedBody.first(static_cast<std::size_t>(ScopedDataCursor))));

    if (InOptions.progress != nullptr)
    {
        ArchiveProgress ScopedProgress;
        ScopedProgress.stage = ArchiveProgressStage::Encrypting;
        ScopedProgress.index = static_cast<std::uint32_t>(InEntries.size());
        ScopedProgress.total = static_cast<std::uint32_t>(InEntries.size());
        ScopedProgress.rawBytes = ScopedRawBytes;
        ScopedProgress.storedBytes = ScopedDataCursor;
        InOptions.progress(InOptions.progressUser, ScopedProgress);
    }

    if (InOptions.cipher != ArchiveCipher::None)
    {
        std::span<std::byte> ScopedCipherBody = ScopedBody.first(static_cast<std::size_t>(ScopedDataCursor));
        std::array<std::byte, kFemArcHeaderBytes> ScopedAad{};
        std::memcpy(ScopedAad.data(), OutArchive.data(), kFemArcHeaderBytes);
        Crypto::AeadTag ScopedTag{};
        bool ScopedOk = false;
        if (InOptions.cipher == ArchiveCipher::ChaCha20Poly1305)
        {
            ScopedOk = Crypto::ChaCha20Poly1305Encrypt(*InOptions.key, InOptions.nonce, ScopedAad, ScopedCipherBody,
                                                       ScopedCipherBody, ScopedTag);
        }
        else
        {
            ScopedOk = Crypto::Aes256GcmEncrypt(*InOptions.key, InOptions.nonce, ScopedAad, ScopedCipherBody,
                                                ScopedCipherBody, ScopedTag);
        }
        if (!ScopedOk)
        {
            return ArchiveStatus::InvalidArgument;
        }
        std::memcpy(OutArchive.data() + kHeaderOffsetTag, ScopedTag.bytes.data(), 16);
    }
    if (InOptions.progress != nullptr)
    {
        ArchiveProgress ScopedProgress;
        ScopedProgress.stage = ArchiveProgressStage::Done;
        ScopedProgress.index = static_cast<std::uint32_t>(InEntries.size());
        ScopedProgress.total = static_cast<std::uint32_t>(InEntries.size());
        ScopedProgress.rawBytes = ScopedRawBytes;
        ScopedProgress.storedBytes = ScopedDataCursor;
        InOptions.progress(InOptions.progressUser, ScopedProgress);
    }
    *OutBytes = kFemArcHeaderBytes + static_cast<std::size_t>(ScopedDataCursor);
    return ArchiveStatus::Ok;
}

ArchiveStatus FemArchiver::Open(std::span<const std::byte> InArchive, const Crypto::AeadKey* InKey,
                                std::span<std::byte> InDecryptBuffer, ArchiveReader& OutReader) noexcept
{
    OutReader = ArchiveReader{};
    if (InArchive.size() < kFemArcHeaderBytes)
    {
        return ArchiveStatus::Corrupt;
    }
    if (std::memcmp(InArchive.data(), kFemArcMagic, 8) != 0)
    {
        return ArchiveStatus::Corrupt;
    }
    if (Load32(InArchive.data() + kHeaderOffsetVersion) != kFemArcVersion ||
        Load32(InArchive.data() + kHeaderOffsetHeaderBytes) != kFemArcHeaderBytes)
    {
        return ArchiveStatus::Unsupported;
    }
    const std::uint32_t ScopedFlags = Load32(InArchive.data() + kHeaderOffsetFlags);
    const ArchiveCodec ScopedCodec = static_cast<ArchiveCodec>(ScopedFlags & 0xFFu);
    const ArchiveCipher ScopedCipher = static_cast<ArchiveCipher>((ScopedFlags >> 8) & 0xFFu);
    if (ScopedCodec > ArchiveCodec::FbyLz || ScopedCipher > ArchiveCipher::Aes256Gcm)
    {
        return ArchiveStatus::Unsupported;
    }
    if (ScopedCodec == ArchiveCodec::Lzma && !LzmaSdkAvailable())
    {
        return ArchiveStatus::CodecUnavailable;
    }
    const std::uint64_t ScopedBodyOffset = Load64(InArchive.data() + kHeaderOffsetBodyOffset);
    const std::uint64_t ScopedBodySize = Load64(InArchive.data() + kHeaderOffsetBodySize);
    if (ScopedBodyOffset < kFemArcHeaderBytes || !Fits32(ScopedBodyOffset) || ScopedBodySize > InArchive.size() ||
        ScopedBodyOffset > InArchive.size() || ScopedBodySize > InArchive.size() - ScopedBodyOffset)
    {
        return ArchiveStatus::Corrupt;
    }
    const std::uint32_t ScopedEntryCount = Load32(InArchive.data() + kHeaderOffsetEntryCount);
    if (ScopedEntryCount > kFemArcMaxEntries)
    {
        return ArchiveStatus::TooManyEntries;
    }
    if (ScopedBodySize < static_cast<std::uint64_t>(ScopedEntryCount) * kFemArcEntryBytes)
    {
        return ArchiveStatus::Corrupt;
    }
    std::span<const std::byte> ScopedBody =
        InArchive.subspan(static_cast<std::size_t>(ScopedBodyOffset), static_cast<std::size_t>(ScopedBodySize));
    if (ScopedCipher != ArchiveCipher::None)
    {
        if (InKey == nullptr)
        {
            return ArchiveStatus::InvalidArgument;
        }
        if (InDecryptBuffer.size() < ScopedBodySize)
        {
            return ArchiveStatus::BufferTooSmall;
        }
        std::memcpy(InDecryptBuffer.data(), ScopedBody.data(), static_cast<std::size_t>(ScopedBodySize));
        std::span<std::byte> ScopedPlainBody = InDecryptBuffer.first(static_cast<std::size_t>(ScopedBodySize));
        std::array<std::byte, kFemArcHeaderBytes> ScopedAad{};
        std::memcpy(ScopedAad.data(), InArchive.data(), kFemArcHeaderBytes);
        std::memset(ScopedAad.data() + kHeaderOffsetTag, 0, 16);
        Crypto::AeadTag ScopedStoredTag{};
        std::memcpy(ScopedStoredTag.bytes.data(), InArchive.data() + kHeaderOffsetTag, 16);
        Crypto::AeadNonce ScopedNonce{};
        std::memcpy(ScopedNonce.bytes.data(), InArchive.data() + kHeaderOffsetNonce, 12);
        bool ScopedOk = false;
        if (ScopedCipher == ArchiveCipher::ChaCha20Poly1305)
        {
            ScopedOk = Crypto::ChaCha20Poly1305Decrypt(*InKey, ScopedNonce, ScopedAad, ScopedPlainBody, ScopedPlainBody,
                                                       ScopedStoredTag);
        }
        else
        {
            ScopedOk = Crypto::Aes256GcmDecrypt(*InKey, ScopedNonce, ScopedAad, ScopedPlainBody, ScopedPlainBody,
                                                ScopedStoredTag);
        }
        if (!ScopedOk)
        {
            return ArchiveStatus::AuthenticationFailed;
        }
        ScopedBody = std::span<const std::byte>(InDecryptBuffer.data(), static_cast<std::size_t>(ScopedBodySize));
    }
    else if (std::any_of(InArchive.data() + kHeaderOffsetTag, InArchive.data() + kHeaderOffsetTag + 16,
                         [](std::byte InByte) { return InByte != std::byte{0}; }))
    {
        return ArchiveStatus::Corrupt;
    }

    OutReader.MArchive = InArchive;
    OutReader.MBody = ScopedBody;
    OutReader.MEntryCount = ScopedEntryCount;
    OutReader.MTableOffset = 0;
    OutReader.MStringOffset = ScopedEntryCount * kFemArcEntryBytes;
    OutReader.MDataOffset = 0;
    OutReader.MRawBytes = Load64(InArchive.data() + kHeaderOffsetRawSize);
    OutReader.MCodec = ScopedCodec;
    OutReader.MCipher = ScopedCipher;
    return ArchiveStatus::Ok;
}

ArchiveStatus ArchiveReader::Entry(std::uint32_t InIndex, ArchiveEntryView& OutEntry) const noexcept
{
    OutEntry = ArchiveEntryView{};
    if (InIndex >= MEntryCount)
    {
        return ArchiveStatus::InvalidArgument;
    }
    const std::byte* ScopedRecord = MBody.data() + MTableOffset + static_cast<std::size_t>(InIndex) * kFemArcEntryBytes;
    const std::uint32_t ScopedPathOffset = Load32(ScopedRecord + 0);
    const std::uint32_t ScopedPathSize = Load32(ScopedRecord + 4);
    const std::uint32_t ScopedDataOffset = Load32(ScopedRecord + 8);
    const std::uint32_t ScopedStoredSize = Load32(ScopedRecord + 12);
    const std::uint64_t ScopedRawSize = Load64(ScopedRecord + 16);
    const ArchiveCodec ScopedCodec = static_cast<ArchiveCodec>(static_cast<std::uint8_t>(ScopedRecord[32]));
    const std::uint8_t ScopedPropertyBytes = static_cast<std::uint8_t>(ScopedRecord[33]);
    if (ScopedCodec > ArchiveCodec::FbyLz || ScopedPropertyBytes > 5 || ScopedPathSize > kFemArcMaxPathBytes ||
        ScopedPathOffset < MStringOffset || ScopedPathSize > MBody.size() ||
        ScopedPathOffset > MBody.size() - ScopedPathSize || ScopedDataOffset > MBody.size() ||
        ScopedStoredSize > MBody.size() - ScopedDataOffset)
    {
        return ArchiveStatus::Corrupt;
    }
    // A Store entry is a byte-for-byte copy. Enforce the invariant at the index
    if (ScopedCodec == ArchiveCodec::Store && ScopedStoredSize != ScopedRawSize)
    {
        return ArchiveStatus::Corrupt;
    }
    OutEntry.path = std::string_view(reinterpret_cast<const char*>(MBody.data() + ScopedPathOffset), ScopedPathSize);
    OutEntry.stored = MBody.subspan(ScopedDataOffset, ScopedStoredSize);
    OutEntry.rawSize = ScopedRawSize;
    OutEntry.crc64 = Load64(ScopedRecord + 24);
    OutEntry.codec = ScopedCodec;
    OutEntry.propertyBytes = ScopedPropertyBytes;
    for (std::size_t ScopedI = 0; ScopedI < ScopedPropertyBytes; ++ScopedI)
    {
        OutEntry.properties[ScopedI] = static_cast<std::uint8_t>(ScopedRecord[34 + ScopedI]);
    }
    return ArchiveStatus::Ok;
}

ArchiveStatus ArchiveReader::Extract(std::uint32_t InIndex, std::span<std::byte> OutPlain,
                                     std::span<std::byte> InScratch) const noexcept
{
    (void)InScratch;
    ArchiveEntryView ScopedEntry{};
    const ArchiveStatus ScopedStatus = Entry(InIndex, ScopedEntry);
    if (ScopedStatus != ArchiveStatus::Ok)
    {
        return ScopedStatus;
    }
    if (OutPlain.size() != ScopedEntry.rawSize)
    {
        return ArchiveStatus::BufferTooSmall;
    }
    ArchiveStatus ScopedResult = ArchiveStatus::Ok;
    if (ScopedEntry.codec == ArchiveCodec::Store)
    {
        // a Store codec carries the plain bytes verbatim, so the stored and
        if (ScopedEntry.stored.size() != ScopedEntry.rawSize)
        {
            return ArchiveStatus::Corrupt;
        }
        std::memcpy(OutPlain.data(), ScopedEntry.stored.data(), ScopedEntry.rawSize);
    }
    else if (ScopedEntry.codec == ArchiveCodec::FbyLz)
    {
        if (!FbyLzDecompress(ScopedEntry.stored, OutPlain))
        {
            return ArchiveStatus::Corrupt;
        }
    }
    else if (ScopedEntry.codec == ArchiveCodec::Lzma)
    {
#if defined(FBY_HAS_LZMA_SDK)
        std::size_t ScopedDestinationSize = OutPlain.size();
        std::size_t ScopedSourceSize = ScopedEntry.stored.size();
        const int ScopedLzmaResult =
            LzmaUncompress(reinterpret_cast<unsigned char*>(OutPlain.data()), &ScopedDestinationSize,
                           reinterpret_cast<const unsigned char*>(ScopedEntry.stored.data()), &ScopedSourceSize,
                           reinterpret_cast<const unsigned char*>(ScopedEntry.properties), ScopedEntry.propertyBytes);
        if (ScopedLzmaResult != 0 || ScopedDestinationSize != OutPlain.size())
        {
            return ArchiveStatus::Corrupt;
        }
#else
        return ArchiveStatus::CodecUnavailable;
#endif
    }
    else
    {
        ScopedResult = ArchiveStatus::Unsupported;
    }
    if (ScopedResult == ArchiveStatus::Ok && Crypto::Crc64Ecma(OutPlain) != ScopedEntry.crc64)
    {
        return ArchiveStatus::Corrupt;
    }
    return ScopedResult;
}

std::uint32_t ArchiveReader::Find(std::string_view InPath) const noexcept
{
    ArchiveEntryView ScopedEntry{};
    for (std::uint32_t ScopedI = 0; ScopedI < MEntryCount; ++ScopedI)
    {
        if (Entry(ScopedI, ScopedEntry) == ArchiveStatus::Ok && ScopedEntry.path == InPath)
        {
            return ScopedI;
        }
    }
    return kFemArcMaxEntries;
}
}
