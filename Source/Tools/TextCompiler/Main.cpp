// FemboyEngine2D - fby_textc, the long-text asset cooker.
#include "Engine/Text/TextAssetFormat.hpp"

#include <charconv>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace
{

struct Options
{
    std::string Input;
    std::string Output;
    std::uint32_t BlockBytes = 4096;
    bool Verify = false;
};

void PrintUsage()
{
    std::printf("fby_textc - compile UTF-8 text into a .fbytext asset\n"
                "  --input <file.txt>     source text (required)\n"
                "  --output <file.fbytext> destination (required)\n"
                "  --block <bytes>        block size (default 4096)\n"
                "  --verify               re-open the cooked asset\n"
                "  --help                 print this text\n");
}

[[nodiscard]] bool ReadFile(const std::string& InPath, std::string* OutText)
{
    std::ifstream ScopedStream(InPath, std::ios::binary);
    if (!ScopedStream)
    {
        std::fprintf(stderr, "fby_textc: cannot open '%s'\n", InPath.c_str());
        return false;
    }
    *OutText = std::string(std::istreambuf_iterator<char>(ScopedStream), std::istreambuf_iterator<char>());
    if (ScopedStream.bad())
    {
        std::fprintf(stderr, "fby_textc: read failed for '%s'\n", InPath.c_str());
        return false;
    }
    return true;
}

[[nodiscard]] bool WriteFile(const std::string& InPath, std::span<const std::byte> InBytes)
{
    std::ofstream ScopedStream(InPath, std::ios::binary | std::ios::trunc);
    if (!ScopedStream)
    {
        std::fprintf(stderr, "fby_textc: cannot write '%s'\n", InPath.c_str());
        return false;
    }
    ScopedStream.write(reinterpret_cast<const char*>(InBytes.data()),
                       static_cast<std::streamsize>(InBytes.size()));
    if (!ScopedStream)
    {
        std::fprintf(stderr, "fby_textc: write failed for '%s'\n", InPath.c_str());
        return false;
    }
    return true;
}

[[nodiscard]] bool ParseOptions(int InArgc, char** InArgv, Options* OutOptions)
{
    for (int ScopedIndex = 1; ScopedIndex < InArgc; ++ScopedIndex)
    {
        const std::string_view ScopedArgument = InArgv[ScopedIndex];
        if (ScopedArgument == "--help" || ScopedArgument == "-h")
        {
            PrintUsage();
            return false;
        }
        if (ScopedArgument == "--verify")
        {
            OutOptions->Verify = true;
            continue;
        }
        if (ScopedIndex + 1 >= InArgc)
        {
            std::fprintf(stderr, "fby_textc: missing value for '%.*s'\n", static_cast<int>(ScopedArgument.size()),
                         ScopedArgument.data());
            return false;
        }
        const std::string_view ScopedValue = InArgv[++ScopedIndex];
        if (ScopedArgument == "--input")
        {
            OutOptions->Input = ScopedValue;
        }
        else if (ScopedArgument == "--output")
        {
            OutOptions->Output = ScopedValue;
        }
        else if (ScopedArgument == "--block")
        {
            std::uint32_t ScopedBlock = 0;
            const auto ScopedResult =
                std::from_chars(ScopedValue.data(), ScopedValue.data() + ScopedValue.size(), ScopedBlock);
            if (ScopedResult.ec != std::errc{} || ScopedResult.ptr != ScopedValue.data() + ScopedValue.size() ||
                ScopedBlock == 0)
            {
                std::fprintf(stderr, "fby_textc: invalid block size\n");
                return false;
            }
            OutOptions->BlockBytes = ScopedBlock;
        }
        else
        {
            std::fprintf(stderr, "fby_textc: unknown option '%.*s'\n", static_cast<int>(ScopedArgument.size()),
                         ScopedArgument.data());
            return false;
        }
    }
    return !OutOptions->Input.empty() && !OutOptions->Output.empty();
}
}

int main(int InArgc, char** InArgv)
{
    Options ScopedOptions;
    if (!ParseOptions(InArgc, InArgv, &ScopedOptions))
    {
        if (InArgc <= 1)
        {
            PrintUsage();
        }
        return 1;
    }

    std::string ScopedText;
    if (!ReadFile(ScopedOptions.Input, &ScopedText))
    {
        return 1;
    }
    std::string ScopedError;
    const std::vector<std::byte> ScopedBytes =
        Fby::Text::BuildTextAsset(ScopedText, ScopedOptions.BlockBytes, &ScopedError);
    if (ScopedBytes.empty())
    {
        std::fprintf(stderr, "fby_textc: %s\n", ScopedError.empty() ? "cook failed" : ScopedError.c_str());
        return 1;
    }
    if (!WriteFile(ScopedOptions.Output, ScopedBytes))
    {
        return 1;
    }

    if (ScopedOptions.Verify)
    {
        Fby::Text::TextAssetView ScopedView;
        if (Fby::Text::OpenTextAsset(ScopedBytes, &ScopedView, &ScopedError) != Fby::Text::TextAssetStatus::Ok)
        {
            std::fprintf(stderr, "fby_textc: verification failed: %s\n", ScopedError.c_str());
            return 1;
        }
        if (ScopedView.Text != ScopedText)
        {
            std::fprintf(stderr, "fby_textc: verification text mismatch\n");
            return 1;
        }
    }

    std::printf("fby_textc: %zu source bytes, %zu cooked bytes -> '%s'\n", ScopedText.size(), ScopedBytes.size(),
                ScopedOptions.Output.c_str());
    return 0;
}
