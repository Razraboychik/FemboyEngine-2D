// FemboyEngine2D - runtime owner for long text assets and layout.
#pragma once

#include "Engine/Text/TextFormat.hpp"
#include "Engine/Text/TextLayout.hpp"
#include "Engine/Text/TextMarkup.hpp"
#include "Engine/Text/TextStore.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <string>
#include <string_view>

namespace Fby::Text
{

class TextService
{
public:
    using Store = TextStore<1u << 20, 4096, 64, 4096>;
    static constexpr std::uint32_t kLayoutCacheEntries = 8;
    static constexpr std::uint32_t kMaxStyledAssets = 8;
    static constexpr std::uint32_t kMaxInlineSprites = 64;
    static constexpr std::uint32_t kMaxStyledTextBytes = 4096;
    static constexpr std::uint32_t kMaxStyledMarkupBytes = 8192;
    static constexpr std::uint32_t kFormatCacheEntries = 8;
    static constexpr std::uint32_t kFormatCacheBytes = 8192;
    static constexpr std::uint32_t kFormattedLayoutEntries = 4;
    static constexpr std::uint32_t kMarkupVariantEntries = 4;

    [[nodiscard]] bool AddAsset(AssetId InAsset, std::string_view InText) noexcept
    {
        return MStore.AddAsset(InAsset, InText);
    }

    [[nodiscard]] bool HasAsset(AssetId InAsset) const noexcept
    {
        return MStore.Contains(InAsset);
    }

    [[nodiscard]] bool AddCookedAsset(AssetId InAsset, std::span<const std::byte> InBytes,
                                      std::string* InError = nullptr) noexcept
    {
        TextAssetView ScopedView;
        if (OpenTextAsset(InBytes, &ScopedView, InError) != TextAssetStatus::Ok)
        {
            return false;
        }
        return MStore.AddCookedAsset(InAsset, ScopedView);
    }

    [[nodiscard]] bool AddMarkupAsset(AssetId InAsset, std::string_view InMarkup) noexcept
    {
        if (!InAsset.IsValid() || FindStyled(InAsset) != nullptr || MStyledCount >= kMaxStyledAssets)
        {
            return false;
        }
        TextStyledBlock ScopedBlock;
        if (InMarkup.size() > kMaxStyledMarkupBytes ||
            CompileTextMarkup(InMarkup, &ScopedBlock) != TextMarkupStatus::Ok ||
            ScopedBlock.TextLength > kMaxStyledTextBytes)
        {
            return false;
        }
        if (!MStore.AddAsset(InAsset, ScopedBlock.View()))
        {
            return false;
        }
        StyledAsset& ScopedStyled = MStyled[MStyledCount++];
        ScopedStyled = {};
        ScopedStyled.Asset = InAsset;
        ScopedStyled.SpanCount = ScopedBlock.SpanCount;
        ScopedStyled.StyleCount = ScopedBlock.StyleCount;
        ScopedStyled.ActionCount = ScopedBlock.ActionCount;
        ScopedStyled.InlineCount = ScopedBlock.InlineCount;
        ScopedStyled.MarkupLength = static_cast<std::uint32_t>(InMarkup.size());
        for (std::size_t ScopedIndex = 0; ScopedIndex < InMarkup.size(); ++ScopedIndex)
        {
            ScopedStyled.Markup[ScopedIndex] = InMarkup[ScopedIndex];
        }
        ScopedStyled.Used = true;
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedBlock.SpanCount; ++ScopedIndex)
        {
            ScopedStyled.Spans[ScopedIndex] = ScopedBlock.Spans[ScopedIndex];
        }
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedBlock.StyleCount; ++ScopedIndex)
        {
            ScopedStyled.Styles[ScopedIndex] = ScopedBlock.Styles[ScopedIndex];
        }
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedBlock.ActionCount; ++ScopedIndex)
        {
            ScopedStyled.Actions[ScopedIndex] = ScopedBlock.Actions[ScopedIndex];
        }
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedBlock.InlineCount; ++ScopedIndex)
        {
            ScopedStyled.Inlines[ScopedIndex] = ScopedBlock.Inlines[ScopedIndex];
        }
        return true;
    }

    [[nodiscard]] std::span<const TextSpan> SpansFor(AssetId InAsset) const noexcept
    {
        const StyledAsset* ScopedStyled = FindStyled(InAsset);
        if (ScopedStyled == nullptr)
        {
            return {};
        }
        return std::span<const TextSpan>(ScopedStyled->Spans.data(), ScopedStyled->SpanCount);
    }

    [[nodiscard]] std::span<const TextStyle> StylesFor(AssetId InAsset) const noexcept
    {
        const StyledAsset* ScopedStyled = FindStyled(InAsset);
        if (ScopedStyled == nullptr)
        {
            return {};
        }
        return std::span<const TextStyle>(ScopedStyled->Styles.data(), ScopedStyled->StyleCount);
    }

    [[nodiscard]] std::span<const TextAction> ActionsFor(AssetId InAsset) const noexcept
    {
        const StyledAsset* ScopedStyled = FindStyled(InAsset);
        if (ScopedStyled == nullptr)
        {
            return {};
        }
        return std::span<const TextAction>(ScopedStyled->Actions.data(), ScopedStyled->ActionCount);
    }

    [[nodiscard]] const TextStyledBlock* CachedMarkupVariant(AssetId InAsset,
                                                             std::uint64_t InConditions) const noexcept
    {
        for (MarkupVariantEntry& ScopedEntry : MMarkupVariants)
        {
            if (ScopedEntry.Used && ScopedEntry.Asset == InAsset && ScopedEntry.Conditions == InConditions)
            {
                ++MMarkupVariantHits;
                ScopedEntry.LastUsed = ++MMarkupVariantClock;
                return &ScopedEntry.Block;
            }
        }
        MarkupVariantEntry* ScopedTarget = nullptr;
        for (MarkupVariantEntry& ScopedEntry : MMarkupVariants)
        {
            if (!ScopedEntry.Used)
            {
                ScopedTarget = &ScopedEntry;
                break;
            }
            if (ScopedTarget == nullptr || ScopedEntry.LastUsed < ScopedTarget->LastUsed)
            {
                ScopedTarget = &ScopedEntry;
            }
        }
        if (ScopedTarget == nullptr)
        {
            return nullptr;
        }
        if (!CompileMarkupVariant(InAsset, InConditions, &ScopedTarget->Block))
        {
            return nullptr;
        }
        ScopedTarget->Asset = InAsset;
        ScopedTarget->Conditions = InConditions;
        ScopedTarget->LastUsed = ++MMarkupVariantClock;
        ScopedTarget->Used = true;
        ++MMarkupVariantMisses;
        return &ScopedTarget->Block;
    }

    [[nodiscard]] std::uint32_t MarkupVariantHits() const noexcept
    {
        return MMarkupVariantHits;
    }

    [[nodiscard]] std::uint32_t MarkupVariantMisses() const noexcept
    {
        return MMarkupVariantMisses;
    }

    [[nodiscard]] bool CompileMarkupVariant(AssetId InAsset, std::uint64_t InConditions,
                                            TextStyledBlock* OutBlock) const noexcept
    {
        const StyledAsset* ScopedStyled = FindStyled(InAsset);
        if (ScopedStyled == nullptr || ScopedStyled->MarkupLength == 0 || OutBlock == nullptr)
        {
            return false;
        }
        return CompileTextMarkup(
                   std::string_view(ScopedStyled->Markup.data(), ScopedStyled->MarkupLength), OutBlock,
                   InConditions) == TextMarkupStatus::Ok;
    }

    [[nodiscard]] std::span<const TextInlineObject> InlinesFor(AssetId InAsset) const noexcept
    {
        const StyledAsset* ScopedStyled = FindStyled(InAsset);
        if (ScopedStyled == nullptr)
        {
            return {};
        }
        return std::span<const TextInlineObject>(ScopedStyled->Inlines.data(), ScopedStyled->InlineCount);
    }

    // Host-provided atlas slice for an inline sprite id. The mapping belongs to
    // the asset that owns the markup, so two blocks can reuse id 0 for
    // different icons.
    [[nodiscard]] bool AddInlineSprite(AssetId InAsset, TextInlineSprite InSprite) noexcept
    {
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < MStyledCount; ++ScopedIndex)
        {
            StyledAsset& ScopedStyled = MStyled[ScopedIndex];
            if (!ScopedStyled.Used || ScopedStyled.Asset != InAsset)
            {
                continue;
            }
            if (ScopedStyled.InlineSpriteCount >= kMaxInlineSprites)
            {
                return false;
            }
            ScopedStyled.InlineSprites[ScopedStyled.InlineSpriteCount++] = InSprite;
            return true;
        }
        return false;
    }
    [[nodiscard]] std::span<const TextInlineSprite> InlineSpritesFor(AssetId InAsset) const noexcept
    {
        const StyledAsset* ScopedStyled = FindStyled(InAsset);
        if (ScopedStyled == nullptr)
        {
            return {};
        }
        return std::span<const TextInlineSprite>(ScopedStyled->InlineSprites.data(), ScopedStyled->InlineSpriteCount);
    }

    [[nodiscard]] TextAssetRef MakeRef(AssetId InAsset, std::uint32_t InBlock = 0) const noexcept
    {
        return MStore.MakeRef(InAsset, InBlock);
    }

    [[nodiscard]] std::string_view Block(TextAssetRef InRef) const noexcept
    {
        return MStore.Block(InRef);
    }

    [[nodiscard]] std::uint32_t BlockCount(AssetId InAsset) const noexcept
    {
        return MStore.BlockCount(InAsset);
    }

    [[nodiscard]] bool Contains(AssetId InAsset) const noexcept
    {
        return MStore.Contains(InAsset);
    }

    [[nodiscard]] TextFormatStatus FormatBlock(TextAssetRef InRef, std::span<const TextFormatArg> InArgs,
                                               std::span<char> OutBuffer,
                                               std::size_t* OutSize = nullptr) const noexcept
    {
        return FormatTextTemplate(Block(InRef), InArgs, OutBuffer, OutSize);
    }

    // Caches the formatted block by caller-supplied value/spec hash. The returned
    // view is stable until the entry is evicted or the service is cleared.
    [[nodiscard]] bool FormatCached(TextAssetRef InRef, std::uint64_t InKey,
                                    std::span<const TextFormatArg> InArgs,
                                    std::string_view* OutText) const noexcept
    {
        if (OutText == nullptr || !InRef.IsValid() || !MStore.Contains(InRef.Asset))
        {
            return false;
        }
        FormatEntry* ScopedTarget = nullptr;
        for (FormatEntry& ScopedEntry : MFormatCache)
        {
            if (ScopedEntry.Used && ScopedEntry.Asset == InRef.Asset.value && ScopedEntry.Block == InRef.Block &&
                ScopedEntry.Generation == InRef.Generation && ScopedEntry.Key == InKey)
            {
                ++MFormatHits;
                ScopedEntry.LastUsed = ++MFormatClock;
                *OutText = std::string_view(ScopedEntry.Text.data(), ScopedEntry.Length);
                return true;
            }
            if (!ScopedEntry.Used)
            {
                if (ScopedTarget == nullptr)
                {
                    ScopedTarget = &ScopedEntry;
                }
            }
            else if (ScopedTarget == nullptr || ScopedEntry.LastUsed < ScopedTarget->LastUsed)
            {
                ScopedTarget = &ScopedEntry;
            }
        }
        if (ScopedTarget == nullptr)
        {
            return false;
        }
        std::size_t ScopedSize = 0;
        if (FormatBlock(InRef, InArgs, std::span<char>(ScopedTarget->Text), &ScopedSize) != TextFormatStatus::Ok ||
            ScopedSize > ScopedTarget->Text.size())
        {
            return false;
        }
        ++MFormatMisses;
        ScopedTarget->Asset = InRef.Asset.value;
        ScopedTarget->Block = InRef.Block;
        ScopedTarget->Generation = InRef.Generation;
        ScopedTarget->Key = InKey;
        ScopedTarget->Length = static_cast<std::uint32_t>(ScopedSize);
        ScopedTarget->LastUsed = ++MFormatClock;
        ScopedTarget->Used = true;
        *OutText = std::string_view(ScopedTarget->Text.data(), ScopedTarget->Length);
        return true;
    }

    [[nodiscard]] std::uint32_t FormatCacheHits() const noexcept
    {
        return MFormatHits;
    }

    [[nodiscard]] std::uint32_t FormatCacheMisses() const noexcept
    {
        return MFormatMisses;
    }

    [[nodiscard]] const TextLayoutResult* CachedFormattedLayout(std::string_view InText,
                                                                const TextLayoutParams& InParams) const noexcept
    {
        FormattedLayoutKey ScopedKey;
        ScopedKey.TextHash = 1469598103934665603ull;
        for (const char ScopedCharacter : InText)
        {
            ScopedKey.TextHash = (ScopedKey.TextHash ^ static_cast<unsigned char>(ScopedCharacter)) * 1099511628211ull;
        }
        ScopedKey.TextHash ^= InText.size();
        ScopedKey.WidthBits = std::bit_cast<std::uint32_t>(InParams.MaxWidth);
        ScopedKey.AdvanceBits = std::bit_cast<std::uint32_t>(InParams.Advance);
        for (FormattedLayoutEntry& ScopedEntry : MFormattedLayouts)
        {
            if (ScopedEntry.Used && ScopedEntry.Key == ScopedKey)
            {
                ++MFormattedLayoutHits;
                return &ScopedEntry.Result;
            }
        }
        FormattedLayoutEntry* ScopedTarget = nullptr;
        for (FormattedLayoutEntry& ScopedEntry : MFormattedLayouts)
        {
            if (!ScopedEntry.Used)
            {
                ScopedTarget = &ScopedEntry;
                break;
            }
            if (ScopedTarget == nullptr || ScopedEntry.LastUsed < ScopedTarget->LastUsed)
            {
                ScopedTarget = &ScopedEntry;
            }
        }
        if (ScopedTarget == nullptr)
        {
            return nullptr;
        }
        (void)LayoutText(InText, InParams, &ScopedTarget->Result);
        ScopedTarget->Key = ScopedKey;
        ScopedTarget->LastUsed = ++MFormattedLayoutClock;
        ScopedTarget->Used = true;
        ++MFormattedLayoutMisses;
        return &ScopedTarget->Result;
    }

    [[nodiscard]] std::uint32_t FormattedLayoutHits() const noexcept
    {
        return MFormattedLayoutHits;
    }

    [[nodiscard]] std::uint32_t FormattedLayoutMisses() const noexcept
    {
        return MFormattedLayoutMisses;
    }

    [[nodiscard]] bool Layout(TextAssetRef InRef, const TextLayoutParams& InParams,
                              TextLayoutResult* OutResult) const noexcept
    {
        if (OutResult == nullptr)
        {
            return false;
        }
        const TextLayoutResult* ScopedCached = CachedLayout(InRef, InParams);
        if (ScopedCached == nullptr)
        {
            OutResult->Count = 0;
            OutResult->Overflow = false;
            return false;
        }
        *OutResult = *ScopedCached;
        return !ScopedCached->Overflow;
    }

    // Cached line ranges stay valid until the entry is evicted or the service is
    // cleared. The renderer uses this pointer directly, so it copies no layout.
    [[nodiscard]] const TextLayoutResult* CachedLayout(TextAssetRef InRef,
                                                       const TextLayoutParams& InParams) const noexcept
    {
        if (!InRef.IsValid() || !MStore.Contains(InRef.Asset))
        {
            return nullptr;
        }
        LayoutKey ScopedKey;
        ScopedKey.Asset = InRef.Asset.value;
        ScopedKey.Block = InRef.Block;
        ScopedKey.Generation = InRef.Generation;
        ScopedKey.WidthBits = std::bit_cast<std::uint32_t>(InParams.MaxWidth);
        ScopedKey.AdvanceBits = std::bit_cast<std::uint32_t>(InParams.Advance);
        ScopedKey.InlineCount = static_cast<std::uint32_t>(InParams.Inlines.size());
        std::uint32_t ScopedInlineHash = 2166136261u;
        for (const TextInlineObject& ScopedInline : InParams.Inlines)
        {
            ScopedInlineHash = (ScopedInlineHash ^ ScopedInline.ByteOffset) * 16777619u;
            ScopedInlineHash = (ScopedInlineHash ^ std::bit_cast<std::uint32_t>(ScopedInline.Width)) * 16777619u;
            ScopedInlineHash = (ScopedInlineHash ^ std::bit_cast<std::uint32_t>(ScopedInline.Height)) * 16777619u;
            ScopedInlineHash = (ScopedInlineHash ^ ScopedInline.Id) * 16777619u;
        }
        ScopedKey.InlineHash = ScopedInlineHash;
        ScopedKey.Fonts = InParams.Fonts;
        ScopedKey.FontId = InParams.FontId;
        ScopedKey.FontSizeBits = std::bit_cast<std::uint32_t>(InParams.FontSize);
        ScopedKey.Spans = InParams.Spans.data();
        ScopedKey.SpanCount = static_cast<std::uint32_t>(InParams.Spans.size());
        ScopedKey.Styles = InParams.Styles.data();
        ScopedKey.StyleCount = static_cast<std::uint32_t>(InParams.Styles.size());
        for (const LayoutEntry& ScopedEntry : MLayouts)
        {
            if (ScopedEntry.Used && ScopedEntry.Key == ScopedKey)
            {
                ++MCacheHits;
                return &ScopedEntry.Result;
            }
        }

        ++MCacheMisses;
        LayoutEntry* ScopedTarget = nullptr;
        for (LayoutEntry& ScopedEntry : MLayouts)
        {
            if (!ScopedEntry.Used)
            {
                ScopedTarget = &ScopedEntry;
                break;
            }
            if (ScopedTarget == nullptr || ScopedEntry.LastUsed < ScopedTarget->LastUsed)
            {
                ScopedTarget = &ScopedEntry;
            }
        }
        if (ScopedTarget == nullptr)
        {
            return nullptr;
        }
        const std::string_view ScopedBlock = Block(InRef);
        TextLayoutParams ScopedParams = InParams;
        if (ScopedParams.Inlines.empty())
        {
            ScopedParams.Inlines = InlinesFor(InRef.Asset);
        }
        (void)LayoutText(ScopedBlock, ScopedParams, &ScopedTarget->Result);
        ScopedTarget->Key = ScopedKey;
        ScopedTarget->LastUsed = ++MUseClock;
        ScopedTarget->Used = true;
        return &ScopedTarget->Result;
    }

    [[nodiscard]] std::uint32_t CacheHits() const noexcept
    {
        return MCacheHits;
    }

    [[nodiscard]] std::uint32_t CacheMisses() const noexcept
    {
        return MCacheMisses;
    }

    void Clear() noexcept
    {
        MStore.Clear();
        for (LayoutEntry& ScopedEntry : MLayouts)
        {
            ScopedEntry = {};
        }
        for (StyledAsset& ScopedStyled : MStyled)
        {
            ScopedStyled = {};
        }
        MStyledCount = 0;
        for (FormatEntry& ScopedFormat : MFormatCache)
        {
            ScopedFormat = {};
        }
        MCacheHits = 0;
        MCacheMisses = 0;
        MFormatHits = 0;
        MFormatMisses = 0;
        MFormatClock = 0;
        for (FormattedLayoutEntry& ScopedLayout : MFormattedLayouts)
        {
            ScopedLayout = {};
        }
        MFormattedLayoutClock = 0;
        MFormattedLayoutHits = 0;
        MFormattedLayoutMisses = 0;
        for (MarkupVariantEntry& ScopedVariant : MMarkupVariants)
        {
            ScopedVariant = {};
        }
        MMarkupVariantClock = 0;
        MMarkupVariantHits = 0;
        MMarkupVariantMisses = 0;
        MUseClock = 0;
    }

    [[nodiscard]] const Store& StoreState() const noexcept
    {
        return MStore;
    }

private:
    struct LayoutKey
    {
        std::uint64_t Asset = 0;
        std::uint32_t Block = 0;
        std::uint32_t Generation = 0;
        std::uint32_t WidthBits = 0;
        std::uint32_t AdvanceBits = 0;
        std::uint32_t InlineCount = 0;
        std::uint32_t InlineHash = 0;
        const void* Fonts = nullptr;
        std::uint32_t FontId = 0;
        std::uint32_t FontSizeBits = 0;
        const void* Spans = nullptr;
        std::uint32_t SpanCount = 0;
        const void* Styles = nullptr;
        std::uint32_t StyleCount = 0;

        [[nodiscard]] friend constexpr bool operator==(const LayoutKey&, const LayoutKey&) = default;
    };

    struct LayoutEntry
    {
        LayoutKey Key{};
        TextLayoutResult Result{};
        std::uint32_t LastUsed = 0;
        bool Used = false;
    };

    struct FormattedLayoutKey
    {
        std::uint64_t TextHash = 0;
        std::uint32_t WidthBits = 0;
        std::uint32_t AdvanceBits = 0;

        [[nodiscard]] friend constexpr bool operator==(const FormattedLayoutKey&,
                                                       const FormattedLayoutKey&) = default;
    };

    struct FormattedLayoutEntry
    {
        FormattedLayoutKey Key{};
        TextLayoutResult Result{};
        std::uint32_t LastUsed = 0;
        bool Used = false;
    };

    struct MarkupVariantEntry
    {
        TextStyledBlock Block{};
        AssetId Asset{};
        std::uint64_t Conditions = 0;
        std::uint32_t LastUsed = 0;
        bool Used = false;
    };

    struct FormatEntry
    {
        std::array<char, kFormatCacheBytes> Text{};
        std::uint64_t Asset = 0;
        std::uint64_t Key = 0;
        std::uint32_t Block = 0;
        std::uint32_t Generation = 0;
        std::uint32_t Length = 0;
        std::uint32_t LastUsed = 0;
        bool Used = false;
    };

    struct StyledAsset
    {
        AssetId Asset{};
        std::array<TextSpan, TextStyledBlock::kMaxSpans> Spans{};
        std::array<TextStyle, TextStyledBlock::kMaxStyles> Styles{};
        std::array<TextAction, TextStyledBlock::kMaxActions> Actions{};
        std::array<TextInlineObject, TextStyledBlock::kMaxInlines> Inlines{};
        std::array<TextInlineSprite, kMaxInlineSprites> InlineSprites{};
        std::array<char, kMaxStyledMarkupBytes> Markup{};
        std::uint32_t SpanCount = 0;
        std::uint32_t StyleCount = 0;
        std::uint32_t ActionCount = 0;
        std::uint32_t InlineCount = 0;
        std::uint32_t InlineSpriteCount = 0;
        std::uint32_t MarkupLength = 0;
        bool Used = false;
    };

    [[nodiscard]] const StyledAsset* FindStyled(AssetId InAsset) const noexcept
    {
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < MStyledCount; ++ScopedIndex)
        {
            if (MStyled[ScopedIndex].Used && MStyled[ScopedIndex].Asset == InAsset)
            {
                return &MStyled[ScopedIndex];
            }
        }
        return nullptr;
    }

    [[nodiscard]] StyledAsset* FindStyled(AssetId InAsset) noexcept
    {
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < MStyledCount; ++ScopedIndex)
        {
            if (MStyled[ScopedIndex].Used && MStyled[ScopedIndex].Asset == InAsset)
            {
                return &MStyled[ScopedIndex];
            }
        }
        return nullptr;
    }

    Store MStore{};
    std::array<StyledAsset, kMaxStyledAssets> MStyled{};
    std::uint32_t MStyledCount = 0;
    mutable std::array<FormatEntry, kFormatCacheEntries> MFormatCache{};
    mutable std::uint32_t MFormatClock = 0;
    mutable std::uint32_t MFormatHits = 0;
    mutable std::uint32_t MFormatMisses = 0;
    mutable std::array<FormattedLayoutEntry, kFormattedLayoutEntries> MFormattedLayouts{};
    mutable std::uint32_t MFormattedLayoutClock = 0;
    mutable std::uint32_t MFormattedLayoutHits = 0;
    mutable std::uint32_t MFormattedLayoutMisses = 0;
    mutable std::array<MarkupVariantEntry, kMarkupVariantEntries> MMarkupVariants{};
    mutable std::uint32_t MMarkupVariantClock = 0;
    mutable std::uint32_t MMarkupVariantHits = 0;
    mutable std::uint32_t MMarkupVariantMisses = 0;
    mutable std::array<LayoutEntry, kLayoutCacheEntries> MLayouts{};
    mutable std::uint32_t MCacheHits = 0;
    mutable std::uint32_t MCacheMisses = 0;
    mutable std::uint32_t MUseClock = 0;
};
}
