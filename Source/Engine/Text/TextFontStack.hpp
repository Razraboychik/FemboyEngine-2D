// FemboyEngine2D - script-to-font stack for the ECS text pipeline.
#pragma once

#include "Engine/Assets/VFS.hpp"
#include "Engine/Text/TextBidi.hpp"
#include "Engine/Text/TextLayout.hpp"
#include "Engine/Text/TextShaper.hpp"
#include "Engine/Text/TextVertical.hpp"
#include "Engine/UI/FontAtlas.hpp"
#include "Engine/UI/FontBuilder.hpp"
#include "Engine/UI/FontCoverage.hpp"
#include "Engine/UI/FontFamilyAsset.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Fby::Text
{

struct FontCandidate
{
    std::string_view Path{};
    std::uint32_t FontId = 0;
    TextScript Script = TextScript::Unknown;
};

// Returns the codepoints of InScript that InCoverage covers inside InText.
[[nodiscard]] inline std::uint32_t CoveredTextCodepoints(const Ui::FontCoverage& InCoverage, std::string_view InText,
                                                         TextScript InScript) noexcept
{
    std::uint32_t ScopedCovered = 0;
    std::size_t ScopedCursor = 0;
    while (ScopedCursor < InText.size())
    {
        const std::uint32_t ScopedCodepoint = DecodeTextUtf8(InText, ScopedCursor);
        if (InScript != TextScript::Unknown && ScriptForCodepoint(ScopedCodepoint) != InScript)
        {
            continue;
        }
        if (InCoverage.Covers(ScopedCodepoint))
        {
            ++ScopedCovered;
        }
    }
    return ScopedCovered;
}

// Owns the atlases and one HarfBuzz face per script. The stack is configured
// once; TextShapeSystem gets Shaper() and TextRenderSystem gets Fonts().
class TextFontStack
{
public:
    // One configured face: the atlas id, the script it routes and the glyph
    // count of the built atlas. Tooling reads this to report what was mounted.
    struct FontSlot
    {
        std::uint32_t FontId = 0;
        TextScript Script = TextScript::Unknown;
        std::uint32_t Glyphs = 0;
        std::uint32_t AtlasBytes = 0;
    };

    [[nodiscard]] bool AddFont(VirtualFileSystem& InVfs, std::string_view InTtfPath, std::uint32_t InFontId,
                               TextScript InScript, float InPixelSize, std::uint32_t InAtlasSize,
                               bool InAllGlyphs, std::string* InError = nullptr) noexcept
    {
        return AddFont(InVfs, InTtfPath, InFontId, InScript, InPixelSize, InAtlasSize, InAllGlyphs,
                       std::span<const std::uint32_t>(), InError);
    }

    // Codepoint subset overload. An empty list keeps the builder defaults, and
    // InAllGlyphs overrides the list so contextual forms reach the atlas.
    [[nodiscard]] bool AddFont(VirtualFileSystem& InVfs, std::string_view InTtfPath, std::uint32_t InFontId,
                               TextScript InScript, float InPixelSize, std::uint32_t InAtlasSize,
                               bool InAllGlyphs, std::span<const std::uint32_t> InCodepoints,
                               std::string* InError = nullptr) noexcept
    {
        if (MCount >= MShapers.size())
        {
            return false;
        }
        Ui::FontBuildOptions ScopedOptions;
        ScopedOptions.fontId = InFontId;
        ScopedOptions.pixelSize = InPixelSize;
        ScopedOptions.atlasSize = InAtlasSize;
        ScopedOptions.allGlyphs = InAllGlyphs;
        // A subset atlas still shapes the spaces inside its own runs, so the
        // space glyph ships with every explicit codepoint list.
        std::vector<std::uint32_t> ScopedCodepoints(InCodepoints.begin(), InCodepoints.end());
        if (!InAllGlyphs && !ScopedCodepoints.empty() &&
            std::find(ScopedCodepoints.begin(), ScopedCodepoints.end(), 0x20u) == ScopedCodepoints.end())
        {
            ScopedCodepoints.push_back(0x20u);
        }
        // Vertical CJK punctuation is a `vert` substitution, so the vertical
        // presentation forms of the punctuation in the subset must be packed as
        // well; otherwise a vertical run reports them as missing glyphs.
        if (!InAllGlyphs && !ScopedCodepoints.empty())
        {
            const std::size_t ScopedHorizontalCount = ScopedCodepoints.size();
            for (std::size_t ScopedIndex = 0; ScopedIndex < ScopedHorizontalCount; ++ScopedIndex)
            {
                const std::uint32_t ScopedVertical = VerticalFormForCodepoint(ScopedCodepoints[ScopedIndex]);
                if (ScopedVertical != 0 &&
                    std::find(ScopedCodepoints.begin(), ScopedCodepoints.end(), ScopedVertical) ==
                        ScopedCodepoints.end())
                {
                    ScopedCodepoints.push_back(ScopedVertical);
                }
            }
        }
        ScopedOptions.codepoints = ScopedCodepoints;
        if (!MAtlases.LoadTtfFromVfs(InVfs, InTtfPath, ScopedOptions, InError))
        {
            return false;
        }
        const auto ScopedBytes = InVfs.Read(InTtfPath);
        if (!ScopedBytes)
        {
            return false;
        }
        if (!MShapers[MCount].SetFont(*ScopedBytes, InPixelSize, InFontId))
        {
            return false;
        }
        std::uint32_t ScopedChain = 0;
        while (ScopedChain < MChainCount && MScripts[ScopedChain] != InScript)
        {
            ++ScopedChain;
        }
        if (ScopedChain == MChainCount)
        {
            if (MChainCount >= MChains.size() || !MRouter.AddScript(InScript, &MChains[MChainCount]))
            {
                return false;
            }
            MScripts[MChainCount++] = InScript;
        }
        if (!MChains[ScopedChain].Add(&MShapers[MCount]))
        {
            return false;
        }
        const Ui::FontAtlas* ScopedAtlas = MAtlases.FindByFontId(InFontId);
        FontSlot& ScopedSlot = MSlots[MCount];
        ScopedSlot.FontId = InFontId;
        ScopedSlot.Script = InScript;
        ScopedSlot.Glyphs = ScopedAtlas != nullptr ? ScopedAtlas->GlyphCount() : 0;
        ScopedSlot.AtlasBytes = ScopedAtlas != nullptr ? static_cast<std::uint32_t>(ScopedAtlas->Pixels().size())
                                                       : 0;
        ++MCount;
        return true;
    }

    // Paged face: the TTF stays resident and glyph pages are built on demand,
    // which is how a font with a full CJK catalogue fits in memory. Pages use
    // the same atlas edge as the rest of the stack, so one texture array can
    // carry every face.
    [[nodiscard]] bool AddPagedFont(VirtualFileSystem& InVfs, std::string_view InTtfPath, std::uint32_t InFontId,
                                    TextScript InScript, float InPixelSize, std::uint32_t InAtlasSize,
                                    std::string* InError = nullptr) noexcept
    {
        if (MCount >= MShapers.size())
        {
            return false;
        }
        Ui::FontBuildOptions ScopedOptions;
        ScopedOptions.fontId = InFontId;
        ScopedOptions.pixelSize = InPixelSize;
        ScopedOptions.atlasSize = InAtlasSize;
        ScopedOptions.allGlyphs = true;
        if (!MAtlases.LoadPagedTtfFromVfs(InVfs, InTtfPath, ScopedOptions, InError))
        {
            return false;
        }
        const auto ScopedBytes = InVfs.Read(InTtfPath);
        if (!ScopedBytes)
        {
            return false;
        }
        if (!MShapers[MCount].SetFont(*ScopedBytes, InPixelSize, InFontId))
        {
            return false;
        }
        std::uint32_t ScopedChain = 0;
        while (ScopedChain < MChainCount && MScripts[ScopedChain] != InScript)
        {
            ++ScopedChain;
        }
        if (ScopedChain == MChainCount)
        {
            if (MChainCount >= MChains.size() || !MRouter.AddScript(InScript, &MChains[MChainCount]))
            {
                return false;
            }
            MScripts[MChainCount++] = InScript;
        }
        if (!MChains[ScopedChain].Add(&MShapers[MCount]))
        {
            return false;
        }
        const Ui::FontAtlas* ScopedAtlas = MAtlases.FindByFontId(InFontId);
        FontSlot& ScopedSlot = MSlots[MCount];
        ScopedSlot.FontId = InFontId;
        ScopedSlot.Script = InScript;
        ScopedSlot.Glyphs = ScopedAtlas != nullptr ? ScopedAtlas->GlyphCount() : 0;
        ScopedSlot.AtlasBytes = ScopedAtlas != nullptr ? static_cast<std::uint32_t>(ScopedAtlas->Pixels().size()) : 0;
        ++MCount;
        return true;
    }

    // Mounts one paged face per candidate and orders every script chain by the
    // coverage the face reports, so a caller does not have to hand-fill the
    // stack. Candidates of the same script form a fallback chain: the first
    // face that shapes without .notdef wins.
    [[nodiscard]] std::uint32_t AddScriptFamily(VirtualFileSystem& InVfs, std::span<const FontCandidate> InCandidates,
                                                float InPixelSize, std::uint32_t InAtlasSize,
                                                std::string* InError = nullptr) noexcept
    {
        std::array<std::uint32_t, 16> ScopedOrder{};
        std::array<std::uint32_t, 16> ScopedCoverage{};
        std::uint32_t ScopedCount = 0;
        for (const FontCandidate& ScopedCandidate : InCandidates)
        {
            if (ScopedCount >= ScopedOrder.size())
            {
                break;
            }
            const auto ScopedBytes = InVfs.Read(ScopedCandidate.Path);
            if (!ScopedBytes)
            {
                continue;
            }
            Ui::FontCoverage ScopedMask;
            (void)Ui::BuildFontCoverageFromTtf(*ScopedBytes, &ScopedMask);
            ScopedOrder[ScopedCount] = ScopedCount;
            ScopedCoverage[ScopedCount] = ScopedMask.CoveredCount();
            ++ScopedCount;
        }
        // Best cover first, then registration order, which keeps the primary
        // face ahead of a fallback that happens to cover more codepoints.
        for (std::uint32_t ScopedIndex = 1; ScopedIndex < ScopedCount; ++ScopedIndex)
        {
            for (std::uint32_t ScopedInner = ScopedIndex;
                 ScopedInner > 0 && ScopedCoverage[ScopedInner] > ScopedCoverage[ScopedInner - 1]; --ScopedInner)
            {
                const std::uint32_t ScopedTempCoverage = ScopedCoverage[ScopedInner];
                ScopedCoverage[ScopedInner] = ScopedCoverage[ScopedInner - 1];
                ScopedCoverage[ScopedInner - 1] = ScopedTempCoverage;
                const std::uint32_t ScopedTempOrder = ScopedOrder[ScopedInner];
                ScopedOrder[ScopedInner] = ScopedOrder[ScopedInner - 1];
                ScopedOrder[ScopedInner - 1] = ScopedTempOrder;
            }
        }
        std::uint32_t ScopedMounted = 0;
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedCount; ++ScopedIndex)
        {
            const FontCandidate& ScopedCandidate = InCandidates[ScopedOrder[ScopedIndex]];
            if (AddPagedFont(InVfs, ScopedCandidate.Path, ScopedCandidate.FontId, ScopedCandidate.Script,
                             InPixelSize, InAtlasSize, InError))
            {
                ++ScopedMounted;
            }
        }
        return ScopedMounted;
    }

    // Orders the faces of a script by how much of InText they cover, best
    // first. The result feeds OrderScriptChain for the current text.
    [[nodiscard]] std::uint32_t RankFaces(TextScript InScript, std::string_view InText,
                                          std::span<std::uint32_t> OutFontIds) const noexcept
    {
        std::array<std::uint32_t, 16> ScopedCovered{};
        std::uint32_t ScopedCount = 0;
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < MCount && ScopedCount < OutFontIds.size(); ++ScopedIndex)
        {
            if (MSlots[ScopedIndex].Script != InScript)
            {
                continue;
            }
            const Ui::FontCoverage* ScopedCoverage = MAtlases.CoverageFor(MSlots[ScopedIndex].FontId);
            OutFontIds[ScopedCount] = MSlots[ScopedIndex].FontId;
            ScopedCovered[ScopedCount] =
                ScopedCoverage != nullptr ? CoveredTextCodepoints(*ScopedCoverage, InText, InScript) : 0;
            ++ScopedCount;
        }
        for (std::uint32_t ScopedIndex = 1; ScopedIndex < ScopedCount; ++ScopedIndex)
        {
            for (std::uint32_t ScopedInner = ScopedIndex;
                 ScopedInner > 0 && ScopedCovered[ScopedInner] > ScopedCovered[ScopedInner - 1]; --ScopedInner)
            {
                const std::uint32_t ScopedTempCoverage = ScopedCovered[ScopedInner];
                ScopedCovered[ScopedInner] = ScopedCovered[ScopedInner - 1];
                ScopedCovered[ScopedInner - 1] = ScopedTempCoverage;
                const std::uint32_t ScopedTempId = OutFontIds[ScopedInner];
                OutFontIds[ScopedInner] = OutFontIds[ScopedInner - 1];
                OutFontIds[ScopedInner - 1] = ScopedTempId;
            }
        }
        return ScopedCount;
    }

    // Rebuilds the chain of a script in the given font id order. This is the
    // one call a caller needs after ranking the faces for its current text.
    [[nodiscard]] bool OrderScriptChain(TextScript InScript, std::span<const std::uint32_t> InFontIds) noexcept
    {
        std::uint32_t ScopedChain = 0;
        while (ScopedChain < MChainCount && MScripts[ScopedChain] != InScript)
        {
            ++ScopedChain;
        }
        if (ScopedChain == MChainCount)
        {
            return false;
        }
        MChains[ScopedChain].Clear();
        for (const std::uint32_t ScopedFontId : InFontIds)
        {
            HarfBuzzShaper* ScopedShaper = nullptr;
            for (std::uint32_t ScopedSlot = 0; ScopedSlot < MCount; ++ScopedSlot)
            {
                if (MSlots[ScopedSlot].FontId == ScopedFontId)
                {
                    ScopedShaper = &MShapers[ScopedSlot];
                    break;
                }
            }
            if (ScopedShaper == nullptr || !MChains[ScopedChain].Add(ScopedShaper))
            {
                return false;
            }
        }
        return true;
    }

    // Mounts one text family from its manifest: every style slot (regular,
    // bold, italic, bold italic, mono) shares one font id and one atlas edge,
    // and the regular face shapes all of them, so a styled run only changes
    // which atlas page a glyph is painted from.
    [[nodiscard]] bool AddFamily(VirtualFileSystem& InVfs, std::string_view InManifestPath, std::uint32_t InFontId,
                                 TextScript InScript, float InPixelSize, std::uint32_t InAtlasSize,
                                 std::string* InError = nullptr) noexcept
    {
        if (MCount >= MShapers.size())
        {
            return false;
        }
        const auto ScopedManifestBytes = InVfs.Read(InManifestPath);
        if (!ScopedManifestBytes)
        {
            if (InError != nullptr)
            {
                *InError = "font family manifest is not present in the VFS";
            }
            return false;
        }
        Ui::FontFamilyManifest ScopedManifest;
        if (!Ui::ParseFontFamilyManifest(
                std::string_view(reinterpret_cast<const char*>(ScopedManifestBytes->data()),
                                 ScopedManifestBytes->size()),
                &ScopedManifest, InError))
        {
            return false;
        }
        Ui::FontBuildOptions ScopedOptions;
        ScopedOptions.fontId = InFontId;
        ScopedOptions.pixelSize = InPixelSize;
        ScopedOptions.atlasSize = InAtlasSize;
        ScopedOptions.sdf = true;
        if (!MAtlases.LoadFamilyManifestFromVfs(InVfs, InManifestPath, ScopedOptions, InError))
        {
            return false;
        }
        const std::string_view ScopedShapePath =
            ScopedManifest.regular.empty() ? std::string_view(ScopedManifest.bold) : std::string_view(ScopedManifest.regular);
        const auto ScopedBytes = InVfs.Read(ScopedShapePath);
        if (!ScopedBytes || !MShapers[MCount].SetFont(*ScopedBytes, InPixelSize, InFontId))
        {
            if (InError != nullptr)
            {
                *InError = "font family has no shapeable regular slot";
            }
            return false;
        }
        std::uint32_t ScopedChain = 0;
        while (ScopedChain < MChainCount && MScripts[ScopedChain] != InScript)
        {
            ++ScopedChain;
        }
        if (ScopedChain == MChainCount)
        {
            if (MChainCount >= MChains.size() || !MRouter.AddScript(InScript, &MChains[MChainCount]))
            {
                return false;
            }
            MScripts[MChainCount++] = InScript;
        }
        if (!MChains[ScopedChain].Add(&MShapers[MCount]))
        {
            return false;
        }
        const Ui::FontAtlas* ScopedAtlas = MAtlases.FindByFontId(InFontId);
        FontSlot& ScopedSlot = MSlots[MCount];
        ScopedSlot.FontId = InFontId;
        ScopedSlot.Script = InScript;
        ScopedSlot.Glyphs = ScopedAtlas != nullptr ? ScopedAtlas->GlyphCount() : 0;
        ScopedSlot.AtlasBytes = ScopedAtlas != nullptr ? static_cast<std::uint32_t>(ScopedAtlas->Pixels().size()) : 0;
        ++MCount;
        return true;
    }

    // Latches the family style atlases a shaped run can need. A styled run
    // resolves through the style slot, which is a separate atlas on the same
    // font id; nothing is loaded for a style the family did not mount.
    [[nodiscard]] bool EnsureStyle(Ui::FontStyle InStyle) noexcept
    {
        return MAtlases.FindByFontIdAndStyle(MSlots[0].FontId, InStyle) != nullptr;
    }

    // Drops every face and atlas so a caller can rebuild for a new pixel size
    // or a new codepoint subset. Shapers release their HarfBuzz handles here.
    void Reset() noexcept
    {
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < MCount; ++ScopedIndex)
        {
            MShapers[ScopedIndex].Reset();
        }
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < MChainCount; ++ScopedIndex)
        {
            MChains[ScopedIndex].Clear();
        }
        MRouter.Clear();
        MAtlases.Clear();
        MScripts = {};
        MSlots = {};
        MCount = 0;
        MChainCount = 0;
    }

    void SetFallback(ITextShaper* InShaper) noexcept
    {
        MRouter.SetFallback(InShaper);
    }
    // Routes bidi-neutral runs such as digits and punctuation to the chain of
    // InScript. Without it those runs have no backend and shaping fails.
    void SetDefaultScript(TextScript InScript) noexcept
    {
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < MChainCount; ++ScopedIndex)
        {
            if (MScripts[ScopedIndex] == InScript)
            {
                MRouter.SetFallback(&MChains[ScopedIndex]);
                return;
            }
        }
        MRouter.SetFallback(nullptr);
    }
    [[nodiscard]] ITextShaper& Shaper() noexcept
    {
        return MRouter;
    }
    [[nodiscard]] const Ui::FontAtlasSet& Fonts() const noexcept
    {
        return MAtlases;
    }
    [[nodiscard]] Ui::FontAtlasSet& Fonts() noexcept
    {
        return MAtlases;
    }
    [[nodiscard]] std::uint32_t Count() const noexcept
    {
        return MCount;
    }
    [[nodiscard]] const FontSlot& Slot(std::uint32_t InIndex) const noexcept
    {
        return MSlots[InIndex < MCount ? InIndex : 0];
    }
    // Total SDF bytes held by the mounted atlases, for the memory report.
    [[nodiscard]] std::uint32_t AtlasBytes() const noexcept
    {
        return MAtlases.AtlasPixelBytes();
    }

private:
    Ui::FontAtlasSet MAtlases;
    std::array<HarfBuzzShaper, 8> MShapers;
    std::array<ChainedShaper, 8> MChains;
    std::array<TextScript, 8> MScripts{};
    std::array<FontSlot, 8> MSlots{};
    ScriptShaper MRouter;
    std::uint32_t MCount = 0;
    std::uint32_t MChainCount = 0;
};

// Loads the page that owns every shaped glyph of a run. A paged face starts
// with page 0 only, so a caller pre-warms the pages a shaped result needs once,
// never per frame. Returns the number of pages that were built.
[[nodiscard]] inline std::uint32_t EnsureGlyphPages(Ui::FontAtlasSet& InFonts,
                                                    std::span<const ShapedGlyph> InGlyphs) noexcept
{
    const std::uint32_t ScopedBefore = InFonts.PageLoads();
    for (const ShapedGlyph& ScopedGlyph : InGlyphs)
    {
        if (InFonts.HasGlyph(ScopedGlyph.FontId, Ui::FontStyle::Regular, ScopedGlyph.GlyphId))
        {
            continue;
        }
        (void)InFonts.EnsurePageForGlyph(ScopedGlyph.FontId, Ui::FontStyle::Regular, ScopedGlyph.GlyphId);
    }
    return InFonts.PageLoads() - ScopedBefore;
}

[[nodiscard]] inline std::uint32_t EnsureShapedGlyphPages(Ui::FontAtlasSet& InFonts,
                                                          const TextShapeResult& InShaped) noexcept
{
    return EnsureGlyphPages(InFonts, std::span<const ShapedGlyph>(InShaped.Glyphs.data(), InShaped.Count));
}
}
