// FemboyEngine2D - VFS font atlas.
#pragma once

#include "Engine/Core/Types.hpp"
#include "Engine/RHI/RHI.hpp"
#include "Engine/UI/FontCoverage.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>
#include <vector>

namespace Fby
{
class VirtualFileSystem;
}

namespace Fby::Ui
{

struct FontBuildOptions;

inline constexpr char kFontAtlasMagic[8] = {'F', 'B', 'Y', 'F', 'N', 'T', '0', '1'};
inline constexpr std::uint32_t kFontAtlasVersion = 2;
inline constexpr std::uint32_t kMaxFontAtlases = 16;
// Glyph id span of one atlas page. Page N holds glyph ids
// [N * kGlyphsPerPage, (N + 1) * kGlyphsPerPage), which keeps the page index a
// pure function of the glyph id and bounds a page to a few megabytes.
inline constexpr std::uint32_t kGlyphsPerPage = 2048;
// Resident TTF buffers used to build pages on demand.
inline constexpr std::uint32_t kMaxRetainedFonts = 8;

enum class FontPixelFormat : std::uint32_t
{
    Rgba8 = 0,
    R8 = 1,
    // One-byte signed distance field. RGB is filled at mount time.
    SdfR8 = 2,
};

struct FontAtlasHeader
{
    char magic[8] = {};
    std::uint32_t version = kFontAtlasVersion;
    std::uint32_t headerBytes = sizeof(FontAtlasHeader);
    std::uint32_t glyphCount = 0;
    std::uint32_t atlasWidth = 0;
    std::uint32_t atlasHeight = 0;
    std::uint32_t pixelFormat = static_cast<std::uint32_t>(FontPixelFormat::Rgba8);
    std::uint32_t glyphTableOffset = sizeof(FontAtlasHeader);
    std::uint32_t pixelOffset = 0;
    float lineHeight = 0.0f;
    float ascent = 0.0f;
    float descent = 0.0f;
    std::uint32_t fontId = 0;
    std::uint32_t reserved = 0;
};

// Font slot cooked into a .fbyfont. The low byte of FontAtlasHeader::reserved
// stores this value; old atlases default to Regular (0).
enum class FontStyle : std::uint32_t
{
    Regular = 0,
    Bold,
    Italic,
    BoldItalic,
    Mono,
};

struct FontFamilyFile
{
    FontStyle style = FontStyle::Regular;
    std::string_view path{};
};

struct GlyphRecord
{
    std::uint32_t codepoint = 0;
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 0.0f;
    float v1 = 0.0f;
    // Pen movement, font pixels
    float advance = 0.0f;
    float bearingX = 0.0f;
    float bearingY = 0.0f;
    // Quad size, font pixels
    float width = 0.0f;
    float height = 0.0f;
    // OpenType glyph id used by the HarfBuzz shaping path.
    std::uint32_t glyphId = 0;
};

static_assert(std::is_trivially_copyable_v<FontAtlasHeader>);
static_assert(sizeof(GlyphRecord) == 44);

class FontAtlas
{
public:
    [[nodiscard]] bool Open(std::span<const std::byte> InBytes) noexcept;
    // Copies the cooked bytes into the atlas, so the source buffer may die.
    [[nodiscard]] bool OpenOwned(std::span<const std::byte> InBytes) noexcept;
    [[nodiscard]] bool IsOpen() const noexcept
    {
        return !MBytes.empty();
    }
    [[nodiscard]] std::uint32_t FontId() const noexcept
    {
        return MHeader.fontId;
    }
    [[nodiscard]] FontStyle Style() const noexcept
    {
        return static_cast<FontStyle>(MHeader.reserved & 0xFFu);
    }
    [[nodiscard]] float LineHeight() const noexcept
    {
        return MHeader.lineHeight;
    }
    [[nodiscard]] float Ascent() const noexcept
    {
        return MHeader.ascent;
    }
    [[nodiscard]] std::uint32_t AtlasWidth() const noexcept
    {
        return MHeader.atlasWidth;
    }
    [[nodiscard]] std::uint32_t AtlasHeight() const noexcept
    {
        return MHeader.atlasHeight;
    }
    [[nodiscard]] FontPixelFormat Format() const noexcept
    {
        return static_cast<FontPixelFormat>(MHeader.pixelFormat);
    }
    [[nodiscard]] std::span<const std::byte> Pixels() const noexcept
    {
        return MPixels;
    }
    [[nodiscard]] const GlyphRecord* Glyph(std::uint32_t InCodepoint) const noexcept;
    [[nodiscard]] const GlyphRecord* GlyphById(std::uint32_t InGlyphId) const noexcept;
    [[nodiscard]] const GlyphRecord& Fallback() const noexcept
    {
        return MGlyphs[0];
    }
    [[nodiscard]] std::uint32_t GlyphCount() const noexcept
    {
        return MHeader.glyphCount;
    }
    [[nodiscard]] const GlyphRecord& GlyphAt(std::uint32_t InIndex) const noexcept
    {
        return MGlyphs[InIndex < MGlyphs.size() ? InIndex : 0];
    }

private:
    FontAtlasHeader MHeader{};
    std::vector<std::byte> MOwnedBytes{};
    std::span<const std::byte> MBytes{};
    std::span<const GlyphRecord> MGlyphs{};
    std::span<const std::byte> MPixels{};
};

// Fixed registry: one Inter and one JetBrains Mono is the normal case, and a
class FontAtlasSet
{
public:
    [[nodiscard]] bool LoadFromVfs(VirtualFileSystem& InVfs, std::string_view InPath);
    [[nodiscard]] bool LoadTtfFromVfs(VirtualFileSystem& InVfs, std::string_view InPath,
                                      const FontBuildOptions& InOptions, std::string* InError = nullptr);
    // Builds every listed style with the same pixel size/AA policy. A missing
    // slot is reported but does not prevent the others from loading.
    [[nodiscard]] bool LoadFamilyFromVfs(VirtualFileSystem& InVfs, std::span<const FontFamilyFile> InFiles,
                                         const FontBuildOptions& InOptions, std::string* InError = nullptr);
    // One TOML file describes all style slots; see FontFamilyAsset.hpp.
    [[nodiscard]] bool LoadFamilyManifestFromVfs(VirtualFileSystem& InVfs, std::string_view InPath,
                                                 const FontBuildOptions& InDefaults,
                                                 std::string* InError = nullptr);
    [[nodiscard]] bool Add(std::span<const std::byte> InBytes);
    // Keeps the TTF bytes resident and builds page 0. Further pages are built
    // on demand by EnsurePageForGlyph; eviction drops the least recently used
    // page when the atlas array is full.
    [[nodiscard]] bool LoadPagedTtfFromVfs(VirtualFileSystem& InVfs, std::string_view InPath,
                                           const FontBuildOptions& InOptions, std::string* InError = nullptr);
    // Page that owns InGlyphId, built from the retained TTF when missing.
    [[nodiscard]] const FontAtlas* EnsurePageForGlyph(std::uint32_t InFontId, FontStyle InStyle,
                                                      std::uint32_t InGlyphId) noexcept;
    [[nodiscard]] bool HasGlyph(std::uint32_t InFontId, FontStyle InStyle, std::uint32_t InGlyphId) const noexcept;
    [[nodiscard]] std::uint32_t PageCount(std::uint32_t InFontId) const noexcept;
    // Glyph ids per page of a paged face, or 0 when the face is not paged.
    [[nodiscard]] std::uint32_t PageGlyphs(std::uint32_t InFontId) const noexcept;
    // Coverage mask of a resident face, or null when the face is not paged.
    [[nodiscard]] const FontCoverage* CoverageFor(std::uint32_t InFontId) const noexcept;
    [[nodiscard]] std::uint32_t PageLoads() const noexcept
    {
        return MPageLoads;
    }
    [[nodiscard]] std::uint32_t PageEvictions() const noexcept
    {
        return MPageEvictions;
    }
    // Copies the glyph ids of every resident page of a face, so a caller can
    // report coverage without walking the glyph tables.
    [[nodiscard]] std::uint32_t CatalogueGlyphs(std::uint32_t InFontId, std::span<std::uint32_t> OutGlyphs) const noexcept;
    void Clear() noexcept;

    // One-call GPU mount: takes the already-mapped atlases, expands them into the
    [[nodiscard]] bool MountToTextureArray(Rhi::RHI& InRhi);
    // Waits for the device before releasing the array: a frame in flight may
    void UnmountFromTextureArray(Rhi::RHI& InRhi);
    [[nodiscard]] bool Mounted() const noexcept
    {
        return MTextureArray.IsValid();
    }
    // True when the mounted array was built from the current atlas set. A false
    [[nodiscard]] bool IsCurrent() const noexcept
    {
        return Mounted() && MMountedGeneration == MGeneration;
    }
    [[nodiscard]] std::uint32_t Generation() const noexcept
    {
        return MGeneration;
    }
    [[nodiscard]] std::uint32_t MountedGeneration() const noexcept
    {
        return MMountedGeneration;
    }
    [[nodiscard]] Rhi::TextureArrayHandle TextureArray() const noexcept
    {
        return MTextureArray;
    }
    // Array layer of a font id, or 0xFFFFFFFF when this set has no such font.
    [[nodiscard]] std::uint32_t LayerForFontId(std::uint32_t InFontId) const noexcept;
    [[nodiscard]] std::uint32_t LayerForFontIdAndStyle(std::uint32_t InFontId, FontStyle InStyle) const noexcept;

    [[nodiscard]] std::uint32_t Count() const noexcept
    {
        return MCount;
    }
    // Sum of the pixel buffers of every loaded atlas. The memory report uses it
    // to show what the SDF pages actually cost.
    [[nodiscard]] std::uint32_t AtlasPixelBytes() const noexcept
    {
        std::uint32_t ScopedTotal = 0;
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < MCount; ++ScopedIndex)
        {
            ScopedTotal += static_cast<std::uint32_t>(MAtlases[ScopedIndex].Pixels().size());
        }
        return ScopedTotal;
    }
    [[nodiscard]] const FontAtlas* At(std::uint32_t InIndex) const noexcept
    {
        return InIndex < MCount ? &MAtlases[InIndex] : nullptr;
    }
    [[nodiscard]] const FontAtlas* FindByFontId(std::uint32_t InFontId) const noexcept;
    [[nodiscard]] const FontAtlas* FindByFontIdAndStyle(std::uint32_t InFontId, FontStyle InStyle) const noexcept;

    // Ordered fallback chain for missing codepoints (CJK, emoji, symbols).
    bool AddFallbackFontId(std::uint32_t InFontId) noexcept;
    void ClearFallbackFontIds() noexcept;
    [[nodiscard]] std::span<const std::uint32_t> FallbackFontIds() const noexcept
    {
        return std::span<const std::uint32_t>(MFallbackFontIds.data(), MFallbackCount);
    }
    // Looks in the requested face, then each fallback face, then Regular.
    // Returns the glyph and the atlas that actually owns it.
    [[nodiscard]] const GlyphRecord* ResolveGlyph(std::uint32_t InFontId, FontStyle InStyle,
                                                  std::uint32_t InCodepoint,
                                                  const FontAtlas** OutAtlas = nullptr) const noexcept;
    [[nodiscard]] const GlyphRecord* ResolveGlyphById(std::uint32_t InFontId, FontStyle InStyle,
                                                      std::uint32_t InGlyphId,
                                                      const FontAtlas** OutAtlas = nullptr) const noexcept;
    [[nodiscard]] std::uint32_t LayerForAtlas(const FontAtlas* InAtlas) const noexcept;
    // Atlas mounted on a texture-array layer, or null when the layer is unused.
    [[nodiscard]] const FontAtlas* AtlasForLayer(std::uint32_t InLayer) const noexcept
    {
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < MCount; ++ScopedIndex)
        {
            if (LayerForAtlas(&MAtlases[ScopedIndex]) == InLayer)
            {
                return &MAtlases[ScopedIndex];
            }
        }
        return nullptr;
    }

private:
    // Mount scratch is grown when an atlas is added, not when BuildFrame mounts
    [[nodiscard]] bool PrepareMountScratch();

    struct RetainedFont
    {
        std::uint32_t fontId = 0;
        FontStyle style = FontStyle::Regular;
        std::vector<std::byte> bytes{};
        float pixelSize = 32.0f;
        std::uint32_t atlasSize = 0;
        std::uint32_t oversample = 1;
        bool sdf = true;
        // Glyph ids per page for this face. Large pixel sizes need smaller
        // pages, so the budget follows the atlas edge and the em size.
        std::uint32_t pageGlyphs = kGlyphsPerPage;
        FontCoverage coverage{};
    };

    [[nodiscard]] const FontAtlas* FindPage(std::uint32_t InFontId, FontStyle InStyle,
                                            std::uint32_t InPageIndex) const noexcept;
    [[nodiscard]] const FontAtlas* BuildPage(const RetainedFont& InFont, std::uint32_t InPageIndex) noexcept;
    [[nodiscard]] std::uint32_t AcquireSlot() noexcept;

    std::array<FontAtlas, kMaxFontAtlases> MAtlases{};
    // Page lookups touch the LRU stamps, so resolution stays const.
    mutable std::array<std::uint64_t, kMaxFontAtlases> MPageLastUse{};
    std::array<std::uint32_t, kMaxFontAtlases> MPageIndex{};
    std::array<RetainedFont, kMaxRetainedFonts> MRetained{};
    std::array<std::uint32_t, 8> MFallbackFontIds{};
    std::uint32_t MFallbackCount = 0;
    std::uint32_t MRetainedCount = 0;
    std::uint32_t MPageLoads = 0;
    std::uint32_t MPageEvictions = 0;
    mutable std::uint64_t MPageClock = 1;
    std::vector<std::byte> MMountScratch{};
    Rhi::TextureArrayHandle MTextureArray{};
    std::uint32_t MCount = 0;
    // Bumped on every Add()/Clear(); the mount stamps the value it captured.
    std::uint32_t MGeneration = 1;
    std::uint32_t MMountedGeneration = 0;
};

// Decodes one UTF-8 scalar and advances `cursor`. Invalid bytes yield U+FFFD
[[nodiscard]] std::uint32_t DecodeUtf8(std::string_view InText, std::size_t& InCursor) noexcept;
[[nodiscard]] std::uint32_t Utf8GlyphCount(std::string_view InText) noexcept;
}
