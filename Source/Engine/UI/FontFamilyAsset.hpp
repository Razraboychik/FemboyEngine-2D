// FemboyEngine2D - one TOML description for a complete font family.
#pragma once

#include "Engine/Assets/VFS.hpp"
#include "Engine/UI/FontAtlas.hpp"
#include "Engine/UI/FontBuilder.hpp"

#include <array>
#include <string>
#include <string_view>

#include <toml.hpp>

namespace Fby::Ui
{

struct FontFamilyManifest
{
    std::string regular;
    std::string bold;
    std::string italic;
    std::string boldItalic;
    std::string mono;
    FontBuildOptions options{};
};

[[nodiscard]] inline bool ParseFontFamilyManifest(std::string_view InText, FontFamilyManifest* OutManifest,
                                                  std::string* InError = nullptr)
{
    if (OutManifest == nullptr)
    {
        return false;
    }
    toml::table ScopedTable;
    try
    {
        ScopedTable = toml::parse(InText);
    }
    catch (const toml::parse_error& ScopedError)
    {
        if (InError != nullptr)
        {
            *InError = ScopedError.what();
        }
        return false;
    }

    const auto ScopedReadPath = [&ScopedTable](std::string_view InKey, std::string* OutPath)
    {
        if (const auto ScopedValue = ScopedTable[InKey].value<std::string>(); ScopedValue)
        {
            *OutPath = *ScopedValue;
        }
    };
    ScopedReadPath("regular", &OutManifest->regular);
    ScopedReadPath("bold", &OutManifest->bold);
    ScopedReadPath("italic", &OutManifest->italic);
    ScopedReadPath("bold_italic", &OutManifest->boldItalic);
    ScopedReadPath("mono", &OutManifest->mono);
    if (OutManifest->regular.empty() && OutManifest->bold.empty() && OutManifest->italic.empty() &&
        OutManifest->boldItalic.empty() && OutManifest->mono.empty())
    {
        if (InError != nullptr)
        {
            *InError = "font family manifest has no slots";
        }
        return false;
    }

    if (const auto ScopedPixelSize = ScopedTable["font_size"].value<double>(); ScopedPixelSize)
    {
        OutManifest->options.pixelSize = static_cast<float>(*ScopedPixelSize);
    }
    if (const auto ScopedOversample = ScopedTable["font_oversample"].value<std::int64_t>(); ScopedOversample)
    {
        OutManifest->options.oversample =
            static_cast<std::uint32_t>(std::clamp<std::int64_t>(*ScopedOversample, 1, 4));
    }
    if (const auto ScopedAtlasSize = ScopedTable["atlas_size"].value<std::int64_t>(); ScopedAtlasSize)
    {
        OutManifest->options.atlasSize =
            static_cast<std::uint32_t>(std::clamp<std::int64_t>(*ScopedAtlasSize, 256, 8192));
    }
    if (const auto ScopedSdf = ScopedTable["font_sdf"].value<bool>(); ScopedSdf)
    {
        OutManifest->options.sdf = *ScopedSdf;
    }
    if (const auto ScopedAllGlyphs = ScopedTable["all_glyphs"].value<bool>(); ScopedAllGlyphs)
    {
        OutManifest->options.allGlyphs = *ScopedAllGlyphs;
    }
    if (const auto ScopedFontId = ScopedTable["font_id"].value<std::int64_t>(); ScopedFontId)
    {
        OutManifest->options.fontId = static_cast<std::uint32_t>(std::clamp<std::int64_t>(*ScopedFontId, 0, 0xFFFF));
    }
    return true;
}

// Lazy family: Regular is built by Open(), other style faces are built only
// when EnsureStyle() is first called. The VFS and manifest data must outlive it.
class LazyFontFamily
{
public:
    [[nodiscard]] bool Open(VirtualFileSystem& InVfs, std::string_view InManifestPath,
                            const FontBuildOptions& InDefaults, std::string* InError = nullptr)
    {
        MVfs = &InVfs;
        const auto ScopedBytes = InVfs.Read(InManifestPath);
        if (!ScopedBytes)
        {
            if (InError != nullptr)
            {
                *InError = "font family manifest is not present in the VFS";
            }
            return false;
        }
        MManifest.options = InDefaults;
        if (!ParseFontFamilyManifest(
                std::string_view(reinterpret_cast<const char*>(ScopedBytes->data()), ScopedBytes->size()),
                &MManifest, InError))
        {
            return false;
        }
        MLoaded.fill(false);
        return EnsureStyle(FontStyle::Regular, InError) || MManifest.regular.empty();
    }

    [[nodiscard]] bool EnsureStyle(FontStyle InStyle, std::string* InError = nullptr)
    {
        const std::uint32_t ScopedIndex = StyleIndex(InStyle);
        if (ScopedIndex >= MLoaded.size() || MLoaded[ScopedIndex])
        {
            return ScopedIndex < MLoaded.size();
        }
        const std::string* ScopedPath = PathFor(InStyle);
        if (ScopedPath == nullptr || ScopedPath->empty() || MVfs == nullptr)
        {
            if (InError != nullptr)
            {
                *InError = "font style slot is not present in the manifest";
            }
            return false;
        }
        FontBuildOptions ScopedOptions = MManifest.options;
        ScopedOptions.style = InStyle;
        if (!MAtlases.LoadTtfFromVfs(*MVfs, *ScopedPath, ScopedOptions, InError))
        {
            return false;
        }
        MLoaded[ScopedIndex] = true;
        return true;
    }

    [[nodiscard]] FontAtlasSet& Atlases() noexcept
    {
        return MAtlases;
    }
    [[nodiscard]] const FontAtlasSet& Atlases() const noexcept
    {
        return MAtlases;
    }
    [[nodiscard]] std::uint32_t LoadedCount() const noexcept
    {
        std::uint32_t ScopedCount = 0;
        for (const bool ScopedLoaded : MLoaded)
        {
            ScopedCount += ScopedLoaded ? 1u : 0u;
        }
        return ScopedCount;
    }

private:
    [[nodiscard]] static constexpr std::uint32_t StyleIndex(FontStyle InStyle) noexcept
    {
        switch (InStyle)
        {
            case FontStyle::Regular:
                return 0;
            case FontStyle::Bold:
                return 1;
            case FontStyle::Italic:
                return 2;
            case FontStyle::BoldItalic:
                return 3;
            case FontStyle::Mono:
                return 4;
        }
        return 5;
    }

    [[nodiscard]] const std::string* PathFor(FontStyle InStyle) const noexcept
    {
        switch (InStyle)
        {
            case FontStyle::Regular:
                return &MManifest.regular;
            case FontStyle::Bold:
                return &MManifest.bold;
            case FontStyle::Italic:
                return &MManifest.italic;
            case FontStyle::BoldItalic:
                return &MManifest.boldItalic;
            case FontStyle::Mono:
                return &MManifest.mono;
        }
        return nullptr;
    }

    FontAtlasSet MAtlases;
    VirtualFileSystem* MVfs = nullptr;
    FontFamilyManifest MManifest;
    std::array<bool, 5> MLoaded{};
};
}
