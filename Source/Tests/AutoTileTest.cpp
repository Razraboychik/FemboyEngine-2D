#include "Engine/ECS/Tilemap.hpp"

#include <cstdio>
#include <cstdint>

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
}

int main()
{
    Fby::TilemapComponent ScopedMap;
    Fby::Tilemap::Resize(ScopedMap, 5, 5);
    for (std::int32_t ScopedY = 0; ScopedY < 5; ++ScopedY)
    {
        for (std::int32_t ScopedX = 0; ScopedX < 5; ++ScopedX)
        {
            (void)Fby::Tilemap::Set(ScopedMap, {ScopedX, ScopedY}, Fby::MakeTile(1));
        }
    }
    Check(Fby::Tilemap::AutoTileMask(ScopedMap, {2, 2}) == 15u, "center has four neighbours");
    Check(Fby::Tilemap::AutoTileMask(ScopedMap, {0, 0}) == 6u, "corner sees its in-map east and south neighbours");

    Fby::Tilemap::AutoTileRule ScopedRule;
    for (std::uint32_t ScopedMask = 0; ScopedMask < 16; ++ScopedMask)
    {
        ScopedRule.Tiles[ScopedMask] = static_cast<std::uint16_t>(ScopedMask + 1u);
    }
    const std::uint32_t ScopedChanged = Fby::Tilemap::AutoTileAll(ScopedMap, ScopedRule);
    Check(ScopedChanged == 25u, "autotile rewrites every non-empty cell");
    Check(Fby::Tilemap::At(ScopedMap, {2, 2}) == Fby::MakeTile(16), "center maps to mask 15 tile");
    Check(Fby::Tilemap::At(ScopedMap, {0, 0}) == Fby::MakeTile(7), "corner maps to its mask tile");

    const std::uint32_t ScopedFlags = Fby::kTileFlagSolid | Fby::kTileFlagOccluder;
    (void)Fby::Tilemap::Set(ScopedMap, {1, 1}, Fby::MakeTile(1, ScopedFlags, 3));
    Check(Fby::Tilemap::AutoTileCell(ScopedMap, {1, 1}, ScopedRule), "single cell autotiles");
    const std::uint32_t ScopedAfter = Fby::Tilemap::At(ScopedMap, {1, 1});
    Check(Fby::TileFlagsOf(ScopedAfter) == ScopedFlags && Fby::TileHeightOf(ScopedAfter) == 3,
          "autotile preserves flags and height");

    Fby::TilemapComponent ScopedHex;
    Fby::Tilemap::Resize(ScopedHex, 3, 3);
    for (std::int32_t ScopedY = 0; ScopedY < 3; ++ScopedY)
    {
        for (std::int32_t ScopedX = 0; ScopedX < 3; ++ScopedX)
        {
            (void)Fby::Tilemap::Set(ScopedHex, {ScopedX, ScopedY}, Fby::MakeTile(1));
        }
    }
    Check(Fby::Tilemap::HexAutoTileMask(ScopedHex, {1, 1}) == 63u, "hex centre sees six neighbours");
    Fby::Tilemap::HexAutoTileRule ScopedHexRule;
    ScopedHexRule.Tiles.fill(64);
    Check(Fby::Tilemap::HexAutoTileAll(ScopedHex, ScopedHexRule) == 9u, "hex autotile rewrites all cells");
    Check(Fby::Tilemap::At(ScopedHex, {1, 1}) == Fby::MakeTile(64), "hex centre maps to the full mask");

    std::uint32_t ScopedStress = 0;
    for (std::uint32_t ScopedIteration = 0; ScopedIteration < 1000; ++ScopedIteration)
    {
        Fby::Tilemap::Clear(ScopedMap);
        Fby::Tilemap::Resize(ScopedMap, 64, 64);
        for (std::int32_t ScopedY = 0; ScopedY < 64; ++ScopedY)
        {
            for (std::int32_t ScopedX = 0; ScopedX < 64; ++ScopedX)
            {
                (void)Fby::Tilemap::Set(ScopedMap, {ScopedX, ScopedY}, Fby::MakeTile(1));
            }
        }
        ScopedStress += Fby::Tilemap::AutoTileAll(ScopedMap, ScopedRule);
    }
    Check(ScopedStress == 1000u * 64u * 64u, "autotile stress rewrites every cell");

    if (GFailures == 0)
    {
        std::printf("autotile_test: all checks passed\n");
        return 0;
    }
    return 1;
}
