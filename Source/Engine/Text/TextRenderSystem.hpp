// FemboyEngine2D - ECS text extraction system.
#pragma once

#include "Engine/Assets/Localization.hpp"
#include "Engine/ECS/Components.hpp"
#include "Engine/ECS/Schedule.hpp"
#include "Engine/Text/TextRender.hpp"
#include "Engine/Text/TextShapeCache.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <string_view>

namespace Fby::Text
{

struct TextRenderSystemStats
{
    std::uint32_t Entities = 0;
    std::uint32_t Items = 0;
    std::uint32_t MissingGlyphs = 0;
    std::uint32_t BufferOverflow = 0;
};

// Extracts every TextComponent with a Transform2D into the caller's render
// item array. The function is allocation free and keeps the text cache warm.
[[nodiscard]] inline TextRenderSystemStats ExtractTextItems(const Ecs::Registry& InRegistry, const TextService& InText,
                                                            const Ui::FontAtlasSet& InFonts,
                                                            std::uint32_t InFontAtlasIndex, float InFontSize,
                                                            const TextLayoutParams& InLayoutParams,
                                                            std::span<Rhi::RenderItem> OutItems,
                                                            const LocalizationTable* InLocalization = nullptr,
                                                            ITextShaper* InShaper = nullptr,
                                                            const TextShapeCache* InShapeCache = nullptr) noexcept
{
    TextRenderSystemStats ScopedStats;
    auto ScopedView = InRegistry.view<TextComponent, Transform2D>();
    for (const Entity ScopedEntity : ScopedView)
    {
        const TextComponent& ScopedText = ScopedView.get<TextComponent>(ScopedEntity);
        const Transform2D& ScopedTransform = ScopedView.get<Transform2D>(ScopedEntity);
        if (const auto* ScopedLocalized = InRegistry.try_get<LocalizedTextComponent>(ScopedEntity);
            ScopedLocalized != nullptr && InLocalization != nullptr && ScopedLocalized->LocalizationKey != 0)
        {
            const std::string_view ScopedLocalizedText = InLocalization->FindVariant(
                ScopedLocalized->LocalizationKey, static_cast<std::int64_t>(ScopedLocalized->PluralCount),
                static_cast<LocalizationGender>(std::min(ScopedLocalized->Gender, 2u)));
            if (!ScopedLocalizedText.empty())
            {
                TextLayoutParams ScopedLocalizedLayout = InLayoutParams;
                ScopedLocalizedLayout.Fonts = &InFonts;
                ScopedLocalizedLayout.FontSize =
                    ScopedLocalized->FontSize > 0.0f ? ScopedLocalized->FontSize : InFontSize;
                if (const Ui::FontAtlas* ScopedBaseFont = InFonts.At(InFontAtlasIndex))
                {
                    ScopedLocalizedLayout.FontId = ScopedBaseFont->FontId();
                }
                TextRenderParams ScopedLocalizedParams;
                ScopedLocalizedParams.Fonts = &InFonts;
                ScopedLocalizedParams.FontAtlasIndex = InFontAtlasIndex;
                ScopedLocalizedParams.FontSize = ScopedLocalizedLayout.FontSize;
                ScopedLocalizedParams.Color = ScopedLocalized->Color;
                ScopedLocalizedParams.Effects.Shadow = (ScopedLocalized->Flags & kTextFlagShadow) != 0;
                ScopedLocalizedParams.Effects.Outline = (ScopedLocalized->Flags & kTextFlagOutline) != 0;
                TextRenderStats ScopedLocalizedStats;
                const std::uint32_t ScopedLocalizedCount = BuildTextRenderItems(
                    ScopedLocalizedText, ScopedTransform.Position, ScopedLocalizedLayout,
                    ScopedLocalizedParams, OutItems.subspan(ScopedStats.Items), &ScopedLocalizedStats);
                ScopedStats.Items += ScopedLocalizedCount;
                ScopedStats.MissingGlyphs += ScopedLocalizedStats.MissingGlyphs;
                ++ScopedStats.Entities;
                continue;
            }
        }
        const TextAssetRef ScopedRef = ScopedText.Ref();
        if (!ScopedRef.IsValid() || (ScopedText.Flags & kTextFlagHidden) != 0)
        {
            continue;
        }
        if (InShapeCache != nullptr)
        {
            if (const auto* ScopedShape = InRegistry.try_get<TextShapeComponent>(ScopedEntity);
                ScopedShape != nullptr && ScopedShape->ShapeGeneration != 0)
            {
                const TextShapeView ScopedShapeView =
                    InShapeCache->View(TextShapeHandle{ScopedShape->ShapeIndex, ScopedShape->ShapeGeneration});
                if (!ScopedShapeView.Glyphs.empty())
                {
                    TextRenderParams ScopedShapedRenderParams;
                    ScopedShapedRenderParams.Fonts = &InFonts;
                    ScopedShapedRenderParams.FontAtlasIndex = InFontAtlasIndex;
                    ScopedShapedRenderParams.FontSize = InFontSize;
                    ScopedShapedRenderParams.Vertical = (ScopedText.Flags & kTextFlagVertical) != 0;
                    ScopedShapedRenderParams.RightToLeft = (ScopedText.Flags & kTextFlagRightToLeft) != 0;
                    ScopedShapedRenderParams.Effects.Shadow = (ScopedText.Flags & kTextFlagShadow) != 0;
                    ScopedShapedRenderParams.Effects.Outline = (ScopedText.Flags & kTextFlagOutline) != 0;
                    // Markup styles travel with the block, so a shaped run keeps
                    // its bold/italic/mono face and its decorations.
                    ScopedShapedRenderParams.Spans = InText.SpansFor(ScopedRef.Asset);
                    ScopedShapedRenderParams.Styles = InText.StylesFor(ScopedRef.Asset);
                    TextRenderStats ScopedShapedStats;
                    const std::uint32_t ScopedShapedCount = BuildShapedTextRenderItems(
                        ScopedShapeView.Glyphs, ScopedShapeView.Lines, ScopedTransform.Position,
                        ScopedShapedRenderParams,
                        OutItems.subspan(ScopedStats.Items), &ScopedShapedStats);
                    ScopedStats.Items += ScopedShapedCount;
                    ScopedStats.MissingGlyphs += ScopedShapedStats.MissingGlyphs;
                    ++ScopedStats.Entities;
                    continue;
                }
            }
        }

        if (InShaper != nullptr && ScopedRef.IsValid())
        {
            const std::string_view ScopedShapedText = InText.Block(ScopedRef);
            TextShapeParams ScopedShapeParams;
            ScopedShapeParams.PixelSize = InFontSize;
            ScopedShapeParams.Vertical = (ScopedText.Flags & kTextFlagVertical) != 0;
            ScopedShapeParams.Direction = (ScopedText.Flags & kTextFlagVertical) != 0
                                              ? TextDirection::TopToBottom
                                              : ((ScopedText.Flags & kTextFlagRightToLeft) != 0
                                                     ? TextDirection::RightToLeft
                                                     : TextDirection::Auto);
            TextShapeResult ScopedShaped;
            if (!ScopedShapedText.empty() &&
                ShapeBidiText(*InShaper, ScopedShapedText, ScopedShapeParams, &ScopedShaped))
            {
                TextLayoutParams ScopedShapedLayoutParams = InLayoutParams;
                ScopedShapedLayoutParams.Fonts = &InFonts;
                ScopedShapedLayoutParams.FontSize = InFontSize;
                ScopedShapedLayoutParams.Vertical = (ScopedText.Flags & kTextFlagVertical) != 0;
                ScopedShapedLayoutParams.RightToLeft = (ScopedText.Flags & kTextFlagRightToLeft) != 0;
                ShapedLayoutResult ScopedShapedLayout;
                if (LayoutShapedGlyphs(ScopedShapedText, ScopedShaped, ScopedShapedLayoutParams, &ScopedShapedLayout))
                {
                    TextRenderParams ScopedShapedRenderParams;
                    ScopedShapedRenderParams.Fonts = &InFonts;
                    ScopedShapedRenderParams.FontAtlasIndex = InFontAtlasIndex;
                    ScopedShapedRenderParams.FontSize = InFontSize;
                    ScopedShapedRenderParams.Vertical = (ScopedText.Flags & kTextFlagVertical) != 0;
                    ScopedShapedRenderParams.RightToLeft = (ScopedText.Flags & kTextFlagRightToLeft) != 0;
                    ScopedShapedRenderParams.Effects.Shadow = (ScopedText.Flags & kTextFlagShadow) != 0;
                    ScopedShapedRenderParams.Effects.Outline = (ScopedText.Flags & kTextFlagOutline) != 0;
                    TextRenderStats ScopedShapedStats;
                    const std::uint32_t ScopedShapedCount =
                        BuildShapedTextRenderItems(ScopedShapedLayout, ScopedShaped, ScopedTransform.Position,
                                                   ScopedShapedRenderParams, OutItems.subspan(ScopedStats.Items),
                                                   &ScopedShapedStats);
                    ScopedStats.Items += ScopedShapedCount;
                    ScopedStats.MissingGlyphs += ScopedShapedStats.MissingGlyphs;
                    ++ScopedStats.Entities;
                    continue;
                }
            }
        }

        TextLayoutParams ScopedLayoutParams = InLayoutParams;
        ScopedLayoutParams.Fonts = &InFonts;
        ScopedLayoutParams.FontSize = InFontSize;
        ScopedLayoutParams.RightToLeft = (ScopedText.Flags & kTextFlagRightToLeft) != 0;
        ScopedLayoutParams.Spans = InText.SpansFor(ScopedRef.Asset);
        ScopedLayoutParams.Styles = InText.StylesFor(ScopedRef.Asset);
        ScopedLayoutParams.Inlines = InText.InlinesFor(ScopedRef.Asset);
        if (const Ui::FontAtlas* ScopedBaseFont = InFonts.At(InFontAtlasIndex))
        {
            ScopedLayoutParams.FontId = ScopedBaseFont->FontId();
        }
        Vector2D ScopedOrigin = ScopedTransform.Position;
        if ((ScopedText.Flags & (kTextFlagCenterX | kTextFlagRightX)) != 0)
        {
            const TextLayoutResult* ScopedLayout = InText.CachedLayout(ScopedRef, ScopedLayoutParams);
            if (ScopedLayout != nullptr)
            {
                float ScopedWidth = 0.0f;
                for (std::uint32_t ScopedLine = 0; ScopedLine < ScopedLayout->Count; ++ScopedLine)
                {
                    ScopedWidth = std::max(ScopedWidth, ScopedLayout->Lines[ScopedLine].Width);
                }
                ScopedOrigin.X -= (ScopedText.Flags & kTextFlagCenterX) != 0 ? ScopedWidth * 0.5f : ScopedWidth;
            }
        }
        TextRenderParams ScopedParams;
        ScopedParams.Fonts = &InFonts;
        ScopedParams.FontAtlasIndex = InFontAtlasIndex;
        ScopedParams.FontSize = InFontSize;
        ScopedParams.RightToLeft = (ScopedText.Flags & kTextFlagRightToLeft) != 0;
        ScopedParams.Spans = InText.SpansFor(ScopedRef.Asset);
        ScopedParams.Styles = InText.StylesFor(ScopedRef.Asset);
        ScopedParams.Inlines = InText.InlinesFor(ScopedRef.Asset);
        ScopedParams.Effects.Shadow = (ScopedText.Flags & kTextFlagShadow) != 0;
        ScopedParams.Effects.Outline = (ScopedText.Flags & kTextFlagOutline) != 0;
        if (const auto* ScopedEffect = InRegistry.try_get<TextEffectComponent>(ScopedEntity))
        {
            ScopedParams.Effects.ShadowColor = ScopedEffect->ShadowColor;
            ScopedParams.Effects.OutlineColor = ScopedEffect->OutlineColor;
            ScopedParams.Effects.ShadowOffset = ScopedEffect->ShadowOffset;
            ScopedParams.Effects.OutlineWidth = ScopedEffect->OutlineWidth;
        }
        if (const auto* ScopedCondition = InRegistry.try_get<TextConditionComponent>(ScopedEntity))
        {
            if (const TextStyledBlock* ScopedVariant =
                    InText.CachedMarkupVariant(ScopedRef.Asset, ScopedCondition->Mask))
            {
                ScopedParams.Inlines = {};
                TextLayoutParams ScopedVariantLayout = ScopedLayoutParams;
                ScopedVariantLayout.Spans =
                    std::span<const TextSpan>(ScopedVariant->Spans.data(), ScopedVariant->SpanCount);
                ScopedVariantLayout.Styles =
                    std::span<const TextStyle>(ScopedVariant->Styles.data(), ScopedVariant->StyleCount);
                ScopedVariantLayout.Inlines =
                    std::span<const TextInlineObject>(ScopedVariant->Inlines.data(), ScopedVariant->InlineCount);
                TextRenderStats ScopedVariantStats;
                const std::uint32_t ScopedVariantCount = BuildStyledTextRenderItems(
                    *ScopedVariant, ScopedOrigin, ScopedVariantLayout, ScopedParams,
                    OutItems.subspan(ScopedStats.Items), &ScopedVariantStats);
                ScopedStats.Items += ScopedVariantCount;
                ScopedStats.MissingGlyphs += ScopedVariantStats.MissingGlyphs;
                ++ScopedStats.Entities;
                continue;
            }
        }
        if (const auto* ScopedFormat = InRegistry.try_get<TextFormatComponent>(ScopedEntity);
            ScopedFormat != nullptr && ScopedFormat->ValueCount > 0)
        {
            const std::array<const BigNum::Int128*, 4> ScopedValues{&ScopedFormat->Value0, &ScopedFormat->Value1,
                                                                   &ScopedFormat->Value2, &ScopedFormat->Value3};
            const std::array<const std::uint32_t*, 4> ScopedSpecs{&ScopedFormat->Spec0, &ScopedFormat->Spec1,
                                                                  &ScopedFormat->Spec2, &ScopedFormat->Spec3};
            const std::uint32_t ScopedArgumentCount = std::min(ScopedFormat->ValueCount, 4u);
            std::array<TextFormatArg, 4> ScopedArgs{};
            for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedArgumentCount; ++ScopedIndex)
            {
                ScopedArgs[ScopedIndex] =
                    MakeBigIntArg(ScopedValues[ScopedIndex], UnpackFormatSpec(*ScopedSpecs[ScopedIndex]));
            }
            std::uint64_t ScopedFormatKey = 1469598103934665603ull;
            const auto ScopedMix = [&ScopedFormatKey](std::uint32_t InValue) noexcept
            {
                ScopedFormatKey = (ScopedFormatKey ^ InValue) * 1099511628211ull;
            };
            for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedArgumentCount; ++ScopedIndex)
            {
                for (const std::uint32_t ScopedLimb : ScopedValues[ScopedIndex]->Limbs)
                {
                    ScopedMix(ScopedLimb);
                }
                ScopedMix(*ScopedSpecs[ScopedIndex]);
            }
            ScopedMix(ScopedArgumentCount);
            std::array<char, 8192> ScopedFormatted{};
            std::string_view ScopedFormattedText;
            bool ScopedHasFormatted = InText.FormatCached(
                ScopedRef, ScopedFormatKey, std::span<const TextFormatArg>(ScopedArgs.data(), ScopedArgumentCount),
                &ScopedFormattedText);
            if (!ScopedHasFormatted)
            {
                std::size_t ScopedFormattedSize = 0;
                if (InText.FormatBlock(ScopedRef,
                                       std::span<const TextFormatArg>(ScopedArgs.data(), ScopedArgumentCount),
                                       std::span<char>(ScopedFormatted), &ScopedFormattedSize) == TextFormatStatus::Ok)
                {
                    ScopedFormattedText = std::string_view(ScopedFormatted.data(), ScopedFormattedSize);
                    ScopedHasFormatted = true;
                }
            }
            if (ScopedHasFormatted)
            {
                ScopedParams.Spans = {};
                ScopedParams.Styles = {};
                ScopedParams.Inlines = {};
                TextRenderStats ScopedFormattedStats;
                const TextLayoutResult* ScopedFormattedLayout =
                    InText.CachedFormattedLayout(ScopedFormattedText, ScopedLayoutParams);
                const std::uint32_t ScopedFormattedCount =
                    ScopedFormattedLayout != nullptr
                        ? BuildTextRenderItems(ScopedFormattedText, *ScopedFormattedLayout, ScopedOrigin, ScopedParams,
                                               OutItems.subspan(ScopedStats.Items), &ScopedFormattedStats)
                        : BuildTextRenderItems(ScopedFormattedText, ScopedOrigin, ScopedLayoutParams, ScopedParams,
                                               OutItems.subspan(ScopedStats.Items), &ScopedFormattedStats);
                ScopedStats.Items += ScopedFormattedCount;
                ScopedStats.MissingGlyphs += ScopedFormattedStats.MissingGlyphs;
                ++ScopedStats.Entities;
                continue;
            }
        }
        TextRenderStats ScopedTextStats;
        const std::uint32_t ScopedCount =
            BuildTextRenderItems(InText, ScopedRef, ScopedOrigin, ScopedLayoutParams, ScopedParams,
                                 OutItems.subspan(ScopedStats.Items), &ScopedTextStats);
        ScopedStats.Items += ScopedCount;
        ScopedStats.MissingGlyphs += ScopedTextStats.MissingGlyphs;
        ++ScopedStats.Entities;
        if (ScopedStats.Items >= OutItems.size() && ScopedCount == 0)
        {
            ++ScopedStats.BufferOverflow;
            break;
        }
    }
    return ScopedStats;
}
}
