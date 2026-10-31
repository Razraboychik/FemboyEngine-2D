#include "Engine/UI/FontAtlas.hpp"

#include "Engine/Assets/VFS.hpp"
#include "Engine/Core/Log.hpp"
#include "Engine/UI/FontBuilder.hpp"
#include "Engine/UI/FontFamilyAsset.hpp"

#include <cstdio>
#include <cstring>
#include <vector>

namespace Fby::Ui
{

bool FontAtlas::Open(std::span<const std::byte> InBytes) noexcept
{
    if (InBytes.size() < sizeof(FontAtlasHeader))
    {
        return false;
    }
    FontAtlasHeader ScopedHeader{};
    std::memcpy(&ScopedHeader, InBytes.data(), sizeof(ScopedHeader));
    if (std::memcmp(ScopedHeader.magic, kFontAtlasMagic, sizeof(ScopedHeader.magic)) != 0 ||
        ScopedHeader.version != kFontAtlasVersion || ScopedHeader.headerBytes != sizeof(FontAtlasHeader) ||
        ScopedHeader.glyphCount == 0 || ScopedHeader.atlasWidth == 0 || ScopedHeader.atlasHeight == 0)
    {
        return false;
    }
    // A .fbyfont comes from the VFS and may be authored by a mod. Bound the
    constexpr std::uint32_t kMaxAtlasEdge = 8192;
    if (ScopedHeader.atlasWidth > kMaxAtlasEdge || ScopedHeader.atlasHeight > kMaxAtlasEdge)
    {
        return false;
    }
    if (ScopedHeader.pixelFormat != static_cast<std::uint32_t>(FontPixelFormat::Rgba8) &&
        ScopedHeader.pixelFormat != static_cast<std::uint32_t>(FontPixelFormat::R8) &&
        ScopedHeader.pixelFormat != static_cast<std::uint32_t>(FontPixelFormat::SdfR8))
    {
        // The mount path treats every non-RGBA8 atlas as R8; an unknown value
        return false;
    }
    const std::uint64_t ScopedGlyphBytes = static_cast<std::uint64_t>(ScopedHeader.glyphCount) * sizeof(GlyphRecord);
    if (ScopedHeader.glyphTableOffset + ScopedGlyphBytes > InBytes.size() || ScopedHeader.pixelOffset > InBytes.size())
    {
        return false;
    }
    if (ScopedHeader.pixelOffset < ScopedHeader.glyphTableOffset + ScopedGlyphBytes)
    {
        return false;
    }
    const std::uint64_t ScopedPixelBytes =
        static_cast<std::uint64_t>(ScopedHeader.atlasWidth) * ScopedHeader.atlasHeight *
        (ScopedHeader.pixelFormat == static_cast<std::uint32_t>(FontPixelFormat::Rgba8) ? 4u : 1u);
    if (ScopedHeader.pixelOffset + ScopedPixelBytes > InBytes.size())
    {
        return false;
    }

    MHeader = ScopedHeader;
    MBytes = InBytes;
    MGlyphs = std::span<const GlyphRecord>(
        reinterpret_cast<const GlyphRecord*>(InBytes.data() + ScopedHeader.glyphTableOffset), ScopedHeader.glyphCount);
    MPixels = InBytes.subspan(ScopedHeader.pixelOffset, static_cast<std::size_t>(ScopedPixelBytes));
    return true;
}

const GlyphRecord* FontAtlas::GlyphById(std::uint32_t InGlyphId) const noexcept
{
    // Whitespace has a zero-size record and must still resolve: the caller
    // advances the pen for it instead of reporting a missing glyph.
    for (const GlyphRecord& ScopedGlyph : MGlyphs)
    {
        if (ScopedGlyph.glyphId == InGlyphId)
        {
            return &ScopedGlyph;
        }
    }
    return nullptr;
}

const GlyphRecord* FontAtlas::Glyph(std::uint32_t InCodepoint) const noexcept
{
    if (MGlyphs.empty())
    {
        return nullptr;
    }
    // Cooker emits ascending codepoints; binary search keeps a label with 100
    const GlyphRecord* ScopedBegin = MGlyphs.data();
    const GlyphRecord* ScopedEnd = ScopedBegin + MGlyphs.size();
    const GlyphRecord* ScopedFound =
        std::lower_bound(ScopedBegin, ScopedEnd, InCodepoint,
                         [](const GlyphRecord& InGlyph, std::uint32_t InValue) { return InGlyph.codepoint < InValue; });
    if (ScopedFound == ScopedEnd || ScopedFound->codepoint != InCodepoint)
    {
        // No legacy 8x8/fallback record: an unsupported codepoint is an honest
        return nullptr;
    }
    return ScopedFound;
}

bool FontAtlasSet::LoadFromVfs(VirtualFileSystem& InVfs, std::string_view InPath)
{
    const auto ScopedBytes = InVfs.Read(InPath);
    if (!ScopedBytes)
    {
        return false;
    }
    return Add(*ScopedBytes);
}

bool FontAtlasSet::LoadTtfFromVfs(VirtualFileSystem& InVfs, std::string_view InPath,
                                  const FontBuildOptions& InOptions, std::string* InError)
{
    const auto ScopedTtf = InVfs.Read(InPath);
    if (!ScopedTtf)
    {
        if (InError != nullptr)
        {
            *InError = "TTF is not present in the VFS";
        }
        return false;
    }
    std::vector<std::byte> ScopedCooked;
    if (!BuildFontAtlasFromTtf(*ScopedTtf, InOptions, &ScopedCooked, InError))
    {
        return false;
    }
    return Add(ScopedCooked);
}

bool FontAtlasSet::LoadPagedTtfFromVfs(VirtualFileSystem& InVfs, std::string_view InPath,
                                       const FontBuildOptions& InOptions, std::string* InError)
{
    const auto ScopedTtf = InVfs.Read(InPath);
    if (!ScopedTtf)
    {
        if (InError != nullptr)
        {
            *InError = "TTF is not present in the VFS";
        }
        return false;
    }
    if (MRetainedCount >= MRetained.size())
    {
        if (InError != nullptr)
        {
            *InError = "too many resident fonts for the page table";
        }
        return false;
    }
    RetainedFont& ScopedFont = MRetained[MRetainedCount];
    ScopedFont = RetainedFont{};
    ScopedFont.fontId = InOptions.fontId;
    ScopedFont.style = InOptions.style;
    ScopedFont.bytes.assign(ScopedTtf->begin(), ScopedTtf->end());
    ScopedFont.pixelSize = InOptions.pixelSize;
    ScopedFont.atlasSize = InOptions.atlasSize;
    ScopedFont.oversample = InOptions.oversample;
    ScopedFont.sdf = InOptions.sdf;
    const std::uint32_t ScopedEdge = InOptions.atlasSize != 0 ? InOptions.atlasSize : 2048u;
    // The packer leaves row waste and one pixel between glyphs, so the budget
    // keeps a quarter of the edge as headroom; a page that does not fit would
    // fall back to the next edge and defeat the memory bound.
    const std::uint32_t ScopedCell = static_cast<std::uint32_t>(InOptions.pixelSize * 2.0f) + 16u;
    const std::uint32_t ScopedCells = std::max((ScopedEdge * ScopedEdge) / std::max(ScopedCell * ScopedCell, 1u), 1u);
    ScopedFont.pageGlyphs = std::clamp((ScopedCells * 3u) / 4u, 64u, kGlyphsPerPage);
    (void)BuildFontCoverageFromTtf(ScopedFont.bytes, &ScopedFont.coverage);
    ++MRetainedCount;
    return BuildPage(ScopedFont, 0) != nullptr;
}

const FontAtlas* FontAtlasSet::FindPage(std::uint32_t InFontId, FontStyle InStyle,
                                        std::uint32_t InPageIndex) const noexcept
{
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < MCount; ++ScopedIndex)
    {
        if (MAtlases[ScopedIndex].FontId() == InFontId && MAtlases[ScopedIndex].Style() == InStyle &&
            MPageIndex[ScopedIndex] == InPageIndex && MAtlases[ScopedIndex].IsOpen())
        {
            MPageLastUse[ScopedIndex] = MPageClock;
            return &MAtlases[ScopedIndex];
        }
    }
    return nullptr;
}

std::uint32_t FontAtlasSet::AcquireSlot() noexcept
{
    if (MCount < MAtlases.size())
    {
        const std::uint32_t ScopedSlot = MCount++;
        MPageLastUse[ScopedSlot] = MPageClock;
        return ScopedSlot;
    }
    // Every slot is live: replace the least recently used page.
    std::uint32_t ScopedVictim = 0xFFFFFFFFu;
    std::uint64_t ScopedOldest = 0xFFFFFFFFFFFFFFFFull;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < MCount; ++ScopedIndex)
    {
        if (MPageLastUse[ScopedIndex] < ScopedOldest)
        {
            ScopedOldest = MPageLastUse[ScopedIndex];
            ScopedVictim = ScopedIndex;
        }
    }
    if (ScopedVictim == 0xFFFFFFFFu)
    {
        return 0xFFFFFFFFu;
    }
    MAtlases[ScopedVictim] = FontAtlas{};
    MPageIndex[ScopedVictim] = 0;
    MPageLastUse[ScopedVictim] = MPageClock;
    ++MPageEvictions;
    ++MGeneration;
    return ScopedVictim;
}

const FontAtlas* FontAtlasSet::BuildPage(const RetainedFont& InFont, std::uint32_t InPageIndex) noexcept
{
    FontBuildOptions ScopedOptions;
    ScopedOptions.fontId = InFont.fontId;
    ScopedOptions.style = InFont.style;
    ScopedOptions.pixelSize = InFont.pixelSize;
    ScopedOptions.atlasSize = InFont.atlasSize;
    ScopedOptions.oversample = InFont.oversample;
    ScopedOptions.sdf = InFont.sdf;
    ScopedOptions.allGlyphs = true;
    ScopedOptions.glyphPageSize = InFont.pageGlyphs;
    ScopedOptions.glyphPageIndex = InPageIndex;
    std::vector<std::byte> ScopedCooked;
    std::string ScopedError;
    if (!BuildFontAtlasFromTtf(InFont.bytes, ScopedOptions, &ScopedCooked, &ScopedError))
    {
        if (InPageIndex == 0)
        {
            LogError("fonts", "font page 0 of font {} failed: {}", InFont.fontId, ScopedError);
        }
        return nullptr;
    }
    const std::uint32_t ScopedSlot = AcquireSlot();
    if (ScopedSlot == 0xFFFFFFFFu)
    {
        return nullptr;
    }
    if (!MAtlases[ScopedSlot].OpenOwned(ScopedCooked))
    {
        return nullptr;
    }
    MPageIndex[ScopedSlot] = InPageIndex;
    MPageLastUse[ScopedSlot] = MPageClock++;
    ++MPageLoads;
    ++MGeneration;
    (void)PrepareMountScratch();
    return &MAtlases[ScopedSlot];
}

const FontAtlas* FontAtlasSet::EnsurePageForGlyph(std::uint32_t InFontId, FontStyle InStyle,
                                                 std::uint32_t InGlyphId) noexcept
{
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < MRetainedCount; ++ScopedIndex)
    {
        const RetainedFont& ScopedFont = MRetained[ScopedIndex];
        if (ScopedFont.fontId != InFontId || ScopedFont.style != InStyle)
        {
            continue;
        }
        const std::uint32_t ScopedPage = InGlyphId / std::max(ScopedFont.pageGlyphs, 1u);
        if (const FontAtlas* ScopedResident = FindPage(InFontId, InStyle, ScopedPage); ScopedResident != nullptr)
        {
            return ScopedResident;
        }
        return BuildPage(ScopedFont, ScopedPage);
    }
    return nullptr;
}

bool FontAtlasSet::HasGlyph(std::uint32_t InFontId, FontStyle InStyle, std::uint32_t InGlyphId) const noexcept
{
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < MCount; ++ScopedIndex)
    {
        if (MAtlases[ScopedIndex].FontId() == InFontId && MAtlases[ScopedIndex].Style() == InStyle &&
            MAtlases[ScopedIndex].GlyphById(InGlyphId) != nullptr)
        {
            return true;
        }
    }
    return false;
}

std::uint32_t FontAtlasSet::PageCount(std::uint32_t InFontId) const noexcept
{
    std::uint32_t ScopedPages = 0;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < MCount; ++ScopedIndex)
    {
        if (MAtlases[ScopedIndex].IsOpen() && MAtlases[ScopedIndex].FontId() == InFontId)
        {
            ++ScopedPages;
        }
    }
    return ScopedPages;
}

std::uint32_t FontAtlasSet::PageGlyphs(std::uint32_t InFontId) const noexcept
{
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < MRetainedCount; ++ScopedIndex)
    {
        if (MRetained[ScopedIndex].fontId == InFontId)
        {
            return MRetained[ScopedIndex].pageGlyphs;
        }
    }
    return 0;
}

const FontCoverage* FontAtlasSet::CoverageFor(std::uint32_t InFontId) const noexcept
{
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < MRetainedCount; ++ScopedIndex)
    {
        if (MRetained[ScopedIndex].fontId == InFontId)
        {
            return &MRetained[ScopedIndex].coverage;
        }
    }
    return nullptr;
}

std::uint32_t FontAtlasSet::CatalogueGlyphs(std::uint32_t InFontId, std::span<std::uint32_t> OutGlyphs) const noexcept
{
    std::uint32_t ScopedWritten = 0;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < MCount; ++ScopedIndex)
    {
        const FontAtlas& ScopedAtlas = MAtlases[ScopedIndex];
        if (!ScopedAtlas.IsOpen() || ScopedAtlas.FontId() != InFontId)
        {
            continue;
        }
        for (std::uint32_t ScopedGlyph = 0; ScopedGlyph < ScopedAtlas.GlyphCount(); ++ScopedGlyph)
        {
            const GlyphRecord& ScopedRecord = ScopedAtlas.GlyphAt(ScopedGlyph);
            if (ScopedWritten >= OutGlyphs.size())
            {
                return ScopedWritten;
            }
            OutGlyphs[ScopedWritten++] = ScopedRecord.glyphId;
        }
    }
    return ScopedWritten;
}

bool FontAtlas::OpenOwned(std::span<const std::byte> InBytes) noexcept
{
    MOwnedBytes.assign(InBytes.begin(), InBytes.end());
    return Open(std::span<const std::byte>(MOwnedBytes.data(), MOwnedBytes.size()));
}

bool FontAtlasSet::LoadFamilyFromVfs(VirtualFileSystem& InVfs, std::span<const FontFamilyFile> InFiles,
                                     const FontBuildOptions& InOptions, std::string* InError)
{
    if (InFiles.empty())
    {
        if (InError != nullptr)
        {
            *InError = "font family has no files";
        }
        return false;
    }
    bool ScopedAnyLoaded = false;
    std::string ScopedLastError;
    for (const FontFamilyFile& ScopedFile : InFiles)
    {
        FontBuildOptions ScopedOptions = InOptions;
        ScopedOptions.style = ScopedFile.style;
        std::string ScopedError;
        if (LoadTtfFromVfs(InVfs, ScopedFile.path, ScopedOptions, &ScopedError))
        {
            ScopedAnyLoaded = true;
        }
        else
        {
            ScopedLastError = ScopedError;
        }
    }
    if (!ScopedAnyLoaded && InError != nullptr)
    {
        *InError = ScopedLastError.empty() ? "no font family slot loaded" : ScopedLastError;
    }
    return ScopedAnyLoaded;
}

bool FontAtlasSet::LoadFamilyManifestFromVfs(VirtualFileSystem& InVfs, std::string_view InPath,
                                             const FontBuildOptions& InDefaults, std::string* InError)
{
    const auto ScopedManifestBytes = InVfs.Read(InPath);
    if (!ScopedManifestBytes)
    {
        if (InError != nullptr)
        {
            *InError = "font family manifest is not present in the VFS";
        }
        return false;
    }
    FontFamilyManifest ScopedManifest;
    ScopedManifest.options = InDefaults;
    // One family needs one layer size: all style pages must be texture-array
    // compatible. 1024 is enough for the default Latin/Cyrillic UI set; larger
    // families can pin 2048/4096 in the manifest.
    if (ScopedManifest.options.atlasSize == 0)
    {
        ScopedManifest.options.atlasSize = 1024;
    }
    if (!ParseFontFamilyManifest(
            std::string_view(reinterpret_cast<const char*>(ScopedManifestBytes->data()), ScopedManifestBytes->size()),
            &ScopedManifest, InError))
    {
        return false;
    }
    std::array<FontFamilyFile, 5> ScopedFiles{};
    std::uint32_t ScopedCount = 0;
    const auto ScopedAppend = [&ScopedFiles, &ScopedCount](FontStyle InStyle, const std::string& InPath)
    {
        if (!InPath.empty())
        {
            ScopedFiles[ScopedCount++] = FontFamilyFile{InStyle, InPath};
        }
    };
    ScopedAppend(FontStyle::Regular, ScopedManifest.regular);
    ScopedAppend(FontStyle::Bold, ScopedManifest.bold);
    ScopedAppend(FontStyle::Italic, ScopedManifest.italic);
    ScopedAppend(FontStyle::BoldItalic, ScopedManifest.boldItalic);
    ScopedAppend(FontStyle::Mono, ScopedManifest.mono);
    return LoadFamilyFromVfs(InVfs, std::span<const FontFamilyFile>(ScopedFiles.data(), ScopedCount),
                             ScopedManifest.options, InError);
}

bool FontAtlasSet::Add(std::span<const std::byte> InBytes)
{
    if (MCount >= MAtlases.size())
    {
        LogError("fonts", "FontAtlasSet::Add: capacity is {} atlases; '{}' was rejected", MAtlases.size(), MCount);
        return false;
    }
    // Every atlas in a set must share one pixel size: GlyphRecord UVs are
    if (MCount > 0)
    {
        const FontAtlas& ScopedFirst = MAtlases[0];
        if (InBytes.size() < sizeof(FontAtlasHeader))
        {
            LogError("fonts", "FontAtlasSet::Add: atlas #{} is {} bytes, smaller than the header", MCount,
                     InBytes.size());
            return false;
        }
        FontAtlasHeader ScopedHeader{};
        std::memcpy(&ScopedHeader, InBytes.data(), sizeof(ScopedHeader));
        if (ScopedHeader.atlasWidth != ScopedFirst.AtlasWidth() ||
            ScopedHeader.atlasHeight != ScopedFirst.AtlasHeight())
        {
            LogError("fonts",
                     "FontAtlasSet::Add: atlas #{} is {}x{} but #0 is {}x{}; re-cook the set with the same --size",
                     MCount, ScopedHeader.atlasWidth, ScopedHeader.atlasHeight, ScopedFirst.AtlasWidth(),
                     ScopedFirst.AtlasHeight());
            return false;
        }
    }
    if (!MAtlases[MCount].OpenOwned(InBytes))
    {
        LogError("fonts", "FontAtlasSet::Add: atlas #{} has an invalid .fbyfont header", MCount);
        return false;
    }
    ++MCount;
    // Adding an atlas invalidates the GPU array: the next MountFonts() must
    ++MGeneration;
    // Grow the mount buffer here (asset load), never inside BuildFrame.
    (void)PrepareMountScratch();
    return true;
}

bool FontAtlasSet::PrepareMountScratch()
{
    if (MCount == 0)
    {
        MMountScratch.clear();
        return false;
    }
    const std::uint32_t ScopedWidth = MAtlases[0].AtlasWidth();
    const std::uint32_t ScopedHeight = MAtlases[0].AtlasHeight();
    if (ScopedWidth == 0 || ScopedHeight == 0)
    {
        MMountScratch.clear();
        return false;
    }
    for (std::uint32_t ScopedIndex = 1; ScopedIndex < MCount; ++ScopedIndex)
    {
        if (MAtlases[ScopedIndex].AtlasWidth() != ScopedWidth ||
            MAtlases[ScopedIndex].AtlasHeight() != ScopedHeight)
        {
            MMountScratch.clear();
            return false;
        }
    }
    const std::size_t ScopedBytes = static_cast<std::size_t>(ScopedWidth) * ScopedHeight * 4u * (MCount + 1u);
    if (MMountScratch.size() != ScopedBytes)
    {
        MMountScratch.resize(ScopedBytes);
    }
    return true;
}

void FontAtlasSet::Clear() noexcept
{
    for (FontAtlas& ScopedAtlas : MAtlases)
    {
        ScopedAtlas = FontAtlas{};
    }
    for (RetainedFont& ScopedFont : MRetained)
    {
        ScopedFont = RetainedFont{};
    }
    MPageIndex = {};
    MPageLastUse = {};
    MCount = 0;
    MFallbackCount = 0;
    MRetainedCount = 0;
    MPageLoads = 0;
    MPageEvictions = 0;
    MPageClock = 1;
    ++MGeneration;
}

const FontAtlas* FontAtlasSet::FindByFontId(std::uint32_t InFontId) const noexcept
{
    for (std::uint32_t ScopedI = 0; ScopedI < MCount; ++ScopedI)
    {
        if (MAtlases[ScopedI].FontId() == InFontId)
        {
            return &MAtlases[ScopedI];
        }
    }
    return nullptr;
}

const FontAtlas* FontAtlasSet::FindByFontIdAndStyle(std::uint32_t InFontId, FontStyle InStyle) const noexcept
{
    for (std::uint32_t ScopedI = 0; ScopedI < MCount; ++ScopedI)
    {
        if (MAtlases[ScopedI].FontId() == InFontId && MAtlases[ScopedI].Style() == InStyle)
        {
            return &MAtlases[ScopedI];
        }
    }
    return nullptr;
}

std::uint32_t FontAtlasSet::LayerForFontIdAndStyle(std::uint32_t InFontId, FontStyle InStyle) const noexcept
{
    for (std::uint32_t ScopedI = 0; ScopedI < MCount; ++ScopedI)
    {
        if (MAtlases[ScopedI].FontId() == InFontId && MAtlases[ScopedI].Style() == InStyle)
        {
            return ScopedI + 1u;
        }
    }
    return 0xFFFFFFFFu;
}

bool FontAtlasSet::AddFallbackFontId(std::uint32_t InFontId) noexcept
{
    if (MFallbackCount >= MFallbackFontIds.size())
    {
        return false;
    }
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < MFallbackCount; ++ScopedIndex)
    {
        if (MFallbackFontIds[ScopedIndex] == InFontId)
        {
            return true;
        }
    }
    MFallbackFontIds[MFallbackCount++] = InFontId;
    return true;
}

void FontAtlasSet::ClearFallbackFontIds() noexcept
{
    MFallbackCount = 0;
}

const GlyphRecord* FontAtlasSet::ResolveGlyph(std::uint32_t InFontId, FontStyle InStyle,
                                              std::uint32_t InCodepoint, const FontAtlas** OutAtlas) const noexcept
{
    const auto ScopedResolve = [this, InCodepoint, OutAtlas](std::uint32_t InId, FontStyle InWantedStyle)
        -> const GlyphRecord*
    {
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < MCount; ++ScopedIndex)
        {
            if (MAtlases[ScopedIndex].FontId() != InId || MAtlases[ScopedIndex].Style() != InWantedStyle)
            {
                continue;
            }
            const GlyphRecord* ScopedGlyph = MAtlases[ScopedIndex].Glyph(InCodepoint);
            if (ScopedGlyph == nullptr || ScopedGlyph->width <= 0.0f || ScopedGlyph->height <= 0.0f)
            {
                continue;
            }
            if (OutAtlas != nullptr)
            {
                *OutAtlas = &MAtlases[ScopedIndex];
            }
            return ScopedGlyph;
        }
        return nullptr;
    };

    if (const GlyphRecord* ScopedGlyph = ScopedResolve(InFontId, InStyle); ScopedGlyph != nullptr)
    {
        return ScopedGlyph;
    }
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < MFallbackCount; ++ScopedIndex)
    {
        if (const GlyphRecord* ScopedGlyph = ScopedResolve(MFallbackFontIds[ScopedIndex], InStyle);
            ScopedGlyph != nullptr)
        {
            return ScopedGlyph;
        }
    }
    return nullptr;
}

const GlyphRecord* FontAtlasSet::ResolveGlyphById(std::uint32_t InFontId, FontStyle InStyle,
                                                  std::uint32_t InGlyphId, const FontAtlas** OutAtlas) const noexcept
{
    // A face may be split over several pages, so every page of the requested
    // face is searched, then every page of the Regular style, then the
    // fallback chain.
    const auto ScopedResolve = [this, InGlyphId, OutAtlas](std::uint32_t InId, FontStyle InWantedStyle)
        -> const GlyphRecord*
    {
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < MCount; ++ScopedIndex)
        {
            if (MAtlases[ScopedIndex].FontId() != InId || MAtlases[ScopedIndex].Style() != InWantedStyle)
            {
                continue;
            }
            const GlyphRecord* ScopedGlyph = MAtlases[ScopedIndex].GlyphById(InGlyphId);
            if (ScopedGlyph != nullptr)
            {
                if (OutAtlas != nullptr)
                {
                    *OutAtlas = &MAtlases[ScopedIndex];
                }
                return ScopedGlyph;
            }
        }
        return nullptr;
    };
    if (const GlyphRecord* ScopedGlyph = ScopedResolve(InFontId, InStyle); ScopedGlyph != nullptr)
    {
        return ScopedGlyph;
    }
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < MFallbackCount; ++ScopedIndex)
    {
        if (const GlyphRecord* ScopedGlyph = ScopedResolve(MFallbackFontIds[ScopedIndex], InStyle);
            ScopedGlyph != nullptr)
        {
            return ScopedGlyph;
        }
    }
    return nullptr;
}

std::uint32_t FontAtlasSet::LayerForAtlas(const FontAtlas* InAtlas) const noexcept
{
    if (InAtlas == nullptr)
    {
        return 0xFFFFFFFFu;
    }
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < MCount; ++ScopedIndex)
    {
        if (&MAtlases[ScopedIndex] == InAtlas)
        {
            return ScopedIndex + 1u;
        }
    }
    return 0xFFFFFFFFu;
}

std::uint32_t FontAtlasSet::LayerForFontId(std::uint32_t InFontId) const noexcept
{
    // Layer 0 is the white texel; fonts start at 1.
    for (std::uint32_t ScopedI = 0; ScopedI < MCount; ++ScopedI)
    {
        if (MAtlases[ScopedI].FontId() == InFontId)
        {
            return ScopedI + 1u;
        }
    }
    return 0xFFFFFFFFu;
}

bool FontAtlasSet::MountToTextureArray(Rhi::RHI& InRhi)
{
    if (MCount == 0)
    {
        LogError("fonts", "MountToTextureArray called on an empty atlas set");
        return false;
    }
    const FontAtlas& ScopedFirst = MAtlases[0];
    if (!ScopedFirst.IsOpen())
    {
        LogError("fonts", "MountToTextureArray: first atlas is not open");
        return false;
    }
    const std::uint32_t ScopedWidth = ScopedFirst.AtlasWidth();
    const std::uint32_t ScopedHeight = ScopedFirst.AtlasHeight();
    if (ScopedWidth == 0 || ScopedHeight == 0 || ScopedWidth > 4096 || ScopedHeight > 4096)
    {
        LogError("fonts", "MountToTextureArray: invalid atlas extent {}x{}", ScopedWidth, ScopedHeight);
        return false;
    }

    // GlyphRecord UVs are normalized to the atlas the glyph came from. Packing
    for (std::uint32_t ScopedAtlasIndex = 1; ScopedAtlasIndex < MCount; ++ScopedAtlasIndex)
    {
        const FontAtlas& ScopedAtlas = MAtlases[ScopedAtlasIndex];
        if (!ScopedAtlas.IsOpen() || ScopedAtlas.AtlasWidth() != ScopedWidth ||
            ScopedAtlas.AtlasHeight() != ScopedHeight)
        {
            LogError("fonts",
                     "MountToTextureArray: atlas #{} is {}x{} but #0 is {}x{}; re-cook with the same --size",
                     ScopedAtlasIndex, ScopedAtlas.AtlasWidth(), ScopedAtlas.AtlasHeight(), ScopedWidth, ScopedHeight);
            return false;
        }
    }

    // Layer 0 is the white texel, so the array is always one layer larger than
    const std::uint32_t ScopedLayerCount = MCount + 1u;
    if (ScopedLayerCount > kMaxFontAtlases + 1u)
    {
        LogError("fonts", "MountToTextureArray: {} font layers requested, the engine supports at most {}",
                 MCount, kMaxFontAtlases);
        return false;
    }
    const std::uint32_t ScopedDeviceLayers = InRhi.Info().maxTextureLayers;
    if (ScopedDeviceLayers != 0 && ScopedLayerCount > ScopedDeviceLayers)
    {
        LogError("fonts",
                 "MountToTextureArray: {} layers (white + {} fonts) exceed the device limit {} on '{}'; "
                 "raise DeviceDesc::maxTextureLayers or ship fewer font atlases",
                 ScopedLayerCount, MCount, ScopedDeviceLayers,
                 InRhi.Info().name[0] != '\0' ? InRhi.Info().name : "unknown device");
        return false;
    }
    if (ScopedDeviceLayers == 0)
    {
        // A backend that reports no limit cannot validate the request; the
        LogWarn("fonts", "MountToTextureArray: the RHI reports maxTextureLayers == 0; using the engine bound of {}",
                kMaxFontAtlases + 1u);
    }
    const std::size_t ScopedLayerBytes = static_cast<std::size_t>(ScopedWidth) * ScopedHeight * 4u;
    const std::size_t ScopedTotalBytes = ScopedLayerBytes * ScopedLayerCount;
    if (MMountScratch.size() != ScopedTotalBytes)
    {
        LogError("fonts", "MountToTextureArray: mount scratch is {} bytes, expected {}; call Add() first",
                 MMountScratch.size(), ScopedTotalBytes);
        return false;
    }
    // Layer 0 is opaque white. Pink UI draws solid panels and images with layer
    std::fill(MMountScratch.begin(), MMountScratch.end(), std::byte{0xFF});
    for (std::uint32_t ScopedAtlasIndex = 0; ScopedAtlasIndex < MCount; ++ScopedAtlasIndex)
    {
        const FontAtlas& ScopedAtlas = MAtlases[ScopedAtlasIndex];
        const std::span<const std::byte> ScopedSource = ScopedAtlas.Pixels();
        std::byte* ScopedTarget =
            MMountScratch.data() + static_cast<std::size_t>(ScopedAtlasIndex + 1u) * ScopedLayerBytes;
        if (ScopedAtlas.Format() == FontPixelFormat::Rgba8)
        {
            if (ScopedSource.size() != ScopedLayerBytes)
            {
                LogError("fonts", "MountToTextureArray: atlas #{} has {} RGBA bytes, expected {}", ScopedAtlasIndex,
                         ScopedSource.size(), ScopedLayerBytes);
                return false;
            }
            std::memcpy(ScopedTarget, ScopedSource.data(), ScopedLayerBytes);
            continue;
        }
        if (ScopedSource.size() != ScopedLayerBytes / 4u)
        {
            LogError("fonts", "MountToTextureArray: atlas #{} has {} R8 bytes, expected {}", ScopedAtlasIndex,
                     ScopedSource.size(), ScopedLayerBytes / 4u);
            return false;
        }
        if (ScopedAtlas.Format() == FontPixelFormat::SdfR8)
        {
            // Keep the distance in R; the SDF text shader ignores RGB from the
            // atlas and uses the instance color instead.
            for (std::size_t ScopedTexel = 0; ScopedTexel < ScopedSource.size(); ++ScopedTexel)
            {
                ScopedTarget[ScopedTexel * 4 + 0] = ScopedSource[ScopedTexel];
                ScopedTarget[ScopedTexel * 4 + 1] = std::byte{0};
                ScopedTarget[ScopedTexel * 4 + 2] = std::byte{0};
                ScopedTarget[ScopedTexel * 4 + 3] = std::byte{0xFF};
            }
            continue;
        }
        // R8 coverage -> white RGB + alpha. Pink UI multiplies the sample by the
        for (std::size_t ScopedTexel = 0; ScopedTexel < ScopedSource.size(); ++ScopedTexel)
        {
            ScopedTarget[ScopedTexel * 4 + 0] = std::byte{0xFF};
            ScopedTarget[ScopedTexel * 4 + 1] = std::byte{0xFF};
            ScopedTarget[ScopedTexel * 4 + 2] = std::byte{0xFF};
            ScopedTarget[ScopedTexel * 4 + 3] = ScopedSource[ScopedTexel];
        }
    }

    Rhi::TextureArrayDesc ScopedDesc;
    ScopedDesc.size = Rhi::Extent2D{ScopedWidth, ScopedHeight};
    ScopedDesc.layers = ScopedLayerCount;
    ScopedDesc.mipLevels = 1;
    ScopedDesc.srgb = false;
    ScopedDesc.format = Assets::TextureFormat::Rgba8;

    // Atomic publish: the new array is fully populated before the old handle is
    const Rhi::TextureArrayHandle ScopedNew =
        InRhi.CreateTextureArray(ScopedDesc, std::span<const std::byte>(MMountScratch.data(), MMountScratch.size()));
    if (!ScopedNew.IsValid())
    {
        LogError("fonts", "MountToTextureArray: the RHI rejected a {}x{} x {} font array", ScopedWidth, ScopedHeight,
                 ScopedLayerCount);
        return false;
    }
    if (MTextureArray.IsValid())
    {
        InRhi.WaitIdle();
        InRhi.DestroyTextureArray(MTextureArray);
    }
    MTextureArray = ScopedNew;
    MMountedGeneration = MGeneration;
    LogInfo("fonts",
            "mounted white + {} font layer(s) as texture array #{} ({}x{}, {} layers, RGBA8, linear)", MCount,
            MTextureArray.id, ScopedWidth, ScopedHeight, ScopedLayerCount);
    return true;
}

void FontAtlasSet::UnmountFromTextureArray(Rhi::RHI& InRhi)
{
    if (!MTextureArray.IsValid())
    {
        MMountedGeneration = 0;
        return;
    }
    // Same barrier rule as MountToTextureArray(): the device must be idle before
    InRhi.WaitIdle();
    InRhi.DestroyTextureArray(MTextureArray);
    MTextureArray = Rhi::TextureArrayHandle{};
    MMountedGeneration = 0;
}

std::uint32_t DecodeUtf8(std::string_view InText, std::size_t& InCursor) noexcept
{
    if (InCursor >= InText.size())
    {
        return 0;
    }
    const auto ScopedFirst = static_cast<std::uint8_t>(InText[InCursor]);
    if (ScopedFirst < 0x80)
    {
        ++InCursor;
        return ScopedFirst;
    }
    std::uint32_t ScopedCodepoint = 0xFFFD;
    std::uint32_t ScopedLength = 0;
    if ((ScopedFirst & 0xE0) == 0xC0)
    {
        ScopedCodepoint = ScopedFirst & 0x1Fu;
        ScopedLength = 2;
    }
    else if ((ScopedFirst & 0xF0) == 0xE0)
    {
        ScopedCodepoint = ScopedFirst & 0x0Fu;
        ScopedLength = 3;
    }
    else if ((ScopedFirst & 0xF8) == 0xF0)
    {
        ScopedCodepoint = ScopedFirst & 0x07u;
        ScopedLength = 4;
    }
    else
    {
        ++InCursor;
        return 0xFFFD;
    }
    if (InCursor + ScopedLength > InText.size())
    {
        ++InCursor;
        return 0xFFFD;
    }
    for (std::uint32_t ScopedI = 1; ScopedI < ScopedLength; ++ScopedI)
    {
        const auto ScopedContinuation = static_cast<std::uint8_t>(InText[InCursor + ScopedI]);
        if ((ScopedContinuation & 0xC0) != 0x80)
        {
            ++InCursor;
            return 0xFFFD;
        }
        ScopedCodepoint = (ScopedCodepoint << 6u) | (ScopedContinuation & 0x3Fu);
    }
    InCursor += ScopedLength;
    return ScopedCodepoint;
}

std::uint32_t Utf8GlyphCount(std::string_view InText) noexcept
{
    std::uint32_t ScopedCount = 0;
    std::size_t ScopedCursor = 0;
    while (ScopedCursor < InText.size())
    {
        const std::uint32_t ScopedCodepoint = DecodeUtf8(InText, ScopedCursor);
        if (ScopedCodepoint != '\n')
        {
            ++ScopedCount;
        }
    }
    return ScopedCount;
}
}
