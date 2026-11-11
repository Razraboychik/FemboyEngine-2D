// FemboyEngine2D - cooked book validation and zero-copy block access.
#include "Engine/Text/BookAsset.hpp"

#include <algorithm>
#include <cstring>
#include <limits>

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

[[nodiscard]] bool IsUtf8Continuation(char InChar) noexcept
{
    return (static_cast<unsigned char>(InChar) & 0xC0u) == 0x80u;
}

[[nodiscard]] bool IsUtf8Boundary(std::span<const char> InText, std::size_t InOffset) noexcept
{
    if (InOffset == 0 || InOffset == InText.size())
    {
        return true;
    }
    return InOffset < InText.size() && !IsUtf8Continuation(InText[InOffset]);
}

[[nodiscard]] bool IsValidUtf8(std::span<const char> InText) noexcept
{
    std::size_t ScopedPosition = 0;
    while (ScopedPosition < InText.size())
    {
        const auto ScopedFirst = static_cast<unsigned char>(InText[ScopedPosition]);
        if (ScopedFirst < 0x80)
        {
            ++ScopedPosition;
            continue;
        }
        std::size_t ScopedLength = 0;
        std::uint32_t ScopedCode = 0;
        std::uint32_t ScopedMinimum = 0;
        if ((ScopedFirst & 0xE0u) == 0xC0u)
        {
            ScopedLength = 2;
            ScopedCode = ScopedFirst & 0x1Fu;
            ScopedMinimum = 0x80;
        }
        else if ((ScopedFirst & 0xF0u) == 0xE0u)
        {
            ScopedLength = 3;
            ScopedCode = ScopedFirst & 0x0Fu;
            ScopedMinimum = 0x800;
        }
        else if ((ScopedFirst & 0xF8u) == 0xF0u)
        {
            ScopedLength = 4;
            ScopedCode = ScopedFirst & 0x07u;
            ScopedMinimum = 0x10000;
        }
        else
        {
            return false;
        }
        if (ScopedPosition + ScopedLength > InText.size())
        {
            return false;
        }
        for (std::size_t ScopedIndex = 1; ScopedIndex < ScopedLength; ++ScopedIndex)
        {
            if (!IsUtf8Continuation(InText[ScopedPosition + ScopedIndex]))
            {
                return false;
            }
            ScopedCode = (ScopedCode << 6) | (static_cast<unsigned char>(InText[ScopedPosition + ScopedIndex]) & 0x3Fu);
        }
        if (ScopedCode < ScopedMinimum || ScopedCode > 0x10FFFF || (ScopedCode >= 0xD800 && ScopedCode <= 0xDFFF))
        {
            return false;
        }
        ScopedPosition += ScopedLength;
    }
    return true;
}

template <typename T>
[[nodiscard]] bool ViewTable(std::span<const std::byte> InBytes, std::uint64_t InOffset, std::uint32_t InCount,
                             std::span<const T>* OutTable) noexcept
{
    if (InCount == 0)
    {
        *OutTable = {};
        return true;
    }
    const std::uint64_t ScopedBytes = static_cast<std::uint64_t>(InCount) * sizeof(T);
    if (InOffset > InBytes.size() || ScopedBytes > InBytes.size() - InOffset ||
        (InOffset % alignof(T)) != 0)
    {
        return false;
    }
    *OutTable = std::span<const T>(reinterpret_cast<const T*>(InBytes.data() + InOffset), InCount);
    return true;
}
}

const char* BookAssetErrorName(BookAssetError InError) noexcept
{
    switch (InError)
    {
        case BookAssetError::Ok:
            return "Ok";
        case BookAssetError::Truncated:
            return "Truncated";
        case BookAssetError::BadMagic:
            return "BadMagic";
        case BookAssetError::BadVersion:
            return "BadVersion";
        case BookAssetError::BadOffset:
            return "BadOffset";
        case BookAssetError::BadTable:
            return "BadTable";
        case BookAssetError::BadBlock:
            return "BadBlock";
        case BookAssetError::BadUtf8:
            return "BadUtf8";
    }
    return "?";
}

BookAssetError OpenBookAsset(std::span<const std::byte> InBytes, BookAssetView* OutView, std::string* InError) noexcept
{
    if (OutView == nullptr)
    {
        SetError(InError, "book: null output view");
        return BookAssetError::BadTable;
    }
    if (InBytes.size() < sizeof(BookHeader))
    {
        SetError(InError, "book: smaller than the header");
        return BookAssetError::Truncated;
    }
    BookAssetView ScopedView;
    ScopedView.Bytes = InBytes;
    std::memcpy(&ScopedView.Header, InBytes.data(), sizeof(BookHeader));
    if (std::memcmp(ScopedView.Header.Magic, kBookMagic, sizeof(kBookMagic)) != 0)
    {
        SetError(InError, "book: magic mismatch");
        return BookAssetError::BadMagic;
    }
    if (ScopedView.Header.Version != kBookVersion)
    {
        SetError(InError, "book: version mismatch");
        return BookAssetError::BadVersion;
    }
    if (ScopedView.Header.TextOffset > InBytes.size() ||
        ScopedView.Header.TextBytes > InBytes.size() - ScopedView.Header.TextOffset)
    {
        SetError(InError, "book: text arena is out of range");
        return BookAssetError::BadOffset;
    }
    if (!ViewTable(InBytes, ScopedView.Header.BlockTableOffset, ScopedView.Header.BlockCount, &ScopedView.Blocks) ||
        !ViewTable(InBytes, ScopedView.Header.SpanTableOffset, ScopedView.Header.SpanCount, &ScopedView.Spans) ||
        !ViewTable(InBytes, ScopedView.Header.StyleTableOffset, ScopedView.Header.StyleCount, &ScopedView.Styles) ||
        !ViewTable(InBytes, ScopedView.Header.InlineTableOffset, ScopedView.Header.InlineCount, &ScopedView.Inlines) ||
        !ViewTable(InBytes, ScopedView.Header.AnchorTableOffset, ScopedView.Header.AnchorCount, &ScopedView.Anchors))
    {
        SetError(InError, "book: a table is out of range or misaligned");
        return BookAssetError::BadTable;
    }
    if (ScopedView.Header.MetadataOffset > InBytes.size() ||
        ScopedView.Header.MetadataBytes > InBytes.size() - ScopedView.Header.MetadataOffset)
    {
        SetError(InError, "book: metadata is out of range");
        return BookAssetError::BadOffset;
    }
    ScopedView.Metadata = InBytes.subspan(ScopedView.Header.MetadataOffset,
                                          static_cast<std::size_t>(ScopedView.Header.MetadataBytes));
    ScopedView.Text = std::span<const char>(
        reinterpret_cast<const char*>(InBytes.data() + ScopedView.Header.TextOffset),
        static_cast<std::size_t>(ScopedView.Header.TextBytes));
    if (!IsValidUtf8(ScopedView.Text))
    {
        SetError(InError, "book: text arena is not valid UTF-8");
        return BookAssetError::BadUtf8;
    }

    for (const BookBlockRecord& ScopedBlock : ScopedView.Blocks)
    {
        if (static_cast<std::uint8_t>(ScopedBlock.Type) > static_cast<std::uint8_t>(BookBlockType::Callout))
        {
            SetError(InError, "book: unknown block type");
            return BookAssetError::BadBlock;
        }
        if (ScopedBlock.TextOffset > ScopedView.Header.TextBytes ||
            ScopedBlock.TextLength > ScopedView.Header.TextBytes - ScopedBlock.TextOffset)
        {
            SetError(InError, "book: block text is out of range");
            return BookAssetError::BadBlock;
        }
        const std::size_t ScopedBegin = ScopedBlock.TextOffset;
        const std::size_t ScopedEnd = static_cast<std::size_t>(ScopedBlock.TextOffset) + ScopedBlock.TextLength;
        if (!IsUtf8Boundary(ScopedView.Text, ScopedBegin) || !IsUtf8Boundary(ScopedView.Text, ScopedEnd))
        {
            SetError(InError, "book: block splits a UTF-8 sequence");
            return BookAssetError::BadUtf8;
        }
        if (ScopedBlock.SpanFirst > ScopedView.Header.SpanCount ||
            ScopedBlock.SpanCount > ScopedView.Header.SpanCount - ScopedBlock.SpanFirst)
        {
            SetError(InError, "book: block span range is out of range");
            return BookAssetError::BadBlock;
        }
        if (ScopedBlock.InlineFirst > ScopedView.Header.InlineCount ||
            ScopedBlock.InlineCount > ScopedView.Header.InlineCount - ScopedBlock.InlineFirst)
        {
            SetError(InError, "book: block inline range is out of range");
            return BookAssetError::BadBlock;
        }
        if (ScopedBlock.AnchorIndex != kBookInvalidIndex && ScopedBlock.AnchorIndex >= ScopedView.Header.AnchorCount)
        {
            SetError(InError, "book: block anchor is out of range");
            return BookAssetError::BadBlock;
        }
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedBlock.SpanCount; ++ScopedIndex)
        {
            const BookSpan& ScopedSpan = ScopedView.Spans[ScopedBlock.SpanFirst + ScopedIndex];
            if (ScopedSpan.TextBegin > ScopedSpan.TextEnd || ScopedSpan.TextEnd > ScopedBlock.TextLength ||
                ScopedSpan.StyleIndex >= ScopedView.Header.StyleCount)
            {
                SetError(InError, "book: a span is out of range");
                return BookAssetError::BadBlock;
            }
        }
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedBlock.InlineCount; ++ScopedIndex)
        {
            if (ScopedView.Inlines[ScopedBlock.InlineFirst + ScopedIndex].ByteOffset > ScopedBlock.TextLength)
            {
                SetError(InError, "book: an inline is out of range");
                return BookAssetError::BadBlock;
            }
        }
    }

    for (const BookAnchor& ScopedAnchor : ScopedView.Anchors)
    {
        if (ScopedAnchor.BlockIndex >= ScopedView.Header.BlockCount)
        {
            SetError(InError, "book: anchor block is out of range");
            return BookAssetError::BadBlock;
        }
        const BookBlockRecord& ScopedBlock = ScopedView.Blocks[ScopedAnchor.BlockIndex];
        if (ScopedAnchor.ByteOffset > ScopedBlock.TextLength ||
            ScopedAnchor.NameOffset > ScopedView.Header.MetadataBytes ||
            ScopedAnchor.NameLength > ScopedView.Header.MetadataBytes - ScopedAnchor.NameOffset)
        {
            SetError(InError, "book: anchor range is out of range");
            return BookAssetError::BadBlock;
        }
        const std::span<const char> ScopedBlockText(ScopedView.Text.data() + ScopedBlock.TextOffset,
                                                     ScopedBlock.TextLength);
        if (!IsUtf8Boundary(ScopedBlockText, ScopedAnchor.ByteOffset))
        {
            SetError(InError, "book: anchor splits a UTF-8 sequence");
            return BookAssetError::BadBlock;
        }
    }

    *OutView = ScopedView;
    SetError(InError, "");
    return BookAssetError::Ok;
}

BookAssetError BookReader::Open(std::span<const std::byte> InBytes, std::string* InError) noexcept
{
    Close();
    BookAssetError ScopedError = OpenBookAsset(InBytes, &MView, InError);
    if (ScopedError != BookAssetError::Ok)
    {
        MView = {};
        MBytes = {};
        return ScopedError;
    }
    MBytes = InBytes;
    return BookAssetError::Ok;
}

void BookReader::Close() noexcept
{
    MView = {};
    MBytes = {};
}

bool BookReader::Block(std::uint32_t InIndex, BookBlockView* OutBlock) const noexcept
{
    if (!IsOpen() || OutBlock == nullptr || InIndex >= MView.Header.BlockCount)
    {
        return false;
    }
    const BookBlockRecord& ScopedRecord = MView.Blocks[InIndex];
    OutBlock->Type = ScopedRecord.Type;
    OutBlock->Flags = ScopedRecord.Flags;
    OutBlock->Text = std::span<const char>(MView.Text.data() + ScopedRecord.TextOffset, ScopedRecord.TextLength);
    OutBlock->Spans = std::span<const BookSpan>(MView.Spans.data() + ScopedRecord.SpanFirst, ScopedRecord.SpanCount);
    OutBlock->Inlines =
        std::span<const BookInline>(MView.Inlines.data() + ScopedRecord.InlineFirst, ScopedRecord.InlineCount);
    OutBlock->AnchorIndex = ScopedRecord.AnchorIndex;
    return true;
}

bool BookReader::Anchor(std::uint32_t InIndex, BookAnchor* OutAnchor, std::string_view* OutName) const noexcept
{
    if (!IsOpen() || OutAnchor == nullptr || InIndex >= MView.Header.AnchorCount)
    {
        return false;
    }
    const BookAnchor& ScopedAnchor = MView.Anchors[InIndex];
    *OutAnchor = ScopedAnchor;
    if (OutName != nullptr)
    {
        const char* ScopedName = reinterpret_cast<const char*>(MView.Metadata.data() + ScopedAnchor.NameOffset);
        *OutName = std::string_view(ScopedName, ScopedAnchor.NameLength);
    }
    return true;
}

}
