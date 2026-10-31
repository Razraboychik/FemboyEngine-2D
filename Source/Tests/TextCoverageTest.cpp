// FemboyEngine2D - coverage-based script font selection and fallback test.
#include "Engine/Assets/VFS.hpp"
#include "Engine/Text/TextFontStack.hpp"
#include "Engine/UI/FontBuilder.hpp"
#include "Engine/UI/FontCoverage.hpp"

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

[[nodiscard]] Fby::Ui::FontCoverage ReadCoverage(Fby::VirtualFileSystem& InVfs, const char* InPath)
{
    Fby::Ui::FontCoverage ScopedCoverage;
    const auto ScopedBytes = InVfs.Read(InPath);
    if (ScopedBytes)
    {
        (void)Fby::Ui::BuildFontCoverageFromTtf(*ScopedBytes, &ScopedCoverage);
    }
    return ScopedCoverage;
}
}

int main()
{
    using namespace Fby;

    VirtualFileSystem ScopedVfs;
    std::string ScopedError;
    Check(ScopedVfs.MountDirectory("assets", 0, &ScopedError), "coverage test mounts the assets");

    // Coverage masks describe the real cmap, not the script a caller declares.
    const Ui::FontCoverage ScopedLatin = ReadCoverage(ScopedVfs, "fonts/Inter-Regular.ttf");
    const Ui::FontCoverage ScopedMono = ReadCoverage(ScopedVfs, "fonts/JetBrainsMono-Regular.ttf");
    const Ui::FontCoverage ScopedArabic = ReadCoverage(ScopedVfs, "fonts/tests/NotoSansArabic-Regular.ttf");
    const Ui::FontCoverage ScopedHan = ReadCoverage(ScopedVfs, "fonts/tests/NotoSansSC-Regular.ttf");
    Check(ScopedLatin.Covers('A') && !ScopedLatin.Covers(0x4F60u),
          "the Latin face covers Latin and not Han");
    Check(ScopedMono.Covers('A') && !ScopedMono.Covers(0x0627u), "the mono face covers Latin and not Arabic");
    Check(ScopedArabic.Covers(0x0627u), "the Arabic face covers the Arabic letter alef");
    Check(ScopedHan.Covers(0x4F60u) && ScopedHan.Covers(0x597Du), "the Han face covers the CJK sample");
    Check(ScopedHan.CoveredCount() > ScopedArabic.CoveredCount(),
          "the CJK catalogue is larger than the Arabic one");
    Check(ScopedLatin.CoveredCount() > 100 && ScopedLatin.CoveredCount() < Ui::kCoverageCodepoints,
          "the coverage mask covers a plausible share of the BMP");

    // A script family mounts its faces without hand-filling the stack, and the
    // chain falls back inside the script: JetBrainsMono shapes no Arabic, so
    // the Arabic face must win even when it is not first.
    const std::array<Text::FontCandidate, 2> ScopedArabicFamily = {
        Text::FontCandidate{"fonts/JetBrainsMono-Regular.ttf", 9, Text::TextScript::Arabic},
        Text::FontCandidate{"fonts/tests/NotoSansArabic-Regular.ttf", 1, Text::TextScript::Arabic},
    };
    Text::TextFontStack ScopedStack;
    Check(ScopedStack.AddScriptFamily(ScopedVfs, std::span<const Text::FontCandidate>(ScopedArabicFamily), 16.0f,
                                      1024, &ScopedError) == 2,
          "the Arabic family mounts both faces");
    Text::TextShapeParams ScopedParams;
    ScopedParams.PixelSize = 16.0f;
    ScopedParams.Script = Text::TextScript::Arabic;
    ScopedParams.Direction = Text::TextDirection::RightToLeft;
    const std::string_view ScopedArabicText = "\xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7";
    Text::TextShapeResult ScopedShaped;
    Check(ScopedStack.Shaper().Shape(ScopedArabicText, ScopedParams, &ScopedShaped),
          "Arabic shapes through the ranked chain");
    bool ScopedArabicFace = ScopedShaped.Count != 0;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedShaped.Count; ++ScopedIndex)
    {
        ScopedArabicFace = ScopedArabicFace && ScopedShaped.Glyphs[ScopedIndex].GlyphId != 0 &&
                           ScopedShaped.Glyphs[ScopedIndex].FontId == 1;
    }
    Check(ScopedArabicFace, "a missing glyph falls through to the next Arabic face");

    // Ranking by the current text puts the face that covers it first.
    std::array<std::uint32_t, 4> ScopedRanked{};
    const std::uint32_t ScopedRankedCount =
        ScopedStack.RankFaces(Text::TextScript::Arabic, ScopedArabicText, std::span<std::uint32_t>(ScopedRanked));
    Check(ScopedRankedCount == 2 && ScopedRanked[0] == 1,
          "ranking puts the face that covers the text first");
    Check(ScopedStack.OrderScriptChain(Text::TextScript::Arabic, std::span<const std::uint32_t>(ScopedRanked.data(), 2)),
          "the ranked order rebuilds the chain");
    Text::TextShapeResult ScopedRankedShaped;
    Check(ScopedStack.Shaper().Shape(ScopedArabicText, ScopedParams, &ScopedRankedShaped) &&
              ScopedRankedShaped.Count != 0 && ScopedRankedShaped.Glyphs[0].FontId == 1,
          "the reordered chain shapes with the best face");

    // Han falls back inside its own script too: the Thai face has no CJK.
    Text::TextFontStack ScopedHanStack;
    const std::array<Text::FontCandidate, 2> ScopedHanFamily = {
        Text::FontCandidate{"fonts/tests/NotoSansThai-Regular.ttf", 3, Text::TextScript::Han},
        Text::FontCandidate{"fonts/tests/NotoSansSC-Regular.ttf", 4, Text::TextScript::Han},
    };
    Check(ScopedHanStack.AddScriptFamily(ScopedVfs, std::span<const Text::FontCandidate>(ScopedHanFamily), 16.0f,
                                         1024, &ScopedError) == 2,
          "the Han family mounts both faces");
    Text::TextShapeParams ScopedHanParams;
    ScopedHanParams.PixelSize = 16.0f;
    ScopedHanParams.Script = Text::TextScript::Han;
    ScopedHanParams.Direction = Text::TextDirection::LeftToRight;
    Text::TextShapeResult ScopedHanShaped;
    Check(ScopedHanStack.Shaper().Shape("\xE4\xBD\xA0\xE5\xA5\xBD", ScopedHanParams, &ScopedHanShaped),
          "CJK shapes through the Han chain");
    Check(ScopedHanShaped.Count == 2 && ScopedHanShaped.Glyphs[0].FontId == 4 &&
              ScopedHanShaped.Glyphs[0].GlyphId != 0,
          "the CJK fallback reaches the Han face");

    // .notdef is reserved for text no face covers.
    Text::TextShapeResult ScopedEmojiShaped;
    Check(ScopedHanStack.Shaper().Shape("\xF0\x9F\x98\x80", ScopedHanParams, &ScopedEmojiShaped),
          "emoji shaping still returns a result");
    Check(ScopedHanShaped.Glyphs[0].GlyphId != 0 && ScopedEmojiShaped.Count == 1 &&
              ScopedEmojiShaped.Glyphs[0].GlyphId == 0,
          "only uncovered text falls back to .notdef");

    if (GFailures != 0)
    {
        std::fprintf(stderr, "%d text coverage test(s) failed\n", GFailures);
        return 1;
    }
    std::printf("text coverage tests: ok\n");
    return 0;
}
