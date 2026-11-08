// FemboyEngine2D - book layout/page benchmark (not a strict perf gate).
#include "Engine/Text/BookCook.hpp"
#include "Engine/Text/BookSession.hpp"

#include <array>
#include <chrono>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace
{
[[nodiscard]] double ElapsedMs(std::chrono::steady_clock::time_point InStart) noexcept
{
    const auto ScopedElapsed = std::chrono::steady_clock::now() - InStart;
    return std::chrono::duration<double, std::milli>(ScopedElapsed).count();
}

std::string BuildSource()
{
    std::string ScopedSource;
    ScopedSource.reserve(256u * 1024u);
    for (std::uint32_t ScopedSection = 0; ScopedSection < 200; ++ScopedSection)
    {
        ScopedSource += "# Section ";
        ScopedSource += std::to_string(ScopedSection);
        ScopedSource += "\n\n";
        for (std::uint32_t ScopedParagraph = 0; ScopedParagraph < 3; ++ScopedParagraph)
        {
            ScopedSource += "This paragraph exists to exercise line wrapping, page splitting and "
                            "the streaming block index without copying the whole book. ";
            ScopedSource += "Numbers: ";
            ScopedSource += std::to_string(ScopedSection * 10 + ScopedParagraph);
            ScopedSource += ".\n\n";
        }
    }
    return ScopedSource;
}
}

int main()
{
    const std::string ScopedSource = BuildSource();
    std::vector<std::byte> ScopedBook;
    std::string ScopedError;
    auto ScopedStart = std::chrono::steady_clock::now();
    if (Fby::Text::CookBook(ScopedSource, &ScopedBook, &ScopedError) != Fby::Text::BookCookError::Ok)
    {
        std::fprintf(stderr, "book_bench: cook failed: %s\n", ScopedError.c_str());
        return 1;
    }
    const double ScopedCookMs = ElapsedMs(ScopedStart);

    Fby::Text::TextLayoutParams ScopedLayout;
    ScopedLayout.MaxWidth = 800.0f;
    ScopedLayout.Advance = 8.0f;
    ScopedLayout.FontSize = 16.0f;
    Fby::Text::BookViewport ScopedViewport;
    ScopedViewport.Width = 800.0f;
    ScopedViewport.Height = 600.0f;
    Fby::Text::BookSession ScopedSession;
    ScopedStart = std::chrono::steady_clock::now();
    if (!ScopedSession.Open(ScopedBook, ScopedViewport, ScopedLayout, &ScopedError))
    {
        std::fprintf(stderr, "book_bench: open failed: %s\n", ScopedError.c_str());
        return 1;
    }
    const double ScopedOpenMs = ElapsedMs(ScopedStart);

    std::array<Fby::Rhi::RenderItem, 2048> ScopedItems{};
    Fby::Text::TextRenderParams ScopedRender;
    const std::uint32_t ScopedTurns = 500;
    std::uint32_t ScopedItemsRead = 0;
    ScopedStart = std::chrono::steady_clock::now();
    for (std::uint32_t ScopedTurn = 0; ScopedTurn < ScopedTurns; ++ScopedTurn)
    {
        const std::uint32_t ScopedPage = ScopedTurn % ScopedSession.PageCount();
        Fby::Text::TextRenderStats ScopedStats;
        ScopedItemsRead += ScopedSession.RenderPage(ScopedPage, Fby::Vector2D{0.0f, 0.0f}, ScopedRender,
                                                    ScopedItems, &ScopedStats);
    }
    const double ScopedPageMs = ElapsedMs(ScopedStart);

    std::printf("book_bench: %zu source bytes, %u blocks, %u pages\n", ScopedSource.size(),
                ScopedSession.Reader().BlockCount(), ScopedSession.PageCount());
    std::printf("book_bench: cook %.2f ms, open %.2f ms, %u page renders %.2f ms (%.3f ms/page), %u items\n",
                ScopedCookMs, ScopedOpenMs, ScopedTurns, ScopedPageMs, ScopedPageMs / ScopedTurns, ScopedItemsRead);
    return ScopedSession.PageCount() > 1 ? 0 : 1;
}
