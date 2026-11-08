// FemboyEngine2D - mmap-friendly cooked book asset.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>

namespace Fby::Text
{

inline constexpr char kBookMagic[8] = {'F', 'B', 'Y', 'B', 'O', 'O', 'K', '1'};
inline constexpr std::uint32_t kBookVersion = 1;
inline constexpr std::uint32_t kBookHeaderBytes = 112;
inline constexpr std::uint32_t kBookBlockBytes = 32;
inline constexpr std::uint32_t kBookInvalidIndex = 0xFFFFFFFFu;

// The block kind is semantic, not a font size: theme mapping decides how a
// Heading becomes a style. Flags carry the heading level for Heading blocks.
enum class BookBlockType : std::uint8_t
{
    Paragraph = 0,
    Heading = 1,
    ListItem = 2,
    Quote = 3,
    Code = 4,
    Table = 5,
    Image = 6,
    PageBreak = 7,
    Callout = 8,
};

struct BookHeader
{
    char Magic[8] = {'F', 'B', 'Y', 'B', 'O', 'O', 'K', '1'};
    std::uint32_t Version = kBookVersion;
    std::uint32_t Flags = 0;
    std::uint32_t BlockCount = 0;
    std::uint32_t SpanCount = 0;
    std::uint32_t StyleCount = 0;
    std::uint32_t InlineCount = 0;
    std::uint32_t AnchorCount = 0;
    std::uint32_t Reserved = 0;
    std::uint64_t TextOffset = 0;
    std::uint64_t TextBytes = 0;
    std::uint64_t BlockTableOffset = 0;
    std::uint64_t SpanTableOffset = 0;
    std::uint64_t StyleTableOffset = 0;
    std::uint64_t InlineTableOffset = 0;
    std::uint64_t AnchorTableOffset = 0;
    std::uint64_t MetadataOffset = 0;
    std::uint64_t MetadataBytes = 0;
};

static_assert(sizeof(BookHeader) == kBookHeaderBytes);
static_assert(std::is_trivially_copyable_v<BookHeader>);

struct BookBlockRecord
{
    BookBlockType Type = BookBlockType::Paragraph;
    std::uint8_t Flags = 0;
    std::uint16_t Reserved = 0;
    std::uint32_t TextOffset = 0;
    std::uint32_t TextLength = 0;
    std::uint32_t SpanFirst = 0;
    std::uint32_t SpanCount = 0;
    std::uint32_t InlineFirst = 0;
    std::uint32_t InlineCount = 0;
    std::uint32_t AnchorIndex = kBookInvalidIndex;
};

static_assert(sizeof(BookBlockRecord) == kBookBlockBytes);

// Ranges are relative to the owning block's UTF-8 text.
struct BookSpan
{
    std::uint32_t TextBegin = 0;
    std::uint32_t TextEnd = 0;
    std::uint32_t StyleIndex = 0;
};

static_assert(sizeof(BookSpan) == 12);

struct BookStyle
{
    std::uint16_t FontId = 0;
    std::uint16_t Flags = 0;
    std::uint16_t Size = 0;
    std::uint16_t Reserved = 0;
    std::uint32_t Color = 0;
};

static_assert(sizeof(BookStyle) == 12);

struct BookInline
{
    std::uint32_t ByteOffset = 0;
    std::uint32_t AssetId = 0;
    std::uint16_t Width = 0;
    std::uint16_t Height = 0;
};

static_assert(sizeof(BookInline) == 12);

struct BookAnchor
{
    std::uint32_t BlockIndex = 0;
    std::uint32_t ByteOffset = 0;
    std::uint32_t NameOffset = 0;
    std::uint32_t NameLength = 0;
};

static_assert(sizeof(BookAnchor) == 16);

struct BookBlockView
{
    BookBlockType Type = BookBlockType::Paragraph;
    std::uint8_t Flags = 0;
    std::span<const char> Text{};
    std::span<const BookSpan> Spans{};
    std::span<const BookInline> Inlines{};
    std::uint32_t AnchorIndex = kBookInvalidIndex;
};

struct BookAssetView
{
    BookHeader Header{};
    std::span<const std::byte> Bytes{};
    std::span<const char> Text{};
    std::span<const BookBlockRecord> Blocks{};
    std::span<const BookSpan> Spans{};
    std::span<const BookStyle> Styles{};
    std::span<const BookInline> Inlines{};
    std::span<const BookAnchor> Anchors{};
    std::span<const std::byte> Metadata{};
};

enum class BookAssetError : std::uint8_t
{
    Ok = 0,
    Truncated,
    BadMagic,
    BadVersion,
    BadOffset,
    BadTable,
    BadBlock,
    BadUtf8,
};

[[nodiscard]] const char* BookAssetErrorName(BookAssetError InError) noexcept;

// Validates the header, tables, UTF-8 arena and every block range. The view
// points into InBytes and stays valid only while those bytes live.
[[nodiscard]] BookAssetError OpenBookAsset(std::span<const std::byte> InBytes, BookAssetView* OutView,
                                           std::string* InError = nullptr) noexcept;

class BookReader
{
public:
    BookReader() = default;

    [[nodiscard]] BookAssetError Open(std::span<const std::byte> InBytes, std::string* InError = nullptr) noexcept;
    void Close() noexcept;
    [[nodiscard]] bool IsOpen() const noexcept
    {
        return MBytes.data() != nullptr;
    }
    [[nodiscard]] const BookHeader& Header() const noexcept
    {
        return MView.Header;
    }
    [[nodiscard]] std::uint32_t BlockCount() const noexcept
    {
        return MView.Header.BlockCount;
    }
    [[nodiscard]] std::span<const BookBlockRecord> Blocks() const noexcept
    {
        return MView.Blocks;
    }
    [[nodiscard]] std::span<const char> Text() const noexcept
    {
        return MView.Text;
    }
    [[nodiscard]] std::span<const std::byte> Metadata() const noexcept
    {
        return MView.Metadata;
    }
    [[nodiscard]] const BookAssetView& View() const noexcept
    {
        return MView;
    }

    [[nodiscard]] bool Block(std::uint32_t InIndex, BookBlockView* OutBlock) const noexcept;
    [[nodiscard]] bool Anchor(std::uint32_t InIndex, BookAnchor* OutAnchor, std::string_view* OutName) const noexcept;

private:
    BookAssetView MView{};
    std::span<const std::byte> MBytes{};
};

}
