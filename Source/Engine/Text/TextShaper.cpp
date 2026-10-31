// FemboyEngine2D - compatibility text shaper.
#include "Engine/Text/TextShaper.hpp"

#include "Engine/Text/TextBidi.hpp"
#include "Engine/Text/TextLayout.hpp"

#if defined(FBY_HAS_HARFBUZZ)
#include <hb-ot.h>
#include <hb.h>

#include <vector>
#endif

namespace Fby::Text
{
#if defined(FBY_HAS_HARFBUZZ)
namespace
{
std::atomic<std::uint32_t> GHbActiveFonts{0};
[[nodiscard]] hb_script_t ToHarfBuzzScript(TextScript InScript) noexcept
{
    switch (InScript)
    {
        case TextScript::Latin:
            return HB_SCRIPT_LATIN;
        case TextScript::Cyrillic:
            return HB_SCRIPT_CYRILLIC;
        case TextScript::Greek:
            return HB_SCRIPT_GREEK;
        case TextScript::Arabic:
            return HB_SCRIPT_ARABIC;
        case TextScript::Hebrew:
            return HB_SCRIPT_HEBREW;
        case TextScript::Devanagari:
            return HB_SCRIPT_DEVANAGARI;
        case TextScript::Bengali:
            return HB_SCRIPT_BENGALI;
        case TextScript::Thai:
            return HB_SCRIPT_THAI;
        case TextScript::Han:
            return HB_SCRIPT_HAN;
        case TextScript::Hiragana:
            return HB_SCRIPT_HIRAGANA;
        case TextScript::Katakana:
            return HB_SCRIPT_KATAKANA;
        case TextScript::Hangul:
            return HB_SCRIPT_HANGUL;
        case TextScript::Unknown:
            return HB_SCRIPT_UNKNOWN;
    }
    return HB_SCRIPT_UNKNOWN;
}

[[nodiscard]] hb_direction_t ToHarfBuzzDirection(TextDirection InDirection) noexcept
{
    switch (InDirection)
    {
        case TextDirection::RightToLeft:
            return HB_DIRECTION_RTL;
        case TextDirection::TopToBottom:
            return HB_DIRECTION_TTB;
        case TextDirection::BottomToTop:
            return HB_DIRECTION_BTT;
        case TextDirection::LeftToRight:
            return HB_DIRECTION_LTR;
        case TextDirection::Auto:
            return HB_DIRECTION_INVALID;
    }
    return HB_DIRECTION_INVALID;
}
}
#endif

bool CodepointShaper::Shape(std::string_view InText, const TextShapeParams& InParams,
                            TextShapeResult* OutResult) noexcept
{
    if (OutResult == nullptr)
    {
        return false;
    }
    ++ShapeCalls;
    OutResult->Count = 0;
    OutResult->Overflow = false;
    std::size_t ScopedCursor = 0;
    while (ScopedCursor < InText.size())
    {
        const std::size_t ScopedCluster = ScopedCursor;
        const std::uint32_t ScopedCodepoint = DecodeTextUtf8(InText, ScopedCursor);
        if (OutResult->Count >= OutResult->Glyphs.size())
        {
            OutResult->Overflow = true;
            return false;
        }
        ShapedGlyph& ScopedGlyph = OutResult->Glyphs[OutResult->Count++];
        ScopedGlyph.GlyphId = ScopedCodepoint;
        ScopedGlyph.Cluster = static_cast<std::uint32_t>(ScopedCluster);
        ScopedGlyph.FontId = InParams.FontId;
        ScopedGlyph.AdvanceX = InParams.PixelSize * 0.5f;
    }
    (void)InParams;
    return true;
}

#if defined(FBY_HAS_HARFBUZZ)
struct HarfBuzzShaper::Impl
{
    std::vector<std::byte> bytes{};
    hb_blob_t* blob = nullptr;
    hb_face_t* face = nullptr;
    hb_font_t* font = nullptr;
    // One buffer per face, cleared between runs: shaping must not allocate on
    // the hot path.
    hb_buffer_t* buffer = nullptr;
    float pixelSize = 16.0f;
    std::uint32_t fontId = 0;
};

HarfBuzzShaper::HarfBuzzShaper()
    : MImpl(new Impl())
{
}

HarfBuzzShaper::~HarfBuzzShaper()
{
    Reset();
    delete MImpl;
}

bool HarfBuzzShaper::SetFont(std::span<const std::byte> InFontBytes, float InPixelSize,
                             std::uint32_t InFontId) noexcept
{
    if (MImpl == nullptr || InFontBytes.empty())
    {
        return false;
    }
    Reset();
    MImpl->bytes.assign(InFontBytes.begin(), InFontBytes.end());
    MImpl->pixelSize = InPixelSize > 0.0f ? InPixelSize : 16.0f;
    MImpl->fontId = InFontId;
    MImpl->blob = hb_blob_create(reinterpret_cast<const char*>(MImpl->bytes.data()),
                                 static_cast<unsigned int>(MImpl->bytes.size()), HB_MEMORY_MODE_READONLY,
                                 nullptr, nullptr);
    if (MImpl->blob == nullptr)
    {
        return false;
    }
    MImpl->face = hb_face_create(MImpl->blob, 0);
    if (MImpl->face == nullptr)
    {
        return false;
    }
    MImpl->font = hb_font_create(MImpl->face);
    if (MImpl->font == nullptr)
    {
        return false;
    }
    hb_ot_font_set_funcs(MImpl->font);
    MImpl->buffer = hb_buffer_create();
    if (MImpl->buffer == nullptr)
    {
        return false;
    }
    ++GHbActiveFonts;
    const int ScopedScale = static_cast<int>(MImpl->pixelSize * 64.0f);
    hb_font_set_scale(MImpl->font, ScopedScale, ScopedScale);
    hb_font_set_ppem(MImpl->font, static_cast<unsigned int>(MImpl->pixelSize),
                     static_cast<unsigned int>(MImpl->pixelSize));
    return true;
}

void HarfBuzzShaper::Reset() noexcept
{
    if (MImpl == nullptr)
    {
        return;
    }
    if (MImpl->buffer != nullptr)
    {
        hb_buffer_destroy(MImpl->buffer);
        MImpl->buffer = nullptr;
    }
    if (MImpl->font != nullptr)
    {
        hb_font_destroy(MImpl->font);
        MImpl->font = nullptr;
        if (GHbActiveFonts > 0)
        {
            --GHbActiveFonts;
        }
    }
    if (MImpl->face != nullptr)
    {
        hb_face_destroy(MImpl->face);
        MImpl->face = nullptr;
    }
    if (MImpl->blob != nullptr)
    {
        hb_blob_destroy(MImpl->blob);
        MImpl->blob = nullptr;
    }
    MImpl->bytes.clear();
}

std::uint32_t HarfBuzzShaper::ActiveFonts() noexcept
{
    return GHbActiveFonts.load();
}

bool HarfBuzzShaper::IsReady() const noexcept
{
    return MImpl != nullptr && MImpl->font != nullptr;
}

bool HarfBuzzShaper::Shape(std::string_view InText, const TextShapeParams& InParams,
                           TextShapeResult* OutResult) noexcept
{
    if (!IsReady() || OutResult == nullptr)
    {
        return false;
    }
    OutResult->Count = 0;
    OutResult->Overflow = false;
    hb_buffer_t* ScopedBuffer = MImpl->buffer;
    if (ScopedBuffer == nullptr)
    {
        return false;
    }
    hb_buffer_clear_contents(ScopedBuffer);
    hb_buffer_add_utf8(ScopedBuffer, InText.data(), static_cast<int>(InText.size()), 0,
                       static_cast<int>(InText.size()));
    const hb_direction_t ScopedDirection = ToHarfBuzzDirection(InParams.Direction);
    if (ScopedDirection != HB_DIRECTION_INVALID)
    {
        hb_buffer_set_direction(ScopedBuffer, ScopedDirection);
    }
    hb_buffer_set_script(ScopedBuffer, ToHarfBuzzScript(InParams.Script));
    if (!InParams.Language.empty())
    {
        hb_buffer_set_language(ScopedBuffer,
                               hb_language_from_string(InParams.Language.data(),
                                                       static_cast<int>(InParams.Language.size())));
    }
    hb_buffer_guess_segment_properties(ScopedBuffer);
    hb_feature_t ScopedFeatures[3]{};
    unsigned int ScopedFeatureCount = 0;
    if (InParams.Vertical)
    {
        hb_feature_from_string("vert=1", -1, &ScopedFeatures[ScopedFeatureCount++]);
        hb_feature_from_string("vrt2=1", -1, &ScopedFeatures[ScopedFeatureCount++]);
        hb_feature_from_string("vkrn=1", -1, &ScopedFeatures[ScopedFeatureCount++]);
    }
    hb_shape(MImpl->font, ScopedBuffer, ScopedFeatures, ScopedFeatureCount);
    unsigned int ScopedGlyphCount = 0;
    const hb_glyph_info_t* ScopedInfos = hb_buffer_get_glyph_infos(ScopedBuffer, &ScopedGlyphCount);
    const hb_glyph_position_t* ScopedPositions = hb_buffer_get_glyph_positions(ScopedBuffer, &ScopedGlyphCount);
    if (ScopedInfos == nullptr || ScopedPositions == nullptr)
    {
        hb_buffer_clear_contents(ScopedBuffer);
        return false;
    }
    for (unsigned int ScopedIndex = 0; ScopedIndex < ScopedGlyphCount; ++ScopedIndex)
    {
        if (OutResult->Count >= OutResult->Glyphs.size())
        {
            OutResult->Overflow = true;
            hb_buffer_clear_contents(ScopedBuffer);
            return false;
        }
        ShapedGlyph& ScopedGlyph = OutResult->Glyphs[OutResult->Count++];
        ScopedGlyph.GlyphId = ScopedInfos[ScopedIndex].codepoint;
        ScopedGlyph.Cluster = ScopedInfos[ScopedIndex].cluster;
        ScopedGlyph.FontId = MImpl->fontId;
        ScopedGlyph.AdvanceX = static_cast<float>(ScopedPositions[ScopedIndex].x_advance) / 64.0f;
        ScopedGlyph.AdvanceY = static_cast<float>(ScopedPositions[ScopedIndex].y_advance) / 64.0f;
        ScopedGlyph.OffsetX = static_cast<float>(ScopedPositions[ScopedIndex].x_offset) / 64.0f;
        ScopedGlyph.OffsetY = static_cast<float>(ScopedPositions[ScopedIndex].y_offset) / 64.0f;
    }
    hb_buffer_clear_contents(ScopedBuffer);
    return true;
}
#else
struct HarfBuzzShaper::Impl
{
};

HarfBuzzShaper::HarfBuzzShaper()
    : MImpl(new Impl())
{
}
HarfBuzzShaper::~HarfBuzzShaper()
{
    delete MImpl;
}
bool HarfBuzzShaper::SetFont(std::span<const std::byte>, float, std::uint32_t) noexcept
{
    return false;
}
void HarfBuzzShaper::Reset() noexcept
{
}
std::uint32_t HarfBuzzShaper::ActiveFonts() noexcept
{
    return 0;
}

bool HarfBuzzShaper::IsReady() const noexcept
{
    return false;
}
bool HarfBuzzShaper::Shape(std::string_view, const TextShapeParams&, TextShapeResult*) noexcept
{
    return false;
}
#endif

bool ShapeBidiText(ITextShaper& InShaper, std::string_view InText, const TextShapeParams& InParams,
                   TextShapeResult* OutResult) noexcept
{
    if (OutResult == nullptr)
    {
        return false;
    }
    std::array<TextRun, kMaxTextRuns> ScopedRuns{};
    std::uint32_t ScopedRunCount = 0;
    if (!SplitRtlRuns(InText, InParams.Direction, std::span<TextRun>(ScopedRuns), &ScopedRunCount) ||
        ScopedRunCount == 0)
    {
        return InShaper.Shape(InText, InParams, OutResult);
    }
    OutResult->Count = 0;
    OutResult->Overflow = false;
    // One reusable part buffer for every run: the shaped result is large and
    // must not be rebuilt per run.
    TextShapeResult ScopedPart;
    for (std::uint32_t ScopedRunIndex = 0; ScopedRunIndex < ScopedRunCount; ++ScopedRunIndex)
    {
        const TextRun& ScopedRun = ScopedRuns[ScopedRunIndex];
        if (ScopedRun.End <= ScopedRun.Begin || ScopedRun.End > InText.size())
        {
            continue;
        }
        TextShapeParams ScopedRunParams = InParams;
        ScopedRunParams.Direction = ScopedRun.Direction;
        ScopedRunParams.Script = InParams.Script != TextScript::Unknown ? InParams.Script : ScopedRun.Script;
        if (!InShaper.Shape(InText.substr(ScopedRun.Begin, ScopedRun.End - ScopedRun.Begin),
                            ScopedRunParams, &ScopedPart))
        {
            return false;
        }
        if (OutResult->Count + ScopedPart.Count > OutResult->Glyphs.size())
        {
            OutResult->Overflow = true;
            return false;
        }
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedPart.Count; ++ScopedIndex)
        {
            ShapedGlyph ScopedGlyph = ScopedPart.Glyphs[ScopedIndex];
            ScopedGlyph.Cluster += ScopedRun.Begin;
            OutResult->Glyphs[OutResult->Count++] = ScopedGlyph;
        }
    }
    return true;
}
}
