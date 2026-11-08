// FemboyEngine2D - Markdown source to cooked book and back.
#include "Engine/Text/BookAsset.hpp"
#include "Engine/Text/BookCook.hpp"

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

constexpr std::string_view kSource = R"FBDOC(+++
title = "Guide"
locale = "ru"
+++

# Intro

Hello **bold** and `code`.

- item one
- item two

> quote

| A | B |
|---|---|
| 1 | 2 |

```cpp
int x = 1;
```

![Cover](img/cover.png)

\pagebreak
)FBDOC";

int main()
{
    std::vector<std::byte> ScopedBook;
    std::string ScopedError;
    FBY_CHECK(Fby::Text::CookBook(kSource, &ScopedBook, &ScopedError) == Fby::Text::BookCookError::Ok);
    FBY_CHECK(ScopedError.empty());
    FBY_CHECK(!ScopedBook.empty());

    Fby::Text::BookReader ScopedReader;
    FBY_CHECK(ScopedReader.Open(ScopedBook, &ScopedError) == Fby::Text::BookAssetError::Ok);
    FBY_CHECK(ScopedReader.BlockCount() == 9);
    FBY_CHECK(std::string_view(reinterpret_cast<const char*>(ScopedReader.Metadata().data()),
                               ScopedReader.Metadata().size())
                  .find("title = \"Guide\"") != std::string_view::npos);

    Fby::Text::BookBlockView ScopedBlock;
    FBY_CHECK(ScopedReader.Block(0, &ScopedBlock));
    FBY_CHECK(ScopedBlock.Type == Fby::Text::BookBlockType::Heading);
    FBY_CHECK(ScopedBlock.Flags == 1);
    FBY_CHECK(std::string_view(ScopedBlock.Text.data(), ScopedBlock.Text.size()) == "Intro");
    FBY_CHECK(ScopedBlock.AnchorIndex == 0);
    Fby::Text::BookAnchor ScopedAnchor;
    std::string_view ScopedAnchorName;
    FBY_CHECK(ScopedReader.Anchor(0, &ScopedAnchor, &ScopedAnchorName));
    FBY_CHECK(ScopedAnchorName == "intro");
    FBY_CHECK(ScopedAnchor.BlockIndex == 0);

    FBY_CHECK(ScopedReader.Block(1, &ScopedBlock));
    FBY_CHECK(ScopedBlock.Type == Fby::Text::BookBlockType::Paragraph);
    FBY_CHECK(std::string_view(ScopedBlock.Text.data(), ScopedBlock.Text.size()) == "Hello bold and code.");
    FBY_CHECK(ScopedBlock.Spans.size() == 2);

    FBY_CHECK(ScopedReader.Block(2, &ScopedBlock));
    FBY_CHECK(ScopedBlock.Type == Fby::Text::BookBlockType::ListItem);
    FBY_CHECK(ScopedReader.Block(3, &ScopedBlock));
    FBY_CHECK(ScopedBlock.Type == Fby::Text::BookBlockType::ListItem);
    FBY_CHECK(ScopedReader.Block(4, &ScopedBlock));
    FBY_CHECK(ScopedBlock.Type == Fby::Text::BookBlockType::Quote);
    FBY_CHECK(ScopedReader.Block(5, &ScopedBlock));
    FBY_CHECK(ScopedBlock.Type == Fby::Text::BookBlockType::Table);
    FBY_CHECK(ScopedReader.Block(6, &ScopedBlock));
    FBY_CHECK(ScopedBlock.Type == Fby::Text::BookBlockType::Code);
    FBY_CHECK(std::string_view(ScopedBlock.Text.data(), ScopedBlock.Text.size()) == "int x = 1;");
    FBY_CHECK(ScopedReader.Block(7, &ScopedBlock));
    FBY_CHECK(ScopedBlock.Type == Fby::Text::BookBlockType::Image);
    FBY_CHECK(ScopedBlock.Inlines.size() == 1);
    FBY_CHECK(ScopedBlock.Inlines[0].AssetId != 0);
    FBY_CHECK(ScopedReader.Block(8, &ScopedBlock));
    FBY_CHECK(ScopedBlock.Type == Fby::Text::BookBlockType::PageBreak);

    std::vector<std::byte> ScopedBad;
    ScopedError.clear();
    FBY_CHECK(Fby::Text::CookBook("+++\ntitle = \"x\"\n", &ScopedBad, &ScopedError) ==
              Fby::Text::BookCookError::InvalidFrontMatter);
    ScopedError.clear();
    FBY_CHECK(Fby::Text::CookBook("```\nunclosed", &ScopedBad, &ScopedError) ==
              Fby::Text::BookCookError::UnclosedCodeFence);
    ScopedError.clear();
    FBY_CHECK(Fby::Text::CookBook("", &ScopedBad, &ScopedError) == Fby::Text::BookCookError::Empty);

    if (GFailures == 0)
    {
        std::printf("book_cook: OK (front matter, blocks, inline styles, cook errors)\n");
        return 0;
    }
    std::fprintf(stderr, "book_cook: %u failure(s)\n", GFailures);
    return 1;
}
