// FemboyEngine2D - source Markdown to .fbybook writer.
#include "Engine/Text/BookCook.hpp"

#include "Engine/Text/BookAsset.hpp"
#include "Engine/Text/TextStyle.hpp"

#include <algorithm>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace Fby::Text
{
namespace
{
inline constexpr std::uint32_t kMaxBookBlocks = 65536;
inline constexpr std::uint32_t kMaxBookSpans = 65536;
inline constexpr std::uint32_t kMaxBookStyles = 256;
inline constexpr std::uint32_t kMaxBookInlines = 4096;
inline constexpr std::size_t kMaxBookText = 16u * 1024u * 1024u;

struct BuilderBlock
{
    BookBlockType Type = BookBlockType::Paragraph;
    std::uint8_t Flags = 0;
    std::string Text;
    std::vector<BookSpan> Spans;
    std::vector<BookInline> Inlines;
};

struct BuilderAnchor
{
    std::uint32_t BlockIndex = 0;
    std::string Name;
};

struct Builder
{
    std::vector<BuilderBlock> Blocks;
    std::vector<BookStyle> Styles;
    std::vector<BuilderAnchor> Anchors;
    std::string Text;
    std::string Metadata;
};

void SetError(std::string* OutError, const char* InMessage)
{
    if (OutError != nullptr)
    {
        *OutError = InMessage;
    }
}

[[nodiscard]] bool StartsWith(std::string_view InText, std::string_view InPrefix) noexcept
{
    return InText.size() >= InPrefix.size() && InText.substr(0, InPrefix.size()) == InPrefix;
}

[[nodiscard]] std::string_view TrimLeft(std::string_view InText) noexcept
{
    while (!InText.empty() && (InText.front() == ' ' || InText.front() == '\t'))
    {
        InText.remove_prefix(1);
    }
    return InText;
}

[[nodiscard]] std::string_view TrimRight(std::string_view InText) noexcept
{
    while (!InText.empty() && (InText.back() == ' ' || InText.back() == '\t' || InText.back() == '\r'))
    {
        InText.remove_suffix(1);
    }
    return InText;
}

[[nodiscard]] std::string_view Trim(std::string_view InText) noexcept
{
    return TrimRight(TrimLeft(InText));
}

[[nodiscard]] std::string MakeAnchorName(std::string_view InText)
{
    std::string ScopedName;
    ScopedName.reserve(InText.size());
    for (const char ScopedChar : InText)
    {
        if (ScopedChar >= 'A' && ScopedChar <= 'Z')
        {
            ScopedName.push_back(static_cast<char>(ScopedChar - 'A' + 'a'));
        }
        else if ((ScopedChar >= 'a' && ScopedChar <= 'z') || (ScopedChar >= '0' && ScopedChar <= '9'))
        {
            ScopedName.push_back(ScopedChar);
        }
        else if (!ScopedName.empty() && ScopedName.back() != '-')
        {
            ScopedName.push_back('-');
        }
    }
    while (!ScopedName.empty() && ScopedName.back() == '-')
    {
        ScopedName.pop_back();
    }
    return ScopedName.empty() ? "section" : ScopedName;
}

[[nodiscard]] std::uint32_t HashPath(std::string_view InPath) noexcept
{
    std::uint32_t ScopedHash = 2166136261u;
    for (const char ScopedChar : InPath)
    {
        ScopedHash ^= static_cast<std::uint8_t>(ScopedChar);
        ScopedHash *= 16777619u;
    }
    return ScopedHash;
}

[[nodiscard]] std::uint32_t StyleIndex(Builder& InBuilder, std::uint16_t InFlags)
{
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < InBuilder.Styles.size(); ++ScopedIndex)
    {
        if (InBuilder.Styles[ScopedIndex].Flags == InFlags)
        {
            return ScopedIndex;
        }
    }
    BookStyle ScopedStyle;
    ScopedStyle.Flags = InFlags;
    InBuilder.Styles.push_back(ScopedStyle);
    return static_cast<std::uint32_t>(InBuilder.Styles.size() - 1);
}

void AddInlineStyles(std::string_view InSource, BuilderBlock& InBlock, Builder& InBuilder)
{
    std::size_t ScopedPosition = 0;
    while (ScopedPosition < InSource.size())
    {
        if (InSource[ScopedPosition] == '\\' && ScopedPosition + 1 < InSource.size())
        {
            InBlock.Text.push_back(InSource[ScopedPosition + 1]);
            ScopedPosition += 2;
            continue;
        }
        if (InSource[ScopedPosition] == '*' && ScopedPosition + 1 < InSource.size() &&
            InSource[ScopedPosition + 1] == '*')
        {
            const std::size_t ScopedClose = InSource.find("**", ScopedPosition + 2);
            if (ScopedClose != std::string_view::npos)
            {
                const std::uint32_t ScopedBegin = static_cast<std::uint32_t>(InBlock.Text.size());
                InBlock.Text.append(InSource.substr(ScopedPosition + 2, ScopedClose - ScopedPosition - 2));
                const std::uint32_t ScopedEnd = static_cast<std::uint32_t>(InBlock.Text.size());
                if (ScopedEnd > ScopedBegin)
                {
                    InBlock.Spans.push_back(
                        BookSpan{ScopedBegin, ScopedEnd, StyleIndex(InBuilder, static_cast<std::uint16_t>(1))});
                }
                ScopedPosition = ScopedClose + 2;
                continue;
            }
        }
        if (InSource[ScopedPosition] == '*')
        {
            const std::size_t ScopedClose = InSource.find('*', ScopedPosition + 1);
            if (ScopedClose != std::string_view::npos)
            {
                const std::uint32_t ScopedBegin = static_cast<std::uint32_t>(InBlock.Text.size());
                InBlock.Text.append(InSource.substr(ScopedPosition + 1, ScopedClose - ScopedPosition - 1));
                const std::uint32_t ScopedEnd = static_cast<std::uint32_t>(InBlock.Text.size());
                if (ScopedEnd > ScopedBegin)
                {
                    InBlock.Spans.push_back(
                        BookSpan{ScopedBegin, ScopedEnd, StyleIndex(InBuilder, static_cast<std::uint16_t>(2))});
                }
                ScopedPosition = ScopedClose + 1;
                continue;
            }
        }
        if (InSource[ScopedPosition] == '`')
        {
            const std::size_t ScopedClose = InSource.find('`', ScopedPosition + 1);
            if (ScopedClose != std::string_view::npos)
            {
                const std::uint32_t ScopedBegin = static_cast<std::uint32_t>(InBlock.Text.size());
                InBlock.Text.append(InSource.substr(ScopedPosition + 1, ScopedClose - ScopedPosition - 1));
                const std::uint32_t ScopedEnd = static_cast<std::uint32_t>(InBlock.Text.size());
                if (ScopedEnd > ScopedBegin)
                {
                    InBlock.Spans.push_back(BookSpan{
                        ScopedBegin, ScopedEnd,
                        StyleIndex(InBuilder, static_cast<std::uint16_t>(TextStyleFlags::Monospace))});
                }
                ScopedPosition = ScopedClose + 1;
                continue;
            }
        }
        InBlock.Text.push_back(InSource[ScopedPosition]);
        ++ScopedPosition;
    }
}

[[nodiscard]] std::vector<std::string> SplitLines(std::string_view InSource)
{
    std::vector<std::string> ScopedLines;
    std::size_t ScopedStart = 0;
    for (std::size_t ScopedPosition = 0; ScopedPosition <= InSource.size(); ++ScopedPosition)
    {
        if (ScopedPosition == InSource.size() || InSource[ScopedPosition] == '\n')
        {
            std::string_view ScopedLine = InSource.substr(ScopedStart, ScopedPosition - ScopedStart);
            if (!ScopedLine.empty() && ScopedLine.back() == '\r')
            {
                ScopedLine.remove_suffix(1);
            }
            ScopedLines.emplace_back(ScopedLine);
            ScopedStart = ScopedPosition + 1;
        }
    }
    return ScopedLines;
}

[[nodiscard]] bool IsBlockStart(std::string_view InLine) noexcept
{
    const std::string_view ScopedLine = TrimLeft(InLine);
    return ScopedLine.empty() || StartsWith(ScopedLine, "#") || StartsWith(ScopedLine, "- ") ||
           StartsWith(ScopedLine, "* ") || StartsWith(ScopedLine, "> ") || StartsWith(ScopedLine, "```") ||
           StartsWith(ScopedLine, "|") || StartsWith(ScopedLine, "![") || StartsWith(ScopedLine, "\\pagebreak");
}

void AddBlock(Builder& InBuilder, BookBlockType InType, std::uint8_t InFlags, std::string_view InText)
{
    BuilderBlock ScopedBlock;
    ScopedBlock.Type = InType;
    ScopedBlock.Flags = InFlags;
    AddInlineStyles(InText, ScopedBlock, InBuilder);
    InBuilder.Blocks.push_back(std::move(ScopedBlock));
}

[[nodiscard]] BookCookError ParseBody(std::string_view InBody, Builder* OutBuilder, std::string* InError)
{
    std::vector<std::string> ScopedLines = SplitLines(InBody);
    std::size_t ScopedIndex = 0;
    while (ScopedIndex < ScopedLines.size())
    {
        const std::string_view ScopedRaw = TrimRight(ScopedLines[ScopedIndex]);
        const std::string_view ScopedLine = TrimLeft(ScopedRaw);
        if (ScopedLine.empty())
        {
            ++ScopedIndex;
            continue;
        }
        if (ScopedLine == "\\pagebreak")
        {
            BuilderBlock ScopedBreak;
            ScopedBreak.Type = BookBlockType::PageBreak;
            OutBuilder->Blocks.push_back(std::move(ScopedBreak));
            ++ScopedIndex;
            continue;
        }
        if (StartsWith(ScopedLine, "```"))
        {
            ++ScopedIndex;
            std::string ScopedCode;
            bool ScopedClosed = false;
            while (ScopedIndex < ScopedLines.size())
            {
                const std::string_view ScopedCodeLine = TrimRight(ScopedLines[ScopedIndex]);
                if (StartsWith(ScopedCodeLine, "```"))
                {
                    ScopedClosed = true;
                    ++ScopedIndex;
                    break;
                }
                if (!ScopedCode.empty())
                {
                    ScopedCode.push_back('\n');
                }
                ScopedCode.append(ScopedCodeLine);
                ++ScopedIndex;
            }
            if (!ScopedClosed)
            {
                SetError(InError, "book cook: unclosed code fence");
                return BookCookError::UnclosedCodeFence;
            }
            BuilderBlock ScopedBlock;
            ScopedBlock.Type = BookBlockType::Code;
            ScopedBlock.Text = std::move(ScopedCode);
            OutBuilder->Blocks.push_back(std::move(ScopedBlock));
            continue;
        }
        if (StartsWith(ScopedLine, "#"))
        {
            std::size_t ScopedLevel = 0;
            while (ScopedLevel < ScopedLine.size() && ScopedLine[ScopedLevel] == '#')
            {
                ++ScopedLevel;
            }
            if (ScopedLevel <= 6 && ScopedLevel < ScopedLine.size() && ScopedLine[ScopedLevel] == ' ')
            {
                const std::string_view ScopedHeading = TrimLeft(ScopedLine.substr(ScopedLevel + 1));
                AddBlock(*OutBuilder, BookBlockType::Heading, static_cast<std::uint8_t>(ScopedLevel), ScopedHeading);
                BuilderAnchor ScopedAnchor;
                ScopedAnchor.BlockIndex = static_cast<std::uint32_t>(OutBuilder->Blocks.size() - 1);
                ScopedAnchor.Name = MakeAnchorName(ScopedHeading);
                OutBuilder->Anchors.push_back(std::move(ScopedAnchor));
                ++ScopedIndex;
                continue;
            }
        }
        if (StartsWith(ScopedLine, "- ") || StartsWith(ScopedLine, "* "))
        {
            AddBlock(*OutBuilder, BookBlockType::ListItem, 0, ScopedLine.substr(2));
            ++ScopedIndex;
            continue;
        }
        if (StartsWith(ScopedLine, "> "))
        {
            AddBlock(*OutBuilder, BookBlockType::Quote, 0, ScopedLine.substr(2));
            ++ScopedIndex;
            continue;
        }
        if (StartsWith(ScopedLine, "|"))
        {
            std::string ScopedTable;
            while (ScopedIndex < ScopedLines.size())
            {
                const std::string_view ScopedTableLine = Trim(ScopedLines[ScopedIndex]);
                if (!StartsWith(ScopedTableLine, "|"))
                {
                    break;
                }
                if (!ScopedTable.empty())
                {
                    ScopedTable.push_back('\n');
                }
                ScopedTable.append(ScopedTableLine);
                ++ScopedIndex;
            }
            AddBlock(*OutBuilder, BookBlockType::Table, 0, ScopedTable);
            continue;
        }
        if (StartsWith(ScopedLine, "!["))
        {
            const std::size_t ScopedAltEnd = ScopedLine.find(']');
            const std::size_t ScopedPathEnd = ScopedLine.find(')', ScopedAltEnd == std::string_view::npos ? 0 : ScopedAltEnd);
            if (ScopedAltEnd != std::string_view::npos && ScopedPathEnd != std::string_view::npos &&
                ScopedAltEnd + 2 < ScopedPathEnd)
            {
                const std::string_view ScopedAlt = ScopedLine.substr(2, ScopedAltEnd - 2);
                const std::string_view ScopedPath = ScopedLine.substr(ScopedAltEnd + 2, ScopedPathEnd - ScopedAltEnd - 2);
                BuilderBlock ScopedBlock;
                ScopedBlock.Type = BookBlockType::Image;
                ScopedBlock.Text.assign(ScopedAlt);
                ScopedBlock.Inlines.push_back(BookInline{0, HashPath(ScopedPath), 0, 0});
                OutBuilder->Blocks.push_back(std::move(ScopedBlock));
                ++ScopedIndex;
                continue;
            }
        }

        // Paragraph: consume lines until a blank line or another block start.
        std::string ScopedParagraph;
        while (ScopedIndex < ScopedLines.size())
        {
            const std::string_view ScopedParagraphLine = TrimRight(ScopedLines[ScopedIndex]);
            if (TrimLeft(ScopedParagraphLine).empty() || (ScopedParagraph.empty() == false &&
                IsBlockStart(ScopedParagraphLine)))
            {
                break;
            }
            if (!ScopedParagraph.empty())
            {
                ScopedParagraph.push_back('\n');
            }
            ScopedParagraph.append(ScopedParagraphLine);
            ++ScopedIndex;
        }
        if (ScopedParagraph.empty())
        {
            ++ScopedIndex;
            continue;
        }
        AddBlock(*OutBuilder, BookBlockType::Paragraph, 0, ScopedParagraph);
    }
    return BookCookError::Ok;
}

template <typename T> std::uint64_t AppendArray(std::vector<std::byte>& InBytes, const std::vector<T>& InValues)
{
    while (InBytes.size() % alignof(T) != 0)
    {
        InBytes.push_back(std::byte{0});
    }
    const std::uint64_t ScopedOffset = InBytes.size();
    const std::size_t ScopedBytes = InValues.size() * sizeof(T);
    InBytes.resize(InBytes.size() + ScopedBytes);
    if (ScopedBytes > 0)
    {
        std::memcpy(InBytes.data() + ScopedOffset, InValues.data(), ScopedBytes);
    }
    return ScopedOffset;
}
}

const char* BookCookErrorName(BookCookError InError) noexcept
{
    switch (InError)
    {
        case BookCookError::Ok:
            return "Ok";
        case BookCookError::Empty:
            return "Empty";
        case BookCookError::InvalidFrontMatter:
            return "InvalidFrontMatter";
        case BookCookError::UnclosedCodeFence:
            return "UnclosedCodeFence";
        case BookCookError::TooManyBlocks:
            return "TooManyBlocks";
        case BookCookError::TooManySpans:
            return "TooManySpans";
        case BookCookError::TooManyStyles:
            return "TooManyStyles";
        case BookCookError::TooManyInlines:
            return "TooManyInlines";
        case BookCookError::TextTooLarge:
            return "TextTooLarge";
        case BookCookError::InvalidMarkup:
            return "InvalidMarkup";
    }
    return "?";
}

BookCookError CookBook(std::string_view InSource, std::vector<std::byte>* OutBytes, std::string* InError)
{
    SetError(InError, "");
    if (OutBytes == nullptr)
    {
        return BookCookError::InvalidMarkup;
    }
    OutBytes->clear();
    if (InSource.size() > kMaxBookText)
    {
        SetError(InError, "book cook: source is too large");
        return BookCookError::TextTooLarge;
    }
    std::size_t ScopedStart = 0;
    if (InSource.size() >= 3 && static_cast<unsigned char>(InSource[0]) == 0xEF &&
        static_cast<unsigned char>(InSource[1]) == 0xBB && static_cast<unsigned char>(InSource[2]) == 0xBF)
    {
        ScopedStart = 3;
    }
    InSource.remove_prefix(ScopedStart);
    std::vector<std::string> ScopedLines = SplitLines(InSource);
    std::string ScopedMetadata;
    std::size_t ScopedLine = 0;
    if (!ScopedLines.empty() && Trim(ScopedLines[0]) == "+++")
    {
        ++ScopedLine;
        bool ScopedClosed = false;
        while (ScopedLine < ScopedLines.size())
        {
            if (Trim(ScopedLines[ScopedLine]) == "+++")
            {
                ScopedClosed = true;
                ++ScopedLine;
                break;
            }
            if (!ScopedMetadata.empty())
            {
                ScopedMetadata.push_back('\n');
            }
            ScopedMetadata.append(TrimRight(ScopedLines[ScopedLine]));
            ++ScopedLine;
        }
        if (!ScopedClosed)
        {
            SetError(InError, "book cook: unclosed front matter");
            return BookCookError::InvalidFrontMatter;
        }
    }

    std::string ScopedBody;
    for (std::size_t ScopedIndex = ScopedLine; ScopedIndex < ScopedLines.size(); ++ScopedIndex)
    {
        if (!ScopedBody.empty())
        {
            ScopedBody.push_back('\n');
        }
        ScopedBody.append(ScopedLines[ScopedIndex]);
    }
    Builder ScopedBuilder;
    ScopedBuilder.Metadata = std::move(ScopedMetadata);
    // Style 0 is always the default so an unstyled span never aliases bold/italic.
    ScopedBuilder.Styles.push_back(BookStyle{});
    const BookCookError ScopedParse = ParseBody(ScopedBody, &ScopedBuilder, InError);
    if (ScopedParse != BookCookError::Ok)
    {
        return ScopedParse;
    }
    if (ScopedBuilder.Blocks.empty())
    {
        SetError(InError, "book cook: document has no blocks");
        return BookCookError::Empty;
    }
    if (ScopedBuilder.Blocks.size() > kMaxBookBlocks)
    {
        SetError(InError, "book cook: too many blocks");
        return BookCookError::TooManyBlocks;
    }
    std::vector<BookBlockRecord> ScopedRecords;
    std::vector<BookSpan> ScopedSpans;
    std::vector<BookInline> ScopedInlines;
    ScopedRecords.reserve(ScopedBuilder.Blocks.size());
    for (BuilderBlock& ScopedBlock : ScopedBuilder.Blocks)
    {
        if (ScopedBlock.Spans.size() > kMaxBookSpans - ScopedSpans.size() ||
            ScopedBlock.Inlines.size() > kMaxBookInlines - ScopedInlines.size())
        {
            SetError(InError, "book cook: too many spans or inlines");
            return BookCookError::TooManySpans;
        }
        BookBlockRecord ScopedRecord;
        ScopedRecord.Type = ScopedBlock.Type;
        ScopedRecord.Flags = ScopedBlock.Flags;
        ScopedRecord.TextOffset = static_cast<std::uint32_t>(ScopedBuilder.Text.size());
        ScopedRecord.TextLength = static_cast<std::uint32_t>(ScopedBlock.Text.size());
        ScopedRecord.SpanFirst = static_cast<std::uint32_t>(ScopedSpans.size());
        ScopedRecord.SpanCount = static_cast<std::uint32_t>(ScopedBlock.Spans.size());
        ScopedRecord.InlineFirst = static_cast<std::uint32_t>(ScopedInlines.size());
        ScopedRecord.InlineCount = static_cast<std::uint32_t>(ScopedBlock.Inlines.size());
        ScopedBuilder.Text.append(ScopedBlock.Text);
        for (BookSpan ScopedSpan : ScopedBlock.Spans)
        {
            ScopedSpans.push_back(ScopedSpan);
        }
        for (BookInline ScopedInline : ScopedBlock.Inlines)
        {
            ScopedInlines.push_back(ScopedInline);
        }
        ScopedRecords.push_back(ScopedRecord);
    }
    if (ScopedBuilder.Text.size() > kMaxBookText || ScopedBuilder.Styles.size() > kMaxBookStyles)
    {
        SetError(InError, "book cook: text or styles exceed the limits");
        return BookCookError::TextTooLarge;
    }

    std::vector<BookAnchor> ScopedAnchors;
    ScopedAnchors.reserve(ScopedBuilder.Anchors.size());
    for (const BuilderAnchor& ScopedBuilderAnchor : ScopedBuilder.Anchors)
    {
        if (!ScopedBuilder.Metadata.empty())
        {
            ScopedBuilder.Metadata.push_back('\n');
        }
        BookAnchor ScopedAnchor;
        ScopedAnchor.BlockIndex = ScopedBuilderAnchor.BlockIndex;
        ScopedAnchor.ByteOffset = 0;
        ScopedAnchor.NameOffset = static_cast<std::uint32_t>(ScopedBuilder.Metadata.size());
        ScopedAnchor.NameLength = static_cast<std::uint32_t>(ScopedBuilderAnchor.Name.size());
        ScopedBuilder.Metadata.append(ScopedBuilderAnchor.Name);
        ScopedAnchors.push_back(ScopedAnchor);
        if (ScopedBuilderAnchor.BlockIndex < ScopedRecords.size())
        {
            ScopedRecords[ScopedBuilderAnchor.BlockIndex].AnchorIndex =
                static_cast<std::uint32_t>(ScopedAnchors.size() - 1);
        }
    }

    std::vector<std::byte> ScopedBytes(kBookHeaderBytes, std::byte{0});
    BookHeader ScopedHeader;
    ScopedHeader.BlockCount = static_cast<std::uint32_t>(ScopedRecords.size());
    ScopedHeader.SpanCount = static_cast<std::uint32_t>(ScopedSpans.size());
    ScopedHeader.StyleCount = static_cast<std::uint32_t>(ScopedBuilder.Styles.size());
    ScopedHeader.InlineCount = static_cast<std::uint32_t>(ScopedInlines.size());
    ScopedHeader.AnchorCount = static_cast<std::uint32_t>(ScopedAnchors.size());
    ScopedHeader.BlockTableOffset = AppendArray(ScopedBytes, ScopedRecords);
    ScopedHeader.SpanTableOffset = AppendArray(ScopedBytes, ScopedSpans);
    ScopedHeader.StyleTableOffset = AppendArray(ScopedBytes, ScopedBuilder.Styles);
    ScopedHeader.InlineTableOffset = AppendArray(ScopedBytes, ScopedInlines);
    ScopedHeader.AnchorTableOffset = AppendArray(ScopedBytes, ScopedAnchors);
    ScopedHeader.TextOffset = ScopedBytes.size();
    ScopedBytes.resize(ScopedBytes.size() + ScopedBuilder.Text.size());
    if (!ScopedBuilder.Text.empty())
    {
        std::memcpy(ScopedBytes.data() + ScopedHeader.TextOffset, ScopedBuilder.Text.data(), ScopedBuilder.Text.size());
    }
    ScopedHeader.TextBytes = ScopedBuilder.Text.size();
    ScopedHeader.MetadataOffset = ScopedBytes.size();
    ScopedBytes.resize(ScopedBytes.size() + ScopedBuilder.Metadata.size());
    if (!ScopedBuilder.Metadata.empty())
    {
        std::memcpy(ScopedBytes.data() + ScopedHeader.MetadataOffset, ScopedBuilder.Metadata.data(),
                    ScopedBuilder.Metadata.size());
    }
    ScopedHeader.MetadataBytes = ScopedBuilder.Metadata.size();
    std::memcpy(ScopedBytes.data(), &ScopedHeader, sizeof(ScopedHeader));
    *OutBytes = std::move(ScopedBytes);
    return BookCookError::Ok;
}

}
