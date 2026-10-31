// FemboyEngine2D - CJK glyph paging, memory bound and page eviction test.
#include "Engine/Assets/VFS.hpp"
#include "Engine/Text/TextFontStack.hpp"
#include "Engine/Text/TextRender.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <string>
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
}

int main()
{
    using namespace Fby;

    VirtualFileSystem ScopedVfs;
    std::string ScopedError;
    Check(ScopedVfs.MountDirectory("assets", 0, &ScopedError), "paging test mounts the assets");
    if (!ScopedVfs.Read("fonts/tests/NotoSansSC-Regular.ttf"))
    {
        std::fprintf(stderr, "paging test: the CJK face is missing\n");
        return 1;
    }

    Ui::FontBuildOptions ScopedOptions;
    ScopedOptions.fontId = 4;
    ScopedOptions.pixelSize = 8.0f;
    ScopedOptions.atlasSize = 512;
    ScopedOptions.allGlyphs = true;

    Ui::FontAtlasSet ScopedPages;
    Check(ScopedPages.LoadPagedTtfFromVfs(ScopedVfs, "fonts/tests/NotoSansSC-Regular.ttf", ScopedOptions,
                                          &ScopedError),
          "paged face opens with page 0");
    Check(ScopedPages.PageCount(4) == 1 && ScopedPages.PageLoads() == 1, "paged face starts with one page");
    const std::uint32_t ScopedPageBytes = 512u * 512u;
    Check(ScopedPages.AtlasPixelBytes() == ScopedPageBytes,
          "one SDF page costs one 512x512 buffer, not the 8192 full-face atlas");
    Check(ScopedPages.AtlasPixelBytes() < 64u * 1024u * 1024u, "the paged atlas stays far below 536 MB");

    // The glyph catalogue of a page is available without building anything else.
    std::array<std::uint32_t, Ui::kGlyphsPerPage> ScopedCatalogue{};
    const std::uint32_t ScopedCatCount = ScopedPages.CatalogueGlyphs(4, std::span<std::uint32_t>(ScopedCatalogue));
    Check(ScopedCatCount > 64, "page 0 exposes a glyph catalogue");
    Check(ScopedCatCount == ScopedPages.At(0)->GlyphCount(), "the catalogue matches the page glyph table");

    // A glyph id from a later page is loaded on demand and lands in its own
    // layer.
    const std::uint32_t ScopedPageGlyphs = ScopedPages.PageGlyphs(4);
    Check(ScopedPageGlyphs > 0 && ScopedPageGlyphs <= Ui::kGlyphsPerPage, "the paged face reports its page budget");
    const std::uint32_t ScopedSecondPageGlyph = ScopedPageGlyphs + 64u;
    Check(!ScopedPages.HasGlyph(4, Ui::FontStyle::Regular, ScopedSecondPageGlyph),
          "a glyph from page 1 is not resident before the request");
    const Ui::FontAtlas* ScopedSecondPage =
        ScopedPages.EnsurePageForGlyph(4, Ui::FontStyle::Regular, ScopedSecondPageGlyph);
    Check(ScopedSecondPage != nullptr, "the page of a missing glyph is built on demand");
    Check(ScopedPages.PageLoads() == 2 && ScopedPages.PageCount(4) == 2, "the on-demand page is counted");
    Check(ScopedPages.LayerForAtlas(ScopedPages.At(0)) != ScopedPages.LayerForAtlas(ScopedSecondPage),
          "pages sit on separate texture-array layers");
    Check(ScopedPages.EnsurePageForGlyph(4, Ui::FontStyle::Regular, ScopedSecondPageGlyph) == ScopedSecondPage &&
              ScopedPages.PageLoads() == 2,
          "a resident page is not rebuilt");

    // Rendering picks the page from the glyph id.
    std::uint32_t ScopedFirstInk = 0xFFFFFFFFu;
    std::uint32_t ScopedSecondInk = 0xFFFFFFFFu;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedCatCount && ScopedFirstInk == 0xFFFFFFFFu; ++ScopedIndex)
    {
        const std::uint32_t ScopedGlyphId = ScopedCatalogue[ScopedIndex];
        const Ui::GlyphRecord* ScopedRecord =
            ScopedPages.At(0)->GlyphById(ScopedGlyphId);
        if (ScopedRecord != nullptr && ScopedRecord->width > 0.0f && ScopedRecord->height > 0.0f)
        {
            ScopedFirstInk = ScopedGlyphId;
        }
    }
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedSecondPage->GlyphCount() &&
                                           ScopedSecondInk == 0xFFFFFFFFu;
         ++ScopedIndex)
    {
        const Ui::GlyphRecord& ScopedRecord = ScopedSecondPage->GlyphAt(ScopedIndex);
        if (ScopedRecord.width > 0.0f && ScopedRecord.height > 0.0f)
        {
            ScopedSecondInk = ScopedRecord.glyphId;
        }
    }
    Check(ScopedFirstInk != 0xFFFFFFFFu && ScopedSecondInk != 0xFFFFFFFFu,
          "both pages hold at least one inked glyph");
    Text::TextShapeResult ScopedShape;
    ScopedShape.Count = 2;
    ScopedShape.Glyphs[0].GlyphId = ScopedFirstInk;
    ScopedShape.Glyphs[0].FontId = 4;
    ScopedShape.Glyphs[0].AdvanceX = 8.0f;
    ScopedShape.Glyphs[1].GlyphId = ScopedSecondInk;
    ScopedShape.Glyphs[1].FontId = 4;
    ScopedShape.Glyphs[1].AdvanceX = 8.0f;
    Text::TextRenderParams ScopedRender;
    ScopedRender.Fonts = &ScopedPages;
    ScopedRender.FontSize = 8.0f;
    std::array<Rhi::RenderItem, 4> ScopedItems{};
    Text::TextRenderStats ScopedStats;
    const std::uint32_t ScopedWritten =
        Text::BuildShapedTextRenderItems(ScopedShape, Vector2D{0.0f, 0.0f}, ScopedRender,
                                         std::span<Rhi::RenderItem>(ScopedItems), &ScopedStats);
    Check(ScopedWritten == 2 && ScopedStats.MissingGlyphs == 0, "both page glyphs render");
    Check(ScopedItems[0].layer != ScopedItems[1].layer, "each glyph renders from its own page layer");

    // Eviction: when the atlas array is full the least recently used page is
    // replaced, and the load/evict counters record it.
    std::vector<std::uint32_t> ScopedHighGlyphs{};
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < Ui::kMaxFontAtlases + 6u; ++ScopedIndex)
    {
        const std::uint32_t ScopedGlyphId = (ScopedIndex + 1u) * ScopedPageGlyphs;
        (void)ScopedPages.EnsurePageForGlyph(4, Ui::FontStyle::Regular, ScopedGlyphId);
        ScopedHighGlyphs.push_back(ScopedGlyphId);
    }
    Check(ScopedPages.PageEvictions() > 0, "page replacement triggers when the array is full");
    Check(ScopedPages.PageCount(4) == Ui::kMaxFontAtlases, "the page table never grows past its slots");
    Check(ScopedPages.HasGlyph(4, Ui::FontStyle::Regular, ScopedHighGlyphs.back()),
          "the most recent page survives eviction");
    Check(ScopedPages.PageLoads() > Ui::kMaxFontAtlases, "every requested page was built once");

    // End to end: a paged Han face in the stack warms the pages a shaped run
    // needs, and the run renders with no missing glyphs.
    Text::TextFontStack ScopedStack;
    Check(ScopedStack.AddPagedFont(ScopedVfs, "fonts/tests/NotoSansSC-Regular.ttf", 4, Text::TextScript::Han,
                                   16.0f, 1024, &ScopedError),
          "the stack mounts a paged Han face");
    const std::string_view ScopedCjkText = "\xE4\xBD\xA0\xE5\xA5\xBD\xEF\xBC\x8C\xE4\xB8\x96\xE7\x95\x8C";
    Text::TextShapeParams ScopedParams;
    ScopedParams.PixelSize = 16.0f;
    ScopedParams.Script = Text::TextScript::Han;
    ScopedParams.Direction = Text::TextDirection::LeftToRight;
    Text::TextShapeResult ScopedStackShape;
    Check(Text::ShapeBidiText(ScopedStack.Shaper(), ScopedCjkText, ScopedParams, &ScopedStackShape),
          "the paged Han face shapes CJK text");
    const std::uint32_t ScopedBeforeWarm = ScopedStack.Fonts().PageLoads();
    const std::uint32_t ScopedWarmed = Text::EnsureShapedGlyphPages(ScopedStack.Fonts(), ScopedStackShape);
    Check(ScopedWarmed == ScopedStack.Fonts().PageLoads() - ScopedBeforeWarm,
          "page warming reports every page it built");
    Text::TextLayoutParams ScopedLayoutParams;
    ScopedLayoutParams.FontSize = 16.0f;
    ScopedLayoutParams.MaxWidth = 400.0f;
    Text::ShapedLayoutResult ScopedLayout;
    Check(Text::LayoutShapedGlyphs(ScopedCjkText, ScopedStackShape, ScopedLayoutParams, &ScopedLayout),
          "the shaped CJK run lays out");
    Text::TextRenderParams ScopedStackRender;
    ScopedStackRender.Fonts = &ScopedStack.Fonts();
    ScopedStackRender.FontSize = 16.0f;
    std::array<Rhi::RenderItem, 32> ScopedStackItems{};
    Text::TextRenderStats ScopedStackStats;
    const std::uint32_t ScopedStackWritten = Text::BuildShapedTextRenderItems(
        ScopedLayout, ScopedStackShape, Vector2D{0.0f, 0.0f}, ScopedStackRender,
        std::span<Rhi::RenderItem>(ScopedStackItems), &ScopedStackStats);
    Check(ScopedStackWritten == ScopedStackShape.Count && ScopedStackStats.MissingGlyphs == 0,
          "the paged Han face renders the whole run");

    if (GFailures != 0)
    {
        std::fprintf(stderr, "%d font paging test(s) failed\n", GFailures);
        return 1;
    }
    std::printf("font paging tests: ok\n");
    return 0;
}
