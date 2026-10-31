// FemboyEngine2D - deterministic import policy (.femeta generation).
#include "Editor/Infrastructure/AssetImport.hpp"

#include "Engine/Assets/PakFormat.hpp"
#include "Editor/Infrastructure/ProjectConfig.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <fstream>
#include <string>

namespace Fby::Editor
{
namespace
{
void SetError(std::string* InError, std::string InText)
{
    if (InError != nullptr)
    {
        *InError = std::move(InText);
    }
}

[[nodiscard]] std::string Lower(std::string_view InText)
{
    std::string ScopedResult(InText);
    std::transform(ScopedResult.begin(), ScopedResult.end(), ScopedResult.begin(),
                   [](unsigned char InCharacter) { return static_cast<char>(std::tolower(InCharacter)); });
    return ScopedResult;
}

[[nodiscard]] std::string ExtensionOf(std::string_view InPath)
{
    const std::size_t ScopedSlash = InPath.find_last_of("/\\");
    const std::size_t ScopedDot = InPath.find_last_of('.');
    if (ScopedDot == std::string_view::npos || (ScopedSlash != std::string_view::npos && ScopedDot < ScopedSlash))
    {
        return {};
    }
    return Lower(InPath.substr(ScopedDot));
}

[[nodiscard]] std::string ReplaceExtension(std::string_view InPath, std::string_view InExtension)
{
    const std::size_t ScopedSlash = InPath.find_last_of('/');
    const std::size_t ScopedDot = InPath.find_last_of('.');
    if (ScopedDot == std::string_view::npos || (ScopedSlash != std::string_view::npos && ScopedDot < ScopedSlash))
    {
        std::string ScopedResult(InPath);
        ScopedResult += InExtension;
        return ScopedResult;
    }
    return std::string(InPath.substr(0, ScopedDot)) + std::string(InExtension);
}

[[nodiscard]] bool CopyPath(char (&InDestination)[kAssetPathCapacity], std::string_view InValue, std::string* InError)
{
    if (InValue.empty() || InValue.size() >= kAssetPathCapacity)
    {
        SetError(InError, "asset path is empty or too long");
        return false;
    }
    std::memcpy(InDestination, InValue.data(), InValue.size());
    InDestination[InValue.size()] = '\0';
    return true;
}
}

bool InferAssetKindFromPath(std::string_view InSourcePath, AssetKind* OutKind) noexcept
{
    if (OutKind == nullptr)
    {
        return false;
    }
    const std::string ScopedExtension = ExtensionOf(InSourcePath);
    if (ScopedExtension == ".png" || ScopedExtension == ".tga")
    {
        *OutKind = AssetKind::Texture;
        return true;
    }
    if (ScopedExtension == ".wav" || ScopedExtension == ".ogg" || ScopedExtension == ".opus")
    {
        *OutKind = AssetKind::AudioClip;
        return true;
    }
    if (ScopedExtension == ".webm")
    {
        *OutKind = AssetKind::Video;
        return true;
    }
    if (ScopedExtension == ".txt" || ScopedExtension == ".md" || ScopedExtension == ".fbytext")
    {
        *OutKind = AssetKind::Text;
        return true;
    }
    if (ScopedExtension == ".ttf" || ScopedExtension == ".otf" || ScopedExtension == ".ttc")
    {
        *OutKind = AssetKind::Font;
        return true;
    }
    if (ScopedExtension == ".fbyfontset" || Lower(InSourcePath).ends_with(".fontfamily.toml"))
    {
        *OutKind = AssetKind::FontFamily;
        return true;
    }
    if (ScopedExtension == ".toml" || ScopedExtension == ".fescene")
    {
        const std::string ScopedNormalized = Assets::NormalizePakPath(InSourcePath);
        // The atlas manifest is a TextureArray asset. Scenes/prefabs are the
        if (ScopedNormalized.starts_with("atlas/") || ScopedNormalized.find("/atlas/") != std::string::npos)
        {
            *OutKind = AssetKind::TextureArray;
            return true;
        }
        if (ScopedNormalized.starts_with("scenes/") || ScopedNormalized.find("/scenes/") != std::string::npos ||
            ScopedNormalized.starts_with("prefabs/") || ScopedNormalized.find("/prefabs/") != std::string::npos)
        {
            *OutKind = AssetKind::Scene;
            return true;
        }
        return false;
    }
    if (ScopedExtension == ".feskl")
    {
        *OutKind = AssetKind::Skeleton;
        return true;
    }
    if (ScopedExtension == ".fanim")
    {
        *OutKind = AssetKind::Animation;
        return true;
    }
    if (ScopedExtension == ".fmat")
    {
        *OutKind = AssetKind::Material;
        return true;
    }
    if (ScopedExtension == ".fby" || ScopedExtension == ".fscript")
    {
        *OutKind = AssetKind::Script;
        return true;
    }
    return false;
}

std::string CookedPathForSource(std::string_view InSourcePath, AssetKind InKind)
{
    const std::string ScopedNormalized = Assets::NormalizePakPath(InSourcePath);
    switch (InKind)
    {
        case AssetKind::Texture:
            return ReplaceExtension(ScopedNormalized, ".ftex");
        case AssetKind::AudioClip:
            return ReplaceExtension(ScopedNormalized, ".fpcm");
        case AssetKind::Text:
            return ReplaceExtension(ScopedNormalized, ".fbytext");
        case AssetKind::Font:
            return ReplaceExtension(ScopedNormalized, ".fbyfont");
        case AssetKind::FontFamily:
            return {};
        case AssetKind::Video:
        case AssetKind::Scene:
        case AssetKind::Skeleton:
        case AssetKind::Animation:
        case AssetKind::Material:
        case AssetKind::Script:
        case AssetKind::TextureArray:
        case AssetKind::Mesh:
        case AssetKind::SpriteSheet:
        case AssetKind::Unknown:
            return {};
    }
    return {};
}

bool BuildDefaultAssetMeta(std::string_view InSourcePath, AssetKind InKind, Assets::TextureFormat InTextureFormat,
                           Assets::AssetMeta* OutMeta, std::string* InError)
{
    if (OutMeta == nullptr)
    {
        SetError(InError, "asset metadata output is null");
        return false;
    }
    const std::string ScopedNormalized = Assets::NormalizePakPath(InSourcePath);
    if (ScopedNormalized.empty())
    {
        SetError(InError, "source path is empty");
        return false;
    }
    if (InKind == AssetKind::Unknown)
    {
        SetError(InError, "cannot build metadata for an unknown asset kind");
        return false;
    }

    Assets::AssetMeta ScopedMeta;
    ScopedMeta.guid = Assets::ComputeAssetGuid(ScopedNormalized, InKind);
    ScopedMeta.id = AssetId::FromPath(ScopedNormalized, InKind);
    ScopedMeta.kind = InKind;
    if (!CopyPath(ScopedMeta.sourcePath, ScopedNormalized, InError))
    {
        return false;
    }
    const std::string ScopedCooked = CookedPathForSource(ScopedNormalized, InKind);
    if (!ScopedCooked.empty())
    {
        if (!CopyPath(ScopedMeta.cookedPath, ScopedCooked, InError))
        {
            return false;
        }
    }
    ScopedMeta.import.textureFormat = static_cast<std::uint32_t>(InTextureFormat);
    // The cooker reads these defaults. max_size is clamped by the texture
    ScopedMeta.import.maxSize = 2048;
    ScopedMeta.import.srgb = true;
    ScopedMeta.import.generateMips = true;
    if (InKind == AssetKind::Font)
    {
        ScopedMeta.import.fontStyle = 0;
        ScopedMeta.import.fontPixels = 32;
        ScopedMeta.import.fontOversample = 1;
        ScopedMeta.import.fontSdf = false;
    }
    ScopedMeta.contentGeneration = 0;
    ScopedMeta.valid = true;
    *OutMeta = ScopedMeta;
    return true;
}

std::string MetaFilePathForSource(std::string_view InSourcePath)
{
    std::string ScopedResult(Assets::NormalizePakPath(InSourcePath));
    ScopedResult += ".femeta";
    return ScopedResult;
}

bool WriteMetaBesideSource(const std::filesystem::path& InAssetsRoot, std::string_view InSourcePath,
                           const Assets::AssetMeta& InMeta, std::string* InError)
{
    const std::string ScopedRelative = MetaFilePathForSource(InSourcePath);
    const std::filesystem::path ScopedPhysical = InAssetsRoot / std::filesystem::path(ScopedRelative);

    std::string ScopedText;
    if (!Assets::WriteAssetMetaFile(InMeta, {}, &ScopedText, InError))
    {
        return false;
    }
    return WriteFileAtomically(ScopedPhysical, ScopedText, InError);
}

std::uint64_t HashSourceFile(const std::filesystem::path& InPhysicalPath) noexcept
{
    std::ifstream ScopedStream(InPhysicalPath, std::ios::binary);
    if (!ScopedStream)
    {
        return 0;
    }
    std::uint64_t ScopedHash = 1469598103934665603ull;
    std::array<char, 64 * 1024> ScopedBuffer{};
    while (ScopedStream)
    {
        ScopedStream.read(ScopedBuffer.data(), static_cast<std::streamsize>(ScopedBuffer.size()));
        const std::streamsize ScopedRead = ScopedStream.gcount();
        for (std::streamsize ScopedI = 0; ScopedI < ScopedRead; ++ScopedI)
        {
            ScopedHash ^= static_cast<std::uint8_t>(ScopedBuffer[static_cast<std::size_t>(ScopedI)]);
            ScopedHash *= 1099511628211ull;
        }
    }
    return ScopedHash;
}

Assets::AssetHandle ImportSourceFile(Assets::AssetDatabase& InDatabase, const std::filesystem::path& InAssetsRoot,
                                     std::string_view InSourcePath, Assets::TextureFormat InTextureFormat,
                                     std::string* InError)
{
    const std::string ScopedNormalized = Assets::NormalizePakPath(InSourcePath);
    if (ScopedNormalized.empty() || Assets::IsAssetMetaPath(ScopedNormalized))
    {
        SetError(InError, "not an importable source path");
        return {};
    }
    if (const Assets::AssetHandle ScopedExisting = InDatabase.FindHandleBySourcePath(ScopedNormalized);
        ScopedExisting.IsValid())
    {
        return ScopedExisting;
    }

    AssetKind ScopedKind = AssetKind::Unknown;
    if (!InferAssetKindFromPath(ScopedNormalized, &ScopedKind))
    {
        SetError(InError, "unrecognized asset extension");
        return {};
    }
    Assets::AssetMeta ScopedMeta;
    if (!BuildDefaultAssetMeta(ScopedNormalized, ScopedKind, InTextureFormat, &ScopedMeta, InError))
    {
        return {};
    }
    ScopedMeta.sourceHash = HashSourceFile(InAssetsRoot / std::filesystem::path(ScopedNormalized));

    if (!WriteMetaBesideSource(InAssetsRoot, ScopedNormalized, ScopedMeta, InError))
    {
        return {};
    }
    const Assets::AssetHandle ScopedHandle = InDatabase.RegisterMeta(ScopedMeta, {}, InError);
    if (!ScopedHandle.IsValid())
    {
        return {};
    }
    return ScopedHandle;
}
}
