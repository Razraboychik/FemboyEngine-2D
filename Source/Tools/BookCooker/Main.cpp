// FemboyEngine2D - source document to .fbybook cooker.
#include "Engine/Text/BookAsset.hpp"
#include "Engine/Text/BookCook.hpp"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace
{
void PrintUsage()
{
    std::printf("fby_bookcook - cook a Markdown .fbdoc into .fbybook\n"
                "  --input <file.fbdoc>   source Markdown with optional TOML front matter\n"
                "  --output <file.fbybook> cooked mmap-friendly book\n"
                "  --help                  show this help\n");
}
}

int main(int InCount, char** InArgs)
{
    std::string ScopedInput;
    std::string ScopedOutput;
    for (int ScopedIndex = 1; ScopedIndex < InCount; ++ScopedIndex)
    {
        const std::string ScopedArgument = InArgs[ScopedIndex];
        if (ScopedArgument == "--help")
        {
            PrintUsage();
            return 0;
        }
        if (ScopedIndex + 1 >= InCount)
        {
            PrintUsage();
            return 1;
        }
        if (ScopedArgument == "--input")
        {
            ScopedInput = InArgs[++ScopedIndex];
        }
        else if (ScopedArgument == "--output")
        {
            ScopedOutput = InArgs[++ScopedIndex];
        }
        else
        {
            PrintUsage();
            return 1;
        }
    }
    if (ScopedInput.empty() || ScopedOutput.empty())
    {
        PrintUsage();
        return 1;
    }

    std::ifstream ScopedSource(ScopedInput, std::ios::binary);
    if (!ScopedSource)
    {
        std::fprintf(stderr, "fby_bookcook: cannot open %s\n", ScopedInput.c_str());
        return 1;
    }
    const std::string ScopedText((std::istreambuf_iterator<char>(ScopedSource)), std::istreambuf_iterator<char>());
    std::vector<std::byte> ScopedBook;
    std::string ScopedError;
    const Fby::Text::BookCookError ScopedCook =
        Fby::Text::CookBook(ScopedText, &ScopedBook, &ScopedError);
    if (ScopedCook != Fby::Text::BookCookError::Ok)
    {
        std::fprintf(stderr, "fby_bookcook: %s: %s\n", Fby::Text::BookCookErrorName(ScopedCook),
                     ScopedError.c_str());
        return 1;
    }
    Fby::Text::BookAssetView ScopedView;
    if (Fby::Text::OpenBookAsset(ScopedBook, &ScopedView, &ScopedError) != Fby::Text::BookAssetError::Ok)
    {
        std::fprintf(stderr, "fby_bookcook: cooked book is invalid: %s\n", ScopedError.c_str());
        return 1;
    }

    std::ofstream ScopedFile(ScopedOutput, std::ios::binary | std::ios::trunc);
    if (!ScopedFile)
    {
        std::fprintf(stderr, "fby_bookcook: cannot write %s\n", ScopedOutput.c_str());
        return 1;
    }
    ScopedFile.write(reinterpret_cast<const char*>(ScopedBook.data()), static_cast<std::streamsize>(ScopedBook.size()));
    if (!ScopedFile)
    {
        std::fprintf(stderr, "fby_bookcook: write failed\n");
        return 1;
    }
    std::printf("fby_bookcook: %u block(s), %zu bytes\n", ScopedView.Header.BlockCount, ScopedBook.size());
    return 0;
}
