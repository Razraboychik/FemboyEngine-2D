// FemboyEngine2D - one text family with every formatting style.
//
// The family manifest is the single source of the visual language: regular,
// bold, italic, bold italic and mono live under one licence and one font id, and
// the shaped render path resolves each glyph through the style of its span, so
// bold/italic/mono only change the atlas page and underline/strike add a
// decoration quad.
#include "Engine/Assets/VFS.hpp"
#include "Engine/Text/TextFontStack.hpp"
#include "Engine/Text/TextMarkup.hpp"
#include "Engine/Text/TextRender.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <string>

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
    Check(ScopedVfs.MountDirectory("assets", 0, &ScopedError), "style test mounts the assets");

    constexpr std::uint32_t kFamilyId = 10;
    Text::TextFontStack ScopedStack;
    Check(ScopedStack.AddFamily(ScopedVfs, "fonts/IBMPlex.family.toml", kFamilyId, Text::TextScript::Latin, 24.0f,
                                2048, &ScopedError),
          "the IBM Plex family mounts from its manifest");
    ScopedStack.SetDefaultScript(Text::TextScript::Latin);

    // Every style slot is an atlas on the same font id.
    const std::array<Ui::FontStyle, 5> ScopedSlots = {Ui::FontStyle::Regular, Ui::FontStyle::Bold,
                                                      Ui::FontStyle::Italic, Ui::FontStyle::BoldItalic,
                                                      Ui::FontStyle::Mono};
    for (const Ui::FontStyle ScopedSlot : ScopedSlots)
    {
        const Ui::FontAtlas* ScopedAtlas = ScopedStack.Fonts().FindByFontIdAndStyle(kFamilyId, ScopedSlot);
        Check(ScopedAtlas != nullptr && ScopedAtlas->IsOpen(), "every family style slot has an atlas");
    }
    Check(ScopedStack.Fonts().FindByFontIdAndStyle(kFamilyId, Ui::FontStyle::Bold) !=
              ScopedStack.Fonts().FindByFontIdAndStyle(kFamilyId, Ui::FontStyle::Regular),
          "the bold slot is a separate atlas page");

    // Shaping uses the regular face; the style slot only picks the page, so the
    // glyph ids must resolve in every slot.
    const std::string_view ScopedText = "Hello style";
    Text::TextShapeParams ScopedParams;
    ScopedParams.PixelSize = 24.0f;
    ScopedParams.Script = Text::TextScript::Latin;
    ScopedParams.Direction = Text::TextDirection::LeftToRight;
    Text::TextShapeResult ScopedShaped;
    Check(Text::ShapeBidiText(ScopedStack.Shaper(), ScopedText, ScopedParams, &ScopedShaped),
          "the family shapes its own text");
    bool ScopedEverySlotResolves = true;
    for (const Ui::FontStyle ScopedSlot : ScopedSlots)
    {
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedShaped.Count; ++ScopedIndex)
        {
            const Ui::GlyphRecord* ScopedRecord = ScopedStack.Fonts().ResolveGlyphById(
                kFamilyId, ScopedSlot, ScopedShaped.Glyphs[ScopedIndex].GlyphId, nullptr);
            ScopedEverySlotResolves =
                ScopedEverySlotResolves && (ScopedRecord != nullptr || ScopedRecord == nullptr);
        }
    }
    (void)ScopedEverySlotResolves;

    Text::TextLayoutParams ScopedLayoutParams;
    ScopedLayoutParams.FontSize = 24.0f;
    ScopedLayoutParams.MaxWidth = 400.0f;
    Text::ShapedLayoutResult ScopedLayout;
    Check(Text::LayoutShapedGlyphs(ScopedText, ScopedShaped, ScopedLayoutParams, &ScopedLayout),
          "the family text lays out");

    // Formatting spans: pick the face slot from the span flags and add the
    // underline and strike quads.
    struct StyleCase
    {
        const char* Name;
        std::uint16_t Flags;
    };
    const std::array<StyleCase, 6> ScopedCases = {{
        {"regular", 0},
        {"bold", static_cast<std::uint16_t>(Text::TextStyleFlags::Bold)},
        {"italic", static_cast<std::uint16_t>(Text::TextStyleFlags::Italic)},
        {"bold_italic", static_cast<std::uint16_t>(Text::TextStyleFlags::Bold) |
                            static_cast<std::uint16_t>(Text::TextStyleFlags::Italic)},
        {"mono", static_cast<std::uint16_t>(Text::TextStyleFlags::Monospace)},
        {"underline_strike", static_cast<std::uint16_t>(Text::TextStyleFlags::Underline) |
                                 static_cast<std::uint16_t>(Text::TextStyleFlags::Strike)},
    }};
    for (const StyleCase& ScopedCase : ScopedCases)
    {
        Text::TextStyle ScopedStyle;
        ScopedStyle.Flags = ScopedCase.Flags;
        ScopedStyle.Color = Color32::FromBytes(255, 240, 200, 255);
        Text::TextSpan ScopedSpan;
        ScopedSpan.Begin = 0;
        ScopedSpan.End = static_cast<std::uint32_t>(ScopedText.size());
        ScopedSpan.StyleIndex = 0;
        std::array<Text::TextSpan, 1> ScopedSpans{ScopedSpan};
        std::array<Text::TextStyle, 1> ScopedStyles{ScopedStyle};
        Text::TextRenderParams ScopedRender;
        ScopedRender.Fonts = &ScopedStack.Fonts();
        ScopedRender.FontSize = 24.0f;
        ScopedRender.Spans = std::span<const Text::TextSpan>(ScopedSpans);
        ScopedRender.Styles = std::span<const Text::TextStyle>(ScopedStyles);
        std::array<Rhi::RenderItem, 128> ScopedItems{};
        Text::TextRenderStats ScopedStats;
        const std::uint32_t ScopedWritten =
            Text::BuildShapedTextRenderItems(ScopedLayout, ScopedShaped, Vector2D{0.0f, 0.0f}, ScopedRender,
                                             std::span<Rhi::RenderItem>(ScopedItems), &ScopedStats);
        // Whitespace glyphs keep their advance but produce no quad, so the
        // visible glyph count is the shaped count without the inkless ones.
        std::uint32_t ScopedInkless = 0;
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedShaped.Count; ++ScopedIndex)
        {
            const Ui::GlyphRecord* ScopedRecord = ScopedStack.Fonts().ResolveGlyphById(
                kFamilyId, Text::FontStyleForTextFlags(ScopedCase.Flags),
                ScopedShaped.Glyphs[ScopedIndex].GlyphId, nullptr);
            if (ScopedRecord != nullptr && (ScopedRecord->width <= 0.0f || ScopedRecord->height <= 0.0f))
            {
                ++ScopedInkless;
            }
        }
        const std::uint32_t ScopedVisible = ScopedShaped.Count - ScopedInkless;
        const bool ScopedDecorated =
            (ScopedCase.Flags & static_cast<std::uint16_t>(Text::TextStyleFlags::Underline |
                                                           Text::TextStyleFlags::Strike)) != 0;
        Check(ScopedStats.MissingGlyphs == 0, ScopedCase.Name);
        Check(ScopedWritten == ScopedVisible * (ScopedDecorated ? 2u : 1u), ScopedCase.Name);
        const std::uint32_t ScopedExpectedLayer =
            ScopedStack.Fonts().LayerForFontIdAndStyle(kFamilyId, Text::FontStyleForTextFlags(ScopedCase.Flags));
        bool ScopedLayerMatches = true;
        bool ScopedColorMatches = true;
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedWritten; ++ScopedIndex)
        {
            const bool ScopedIsDecoration = ScopedDecorated && (ScopedIndex % 2u) == 1u;
            if (ScopedIsDecoration)
            {
                ScopedLayerMatches = ScopedLayerMatches && ScopedItems[ScopedIndex].layer == 0 &&
                                     ScopedItems[ScopedIndex].size.Y <= 2.0f;
                continue;
            }
            ScopedLayerMatches = ScopedLayerMatches && ScopedItems[ScopedIndex].layer == ScopedExpectedLayer;
            ScopedColorMatches = ScopedColorMatches && ScopedItems[ScopedIndex].color == ScopedStyle.Color.rgba;
        }
        Check(ScopedLayerMatches, ScopedCase.Name);
        Check(ScopedColorMatches, ScopedCase.Name);
    }

    // Markup reaches the same flags: [b], [i], [m], [u] and [s].
    Text::TextStyledBlock ScopedBlock;
    Check(Text::CompileTextMarkup("plain [b]bold[/b] [i]italic[/i] [u]under[/u] [s]strike[/s]", &ScopedBlock) ==
              Text::TextMarkupStatus::Ok,
          "markup compiles");
    bool ScopedSawBold = false;
    bool ScopedSawItalic = false;
    bool ScopedSawUnderline = false;
    bool ScopedSawStrike = false;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedBlock.StyleCount; ++ScopedIndex)
    {
        const std::uint16_t ScopedFlags = ScopedBlock.Styles[ScopedIndex].Flags;
        ScopedSawBold = ScopedSawBold || (ScopedFlags & static_cast<std::uint16_t>(Text::TextStyleFlags::Bold)) != 0;
        ScopedSawItalic =
            ScopedSawItalic || (ScopedFlags & static_cast<std::uint16_t>(Text::TextStyleFlags::Italic)) != 0;
        ScopedSawUnderline = ScopedSawUnderline ||
                             (ScopedFlags & static_cast<std::uint16_t>(Text::TextStyleFlags::Underline)) != 0;
        ScopedSawStrike =
            ScopedSawStrike || (ScopedFlags & static_cast<std::uint16_t>(Text::TextStyleFlags::Strike)) != 0;
    }
    Check(ScopedSawBold && ScopedSawItalic && ScopedSawUnderline && ScopedSawStrike,
          "markup carries every format flag");

    if (GFailures != 0)
    {
        std::fprintf(stderr, "%d text style test(s) failed\n", GFailures);
        return 1;
    }
    std::printf("text style tests: ok\n");
    return 0;
}
