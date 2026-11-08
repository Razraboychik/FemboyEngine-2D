// FemboyEngine2D - offline sprite atlas packer: PNG sprites -> page PNG + metadata.
#include "Engine/Assets/SpriteAtlas.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace
{
struct SpriteImage
{
    Fby::Assets::AtlasSpriteSource Source{};
    std::vector<std::uint8_t> Pixels;
};

[[nodiscard]] std::uint32_t HashName(std::string_view InName) noexcept
{
    std::uint32_t ScopedHash = 2166136261u;
    for (const char ScopedChar : InName)
    {
        ScopedHash ^= static_cast<std::uint8_t>(ScopedChar);
        ScopedHash *= 16777619u;
    }
    return ScopedHash == 0 ? 1u : ScopedHash;
}

void PrintUsage()
{
    std::printf("usage: fby_atlas --input <dir> --output <base> [--page 2048] [--padding 2]\n");
}

bool ParseUint(const char* InText, std::uint32_t* OutValue)
{
    char* ScopedEnd = nullptr;
    const unsigned long ScopedValue = std::strtoul(InText, &ScopedEnd, 10);
    if (ScopedEnd == InText || *ScopedEnd != '\0' || ScopedValue > 16384ul)
    {
        return false;
    }
    *OutValue = static_cast<std::uint32_t>(ScopedValue);
    return true;
}
}

int main(int InArgc, char** InArgv)
{
    std::filesystem::path ScopedInput;
    std::filesystem::path ScopedOutput;
    std::uint32_t ScopedPage = 2048;
    std::uint32_t ScopedPadding = 2;

    for (int ScopedI = 1; ScopedI < InArgc; ++ScopedI)
    {
        const std::string_view ScopedArg = InArgv[ScopedI];
        if (ScopedArg == "--input" && ScopedI + 1 < InArgc)
        {
            ScopedInput = InArgv[++ScopedI];
        }
        else if (ScopedArg == "--output" && ScopedI + 1 < InArgc)
        {
            ScopedOutput = InArgv[++ScopedI];
        }
        else if (ScopedArg == "--page" && ScopedI + 1 < InArgc)
        {
            if (!ParseUint(InArgv[++ScopedI], &ScopedPage))
            {
                std::fprintf(stderr, "fby_atlas: invalid --page\n");
                return 2;
            }
        }
        else if (ScopedArg == "--padding" && ScopedI + 1 < InArgc)
        {
            if (!ParseUint(InArgv[++ScopedI], &ScopedPadding) || ScopedPadding > 32)
            {
                std::fprintf(stderr, "fby_atlas: invalid --padding\n");
                return 2;
            }
        }
        else
        {
            PrintUsage();
            return 2;
        }
    }
    if (ScopedInput.empty() || ScopedOutput.empty())
    {
        PrintUsage();
        return 2;
    }

    std::vector<std::filesystem::path> ScopedPaths;
    for (const std::filesystem::directory_entry& ScopedEntry : std::filesystem::directory_iterator(ScopedInput))
    {
        if (ScopedEntry.is_regular_file() && ScopedEntry.path().extension() == ".png")
        {
            ScopedPaths.push_back(ScopedEntry.path());
        }
    }
    std::sort(ScopedPaths.begin(), ScopedPaths.end());
    if (ScopedPaths.empty())
    {
        std::fprintf(stderr, "fby_atlas: no PNG files in '%s'\n", ScopedInput.string().c_str());
        return 1;
    }

    std::vector<SpriteImage> ScopedImages;
    ScopedImages.reserve(ScopedPaths.size());
    for (const std::filesystem::path& ScopedPath : ScopedPaths)
    {
        int ScopedWidth = 0;
        int ScopedHeight = 0;
        int ScopedChannels = 0;
        stbi_uc* ScopedPixels = stbi_load(ScopedPath.string().c_str(), &ScopedWidth, &ScopedHeight, &ScopedChannels, 4);
        if (ScopedPixels == nullptr || ScopedWidth <= 0 || ScopedHeight <= 0)
        {
            std::fprintf(stderr, "fby_atlas: cannot decode '%s'\n", ScopedPath.string().c_str());
            if (ScopedPixels != nullptr)
            {
                stbi_image_free(ScopedPixels);
            }
            return 1;
        }
        SpriteImage ScopedImage;
        ScopedImage.Source.Id = HashName(ScopedPath.filename().string());
        ScopedImage.Source.Width = static_cast<std::uint32_t>(ScopedWidth);
        ScopedImage.Source.Height = static_cast<std::uint32_t>(ScopedHeight);
        ScopedImage.Source.Padding = ScopedPadding;
        ScopedImage.Pixels.assign(ScopedPixels, ScopedPixels + static_cast<std::size_t>(ScopedWidth) * ScopedHeight * 4u);
        stbi_image_free(ScopedPixels);
        for (const SpriteImage& ScopedOther : ScopedImages)
        {
            if (ScopedOther.Source.Id == ScopedImage.Source.Id)
            {
                std::fprintf(stderr, "fby_atlas: duplicate sprite id for '%s'\n", ScopedPath.string().c_str());
                return 1;
            }
        }
        ScopedImages.push_back(std::move(ScopedImage));
    }

    std::vector<Fby::Assets::AtlasSpriteSource> ScopedSources;
    ScopedSources.reserve(ScopedImages.size());
    for (const SpriteImage& ScopedImage : ScopedImages)
    {
        ScopedSources.push_back(ScopedImage.Source);
    }
    Fby::Assets::SpriteAtlas ScopedAtlas{};
    const Fby::Assets::AtlasPackError ScopedPack =
        Fby::Assets::PackSpriteAtlas(ScopedSources, ScopedPage, ScopedPage, &ScopedAtlas);
    if (ScopedPack != Fby::Assets::AtlasPackError::None)
    {
        std::fprintf(stderr, "fby_atlas: packing failed (%u)\n", static_cast<unsigned>(ScopedPack));
        return 1;
    }

    std::vector<std::uint8_t> ScopedPagePixels(static_cast<std::size_t>(ScopedPage) * ScopedPage * 4u, 0u);
    for (std::size_t ScopedI = 0; ScopedI < ScopedImages.size(); ++ScopedI)
    {
        const Fby::Assets::AtlasSprite& ScopedSprite = ScopedAtlas.Sprites[ScopedI];
        const SpriteImage& ScopedImage = ScopedImages[ScopedI];
        for (std::uint32_t ScopedY = 0; ScopedY < ScopedSprite.Height; ++ScopedY)
        {
            const std::uint8_t* ScopedSource =
                ScopedImage.Pixels.data() + static_cast<std::size_t>(ScopedY) * ScopedSprite.Width * 4u;
            std::uint8_t* ScopedDestination = ScopedPagePixels.data() +
                                              (static_cast<std::size_t>(ScopedSprite.Y + ScopedY) * ScopedPage +
                                               ScopedSprite.X) *
                                                  4u;
            std::memcpy(ScopedDestination, ScopedSource, static_cast<std::size_t>(ScopedSprite.Width) * 4u);
        }
    }

    const std::filesystem::path ScopedPagePath = ScopedOutput.string() + ".png";
    const std::filesystem::path ScopedMetaPath = ScopedOutput.string() + ".fbyatlas";
    if (stbi_write_png(ScopedPagePath.string().c_str(), static_cast<int>(ScopedPage), static_cast<int>(ScopedPage), 4,
                       ScopedPagePixels.data(), static_cast<int>(ScopedPage * 4u)) == 0)
    {
        std::fprintf(stderr, "fby_atlas: cannot write '%s'\n", ScopedPagePath.string().c_str());
        return 1;
    }

    std::ofstream ScopedMeta(ScopedMetaPath, std::ios::trunc);
    if (!ScopedMeta)
    {
        std::fprintf(stderr, "fby_atlas: cannot write '%s'\n", ScopedMetaPath.string().c_str());
        return 1;
    }
    ScopedMeta << "FBYATLAS 1\n";
    ScopedMeta << "page " << ScopedPage << ' ' << ScopedPage << ' ' << ScopedPagePath.filename().string() << '\n';
    for (std::uint32_t ScopedI = 0; ScopedI < ScopedAtlas.SpriteCount; ++ScopedI)
    {
        const Fby::Assets::AtlasSprite& ScopedSprite = ScopedAtlas.Sprites[ScopedI];
        ScopedMeta << "sprite " << ScopedSprite.Id << ' ' << ScopedSprite.X << ' ' << ScopedSprite.Y << ' '
                   << ScopedSprite.Width << ' ' << ScopedSprite.Height << ' ' << ScopedSprite.PivotX << ' '
                   << ScopedSprite.PivotY << '\n';
    }
    std::printf("fby_atlas: %u sprites -> %s, %s\n", ScopedAtlas.SpriteCount, ScopedPagePath.string().c_str(),
                ScopedMetaPath.string().c_str());
    return 0;
}
