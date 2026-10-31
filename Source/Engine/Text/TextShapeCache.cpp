// FemboyEngine2D - fixed-capacity ECS/DOD shape cache.
#include "Engine/Text/TextShapeCache.hpp"

#include "Engine/Text/TextBidi.hpp"

#include <algorithm>
#include <bit>

namespace Fby::Text
{

std::uint64_t TextShapeCache::HashText(std::string_view InText) noexcept
{
    std::uint64_t ScopedHash = 1469598103934665603ull;
    for (const char ScopedCharacter : InText)
    {
        ScopedHash ^= static_cast<std::uint8_t>(ScopedCharacter);
        ScopedHash *= 1099511628211ull;
    }
    return ScopedHash;
}

std::uint64_t TextShapeCache::HashInlines(std::span<const TextInlineObject> InInlines) noexcept
{
    if (InInlines.empty())
    {
        return 0;
    }
    std::uint64_t ScopedHash = 1469598103934665603ull;
    const auto ScopedMix = [&ScopedHash](std::uint64_t InValue)
    {
        ScopedHash ^= InValue + 0x9E3779B97F4A7C15ull + (ScopedHash << 6u) + (ScopedHash >> 2u);
        ScopedHash *= 1099511628211ull;
    };
    ScopedMix(InInlines.size());
    for (const TextInlineObject& ScopedInline : InInlines)
    {
        ScopedMix(ScopedInline.ByteOffset);
        ScopedMix(std::bit_cast<std::uint32_t>(ScopedInline.Width));
        ScopedMix(std::bit_cast<std::uint32_t>(ScopedInline.Height));
        ScopedMix(ScopedInline.Id);
        ScopedMix(static_cast<std::uint32_t>(ScopedInline.Kind));
        ScopedMix(std::bit_cast<std::uint32_t>(ScopedInline.Progress));
        ScopedMix(ScopedInline.Flags);
    }
    return ScopedHash;
}

void TextShapeCache::Touch(std::uint32_t InIndex) const noexcept
{
    MLastUse[InIndex] = MClock++;
}

TextShapeHandle TextShapeCache::Find(const TextShapeKey& InKey) const noexcept
{
    ++MLookups;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < kMaxShapeEntries; ++ScopedIndex)
    {
        if (MGenerations[ScopedIndex] != 0 && MKeys[ScopedIndex] == InKey)
        {
            ++MHits;
            Touch(ScopedIndex);
            return TextShapeHandle{ScopedIndex, MGenerations[ScopedIndex]};
        }
    }
    return {};
}

TextShapeView TextShapeCache::View(TextShapeHandle InHandle) const noexcept
{
    if (!InHandle.IsValid() || InHandle.Index >= kMaxShapeEntries ||
        MGenerations[InHandle.Index] != InHandle.Generation)
    {
        return {};
    }
    Touch(InHandle.Index);
    const std::uint32_t ScopedGlyphBegin = MGlyphBegin[InHandle.Index];
    const std::uint32_t ScopedLineBegin = MLineBegin[InHandle.Index];
    return TextShapeView{
        std::span<const ShapedGlyph>(MGlyphs.data() + ScopedGlyphBegin, MGlyphCount[InHandle.Index]),
        std::span<const ShapedLine>(MLines.data() + ScopedLineBegin, MLineCount[InHandle.Index]),
        MWidths[InHandle.Index]};
}

std::uint32_t TextShapeCache::AcquireSlot() noexcept
{
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < kMaxShapeEntries; ++ScopedIndex)
    {
        if (MGenerations[ScopedIndex] == 0)
        {
            return ScopedIndex;
        }
    }
    return 0xFFFFFFFFu;
}

std::uint32_t TextShapeCache::FindOldest() const noexcept
{
    std::uint32_t ScopedOldest = 0xFFFFFFFFu;
    std::uint64_t ScopedOldestUse = 0;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < kMaxShapeEntries; ++ScopedIndex)
    {
        if (MGenerations[ScopedIndex] != 0 &&
            (ScopedOldest == 0xFFFFFFFFu || MLastUse[ScopedIndex] < ScopedOldestUse))
        {
            ScopedOldest = ScopedIndex;
            ScopedOldestUse = MLastUse[ScopedIndex];
        }
    }
    return ScopedOldest;
}

bool TextShapeCache::EvictOldest() noexcept
{
    const std::uint32_t ScopedOldest = FindOldest();
    if (ScopedOldest == 0xFFFFFFFFu)
    {
        return false;
    }
    MGenerations[ScopedOldest] = 0;
    --MLiveCount;
    ++MEvictions;
    return true;
}

void TextShapeCache::Compact() noexcept
{
    std::uint32_t ScopedGlyphWrite = 0;
    std::uint32_t ScopedLineWrite = 0;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < kMaxShapeEntries; ++ScopedIndex)
    {
        if (MGenerations[ScopedIndex] == 0)
        {
            continue;
        }
        const std::uint32_t ScopedGlyphCount = MGlyphCount[ScopedIndex];
        const std::uint32_t ScopedLineCount = MLineCount[ScopedIndex];
        std::copy_n(MGlyphs.begin() + MGlyphBegin[ScopedIndex], ScopedGlyphCount,
                    MGlyphs.begin() + ScopedGlyphWrite);
        std::copy_n(MLines.begin() + MLineBegin[ScopedIndex], ScopedLineCount, MLines.begin() + ScopedLineWrite);
        MGlyphBegin[ScopedIndex] = ScopedGlyphWrite;
        MLineBegin[ScopedIndex] = ScopedLineWrite;
        ScopedGlyphWrite += ScopedGlyphCount;
        ScopedLineWrite += ScopedLineCount;
    }
    MGlyphUsed = ScopedGlyphWrite;
    MLineUsed = ScopedLineWrite;
}

TextShapeHandle TextShapeCache::Insert(ITextShaper& InShaper, std::string_view InText, const TextShapeKey& InKey,
                                       const TextLayoutParams& InLayoutParams, TextShapeResult* InScratch) noexcept
{
    if (const TextShapeHandle ScopedExisting = Find(InKey); ScopedExisting.IsValid())
    {
        return ScopedExisting;
    }
    ++MMisses;
    TextShapeResult* ScopedScratch = InScratch != nullptr ? InScratch : &MScratch;
    TextShapeParams ScopedParams;
    ScopedParams.FontId = InKey.FontId;
    ScopedParams.PixelSize = InKey.PixelSize;
    ScopedParams.Direction = InKey.Direction;
    ScopedParams.Vertical = InKey.Direction == TextDirection::TopToBottom;
    if (!ShapeBidiText(InShaper, InText, ScopedParams, ScopedScratch))
    {
        return {};
    }
    ShapedLayoutResult ScopedLayout;
    if (!LayoutShapedGlyphs(InText, *ScopedScratch, InLayoutParams, &ScopedLayout))
    {
        return {};
    }
    while (MGlyphUsed + ScopedScratch->Count > MGlyphs.size() || MLineUsed + ScopedLayout.Count > MLines.size())
    {
        if (!EvictOldest())
        {
            return {};
        }
        Compact();
    }
    std::uint32_t ScopedSlot = AcquireSlot();
    while (ScopedSlot == 0xFFFFFFFFu)
    {
        if (!EvictOldest())
        {
            return {};
        }
        Compact();
        ScopedSlot = AcquireSlot();
    }
    MKeys[ScopedSlot] = InKey;
    MGlyphBegin[ScopedSlot] = MGlyphUsed;
    MGlyphCount[ScopedSlot] = ScopedScratch->Count;
    MLineBegin[ScopedSlot] = MLineUsed;
    MLineCount[ScopedSlot] = ScopedLayout.Count;
    float ScopedWidth = 0.0f;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedLayout.Count; ++ScopedIndex)
    {
        MLines[MLineUsed++] = ScopedLayout.Lines[ScopedIndex];
        ScopedWidth = std::max(ScopedWidth, ScopedLayout.Lines[ScopedIndex].Width);
    }
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedScratch->Count; ++ScopedIndex)
    {
        MGlyphs[MGlyphUsed++] = ScopedScratch->Glyphs[ScopedIndex];
    }
    MWidths[ScopedSlot] = ScopedWidth;
    MGenerations[ScopedSlot] = MNextGeneration++;
    ++MLiveCount;
    Touch(ScopedSlot);
    return TextShapeHandle{ScopedSlot, MGenerations[ScopedSlot]};
}

void TextShapeCache::Clear() noexcept
{
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < kMaxShapeEntries; ++ScopedIndex)
    {
        MGenerations[ScopedIndex] = 0;
        MLastUse[ScopedIndex] = 0;
    }
    MGlyphUsed = 0;
    MLineUsed = 0;
    MLiveCount = 0;
    ++MNextGeneration;
    MClock = 1;
}
}
