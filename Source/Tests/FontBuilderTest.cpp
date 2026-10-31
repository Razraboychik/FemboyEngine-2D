// FemboyEngine2D - in-engine TTF builder and style-slot test.
#include "Engine/Assets/VFS.hpp"
#include "Engine/UI/FontAtlas.hpp"
#include "Engine/UI/FontFamilyAsset.hpp"
#include "Engine/UI/FontBuilder.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
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
    Check(ScopedVfs.MountDirectory("assets", 0, &ScopedError), "font asset directory mounts");

    std::array<std::uint32_t, 1> ScopedCodepoints{'A'};
    Ui::FontBuildOptions ScopedOptions;
    ScopedOptions.pixelSize = 24.0f;
    ScopedOptions.codepoints = std::span<const std::uint32_t>(ScopedCodepoints);

    Ui::FontAtlasSet ScopedFonts;
    Check(ScopedFonts.LoadTtfFromVfs(ScopedVfs, "fonts/Inter-Regular.ttf", ScopedOptions, &ScopedError),
          "TTF loads through the in-engine builder");
    Check(ScopedFonts.Count() == 1, "TTF produces one atlas");
    Check(ScopedFonts.At(0) != nullptr && ScopedFonts.At(0)->Glyph('A') != nullptr, "TTF atlas has the requested glyph");

    ScopedOptions.style = Ui::FontStyle::Bold;
    Check(ScopedFonts.LoadTtfFromVfs(ScopedVfs, "fonts/Inter-Regular.ttf", ScopedOptions, &ScopedError),
          "TTF bold slot builds");
    Check(ScopedFonts.FindByFontIdAndStyle(0, Ui::FontStyle::Bold) != nullptr, "bold slot is present");
    Check(ScopedFonts.LayerForFontIdAndStyle(0, Ui::FontStyle::Bold) == 2, "bold slot layer");

    const std::array<Ui::FontFamilyFile, 2> ScopedFamily{{
        {Ui::FontStyle::Regular, "fonts/Inter-Regular.ttf"},
        {Ui::FontStyle::Bold, "fonts/Inter-Regular.ttf"},
    }};
    Ui::FontAtlasSet ScopedFamilySet;
    Ui::FontBuildOptions ScopedFamilyOptions;
    ScopedFamilyOptions.pixelSize = 24.0f;
    ScopedFamilyOptions.codepoints = std::span<const std::uint32_t>(ScopedCodepoints);
    Check(ScopedFamilySet.LoadFamilyFromVfs(ScopedVfs, std::span<const Ui::FontFamilyFile>(ScopedFamily),
                                            ScopedFamilyOptions, &ScopedError),
          "font family loads all listed slots");
    Check(ScopedFamilySet.Count() == 2 && ScopedFamilySet.FindByFontIdAndStyle(0, Ui::FontStyle::Bold) != nullptr,
          "font family has regular and bold slots");

    const std::array<Ui::FontFamilyFile, 5> ScopedFullFamily{{
        {Ui::FontStyle::Regular, "fonts/Inter-Regular.ttf"},
        {Ui::FontStyle::Bold, "fonts/Inter-Regular.ttf"},
        {Ui::FontStyle::Italic, "fonts/Inter-Regular.ttf"},
        {Ui::FontStyle::BoldItalic, "fonts/Inter-Regular.ttf"},
        {Ui::FontStyle::Mono, "fonts/Inter-Regular.ttf"},
    }};
    Ui::FontAtlasSet ScopedFullFamilySet;
    Check(ScopedFullFamilySet.LoadFamilyFromVfs(ScopedVfs, std::span<const Ui::FontFamilyFile>(ScopedFullFamily),
                                                ScopedFamilyOptions, &ScopedError),
          "font family loads five style slots");
    Check(ScopedFullFamilySet.Count() == 5, "font family fits regular/bold/italic/bolditalic/mono");

    const std::filesystem::path ScopedManifestDir = std::filesystem::current_path() / "build" / "_font_family_test";
    std::error_code ScopedFilesystemError;
    std::filesystem::create_directories(ScopedManifestDir, ScopedFilesystemError);
    {
        std::ofstream ScopedManifestFile(ScopedManifestDir / "family.toml", std::ios::binary | std::ios::trunc);
        ScopedManifestFile << "regular = \"fonts/Inter-Regular.ttf\"\n"
                              "bold = \"fonts/Inter-Regular.ttf\"\n"
                              "font_size = 24\n"
                              "font_oversample = 2\n"
                              "atlas_size = 512\n";
    }
    VirtualFileSystem ScopedManifestVfs;
    Check(ScopedManifestVfs.MountDirectory("assets", 0, &ScopedError), "manifest font assets mount");
    Check(ScopedManifestVfs.MountDirectory(ScopedManifestDir.string(), 2, &ScopedError),
          "manifest directory mounts");
    Ui::FontAtlasSet ScopedManifestSet;
    Check(ScopedManifestSet.LoadFamilyManifestFromVfs(ScopedManifestVfs, "family.toml", ScopedFamilyOptions,
                                                      &ScopedError),
          "font family manifest loads");
    Check(ScopedManifestSet.Count() == 2 && ScopedManifestSet.FindByFontIdAndStyle(0, Ui::FontStyle::Bold) != nullptr,
          "manifest family has regular and bold");
    Check(ScopedManifestSet.At(0) != nullptr && ScopedManifestSet.At(0)->AtlasWidth() == 512,
          "manifest pins one atlas layer size");
    const Ui::FontAtlas* ScopedGlyphIdAtlas = ScopedManifestSet.At(0);
    const Ui::GlyphRecord* ScopedAGlyph = ScopedGlyphIdAtlas != nullptr ? ScopedGlyphIdAtlas->Glyph('A') : nullptr;
    Check(ScopedAGlyph != nullptr && ScopedAGlyph->glyphId != 0 &&
              ScopedGlyphIdAtlas->GlyphById(ScopedAGlyph->glyphId) == ScopedAGlyph,
          "glyph-id atlas resolves the shaped glyph id");

    Ui::LazyFontFamily ScopedLazyFamily;
    Check(ScopedLazyFamily.Open(ScopedManifestVfs, "family.toml", ScopedFamilyOptions, &ScopedError),
          "lazy family opens with Regular");
    Check(ScopedLazyFamily.LoadedCount() == 1 && ScopedLazyFamily.Atlases().Count() == 1,
          "lazy family loads only Regular initially");
    Check(ScopedLazyFamily.EnsureStyle(Ui::FontStyle::Bold, &ScopedError), "lazy family loads Bold on demand");
    Check(ScopedLazyFamily.LoadedCount() == 2 && ScopedLazyFamily.Atlases().Count() == 2,
          "lazy family adds the requested style slot");
    Check(!ScopedLazyFamily.EnsureStyle(Ui::FontStyle::Mono, &ScopedError),
          "lazy family reports a missing slot");
    std::filesystem::remove_all(ScopedManifestDir, ScopedFilesystemError);

    ScopedOptions.style = Ui::FontStyle::Italic;
    ScopedOptions.sdf = true;
    ScopedOptions.oversample = 1;
    Ui::FontAtlasSet ScopedSdfFonts;
    Check(ScopedSdfFonts.LoadTtfFromVfs(ScopedVfs, "fonts/Inter-Regular.ttf", ScopedOptions, &ScopedError),
          "SDF TTF slot builds");
    const Ui::FontAtlas* ScopedItalic = ScopedSdfFonts.FindByFontIdAndStyle(0, Ui::FontStyle::Italic);
    Check(ScopedItalic != nullptr && ScopedItalic->Format() == Ui::FontPixelFormat::SdfR8,
          "SDF slot has the SDF pixel format");

    Ui::FontBuildOptions ScopedAllGlyphOptions;
    ScopedAllGlyphOptions.pixelSize = 16.0f;
    ScopedAllGlyphOptions.atlasSize = 2048;
    ScopedAllGlyphOptions.allGlyphs = true;
    ScopedAllGlyphOptions.codepoints = {};
    Ui::FontAtlasSet ScopedAllGlyphFonts;
    Check(ScopedAllGlyphFonts.LoadTtfFromVfs(ScopedVfs, "fonts/tests/NotoSansArabic-Regular.ttf",
                                             ScopedAllGlyphOptions, &ScopedError),
          "all-glyph SDF atlas builds for Arabic");
    const Ui::FontAtlas* ScopedAllGlyphAtlas = ScopedAllGlyphFonts.At(0);
    Check(ScopedAllGlyphAtlas != nullptr && ScopedAllGlyphAtlas->GlyphCount() > 100,
          "Arabic all-glyph atlas contains contextual glyph slots");

    // A full CJK face is paged: one 2048 page is a quarter of the old 8192
    // atlas and the catalogue grows on demand.
    Ui::FontBuildOptions ScopedCjkOptions;
    ScopedCjkOptions.fontId = 4;
    ScopedCjkOptions.pixelSize = 16.0f;
    ScopedCjkOptions.atlasSize = 2048;
    ScopedCjkOptions.allGlyphs = true;
    ScopedCjkOptions.codepoints = {};
    Ui::FontAtlasSet ScopedCjkFonts;
    Check(ScopedCjkFonts.LoadPagedTtfFromVfs(ScopedVfs, "fonts/tests/NotoSansSC-Regular.ttf", ScopedCjkOptions,
                                             &ScopedError),
          "paged CJK SDF atlas builds");
    Check(ScopedCjkFonts.PageCount(4) == 1 && ScopedCjkFonts.At(0) != nullptr &&
              ScopedCjkFonts.At(0)->GlyphCount() > 64,
          "the CJK page carries the first glyph catalogue entries");
    Check(ScopedCjkFonts.AtlasPixelBytes() == 2048u * 2048u,
          "one CJK page costs one 2048 buffer instead of the 8192 full-face atlas");
    const std::uint32_t ScopedCjkPageGlyphs = ScopedCjkFonts.PageGlyphs(4);
    Check(ScopedCjkPageGlyphs != 0, "the CJK face reports its page budget");
    Check(ScopedCjkFonts.EnsurePageForGlyph(4, Ui::FontStyle::Regular, ScopedCjkPageGlyphs * 3u) != nullptr,
          "a later CJK page builds on demand");
    std::array<std::uint32_t, 4096> ScopedCjkCatalogue{};
    const std::uint32_t ScopedCjkCatalogueSize =
        ScopedCjkFonts.CatalogueGlyphs(4, std::span<std::uint32_t>(ScopedCjkCatalogue));
    Check(ScopedCjkCatalogueSize > ScopedCjkPageGlyphs,
          "the CJK glyph catalogue spans more than one page");

    if (GFailures != 0)
    {
        std::fprintf(stderr, "%d font builder test(s) failed\n", GFailures);
        return 1;
    }
    std::printf("font builder tests: ok\n");
    return 0;
}
