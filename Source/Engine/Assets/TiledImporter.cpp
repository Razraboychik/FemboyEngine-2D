#include "Engine/Assets/TiledImporter.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstring>
#include <cstdlib>

namespace Fby::Assets
{
namespace
{
void SetError(std::string* InError, std::string_view InMessage)
{
    if (InError != nullptr)
    {
        InError->assign(InMessage);
    }
}


[[nodiscard]] std::string_view Attribute(std::string_view InTag, std::string_view InName) noexcept
{
    const std::string ScopedNeedle = std::string(InName) + "=";
    std::size_t ScopedPosition = InTag.find(ScopedNeedle);
    while (ScopedPosition != std::string_view::npos)
    {
        if (ScopedPosition == 0 || std::isspace(static_cast<unsigned char>(InTag[ScopedPosition - 1])) != 0 ||
            InTag[ScopedPosition - 1] == '<')
        {
            break;
        }
        ScopedPosition = InTag.find(ScopedNeedle, ScopedPosition + ScopedNeedle.size());
    }
    if (ScopedPosition == std::string_view::npos)
    {
        return {};
    }
    std::size_t ScopedValue = ScopedPosition + ScopedNeedle.size();
    if (ScopedValue >= InTag.size() || (InTag[ScopedValue] != '"' && InTag[ScopedValue] != '\''))
    {
        return {};
    }
    const char ScopedQuote = InTag[ScopedValue++];
    const std::size_t ScopedEnd = InTag.find(ScopedQuote, ScopedValue);
    if (ScopedEnd == std::string_view::npos)
    {
        return {};
    }
    return InTag.substr(ScopedValue, ScopedEnd - ScopedValue);
}

[[nodiscard]] float AttributeFloat(std::string_view InTag, std::string_view InName, float InFallback = 0.0f) noexcept
{
    const std::string_view ScopedText = Attribute(InTag, InName);
    if (ScopedText.empty())
    {
        return InFallback;
    }
    const std::string ScopedValue(ScopedText);
    char* ScopedEnd = nullptr;
    const float ScopedResult = std::strtof(ScopedValue.c_str(), &ScopedEnd);
    return ScopedEnd != nullptr && *ScopedEnd == 0 ? ScopedResult : InFallback;
}

[[nodiscard]] bool ParseU32(std::string_view InText, std::uint32_t* OutValue) noexcept
{
    std::uint32_t ScopedValue = 0;
    const auto ScopedResult = std::from_chars(InText.data(), InText.data() + InText.size(), ScopedValue);
    if (ScopedResult.ec != std::errc{} || ScopedResult.ptr != InText.data() + InText.size())
    {
        return false;
    }
    *OutValue = ScopedValue;
    return true;
}

[[nodiscard]] bool ParseCsvLayer(std::string_view InData, std::uint32_t InExpected, std::vector<std::uint32_t>* OutGids,
                                 std::string* InError)
{
    OutGids->clear();
    OutGids->reserve(InExpected);
    std::size_t ScopedCursor = 0;
    while (ScopedCursor < InData.size())
    {
        while (ScopedCursor < InData.size() &&
               (std::isspace(static_cast<unsigned char>(InData[ScopedCursor])) != 0 || InData[ScopedCursor] == ','))
        {
            ++ScopedCursor;
        }
        if (ScopedCursor >= InData.size())
        {
            break;
        }
        const std::size_t ScopedStart = ScopedCursor;
        while (ScopedCursor < InData.size() && std::isdigit(static_cast<unsigned char>(InData[ScopedCursor])) != 0)
        {
            ++ScopedCursor;
        }
        std::uint32_t ScopedValue = 0;
        if (ScopedStart == ScopedCursor ||
            !ParseU32(InData.substr(ScopedStart, ScopedCursor - ScopedStart), &ScopedValue))
        {
            SetError(InError, "tiled layer contains a non-numeric tile id");
            return false;
        }
        OutGids->push_back(ScopedValue);
        if (OutGids->size() > InExpected)
        {
            SetError(InError, "tiled layer has more cells than the map dimensions");
            return false;
        }
    }
    if (OutGids->size() != InExpected)
    {
        SetError(InError, "tiled layer has fewer cells than the map dimensions");
        return false;
    }
    return true;
}

[[nodiscard]] int Base64Value(char InChar) noexcept
{
    if (InChar >= 'A' && InChar <= 'Z')
    {
        return InChar - 'A';
    }
    if (InChar >= 'a' && InChar <= 'z')
    {
        return InChar - 'a' + 26;
    }
    if (InChar >= '0' && InChar <= '9')
    {
        return InChar - '0' + 52;
    }
    if (InChar == '+')
    {
        return 62;
    }
    if (InChar == '/')
    {
        return 63;
    }
    return -1;
}

[[nodiscard]] bool ParseBase64Layer(std::string_view InData, std::uint32_t InExpected,
                                    std::vector<std::uint32_t>* OutGids, std::string* InError)
{
    std::vector<std::byte> ScopedBytes;
    std::uint32_t ScopedAccumulator = 0;
    std::uint32_t ScopedBits = 0;
    for (const char ScopedChar : InData)
    {
        if (std::isspace(static_cast<unsigned char>(ScopedChar)) != 0)
        {
            continue;
        }
        if (ScopedChar == '=')
        {
            break;
        }
        const int ScopedValue = Base64Value(ScopedChar);
        if (ScopedValue < 0)
        {
            SetError(InError, "tiled base64 layer contains an invalid character");
            return false;
        }
        ScopedAccumulator = (ScopedAccumulator << 6u) | static_cast<std::uint32_t>(ScopedValue);
        ScopedBits += 6;
        if (ScopedBits >= 8)
        {
            ScopedBits -= 8;
            ScopedBytes.push_back(static_cast<std::byte>((ScopedAccumulator >> ScopedBits) & 0xFFu));
        }
    }
    if (ScopedBytes.size() < static_cast<std::size_t>(InExpected) * 4u)
    {
        SetError(InError, "tiled base64 layer is shorter than the map dimensions");
        return false;
    }
    OutGids->clear();
    OutGids->reserve(InExpected);
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < InExpected; ++ScopedIndex)
    {
        const std::size_t ScopedOffset = static_cast<std::size_t>(ScopedIndex) * 4u;
        const std::uint32_t ScopedGid = static_cast<std::uint32_t>(ScopedBytes[ScopedOffset]) |
                                       (static_cast<std::uint32_t>(ScopedBytes[ScopedOffset + 1]) << 8u) |
                                       (static_cast<std::uint32_t>(ScopedBytes[ScopedOffset + 2]) << 16u) |
                                       (static_cast<std::uint32_t>(ScopedBytes[ScopedOffset + 3]) << 24u);
        OutGids->push_back(ScopedGid);
    }
    return true;
}

[[nodiscard]] std::string_view DataSection(std::string_view InLayer) noexcept
{
    const std::size_t ScopedStart = InLayer.find("<data");
    if (ScopedStart == std::string_view::npos)
    {
        return {};
    }
    const std::size_t ScopedOpen = InLayer.find('>', ScopedStart);
    const std::size_t ScopedClose = InLayer.find("</data>", ScopedOpen);
    if (ScopedOpen == std::string_view::npos || ScopedClose == std::string_view::npos)
    {
        return {};
    }
    return InLayer.substr(ScopedOpen + 1, ScopedClose - ScopedOpen - 1);
}
}

bool ParseTiledMap(std::string_view InXml, TiledMap* OutMap, std::string* InError)
{
    if (OutMap == nullptr || InXml.empty())
    {
        SetError(InError, "empty TMX document");
        return false;
    }
    const std::size_t ScopedMapStart = InXml.find("<map");
    const std::size_t ScopedMapEnd = InXml.find('>', ScopedMapStart);
    if (ScopedMapStart == std::string_view::npos || ScopedMapEnd == std::string_view::npos)
    {
        SetError(InError, "TMX has no map element");
        return false;
    }
    const std::string_view ScopedMapTag = InXml.substr(ScopedMapStart, ScopedMapEnd - ScopedMapStart + 1);
    TiledMap ScopedMap;
    if (!ParseU32(Attribute(ScopedMapTag, "width"), &ScopedMap.Width) ||
        !ParseU32(Attribute(ScopedMapTag, "height"), &ScopedMap.Height) ||
        !ParseU32(Attribute(ScopedMapTag, "tilewidth"), &ScopedMap.TileWidth) ||
        !ParseU32(Attribute(ScopedMapTag, "tileheight"), &ScopedMap.TileHeight) || ScopedMap.Width == 0 ||
        ScopedMap.Height == 0 || ScopedMap.TileWidth == 0 || ScopedMap.TileHeight == 0 ||
        ScopedMap.Width > 8192 || ScopedMap.Height > 8192 ||
        ScopedMap.Width > 0xFFFFFFFFu / ScopedMap.Height)
    {
        SetError(InError, "TMX map dimensions are missing or invalid");
        return false;
    }
    if (const std::string_view ScopedOrientation = Attribute(ScopedMapTag, "orientation"); !ScopedOrientation.empty())
    {
        ScopedMap.Orientation = std::string(ScopedOrientation);
    }
    if (ScopedMap.Orientation != "orthogonal" && ScopedMap.Orientation != "isometric" &&
        ScopedMap.Orientation != "hexagonal")
    {
        SetError(InError, "unsupported Tiled orientation");
        return false;
    }

    std::size_t ScopedCursor = ScopedMapEnd;
    while (true)
    {
        const std::size_t ScopedTileset = InXml.find("<tileset", ScopedCursor);
        const std::size_t ScopedLayer = InXml.find("<layer", ScopedCursor);
        if (ScopedTileset == std::string_view::npos && ScopedLayer == std::string_view::npos)
        {
            break;
        }
        if (ScopedTileset != std::string_view::npos &&
            (ScopedLayer == std::string_view::npos || ScopedTileset < ScopedLayer))
        {
            const std::size_t ScopedEnd = InXml.find('>', ScopedTileset);
            if (ScopedEnd == std::string_view::npos)
            {
                break;
            }
            const std::string_view ScopedTag = InXml.substr(ScopedTileset, ScopedEnd - ScopedTileset + 1);
            TiledTileset ScopedTilesetValue;
            (void)ParseU32(Attribute(ScopedTag, "firstgid"), &ScopedTilesetValue.FirstGid);
            if (const std::string_view ScopedSource = Attribute(ScopedTag, "source"); !ScopedSource.empty())
            {
                ScopedTilesetValue.Source = std::string(ScopedSource);
            }
            (void)ParseU32(Attribute(ScopedTag, "columns"), &ScopedTilesetValue.Columns);
            (void)ParseU32(Attribute(ScopedTag, "tilecount"), &ScopedTilesetValue.TileCount);
            ScopedMap.Tilesets.push_back(std::move(ScopedTilesetValue));
            ScopedCursor = ScopedEnd + 1;
            continue;
        }

        const std::size_t ScopedEnd = InXml.find("</layer>", ScopedLayer);
        if (ScopedEnd == std::string_view::npos)
        {
            SetError(InError, "TMX layer is not closed");
            return false;
        }
        const std::string_view ScopedBlock = InXml.substr(ScopedLayer, ScopedEnd - ScopedLayer + 8);
        const std::size_t ScopedTagEnd = ScopedBlock.find('>');
        if (ScopedTagEnd == std::string_view::npos)
        {
            SetError(InError, "TMX layer has no open tag");
            return false;
        }
        const std::string_view ScopedTag = ScopedBlock.substr(0, ScopedTagEnd + 1);
        TiledLayer ScopedLayerValue;
        ScopedLayerValue.Name = std::string(Attribute(ScopedTag, "name"));
        if (Attribute(ScopedTag, "visible") == "0")
        {
            ScopedLayerValue.Visible = false;
        }
        const std::string_view ScopedData = DataSection(ScopedBlock);
        if (ScopedData.empty())
        {
            SetError(InError, "TMX layer has no tile data");
            return false;
        }
        const std::size_t ScopedDataTagStart = ScopedBlock.find("<data");
        const std::size_t ScopedDataTagEnd =
            ScopedDataTagStart == std::string_view::npos ? std::string_view::npos : ScopedBlock.find('>', ScopedDataTagStart);
        const std::string_view ScopedDataTag =
            ScopedDataTagStart == std::string_view::npos || ScopedDataTagEnd == std::string_view::npos
                ? std::string_view{}
                : ScopedBlock.substr(ScopedDataTagStart, ScopedDataTagEnd - ScopedDataTagStart + 1);
        if (Attribute(ScopedDataTag, "encoding") == "base64")
        {
            if (!Attribute(ScopedDataTag, "compression").empty())
            {
                SetError(InError, "compressed Tiled layers are not supported");
                return false;
            }
            if (!ParseBase64Layer(ScopedData, ScopedMap.Width * ScopedMap.Height, &ScopedLayerValue.Gids, InError))
            {
                return false;
            }
        }
        else if (!ParseCsvLayer(ScopedData, ScopedMap.Width * ScopedMap.Height, &ScopedLayerValue.Gids, InError))
        {
            return false;
        }
        ScopedMap.Layers.push_back(std::move(ScopedLayerValue));
        ScopedCursor = ScopedEnd + 8;
    }

    if (ScopedMap.Layers.empty())
    {
        SetError(InError, "TMX contains no tile layers");
        return false;
    }
    std::size_t ScopedObjectCursor = ScopedMapEnd;
    while (true)
    {
        const std::size_t ScopedGroup = InXml.find("<objectgroup", ScopedObjectCursor);
        if (ScopedGroup == std::string_view::npos)
        {
            break;
        }
        const std::size_t ScopedGroupEnd = InXml.find("</objectgroup>", ScopedGroup);
        if (ScopedGroupEnd == std::string_view::npos)
        {
            SetError(InError, "TMX object group is not closed");
            return false;
        }
        const std::string_view ScopedBlock = InXml.substr(ScopedGroup, ScopedGroupEnd - ScopedGroup);
        const std::size_t ScopedGroupTagEnd = ScopedBlock.find('>');
        std::size_t ScopedObject = ScopedGroupTagEnd == std::string_view::npos ? 0 : ScopedGroupTagEnd + 1;
        while ((ScopedObject = ScopedBlock.find("<object", ScopedObject)) != std::string_view::npos)
        {
            const std::size_t ScopedEnd = ScopedBlock.find('>', ScopedObject);
            if (ScopedEnd == std::string_view::npos)
            {
                SetError(InError, "TMX object tag is not closed");
                return false;
            }
            const std::string_view ScopedTag = ScopedBlock.substr(ScopedObject, ScopedEnd - ScopedObject + 1);
            TiledObject ScopedValue;
            ScopedValue.Name = std::string(Attribute(ScopedTag, "name"));
            ScopedValue.Type = std::string(Attribute(ScopedTag, "type"));
            ScopedValue.X = AttributeFloat(ScopedTag, "x");
            ScopedValue.Y = AttributeFloat(ScopedTag, "y");
            ScopedValue.Width = AttributeFloat(ScopedTag, "width");
            ScopedValue.Height = AttributeFloat(ScopedTag, "height");
            ScopedValue.Point = Attribute(ScopedTag, "point") == "1";
            if (ScopedValue.Name.empty() && ScopedValue.Type.empty() && ScopedValue.Width == 0.0f &&
                ScopedValue.Height == 0.0f && !ScopedValue.Point)
            {
                ScopedObject = ScopedEnd + 1;
                continue;
            }
            ScopedMap.Objects.push_back(std::move(ScopedValue));
            ScopedObject = ScopedEnd + 1;
            if (ScopedMap.Objects.size() > 65536)
            {
                SetError(InError, "TMX contains too many objects");
                return false;
            }
        }
        ScopedObjectCursor = ScopedGroupEnd + 13;
    }
    *OutMap = std::move(ScopedMap);
    return true;
}

bool TiledMapToTilemap(const TiledMap& InMap, std::uint32_t InLayerIndex, TilemapComponent* OutTilemap,
                       std::string* InError)
{
    if (OutTilemap == nullptr || InLayerIndex >= InMap.Layers.size())
    {
        SetError(InError, "Tiled layer index is out of range");
        return false;
    }
    if (InMap.Width > kTilemapMaxWidth || InMap.Height > kTilemapMaxHeight)
    {
        SetError(InError, "Tiled map exceeds the runtime tilemap capacity");
        return false;
    }
    if (InMap.Tilesets.empty())
    {
        SetError(InError, "Tiled map has no tileset");
        return false;
    }
    TilemapComponent ScopedTilemap;
    Tilemap::Resize(ScopedTilemap, InMap.Width, InMap.Height);
    ScopedTilemap.Grid.CellSize = {static_cast<float>(InMap.TileWidth), static_cast<float>(InMap.TileHeight)};
    if (InMap.Orientation == "isometric")
    {
        ScopedTilemap.Grid.Mode = Grid::ProjectionMode::IsoRect;
    }
    else if (InMap.Orientation == "hexagonal")
    {
        ScopedTilemap.Grid.Mode = Grid::ProjectionMode::IsoHex;
    }
    else
    {
        ScopedTilemap.Grid.Mode = Grid::ProjectionMode::TopDown;
    }
    const TiledLayer& ScopedLayer = InMap.Layers[InLayerIndex];
    for (std::uint32_t ScopedY = 0; ScopedY < InMap.Height; ++ScopedY)
    {
        for (std::uint32_t ScopedX = 0; ScopedX < InMap.Width; ++ScopedX)
        {
            const std::uint32_t ScopedGid = ScopedLayer.Gids[ScopedY * InMap.Width + ScopedX];
            if (ScopedGid == 0)
            {
                continue;
            }
            const auto ScopedTileset = std::find_if(InMap.Tilesets.rbegin(), InMap.Tilesets.rend(),
                                                    [ScopedGid](const TiledTileset& InEntry)
                                                    { return InEntry.FirstGid <= ScopedGid; });
            if (ScopedTileset == InMap.Tilesets.rend())
            {
                SetError(InError, "tile gid does not belong to a tileset");
                return false;
            }
            const std::uint32_t ScopedLocal = ScopedGid - ScopedTileset->FirstGid + 1u;
            if (ScopedLocal == 0 || ScopedLocal > 0xFFFFu)
            {
                SetError(InError, "tile gid cannot be represented in the runtime tile word");
                return false;
            }
            (void)Tilemap::Set(ScopedTilemap, {static_cast<std::int32_t>(ScopedX), static_cast<std::int32_t>(ScopedY)},
                               MakeTile(static_cast<std::uint16_t>(ScopedLocal)));
        }
    }
    *OutTilemap = ScopedTilemap;
    return true;
}
}
