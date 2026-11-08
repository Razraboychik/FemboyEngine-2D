// FemboyEngine2D - paged, streaming book session.
#pragma once

#include "Engine/Text/BookAsset.hpp"
#include "Engine/Text/TextLayout.hpp"
#include "Engine/Text/TextRender.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Fby::Text
{

struct BookViewport
{
    float Width = 640.0f;
    float Height = 480.0f;
};

struct BookPage
{
    std::uint32_t FirstBlock = 0;
    std::uint32_t BlockCount = 0;
    std::uint32_t FirstSegment = 0;
    std::uint32_t SegmentCount = 0;
    float Height = 0.0f;
    bool Overflow = false;
};

struct BookPageSegment
{
    std::uint32_t Block = 0;
    std::uint32_t FirstLine = 0;
    std::uint32_t LineCount = 0;
    float Top = 0.0f;
};

// A page index is a fixed array of block ranges; the source text stays in the
// BookReader's mapped bytes. Only layout results for the block currently being
// measured/rendered are materialized.
class BookSession
{
public:
    static constexpr std::uint32_t kMaxPages = 4096;
    static constexpr std::uint32_t kMaxPageBlocks = 64;
    static constexpr std::uint32_t kMaxPageSegments = 16384;

    BookSession() = default;
    BookSession(const BookSession&) = delete;
    BookSession& operator=(const BookSession&) = delete;

    [[nodiscard]] bool Open(std::span<const std::byte> InBytes, const BookViewport& InViewport,
                            const TextLayoutParams& InLayout, std::string* InError = nullptr);
    void Close() noexcept;

    [[nodiscard]] bool IsOpen() const noexcept;
    [[nodiscard]] const BookReader& Reader() const noexcept
    {
        return MReader;
    }
    [[nodiscard]] const BookViewport& Viewport() const noexcept
    {
        return MViewport;
    }
    [[nodiscard]] std::uint32_t PageCount() const noexcept
    {
        return MPageCount;
    }
    [[nodiscard]] std::uint32_t CurrentPage() const noexcept
    {
        return MCurrentPage;
    }
    [[nodiscard]] bool Page(std::uint32_t InIndex, BookPage* OutPage) const noexcept;
    [[nodiscard]] bool SetPage(std::uint32_t InIndex) noexcept;
    [[nodiscard]] bool NextPage() noexcept;
    [[nodiscard]] bool PreviousPage() noexcept;
    [[nodiscard]] bool GoToAnchor(std::uint32_t InAnchorIndex) noexcept;

    // Re-lays out only the blocks of InPage and extracts glyph items. The
    // caller owns the destination render-item array.
    [[nodiscard]] std::uint32_t RenderPage(std::uint32_t InPage, Vector2D InOrigin,
                                           const TextRenderParams& InRenderParams,
                                           std::span<Rhi::RenderItem> OutItems,
                                           TextRenderStats* OutStats = nullptr) noexcept;

private:
    [[nodiscard]] bool BuildPages(std::string* InError);
    [[nodiscard]] bool LayoutBlock(std::uint32_t InBlock, BookBlockView* OutBlock);
    void PrepareBlockStyle(const BookBlockView& InBlock);
    void PrepareSliceStyle(const BookBlockView& InBlock, std::uint32_t InBegin, std::uint32_t InEnd);

    BookReader MReader{};
    BookViewport MViewport{};
    TextLayoutParams MLayout{};
    std::unique_ptr<TextLayoutResult> MWorkingLayout;
    std::array<BookPage, kMaxPages> MPages{};
    std::array<BookPageSegment, kMaxPageSegments> MSegments{};
    std::uint32_t MPageCount = 0;
    std::uint32_t MSegmentCount = 0;
    std::uint32_t MCurrentPage = 0;
    std::vector<TextSpan> MSpans;
    std::vector<TextStyle> MStyles;
    bool MOpened = false;
};

}
