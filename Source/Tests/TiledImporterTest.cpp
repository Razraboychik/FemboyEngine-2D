#include "Engine/Assets/TiledImporter.hpp"

#include <cstdio>
#include <string>

namespace
{
std::uint32_t GFailures = 0;

void Check(bool InCondition, const char* InMessage)
{
    if (!InCondition)
    {
        std::fprintf(stderr, "FAIL: %s\n", InMessage);
        ++GFailures;
    }
}

constexpr const char* kOrthogonalMap = R"(<?xml version="1.0"?>
<map version="1.10" orientation="orthogonal" width="2" height="2" tilewidth="16" tileheight="16">
 <tileset firstgid="1" source="tiles.tsx"/>
 <layer name="ground">
  <data encoding="csv">1,2,
0,3</data>
 </layer>
</map>)";
}

int main()
{
    Fby::Assets::TiledMap ScopedMap;
    std::string ScopedError;
    const std::string ScopedObjects = R"(<map orientation="orthogonal" width="1" height="1" tilewidth="8" tileheight="8">
<tileset firstgid="1"/><layer name="x"><data encoding="csv">1</data></layer>
<objectgroup name="spawns"><object name="player" type="spawn" x="10" y="20" width="16" height="16"/>
<object name="exit" point="1" x="2" y="3"/></objectgroup></map>)";
    Fby::Assets::TiledMap ScopedObjectMap;
    Check(Fby::Assets::ParseTiledMap(ScopedObjects, &ScopedObjectMap, &ScopedError), "object group parses");
    Check(ScopedObjectMap.Objects.size() == 2, "object count");
    Check(ScopedObjectMap.Objects[0].Name == "player", "object name");
    Check(ScopedObjectMap.Objects[0].Type == "spawn", "object type");
    Check(ScopedObjectMap.Objects[1].Point, "object point");

    Check(Fby::Assets::ParseTiledMap(kOrthogonalMap, &ScopedMap, &ScopedError), "orthogonal TMX parses");
    Check(ScopedMap.Width == 2 && ScopedMap.Height == 2 && ScopedMap.TileWidth == 16, "map dimensions");
    Check(ScopedMap.Tilesets.size() == 1 && ScopedMap.Tilesets[0].FirstGid == 1, "tileset parsed");
    Check(ScopedMap.Layers.size() == 1 && ScopedMap.Layers[0].Gids.size() == 4, "layer cells parsed");

    Fby::TilemapComponent ScopedTilemap;
    Check(Fby::Assets::TiledMapToTilemap(ScopedMap, 0, &ScopedTilemap, &ScopedError), "TMX converts to tilemap");
    Check(Fby::Tilemap::At(ScopedTilemap, {0, 0}) == Fby::MakeTile(1), "first tile");
    Check(Fby::Tilemap::At(ScopedTilemap, {1, 0}) == Fby::MakeTile(2), "second tile");
    Check(Fby::Tilemap::At(ScopedTilemap, {0, 1}) == 0, "empty tile stays empty");
    Check(ScopedTilemap.Grid.Mode == Fby::Grid::ProjectionMode::TopDown, "orthogonal projection");

    const std::string ScopedIso = R"(<map orientation="isometric" width="2" height="1" tilewidth="32" tileheight="16">
<tileset firstgid="1" source="tiles.tsx"/>
<layer name="iso"><data encoding="csv">1,1</data></layer></map>)";
    Check(Fby::Assets::ParseTiledMap(ScopedIso, &ScopedMap, &ScopedError), "isometric TMX parses");
    Check(Fby::Assets::TiledMapToTilemap(ScopedMap, 0, &ScopedTilemap, &ScopedError) &&
              ScopedTilemap.Grid.Mode == Fby::Grid::ProjectionMode::IsoRect,
          "isometric projection");

    const std::string ScopedHex = R"(<map orientation="hexagonal" width="1" height="1" tilewidth="32" tileheight="32">
<tileset firstgid="1" source="tiles.tsx"/><layer name="h"><data encoding="csv">1</data></layer></map>)";
    Check(Fby::Assets::ParseTiledMap(ScopedHex, &ScopedMap, &ScopedError) &&
              Fby::Assets::TiledMapToTilemap(ScopedMap, 0, &ScopedTilemap, &ScopedError) &&
              ScopedTilemap.Grid.Mode == Fby::Grid::ProjectionMode::IsoHex,
          "hex projection");

    const std::string ScopedReordered = R"(<map orientation="orthogonal" tilewidth="8" tileheight="8" width="2" height="2">
<tileset firstgid="1"/><layer name="x"><data encoding="csv">1,1,1,1</data></layer></map>)";
    Check(Fby::Assets::ParseTiledMap(ScopedReordered, &ScopedMap, &ScopedError) && ScopedMap.Width == 2 &&
              ScopedMap.Height == 2 && ScopedMap.TileWidth == 8,
          "tilewidth/tileheight do not mask width/height");

    Check(!Fby::Assets::ParseTiledMap("<map width=\"2\" height=\"2\"/>", &ScopedMap, &ScopedError),
          "missing dimensions rejected");
    Check(!Fby::Assets::ParseTiledMap("<map orientation=\"orthogonal\" width=\"99999\" height=\"2\" tilewidth=\"8\" tileheight=\"8\"/>",
                                      &ScopedMap, &ScopedError),
          "oversized TMX dimensions rejected");
    const std::string ScopedBase64 = R"(<map orientation="orthogonal" width="2" height="2" tilewidth="8" tileheight="8">
<tileset firstgid="1"/><layer name="x"><data encoding="base64">AQAAAAIAAAAAAAAAAwAAAA==</data></layer></map>)";
    Check(Fby::Assets::ParseTiledMap(ScopedBase64, &ScopedMap, &ScopedError), "base64 layer parses");
    Check(ScopedMap.Layers.size() == 1 && ScopedMap.Layers[0].Gids.size() == 4 &&
              ScopedMap.Layers[0].Gids[1] == 2 && ScopedMap.Layers[0].Gids[3] == 3,
          "base64 gids decode little-endian");
    Check(!Fby::Assets::ParseTiledMap(R"(<map orientation="orthogonal" width="2" height="2" tilewidth="8" tileheight="8">
<tileset firstgid="1"/><layer name="x"><data encoding="base64" compression="zlib">AAAA</data></layer></map>)",
                                      &ScopedMap, &ScopedError),
          "compressed base64 rejected");
    Check(!Fby::Assets::ParseTiledMap(R"(<map orientation="orthogonal" width="2" height="2" tilewidth="8" tileheight="8">
<tileset firstgid="1"/><layer name="x"><data encoding="csv">1,2,3</data></layer></map>)",
                                      &ScopedMap, &ScopedError),
          "short CSV rejected");

    std::uint32_t ScopedParsed = 0;
    for (std::uint32_t ScopedIteration = 0; ScopedIteration < 5000; ++ScopedIteration)
    {
        if (Fby::Assets::ParseTiledMap(kOrthogonalMap, &ScopedMap, &ScopedError))
        {
            ++ScopedParsed;
        }
    }
    Check(ScopedParsed == 5000, "TMX parse stress");

    if (GFailures == 0)
    {
        std::printf("tiled_importer_test: all checks passed\n");
        return 0;
    }
    return 1;
}
