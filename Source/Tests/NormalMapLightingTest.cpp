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

struct Normal
{
    float X;
    float Y;
    float Z;
};

Normal DecodeNormal(float InR, float InG, float InB, float InStrength)
{
    Normal ScopedNormal{InR * 2.0f - 1.0f, InG * 2.0f - 1.0f, InB * 2.0f - 1.0f};
    ScopedNormal.X *= InStrength;
    ScopedNormal.Y *= InStrength;
    const float ScopedLength = std::sqrt(ScopedNormal.X * ScopedNormal.X + ScopedNormal.Y * ScopedNormal.Y +
                                         ScopedNormal.Z * ScopedNormal.Z);
    if (ScopedLength <= 0.00001f)
    {
        return Normal{0.0f, 0.0f, 1.0f};
    }
    ScopedNormal.X /= ScopedLength;
    ScopedNormal.Y /= ScopedLength;
    ScopedNormal.Z /= ScopedLength;
    return ScopedNormal;
}

bool Near(float InA, float InB, float InTolerance = 0.001f)
{
    return std::fabs(InA - InB) < InTolerance;
}
}

int main()
{
    const Normal ScopedFlat = DecodeNormal(0.5f, 0.5f, 1.0f, 1.0f);
    Check(Near(ScopedFlat.X, 0.0f) && Near(ScopedFlat.Y, 0.0f) && Near(ScopedFlat.Z, 1.0f),
          "flat normal decodes to +Z");
    Check(Fby::Rhi::NormalMapStrength(Fby::Rhi::EncodeNormalMap(1, 1.0f)) == 1.0f, "full strength survives flags");
    const Normal ScopedTangent = DecodeNormal(1.0f, 0.5f, 0.5f, 1.0f);
    const float ScopedLightX = -0.4f / std::sqrt(0.4f * 0.4f + 0.6f * 0.6f + 0.7f * 0.7f);
    const float ScopedLightY = -0.6f / std::sqrt(0.4f * 0.4f + 0.6f * 0.6f + 0.7f * 0.7f);
    const float ScopedLightZ = 0.7f / std::sqrt(0.4f * 0.4f + 0.6f * 0.6f + 0.7f * 0.7f);
    const float ScopedTangentDot = ScopedTangent.X * ScopedLightX + ScopedTangent.Y * ScopedLightY +
                                   ScopedTangent.Z * ScopedLightZ;
    const float ScopedFlatDot = ScopedFlat.X * ScopedLightX + ScopedFlat.Y * ScopedLightY + ScopedFlat.Z * ScopedLightZ;
    Check(std::fabs(ScopedTangentDot - ScopedFlatDot) > 0.1f, "tangent normal changes the directional response");
    const Normal ScopedHalf = DecodeNormal(0.5f, 0.5f, 1.0f, 0.0f);
    Check(Near(ScopedHalf.Z, 1.0f), "zero strength keeps a valid normal");

    if (GFailures == 0)
    {
        std::printf("normal_map_lighting_test: all checks passed\n");
        return 0;
    }
    return 1;
}
