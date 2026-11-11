// FemboyEngine2D - minimal RGBA8 PNG writer.
#include "Engine/Assets/PngWriter.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <limits>

namespace Fby::Assets
{
namespace
{
constexpr std::array<std::uint8_t, 8> kPngSignature{0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
constexpr std::size_t kMaxStoredBlock = 65535;

std::uint32_t Crc32(std::span<const std::uint8_t> InBytes)
{
    std::uint32_t ScopedCrc = 0xFFFFFFFFu;
    for (const std::uint8_t ScopedByte : InBytes)
    {
        ScopedCrc ^= ScopedByte;
        for (int ScopedBit = 0; ScopedBit < 8; ++ScopedBit)
        {
            const std::uint32_t ScopedMask = 0u - (ScopedCrc & 1u);
            ScopedCrc = (ScopedCrc >> 1u) ^ (0xEDB88320u & ScopedMask);
        }
    }
    return ScopedCrc ^ 0xFFFFFFFFu;
}

std::uint32_t Adler32(std::span<const std::uint8_t> InBytes)
{
    std::uint32_t ScopedA = 1;
    std::uint32_t ScopedB = 0;
    for (const std::uint8_t ScopedByte : InBytes)
    {
        ScopedA = (ScopedA + ScopedByte) % 65521u;
        ScopedB = (ScopedB + ScopedA) % 65521u;
    }
    return (ScopedB << 16u) | ScopedA;
}

void AppendBigEndian(std::vector<std::byte>* InDestination, std::uint32_t InValue)
{
    InDestination->push_back(static_cast<std::byte>((InValue >> 24u) & 0xFFu));
    InDestination->push_back(static_cast<std::byte>((InValue >> 16u) & 0xFFu));
    InDestination->push_back(static_cast<std::byte>((InValue >> 8u) & 0xFFu));
    InDestination->push_back(static_cast<std::byte>(InValue & 0xFFu));
}

void AppendBytes(std::vector<std::byte>* InDestination, std::span<const std::uint8_t> InBytes)
{
    for (const std::uint8_t ScopedByte : InBytes)
    {
        InDestination->push_back(static_cast<std::byte>(ScopedByte));
    }
}

void AppendChunk(std::vector<std::byte>* InDestination, const char (&InType)[5], std::span<const std::uint8_t> InData)
{
    AppendBigEndian(InDestination, static_cast<std::uint32_t>(InData.size()));
    const auto ScopedType = std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(InType), 4);
    AppendBytes(InDestination, ScopedType);
    AppendBytes(InDestination, InData);

    std::vector<std::uint8_t> ScopedCrcInput;
    ScopedCrcInput.reserve(ScopedType.size() + InData.size());
    ScopedCrcInput.insert(ScopedCrcInput.end(), ScopedType.begin(), ScopedType.end());
    ScopedCrcInput.insert(ScopedCrcInput.end(), InData.begin(), InData.end());
    AppendBigEndian(InDestination, Crc32(ScopedCrcInput));
}

void AppendStoredZlib(std::vector<std::uint8_t>* InDestination, std::span<const std::uint8_t> InRaw)
{
    // CMF/FLG: deflate, 32 KiB window, no dictionary, fastest compression.
    InDestination->push_back(0x78);
    InDestination->push_back(0x01);
    std::size_t ScopedOffset = 0;
    do
    {
        const std::size_t ScopedBlock = std::min(kMaxStoredBlock, InRaw.size() - ScopedOffset);
        const bool ScopedFinal = ScopedOffset + ScopedBlock == InRaw.size();
        InDestination->push_back(static_cast<std::uint8_t>(ScopedFinal ? 0x01 : 0x00));
        InDestination->push_back(static_cast<std::uint8_t>(ScopedBlock & 0xFFu));
        InDestination->push_back(static_cast<std::uint8_t>((ScopedBlock >> 8u) & 0xFFu));
        const std::uint16_t ScopedComplement = static_cast<std::uint16_t>(~ScopedBlock);
        InDestination->push_back(static_cast<std::uint8_t>(ScopedComplement & 0xFFu));
        InDestination->push_back(static_cast<std::uint8_t>((ScopedComplement >> 8u) & 0xFFu));
        InDestination->insert(InDestination->end(), InRaw.begin() + static_cast<std::ptrdiff_t>(ScopedOffset),
                      InRaw.begin() + static_cast<std::ptrdiff_t>(ScopedOffset + ScopedBlock));
        ScopedOffset += ScopedBlock;
    } while (ScopedOffset < InRaw.size());

    const std::uint32_t ScopedAdler = Adler32(InRaw);
    InDestination->push_back(static_cast<std::uint8_t>((ScopedAdler >> 24u) & 0xFFu));
    InDestination->push_back(static_cast<std::uint8_t>((ScopedAdler >> 16u) & 0xFFu));
    InDestination->push_back(static_cast<std::uint8_t>((ScopedAdler >> 8u) & 0xFFu));
    InDestination->push_back(static_cast<std::uint8_t>(ScopedAdler & 0xFFu));
}
}

bool EncodePng(std::uint32_t InWidth, std::uint32_t InHeight, std::span<const std::uint8_t> InRgba,
               std::vector<std::byte>* OutPng)
{
    if (OutPng == nullptr || InWidth == 0 || InHeight == 0)
    {
        return false;
    }
    const std::uint64_t ScopedPixels = static_cast<std::uint64_t>(InWidth) * InHeight;
    if (ScopedPixels > std::numeric_limits<std::size_t>::max() / 4u)
    {
        return false;
    }
    const std::size_t ScopedRgbaBytes = static_cast<std::size_t>(ScopedPixels) * 4u;
    if (InRgba.size() != ScopedRgbaBytes)
    {
        return false;
    }
    if (ScopedPixels > std::numeric_limits<std::size_t>::max() - InHeight)
    {
        return false;
    }

    // Each scanline is a filter byte followed by its RGBA texels. Filter 0 is
    // deliberate: it keeps the writer small and the output lossless.
    std::vector<std::uint8_t> ScopedRaw;
    ScopedRaw.reserve(static_cast<std::size_t>(InHeight) * (1u + static_cast<std::size_t>(InWidth) * 4u));
    for (std::uint32_t ScopedY = 0; ScopedY < InHeight; ++ScopedY)
    {
        ScopedRaw.push_back(0);
        const std::size_t ScopedRow = static_cast<std::size_t>(ScopedY) * InWidth * 4u;
        ScopedRaw.insert(ScopedRaw.end(), InRgba.begin() + static_cast<std::ptrdiff_t>(ScopedRow),
                         InRgba.begin() + static_cast<std::ptrdiff_t>(ScopedRow + static_cast<std::size_t>(InWidth) * 4u));
    }

    std::vector<std::uint8_t> ScopedZlib;
    ScopedZlib.reserve(ScopedRaw.size() + ScopedRaw.size() / kMaxStoredBlock * 5 + 16);
    AppendStoredZlib(&ScopedZlib, ScopedRaw);

    std::array<std::uint8_t, 13> ScopedHeader{};
    ScopedHeader[0] = static_cast<std::uint8_t>((InWidth >> 24u) & 0xFFu);
    ScopedHeader[1] = static_cast<std::uint8_t>((InWidth >> 16u) & 0xFFu);
    ScopedHeader[2] = static_cast<std::uint8_t>((InWidth >> 8u) & 0xFFu);
    ScopedHeader[3] = static_cast<std::uint8_t>(InWidth & 0xFFu);
    ScopedHeader[4] = static_cast<std::uint8_t>((InHeight >> 24u) & 0xFFu);
    ScopedHeader[5] = static_cast<std::uint8_t>((InHeight >> 16u) & 0xFFu);
    ScopedHeader[6] = static_cast<std::uint8_t>((InHeight >> 8u) & 0xFFu);
    ScopedHeader[7] = static_cast<std::uint8_t>(InHeight & 0xFFu);
    // Bit depth, RGBA, compression, filter and interlace.
    ScopedHeader[8] = 8;
    ScopedHeader[9] = 6;
    ScopedHeader[10] = 0;
    ScopedHeader[11] = 0;
    ScopedHeader[12] = 0;

    OutPng->clear();
    AppendBytes(OutPng, kPngSignature);
    AppendChunk(OutPng, "IHDR", ScopedHeader);
    AppendChunk(OutPng, "IDAT", ScopedZlib);
    AppendChunk(OutPng, "IEND", {});
    return true;
}

bool WritePng(const std::filesystem::path& InPath, std::uint32_t InWidth, std::uint32_t InHeight,
              std::span<const std::uint8_t> InRgba)
{
    std::vector<std::byte> ScopedPng;
    if (!EncodePng(InWidth, InHeight, InRgba, &ScopedPng))
    {
        return false;
    }
    std::error_code ScopedCode;
    if (!InPath.parent_path().empty())
    {
        std::filesystem::create_directories(InPath.parent_path(), ScopedCode);
    }
    std::ofstream ScopedOutput(InPath, std::ios::binary | std::ios::trunc);
    if (!ScopedOutput)
    {
        return false;
    }
    ScopedOutput.write(reinterpret_cast<const char*>(ScopedPng.data()),
                       static_cast<std::streamsize>(ScopedPng.size()));
    return static_cast<bool>(ScopedOutput);
}

}
