// FemboyEngine2D - cooked book validation, zero-copy blocks and anchors.
#include "Engine/Text/BookAsset.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
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

template <typename T> std::size_t Append(std::vector<std::byte>& InBytes, const T& InValue)
{
    while (InBytes.size() % alignof(T) != 0)
    {
        InBytes.push_back(std::byte{0});
    }
    const std::size_t ScopedOffset = InBytes.size();
    InBytes.resize(ScopedOffset + sizeof(T));
    std::memcpy(InBytes.data() + ScopedOffset, &InValue, sizeof(T));
    return ScopedOffset;
}

std::vector<std::byte> BuildBook()
{
    std::vector<std::byte> ScopedBytes(Fby::Text::kBookHeaderBytes, std::byte{0});
    Fby::Text::BookHeader ScopedHeader;
    ScopedHeader.BlockCount = 2;
    ScopedHeader.SpanCount = 1;
    ScopedHeader.StyleCount = 1;
    ScopedHeader.InlineCount = 1;
    ScopedHeader.AnchorCount = 1;

    Fby::Text::BookBlockRecord ScopedFirst;
    ScopedFirst.Type = Fby::Text::BookBlockType::Heading;
    ScopedFirst.Flags = 1;
    ScopedFirst.TextOffset = 0;
    ScopedFirst.TextLength = 5;
    ScopedFirst.SpanFirst = 0;
    ScopedFirst.SpanCount = 1;
    ScopedFirst.AnchorIndex = 0;

    Fby::Text::BookBlockRecord ScopedSecond;
    ScopedSecond.Type = Fby::Text::BookBlockType::Paragraph;
    ScopedSecond.TextOffset = 5;
    ScopedSecond.TextLength = 9;
    ScopedSecond.InlineFirst = 0;
    ScopedSecond.InlineCount = 1;

    ScopedHeader.BlockTableOffset = Append(ScopedBytes, ScopedFirst);
    Append(ScopedBytes, ScopedSecond);
    Fby::Text::BookSpan ScopedSpan;
    ScopedSpan.TextBegin = 0;
    ScopedSpan.TextEnd = 5;
    ScopedSpan.StyleIndex = 0;
    ScopedHeader.SpanTableOffset = Append(ScopedBytes, ScopedSpan);
    Fby::Text::BookStyle ScopedStyle;
    ScopedStyle.Size = 16;
    ScopedHeader.StyleTableOffset = Append(ScopedBytes, ScopedStyle);
    Fby::Text::BookInline ScopedInline;
    ScopedInline.ByteOffset = 0;
    ScopedInline.AssetId = 42;
    ScopedInline.Width = 16;
    ScopedInline.Height = 16;
    ScopedHeader.InlineTableOffset = Append(ScopedBytes, ScopedInline);
    Fby::Text::BookAnchor ScopedAnchor;
    ScopedAnchor.NameOffset = 0;
    ScopedAnchor.NameLength = 3;
    ScopedHeader.AnchorTableOffset = Append(ScopedBytes, ScopedAnchor);

    ScopedHeader.TextOffset = ScopedBytes.size();
    const char ScopedText[] = "TitleBody text";
    ScopedBytes.resize(ScopedBytes.size() + sizeof(ScopedText) - 1);
    std::memcpy(ScopedBytes.data() + ScopedHeader.TextOffset, ScopedText, sizeof(ScopedText) - 1);
    ScopedHeader.TextBytes = sizeof(ScopedText) - 1;
    ScopedHeader.MetadataOffset = ScopedBytes.size();
    const char ScopedMetadata[] = "top";
    ScopedBytes.resize(ScopedBytes.size() + sizeof(ScopedMetadata) - 1);
    std::memcpy(ScopedBytes.data() + ScopedHeader.MetadataOffset, ScopedMetadata, sizeof(ScopedMetadata) - 1);
    ScopedHeader.MetadataBytes = sizeof(ScopedMetadata) - 1;
    std::memcpy(ScopedBytes.data(), &ScopedHeader, sizeof(ScopedHeader));
    return ScopedBytes;
}
}

int main()
{
    using namespace Fby::Text;
    const std::vector<std::byte> ScopedBytes = BuildBook();
    BookReader ScopedReader;
    std::string ScopedError;
    FBY_CHECK(ScopedReader.Open(ScopedBytes, &ScopedError) == BookAssetError::Ok);
    FBY_CHECK(ScopedError.empty());
    FBY_CHECK(ScopedReader.IsOpen());
    FBY_CHECK(ScopedReader.BlockCount() == 2);
    FBY_CHECK(ScopedReader.Header().TextBytes == 14);
    FBY_CHECK(ScopedReader.Text().size() == 14);

    BookBlockView ScopedBlock;
    FBY_CHECK(ScopedReader.Block(0, &ScopedBlock));
    FBY_CHECK(ScopedBlock.Type == BookBlockType::Heading);
    FBY_CHECK(ScopedBlock.Flags == 1);
    FBY_CHECK(ScopedBlock.Text.size() == 5);
    FBY_CHECK(std::string_view(ScopedBlock.Text.data(), ScopedBlock.Text.size()) == "Title");
    FBY_CHECK(ScopedBlock.Spans.size() == 1);
    FBY_CHECK(ScopedBlock.Spans[0].TextEnd == 5);
    FBY_CHECK(ScopedBlock.AnchorIndex == 0);

    FBY_CHECK(ScopedReader.Block(1, &ScopedBlock));
    FBY_CHECK(ScopedBlock.Type == BookBlockType::Paragraph);
    FBY_CHECK(std::string_view(ScopedBlock.Text.data(), ScopedBlock.Text.size()) == "Body text");
    FBY_CHECK(ScopedBlock.Inlines.size() == 1);
    FBY_CHECK(ScopedBlock.Inlines[0].AssetId == 42);

    BookAnchor ScopedAnchor;
    std::string_view ScopedName;
    FBY_CHECK(ScopedReader.Anchor(0, &ScopedAnchor, &ScopedName));
    FBY_CHECK(ScopedName == "top");
    FBY_CHECK(ScopedAnchor.BlockIndex == 0);

    std::vector<std::byte> ScopedBad = ScopedBytes;
    ScopedBad[0] = std::byte{'X'};
    BookReader ScopedBadReader;
    FBY_CHECK(ScopedBadReader.Open(ScopedBad, &ScopedError) == BookAssetError::BadMagic);

    ScopedBad = ScopedBytes;
    FBY_CHECK(ScopedBadReader.Open(std::span<const std::byte>(ScopedBad.data(), 10), &ScopedError) ==
              BookAssetError::Truncated);

    ScopedBad = ScopedBytes;
    FBY_CHECK(ScopedBadReader.Open(std::span<const std::byte>(ScopedBad.data(), 0), &ScopedError) ==
              BookAssetError::Truncated);

    // A block range outside the text arena.
    ScopedBad = ScopedBytes;
    BookBlockRecord ScopedBadBlock;
    std::memcpy(&ScopedBadBlock, ScopedBad.data() + ScopedReader.Header().BlockTableOffset, sizeof(ScopedBadBlock));
    ScopedBadBlock.TextOffset = 999;
    std::memcpy(ScopedBad.data() + ScopedReader.Header().BlockTableOffset, &ScopedBadBlock, sizeof(ScopedBadBlock));
    FBY_CHECK(ScopedBadReader.Open(ScopedBad, &ScopedError) == BookAssetError::BadBlock);

    // Invalid UTF-8 in the arena.
    ScopedBad = ScopedBytes;
    ScopedBad[ScopedReader.Header().TextOffset] = std::byte{0xFF};
    FBY_CHECK(ScopedBadReader.Open(ScopedBad, &ScopedError) == BookAssetError::BadUtf8);

    FBY_CHECK(std::string_view(BookAssetErrorName(BookAssetError::Ok)) == "Ok");
    FBY_CHECK(std::string_view(BookAssetErrorName(BookAssetError::BadBlock)) == "BadBlock");

    if (GFailures == 0)
    {
        std::printf("book_asset: OK (blocks, spans, inlines, anchors, invalid inputs)\n");
        return 0;
    }
    std::fprintf(stderr, "book_asset: %u failure(s)\n", GFailures);
    return 1;
}
