// FemboyEngine2D - text block to render item extraction test.
#include "Engine/Assets/Localization.hpp"
#include "Engine/Assets/LocalizationCompiler.hpp"
#include "Engine/Core/AssetId.hpp"
#include "Engine/ECS/Components.hpp"
#include "Engine/RHI/RenderItem.hpp"
#include "Engine/Text/TextMarkup.hpp"
#include "Engine/Text/TextRender.hpp"
#include "Engine/Text/TextRenderSystem.hpp"

#include <array>
#include <cstdio>
#include <cstring>
#include <memory>
#include <span>
#include <vector>

namespace
{
int GFailures = 0;

void Check(bool InCondition, const char* InMessage)
{
    if (!InCondition)
    {
        std::fprintf(stderr, "FAIL: %s\n", InMessage);
        ++GFailures;
    }
}

[[nodiscard]] std::vector<std::byte> MakeFontAtlas(Fby::Ui::FontStyle InStyle = Fby::Ui::FontStyle::Regular,
                                                   std::string_view InCodepoints = " 0123456789A",
                                                   std::uint32_t InFontId = 0)
{
    using namespace Fby::Ui;
    constexpr std::uint32_t ScopedWidth = 8;
    constexpr std::uint32_t ScopedHeight = 8;
    constexpr std::uint32_t ScopedGlyphs = 12;
    const std::size_t ScopedPixelBytes = static_cast<std::size_t>(ScopedWidth) * ScopedHeight * 4u;
    std::vector<std::byte> ScopedBytes(
        sizeof(FontAtlasHeader) + ScopedGlyphs * sizeof(GlyphRecord) + ScopedPixelBytes, std::byte{0});

    FontAtlasHeader ScopedHeader{};
    std::memcpy(ScopedHeader.magic, kFontAtlasMagic, sizeof(ScopedHeader.magic));
    ScopedHeader.version = kFontAtlasVersion;
    ScopedHeader.headerBytes = sizeof(FontAtlasHeader);
    ScopedHeader.glyphCount = ScopedGlyphs;
    ScopedHeader.atlasWidth = ScopedWidth;
    ScopedHeader.atlasHeight = ScopedHeight;
    ScopedHeader.pixelFormat = static_cast<std::uint32_t>(FontPixelFormat::Rgba8);
    ScopedHeader.glyphTableOffset = sizeof(FontAtlasHeader);
    ScopedHeader.pixelOffset =
        static_cast<std::uint32_t>(sizeof(FontAtlasHeader) + ScopedGlyphs * sizeof(GlyphRecord));
    ScopedHeader.lineHeight = 8.0f;
    ScopedHeader.ascent = 6.0f;
    ScopedHeader.descent = 2.0f;
    ScopedHeader.fontId = InFontId;
    ScopedHeader.reserved = static_cast<std::uint32_t>(InStyle);
    std::memcpy(ScopedBytes.data(), &ScopedHeader, sizeof(ScopedHeader));

    std::array<GlyphRecord, ScopedGlyphs> ScopedGlyphsRecords{};
    ScopedGlyphsRecords[0].codepoint = ' ';
    ScopedGlyphsRecords[0].u1 = 0.5f;
    ScopedGlyphsRecords[0].v1 = 1.0f;
    const float ScopedAdvance = InStyle == Fby::Ui::FontStyle::Bold ? 6.0f : 4.0f;
    ScopedGlyphsRecords[0].advance = ScopedAdvance;
    ScopedGlyphsRecords[0].width = ScopedAdvance;
    ScopedGlyphsRecords[0].height = 8.0f;
    for (std::uint32_t ScopedIndex = 1; ScopedIndex < ScopedGlyphs; ++ScopedIndex)
    {
        ScopedGlyphsRecords[ScopedIndex] = ScopedGlyphsRecords[0];
        ScopedGlyphsRecords[ScopedIndex].codepoint =
            static_cast<std::uint32_t>(InCodepoints[ScopedIndex]);
        ScopedGlyphsRecords[ScopedIndex].u0 = 0.5f;
        ScopedGlyphsRecords[ScopedIndex].u1 = 1.0f;
        ScopedGlyphsRecords[ScopedIndex].bearingY = 6.0f;
        ScopedGlyphsRecords[ScopedIndex].glyphId =
            static_cast<std::uint32_t>(InCodepoints[ScopedIndex]);
    }
    std::memcpy(ScopedBytes.data() + sizeof(FontAtlasHeader), ScopedGlyphsRecords.data(),
                sizeof(ScopedGlyphsRecords));

    std::byte* ScopedPixels = ScopedBytes.data() + ScopedHeader.pixelOffset;
    for (std::size_t ScopedTexel = 0; ScopedTexel < static_cast<std::size_t>(ScopedWidth) * ScopedHeight; ++ScopedTexel)
    {
        ScopedPixels[ScopedTexel * 4 + 0] = std::byte{0xFF};
        ScopedPixels[ScopedTexel * 4 + 1] = std::byte{0xFF};
        ScopedPixels[ScopedTexel * 4 + 2] = std::byte{0xFF};
        ScopedPixels[ScopedTexel * 4 + 3] = std::byte{0xFF};
    }
    return ScopedBytes;
}
[[nodiscard]] std::vector<std::byte> MakeSdfFontAtlas()
{
    using namespace Fby::Ui;
    constexpr std::uint32_t ScopedWidth = 8;
    constexpr std::uint32_t ScopedHeight = 8;
    constexpr std::uint32_t ScopedGlyphs = 2;
    const std::size_t ScopedPixelBytes = static_cast<std::size_t>(ScopedWidth) * ScopedHeight;
    std::vector<std::byte> ScopedBytes(
        sizeof(FontAtlasHeader) + ScopedGlyphs * sizeof(GlyphRecord) + ScopedPixelBytes, std::byte{0});

    FontAtlasHeader ScopedHeader{};
    std::memcpy(ScopedHeader.magic, kFontAtlasMagic, sizeof(ScopedHeader.magic));
    ScopedHeader.version = kFontAtlasVersion;
    ScopedHeader.headerBytes = sizeof(FontAtlasHeader);
    ScopedHeader.glyphCount = ScopedGlyphs;
    ScopedHeader.atlasWidth = ScopedWidth;
    ScopedHeader.atlasHeight = ScopedHeight;
    ScopedHeader.pixelFormat = static_cast<std::uint32_t>(FontPixelFormat::SdfR8);
    ScopedHeader.glyphTableOffset = sizeof(FontAtlasHeader);
    ScopedHeader.pixelOffset =
        static_cast<std::uint32_t>(sizeof(FontAtlasHeader) + ScopedGlyphs * sizeof(GlyphRecord));
    ScopedHeader.lineHeight = 8.0f;
    ScopedHeader.ascent = 6.0f;
    ScopedHeader.descent = 2.0f;
    std::memcpy(ScopedBytes.data(), &ScopedHeader, sizeof(ScopedHeader));

    std::array<GlyphRecord, ScopedGlyphs> ScopedGlyphsRecords{};
    ScopedGlyphsRecords[0].codepoint = ' ';
    ScopedGlyphsRecords[0].u1 = 0.5f;
    ScopedGlyphsRecords[0].v1 = 1.0f;
    ScopedGlyphsRecords[0].advance = 4.0f;
    ScopedGlyphsRecords[0].width = 4.0f;
    ScopedGlyphsRecords[0].height = 8.0f;
    ScopedGlyphsRecords[1] = ScopedGlyphsRecords[0];
    ScopedGlyphsRecords[1].codepoint = 'A';
    ScopedGlyphsRecords[1].u0 = 0.5f;
    ScopedGlyphsRecords[1].u1 = 1.0f;
    ScopedGlyphsRecords[1].bearingY = 6.0f;
    std::memcpy(ScopedBytes.data() + sizeof(FontAtlasHeader), ScopedGlyphsRecords.data(),
                sizeof(ScopedGlyphsRecords));

    std::byte* ScopedPixels = ScopedBytes.data() + ScopedHeader.pixelOffset;
    for (std::size_t ScopedTexel = 0; ScopedTexel < static_cast<std::size_t>(ScopedWidth) * ScopedHeight;
         ++ScopedTexel)
    {
        ScopedPixels[ScopedTexel] = std::byte{128};
    }
    return ScopedBytes;
}
}

int main()
{
    using namespace Fby;

    Ui::FontAtlasSet ScopedFonts;
    const std::vector<std::byte> ScopedAtlas = MakeFontAtlas();
    Check(ScopedFonts.Add(ScopedAtlas), "synthetic font atlas loads");
    Check(ScopedFonts.Add(MakeFontAtlas(Ui::FontStyle::Bold)), "synthetic bold atlas loads");
    Check(ScopedFonts.LayerForFontId(0) == 1, "synthetic font atlas has a layer");
    Check(ScopedFonts.FindByFontIdAndStyle(0, Ui::FontStyle::Bold) != nullptr, "bold style slot is addressable");
    Check(ScopedFonts.LayerForFontIdAndStyle(0, Ui::FontStyle::Bold) == 2, "bold style layer");

    auto ScopedText = std::make_unique<Text::TextService>();
    const AssetId ScopedAsset = AssetId::FromPath("text/render.txt", AssetKind::Text);
    Check(ScopedText->AddAsset(ScopedAsset, "AA"), "text render asset stores");
    const Text::TextAssetRef ScopedRef = ScopedText->MakeRef(ScopedAsset);

    Text::TextLayoutParams ScopedLayoutParams;
    ScopedLayoutParams.MaxWidth = 64.0f;
    ScopedLayoutParams.Advance = 8.0f;
    Text::TextRenderParams ScopedRenderParams;
    ScopedRenderParams.Fonts = &ScopedFonts;
    ScopedRenderParams.FontSize = 16.0f;
    std::array<Rhi::RenderItem, 8> ScopedItems{};
    Text::TextRenderStats ScopedStats;
    const std::uint32_t ScopedCount = Text::BuildTextRenderItems(
        *ScopedText, ScopedRef, Vector2D{0.0f, 0.0f}, ScopedLayoutParams, ScopedRenderParams,
        std::span<Rhi::RenderItem>(ScopedItems), &ScopedStats);
    Check(ScopedCount == 2, "text render emits one quad per glyph");
    Check(ScopedStats.Items == 2 && ScopedStats.MissingGlyphs == 0, "text render stats");
    Check(ScopedItems[0].layer == 1, "text render uses the font layer");
    Check(ScopedItems[0].uvMax.X > ScopedItems[0].uvMin.X, "text render carries glyph UVs");
    Check(ScopedItems[1].position.X > ScopedItems[0].position.X, "text render advances the pen");

    std::array<Text::TextStyle, 2> ScopedStyles{};
    ScopedStyles[0].Color = Color32::FromBytes(0xFF, 0, 0, 0xFF);
    ScopedStyles[1].Color = Color32::FromBytes(0, 0, 0xFF, 0xFF);
    std::array<Text::TextSpan, 2> ScopedSpans{};
    ScopedSpans[0] = Text::TextSpan{0, 1, 0, 0};
    ScopedSpans[1] = Text::TextSpan{1, 2, 1, 0};
    ScopedRenderParams.Spans = std::span<const Text::TextSpan>(ScopedSpans);
    ScopedRenderParams.Styles = std::span<const Text::TextStyle>(ScopedStyles);
    const std::uint32_t ScopedStyledCount = Text::BuildTextRenderItems(
        *ScopedText, ScopedRef, Vector2D{0.0f, 0.0f}, ScopedLayoutParams, ScopedRenderParams,
        std::span<Rhi::RenderItem>(ScopedItems), &ScopedStats);
    Check(ScopedStyledCount == 2, "styled text render emits both glyphs");
    Check(ScopedItems[0].color == ScopedStyles[0].Color.rgba, "first span color");
    Check(ScopedItems[1].color == ScopedStyles[1].Color.rgba, "second span color");
    ScopedRenderParams.Spans = {};
    ScopedRenderParams.Styles = {};

    std::array<Text::TextStyle, 1> ScopedBoldStyles{};
    ScopedBoldStyles[0].Flags = static_cast<std::uint16_t>(Text::TextStyleFlags::Bold);
    std::array<Text::TextSpan, 1> ScopedBoldSpans{};
    ScopedBoldSpans[0] = Text::TextSpan{0, 2, 0, 0};
    Text::TextLayoutParams ScopedBoldLayout = ScopedLayoutParams;
    ScopedBoldLayout.Fonts = &ScopedFonts;
    ScopedBoldLayout.FontId = 0;
    ScopedBoldLayout.FontSize = 16.0f;
    ScopedBoldLayout.Spans = std::span<const Text::TextSpan>(ScopedBoldSpans);
    ScopedBoldLayout.Styles = std::span<const Text::TextStyle>(ScopedBoldStyles);
    Text::TextLayoutResult ScopedBoldResult;
    Check(Text::LayoutText("AA", ScopedBoldLayout, &ScopedBoldResult), "bold text lays out");
    Check(ScopedBoldResult.Lines[0].Width == 24.0f, "bold text uses bold metrics");
    ScopedRenderParams.Spans = ScopedBoldLayout.Spans;
    ScopedRenderParams.Styles = ScopedBoldLayout.Styles;
    std::array<Rhi::RenderItem, 8> ScopedBoldItems{};
    const std::uint32_t ScopedBoldCount = Text::BuildTextRenderItems(
        *ScopedText, ScopedRef, Vector2D{0.0f, 0.0f}, ScopedBoldLayout, ScopedRenderParams,
        std::span<Rhi::RenderItem>(ScopedBoldItems), &ScopedStats);
    Check(ScopedBoldCount == 2, "bold text renders two glyphs");
    Check(ScopedBoldItems[0].layer == 2, "bold text selects the bold atlas layer");
    ScopedRenderParams.Spans = {};
    ScopedRenderParams.Styles = {};

    std::array<Text::TextStyle, 1> ScopedSizeStyles{};
    ScopedSizeStyles[0].FontSize = 24;
    Text::TextLayoutParams ScopedSizeLayout = ScopedBoldLayout;
    ScopedSizeLayout.Styles = std::span<const Text::TextStyle>(ScopedSizeStyles);
    Text::TextLayoutResult ScopedSizeResult;
    Check(Text::LayoutText("AA", ScopedSizeLayout, &ScopedSizeResult), "sized text lays out");
    Check(ScopedSizeResult.Lines[0].Width == 24.0f, "sized text scales advance");
    Check(ScopedSizeResult.Lines[0].Height == 24.0f, "sized text sets line height");
    ScopedRenderParams.Spans = ScopedSizeLayout.Spans;
    ScopedRenderParams.Styles = ScopedSizeLayout.Styles;
    std::array<Rhi::RenderItem, 8> ScopedSizeItems{};
    const std::uint32_t ScopedSizeCount = Text::BuildTextRenderItems(
        *ScopedText, ScopedRef, Vector2D{0.0f, 0.0f}, ScopedSizeLayout, ScopedRenderParams,
        std::span<Rhi::RenderItem>(ScopedSizeItems), &ScopedStats);
    Check(ScopedSizeCount == 2, "sized text renders two glyphs");
    Check(ScopedSizeItems[0].size.Y == 24.0f, "sized text scales glyph height");
    ScopedRenderParams.Spans = {};
    ScopedRenderParams.Styles = {};

    Ui::FontAtlasSet ScopedSdfFonts;
    Check(ScopedSdfFonts.Add(MakeSdfFontAtlas()), "synthetic SDF atlas loads");
    Text::TextRenderParams ScopedSdfParams;
    ScopedSdfParams.Fonts = &ScopedSdfFonts;
    ScopedSdfParams.FontSize = 16.0f;
    std::array<Rhi::RenderItem, 8> ScopedSdfItems{};
    const std::uint32_t ScopedSdfCount = Text::BuildTextRenderItems(
        *ScopedText, ScopedRef, Vector2D{0.0f, 0.0f}, ScopedLayoutParams, ScopedSdfParams,
        std::span<Rhi::RenderItem>(ScopedSdfItems), &ScopedStats);
    Check(ScopedSdfCount == 2, "SDF text emits glyphs");
    Check((ScopedSdfItems[0].flags & Rhi::kRenderFlagSdfText) != 0, "SDF text sets the shader flag");

    Text::TextShapeResult ScopedShaped;
    ScopedShaped.Count = 1;
    ScopedShaped.Glyphs[0].GlyphId = static_cast<std::uint32_t>('A');
    ScopedShaped.Glyphs[0].Cluster = 0;
    ScopedShaped.Glyphs[0].AdvanceX = 6.0f;
    std::array<Rhi::RenderItem, 4> ScopedShapedItems{};
    const std::uint32_t ScopedShapedCount = Text::BuildShapedTextRenderItems(
        ScopedShaped, Vector2D{0.0f, 0.0f}, ScopedRenderParams,
        std::span<Rhi::RenderItem>(ScopedShapedItems), &ScopedStats);
    Check(ScopedShapedCount == 1 && ScopedShapedItems[0].layer == 1, "shaped glyph renders through the glyph-id atlas");

    ScopedSdfParams.Effects.Glow = true;
    ScopedSdfParams.Effects.GlowRadius = 2.0f;
    ScopedSdfParams.Effects.Outline = true;
    ScopedSdfParams.Effects.OutlineWidth = 1.0f;
    std::array<Rhi::RenderItem, 8> ScopedSdfEffectItems{};
    const std::uint32_t ScopedSdfEffectCount = Text::BuildTextRenderItems(
        *ScopedText, ScopedRef, Vector2D{0.0f, 0.0f}, ScopedLayoutParams, ScopedSdfParams,
        std::span<Rhi::RenderItem>(ScopedSdfEffectItems), &ScopedStats);
    Check(ScopedSdfEffectCount == 6, "SDF glow and outline use one shader quad per effect");
    Check((ScopedSdfEffectItems[0].flags & Rhi::kRenderFlagSdfGlow) != 0, "SDF glow flag");
    Check((ScopedSdfEffectItems[1].flags & Rhi::kRenderFlagSdfOutline) != 0, "SDF outline flag");
    ScopedSdfParams.Effects = {};

    Ui::FontAtlasSet ScopedFallbackFonts;
    Check(ScopedFallbackFonts.Add(MakeFontAtlas(Ui::FontStyle::Regular, " 0123456789A", 0)),
          "fallback primary atlas loads");
    Check(ScopedFallbackFonts.Add(MakeFontAtlas(Ui::FontStyle::Regular, " 0123456789B", 1)),
          "fallback secondary atlas loads");
    Check(ScopedFallbackFonts.AddFallbackFontId(1), "fallback font id registers");
    Text::TextRenderParams ScopedFallbackParams;
    ScopedFallbackParams.Fonts = &ScopedFallbackFonts;
    ScopedFallbackParams.FontSize = 16.0f;
    std::array<Rhi::RenderItem, 4> ScopedFallbackItems{};
    const std::uint32_t ScopedFallbackCount = Text::BuildTextRenderItems(
        "B", Vector2D{0.0f, 0.0f}, ScopedLayoutParams, ScopedFallbackParams,
        std::span<Rhi::RenderItem>(ScopedFallbackItems), &ScopedStats);
    Check(ScopedFallbackCount == 1 && ScopedStats.MissingGlyphs == 0, "fallback font resolves a missing glyph");
    Check(ScopedFallbackItems[0].layer == 2, "fallback glyph uses its own layer");

    const AssetId ScopedMarkupAsset = AssetId::FromPath("text/markup.txt", AssetKind::Text);
    Check(ScopedText->AddMarkupAsset(ScopedMarkupAsset, "[color=#FF0000]A[/color]A"), "markup asset stores");
    const Text::TextAssetRef ScopedMarkupRef = ScopedText->MakeRef(ScopedMarkupAsset);
    ScopedRenderParams.Spans = ScopedText->SpansFor(ScopedMarkupAsset);
    ScopedRenderParams.Styles = ScopedText->StylesFor(ScopedMarkupAsset);
    std::array<Rhi::RenderItem, 8> ScopedServiceItems{};
    const std::uint32_t ScopedServiceCount = Text::BuildTextRenderItems(
        *ScopedText, ScopedMarkupRef, Vector2D{0.0f, 0.0f}, ScopedLayoutParams, ScopedRenderParams,
        std::span<Rhi::RenderItem>(ScopedServiceItems), &ScopedStats);
    Check(ScopedServiceCount == 2, "markup service asset renders two glyphs");
    Check(ScopedServiceItems[0].color == Color32::FromBytes(0xFF, 0, 0, 0xFF).rgba, "markup service first color");
    Check(ScopedServiceItems[1].color == Color32::White().rgba, "markup service default color");
    Check(ScopedText->ActionsFor(ScopedMarkupAsset).empty(), "plain color markup has no actions");
    const AssetId ScopedLinkAsset = AssetId::FromPath("text/link.txt", AssetKind::Text);
    Check(ScopedText->AddMarkupAsset(ScopedLinkAsset, "[link=3]A[/link]A"), "link markup asset stores");
    Check(ScopedText->ActionsFor(ScopedLinkAsset).size() == 1, "services exposes link actions");
    ScopedRenderParams.Spans = {};
    ScopedRenderParams.Styles = {};

    const AssetId ScopedMissingAsset = AssetId::FromPath("text/missing.txt", AssetKind::Text);
    Check(ScopedText->AddAsset(ScopedMissingAsset, "AZ"), "missing glyph asset stores");
    const Text::TextAssetRef ScopedMissingRef = ScopedText->MakeRef(ScopedMissingAsset);
    const std::uint32_t ScopedMissingCount = Text::BuildTextRenderItems(
        *ScopedText, ScopedMissingRef, Vector2D{0.0f, 0.0f}, ScopedLayoutParams, ScopedRenderParams,
        std::span<Rhi::RenderItem>(ScopedItems), &ScopedStats);
    Check(ScopedMissingCount == 1, "missing glyphs are skipped");
    Check(ScopedStats.MissingGlyphs == 1, "missing glyph count");

    Check(Text::BuildTextRenderItems(*ScopedText, ScopedRef, Vector2D{}, ScopedLayoutParams, ScopedRenderParams,
                                     std::span<Rhi::RenderItem>(), nullptr) == 0,
          "empty output buffer reports zero items");

    entt::registry ScopedRegistry;
    const Entity ScopedEntity = ScopedRegistry.create();
    TextComponent ScopedComponent;
    ScopedComponent.SetRef(ScopedRef);
    ScopedRegistry.emplace<TextComponent>(ScopedEntity, ScopedComponent);
    ScopedRegistry.emplace<Transform2D>(ScopedEntity, Transform2D{Vector2D{10.0f, 20.0f}, Vector2D{1.0f, 1.0f}, 0.0f});
    std::array<Rhi::RenderItem, 8> ScopedSystemItems{};
    const Text::TextRenderSystemStats ScopedSystemStats =
        Text::ExtractTextItems(ScopedRegistry, *ScopedText, ScopedFonts, 0, 16.0f, ScopedLayoutParams,
                               std::span<Rhi::RenderItem>(ScopedSystemItems));
    Check(ScopedSystemStats.Entities == 1, "text system extracts one entity");
    Check(ScopedSystemStats.Items == 2, "text system emits two items");
    Text::CodepointShaper ScopedShaper;
    std::array<Rhi::RenderItem, 8> ScopedShapedSystemItems{};
    const Text::TextRenderSystemStats ScopedShapedSystemStats = Text::ExtractTextItems(
        ScopedRegistry, *ScopedText, ScopedFonts, 0, 16.0f, ScopedLayoutParams,
        std::span<Rhi::RenderItem>(ScopedShapedSystemItems), nullptr, &ScopedShaper);
    Check(ScopedShapedSystemStats.Items == 2 && ScopedShapedSystemStats.Entities == 1,
          "text system uses the optional shaped path");
    auto ScopedShapeCache = std::make_unique<Text::TextShapeCache>();
    Text::TextShapeKey ScopedShapeKey;
    ScopedShapeKey.TextHash = Text::TextShapeCache::HashText("AA");
    ScopedShapeKey.TextLength = 2;
    ScopedShapeKey.PixelSize = 16.0f;
    const Text::TextShapeHandle ScopedShapeHandle =
        ScopedShapeCache->Insert(ScopedShaper, "AA", ScopedShapeKey, ScopedLayoutParams);
    Check(ScopedShapeHandle.IsValid(), "ECS shape cache entry inserts");
    ScopedRegistry.emplace<TextShapeComponent>(
        ScopedEntity, TextShapeComponent{ScopedShapeHandle.Index, ScopedShapeHandle.Generation});
    std::array<Rhi::RenderItem, 8> ScopedCachedSystemItems{};
    const Text::TextRenderSystemStats ScopedCachedSystemStats = Text::ExtractTextItems(
        ScopedRegistry, *ScopedText, ScopedFonts, 0, 16.0f, ScopedLayoutParams,
        std::span<Rhi::RenderItem>(ScopedCachedSystemItems), nullptr, nullptr, ScopedShapeCache.get());
    Check(ScopedCachedSystemStats.Items == 2 && ScopedCachedSystemStats.Entities == 1,
          "text system consumes a DOD shape-cache handle");
    Check(ScopedSystemItems[0].position.X >= 10.0f, "text system applies the transform");

    ScopedRegistry.get<TextComponent>(ScopedEntity).SetRef(ScopedRef, kTextFlagShadow | kTextFlagOutline);
    TextEffectComponent ScopedEffect;
    ScopedEffect.ShadowColor = Color32::FromBytes(0, 0, 0xFF, 0xFF);
    ScopedEffect.OutlineColor = Color32::FromBytes(0xFF, 0, 0, 0xFF);
    ScopedEffect.ShadowOffset = Vector2D{5.0f, 6.0f};
    ScopedEffect.OutlineWidth = 2.0f;
    ScopedRegistry.emplace<TextEffectComponent>(ScopedEntity, ScopedEffect);
    std::array<Rhi::RenderItem, 16> ScopedEffectSystemItems{};
    const Text::TextRenderSystemStats ScopedEffectSystemStats =
        Text::ExtractTextItems(ScopedRegistry, *ScopedText, ScopedFonts, 0, 16.0f, ScopedLayoutParams,
                               std::span<Rhi::RenderItem>(ScopedEffectSystemItems));
    Check(ScopedEffectSystemStats.Items == 6, "text component flags enable shadow and shader outline");
    Check(ScopedEffectSystemItems[0].color == ScopedEffect.ShadowColor.rgba, "text effect shadow color");
    Check(ScopedEffectSystemItems[1].color == ScopedEffect.OutlineColor.rgba, "text effect outline color");
    ScopedRegistry.get<TextComponent>(ScopedEntity).SetRef(ScopedRef);

    std::string ScopedLocalizationError;
    const std::vector<std::byte> ScopedLocalizationBytes = Fby::Assets::CompileLocalization(
        "default = \"en\"\n[en]\n42 = \"A\"\n", &ScopedLocalizationError);
    Fby::LocalizationTable ScopedLocalization =
        Fby::LocalizationTable::Open(ScopedLocalizationBytes, &ScopedLocalizationError);
    Check(ScopedLocalization.IsValid(), "localization table opens");
    entt::registry ScopedLocalizedRegistry;
    const Entity ScopedLocalizedEntity = ScopedLocalizedRegistry.create();
    ScopedLocalizedRegistry.emplace<TextComponent>(ScopedLocalizedEntity, TextComponent{});
    ScopedLocalizedRegistry.emplace<Transform2D>(
        ScopedLocalizedEntity, Transform2D{Vector2D{10.0f, 20.0f}, Vector2D{1.0f, 1.0f}, 0.0f});
    ScopedLocalizedRegistry.emplace<LocalizedTextComponent>(
        ScopedLocalizedEntity, LocalizedTextComponent{42, 0.0f, Color32::White(), 0});
    std::array<Rhi::RenderItem, 8> ScopedLocalizedItems{};
    const Text::TextRenderSystemStats ScopedLocalizedStats = Text::ExtractTextItems(
        ScopedLocalizedRegistry, *ScopedText, ScopedFonts, 0, 16.0f, ScopedLayoutParams,
        std::span<Rhi::RenderItem>(ScopedLocalizedItems), &ScopedLocalization);
    Check(ScopedLocalizedStats.Items == 1 && ScopedLocalizedStats.Entities == 1,
          "localized TextComponent renders the table string");

    const std::vector<std::byte> ScopedVariantBytes = Fby::Assets::CompileLocalization(
        "default = \"en\"\n"
        "[en]\n"
        "1001 = \"items\"\n"
        "1002 = \"item\"\n"
        "1009 = \"his\"\n"
        "[ru]\n"
        "1001 = \"items-many\"\n"
        "1002 = \"item-one\"\n"
        "1003 = \"item-few\"\n"
        "1004 = \"items-many\"\n"
        "1009 = \"his-neutral\"\n"
        "1010 = \"his-male\"\n",
        &ScopedLocalizationError);
    Fby::LocalizationTable ScopedVariants =
        Fby::LocalizationTable::Open(ScopedVariantBytes, &ScopedLocalizationError);
    Check(ScopedVariants.IsValid(), "localization variants table opens");
    Check(ScopedVariants.FindVariant(1001, 1, Fby::LocalizationGender::Neutral) == "item",
          "english plural one");
    Check(ScopedVariants.FindVariant(1001, 2, Fby::LocalizationGender::Neutral) == "items",
          "english plural other");
    Check(ScopedVariants.SelectLocale("ru"), "russian locale selects");
    Check(ScopedVariants.FindVariant(1001, 2, Fby::LocalizationGender::Neutral) == "item-few",
          "russian plural few");
    Check(ScopedVariants.FindVariant(1001, 5, Fby::LocalizationGender::Neutral) == "items-many",
          "russian plural many");
    Check(ScopedVariants.FindVariant(1001, 0, Fby::LocalizationGender::Male) == "his-male",
          "gender fallback resolves");

    auto ScopedHitBlock = std::make_unique<Text::TextStyledBlock>();
    Check(Text::CompileTextMarkup("[button=7:20:10]", ScopedHitBlock.get()) == Text::TextMarkupStatus::Ok,
          "inline button compiles for hit testing");
    Text::TextLayoutParams ScopedHitLayoutParams;
    ScopedHitLayoutParams.Inlines =
        std::span<const Text::TextInlineObject>(ScopedHitBlock->Inlines.data(), ScopedHitBlock->InlineCount);
    Text::TextLayoutResult ScopedHitLayout;
    const std::string_view ScopedHitText(ScopedHitBlock->Text.data(), ScopedHitBlock->TextLength);
    Check(Text::LayoutText(ScopedHitText, ScopedHitLayoutParams, &ScopedHitLayout), "inline button lays out");
    Text::TextInlineHit ScopedInlineHit;
    Check(Text::HitTestTextInline(ScopedHitText, ScopedHitLayout, ScopedHitLayoutParams, Vector2D{0.0f, 0.0f},
                                  Vector2D{10.0f, 5.0f}, &ScopedInlineHit) &&
              ScopedInlineHit.Id == 7 && ScopedInlineHit.Kind == Text::TextInlineKind::Button,
          "inline button hit test");
    Check(!Text::HitTestTextInline(ScopedHitText, ScopedHitLayout, ScopedHitLayoutParams, Vector2D{0.0f, 0.0f},
                                   Vector2D{40.0f, 5.0f}),
          "inline button outside hit test");

    auto ScopedRtlHitBlock = std::make_unique<Text::TextStyledBlock>();
    Check(Text::CompileTextMarkup("[button=7:20:10][separator=10:10]", ScopedRtlHitBlock.get()) ==
              Text::TextMarkupStatus::Ok,
          "RTL inline objects compile");
    Text::TextLayoutParams ScopedRtlHitParams;
    ScopedRtlHitParams.RightToLeft = true;
    ScopedRtlHitParams.Inlines =
        std::span<const Text::TextInlineObject>(ScopedRtlHitBlock->Inlines.data(), ScopedRtlHitBlock->InlineCount);
    Text::TextLayoutResult ScopedRtlHitLayout;
    const std::string_view ScopedRtlHitText(ScopedRtlHitBlock->Text.data(), ScopedRtlHitBlock->TextLength);
    Check(Text::LayoutText(ScopedRtlHitText, ScopedRtlHitParams, &ScopedRtlHitLayout), "RTL inline objects lay out");
    Text::TextInlineHit ScopedRtlButtonHit;
    Check(Text::HitTestTextInline(ScopedRtlHitText, ScopedRtlHitLayout, ScopedRtlHitParams, Vector2D{0.0f, 0.0f},
                                  Vector2D{15.0f, 5.0f}, &ScopedRtlButtonHit) &&
              ScopedRtlButtonHit.Id == 7,
          "RTL inline hit-test mirrors the button to the right");
    Text::TextInlineHit ScopedRtlSeparatorHit;
    Check(Text::HitTestTextInline(ScopedRtlHitText, ScopedRtlHitLayout, ScopedRtlHitParams, Vector2D{0.0f, 0.0f},
                                  Vector2D{5.0f, 5.0f}, &ScopedRtlSeparatorHit) &&
              ScopedRtlSeparatorHit.Kind == Text::TextInlineKind::Separator,
          "RTL inline hit-test mirrors the separator to the left");

    entt::registry ScopedFormatRegistry;
    const Entity ScopedFormatEntity = ScopedFormatRegistry.create();
    const AssetId ScopedFormatAsset = AssetId::FromPath("text/format.txt", AssetKind::Text);
    Check(ScopedText->AddAsset(ScopedFormatAsset, "{0}"), "format text asset stores");
    TextComponent ScopedFormatText;
    ScopedFormatText.SetRef(ScopedText->MakeRef(ScopedFormatAsset));
    ScopedFormatRegistry.emplace<TextComponent>(ScopedFormatEntity, ScopedFormatText);
    ScopedFormatRegistry.emplace<Transform2D>(ScopedFormatEntity,
                                             Transform2D{Vector2D{0.0f, 0.0f}, Vector2D{1.0f, 1.0f}, 0.0f});
    TextFormatComponent ScopedFormat;
    ScopedFormat.Value0 = BigNum::Int128::FromInt64(42);
    ScopedFormat.ValueCount = 1;
    ScopedFormatRegistry.emplace<TextFormatComponent>(ScopedFormatEntity, ScopedFormat);
    std::array<Rhi::RenderItem, 8> ScopedFormatItems{};
    const Text::TextRenderSystemStats ScopedFormatStats =
        Text::ExtractTextItems(ScopedFormatRegistry, *ScopedText, ScopedFonts, 0, 16.0f, ScopedLayoutParams,
                               std::span<Rhi::RenderItem>(ScopedFormatItems));
    Check(ScopedFormatStats.Items == 2, "text format component renders its BigInt placeholder");
    Check(ScopedFormatStats.MissingGlyphs == 0, "formatted digits use the atlas");

    const AssetId ScopedConditionAsset = AssetId::FromPath("text/condition.txt", AssetKind::Text);
    Check(ScopedText->AddMarkupAsset(ScopedConditionAsset, "A[if=0]A[/if]"), "condition markup asset stores");
    entt::registry ScopedConditionRegistry;
    const Entity ScopedConditionEntity = ScopedConditionRegistry.create();
    TextComponent ScopedConditionText;
    ScopedConditionText.SetRef(ScopedText->MakeRef(ScopedConditionAsset));
    ScopedConditionRegistry.emplace<TextComponent>(ScopedConditionEntity, ScopedConditionText);
    ScopedConditionRegistry.emplace<Transform2D>(ScopedConditionEntity,
                                                Transform2D{Vector2D{0.0f, 0.0f}, Vector2D{1.0f, 1.0f}, 0.0f});
    TextConditionComponent ScopedCondition;
    ScopedCondition.Mask = 0;
    ScopedConditionRegistry.emplace<TextConditionComponent>(ScopedConditionEntity, ScopedCondition);
    Text::TextStyledBlock ScopedVariantCheck;
    Check(ScopedText->CompileMarkupVariant(ScopedConditionAsset, 0, &ScopedVariantCheck), "condition variant compiles");
    Check(ScopedVariantCheck.View() == "A", "condition variant removes the disabled block");
    std::array<Rhi::RenderItem, 8> ScopedConditionItems{};
    Text::TextRenderSystemStats ScopedConditionStats =
        Text::ExtractTextItems(ScopedConditionRegistry, *ScopedText, ScopedFonts, 0, 16.0f, ScopedLayoutParams,
                               std::span<Rhi::RenderItem>(ScopedConditionItems));
    Check(ScopedConditionStats.Items == 1, "condition mask removes the disabled block");
    ScopedConditionRegistry.get<TextConditionComponent>(ScopedConditionEntity).Mask = 1;
    ScopedConditionStats = Text::ExtractTextItems(
        ScopedConditionRegistry, *ScopedText, ScopedFonts, 0, 16.0f, ScopedLayoutParams,
        std::span<Rhi::RenderItem>(ScopedConditionItems));
    Check(ScopedConditionStats.Items == 2, "condition mask keeps the enabled block");
    ScopedConditionRegistry.get<TextConditionComponent>(ScopedConditionEntity).Mask = 0;
    ScopedConditionStats = Text::ExtractTextItems(
        ScopedConditionRegistry, *ScopedText, ScopedFonts, 0, 16.0f, ScopedLayoutParams,
        std::span<Rhi::RenderItem>(ScopedConditionItems));
    Check(ScopedText->MarkupVariantHits() >= 1, "condition variant cache hit");

    auto ScopedMarkup = std::make_unique<Text::TextStyledBlock>();
    Check(Text::CompileTextMarkup("[color=#FF0000]A[/color]A", ScopedMarkup.get()) == Text::TextMarkupStatus::Ok,
          "styled markup compiles for render");
    std::array<Rhi::RenderItem, 8> ScopedMarkupItems{};
    Text::TextRenderStats ScopedMarkupStats;
    const std::uint32_t ScopedMarkupCount =
        Text::BuildStyledTextRenderItems(*ScopedMarkup, Vector2D{0.0f, 0.0f}, ScopedLayoutParams, ScopedRenderParams,
                                         std::span<Rhi::RenderItem>(ScopedMarkupItems), &ScopedMarkupStats);
    Check(ScopedMarkupCount == 2, "styled markup renders both glyphs");
    Check(ScopedMarkupItems[0].color == Color32::FromBytes(0xFF, 0, 0, 0xFF).rgba, "markup first color");
    Check(ScopedMarkupItems[1].color == Color32::White().rgba, "markup default color");

    auto ScopedUnderline = std::make_unique<Text::TextStyledBlock>();
    Check(Text::CompileTextMarkup("[u]A[/u]", ScopedUnderline.get()) == Text::TextMarkupStatus::Ok,
          "underline markup compiles");
    std::array<Rhi::RenderItem, 8> ScopedUnderlineItems{};
    Text::TextRenderStats ScopedUnderlineStats;
    const std::uint32_t ScopedUnderlineCount = Text::BuildStyledTextRenderItems(
        *ScopedUnderline, Vector2D{0.0f, 0.0f}, ScopedLayoutParams, ScopedRenderParams,
        std::span<Rhi::RenderItem>(ScopedUnderlineItems), &ScopedUnderlineStats);
    Check(ScopedUnderlineCount == 2, "underline adds a decoration item");
    Check(ScopedUnderlineItems[1].layer == 0, "underline uses the white layer");

    auto ScopedIcon = std::make_unique<Text::TextStyledBlock>();
    Check(Text::CompileTextMarkup("[icon=5:20:12]", ScopedIcon.get()) == Text::TextMarkupStatus::Ok,
          "inline icon markup compiles");
    std::array<Rhi::RenderItem, 8> ScopedIconItems{};
    Text::TextRenderStats ScopedIconStats;
    std::uint32_t ScopedIconCount = Text::BuildStyledTextRenderItems(
        *ScopedIcon, Vector2D{0.0f, 0.0f}, ScopedLayoutParams, ScopedRenderParams,
        std::span<Rhi::RenderItem>(ScopedIconItems), &ScopedIconStats);
    Check(ScopedIconCount == 1, "inline icon emits one quad");
    Check(ScopedIconItems[0].layer == 0, "inline icon uses the white layer");
    Check(ScopedIconItems[0].size.X == 20.0f && ScopedIconItems[0].size.Y == 12.0f, "inline icon size");

    const std::array<Text::TextInlineSprite, 1> ScopedInlineSprites{
        Text::TextInlineSprite{5, 4, 0.25f, 0.25f, 0.75f, 0.75f}};
    ScopedRenderParams.InlineSprites = std::span<const Text::TextInlineSprite>(ScopedInlineSprites);
    ScopedIconCount = Text::BuildStyledTextRenderItems(
        *ScopedIcon, Vector2D{0.0f, 0.0f}, ScopedLayoutParams, ScopedRenderParams,
        std::span<Rhi::RenderItem>(ScopedIconItems), &ScopedIconStats);
    Check(ScopedIconItems[0].layer == 4, "inline sprite layer");
    Check(ScopedIconItems[0].uvMin.X == 0.25f && ScopedIconItems[0].uvMax.Y == 0.75f, "inline sprite UVs");
    ScopedRenderParams.InlineSprites = {};

    const auto ScopedBuildInline = [&](std::string_view InMarkup, std::span<Rhi::RenderItem> Out)
    {
        auto ScopedBlock = std::make_unique<Text::TextStyledBlock>();
        if (Text::CompileTextMarkup(InMarkup, ScopedBlock.get()) != Text::TextMarkupStatus::Ok)
        {
            return 0u;
        }
        Text::TextRenderStats ScopedInlineStats;
        return Text::BuildStyledTextRenderItems(*ScopedBlock, Vector2D{0.0f, 0.0f}, ScopedLayoutParams,
                                                ScopedRenderParams, Out, &ScopedInlineStats);
    };
    std::array<Rhi::RenderItem, 8> ScopedInlineItems{};
    Check(ScopedBuildInline("[border=20:10]", std::span<Rhi::RenderItem>(ScopedInlineItems)) == 4,
          "inline border emits four edges");
    Check(ScopedBuildInline("[progress=50:20:8]", std::span<Rhi::RenderItem>(ScopedInlineItems)) == 2,
          "inline progress emits track and fill");
    Check(ScopedBuildInline("[separator=20:2]", std::span<Rhi::RenderItem>(ScopedInlineItems)) == 1,
          "inline separator emits one line");
    Check(ScopedBuildInline("[button=7:20:10]", std::span<Rhi::RenderItem>(ScopedInlineItems)) == 5,
          "inline button emits body and four edges");

    ScopedRenderParams.Effects.Shadow = true;
    ScopedRenderParams.Effects.Outline = true;
    ScopedRenderParams.Effects.OutlineWidth = 1.0f;
    std::array<Rhi::RenderItem, 16> ScopedEffectItems{};
    Text::TextRenderStats ScopedEffectStats;
    const std::uint32_t ScopedEffectCount = Text::BuildTextRenderItems(
        *ScopedText, ScopedRef, Vector2D{0.0f, 0.0f}, ScopedLayoutParams, ScopedRenderParams,
        std::span<Rhi::RenderItem>(ScopedEffectItems), &ScopedEffectStats);
    Check(ScopedEffectCount == 6, "text effects emit shadow, shader outline and glyph");
    Check(ScopedEffectItems[0].color == ScopedRenderParams.Effects.ShadowColor.rgba, "shadow color");
    Check(ScopedEffectItems[1].color == ScopedRenderParams.Effects.OutlineColor.rgba, "outline color");
    Check(ScopedEffectItems[5].color == Color32::White().rgba, "glyph color after effects");
    ScopedRenderParams.Effects = {};

    ScopedRenderParams.Animation.Typewriter = true;
    ScopedRenderParams.Animation.VisibleGlyphs = 1;
    std::array<Rhi::RenderItem, 8> ScopedTypewriterItems{};
    const std::uint32_t ScopedTypewriterCount = Text::BuildTextRenderItems(
        *ScopedText, ScopedRef, Vector2D{0.0f, 0.0f}, ScopedLayoutParams, ScopedRenderParams,
        std::span<Rhi::RenderItem>(ScopedTypewriterItems), &ScopedStats);
    Check(ScopedTypewriterCount == 1, "typewriter reveals the requested prefix");

    ScopedRenderParams.Animation = {};
    ScopedRenderParams.Animation.Wave = true;
    ScopedRenderParams.Animation.WaveAmplitude = 4.0f;
    ScopedRenderParams.Animation.Time = 0.5f;
    std::array<Rhi::RenderItem, 8> ScopedWaveItems{};
    const std::uint32_t ScopedWaveCount = Text::BuildTextRenderItems(
        *ScopedText, ScopedRef, Vector2D{0.0f, 0.0f}, ScopedLayoutParams, ScopedRenderParams,
        std::span<Rhi::RenderItem>(ScopedWaveItems), &ScopedStats);
    Check(ScopedWaveCount == 2, "wave keeps both glyphs");
    Check(ScopedWaveItems[0].position.Y != ScopedItems[0].position.Y, "wave offsets glyphs");

    ScopedRenderParams.Animation = {};
    ScopedRenderParams.Animation.Rainbow = true;
    ScopedRenderParams.Animation.Time = 0.5f;
    std::array<Rhi::RenderItem, 8> ScopedRainbowItems{};
    const std::uint32_t ScopedRainbowCount = Text::BuildTextRenderItems(
        *ScopedText, ScopedRef, Vector2D{0.0f, 0.0f}, ScopedLayoutParams, ScopedRenderParams,
        std::span<Rhi::RenderItem>(ScopedRainbowItems), &ScopedStats);
    Check(ScopedRainbowCount == 2, "rainbow keeps both glyphs");
    Check(ScopedRainbowItems[0].color != Color32::White().rgba, "rainbow recolors glyphs");
    ScopedRenderParams.Animation = {};

    ScopedRenderParams.Gradient.Enabled = true;
    ScopedRenderParams.Gradient.Start = Color32::FromBytes(0xFF, 0, 0, 0xFF);
    ScopedRenderParams.Gradient.End = Color32::FromBytes(0, 0, 0xFF, 0xFF);
    std::array<Rhi::RenderItem, 8> ScopedGradientItems{};
    const std::uint32_t ScopedGradientCount = Text::BuildTextRenderItems(
        *ScopedText, ScopedRef, Vector2D{0.0f, 0.0f}, ScopedLayoutParams, ScopedRenderParams,
        std::span<Rhi::RenderItem>(ScopedGradientItems), &ScopedStats);
    Check(ScopedGradientCount == 2, "text gradient keeps both glyphs");
    Check(ScopedGradientItems[0].color != ScopedGradientItems[1].color, "text gradient changes color along the line");
    ScopedRenderParams.Gradient = {};

    ScopedRenderParams.Effects.Glow = true;
    ScopedRenderParams.Effects.GlowRadius = 2.0f;
    std::array<Rhi::RenderItem, 24> ScopedGlowItems{};
    const std::uint32_t ScopedGlowCount = Text::BuildTextRenderItems(
        *ScopedText, ScopedRef, Vector2D{0.0f, 0.0f}, ScopedLayoutParams, ScopedRenderParams,
        std::span<Rhi::RenderItem>(ScopedGlowItems), &ScopedStats);
    Check(ScopedGlowCount == 4, "glow emits one shader quad plus the glyph per character");
    Check(ScopedGlowItems[0].color == ScopedRenderParams.Effects.GlowColor.rgba, "glow color");
    ScopedRenderParams.Effects = {};

    if (GFailures != 0)
    {
        std::fprintf(stderr, "%d text render test(s) failed\n", GFailures);
        return 1;
    }
    std::printf("text render tests: ok\n");
    return 0;
}
