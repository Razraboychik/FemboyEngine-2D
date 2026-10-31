// FemboyEngine2D - golden shaping and layout test for every mounted script.
//
// The fonts are checked in, so the shaped glyph ids and advances are stable.
// This test locks them: any change in HarfBuzz, the feature set, the script
// routing or the font subset shows up as an exact mismatch instead of a vague
// render difference.
#include "Engine/Assets/VFS.hpp"
#include "Engine/Text/TextFontStack.hpp"
#include "Engine/Text/TextRender.hpp"
#include "Engine/Text/TextShapedLayout.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>

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

// One expected glyph: the face that must produce it, the glyph id and the
// advance in 1/64 pixel so the comparison is exact.
struct GoldenGlyph
{
    std::uint32_t FontId;
    std::uint32_t GlyphId;
    std::int32_t AdvanceX64;
};

struct GoldenCase
{
    const char* Name;
    const char* Text;
    Fby::Text::TextScript Script;
    Fby::Text::TextDirection Direction;
    bool Vertical;
    std::uint32_t GlyphCount;
    GoldenGlyph Glyphs[16];
};

// The goldens are recorded from the checked-in fonts at 16 px. Printing mode
// (`FBY_GOLDEN_PRINT=1`) emits the current table for a re-record.
constexpr std::array<GoldenCase, 5> kGoldenCases{{
    {"latin", "Hello", Fby::Text::TextScript::Latin, Fby::Text::TextDirection::LeftToRight, false, 5,
     {{0, 161, 761}, {0, 614, 597}, {0, 736, 248}, {0, 736, 248}, {0, 790, 614}}},
    {"arabic", "\xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7", Fby::Text::TextScript::Arabic,
     Fby::Text::TextDirection::RightToLeft, false, 6,
     {{1, 9, 298}, {1, 317, 0}, {1, 16, 351}, {1, 27, 603}, {1, 31, 364}, {1, 79, 538}}},
    {"devanagari", "\xE0\xA4\xA8\xE0\xA4\xAE\xE0\xA4\xB8\xE0\xA5\x8D\xE0\xA4\xA4\xE0\xA5\x87",
     Fby::Text::TextScript::Devanagari, Fby::Text::TextDirection::LeftToRight, false, 5,
     {{2, 75, 568}, {2, 80, 612}, {2, 256, 398}, {2, 71, 584}, {2, 40, 0}}},
    {"thai", "\xE0\xB8\xAA\xE0\xB8\xA7\xE0\xB8\xB1\xE0\xB8\xAA\xE0\xB8\x94\xE0\xB8\xB5",
     Fby::Text::TextScript::Thai, Fby::Text::TextDirection::LeftToRight, false, 6,
     {{3, 110, 586}, {3, 134, 504}, {3, 45, 0}, {3, 110, 586}, {3, 12, 631}, {3, 94, 0}}},
    {"cjk", "\xE4\xBD\xA0\xE5\xA5\xBD", Fby::Text::TextScript::Han, Fby::Text::TextDirection::LeftToRight,
     false, 2, {{4, 9112, 1024}, {4, 11679, 1024}}},
}};

// A right-to-left visual order golden: Hebrew is not mounted, so the runs use
// the Latin face but the visual order is what this case locks.
struct GoldenRunOrder
{
    std::uint32_t Script;
    std::uint32_t Direction;
};
}

int main()
{
    using namespace Fby;
    const bool ScopedPrint = std::getenv("FBY_GOLDEN_PRINT") != nullptr;

    VirtualFileSystem ScopedVfs;
    std::string ScopedError;
    Check(ScopedVfs.MountDirectory("assets", 0, &ScopedError), "golden test mounts the assets");

    Text::TextFontStack ScopedStack;
    const std::array<Text::FontCandidate, 4> ScopedCandidates = {
        Text::FontCandidate{"fonts/Inter-Regular.ttf", 0, Text::TextScript::Latin},
        Text::FontCandidate{"fonts/tests/NotoSansArabic-Regular.ttf", 1, Text::TextScript::Arabic},
        Text::FontCandidate{"fonts/tests/NotoSansDevanagari-Regular.ttf", 2, Text::TextScript::Devanagari},
        Text::FontCandidate{"fonts/tests/NotoSansThai-Regular.ttf", 3, Text::TextScript::Thai},
    };
    Check(ScopedStack.AddScriptFamily(ScopedVfs, std::span<const Text::FontCandidate>(ScopedCandidates), 16.0f,
                                      1024, &ScopedError) == 4,
          "golden test mounts every script face");
    Check(ScopedStack.AddPagedFont(ScopedVfs, "fonts/tests/NotoSansSC-Regular.ttf", 4, Text::TextScript::Han,
                                   16.0f, 1024, &ScopedError),
          "golden test mounts the CJK face");
    ScopedStack.SetDefaultScript(Text::TextScript::Latin);

    for (const GoldenCase& ScopedCase : kGoldenCases)
    {
        Text::TextShapeParams ScopedParams;
        ScopedParams.PixelSize = 16.0f;
        ScopedParams.Script = ScopedCase.Script;
        ScopedParams.Direction = ScopedCase.Direction;
        ScopedParams.Vertical = ScopedCase.Vertical;
        Text::TextShapeResult ScopedShaped;
        if (!Text::ShapeBidiText(ScopedStack.Shaper(), ScopedCase.Text, ScopedParams, &ScopedShaped))
        {
            Check(false, ScopedCase.Name);
            continue;
        }
        if (ScopedPrint)
        {
            std::printf("    {\"%s\", \"%s\", TextScript::%u, TextDirection::%u, false, %u,\n     {{\n",
                        ScopedCase.Name, ScopedCase.Text, static_cast<unsigned>(ScopedCase.Script),
                        static_cast<unsigned>(ScopedCase.Direction), ScopedShaped.Count);
            for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedShaped.Count; ++ScopedIndex)
            {
                const Text::ShapedGlyph& ScopedGlyph = ScopedShaped.Glyphs[ScopedIndex];
                std::printf("        {%u, %u, %d},\n", ScopedGlyph.FontId, ScopedGlyph.GlyphId,
                            static_cast<std::int32_t>(ScopedGlyph.AdvanceX * 64.0f + 0.5f));
            }
            std::printf("     }}},\n");
            continue;
        }
        Check(ScopedShaped.Count == ScopedCase.GlyphCount, ScopedCase.Name);
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedShaped.Count && ScopedIndex < 16u; ++ScopedIndex)
        {
            const Text::ShapedGlyph& ScopedGlyph = ScopedShaped.Glyphs[ScopedIndex];
            const GoldenGlyph& ScopedExpected = ScopedCase.Glyphs[ScopedIndex];
            const bool ScopedMatches =
                ScopedGlyph.GlyphId == ScopedExpected.GlyphId &&
                ScopedGlyph.FontId == ScopedExpected.FontId &&
                static_cast<std::int32_t>(ScopedGlyph.AdvanceX * 64.0f + 0.5f) == ScopedExpected.AdvanceX64;
            if (!ScopedMatches)
            {
                std::fprintf(stderr, "FAIL: %s glyph %u: got { %u, %u, %d }\n", ScopedCase.Name, ScopedIndex,
                             ScopedGlyph.FontId, ScopedGlyph.GlyphId,
                             static_cast<std::int32_t>(ScopedGlyph.AdvanceX * 64.0f + 0.5f));
                ++GFailures;
            }
        }
    }

    // Regression: every golden run must render with zero missing glyphs and the
    // pages must be warmed from the shaped result.
    for (const GoldenCase& ScopedCase : kGoldenCases)
    {
        if (ScopedPrint)
        {
            continue;
        }
        Text::TextShapeParams ScopedParams;
        ScopedParams.PixelSize = 16.0f;
        ScopedParams.Script = ScopedCase.Script;
        ScopedParams.Direction = ScopedCase.Direction;
        ScopedParams.Vertical = ScopedCase.Vertical;
        Text::TextShapeResult ScopedShaped;
        if (!Text::ShapeBidiText(ScopedStack.Shaper(), ScopedCase.Text, ScopedParams, &ScopedShaped))
        {
            continue;
        }
        (void)Text::EnsureShapedGlyphPages(ScopedStack.Fonts(), ScopedShaped);
        Text::TextLayoutParams ScopedLayoutParams;
        ScopedLayoutParams.FontSize = 16.0f;
        ScopedLayoutParams.MaxWidth = 320.0f;
        ScopedLayoutParams.Vertical = ScopedCase.Vertical;
        Text::ShapedLayoutResult ScopedLayout;
        Check(Text::LayoutShapedGlyphs(ScopedCase.Text, ScopedShaped, ScopedLayoutParams, &ScopedLayout),
              ScopedCase.Name);
        Text::TextRenderParams ScopedRender;
        ScopedRender.Fonts = &ScopedStack.Fonts();
        ScopedRender.FontSize = 16.0f;
        ScopedRender.Vertical = ScopedCase.Vertical;
        std::array<Rhi::RenderItem, 32> ScopedItems{};
        Text::TextRenderStats ScopedStats;
        const std::uint32_t ScopedWritten =
            Text::BuildShapedTextRenderItems(ScopedLayout, ScopedShaped, Vector2D{0.0f, 0.0f}, ScopedRender,
                                             std::span<Rhi::RenderItem>(ScopedItems), &ScopedStats);
        Check(ScopedStats.MissingGlyphs == 0 && ScopedWritten != 0, ScopedCase.Name);
    }

    if (GFailures != 0)
    {
        std::fprintf(stderr, "%d text golden test(s) failed\n", GFailures);
        return 1;
    }
    std::printf("text golden tests: ok\n");
    return 0;
}
