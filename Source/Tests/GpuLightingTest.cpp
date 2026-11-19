// FemboyEngine2D - GPU light and cluster layout tests.
#include "Engine/RHI/GpuLighting.hpp"

#include <cstddef>
#include <cstdio>

int main()
{
    using namespace Fby::Rhi;

    static_assert(sizeof(GpuLight) == 64);
    static_assert(offsetof(GpuLight, PositionAndRadius) == 0);
    static_assert(offsetof(GpuLight, ColorAndIntensity) == 16);
    static_assert(offsetof(GpuLight, DirectionAndCone) == 32);
    static_assert(offsetof(GpuLight, Type) == 48);
    static_assert(offsetof(GpuLight, Flags) == 52);
    static_assert(offsetof(GpuLight, ShadowIndex) == 56);
    static_assert(offsetof(GpuLight, Reserved) == 60);
    static_assert(sizeof(ClusterHeader) == 16);
    static_assert(offsetof(ClusterHeader, Offset) == 0);
    static_assert(offsetof(ClusterHeader, Count) == 4);
    static_assert(offsetof(ClusterHeader, OverflowCount) == 8);
    static_assert(offsetof(ClusterHeader, Flags) == 12);
    static_assert(sizeof(ClusterGridConstants) == 16);
    static_assert(offsetof(ClusterGridConstants, TilesX) == 0);
    static_assert(offsetof(ClusterGridConstants, TilesY) == 4);
    static_assert(offsetof(ClusterGridConstants, LightCount) == 8);
    static_assert(offsetof(ClusterGridConstants, MaxPerCluster) == 12);
    static_assert(ClusterTileSize == 16u);
    static_assert(ClusterDepthSlices == 8u);
    static_assert(MaxLightsPerCluster == 64u);

    if (ClusterTileSize != 16u || MaxLightsPerCluster != 64u)
    {
        std::fprintf(stderr, "gpu_lighting: invalid cluster constants\n");
        return 1;
    }
    std::printf("gpu_lighting: OK\n");
    return 0;
}
