// FemboyEngine2D - minimal PNG writer tests.
#include "Engine/Assets/PngWriter.hpp"

#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <vector>

namespace
{
int GFailures = 0;

void Check(bool InCondition, const char* InMessage)
{
    if (!InCondition)
    {
        std::printf("FAIL: %s\n", InMessage);
        ++GFailures;
    }
}

std::uint32_t ReadBigEndian(const std::vector<std::byte>& InBytes, std::size_t InOffset)
{
    return (static_cast<std::uint32_t>(InBytes[InOffset]) << 24u) |
           (static_cast<std::uint32_t>(InBytes[InOffset + 1]) << 16u) |
           (static_cast<std::uint32_t>(InBytes[InOffset + 2]) << 8u) |
           static_cast<std::uint32_t>(InBytes[InOffset + 3]);
}

void TestEncode()
{
    std::vector<std::uint8_t> ScopedPixels(2u * 2u * 4u);
    for (std::size_t ScopedI = 0; ScopedI < ScopedPixels.size(); ++ScopedI)
    {
        ScopedPixels[ScopedI] = static_cast<std::uint8_t>(ScopedI * 7u);
    }

    std::vector<std::byte> ScopedPng;
    Check(Fby::Assets::EncodePng(2, 2, std::span<const std::uint8_t>(ScopedPixels.data(), ScopedPixels.size()), &ScopedPng), "encode accepts a valid RGBA8 image");
    Check(ScopedPng.size() > 8, "encoded PNG is non-empty");
    static constexpr std::array<std::uint8_t, 8> ScopedSignature{0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    bool ScopedSignatureOk = true;
    for (std::size_t ScopedI = 0; ScopedI < ScopedSignature.size(); ++ScopedI)
    {
        if (static_cast<std::uint8_t>(ScopedPng[ScopedI]) != ScopedSignature[ScopedI])
        {
            ScopedSignatureOk = false;
        }
    }
    Check(ScopedSignatureOk, "PNG signature is present");
    Check(ScopedPng.size() >= 24 && ReadBigEndian(ScopedPng, 16) == 2 && ReadBigEndian(ScopedPng, 20) == 2,
          "IHDR carries the image dimensions");
    Check(static_cast<std::uint8_t>(ScopedPng[24]) == 8 && static_cast<std::uint8_t>(ScopedPng[25]) == 6,
          "IHDR describes 8-bit RGBA");

    std::vector<std::byte> ScopedInvalid;
    Check(!Fby::Assets::EncodePng(2, 2, std::span<const std::uint8_t>(ScopedPixels.data(), 15), &ScopedInvalid),
          "encode rejects a short pixel span");
    Check(!Fby::Assets::EncodePng(0, 2, std::span<const std::uint8_t>(ScopedPixels.data(), ScopedPixels.size()), &ScopedInvalid), "encode rejects zero width");
}

void TestFile()
{
    std::vector<std::uint8_t> ScopedPixels(1u * 1u * 4u, 0x80u);
    const std::filesystem::path ScopedPath =
        std::filesystem::temp_directory_path() / "fby_png_writer_test.png";
    Check(Fby::Assets::WritePng(ScopedPath, 1, 1, std::span<const std::uint8_t>(ScopedPixels.data(), ScopedPixels.size())), "write stores a PNG file");

    std::ifstream ScopedInput(ScopedPath, std::ios::binary);
    std::array<char, 8> ScopedHeader{};
    ScopedInput.read(ScopedHeader.data(), static_cast<std::streamsize>(ScopedHeader.size()));
    Check(ScopedInput.gcount() == static_cast<std::streamsize>(ScopedHeader.size()),
          "written file is readable and non-empty");
    Check(static_cast<std::uint8_t>(ScopedHeader[0]) == 0x89 && ScopedHeader[1] == 'P',
          "written file starts with the PNG signature");

    std::error_code ScopedCode;
    std::filesystem::remove(ScopedPath, ScopedCode);
}
}

int main()
{
    TestEncode();
    TestFile();
    if (GFailures != 0)
    {
        std::printf("%d PNG writer test failure(s)\n", GFailures);
        return 1;
    }
    std::printf("PNG writer tests passed\n");
    return 0;
}
