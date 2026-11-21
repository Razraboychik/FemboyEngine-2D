// FemboyEngine2D - Tiled TMX import for tilemap and runtime tile layers.
#pragma once

#include "Engine/ECS/Tilemap.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Fby::Assets
{
struct TiledTileset
{
    std::uint32_t FirstGid = 1;
    std::string Source;
    std::uint32_t Columns = 0;
    std::uint32_t TileCount = 0;
};

struct TiledLayer
{
    std::string Name;
    std::vector<std::uint32_t> Gids;
    bool Visible = true;
};

struct TiledObject
{
    std::string Name;
    std::string Type;
    float X = 0.0f;
    float Y = 0.0f;
    float Width = 0.0f;
    float Height = 0.0f;
    bool Point = false;
};

struct TiledMap
{
    std::uint32_t Width = 0;
    std::uint32_t Height = 0;
    std::uint32_t TileWidth = 0;
    std::uint32_t TileHeight = 0;
    std::string Orientation = "orthogonal";
    std::vector<TiledTileset> Tilesets;
    std::vector<TiledLayer> Layers;
    std::vector<TiledObject> Objects;
};

// Reads the subset of TMX used by the engine: orthogonal, isometric and
// hexagonal maps with CSV or XML tile data. Base64/compressed layers are
// rejected instead of being guessed.
[[nodiscard]] bool ParseTiledMap(std::string_view InXml, TiledMap* OutMap, std::string* InError = nullptr);

// Converts one visible layer to the fixed-capacity runtime component. Maps
// larger than 64x64 must be streamed by the caller.
[[nodiscard]] bool TiledMapToTilemap(const TiledMap& InMap, std::uint32_t InLayerIndex, TilemapComponent* OutTilemap,
                                     std::string* InError = nullptr);
}
