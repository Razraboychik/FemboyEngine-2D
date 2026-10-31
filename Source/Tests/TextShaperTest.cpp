// FemboyEngine2D - shaper/bidi boundary tests.
#include "Engine/Assets/VFS.hpp"
#include "Engine/Text/TextBidi.hpp"
#include "Engine/Text/TextRender.hpp"
#include "Engine/Text/TextFontStack.hpp"
#include "Engine/Text/TextShaper.hpp"
#include "Engine/UI/FontAtlas.hpp"
#include "Engine/UI/FontBuilder.hpp"

#include <array>
#include <cstdio>
#include <fstream>
#include <iterator>
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

[[nodiscard]] std::vector<char> ReadFileBytes(const char* InPath)
{
    std::ifstream ScopedFile(InPath, std::ios::binary);
    return std::vector<char>((std::istreambuf_iterator<char>(ScopedFile)), std::istreambuf_iterator<char>());
}

class MarkerShaper final : public Fby::Text::ITextShaper
{
public:
    explicit MarkerShaper(std::uint32_t InMarker)
        : Marker(InMarker)
    {
    }

    [[nodiscard]] bool Shape(std::string_view, const Fby::Text::TextShapeParams&,
                             Fby::Text::TextShapeResult* OutResult) noexcept override
    {
        if (OutResult == nullptr)
        {
            return false;
        }
        OutResult->Count = 1;
        OutResult->Glyphs[0].GlyphId = Marker;
        return true;
    }

    std::uint32_t Marker = 0;
};

class NotdefShaper final : public Fby::Text::ITextShaper
{
public:
    [[nodiscard]] bool Shape(std::string_view, const Fby::Text::TextShapeParams&,
                             Fby::Text::TextShapeResult* OutResult) noexcept override
    {
        if (OutResult == nullptr)
        {
            return false;
        }
        OutResult->Count = 1;
        OutResult->Glyphs[0].GlyphId = 0;
        return true;
    }
};

class RecordingShaper final : public Fby::Text::ITextShaper
{
public:
    [[nodiscard]] bool Shape(std::string_view InText, const Fby::Text::TextShapeParams& InParams,
                             Fby::Text::TextShapeResult* OutResult) noexcept override
    {
        if (OutResult == nullptr || Calls >= Directions.size())
        {
            return false;
        }
        Directions[Calls] = InParams.Direction;
        ++Calls;
        OutResult->Count = 1;
        OutResult->Glyphs[0].GlyphId = InText.empty() ? 0u : static_cast<std::uint32_t>(InText.front());
        OutResult->Glyphs[0].Cluster = 0;
        OutResult->Glyphs[0].AdvanceX = 8.0f;
        return true;
    }

    std::array<Fby::Text::TextDirection, 8> Directions{};
    std::uint32_t Calls = 0;
};
}

int main()
{
    using namespace Fby;
    Text::CodepointShaper ScopedShaper;
    Text::TextShapeParams ScopedParams;
    ScopedParams.PixelSize = 16.0f;
    Text::TextShapeResult ScopedResult;
    Check(ScopedShaper.Shape("AB", ScopedParams, &ScopedResult), "compatibility shaper succeeds");
    Check(ScopedResult.Count == 2 && ScopedResult.Glyphs[0].Cluster == 0 && ScopedResult.Glyphs[1].Cluster == 1,
          "compatibility shaper emits one glyph per byte");

    // Hebrew alef/bet, ASCII digits, then Latin.
    constexpr std::string_view ScopedMixed = "abc\xD7\x90\xD7\x91" "123";
    std::array<Text::TextRun, 16> ScopedRuns{};
    std::uint32_t ScopedRunCount = 0;
    Check(Text::SplitRtlRuns(ScopedMixed, Text::TextDirection::LeftToRight,
                             std::span<Text::TextRun>(ScopedRuns), &ScopedRunCount),
          "bidi splitter covers the string");
    Check(ScopedRunCount >= 3, "bidi splitter separates Latin, Hebrew and digits");
    bool ScopedHasRtl = false;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedRunCount; ++ScopedIndex)
    {
        ScopedHasRtl = ScopedHasRtl || ScopedRuns[ScopedIndex].Direction == Text::TextDirection::RightToLeft;
    }
    Check(ScopedHasRtl, "bidi splitter finds an RTL run");
    Check(Text::ScriptForCodepoint(0x05D0u) == Text::TextScript::Hebrew, "Hebrew script classification");
    Check(Text::ScriptForCodepoint(0x0627u) == Text::TextScript::Arabic, "Arabic script classification");
    Check(Text::ScriptForCodepoint(0x4E00u) == Text::TextScript::Han, "Han script classification");

    std::ifstream ScopedFontFile("assets/fonts/Inter-Regular.ttf", std::ios::binary);
    const std::vector<char> ScopedFontBytes((std::istreambuf_iterator<char>(ScopedFontFile)),
                                            std::istreambuf_iterator<char>());
    if (!ScopedFontBytes.empty())
    {
        Text::HarfBuzzShaper ScopedHb;
        Check(ScopedHb.SetFont(std::span<const std::byte>(
                                   reinterpret_cast<const std::byte*>(ScopedFontBytes.data()),
                                   ScopedFontBytes.size()),
                               32.0f),
              "HarfBuzz font loads");
        Check(ScopedHb.IsReady(), "HarfBuzz shaper is ready");
        Text::TextShapeParams ScopedHbParams;
        ScopedHbParams.Script = Text::TextScript::Latin;
        ScopedHbParams.Direction = Text::TextDirection::LeftToRight;
        ScopedHbParams.PixelSize = 32.0f;
        Text::TextShapeResult ScopedHbResult;
        Check(ScopedHb.Shape("Hello", ScopedHbParams, &ScopedHbResult), "HarfBuzz shapes Latin text");
        Check(ScopedHbResult.Count == 5, "HarfBuzz emits shaped glyphs");
        Check(ScopedHbResult.Glyphs[0].GlyphId != 0 && ScopedHbResult.Glyphs[0].AdvanceX > 0.0f,
              "HarfBuzz glyph id and advance are populated");

        std::ifstream ScopedAtlasFile("assets/fonts/Inter.fbyfont", std::ios::binary);
        const std::vector<char> ScopedAtlasBytes((std::istreambuf_iterator<char>(ScopedAtlasFile)),
                                                 std::istreambuf_iterator<char>());
        Ui::FontAtlasSet ScopedAtlases;
        Check(!ScopedAtlasBytes.empty() &&
                  ScopedAtlases.Add(std::span<const std::byte>(
                      reinterpret_cast<const std::byte*>(ScopedAtlasBytes.data()), ScopedAtlasBytes.size())),
              "glyph-id atlas loads");
        Text::TextRenderParams ScopedRenderParams;
        ScopedRenderParams.Fonts = &ScopedAtlases;
        ScopedRenderParams.FontAtlasIndex = 0;
        ScopedRenderParams.FontSize = 32.0f;
        std::array<Rhi::RenderItem, 16> ScopedItems{};
        Text::TextRenderStats ScopedRenderStats;
        const std::uint32_t ScopedItemCount = Text::BuildShapedTextRenderItems(
            ScopedHbResult, Vector2D{0.0f, 0.0f}, ScopedRenderParams, std::span<Rhi::RenderItem>(ScopedItems),
            &ScopedRenderStats);
        Check(ScopedItemCount == 5 && ScopedRenderStats.MissingGlyphs == 0,
              "HarfBuzz glyph ids render through the glyph-id atlas");

        Text::TextShapeResult ScopedWrappedShaped;
        Check(ScopedHb.Shape("HELLO", ScopedHbParams, &ScopedWrappedShaped), "HarfBuzz shapes wrapped text");
        Text::TextLayoutParams ScopedWrapParams;
        ScopedWrapParams.MaxWidth = 40.0f;
        ScopedWrapParams.FontSize = 32.0f;
        Text::ShapedLayoutResult ScopedWrapLayout;
        Check(Text::LayoutShapedGlyphs("HELLO", ScopedWrappedShaped, ScopedWrapParams, &ScopedWrapLayout) &&
                  ScopedWrapLayout.Count > 1,
              "shaped layout wraps at the width limit");
        std::array<Rhi::RenderItem, 32> ScopedWrappedItems{};
        Text::TextRenderStats ScopedWrappedStats;
        Check(Text::BuildShapedTextRenderItems(ScopedWrapLayout, ScopedWrappedShaped, Vector2D{0.0f, 0.0f},
                                               ScopedRenderParams, std::span<Rhi::RenderItem>(ScopedWrappedItems),
                                               &ScopedWrappedStats) == 5,
              "wrapped shaped glyphs render");

        Text::TextShapeResult ScopedVerticalShaped;
    ScopedVerticalShaped.Count = 2;
    ScopedVerticalShaped.Glyphs[0].GlyphId = static_cast<std::uint32_t>('A');
    ScopedVerticalShaped.Glyphs[0].AdvanceY = -16.0f;
    ScopedVerticalShaped.Glyphs[1].GlyphId = static_cast<std::uint32_t>('A');
    ScopedVerticalShaped.Glyphs[1].AdvanceY = -16.0f;
    Text::TextLayoutParams ScopedVerticalParams;
    ScopedVerticalParams.Vertical = true;
    ScopedVerticalParams.MaxWidth = 24.0f;
    ScopedVerticalParams.FontSize = 16.0f;
    Text::ShapedLayoutResult ScopedVerticalLayout;
    Check(Text::LayoutShapedGlyphs("AA", ScopedVerticalShaped, ScopedVerticalParams, &ScopedVerticalLayout) &&
              ScopedVerticalLayout.Count == 2,
          "vertical shaped layout splits into bounded columns");
    Text::TextRenderParams ScopedVerticalRender;
    ScopedVerticalRender.Fonts = &ScopedAtlases;
    ScopedVerticalRender.FontSize = 32.0f;
    ScopedVerticalRender.Vertical = true;
    std::array<Rhi::RenderItem, 8> ScopedVerticalItems{};
    Check(Text::BuildShapedTextRenderItems(ScopedVerticalLayout, ScopedVerticalShaped, Vector2D{0.0f, 0.0f},
                                           ScopedVerticalRender, std::span<Rhi::RenderItem>(ScopedVerticalItems)) == 2,
          "vertical shaped glyphs render down a column");
        Text::ShapedGlyphHit ScopedVerticalHit;
        Check(Text::HitTestShapedGlyph(ScopedVerticalShaped.Glyphs, ScopedVerticalLayout.Lines,
                                       Vector2D{0.0f, 0.0f}, 16.0f, true, Vector2D{4.0f, 10.0f},
                                       &ScopedVerticalHit) &&
                  ScopedVerticalHit.GlyphIndex == 0,
              "vertical shaped hit-test finds a column glyph");
        Text::ShapedGlyphHit ScopedHorizontalHit;
        Check(Text::HitTestShapedGlyph(ScopedWrappedShaped.Glyphs, ScopedWrapLayout.Lines,
                                       Vector2D{0.0f, 0.0f}, 32.0f, false, Vector2D{2.0f, 2.0f},
                                       &ScopedHorizontalHit) &&
                  ScopedHorizontalHit.GlyphIndex == 0,
              "horizontal shaped hit-test finds the first glyph");
    }

    RecordingShaper ScopedRecording;
    Text::TextShapeParams ScopedBidiParams;
    ScopedBidiParams.Direction = Text::TextDirection::LeftToRight;
    Text::TextShapeResult ScopedBidiResult;
    Check(Text::ShapeBidiText(ScopedRecording, ScopedMixed, ScopedBidiParams, &ScopedBidiResult),
          "bidi shaper dispatches per run");
    Check(ScopedRecording.Calls >= 2, "bidi shaper separates Hebrew and Latin runs");
    bool ScopedRecordedRtl = false;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedRecording.Calls; ++ScopedIndex)
    {
        ScopedRecordedRtl =
            ScopedRecordedRtl || ScopedRecording.Directions[ScopedIndex] == Text::TextDirection::RightToLeft;
    }
    Check(ScopedRecordedRtl, "bidi shaper gives the Hebrew run RTL direction");

    NotdefShaper ScopedNotdef;
    std::array<Text::ITextShaper*, 2> ScopedBackends{&ScopedNotdef, &ScopedShaper};
    Text::ChainedShaper ScopedChain;
    Check(ScopedChain.SetShapers(std::span<Text::ITextShaper*>(ScopedBackends)), "shaper chain configures");
    Text::TextShapeResult ScopedChainResult;
    Check(ScopedChain.Shape("A", ScopedParams, &ScopedChainResult) && ScopedChainResult.Glyphs[0].GlyphId == 'A',
          "shaper chain skips a .notdef backend");

    const auto ScopedCheckScriptFont = [](const char* InPath, std::string_view InText,
                                          Fby::Text::TextScript InScript)
    {
        const std::vector<char> ScopedBytes = ReadFileBytes(InPath);
        if (ScopedBytes.empty())
        {
            return;
        }
        Fby::Text::HarfBuzzShaper ScopedScriptShaper;
        Check(ScopedScriptShaper.SetFont(
                  std::span<const std::byte>(reinterpret_cast<const std::byte*>(ScopedBytes.data()),
                                             ScopedBytes.size()),
                  24.0f, 1),
              "script font loads");
        Fby::Text::TextShapeParams ScopedScriptParams;
        ScopedScriptParams.Script = InScript;
        ScopedScriptParams.PixelSize = 24.0f;
        Fby::Text::TextShapeResult ScopedScriptResult;
        Check(ScopedScriptShaper.Shape(InText, ScopedScriptParams, &ScopedScriptResult) &&
                  ScopedScriptResult.Count > 0 && ScopedScriptResult.Glyphs[0].FontId == 1,
              "complex script shapes with font id");
    };
    ScopedCheckScriptFont("assets/fonts/tests/NotoSansArabic-Regular.ttf",
                          "\xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7", Text::TextScript::Arabic);
    ScopedCheckScriptFont("assets/fonts/tests/NotoSansDevanagari-Regular.ttf",
                          "\xE0\xA4\xA8\xE0\xA4\xAE\xE0\xA4\xB8\xE0\xA5\x8D\xE0\xA4\xA4\xE0\xA5\x87",
                          Text::TextScript::Devanagari);
    ScopedCheckScriptFont("assets/fonts/tests/NotoSansThai-Regular.ttf",
                          "\xE0\xB8\xAA\xE0\xB8\xA7\xE0\xB8\xB1\xE0\xB8\xAA\xE0\xB8\x94\xE0\xB8\xB5",
                          Text::TextScript::Thai);

    const std::vector<char> ScopedCjkBytes = ReadFileBytes("assets/fonts/tests/NotoSansSC-Regular.ttf");
    if (!ScopedCjkBytes.empty())
    {
        Text::HarfBuzzShaper ScopedCjkShaper;
        Check(ScopedCjkShaper.SetFont(
                  std::span<const std::byte>(reinterpret_cast<const std::byte*>(ScopedCjkBytes.data()),
                                             ScopedCjkBytes.size()),
                  24.0f, 2),
              "CJK font loads");
        Text::TextShapeParams ScopedCjkParams;
        ScopedCjkParams.Script = Text::TextScript::Han;
        ScopedCjkParams.Direction = Text::TextDirection::TopToBottom;
        ScopedCjkParams.Vertical = true;
        ScopedCjkParams.PixelSize = 24.0f;
        Text::TextShapeResult ScopedCjkResult;
        Check(ScopedCjkShaper.Shape("\xE4\xBD\xA0\xE5\xA5\xBD", ScopedCjkParams, &ScopedCjkResult) &&
                  ScopedCjkResult.Count > 0,
              "CJK vertical shaping succeeds");

        Text::TextShapeParams ScopedCjkHorizontal = ScopedCjkParams;
        ScopedCjkHorizontal.Vertical = false;
        ScopedCjkHorizontal.Direction = Text::TextDirection::LeftToRight;
        Text::TextShapeResult ScopedCjkHorizontalResult;
        Check(ScopedCjkShaper.Shape("\xE3\x80\x82", ScopedCjkHorizontal, &ScopedCjkHorizontalResult) &&
                  ScopedCjkHorizontalResult.Count > 0,
              "CJK punctuation shapes horizontally");
        Text::TextShapeResult ScopedCjkVerticalPunctuation;
        Check(ScopedCjkShaper.Shape("\xE3\x80\x82", ScopedCjkParams, &ScopedCjkVerticalPunctuation) &&
                  ScopedCjkVerticalPunctuation.Count > 0,
              "CJK punctuation shapes vertically");
        Check(ScopedCjkHorizontalResult.Glyphs[0].GlyphId != ScopedCjkVerticalPunctuation.Glyphs[0].GlyphId,
              "vert feature selects a vertical punctuation glyph");
    }

    VirtualFileSystem ScopedFallbackVfs;
    std::string ScopedFallbackError;
    if (ScopedFallbackVfs.MountDirectory("assets", 0, &ScopedFallbackError))
    {
        Ui::FontAtlasSet ScopedFallbackFonts;
        const std::vector<char> ScopedLatinBytes = ReadFileBytes("assets/fonts/Inter.fbyfont");
        Check(!ScopedLatinBytes.empty() &&
                  ScopedFallbackFonts.Add(std::span<const std::byte>(
                      reinterpret_cast<const std::byte*>(ScopedLatinBytes.data()), ScopedLatinBytes.size())),
              "fallback primary atlas loads");
        Ui::FontBuildOptions ScopedArabicAtlasOptions;
        ScopedArabicAtlasOptions.fontId = 1;
        ScopedArabicAtlasOptions.pixelSize = 16.0f;
        ScopedArabicAtlasOptions.atlasSize = 2048;
        ScopedArabicAtlasOptions.allGlyphs = true;
        ScopedArabicAtlasOptions.codepoints = {};
        Check(ScopedFallbackFonts.LoadTtfFromVfs(ScopedFallbackVfs, "fonts/tests/NotoSansArabic-Regular.ttf",
                                                 ScopedArabicAtlasOptions, &ScopedFallbackError),
              "fallback Arabic atlas loads");
        const std::vector<char> ScopedInterBytes = ReadFileBytes("assets/fonts/Inter-Regular.ttf");
        const std::vector<char> ScopedArabicFallbackBytes =
            ReadFileBytes("assets/fonts/tests/NotoSansArabic-Regular.ttf");
        Text::HarfBuzzShaper ScopedPrimaryShaper;
        Text::HarfBuzzShaper ScopedArabicShaper;
        Check(ScopedPrimaryShaper.SetFont(
                  std::span<const std::byte>(reinterpret_cast<const std::byte*>(ScopedInterBytes.data()),
                                             ScopedInterBytes.size()),
                  16.0f, 0),
              "fallback primary shaper loads");
        Check(ScopedArabicShaper.SetFont(
                  std::span<const std::byte>(
                      reinterpret_cast<const std::byte*>(ScopedArabicFallbackBytes.data()),
                      ScopedArabicFallbackBytes.size()),
                  16.0f, 1),
              "fallback Arabic shaper loads");
        Text::ChainedShaper ScopedFallbackChain;
        std::array<Text::ITextShaper*, 2> ScopedFallbackBackends{&ScopedPrimaryShaper, &ScopedArabicShaper};
        Check(ScopedFallbackChain.SetShapers(std::span<Text::ITextShaper*>(ScopedFallbackBackends)),
              "fallback chain configures");
        Text::TextShapeParams ScopedFallbackParams;
        ScopedFallbackParams.Script = Text::TextScript::Arabic;
        ScopedFallbackParams.Direction = Text::TextDirection::RightToLeft;
        ScopedFallbackParams.PixelSize = 16.0f;
        Text::TextShapeResult ScopedFallbackShaped;
        Check(ScopedFallbackChain.Shape("\xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7", ScopedFallbackParams,
                                        &ScopedFallbackShaped) &&
                  ScopedFallbackShaped.Count > 0 && ScopedFallbackShaped.Glyphs[0].FontId == 1,
              "fallback chain picks the Arabic font");
        Text::TextRenderParams ScopedFallbackRender;
        ScopedFallbackRender.Fonts = &ScopedFallbackFonts;
        ScopedFallbackRender.FontAtlasIndex = 0;
        ScopedFallbackRender.FontSize = 16.0f;
        std::array<Rhi::RenderItem, 32> ScopedFallbackItems{};
        Text::TextRenderStats ScopedFallbackStats;
        Check(Text::BuildShapedTextRenderItems(ScopedFallbackShaped, Vector2D{0.0f, 0.0f}, ScopedFallbackRender,
                                               std::span<Rhi::RenderItem>(ScopedFallbackItems),
                                               &ScopedFallbackStats) > 0 &&
                  ScopedFallbackStats.MissingGlyphs == 0,
              "fallback glyphs select their own atlas layer");
    }

    VirtualFileSystem ScopedScriptVfs;
    std::string ScopedVfsError;
    if (ScopedScriptVfs.MountDirectory("assets", 0, &ScopedVfsError))
    {
        Ui::FontBuildOptions ScopedArabicOptions;
        ScopedArabicOptions.pixelSize = 16.0f;
        ScopedArabicOptions.atlasSize = 2048;
        ScopedArabicOptions.allGlyphs = true;
        ScopedArabicOptions.codepoints = {};
        Ui::FontAtlasSet ScopedArabicFonts;
        Check(ScopedArabicFonts.LoadTtfFromVfs(ScopedScriptVfs, "fonts/tests/NotoSansArabic-Regular.ttf",
                                              ScopedArabicOptions, &ScopedVfsError),
              "Arabic full-glyph SDF atlas loads");
        const std::vector<char> ScopedArabicBytes =
            ReadFileBytes("assets/fonts/tests/NotoSansArabic-Regular.ttf");
        Text::HarfBuzzShaper ScopedArabicShaper;
        Check(ScopedArabicShaper.SetFont(
                  std::span<const std::byte>(reinterpret_cast<const std::byte*>(ScopedArabicBytes.data()),
                                             ScopedArabicBytes.size()),
                  16.0f, 0),
              "Arabic shaping font loads");
        Text::TextShapeParams ScopedArabicParams;
        ScopedArabicParams.Script = Text::TextScript::Arabic;
        ScopedArabicParams.Direction = Text::TextDirection::RightToLeft;
        ScopedArabicParams.PixelSize = 16.0f;
        Text::TextShapeResult ScopedArabicShaped;
        Check(ScopedArabicShaper.Shape("\xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7", ScopedArabicParams,
                                      &ScopedArabicShaped),
              "Arabic text shapes");
        Text::TextRenderParams ScopedArabicRender;
        ScopedArabicRender.Fonts = &ScopedArabicFonts;
        ScopedArabicRender.FontAtlasIndex = 0;
        ScopedArabicRender.FontSize = 16.0f;
        std::array<Rhi::RenderItem, 32> ScopedArabicItems{};
        Text::TextRenderStats ScopedArabicStats;
        Check(Text::BuildShapedTextRenderItems(ScopedArabicShaped, Vector2D{0.0f, 0.0f}, ScopedArabicRender,
                                               std::span<Rhi::RenderItem>(ScopedArabicItems),
                                               &ScopedArabicStats) > 0 &&
                  ScopedArabicStats.MissingGlyphs == 0,
              "Arabic contextual glyph ids render from the full SDF atlas");
    }

    const std::vector<char> ScopedLifecycleBytes = ReadFileBytes("assets/fonts/Inter-Regular.ttf");
    if (!ScopedLifecycleBytes.empty())
    {
        Text::HarfBuzzShaper ScopedLifecycle;
        for (std::uint32_t ScopedIteration = 0; ScopedIteration < 8; ++ScopedIteration)
        {
            Check(ScopedLifecycle.SetFont(
                      std::span<const std::byte>(reinterpret_cast<const std::byte*>(ScopedLifecycleBytes.data()),
                                                 ScopedLifecycleBytes.size()),
                      16.0f, 0),
                  "HarfBuzz lifecycle font load");
            Text::TextShapeParams ScopedLifecycleParams;
            ScopedLifecycleParams.PixelSize = 16.0f;
            Text::TextShapeResult ScopedLifecycleResult;
            Check(ScopedLifecycle.Shape("A", ScopedLifecycleParams, &ScopedLifecycleResult),
                  "HarfBuzz lifecycle shape");
            ScopedLifecycle.Reset();
            Check(!ScopedLifecycle.IsReady(), "HarfBuzz Reset releases the font");
            Check(Text::HarfBuzzShaper::ActiveFonts() == 0, "HarfBuzz resource counter is balanced");
        }
    }

    MarkerShaper ScopedArabicMarker(11);
    MarkerShaper ScopedLatinMarker(22);
    MarkerShaper ScopedFallbackMarker(33);
    Text::ScriptShaper ScopedScriptRouter;
    Check(ScopedScriptRouter.AddScript(Text::TextScript::Arabic, &ScopedArabicMarker),
          "script router adds Arabic");
    Check(ScopedScriptRouter.AddScript(Text::TextScript::Latin, &ScopedLatinMarker),
          "script router adds Latin");
    ScopedScriptRouter.SetFallback(&ScopedFallbackMarker);
    Text::TextShapeParams ScopedRouterParams;
    ScopedRouterParams.Script = Text::TextScript::Arabic;
    Text::TextShapeResult ScopedRouterResult;
    Check(ScopedScriptRouter.Shape("A", ScopedRouterParams, &ScopedRouterResult) &&
              ScopedRouterResult.Glyphs[0].GlyphId == 11,
          "script router selects Arabic");
    ScopedRouterParams.Script = Text::TextScript::Thai;
    Check(ScopedScriptRouter.Shape("A", ScopedRouterParams, &ScopedRouterResult) &&
              ScopedRouterResult.Glyphs[0].GlyphId == 33,
          "script router uses fallback for an unmapped script");

    VirtualFileSystem ScopedCjkVfs;
    std::string ScopedCjkError;
    if (ScopedCjkVfs.MountDirectory("assets", 0, &ScopedCjkError))
    {
        Ui::FontBuildOptions ScopedCjkAtlasOptions;
        ScopedCjkAtlasOptions.fontId = 4;
        ScopedCjkAtlasOptions.pixelSize = 16.0f;
        ScopedCjkAtlasOptions.atlasSize = 2048;
        ScopedCjkAtlasOptions.allGlyphs = true;
        ScopedCjkAtlasOptions.codepoints = {};
        Ui::FontAtlasSet ScopedCjkFonts;
        Check(ScopedCjkFonts.LoadPagedTtfFromVfs(ScopedCjkVfs, "fonts/tests/NotoSansSC-Regular.ttf",
                                                 ScopedCjkAtlasOptions, &ScopedCjkError),
              "paged CJK SDF atlas loads for rendering");
        const std::vector<char> ScopedCjkRenderBytes =
            ReadFileBytes("assets/fonts/tests/NotoSansSC-Regular.ttf");
        Text::HarfBuzzShaper ScopedCjkRenderShaper;
        Check(ScopedCjkRenderShaper.SetFont(
                  std::span<const std::byte>(reinterpret_cast<const std::byte*>(ScopedCjkRenderBytes.data()),
                                             ScopedCjkRenderBytes.size()),
                  16.0f, 4),
              "CJK render shaper loads");
        Text::TextShapeParams ScopedCjkRenderParams;
        ScopedCjkRenderParams.Script = Text::TextScript::Han;
        ScopedCjkRenderParams.Direction = Text::TextDirection::LeftToRight;
        ScopedCjkRenderParams.PixelSize = 16.0f;
        Text::TextShapeResult ScopedCjkRenderShaped;
        Check(ScopedCjkRenderShaper.Shape("\xE4\xBD\xA0\xE5\xA5\xBD", ScopedCjkRenderParams,
                                        &ScopedCjkRenderShaped),
              "CJK shapes for SDF rendering");
        // The paged face builds the pages the shaped run needs and keeps the
        // full glyph catalogue addressable.
        Check(Text::EnsureShapedGlyphPages(ScopedCjkFonts, ScopedCjkRenderShaped) >= 0,
              "CJK pages warm for the shaped run");
        Check(ScopedCjkFonts.PageCount(4) >= 1 && ScopedCjkFonts.AtlasPixelBytes() < 64u * 1024u * 1024u,
              "the CJK face keeps one bounded page per atlas slot");
        Text::TextRenderParams ScopedCjkRender;
        ScopedCjkRender.Fonts = &ScopedCjkFonts;
        ScopedCjkRender.FontAtlasIndex = 0;
        ScopedCjkRender.FontSize = 16.0f;
        std::array<Rhi::RenderItem, 8> ScopedCjkItems{};
        Text::TextRenderStats ScopedCjkStats;
        Check(Text::BuildShapedTextRenderItems(ScopedCjkRenderShaped, Vector2D{0.0f, 0.0f}, ScopedCjkRender,
                                               std::span<Rhi::RenderItem>(ScopedCjkItems), &ScopedCjkStats) == 2 &&
                  ScopedCjkStats.MissingGlyphs == 0,
              "CJK glyphs render from their paged SDF atlas");
    }

    VirtualFileSystem ScopedStackVfs;
    std::string ScopedStackError;
    if (ScopedStackVfs.MountDirectory("assets", 0, &ScopedStackError))
    {
        auto ScopedStack = std::make_unique<Text::TextFontStack>();
        Check(ScopedStack->AddFont(ScopedStackVfs, "fonts/Inter-Regular.ttf", 0, Text::TextScript::Latin, 16.0f,
                                   2048, true, &ScopedStackError),
              "text font stack adds Latin");
        Check(ScopedStack->AddFont(ScopedStackVfs, "fonts/tests/NotoSansArabic-Regular.ttf", 1,
                                   Text::TextScript::Arabic, 16.0f, 2048, true, &ScopedStackError),
              "text font stack adds Arabic");
        Text::TextShapeParams ScopedStackParams;
        ScopedStackParams.Script = Text::TextScript::Arabic;
        ScopedStackParams.Direction = Text::TextDirection::RightToLeft;
        ScopedStackParams.PixelSize = 16.0f;
        Text::TextShapeResult ScopedStackShaped;
        Check(ScopedStack->Shaper().Shape("\xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7", ScopedStackParams,
                                           &ScopedStackShaped),
              "text font stack routes Arabic shaping");
        Check(ScopedStack->Count() == 2 && ScopedStackShaped.Glyphs[0].FontId == 1,
              "text font stack carries the Arabic font id");
    }

    // Script stack: every face owns an atlas, the shaper routes each bidi run,
    // and the paint pass selects the atlas through ShapedGlyph.FontId.
    VirtualFileSystem ScopedFaceVfs;
    std::string ScopedFaceError;
    if (ScopedFaceVfs.MountDirectory("assets", 0, &ScopedFaceError))
    {
        const std::uint32_t ScopedFontsBefore = Text::HarfBuzzShaper::ActiveFonts();
        const std::array<std::uint32_t, 5> ScopedHanCodepoints = {0x4F60u, 0x597Du, 0xFF0Cu, 0x4E16u, 0x754Cu};
        auto ScopedFaces = std::make_unique<Text::TextFontStack>();
        Check(ScopedFaces->AddFont(ScopedFaceVfs, "fonts/Inter-Regular.ttf", 0, Text::TextScript::Latin, 24.0f, 0,
                                   false, std::span<const std::uint32_t>(), &ScopedFaceError),
              "script stack mounts the Latin face");
        Check(ScopedFaces->AddFont(ScopedFaceVfs, "fonts/tests/NotoSansArabic-Regular.ttf", 1,
                                   Text::TextScript::Arabic, 24.0f, 0, true,
                                   std::span<const std::uint32_t>(), &ScopedFaceError),
              "script stack mounts the Arabic face");
        Check(ScopedFaces->AddFont(ScopedFaceVfs, "fonts/tests/NotoSansSC-Regular.ttf", 4, Text::TextScript::Han,
                                   24.0f, 0, false, std::span<const std::uint32_t>(ScopedHanCodepoints),
                                   &ScopedFaceError),
              "script stack mounts a Han face for the text codepoints");
        ScopedFaces->SetDefaultScript(Text::TextScript::Latin);
        Check(ScopedFaces->Count() == 3 && ScopedFaces->Slot(2).Glyphs != 0,
              "script stack reports one atlas per mounted face");

        const std::string_view ScopedMixedText = "abc \xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7 \xE4\xBD\xA0\xE5\xA5\xBD";
        Text::TextShapeParams ScopedMixedParams;
        ScopedMixedParams.PixelSize = 24.0f;
        Text::TextShapeResult ScopedMixedShaped;
        Check(Text::ShapeBidiText(ScopedFaces->Shaper(), ScopedMixedText, ScopedMixedParams, &ScopedMixedShaped),
              "mixed script text shapes through the router");
        bool ScopedSawLatin = false;
        bool ScopedSawArabic = false;
        bool ScopedSawHan = false;
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedMixedShaped.Count; ++ScopedIndex)
        {
            const std::uint32_t ScopedFontId = ScopedMixedShaped.Glyphs[ScopedIndex].FontId;
            ScopedSawLatin = ScopedSawLatin || ScopedFontId == 0;
            ScopedSawArabic = ScopedSawArabic || ScopedFontId == 1;
            ScopedSawHan = ScopedSawHan || ScopedFontId == 4;
        }
        Check(ScopedSawLatin && ScopedSawArabic && ScopedSawHan,
              "each bidi run keeps the font id of its own face");

        Text::TextLayoutParams ScopedFaceLayoutParams;
        ScopedFaceLayoutParams.FontSize = 24.0f;
        Text::ShapedLayoutResult ScopedFaceLayout;
        Check(Text::LayoutShapedGlyphs(ScopedMixedText, ScopedMixedShaped, ScopedFaceLayoutParams,
                                       &ScopedFaceLayout),
              "mixed script text lays out");
        Text::TextRenderParams ScopedFaceRender;
        ScopedFaceRender.Fonts = &ScopedFaces->Fonts();
        ScopedFaceRender.FontSize = 24.0f;
        std::array<Rhi::RenderItem, 64> ScopedFaceItems{};
        Text::TextRenderStats ScopedFaceStats;
        const std::uint32_t ScopedFaceItemsWritten =
            Text::BuildShapedTextRenderItems(ScopedFaceLayout, ScopedMixedShaped, Vector2D{0.0f, 0.0f},
                                             ScopedFaceRender, std::span<Rhi::RenderItem>(ScopedFaceItems),
                                             &ScopedFaceStats);
        std::uint32_t ScopedInklessGlyphs = 0;
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedMixedShaped.Count; ++ScopedIndex)
        {
            const Text::ShapedGlyph& ScopedGlyph = ScopedMixedShaped.Glyphs[ScopedIndex];
            const Ui::FontAtlas* ScopedResolved = nullptr;
            const Ui::GlyphRecord* ScopedRecord = ScopedFaces->Fonts().ResolveGlyphById(
                ScopedGlyph.FontId, Ui::FontStyle::Regular, ScopedGlyph.GlyphId, &ScopedResolved);
            if (ScopedRecord != nullptr && (ScopedRecord->width <= 0.0f || ScopedRecord->height <= 0.0f))
            {
                ++ScopedInklessGlyphs;
            }
        }
        Check(ScopedFaceItemsWritten + ScopedInklessGlyphs == ScopedMixedShaped.Count &&
                  ScopedFaceStats.MissingGlyphs == 0 && ScopedInklessGlyphs != 0,
              "every script glyph resolves through its own atlas and whitespace is not missing");

        Text::TextShapeParams ScopedForcedParams;
        ScopedForcedParams.PixelSize = 24.0f;
        ScopedForcedParams.Script = Text::TextScript::Arabic;
        Text::TextShapeResult ScopedForcedShaped;
        Check(Text::ShapeBidiText(ScopedFaces->Shaper(), "abc", ScopedForcedParams, &ScopedForcedShaped),
              "a forced script shapes through the router");
        Check(ScopedForcedShaped.Count != 0 && ScopedForcedShaped.Glyphs[0].FontId == 1,
              "a forced script switches the face, not only the parameters");

        ScopedFaces->Reset();
        Check(ScopedFaces->Count() == 0 && ScopedFaces->Fonts().Count() == 0,
              "resetting the script stack drops every atlas");
        Check(Text::HarfBuzzShaper::ActiveFonts() == ScopedFontsBefore,
              "resetting the script stack releases every HarfBuzz font");
    }

    if (GFailures != 0)
    {
        std::fprintf(stderr, "%d text shaper test(s) failed\n", GFailures);
        return 1;
    }
    std::printf("text shaper tests: ok\n");
    return 0;
}
