// FemboyEngine2D - localization asset tool.
#include "Engine/Assets/LocalizationCompiler.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace
{
std::string ReadText(const char* InPath)
{
    std::ifstream ScopedStream(InPath, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(ScopedStream), std::istreambuf_iterator<char>());
}

std::vector<std::byte> ReadBinary(const char* InPath)
{
    std::ifstream ScopedStream(InPath, std::ios::binary);
    const std::string ScopedData((std::istreambuf_iterator<char>(ScopedStream)), std::istreambuf_iterator<char>());
    std::vector<std::byte> ScopedBytes(ScopedData.size());
    std::memcpy(ScopedBytes.data(), ScopedData.data(), ScopedData.size());
    return ScopedBytes;
}

bool WriteBinary(const char* InPath, std::span<const std::byte> InData)
{
    std::ofstream ScopedStream(InPath, std::ios::binary);
    ScopedStream.write(reinterpret_cast<const char*>(InData.data()), static_cast<std::streamsize>(InData.size()));
    return static_cast<bool>(ScopedStream);
}

bool WriteText(const char* InPath, std::string_view InText)
{
    std::ofstream ScopedStream(InPath, std::ios::binary);
    ScopedStream.write(InText.data(), static_cast<std::streamsize>(InText.size()));
    return static_cast<bool>(ScopedStream);
}

int Usage()
{
    std::fprintf(stderr, "usage: fby_loc_tool compile|decompile|roundtrip <input> [output]\n");
    return 2;
}
}

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        return Usage();
    }

    const std::string ScopedCommand = argv[1];
    const std::string ScopedInput = argv[2];

    if (ScopedCommand == "compile")
    {
        if (argc < 4)
        {
            return Usage();
        }
        std::string ScopedError;
        const std::vector<std::byte> ScopedPacked =
            Fby::Assets::CompileLocalization(ReadText(ScopedInput.c_str()), &ScopedError);
        if (ScopedPacked.empty())
        {
            std::fprintf(stderr, "compile failed: %s\n", ScopedError.c_str());
            return 1;
        }
        if (!WriteBinary(argv[3], ScopedPacked))
        {
            std::fprintf(stderr, "cannot write %s\n", argv[3]);
            return 1;
        }
        std::printf("compiled %s -> %s (%zu bytes)\n", ScopedInput.c_str(), argv[3], ScopedPacked.size());
        return 0;
    }

    if (ScopedCommand == "decompile")
    {
        if (argc < 4)
        {
            return Usage();
        }
        std::string ScopedError;
        const std::string ScopedText =
            Fby::Assets::DecompileLocalization(ReadBinary(ScopedInput.c_str()), &ScopedError);
        if (ScopedText.empty())
        {
            std::fprintf(stderr, "decompile failed: %s\n", ScopedError.c_str());
            return 1;
        }
        if (!WriteText(argv[3], ScopedText))
        {
            std::fprintf(stderr, "cannot write %s\n", argv[3]);
            return 1;
        }
        std::printf("decompiled %s -> %s (%zu bytes)\n", ScopedInput.c_str(), argv[3], ScopedText.size());
        return 0;
    }

    if (ScopedCommand == "roundtrip")
    {
        const std::string ScopedSource = ReadText(ScopedInput.c_str());
        std::string ScopedError;
        const std::vector<std::byte> ScopedFirst = Fby::Assets::CompileLocalization(ScopedSource, &ScopedError);
        if (ScopedFirst.empty())
        {
            std::fprintf(stderr, "compile failed: %s\n", ScopedError.c_str());
            return 1;
        }
        const std::string ScopedText = Fby::Assets::DecompileLocalization(ScopedFirst, &ScopedError);
        if (ScopedText.empty())
        {
            std::fprintf(stderr, "decompile failed: %s\n", ScopedError.c_str());
            return 1;
        }
        const std::vector<std::byte> ScopedSecond = Fby::Assets::CompileLocalization(ScopedText, &ScopedError);
        if (ScopedSecond.empty())
        {
            std::fprintf(stderr, "recompile failed: %s\n", ScopedError.c_str());
            return 1;
        }
        if (ScopedFirst.size() != ScopedSecond.size() ||
            std::memcmp(ScopedFirst.data(), ScopedSecond.data(), ScopedFirst.size()) != 0)
        {
            std::fprintf(stderr, "round-trip mismatch: %zu vs %zu bytes\n", ScopedFirst.size(), ScopedSecond.size());
            return 1;
        }
        std::printf("round-trip ok: %s -> %zu bytes -> text -> %zu bytes (identical)\n", ScopedInput.c_str(),
                    ScopedFirst.size(), ScopedSecond.size());
        return 0;
    }

    return Usage();
}
