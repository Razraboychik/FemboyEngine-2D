// FemboyEngine2D - long text storage and wrapping acceptance test.
#include "Engine/Core/AssetId.hpp"
#include "Engine/ECS/ComponentRegistry.hpp"
#include "Engine/ECS/Components.hpp"
#include "Engine/ECS/Scene.hpp"
#include "Engine/ECS/Snapshot.hpp"
#include "Engine/Text/TextAsset.hpp"
#include "Engine/Text/TextAssetFormat.hpp"
#include "Engine/Text/TextLayout.hpp"
#include "Engine/Text/TextService.hpp"
#include "Engine/Text/TextStore.hpp"
#include "Engine/Assets/VFS.hpp"
#include "Engine/Text/TextFontStack.hpp"
#include "Engine/Text/TextShaper.hpp"
#include "Engine/Text/TextShapedLayout.hpp"
#include "Engine/Text/TextRender.hpp"
#include "Engine/Text/TextVertical.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <span>
#include <string>
#include <string_view>
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

void CheckString(std::string_view InActual, std::string_view InExpected, const char* InMessage)
{
    if (InActual != InExpected)
    {
        std::fprintf(stderr, "FAIL: %s (actual '%.*s', expected '%.*s')\n", InMessage,
                     static_cast<int>(InActual.size()), InActual.data(), static_cast<int>(InExpected.size()),
                     InExpected.data());
        ++GFailures;
    }
}

std::string TrimRight(std::string_view InText)
{
    std::size_t ScopedEnd = InText.size();
    while (ScopedEnd > 0 && (InText[ScopedEnd - 1] == ' ' || InText[ScopedEnd - 1] == '\t'))
    {
        --ScopedEnd;
    }
    return std::string(InText.substr(0, ScopedEnd));
}

void TestStoreBlocks()
{
    using Fby::Text::TextStore;

    const Fby::AssetId ScopedAsset = Fby::AssetId::FromPath("stories/prologue.txt", Fby::AssetKind::Text);
    Check(ScopedAsset.IsValid(), "text asset id is valid");

    TextStore<128, 16, 4, 16> ScopedStore;
    constexpr std::string_view kText = "First paragraph.\nSecond paragraph is longer than one block.";
    Check(ScopedStore.AddAsset(ScopedAsset, kText), "text asset stores");
    Check(ScopedStore.Contains(ScopedAsset), "text asset is present");
    Check(ScopedStore.AssetCount() == 1, "text asset count");
    Check(ScopedStore.BlockCount(ScopedAsset) > 1, "long text splits into blocks");
    Check(ScopedStore.UsedBytes() == kText.size(), "text store copies the exact byte count");

    std::string ScopedReconstructed;
    for (std::uint32_t ScopedBlock = 0; ScopedBlock < ScopedStore.BlockCount(ScopedAsset); ++ScopedBlock)
    {
        const auto ScopedRef = ScopedStore.MakeRef(ScopedAsset, ScopedBlock);
        Check(ScopedRef.IsValid(), "text block ref is valid");
        ScopedReconstructed += ScopedStore.Block(ScopedRef);
    }
    CheckString(ScopedReconstructed, kText, "text blocks reconstruct the source");

    const auto ScopedFirst = ScopedStore.MakeRef(ScopedAsset, 0);
    Check(!ScopedStore.MakeRef(ScopedAsset, 99).IsValid(), "out-of-range block rejects");
    Check(!ScopedStore.AddAsset(ScopedAsset, "duplicate"), "duplicate asset rejects");

    Fby::Text::TextAssetRef ScopedStale = ScopedFirst;
    ScopedStale.Generation += 1;
    Check(ScopedStore.Block(ScopedStale).empty(), "stale text ref rejects");

    ScopedStore.Clear();
    Check(!ScopedStore.Contains(ScopedAsset), "clear removes assets");
    Check(ScopedStore.BlockCount() == 0, "clear resets blocks");
}

void TestUtf8Blocks()
{
    using Fby::Text::TextStore;

    const Fby::AssetId ScopedAsset = Fby::AssetId::FromPath("stories/utf8.txt", Fby::AssetKind::Text);
    TextStore<64, 16, 2, 7> ScopedStore;
    constexpr std::string_view kText = "café monde";
    Check(ScopedStore.AddAsset(ScopedAsset, kText), "utf8 text stores");

    std::string ScopedReconstructed;
    for (std::uint32_t ScopedBlock = 0; ScopedBlock < ScopedStore.BlockCount(ScopedAsset); ++ScopedBlock)
    {
        ScopedReconstructed += ScopedStore.Block(ScopedStore.MakeRef(ScopedAsset, ScopedBlock));
    }
    CheckString(ScopedReconstructed, kText, "utf8 blocks keep valid boundaries");
}

void TestLayout()
{
    using Fby::Text::CountTextGlyphs;
    using Fby::Text::LayoutText;
    using Fby::Text::TextLayoutParams;
    using Fby::Text::TextLayoutResult;

    TextLayoutParams ScopedParams;
    ScopedParams.MaxWidth = 5.0f;
    ScopedParams.Advance = 1.0f;
    TextLayoutResult ScopedResult;
    Check(LayoutText("hello world", ScopedParams, &ScopedResult), "simple wrap succeeds");
    Check(ScopedResult.Count == 2, "simple wrap has two lines");
    if (ScopedResult.Count == 2)
    {
        constexpr std::string_view kSource = "hello world";
        CheckString(TrimRight(kSource.substr(ScopedResult.Lines[0].Begin,
                                            ScopedResult.Lines[0].End - ScopedResult.Lines[0].Begin)),
                    "hello", "first wrapped line");
        CheckString(kSource.substr(ScopedResult.Lines[1].Begin,
                                   ScopedResult.Lines[1].End - ScopedResult.Lines[1].Begin),
                    "world", "second wrapped line");
    }

    ScopedParams.MaxWidth = 3.0f;
    Check(LayoutText("abcdefgh", ScopedParams, &ScopedResult), "long word wrap succeeds");
    Check(ScopedResult.Count == 3, "long word wraps into three chunks");

    ScopedParams.MaxWidth = 100.0f;
    Check(LayoutText("one\ntwo", ScopedParams, &ScopedResult), "explicit newline wrap succeeds");
    Check(ScopedResult.Count == 2, "explicit newline creates two lines");

    ScopedParams.MaxWidth = 3.0f;
    Check(LayoutText("café", ScopedParams, &ScopedResult), "utf8 wrap succeeds");
    Check(ScopedResult.Count == 2, "utf8 wrap counts codepoints");
    Check(CountTextGlyphs("café") == 4, "utf8 glyph count");

    Check(LayoutText("", ScopedParams, &ScopedResult), "empty text layout succeeds");
    Check(ScopedResult.Count == 1, "empty text has one empty line");
}

void TestServiceAndComponent()
{
    using Fby::Text::TextService;

    auto ScopedService = std::make_unique<TextService>();
    const Fby::AssetId ScopedAsset = Fby::AssetId::FromPath("stories/service.txt", Fby::AssetKind::Text);
    Check(ScopedService->AddAsset(ScopedAsset, "hello world from text service"), "text service adds an asset");
    const auto ScopedRef = ScopedService->MakeRef(ScopedAsset);
    Check(ScopedRef.IsValid(), "text service makes a ref");

    Fby::Text::TextLayoutParams ScopedParams;
    ScopedParams.MaxWidth = 10.0f;
    ScopedParams.Advance = 1.0f;
    Fby::Text::TextLayoutResult ScopedLayout;
    Check(ScopedService->Layout(ScopedRef, ScopedParams, &ScopedLayout), "text service layouts a block");
    Check(ScopedLayout.Count > 1, "text service wraps a long line");

    const Fby::Text::TextLayoutResult* ScopedFirstCached = ScopedService->CachedLayout(ScopedRef, ScopedParams);
    const Fby::Text::TextLayoutResult* ScopedSecondCached = ScopedService->CachedLayout(ScopedRef, ScopedParams);
    Check(ScopedFirstCached != nullptr && ScopedFirstCached == ScopedSecondCached, "text layout cache hits");
    Check(ScopedService->CacheMisses() == 1 && ScopedService->CacheHits() >= 1, "text layout cache counters");

    Fby::Scene::ComponentRegistry ScopedComponents;
    Fby::Scene::RegisterBuiltInComponents(ScopedComponents);
    entt::registry ScopedRegistry;
    const Fby::Entity ScopedEntity = ScopedRegistry.create();
    Fby::TextComponent ScopedComponent;
    ScopedComponent.SetRef(ScopedRef, 1);
    ScopedRegistry.emplace<Fby::TextComponent>(ScopedEntity, ScopedComponent);

    const std::string ScopedToml = Fby::Scene::SaveSceneToml(ScopedRegistry, ScopedComponents);
    Check(ScopedToml.find("TextComponent") != std::string::npos, "scene TOML carries TextComponent");

    entt::registry ScopedLoaded;
    const auto ScopedLoadedResult = Fby::Scene::LoadSceneToml(ScopedLoaded, ScopedComponents, ScopedToml);
    Check(ScopedLoadedResult.has_value(), "text component scene loads");
    auto ScopedView = ScopedLoaded.view<Fby::TextComponent>();
    Check(!ScopedView.empty(), "loaded scene has TextComponent");
    for (const Fby::Entity ScopedLoadedEntity : ScopedView)
    {
        const Fby::TextComponent& ScopedValue = ScopedView.get<Fby::TextComponent>(ScopedLoadedEntity);
        Check(ScopedValue.Asset == ScopedRef.Asset.value && ScopedValue.Block == ScopedRef.Block &&
                  ScopedValue.Generation == ScopedRef.Generation && ScopedValue.Flags == 1,
              "text component scene round-trip");
    }

    const Fby::Ecs::SnapshotSchema ScopedSchema = Fby::Ecs::SnapshotSchema::BuiltIns();
    const std::size_t ScopedBytes = Fby::Ecs::SnapshotSize(ScopedRegistry, ScopedSchema);
    std::vector<std::byte> ScopedSnapshot(ScopedBytes);
    Fby::DeterministicRng ScopedRng = Fby::DeterministicRng::FromSeed(3);
    Check(Fby::Ecs::CaptureSnapshot(ScopedRegistry, ScopedSchema, ScopedSnapshot, 1, ScopedRng),
          "text component snapshot captures");
    entt::registry ScopedRestored;
    Check(Fby::Ecs::RestoreSnapshot(ScopedRestored, ScopedSchema, ScopedSnapshot), "text component snapshot restores");
    auto ScopedRestoredView = ScopedRestored.view<Fby::TextComponent>();
    Check(!ScopedRestoredView.empty(), "restored registry has TextComponent");
}

void TestInlineLayout()
{
    using Fby::Text::LayoutText;
    using Fby::Text::TextInlineObject;
    using Fby::Text::TextLayoutParams;
    using Fby::Text::TextLayoutResult;

    const std::string ScopedText = "A\xEF\xBF\xBC\x42";
    const std::array<TextInlineObject, 1> ScopedInlines{TextInlineObject{1, 20.0f, 10.0f, 5}};
    TextLayoutParams ScopedParams;
    ScopedParams.MaxWidth = 100.0f;
    ScopedParams.Advance = 8.0f;
    ScopedParams.Inlines = std::span<const TextInlineObject>(ScopedInlines);
    TextLayoutResult ScopedLayout;
    Check(LayoutText(ScopedText, ScopedParams, &ScopedLayout), "inline layout fits");
    Check(ScopedLayout.Count == 1, "inline layout has one line");
    Check(ScopedLayout.Lines[0].GlyphCount == 3, "inline layout counts the placeholder");
    Check(ScopedLayout.Lines[0].Width == 36.0f, "inline layout reserves its width");

    ScopedParams.MaxWidth = 30.0f;
    Check(LayoutText(ScopedText, ScopedParams, &ScopedLayout), "inline layout wraps");
    Check(ScopedLayout.Count == 2, "inline layout wraps into two lines");
    Check(ScopedLayout.Lines[0].Width == 28.0f, "inline layout first line width");
    Check(ScopedLayout.Lines[1].Width == 8.0f, "inline layout second line width");
}

void TestMarkupInlines()
{
    using Fby::Text::TextService;

    auto ScopedService = std::make_unique<TextService>();
    const Fby::AssetId ScopedAsset = Fby::AssetId::FromPath("text/icon.txt", Fby::AssetKind::Text);
    Check(ScopedService->AddMarkupAsset(ScopedAsset, "A[icon=5:20:12]B"), "icon markup stores");
    const Fby::Text::TextAssetRef ScopedRef = ScopedService->MakeRef(ScopedAsset);
    Check(ScopedService->Block(ScopedRef).size() == 5, "icon markup inserts a UTF-8 placeholder");
    Check(ScopedService->InlinesFor(ScopedAsset).size() == 1, "icon markup stores one inline");
    if (ScopedService->InlinesFor(ScopedAsset).size() == 1)
    {
        Check(ScopedService->InlinesFor(ScopedAsset)[0].Id == 5, "icon markup id");
        Check(ScopedService->InlinesFor(ScopedAsset)[0].Width == 20.0f, "icon markup width");
    }
    Fby::Text::TextLayoutParams ScopedParams;
    ScopedParams.MaxWidth = 100.0f;
    ScopedParams.Advance = 8.0f;
    Fby::Text::TextLayoutResult ScopedLayout;
    Check(ScopedService->Layout(ScopedRef, ScopedParams, &ScopedLayout), "icon markup lays out");
    Check(ScopedLayout.Lines[0].Width == 36.0f, "icon markup contributes to layout width");
}

void TestCookedAsset()
{
    using Fby::Text::TextAssetStatus;
    using Fby::Text::TextAssetView;
    using Fby::Text::TextService;

    const std::string ScopedSource = "Cooked text with enough words to span several blocks.";
    std::string ScopedError;
    const std::vector<std::byte> ScopedBytes = Fby::Text::BuildTextAsset(ScopedSource, 16, &ScopedError);
    Check(!ScopedBytes.empty(), "cooked text builds");
    TextAssetView ScopedView;
    Check(Fby::Text::OpenTextAsset(ScopedBytes, &ScopedView, &ScopedError) == TextAssetStatus::Ok,
          "cooked text opens");

    auto ScopedService = std::make_unique<TextService>();
    const Fby::AssetId ScopedAsset = Fby::AssetId::FromPath("stories/cooked.txt", Fby::AssetKind::Text);
    Check(ScopedService->AddCookedAsset(ScopedAsset, ScopedBytes, &ScopedError), "service imports cooked text");
    Check(ScopedService->BlockCount(ScopedAsset) == ScopedView.Header.BlockCount, "cooked blocks are preserved");
    const auto ScopedRef = ScopedService->MakeRef(ScopedAsset);
    Check(ScopedService->Block(ScopedRef) == ScopedView.Block(0), "first cooked block matches");
}
}

void TestVerticalText()
{
    using Fby::Text::HitTestShapedGlyph;
    using Fby::Text::LayoutShapedGlyphs;
    using Fby::Text::ShapedGlyphHit;
    using Fby::Text::ShapedLayoutResult;
    using Fby::Text::TextDirection;
    using Fby::Text::TextLayoutParams;
    using Fby::Text::TextRenderParams;
    using Fby::Text::TextRenderStats;
    using Fby::Text::TextScript;
    using Fby::Text::TextShapeParams;
    using Fby::Text::TextShapeResult;
    using Fby::Text::TextFontStack;
    using Fby::Text::BuildShapedTextRenderItems;

    Fby::VirtualFileSystem ScopedVfs;
    std::string ScopedError;
    Check(ScopedVfs.MountDirectory("assets", 0, &ScopedError), "vertical test mounts the assets");
    const auto ScopedFontBytes = ScopedVfs.Read("fonts/tests/NotoSansSC-Regular.ttf");
    Check(ScopedFontBytes.has_value(), "vertical test reads the CJK face");
    if (!ScopedFontBytes)
    {
        return;
    }

    // Shaping a vertical CJK punctuation mark must select the vertical glyph,
    // not the horizontal one, and place it inside the em box.
    Fby::Text::HarfBuzzShaper ScopedShaper;
    Check(ScopedShaper.SetFont(*ScopedFontBytes, 24.0f, 4), "vertical test loads the CJK shaper");
    const std::string_view ScopedPunctuation = "\xE3\x80\x82\xE3\x80\x81";
    TextShapeParams ScopedHorizontalParams;
    ScopedHorizontalParams.PixelSize = 24.0f;
    ScopedHorizontalParams.Script = TextScript::Han;
    ScopedHorizontalParams.Direction = TextDirection::LeftToRight;
    TextShapeResult ScopedHorizontalShaped;
    Check(ScopedShaper.Shape(ScopedPunctuation, ScopedHorizontalParams, &ScopedHorizontalShaped),
          "horizontal punctuation shapes");
    TextShapeParams ScopedVerticalParams = ScopedHorizontalParams;
    ScopedVerticalParams.Vertical = true;
    ScopedVerticalParams.Direction = TextDirection::TopToBottom;
    TextShapeResult ScopedVerticalShaped;
    Check(ScopedShaper.Shape(ScopedPunctuation, ScopedVerticalParams, &ScopedVerticalShaped),
          "vertical punctuation shapes");
    Check(ScopedHorizontalShaped.Count == 2 && ScopedVerticalShaped.Count == 2,
          "both punctuation runs keep two glyphs");
    Check(ScopedVerticalShaped.Glyphs[0].GlyphId != ScopedHorizontalShaped.Glyphs[0].GlyphId ||
              ScopedVerticalShaped.Glyphs[1].GlyphId != ScopedHorizontalShaped.Glyphs[1].GlyphId,
          "vertical punctuation selects the vert glyph id");
    Check(ScopedVerticalShaped.Glyphs[0].AdvanceY < 0.0f && ScopedVerticalShaped.Glyphs[0].AdvanceX == 0.0f,
          "vertical punctuation advances down the column");
    Check(ScopedVerticalShaped.Glyphs[0].OffsetY != 0.0f,
          "vertical punctuation carries the vertical offset");

    // The subset atlas must ship the vert glyphs, otherwise the preview would
    // report them as missing.
    const std::array<std::uint32_t, 6> ScopedHanCodepoints = {0x4F60u, 0x597Du, 0x3001u, 0x3002u, 0x3010u, 0x3011u};
    TextFontStack ScopedStack;
    Check(ScopedStack.AddFont(ScopedVfs, "fonts/tests/NotoSansSC-Regular.ttf", 4, TextScript::Han, 24.0f, 0, false,
                              std::span<const std::uint32_t>(ScopedHanCodepoints), &ScopedError),
          "vertical atlas builds for the Han subset");
    ScopedStack.SetDefaultScript(TextScript::Latin);
    TextShapeResult ScopedStackShaped;
    Check(Fby::Text::ShapeBidiText(ScopedStack.Shaper(), ScopedPunctuation, ScopedVerticalParams,
                                  &ScopedStackShaped),
          "vertical shaped text goes through the stack");
    TextLayoutParams ScopedLayoutParams;
    ScopedLayoutParams.Vertical = true;
    ScopedLayoutParams.FontSize = 24.0f;
    ScopedLayoutParams.MaxWidth = 60.0f;
    ShapedLayoutResult ScopedLayout;
    Check(LayoutShapedGlyphs(ScopedPunctuation, ScopedStackShaped, ScopedLayoutParams, &ScopedLayout),
          "vertical punctuation lays out in columns");
    TextRenderParams ScopedRenderParams;
    ScopedRenderParams.Fonts = &ScopedStack.Fonts();
    ScopedRenderParams.FontSize = 24.0f;
    ScopedRenderParams.Vertical = true;
    std::array<Fby::Rhi::RenderItem, 16> ScopedItems{};
    TextRenderStats ScopedStats;
    const std::uint32_t ScopedItemsWritten = BuildShapedTextRenderItems(
        ScopedLayout, ScopedStackShaped, Fby::Vector2D{0.0f, 0.0f}, ScopedRenderParams,
        std::span<Fby::Rhi::RenderItem>(ScopedItems), &ScopedStats);
    Check(ScopedItemsWritten == ScopedStackShaped.Count && ScopedStats.MissingGlyphs == 0,
          "vertical punctuation renders from the subset atlas");

    // Columns must not overlap: the next column starts after the previous one.
    const std::string_view ScopedCjkText = "\xE4\xBD\xA0\xE5\xA5\xBD\xE4\xB8\x96\xE7\x95\x8C";
    TextShapeResult ScopedCjkShaped;
    Check(ScopedShaper.Shape(ScopedCjkText, ScopedVerticalParams, &ScopedCjkShaped),
          "vertical CJK shapes");
    TextLayoutParams ScopedColumnParams;
    ScopedColumnParams.Vertical = true;
    ScopedColumnParams.FontSize = 24.0f;
    ScopedColumnParams.MaxWidth = 50.0f;
    ShapedLayoutResult ScopedColumnLayout;
    Check(LayoutShapedGlyphs(ScopedCjkText, ScopedCjkShaped, ScopedColumnParams, &ScopedColumnLayout) &&
              ScopedColumnLayout.Count >= 2,
          "vertical CJK splits into at least two columns");
    bool ScopedColumnsDense = true;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedColumnLayout.Count; ++ScopedIndex)
    {
        const Fby::Text::ShapedLine& ScopedLine = ScopedColumnLayout.Lines[ScopedIndex];
        ScopedColumnsDense = ScopedColumnsDense && ScopedLine.Width >= ScopedColumnParams.FontSize;
    }
    Check(ScopedColumnsDense, "every vertical column is at least one em wide");

    // The hit-test uses the same column origins, so a point in the second
    // column resolves to a glyph of the second column.
    const Fby::Text::ShapedLine& ScopedFirstColumn = ScopedColumnLayout.Lines[0];
    const float ScopedSecondColumnX = ScopedFirstColumn.Width + ScopedColumnParams.FontSize;
    ShapedGlyphHit ScopedFirstHit;
    Check(HitTestShapedGlyph(ScopedCjkShaped.Glyphs,
                             std::span<const Fby::Text::ShapedLine>(&ScopedFirstColumn, 1),
                             Fby::Vector2D{0.0f, 0.0f}, 24.0f, true, Fby::Vector2D{4.0f, 6.0f}, &ScopedFirstHit) &&
              ScopedFirstHit.GlyphIndex == 0 && ScopedFirstHit.Cluster == 0,
          "vertical hit-test finds the first column");
    ShapedGlyphHit ScopedSecondHit;
    Check(HitTestShapedGlyph(ScopedCjkShaped.Glyphs,
                             std::span<const Fby::Text::ShapedLine>(&ScopedColumnLayout.Lines[1], 1),
                             Fby::Vector2D{ScopedSecondColumnX, 0.0f}, 24.0f, true,
                             Fby::Vector2D{ScopedSecondColumnX + 4.0f, 6.0f}, &ScopedSecondHit) &&
              ScopedSecondHit.GlyphIndex == ScopedColumnLayout.Lines[1].BeginGlyph,
          "vertical hit-test finds the second column");
    Check(!HitTestShapedGlyph(ScopedCjkShaped.Glyphs, std::span<const Fby::Text::ShapedLine>(&ScopedFirstColumn, 1),
                              Fby::Vector2D{0.0f, 0.0f}, 24.0f, true,
                              Fby::Vector2D{ScopedSecondColumnX + 4.0f, 6.0f}),
          "vertical hit-test rejects a point outside the column");
}

void TestBidiLayout()
{
    using Fby::Text::HitTestTextInline;
    using Fby::Text::LayoutText;
    using Fby::Text::TextDirection;
    using Fby::Text::TextInlineHit;
    using Fby::Text::TextInlineObject;
    using Fby::Text::TextLayoutParams;
    using Fby::Text::TextLayoutResult;
    using Fby::Text::TextScript;

    TextLayoutParams ScopedParams;
    ScopedParams.MaxWidth = 400.0f;
    ScopedParams.Advance = 8.0f;
    TextLayoutResult ScopedLayout;

    // Mixed Hebrew, Latin and digits: the Latin run stays leftmost, the digits
    // keep a left-to-right level and the Hebrew run closes the visual order.
    const std::string_view ScopedMixed = "abc \xD7\x90\xD7\x91\xD7\x92 123";
    Check(LayoutText(ScopedMixed, ScopedParams, &ScopedLayout), "bidi layout succeeds");
    Check(ScopedLayout.Count == 1, "bidi layout keeps one line");
    Check(ScopedLayout.RunCount == 3, "bidi layout records three visual runs");
    Check(ScopedLayout.Runs[0].Script == TextScript::Latin &&
              ScopedLayout.Runs[0].Direction == TextDirection::LeftToRight,
          "bidi layout keeps the Latin run first");
    Check(ScopedLayout.Runs[1].Direction == TextDirection::LeftToRight,
          "bidi layout keeps the digit run left-to-right");
    Check(ScopedLayout.Runs[2].Script == TextScript::Hebrew &&
              ScopedLayout.Runs[2].Direction == TextDirection::RightToLeft,
          "bidi layout moves the Hebrew run to the visual end");
    bool ScopedRangesValid = true;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedLayout.RunCount; ++ScopedIndex)
    {
        ScopedRangesValid =
            ScopedRangesValid && ScopedLayout.Runs[ScopedIndex].End > ScopedLayout.Runs[ScopedIndex].Begin;
    }
    Check(ScopedRangesValid, "bidi visual runs keep non-empty ranges");

    // Right-to-left base direction turns the paragraph around: the Hebrew run
    // leads and the Latin run becomes the trailing level-2 run.
    TextLayoutParams ScopedRtlParams = ScopedParams;
    ScopedRtlParams.RightToLeft = true;
    const std::string_view ScopedRtlText = "abc \xD7\x90\xD7\x91\xD7\x92";
    Check(LayoutText(ScopedRtlText, ScopedRtlParams, &ScopedLayout), "rtl layout succeeds");
    Check(ScopedLayout.RunCount >= 2, "rtl layout records both runs");
    Check(ScopedLayout.Runs[0].Direction == TextDirection::RightToLeft &&
              ScopedLayout.Runs[0].Script == TextScript::Hebrew,
          "rtl layout starts with the Hebrew run");
    Check(ScopedLayout.Runs[ScopedLayout.RunCount - 1].Script == TextScript::Latin,
          "rtl layout ends with the Latin run");

    // Left-to-right text stays a single run, so the old path is unchanged.
    Check(LayoutText("plain latin text", ScopedParams, &ScopedLayout), "ltr layout succeeds");
    Check(ScopedLayout.RunCount == 1 && ScopedLayout.Runs[0].Direction == TextDirection::LeftToRight,
          "ltr layout keeps one left-to-right run");
    bool ScopedWidthsPositive = true;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedLayout.Count; ++ScopedIndex)
    {
        ScopedWidthsPositive = ScopedWidthsPositive && ScopedLayout.Lines[ScopedIndex].Width > 0.0f;
    }
    Check(ScopedWidthsPositive, "ltr layout keeps its line widths");

    // Inline atoms follow the visual runs. In a right-to-left line the first
    // logical atom sits at the right edge, so the mirrored point hits it.
    TextInlineObject ScopedInline;
    ScopedInline.ByteOffset = 0;
    ScopedInline.Width = 20.0f;
    ScopedInline.Height = 16.0f;
    ScopedInline.Id = 7;
    TextLayoutParams ScopedInlineParams;
    ScopedInlineParams.MaxWidth = 400.0f;
    ScopedInlineParams.Advance = 8.0f;
    ScopedInlineParams.Inlines = std::span<const TextInlineObject>(&ScopedInline, 1);
    ScopedInlineParams.RightToLeft = true;
    const std::string_view ScopedInlineText = "\xEF\xBF\xBC" "AB";
    Check(LayoutText(ScopedInlineText, ScopedInlineParams, &ScopedLayout), "rtl inline layout succeeds");
    Check(ScopedLayout.Lines[0].Width == 36.0f, "rtl inline layout reserves the atom width");
    TextInlineHit ScopedHit;
    Check(HitTestTextInline(ScopedInlineText, ScopedLayout, ScopedInlineParams, Fby::Vector2D{0.0f, 0.0f},
                            Fby::Vector2D{26.0f, 4.0f}, &ScopedHit),
          "rtl inline hit-test finds the mirrored atom");
    Check(ScopedHit.Id == 7 && ScopedHit.X == 16.0f, "rtl inline hit-test reports the mirrored position");
    Check(!HitTestTextInline(ScopedInlineText, ScopedLayout, ScopedInlineParams, Fby::Vector2D{0.0f, 0.0f},
                             Fby::Vector2D{4.0f, 4.0f}),
          "rtl inline hit-test rejects a glyph cell");

    // Left-to-right regression: the same atom stays at the left edge.
    TextLayoutParams ScopedLtrParams = ScopedInlineParams;
    ScopedLtrParams.RightToLeft = false;
    Check(LayoutText(ScopedInlineText, ScopedLtrParams, &ScopedLayout), "ltr inline layout succeeds");
    Check(HitTestTextInline(ScopedInlineText, ScopedLayout, ScopedLtrParams, Fby::Vector2D{0.0f, 0.0f},
                            Fby::Vector2D{10.0f, 4.0f}, &ScopedHit) &&
              ScopedHit.Id == 7 && ScopedHit.X == 0.0f,
          "ltr inline hit-test keeps the atom at the left edge");
    Check(!HitTestTextInline(ScopedInlineText, ScopedLayout, ScopedLtrParams, Fby::Vector2D{0.0f, 0.0f},
                             Fby::Vector2D{30.0f, 4.0f}),
          "ltr inline hit-test rejects the trailing glyph cell");
}

int main()
{
    TestStoreBlocks();
    TestUtf8Blocks();
    TestLayout();
    TestServiceAndComponent();
    TestInlineLayout();
    TestMarkupInlines();
    TestCookedAsset();
    TestBidiLayout();
    TestVerticalText();

    if (GFailures != 0)
    {
        std::fprintf(stderr, "%d text test(s) failed\n", GFailures);
        return 1;
    }
    std::printf("text tests: ok\n");
    return 0;
}
