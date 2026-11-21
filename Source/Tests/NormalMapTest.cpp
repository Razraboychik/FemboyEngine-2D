#include "Engine/RHI/RenderItem.hpp"

#include <cmath>
#include <cstdio>

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
    Check(sizeof(Fby::Rhi::RenderItem) == 48, "normal metadata must not grow RenderItem");
    const std::uint32_t ScopedFlags = Fby::Rhi::EncodeNormalMap(7, 0.5f);
    Check(Fby::Rhi::NormalMapLayer(ScopedFlags) == 7, "normal layer roundtrips");
    Check(Fby::Rhi::NormalMapStrength(ScopedFlags) > 0.45f && Fby::Rhi::NormalMapStrength(ScopedFlags) < 0.55f,
          "normal strength roundtrips");
    Check(Fby::Rhi::NormalMapLayer(Fby::Rhi::EncodeNormalMap(999, 2.0f)) == 255, "normal layer clamps");
    Check(Fby::Rhi::NormalMapStrength(Fby::Rhi::EncodeNormalMap(0, 2.0f)) == 1.0f, "normal strength clamps high");
    Check(Fby::Rhi::NormalMapStrength(Fby::Rhi::EncodeNormalMap(0, -1.0f)) == 0.0f, "normal strength clamps low");
    Check(Fby::Rhi::EncodeNormalMap(0, 1.0f) != 0, "active normal map has a flag");
    const float ScopedVertexMeta = Fby::Rhi::EncodeNormalVertexMeta(7, 1.0f);
    Check(std::floor(ScopedVertexMeta) == 7.0f, "vertex meta keeps the layer at full strength");
    Check(std::fabs(std::fmod(ScopedVertexMeta, 1.0f) * 2.0f - 1.0f) < 0.001f,
          "vertex meta keeps full strength below the next layer");
    std::uint32_t ScopedRounds = 0;
    for (std::uint32_t ScopedI = 0; ScopedI < 1000000; ++ScopedI)
    {
        const std::uint32_t Scoped = Fby::Rhi::EncodeNormalMap(ScopedI % 256u, static_cast<float>(ScopedI % 33u) / 32.0f);
        if (Fby::Rhi::NormalMapLayer(Scoped) == ScopedI % 256u)
        {
            ++ScopedRounds;
        }
    }
    Check(ScopedRounds == 1000000, "normal metadata stress");
    if (GFailures == 0)
    {
        std::printf("normal_map_test: all checks passed\n");
        return 0;
    }
    return 1;
}
