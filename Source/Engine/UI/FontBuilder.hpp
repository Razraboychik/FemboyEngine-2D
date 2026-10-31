// FemboyEngine2D - in-engine TTF/OTF -> .fbyfont builder.
#pragma once

#include "Engine/UI/FontAtlas.hpp"
#include "Engine/UI/FontCoverage.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace Fby::Ui
{

struct FontBuildOptions
{
    std::uint32_t fontId = 0;
    FontStyle style = FontStyle::Regular;
    float pixelSize = 32.0f;
    bool sdf = true;
    // Pack every glyph id instead of only the requested codepoints. Required
    // for contextual Arabic/Indic glyphs produced by shaping.
    bool allGlyphs = false;
    std::uint32_t oversample = 1;
    // 0 grows the atlas until the glyphs fit. A family should pin one size so
    // every style layer is compatible with the same texture array.
    std::uint32_t atlasSize = 0;
    // Empty means the built-in ASCII + Latin-1 + Cyrillic set.
    std::span<const std::uint32_t> codepoints{};
    // All-glyph paging. A non-zero page size packs only the glyph ids of page
    // `glyphPageIndex`, so a large CJK face is built a few megabytes at a time.
    std::uint32_t glyphPageSize = 0;
    std::uint32_t glyphPageIndex = 0;
};

// Builds the cooked .fbyfont bytes in memory. This is the same contract the
// offline fby_fontc tool writes, so a runtime-built atlas can be mounted or
// cached without a second loader.
[[nodiscard]] bool BuildFontAtlasFromTtf(std::span<const std::byte> InTtf, const FontBuildOptions& InOptions,
                                         std::vector<std::byte>* OutCooked, std::string* InError = nullptr);

[[nodiscard]] std::vector<std::uint32_t> DefaultFontCodepoints();

// Marks every BMP codepoint that maps to a real glyph in the face. The stack
// builds it once when a face is mounted and uses it to rank fallbacks.
[[nodiscard]] bool BuildFontCoverageFromTtf(std::span<const std::byte> InTtf, FontCoverage* OutCoverage);
}
