// FemboyEngine2D - deterministic sprite atlas packing and UV lookup.
#include "Engine/Assets/SpriteAtlas.hpp"

#include <array>
#include <cstdio>
#include <cstring>

namespace
{
std::uint32_t GFailures = 0;

#define FBY_CHECK(condition)                                                                                           \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);                                  \
            ++GFailures;                                                                                               \
        }                                                                                                              \
    } while (false)

bool Overlaps(const Fby::Assets::AtlasSprite& InLeft, const Fby::Assets::AtlasSprite& InRight)
{
    return InLeft.X < InRight.X + InRight.Width && InRight.X < InLeft.X + InLeft.Width &&
           InLeft.Y < InRight.Y + InRight.Height && InRight.Y < InLeft.Y + InLeft.Height;
}
}

int main()
{
    const std::array<Fby::Assets::AtlasSpriteSource, 5> ScopedSources{{
        {7, 32, 32, 2},
        {3, 64, 16, 2},
        {9, 16, 48, 1},
        {1, 32, 16, 0},
        {5, 8, 8, 4},
    }};

    Fby::Assets::SpriteAtlas ScopedAtlas{};
    FBY_CHECK(Fby::Assets::PackSpriteAtlas(ScopedSources, 128, 128, &ScopedAtlas) == Fby::Assets::AtlasPackError::None);
    FBY_CHECK(ScopedAtlas.SpriteCount == ScopedSources.size());
    FBY_CHECK(ScopedAtlas.PageWidth == 128 && ScopedAtlas.PageHeight == 128);

    for (std::uint32_t ScopedI = 0; ScopedI < ScopedAtlas.SpriteCount; ++ScopedI)
    {
        const Fby::Assets::AtlasSprite& ScopedSprite = ScopedAtlas.Sprites[ScopedI];
        FBY_CHECK(ScopedSprite.X + ScopedSprite.Width <= ScopedAtlas.PageWidth);
        FBY_CHECK(ScopedSprite.Y + ScopedSprite.Height <= ScopedAtlas.PageHeight);
        for (std::uint32_t ScopedJ = ScopedI + 1; ScopedJ < ScopedAtlas.SpriteCount; ++ScopedJ)
        {
            FBY_CHECK(!Overlaps(ScopedSprite, ScopedAtlas.Sprites[ScopedJ]));
        }
    }

    const Fby::Assets::AtlasSprite* ScopedSeven = Fby::Assets::FindAtlasSprite(ScopedAtlas, 7);
    FBY_CHECK(ScopedSeven != nullptr && ScopedSeven->Width == 32 && ScopedSeven->Height == 32);
    FBY_CHECK(Fby::Assets::FindAtlasSprite(ScopedAtlas, 42) == nullptr);

    Fby::Assets::AtlasUv ScopedUv{};
    FBY_CHECK(Fby::Assets::AtlasSpriteUv(ScopedAtlas, 7, &ScopedUv));
    FBY_CHECK(ScopedUv.Min.X >= 0.0f && ScopedUv.Max.X <= 1.0f);
    FBY_CHECK(ScopedUv.Min.Y >= 0.0f && ScopedUv.Max.Y <= 1.0f);
    FBY_CHECK(ScopedUv.Max.X > ScopedUv.Min.X && ScopedUv.Max.Y > ScopedUv.Min.Y);
    FBY_CHECK(!Fby::Assets::AtlasSpriteUv(ScopedAtlas, 42, &ScopedUv));

    // The pack is reproducible byte for byte for the same input.
    Fby::Assets::SpriteAtlas ScopedRepeat{};
    FBY_CHECK(Fby::Assets::PackSpriteAtlas(ScopedSources, 128, 128, &ScopedRepeat) == Fby::Assets::AtlasPackError::None);
    for (std::uint32_t ScopedI = 0; ScopedI < ScopedAtlas.SpriteCount; ++ScopedI)
    {
        FBY_CHECK(ScopedAtlas.Sprites[ScopedI].X == ScopedRepeat.Sprites[ScopedI].X);
        FBY_CHECK(ScopedAtlas.Sprites[ScopedI].Y == ScopedRepeat.Sprites[ScopedI].Y);
    }

    // A sprite larger than the page and a page that cannot hold the rows fail.
    const std::array<Fby::Assets::AtlasSpriteSource, 1> ScopedHuge{{{1, 256, 16, 0}}};
    FBY_CHECK(Fby::Assets::PackSpriteAtlas(ScopedHuge, 128, 128, &ScopedRepeat) ==
              Fby::Assets::AtlasPackError::DoesNotFit);

    const std::array<Fby::Assets::AtlasSpriteSource, 4> ScopedRows{{{1, 100, 100, 0},
                                                                    {2, 100, 100, 0},
                                                                    {3, 100, 100, 0},
                                                                    {4, 100, 100, 0}}};
    FBY_CHECK(Fby::Assets::PackSpriteAtlas(ScopedRows, 128, 128, &ScopedRepeat) ==
              Fby::Assets::AtlasPackError::DoesNotFit);

    const std::array<Fby::Assets::AtlasSpriteSource, 1> ScopedInvalid{{{0, 16, 16, 0}}};
    FBY_CHECK(Fby::Assets::PackSpriteAtlas(ScopedInvalid, 128, 128, &ScopedRepeat) ==
              Fby::Assets::AtlasPackError::InvalidSprite);
    FBY_CHECK(Fby::Assets::PackSpriteAtlas(ScopedSources, 0, 128, &ScopedRepeat) ==
              Fby::Assets::AtlasPackError::PageTooLarge);

    const std::string_view ScopedAtlasText =
        "FBYATLAS 1\n"
        "page 128 128 page.png\n"
        "sprite 7 2 2 32 32 0.5 0.5\n"
        "sprite 9 40 2 16 16 0.25 0.75\n";
    Fby::Assets::SpriteAtlasAsset ScopedAsset{};
    std::string ScopedParseError;
    FBY_CHECK(Fby::Assets::LoadSpriteAtlasAsset(ScopedAtlasText, &ScopedAsset, &ScopedParseError));
    FBY_CHECK(ScopedAsset.Valid);
    FBY_CHECK(ScopedAsset.Atlas.SpriteCount == 2);
    FBY_CHECK(std::strcmp(ScopedAsset.PagePath.data(), "page.png") == 0);
    FBY_CHECK(Fby::Assets::FindAtlasSprite(ScopedAsset.Atlas, 9) != nullptr);
    FBY_CHECK(Fby::Assets::FindAtlasSprite(ScopedAsset.Atlas, 9)->PivotY == 0.75f);

    const std::string_view ScopedDuplicate =
        "FBYATLAS 1\npage 128 128 p.png\nsprite 1 0 0 8 8 0.5 0.5\nsprite 1 8 0 8 8 0.5 0.5\n";
    FBY_CHECK(!Fby::Assets::LoadSpriteAtlasAsset(ScopedDuplicate, &ScopedAsset, &ScopedParseError));
    const std::string_view ScopedOutside =
        "FBYATLAS 1\npage 128 128 p.png\nsprite 1 120 120 16 16 0.5 0.5\n";
    FBY_CHECK(!Fby::Assets::LoadSpriteAtlasAsset(ScopedOutside, &ScopedAsset, &ScopedParseError));
    FBY_CHECK(!Fby::Assets::LoadSpriteAtlasAsset("sprite 1 0 0 8 8 0.5 0.5\n", &ScopedAsset, &ScopedParseError));

    if (GFailures == 0)
    {
        std::printf("sprite_atlas: OK (determinism, overlap, UV, overflow)\n");
        return 0;
    }
    std::fprintf(stderr, "sprite_atlas: %u failure(s)\n", GFailures);
    return 1;
}
