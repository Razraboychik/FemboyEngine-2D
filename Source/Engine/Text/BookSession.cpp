// FemboyEngine2D - page index, line splitting and per-page text extraction.
#include "Engine/Text/BookSession.hpp"

#include <algorithm>
#include <limits>
#include <string_view>

namespace Fby::Text
{
namespace
{
void SetError(std::string* OutError, const char* InMessage)
{
    if (OutError != nullptr)
    {
        *OutError = InMessage;
    }
}

[[nodiscard]] float LineHeight(const TextLayoutResult& InLayout, std::uint32_t InLine, float InFallback) noexcept
{
    const float ScopedHeight = InLayout.Lines[InLine].Height;
    return ScopedHeight > 0.0f ? ScopedHeight : InFallback;
}
}

bool BookSession::Open(std::span<const std::byte> InBytes, const BookViewport& InViewport,
                       const TextLayoutParams& InLayout, std::string* InError)
{
    Close();
    if (InViewport.Width <= 0.0f || InViewport.Height <= 0.0f)
    {
        SetError(InError, "book session: viewport must be positive");
        return false;
    }
    if (MReader.Open(InBytes, InError) != BookAssetError::Ok)
    {
        return false;
    }
    MViewport = InViewport;
    MLayout = InLayout;
    MLayout.MaxWidth = InViewport.Width;
    MWorkingLayout = std::make_unique<TextLayoutResult>();
    MOpened = true;
    if (!BuildPages(InError))
    {
        Close();
        return false;
    }
    return true;
}

void BookSession::Close() noexcept
{
    MReader.Close();
    MSpans.clear();
    MStyles.clear();
    MWorkingLayout.reset();
    MPageCount = 0;
    MSegmentCount = 0;
    MCurrentPage = 0;
    MOpened = false;
}

bool BookSession::IsOpen() const noexcept
{
    return MOpened && MReader.IsOpen();
}

void BookSession::PrepareBlockStyle(const BookBlockView& InBlock)
{
    MSpans.clear();
    MStyles.clear();
    MSpans.reserve(InBlock.Spans.size());
    MStyles.reserve(MReader.Header().StyleCount);
    for (const BookStyle& ScopedBookStyle : MReader.View().Styles)
    {
        TextStyle ScopedStyle;
        ScopedStyle.Color = Color32{ScopedBookStyle.Color};
        ScopedStyle.FontSize = ScopedBookStyle.Size;
        ScopedStyle.Flags = ScopedBookStyle.Flags;
        MStyles.push_back(ScopedStyle);
    }
    for (const BookSpan& ScopedBookSpan : InBlock.Spans)
    {
        TextSpan ScopedSpan;
        ScopedSpan.Begin = ScopedBookSpan.TextBegin;
        ScopedSpan.End = ScopedBookSpan.TextEnd;
        ScopedSpan.StyleIndex = static_cast<std::uint16_t>(
            std::min<std::uint32_t>(ScopedBookSpan.StyleIndex, std::numeric_limits<std::uint16_t>::max()));
        MSpans.push_back(ScopedSpan);
    }
}

void BookSession::PrepareSliceStyle(const BookBlockView& InBlock, std::uint32_t InBegin, std::uint32_t InEnd)
{
    MSpans.clear();
    MStyles.clear();
    MSpans.reserve(InBlock.Spans.size());
    for (const BookStyle& ScopedBookStyle : MReader.View().Styles)
    {
        TextStyle ScopedStyle;
        ScopedStyle.Color = Color32{ScopedBookStyle.Color};
        ScopedStyle.FontSize = ScopedBookStyle.Size;
        ScopedStyle.Flags = ScopedBookStyle.Flags;
        MStyles.push_back(ScopedStyle);
    }
    for (const BookSpan& ScopedBookSpan : InBlock.Spans)
    {
        const std::uint32_t ScopedBegin = std::max(ScopedBookSpan.TextBegin, InBegin);
        const std::uint32_t ScopedEnd = std::min(ScopedBookSpan.TextEnd, InEnd);
        if (ScopedEnd <= ScopedBegin)
        {
            continue;
        }
        TextSpan ScopedSpan;
        ScopedSpan.Begin = ScopedBegin - InBegin;
        ScopedSpan.End = ScopedEnd - InBegin;
        ScopedSpan.StyleIndex = static_cast<std::uint16_t>(
            std::min<std::uint32_t>(ScopedBookSpan.StyleIndex, std::numeric_limits<std::uint16_t>::max()));
        MSpans.push_back(ScopedSpan);
    }
}

bool BookSession::LayoutBlock(std::uint32_t InBlock, BookBlockView* OutBlock)
{
    if (!MReader.Block(InBlock, OutBlock))
    {
        return false;
    }
    PrepareBlockStyle(*OutBlock);
    TextLayoutParams ScopedParams = MLayout;
    ScopedParams.Spans = MSpans;
    ScopedParams.Styles = MStyles;
    return LayoutText(std::string_view(OutBlock->Text.data(), OutBlock->Text.size()), ScopedParams,
                      MWorkingLayout.get());
}

bool BookSession::BuildPages(std::string* InError)
{
    MPageCount = 0;
    MSegmentCount = 0;
    MCurrentPage = 0;
    BookPage ScopedCurrent;
    std::uint32_t ScopedPageFirstSegment = 0;
    const auto ScopedClosePage = [&]()
    {
        if (ScopedCurrent.SegmentCount == 0)
        {
            return true;
        }
        if (MPageCount >= kMaxPages)
        {
            SetError(InError, "book session: too many pages");
            return false;
        }
        ScopedCurrent.FirstSegment = ScopedPageFirstSegment;
        ScopedCurrent.FirstBlock = MSegments[ScopedPageFirstSegment].Block;
        const BookPageSegment& ScopedLast = MSegments[MSegmentCount - 1];
        ScopedCurrent.BlockCount = ScopedLast.Block - ScopedCurrent.FirstBlock + 1;
        ScopedCurrent.Height = std::min(ScopedCurrent.Height, MViewport.Height);
        MPages[MPageCount++] = ScopedCurrent;
        ScopedCurrent = {};
        ScopedPageFirstSegment = MSegmentCount;
        return true;
    };
    const float ScopedFallbackHeight = MLayout.FontSize > 0.0f ? MLayout.FontSize : 8.0f;

    for (std::uint32_t ScopedBlock = 0; ScopedBlock < MReader.BlockCount(); ++ScopedBlock)
    {
        BookBlockView ScopedView;
        if (!MReader.Block(ScopedBlock, &ScopedView))
        {
            SetError(InError, "book session: block read failed");
            return false;
        }
        if (ScopedView.Type == BookBlockType::PageBreak)
        {
            if (!ScopedClosePage())
            {
                return false;
            }
            continue;
        }
        if (!LayoutBlock(ScopedBlock, &ScopedView) || MWorkingLayout->Count == 0)
        {
            continue;
        }
        std::uint32_t ScopedLine = 0;
        while (ScopedLine < MWorkingLayout->Count)
        {
            if (ScopedCurrent.SegmentCount > 0 &&
                (ScopedCurrent.Height >= MViewport.Height || ScopedCurrent.SegmentCount >= kMaxPageBlocks))
            {
                if (!ScopedClosePage())
                {
                    return false;
                }
            }
            const float ScopedAvailable = std::max(0.0f, MViewport.Height - ScopedCurrent.Height);
            std::uint32_t ScopedLines = 0;
            float ScopedSliceHeight = 0.0f;
            while (ScopedLine + ScopedLines < MWorkingLayout->Count)
            {
                const float ScopedHeight = LineHeight(*MWorkingLayout, ScopedLine + ScopedLines, ScopedFallbackHeight);
                if (ScopedLines > 0 && ScopedSliceHeight + ScopedHeight > ScopedAvailable)
                {
                    break;
                }
                ScopedSliceHeight += ScopedHeight;
                ++ScopedLines;
                if (ScopedSliceHeight > ScopedAvailable)
                {
                    break;
                }
            }
            if (ScopedLines == 0)
            {
                ScopedLines = 1;
            }
            if (MSegmentCount >= kMaxPageSegments)
            {
                SetError(InError, "book session: too many page segments");
                return false;
            }
            BookPageSegment ScopedSegment;
            ScopedSegment.Block = ScopedBlock;
            ScopedSegment.FirstLine = ScopedLine;
            ScopedSegment.LineCount = ScopedLines;
            ScopedSegment.Top = ScopedCurrent.Height;
            MSegments[MSegmentCount++] = ScopedSegment;
            ++ScopedCurrent.SegmentCount;
            if (ScopedSliceHeight > MViewport.Height)
            {
                ScopedCurrent.Overflow = true;
            }
            ScopedCurrent.Height += ScopedSliceHeight;
            ScopedLine += ScopedLines;
            if (ScopedLine < MWorkingLayout->Count)
            {
                if (!ScopedClosePage())
                {
                    return false;
                }
            }
        }
    }
    if (!ScopedClosePage())
    {
        return false;
    }
    if (MPageCount == 0)
    {
        SetError(InError, "book session: no renderable pages");
        return false;
    }
    return true;
}

bool BookSession::Page(std::uint32_t InIndex, BookPage* OutPage) const noexcept
{
    if (!IsOpen() || OutPage == nullptr || InIndex >= MPageCount)
    {
        return false;
    }
    *OutPage = MPages[InIndex];
    return true;
}

bool BookSession::SetPage(std::uint32_t InIndex) noexcept
{
    if (!IsOpen() || InIndex >= MPageCount)
    {
        return false;
    }
    MCurrentPage = InIndex;
    return true;
}

bool BookSession::NextPage() noexcept
{
    return SetPage(MCurrentPage + 1);
}

bool BookSession::PreviousPage() noexcept
{
    if (MCurrentPage == 0)
    {
        return false;
    }
    return SetPage(MCurrentPage - 1);
}

bool BookSession::GoToAnchor(std::uint32_t InAnchorIndex) noexcept
{
    BookAnchor ScopedAnchor;
    if (!MReader.Anchor(InAnchorIndex, &ScopedAnchor, nullptr))
    {
        return false;
    }
    for (std::uint32_t ScopedPage = 0; ScopedPage < MPageCount; ++ScopedPage)
    {
        const BookPage& ScopedBookPage = MPages[ScopedPage];
        if (ScopedAnchor.BlockIndex >= ScopedBookPage.FirstBlock &&
            ScopedAnchor.BlockIndex < ScopedBookPage.FirstBlock + ScopedBookPage.BlockCount)
        {
            return SetPage(ScopedPage);
        }
    }
    return false;
}

std::uint32_t BookSession::RenderPage(std::uint32_t InPage, Vector2D InOrigin, const TextRenderParams& InRenderParams,
                                      std::span<Rhi::RenderItem> OutItems, TextRenderStats* OutStats) noexcept
{
    if (OutStats != nullptr)
    {
        *OutStats = {};
    }
    if (!IsOpen() || InPage >= MPageCount)
    {
        return 0;
    }
    const BookPage& ScopedPage = MPages[InPage];
    std::uint32_t ScopedWritten = 0;
    TextRenderStats ScopedTotal;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedPage.SegmentCount; ++ScopedIndex)
    {
        const BookPageSegment& ScopedSegment = MSegments[ScopedPage.FirstSegment + ScopedIndex];
        BookBlockView ScopedView;
        if (!MReader.Block(ScopedSegment.Block, &ScopedView))
        {
            continue;
        }
        if (!LayoutBlock(ScopedSegment.Block, &ScopedView) ||
            ScopedSegment.FirstLine + ScopedSegment.LineCount > MWorkingLayout->Count)
        {
            continue;
        }
        const TextLine& ScopedFirstLine = MWorkingLayout->Lines[ScopedSegment.FirstLine];
        const TextLine& ScopedLastLine =
            MWorkingLayout->Lines[ScopedSegment.FirstLine + ScopedSegment.LineCount - 1];
        if (ScopedLastLine.End <= ScopedFirstLine.Begin)
        {
            continue;
        }
        const std::uint32_t ScopedBegin = ScopedFirstLine.Begin;
        const std::uint32_t ScopedEnd = ScopedLastLine.End;
        PrepareSliceStyle(ScopedView, ScopedBegin, ScopedEnd);
        TextLayoutParams ScopedLayout = MLayout;
        ScopedLayout.Spans = MSpans;
        ScopedLayout.Styles = MStyles;
        const std::string_view ScopedText(ScopedView.Text.data() + ScopedBegin, ScopedEnd - ScopedBegin);
        if (!LayoutText(ScopedText, ScopedLayout, MWorkingLayout.get()))
        {
            continue;
        }
        TextRenderParams ScopedParams = InRenderParams;
        ScopedParams.Spans = MSpans;
        ScopedParams.Styles = MStyles;
        TextRenderStats ScopedStats;
        const std::uint32_t ScopedItems =
            BuildTextRenderItems(ScopedText, *MWorkingLayout, Vector2D{InOrigin.X, InOrigin.Y + ScopedSegment.Top},
                                 ScopedParams, OutItems.subspan(ScopedWritten), &ScopedStats);
        ScopedWritten += ScopedItems;
        ScopedTotal.Items += ScopedStats.Items;
        ScopedTotal.MissingGlyphs += ScopedStats.MissingGlyphs;
        ScopedTotal.Lines += ScopedStats.Lines;
        ScopedTotal.LayoutOverflow = ScopedTotal.LayoutOverflow || ScopedStats.LayoutOverflow;
    }
    // A line taller than the viewport is still one indivisible line. Keep the
    // page geometry inside the viewport and cull quads that fall outside it; the
    // host can inspect BookPage::Overflow if it wants to show a scroll hint.
    const float ScopedTop = InOrigin.Y;
    const float ScopedBottom = InOrigin.Y + MViewport.Height;
    std::uint32_t ScopedKept = 0;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedWritten; ++ScopedIndex)
    {
        const Rhi::RenderItem& ScopedItem = OutItems[ScopedIndex];
        const float ScopedItemTop = ScopedItem.position.Y - ScopedItem.size.Y * 0.5f;
        const float ScopedItemBottom = ScopedItem.position.Y + ScopedItem.size.Y * 0.5f;
        if (ScopedItemBottom < ScopedTop || ScopedItemTop > ScopedBottom)
        {
            continue;
        }
        OutItems[ScopedKept++] = ScopedItem;
    }
    if (OutStats != nullptr)
    {
        *OutStats = ScopedTotal;
        OutStats->Items = ScopedKept;
    }
    return ScopedKept;
}

}
