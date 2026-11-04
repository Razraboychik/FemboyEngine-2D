// FemboyEngine2D - static bake tile cache tests.
#include "Engine/RHI/StaticBake.hpp"

#include <cstdio>

namespace
{
int GFailures = 0;

#define FBY_CHECK(condition)                                                                                           \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #condition);                                  \
            ++GFailures;                                                                                              \
        }                                                                                                              \
    } while (0)
}

int main()
{
    Fby::Rhi::StaticBakeSettings ScopedSettings;
    ScopedSettings.tileSize = 256;
    // Two 256x256 RGBA tiles.
    ScopedSettings.budgetBytes = 2u * 256u * 256u * 4u;
    ScopedSettings.maxBakesPerFrame = 2;

    Fby::Rhi::StaticBakeCache ScopedCache;
    ScopedCache.Configure(ScopedSettings);
    ScopedCache.BeginFrame();
    ScopedCache.SetZoom(1.0f);
    FBY_CHECK(ScopedCache.ZoomBucket() == Fby::Rhi::ZoomBucketFor(1.0f));

    Fby::Rhi::StaticBakeTileKey ScopedKey{};
    FBY_CHECK(!ScopedCache.Acquire(0, 0, 0, &ScopedKey));
    ScopedCache.Commit(ScopedKey, 64u * 1024u);
    FBY_CHECK(ScopedCache.Acquire(0, 0, 0) == true);

    Fby::Rhi::StaticBakeTileKey ScopedSecond{};
    FBY_CHECK(!ScopedCache.Acquire(0, 1, 0, &ScopedSecond));
    ScopedCache.Commit(ScopedSecond, 256u * 256u * 4u);
    FBY_CHECK(ScopedCache.Acquire(0, 1, 0) == true);

    // Budget is two tiles. The next two commits force LRU eviction.
    ScopedCache.BeginFrame();
    Fby::Rhi::StaticBakeTileKey ScopedThird{};
    FBY_CHECK(!ScopedCache.Acquire(0, 2, 0, &ScopedThird));
    ScopedCache.Commit(ScopedThird, 256u * 256u * 4u);
    Fby::Rhi::StaticBakeTileKey ScopedFourth{};
    FBY_CHECK(!ScopedCache.Acquire(0, 3, 0, &ScopedFourth));
    ScopedCache.Commit(ScopedFourth, 256u * 256u * 4u);
    FBY_CHECK(ScopedCache.Stats().evictions > 0);

    ScopedCache.InvalidateLayer(0);
    ScopedCache.BeginFrame();
    FBY_CHECK(ScopedCache.Stats().tiles == 0);

    // Zoom buckets separate caches.
    ScopedCache.SetZoom(4.0f);
    FBY_CHECK(ScopedCache.ZoomBucket() != Fby::Rhi::ZoomBucketFor(1.0f));

    ScopedCache.SetLightVersion(1);
    FBY_CHECK(ScopedCache.Acquire(0, 0, 0) == false);
    ScopedCache.Commit(Fby::Rhi::StaticBakeTileKey{0, 0, 0, ScopedCache.ZoomBucket(), 1}, 64u * 1024u);
    ScopedCache.SetLightVersion(2);
    FBY_CHECK(ScopedCache.Acquire(0, 0, 0) == false);

    if (GFailures != 0)
    {
        std::fprintf(stderr, "static_bake_test: %d failure(s)\n", GFailures);
        return 1;
    }
    std::printf("static_bake_test: OK\n");
    return 0;
}
