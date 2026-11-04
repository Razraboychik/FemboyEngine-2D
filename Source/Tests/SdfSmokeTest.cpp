// FemboyEngine2D - smoke region tests.
#include "Engine/Physics/SdfSmoke.hpp"
#include "Engine/Physics/SdfWorld.hpp"

#include <cmath>
#include <cstdio>

namespace
{
int GFailures = 0;

void Check(bool InCondition, const char* InMessage)
{
    if (!InCondition)
    {
        std::fprintf(stderr, "FAIL: %s\n", InMessage);
        ++GFailures;
    }
}

[[nodiscard]] Fby::SdfSmokeDesc MakeSmokeDesc(std::uint32_t InWidth, std::uint32_t InHeight)
{
    Fby::SdfSmokeDesc ScopedDesc;
    ScopedDesc.Origin = Fby::Vector2D{0.0f, 0.0f};
    ScopedDesc.CellSize = Fby::Vector2D{8.0f, 8.0f};
    ScopedDesc.Width = InWidth;
    ScopedDesc.Height = InHeight;
    ScopedDesc.Dissipation = 0.995f;
    ScopedDesc.Wind = Fby::Vector2D{16.0f, 0.0f};
    ScopedDesc.Absorption = 0.25f;
    return ScopedDesc;
}

void TestAdvection()
{
    Fby::SdfSmokeField ScopedSmoke;
    Check(ScopedSmoke.Configure(MakeSmokeDesc(32, 8)), "configure the advection smoke");
    Check(ScopedSmoke.AddDensityWorld(Fby::Vector2D{64.0f, 32.0f}, 10.0f, 1.0f), "seed the smoke");
    const float ScopedBefore = ScopedSmoke.DensityAt(8, 4);
    Check(ScopedBefore > 0.5f, "the seed lands in its cell");
    for (std::uint32_t ScopedStep = 0; ScopedStep < 60; ++ScopedStep)
    {
        ScopedSmoke.Step(1.0f / 60.0f);
    }
    const float ScopedAfter = ScopedSmoke.DensityAt(8, 4);
    const float ScopedRight = ScopedSmoke.DensityAt(10, 4);
    Check(ScopedAfter < ScopedBefore, "the puff leaves its starting cell");
    Check(ScopedRight > 0.1f, "the wind carries the puff to the right");
    Check(ScopedSmoke.DensityAt(0, 4) < 0.05f, "nothing is carried against the wind");
}

void TestTransmittance()
{
    Fby::SdfSmokeField ScopedSmoke;
    Check(ScopedSmoke.Configure(MakeSmokeDesc(16, 16)), "configure the transmittance smoke");
    Check(ScopedSmoke.AddDensityWorld(Fby::Vector2D{64.0f, 64.0f}, 24.0f, 2.0f), "seed the cloud");
    const float ScopedThrough = ScopedSmoke.Transmittance(Fby::Vector2D{0.0f, 64.0f}, Fby::Vector2D{1.0f, 0.0f},
                                                          128.0f, 32);
    const float ScopedClear = ScopedSmoke.Transmittance(Fby::Vector2D{0.0f, 0.0f}, Fby::Vector2D{1.0f, 0.0f}, 128.0f,
                                                        32);
    Check(ScopedThrough < 0.8f, "the cloud absorbs the ray");
    Check(ScopedClear > 0.99f, "clear air transmits the ray");
    Check(ScopedThrough < ScopedClear, "smoke dims more than clear air");
}

void TestGiAttenuation()
{
    Fby::SdfWorldDesc ScopedWorldDesc;
    ScopedWorldDesc.Origin = Fby::Vector2D{0.0f, 0.0f};
    ScopedWorldDesc.CellSize = Fby::Vector2D{4.0f, 4.0f};
    ScopedWorldDesc.Width = 32;
    ScopedWorldDesc.Height = 32;
    ScopedWorldDesc.OutsideDistance = 1000.0f;
    Fby::SdfWorld ScopedWorld;
    Check(ScopedWorld.Configure(ScopedWorldDesc), "configure the attenuation world");
    Check(ScopedWorld.BakeAll(), "bake the attenuation world");

    Fby::SdfGiBakeSettings ScopedSettings;
    ScopedSettings.SunDirection = Fby::Vector2D{1.0f, 0.0f};
    Fby::SdfGiBaked ScopedBake;
    Check(ScopedBake.Build(ScopedWorld.Bricks(), Fby::Vector2D{0.0f, 0.0f}, Fby::Vector2D{8.0f, 8.0f}, 16, 16,
                           ScopedSettings),
          "bake the attenuation probes");
    const float ScopedBeforeDirect = ScopedBake.ProbeAt(8, 8)->Direct[0];
    Check(ScopedBeforeDirect > 0.9f, "the open probe is lit before the smoke");

    Fby::SdfSmokeField ScopedSmoke;
    Check(ScopedSmoke.Configure(MakeSmokeDesc(16, 16)), "configure the attenuation smoke");
    Check(ScopedSmoke.AddDensityWorld(Fby::Vector2D{100.0f, 68.0f}, 20.0f, 0.5f), "seed the cloud before the probe");
    ScopedSmoke.AttenuateGi(ScopedBake, Fby::Vector2D{1.0f, 0.0f}, 96.0f);

    const Fby::SdfGiBakedProbe* ScopedAfter = ScopedBake.ProbeAt(8, 8);
    Check(ScopedAfter->Direct[0] < ScopedBeforeDirect, "the smoke dims the probe");
    Check(ScopedAfter->Direct[0] > 0.0f, "the probe is dimmed, not extinguished");
    const Fby::SdfGiBakedProbe* ScopedFar = ScopedBake.ProbeAt(0, 15);
    Check(ScopedFar->Direct[0] > 0.9f, "a probe away from the cloud keeps its light");
}

void TestDeterminism()
{
    Fby::SdfSmokeField ScopedFirst;
    Fby::SdfSmokeField ScopedSecond;
    Check(ScopedFirst.Configure(MakeSmokeDesc(16, 16)), "configure the first smoke");
    Check(ScopedSecond.Configure(MakeSmokeDesc(16, 16)), "configure the second smoke");
    for (Fby::SdfSmokeField* ScopedField : {&ScopedFirst, &ScopedSecond})
    {
        Check(ScopedField->AddDensityWorld(Fby::Vector2D{64.0f, 64.0f}, 12.0f, 1.0f), "seed both clouds");
        for (std::uint32_t ScopedStep = 0; ScopedStep < 30; ++ScopedStep)
        {
            ScopedField->Step(1.0f / 60.0f);
        }
    }
    bool ScopedMatches = true;
    for (std::uint32_t ScopedY = 0; ScopedY < 16; ++ScopedY)
    {
        for (std::uint32_t ScopedX = 0; ScopedX < 16; ++ScopedX)
        {
            if (ScopedFirst.DensityAt(ScopedX, ScopedY) != ScopedSecond.DensityAt(ScopedX, ScopedY))
            {
                ScopedMatches = false;
            }
        }
    }
    Check(ScopedMatches, "the smoke step is deterministic");
}
}

int main()
{
    TestAdvection();
    TestTransmittance();
    TestGiAttenuation();
    TestDeterminism();
    if (GFailures != 0)
    {
        std::fprintf(stderr, "%d sdf smoke test(s) failed\n", GFailures);
        return 1;
    }
    std::printf("sdf_smoke: OK\n");
    return 0;
}
