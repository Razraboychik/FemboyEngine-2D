// FemboyEngine2D - paged book session, navigation and per-page render.
#include "Engine/Text/BookCook.hpp"
#include "Engine/Text/BookSession.hpp"

#include <array>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace
{
std::uint32_t GFailures = 0;

#define FBY_CHECK(condition)                                                                                           \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);                                  \
            ++GFailures;                                                                                               \
        }                                                                                                              \
    } while (false)
}

constexpr std::string_view kSource = R"FBDOC(# Guide

First paragraph with enough words to wrap across a few short lines.

Second paragraph continues the page.

\pagebreak

Second page starts here.

Third paragraph on the second page.
)FBDOC";

int main()
{
    std::vector<std::byte> ScopedBook;
    std::string ScopedError;
    FBY_CHECK(Fby::Text::CookBook(kSource, &ScopedBook, &ScopedError) == Fby::Text::BookCookError::Ok);
    FBY_CHECK(!ScopedBook.empty());

    Fby::Text::TextLayoutParams ScopedLayout;
    ScopedLayout.MaxWidth = 80.0f;
    ScopedLayout.Advance = 8.0f;
    Fby::Text::BookViewport ScopedViewport;
    ScopedViewport.Width = 80.0f;
    ScopedViewport.Height = 30.0f;
    Fby::Text::BookSession ScopedSession;
    FBY_CHECK(ScopedSession.Open(ScopedBook, ScopedViewport, ScopedLayout, &ScopedError));
    FBY_CHECK(ScopedSession.IsOpen());
    FBY_CHECK(ScopedSession.PageCount() >= 2);
    FBY_CHECK(ScopedSession.PageCount() <= Fby::Text::BookSession::kMaxPages);
    FBY_CHECK(ScopedSession.CurrentPage() == 0);

    Fby::Text::BookPage ScopedPage;
    FBY_CHECK(ScopedSession.Page(0, &ScopedPage));
    FBY_CHECK(ScopedPage.BlockCount > 0);
    FBY_CHECK(ScopedPage.FirstBlock == 0);
    FBY_CHECK(ScopedSession.Page(1, &ScopedPage));
    FBY_CHECK(ScopedPage.BlockCount > 0);
    FBY_CHECK(ScopedPage.FirstBlock > 0);
    FBY_CHECK(!ScopedSession.Page(ScopedSession.PageCount(), &ScopedPage));

    FBY_CHECK(ScopedSession.NextPage());
    FBY_CHECK(ScopedSession.CurrentPage() == 1);
    FBY_CHECK(ScopedSession.SetPage(ScopedSession.PageCount() - 1));
    FBY_CHECK(!ScopedSession.NextPage());
    FBY_CHECK(ScopedSession.PreviousPage());
    FBY_CHECK(ScopedSession.SetPage(0));
    FBY_CHECK(!ScopedSession.PreviousPage());
    FBY_CHECK(!ScopedSession.SetPage(ScopedSession.PageCount()));
    FBY_CHECK(ScopedSession.GoToAnchor(0));
    FBY_CHECK(ScopedSession.CurrentPage() == 0);
    FBY_CHECK(!ScopedSession.GoToAnchor(99));

    Fby::Text::TextRenderParams ScopedRender;
    std::array<Fby::Rhi::RenderItem, 128> ScopedItems{};
    Fby::Text::TextRenderStats ScopedStats;
    const std::uint32_t ScopedRendered =
        ScopedSession.RenderPage(1, Fby::Vector2D{0.0f, 0.0f}, ScopedRender, ScopedItems, &ScopedStats);
    FBY_CHECK(ScopedRendered <= ScopedItems.size());
    FBY_CHECK(ScopedStats.Items == ScopedRendered);

    // A tall paragraph must split across pages instead of overflowing one page.
    const std::string_view ScopedLong =
        "one two three four five six seven eight nine ten eleven twelve thirteen fourteen fifteen sixteen "
        "seventeen eighteen nineteen twenty twenty-one twenty-two twenty-three twenty-four";
    std::vector<std::byte> ScopedLongBook;
    FBY_CHECK(Fby::Text::CookBook(ScopedLong, &ScopedLongBook, &ScopedError) == Fby::Text::BookCookError::Ok);
    Fby::Text::BookViewport ScopedTallViewport;
    ScopedTallViewport.Width = 40.0f;
    ScopedTallViewport.Height = 8.0f;
    Fby::Text::BookSession ScopedTallSession;
    FBY_CHECK(ScopedTallSession.Open(ScopedLongBook, ScopedTallViewport, ScopedLayout, &ScopedError));
    FBY_CHECK(ScopedTallSession.PageCount() > 1);
    Fby::Text::BookPage ScopedTallFirst;
    Fby::Text::BookPage ScopedTallSecond;
    FBY_CHECK(ScopedTallSession.Page(0, &ScopedTallFirst));
    FBY_CHECK(ScopedTallSession.Page(1, &ScopedTallSecond));
    FBY_CHECK(ScopedTallFirst.FirstBlock == 0 && ScopedTallSecond.FirstBlock == 0);
    FBY_CHECK(ScopedTallFirst.SegmentCount == 1);

    // A single line taller than the viewport is clipped and flagged, never
    // allowed to make the page geometry taller than the viewport.
    Fby::Text::TextLayoutParams ScopedGiantLayout = ScopedLayout;
    ScopedGiantLayout.FontSize = 32.0f;
    Fby::Text::BookSession ScopedGiantSession;
    FBY_CHECK(ScopedGiantSession.Open(ScopedLongBook, ScopedTallViewport, ScopedGiantLayout, &ScopedError));
    Fby::Text::BookPage ScopedGiantPage;
    FBY_CHECK(ScopedGiantSession.Page(0, &ScopedGiantPage));
    FBY_CHECK(ScopedGiantPage.Overflow);
    FBY_CHECK(ScopedGiantPage.Height <= ScopedTallViewport.Height);

    Fby::Text::BookViewport ScopedBadViewport;
    ScopedBadViewport.Width = 0.0f;
    Fby::Text::BookSession ScopedBadSession;
    FBY_CHECK(!ScopedBadSession.Open(ScopedBook, ScopedBadViewport, ScopedLayout, &ScopedError));
    FBY_CHECK(!ScopedBadSession.IsOpen());

    ScopedSession.Close();
    FBY_CHECK(!ScopedSession.IsOpen());

    if (GFailures == 0)
    {
        std::printf("book_session: OK (pages, navigation, render, invalid viewport)\n");
        return 0;
    }
    std::fprintf(stderr, "book_session: %u failure(s)\n", GFailures);
    return 1;
}
