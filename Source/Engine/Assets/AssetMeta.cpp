// FemboyEngine2D - .femeta metadata parsing and deterministic writing.
#include "Engine/Assets/AssetMeta.hpp"

#include "Engine/Assets/PakFormat.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <format>
#include <string>

#include <toml.hpp>

namespace Fby::Assets
{
namespace
{
void SetError(std::string* InError, std::string_view InText)
{
    if (InError != nullptr)
    {
        *InError = InText;
    }
}

[[nodiscard]] bool CopyPath(char (&InDestination)[kAssetPathCapacity], std::string_view InValue, std::string* InError)
{
    if (InValue.empty() || InValue.size() >= kAssetPathCapacity)
    {
        SetError(InError, "asset path is empty or exceeds kAssetPathCapacity");
        return false;
    }
    std::memcpy(InDestination, InValue.data(), InValue.size());
    InDestination[InValue.size()] = '\0';
    return true;
}

[[nodiscard]] bool ReadHexOrInt(const toml::node& InNode, std::uint64_t* Out) noexcept
{
    if (const auto ScopedInteger = InNode.value<std::int64_t>(); ScopedInteger && *ScopedInteger >= 0)
    {
        *Out = static_cast<std::uint64_t>(*ScopedInteger);
        return true;
    }
    if (const auto ScopedText = InNode.value<std::string>(); ScopedText)
    {
        std::string_view ScopedView(*ScopedText);
        int ScopedBase = 10;
        if (ScopedView.starts_with("0x") || ScopedView.starts_with("0X"))
        {
            ScopedView.remove_prefix(2);
            ScopedBase = 16;
        }
        if (ScopedView.empty())
        {
            return false;
        }
        std::uint64_t ScopedValue = 0;
        for (const char ScopedCharacter : ScopedView)
        {
            std::uint32_t ScopedDigit = 0;
            if (ScopedCharacter >= '0' && ScopedCharacter <= '9')
            {
                ScopedDigit = static_cast<std::uint32_t>(ScopedCharacter - '0');
            }
            else if (ScopedCharacter >= 'a' && ScopedCharacter <= 'f')
            {
                ScopedDigit = static_cast<std::uint32_t>(ScopedCharacter - 'a' + 10);
            }
            else if (ScopedCharacter >= 'A' && ScopedCharacter <= 'F')
            {
                ScopedDigit = static_cast<std::uint32_t>(ScopedCharacter - 'A' + 10);
            }
            else
            {
                return false;
            }
            if (ScopedDigit >= static_cast<std::uint32_t>(ScopedBase))
            {
                return false;
            }
            ScopedValue = ScopedValue * static_cast<std::uint64_t>(ScopedBase) + ScopedDigit;
        }
        *Out = ScopedValue;
        return true;
    }
    return false;
}
}

AssetGuid ComputeAssetGuid(std::string_view InSourcePath, AssetKind InKind) noexcept
{
    // FNV-1a with a domain separator distinct from AssetId::FromPath, so a GUID
    std::uint64_t ScopedHash = 1469598103934665603ull;
    const auto ScopedMix = [&ScopedHash](std::uint8_t InByte)
    {
        ScopedHash ^= InByte;
        ScopedHash *= 1099511628211ull;
    };
    ScopedMix('G');
    ScopedMix('U');
    ScopedMix('I');
    ScopedMix('D');
    ScopedMix(static_cast<std::uint8_t>(static_cast<std::uint16_t>(InKind) & 0xFFu));
    ScopedMix(static_cast<std::uint8_t>((static_cast<std::uint16_t>(InKind) >> 8) & 0xFFu));
    for (const char ScopedCharacter : InSourcePath)
    {
        ScopedMix(static_cast<std::uint8_t>(static_cast<unsigned char>(ScopedCharacter)));
    }
    return ScopedHash != 0 ? ScopedHash : 1ull;
}

AssetHandle MakeAssetHandle(std::uint32_t InIndex, std::uint32_t InGeneration) noexcept
{
    AssetHandle ScopedHandle;
    ScopedHandle.index = InIndex;
    ScopedHandle.generation = InGeneration;
    return ScopedHandle;
}

std::string_view AssetKindName(AssetKind InKind) noexcept
{
    switch (InKind)
    {
        case AssetKind::Skeleton:
            return "skeleton";
        case AssetKind::Texture:
            return "texture";
        case AssetKind::TextureArray:
            return "texture-array";
        case AssetKind::AudioClip:
            return "audio";
        case AssetKind::Mesh:
            return "mesh";
        case AssetKind::SpriteSheet:
            return "sprite-sheet";
        case AssetKind::Scene:
            return "scene";
        case AssetKind::Video:
            return "video";
        case AssetKind::Animation:
            return "animation";
        case AssetKind::Material:
            return "material";
        case AssetKind::Script:
            return "script";
        case AssetKind::Text:
            return "text";
        case AssetKind::Font:
            return "font";
        case AssetKind::FontFamily:
            return "font-family";
        case AssetKind::Unknown:
            break;
    }
    return "unknown";
}

bool ParseAssetKind(std::string_view InText, AssetKind* Out) noexcept
{
    if (Out == nullptr)
    {
        return false;
    }
    if (InText == "skeleton")
    {
        *Out = AssetKind::Skeleton;
    }
    else if (InText == "texture" || InText == "png" || InText == "tga")
    {
        *Out = AssetKind::Texture;
    }
    else if (InText == "texture-array" || InText == "atlas")
    {
        *Out = AssetKind::TextureArray;
    }
    else if (InText == "audio" || InText == "wav" || InText == "ogg" || InText == "opus")
    {
        *Out = AssetKind::AudioClip;
    }
    else if (InText == "video" || InText == "webm" || InText == "vp9")
    {
        *Out = AssetKind::Video;
    }
    else if (InText == "animation" || InText == "timeline")
    {
        *Out = AssetKind::Animation;
    }
    else if (InText == "text" || InText == "txt" || InText == "md" || InText == "fbytext")
    {
        *Out = AssetKind::Text;
    }
    else if (InText == "font" || InText == "ttf" || InText == "otf" || InText == "fbyfont")
    {
        *Out = AssetKind::Font;
    }
    else if (InText == "font-family" || InText == "fbyfontset" || InText == "fontfamily")
    {
        *Out = AssetKind::FontFamily;
    }
    else if (InText == "material")
    {
        *Out = AssetKind::Material;
    }
    else if (InText == "script")
    {
        *Out = AssetKind::Script;
    }
    else if (InText == "mesh")
    {
        *Out = AssetKind::Mesh;
    }
    else if (InText == "sprite-sheet" || InText == "sheet")
    {
        *Out = AssetKind::SpriteSheet;
    }
    else if (InText == "scene" || InText == "prefab")
    {
        *Out = AssetKind::Scene;
    }
    else
    {
        return false;
    }
    return true;
}

std::string_view CompressionFormatName(TextureFormat InFormat) noexcept
{
    return TextureFormatName(InFormat);
}

bool ParseCompressionFormat(std::string_view InText, TextureFormat* Out) noexcept
{
    if (Out == nullptr)
    {
        return false;
    }
    for (std::uint32_t ScopedValue = 0; ScopedValue <= static_cast<std::uint32_t>(TextureFormat::Astc6x6Srgb);
         ++ScopedValue)
    {
        const auto ScopedFormat = static_cast<TextureFormat>(ScopedValue);
        if (InText == TextureFormatName(ScopedFormat))
        {
            *Out = ScopedFormat;
            return true;
        }
    }
    return false;
}

bool ParseAssetMetaFile(std::string_view InText, AssetMeta* InMeta, AssetDependency* InDependencies,
                        std::uint32_t InDependencyCapacity, std::uint32_t* OutDependencyCount, std::string* InError)
{
    if (InMeta == nullptr || OutDependencyCount == nullptr)
    {
        SetError(InError, "null output");
        return false;
    }
    *OutDependencyCount = 0;
    *InMeta = {};

    toml::table ScopedTable;
    try
    {
        ScopedTable = toml::parse(InText);
    }
    catch (const toml::parse_error& ScopedParseError)
    {
        SetError(InError, std::format("femeta TOML error: {}", ScopedParseError.description()));
        return false;
    }

    std::uint64_t ScopedGuid = 0;
    std::uint64_t ScopedAssetId = 0;
    if (const toml::node* ScopedNode = ScopedTable.get("guid");
        ScopedNode != nullptr && !ReadHexOrInt(*ScopedNode, &ScopedGuid))
    {
        SetError(InError, "femeta: guid must be a hex string or a non-negative integer");
        return false;
    }
    if (const toml::node* ScopedNode = ScopedTable.get("asset_id");
        ScopedNode != nullptr && !ReadHexOrInt(*ScopedNode, &ScopedAssetId))
    {
        SetError(InError, "femeta: asset_id must be a hex string or a non-negative integer");
        return false;
    }

    AssetKind ScopedKind = AssetKind::Unknown;
    if (const auto ScopedType = ScopedTable["type"].value<std::string>(); ScopedType)
    {
        if (!ParseAssetKind(*ScopedType, &ScopedKind))
        {
            SetError(InError, "femeta: unknown resource type");
            return false;
        }
    }

    if (const auto ScopedSource = ScopedTable["source"].value<std::string>(); ScopedSource)
    {
        if (!CopyPath(InMeta->sourcePath, *ScopedSource, InError))
        {
            return false;
        }
    }
    if (const auto ScopedCooked = ScopedTable["cooked"].value<std::string>(); ScopedCooked)
    {
        if (!CopyPath(InMeta->cookedPath, *ScopedCooked, InError))
        {
            return false;
        }
    }

    std::uint64_t ScopedSourceHash = 0;
    std::uint64_t ScopedCookedHash = 0;
    if (const toml::node* ScopedNode = ScopedTable.get("source_hash"); ScopedNode != nullptr)
    {
        (void)ReadHexOrInt(*ScopedNode, &ScopedSourceHash);
    }
    if (const toml::node* ScopedNode = ScopedTable.get("cooked_hash"); ScopedNode != nullptr)
    {
        (void)ReadHexOrInt(*ScopedNode, &ScopedCookedHash);
    }

    if (ScopedGuid == 0 && InMeta->sourcePath[0] != '\0')
    {
        ScopedGuid = ComputeAssetGuid(InMeta->sourcePath, ScopedKind);
    }
    if (ScopedAssetId == 0 && InMeta->sourcePath[0] != '\0')
    {
        ScopedAssetId = AssetId::FromPath(InMeta->sourcePath, ScopedKind).value;
    }

    InMeta->guid = ScopedGuid;
    InMeta->id = AssetId{ScopedAssetId};
    InMeta->kind = ScopedKind;
    InMeta->sourceHash = ScopedSourceHash;
    InMeta->cookedHash = ScopedCookedHash;

    if (const toml::table* ScopedImport = ScopedTable["import"].as_table(); ScopedImport != nullptr)
    {
        if (const auto ScopedFormat = (*ScopedImport)["format"].value<std::string>(); ScopedFormat)
        {
            TextureFormat ScopedParsed{};
            if (ParseCompressionFormat(*ScopedFormat, &ScopedParsed))
            {
                InMeta->import.textureFormat = static_cast<std::uint32_t>(ScopedParsed);
            }
        }
        if (const auto ScopedMaxSize = (*ScopedImport)["max_size"].value<std::int64_t>();
            ScopedMaxSize && *ScopedMaxSize > 0)
        {
            InMeta->import.maxSize = static_cast<std::uint32_t>(std::min<std::int64_t>(*ScopedMaxSize, 16384));
        }
        if (const auto ScopedSrgb = (*ScopedImport)["srgb"].value<bool>(); ScopedSrgb)
        {
            InMeta->import.srgb = *ScopedSrgb;
        }
        if (const auto ScopedMips = (*ScopedImport)["mips"].value<bool>(); ScopedMips)
        {
            InMeta->import.generateMips = *ScopedMips;
        }
        if (const auto ScopedFontStyle = (*ScopedImport)["font_style"].value<std::int64_t>(); ScopedFontStyle)
        {
            InMeta->import.fontStyle =
                static_cast<std::uint32_t>(std::clamp<std::int64_t>(*ScopedFontStyle, 0, 4));
        }
        if (const auto ScopedFontPixels = (*ScopedImport)["font_size"].value<std::int64_t>();
            ScopedFontPixels && *ScopedFontPixels > 0)
        {
            InMeta->import.fontPixels =
                static_cast<std::uint32_t>(std::clamp<std::int64_t>(*ScopedFontPixels, 4, 256));
        }
        if (const auto ScopedFontOversample = (*ScopedImport)["font_oversample"].value<std::int64_t>();
            ScopedFontOversample && *ScopedFontOversample > 0)
        {
            InMeta->import.fontOversample =
                static_cast<std::uint32_t>(std::clamp<std::int64_t>(*ScopedFontOversample, 1, 4));
        }
        if (const auto ScopedFontSdf = (*ScopedImport)["font_sdf"].value<bool>(); ScopedFontSdf)
        {
            InMeta->import.fontSdf = *ScopedFontSdf;
        }
    }

    std::uint32_t ScopedCount = 0;
    if (const toml::table* ScopedDependencyTable = ScopedTable["dependencies"].as_table();
        ScopedDependencyTable != nullptr)
    {
        if (const toml::array* ScopedAssets = (*ScopedDependencyTable)["assets"].as_array(); ScopedAssets != nullptr)
        {
            for (const toml::node& ScopedNode : *ScopedAssets)
            {
                if (ScopedCount >= InDependencyCapacity)
                {
                    SetError(InError, "femeta: too many dependencies");
                    return false;
                }
                std::uint64_t ScopedValue = 0;
                if (!ReadHexOrInt(ScopedNode, &ScopedValue))
                {
                    SetError(InError, "femeta: dependency must be a GUID string or integer");
                    return false;
                }
                InDependencies[ScopedCount] = AssetDependency{ScopedValue, 0, 0};
                ++ScopedCount;
            }
        }
    }
    *OutDependencyCount = ScopedCount;
    InMeta->valid = ScopedKind != AssetKind::Unknown && InMeta->sourcePath[0] != '\0';
    if (!InMeta->valid)
    {
        SetError(InError, "femeta: asset needs a type and a source path");
        return false;
    }
    return true;
}

bool WriteAssetMetaFile(const AssetMeta& InMeta, std::span<const AssetDependency> InDependencies, std::string* Out,
                        std::string* InError)
{
    if (Out == nullptr)
    {
        SetError(InError, "null output");
        return false;
    }
    if (!InMeta.valid || InMeta.sourcePath[0] == '\0')
    {
        SetError(InError, "cannot write an invalid femeta");
        return false;
    }
    *Out = std::format("guid = \"0x{:016X}\"\n", InMeta.guid);
    *Out += std::format("asset_id = \"0x{:016X}\"\n", InMeta.id.value);
    *Out += std::format("type = \"{}\"\n", AssetKindName(InMeta.kind));
    *Out += std::format("source = \"{}\"\n", InMeta.sourcePath);
    if (InMeta.cookedPath[0] != '\0')
    {
        *Out += std::format("cooked = \"{}\"\n", InMeta.cookedPath);
    }
    *Out += std::format("source_hash = \"0x{:016X}\"\n", InMeta.sourceHash);
    *Out += std::format("cooked_hash = \"0x{:016X}\"\n", InMeta.cookedHash);
    *Out += "\n[import]\n";
    *Out += std::format("format = \"{}\"\n",
                        CompressionFormatName(static_cast<TextureFormat>(InMeta.import.textureFormat)));
    *Out += std::format("max_size = {}\n", InMeta.import.maxSize);
    *Out += std::format("srgb = {}\n", InMeta.import.srgb ? "true" : "false");
    *Out += std::format("mips = {}\n", InMeta.import.generateMips ? "true" : "false");
    *Out += std::format("font_style = {}\n", InMeta.import.fontStyle);
    *Out += std::format("font_size = {}\n", InMeta.import.fontPixels);
    *Out += std::format("font_oversample = {}\n", InMeta.import.fontOversample);
    *Out += std::format("font_sdf = {}\n", InMeta.import.fontSdf ? "true" : "false");
    if (!InDependencies.empty())
    {
        *Out += "\n[dependencies]\nassets = [";
        for (std::size_t ScopedI = 0; ScopedI < InDependencies.size(); ++ScopedI)
        {
            *Out += std::format("{}\"0x{:016X}\"", ScopedI == 0 ? "" : ", ", InDependencies[ScopedI].guid);
        }
        *Out += "]\n";
    }
    return true;
}

bool IsAssetMetaPath(std::string_view InPath) noexcept
{
    return InPath.size() >= 7 && InPath.substr(InPath.size() - 7) == ".femeta";
}
}
