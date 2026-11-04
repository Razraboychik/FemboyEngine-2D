// FemboyEngine2D - baked GI probe tests.
#include "Engine/Physics/SdfGiAsset.hpp"
#include "Engine/Physics/SdfGiBake.hpp"
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

[[nodiscard]] Fby::SdfWorldDesc MakeWorldDesc()
{
    Fby::SdfWorldDesc ScopedDesc;
    ScopedDesc.Origin = Fby::Vector2D{0.0f, 0.0f};
    ScopedDesc.CellSize = Fby::Vector2D{4.0f, 4.0f};
    ScopedDesc.Width = 64;
    ScopedDesc.Height = 64;
    ScopedDesc.NarrowBand = 16.0f;
    ScopedDesc.OutsideDistance = 1000.0f;
    return ScopedDesc;
}

void TestBake()
{
    Fby::SdfWorld ScopedWorld;
    Check(ScopedWorld.Configure(MakeWorldDesc()), "configure the GI world");
    // A short vertical wall: it shadows probes at mid height on its left side,
    // while probes above it still see the sun from the right.
    Check(ScopedWorld.AddBox(Fby::Vector2D{128.0f, 64.0f}, Fby::Vector2D{8.0f, 30.0f}), "add the wall");
    Check(ScopedWorld.BakeAll(), "bake the GI world");

    Fby::SdfGiBakeSettings ScopedSettings;
    ScopedSettings.SunDirection = Fby::Vector2D{1.0f, 0.0f};
    ScopedSettings.BounceRadius = 48.0f;
    ScopedSettings.BounceSamples = 8;
    ScopedSettings.BounceIntensity = 0.5f;

    Fby::SdfGiBaked ScopedBake;
    Check(ScopedBake.Build(ScopedWorld.Bricks(), Fby::Vector2D{0.0f, 0.0f}, Fby::Vector2D{4.0f, 4.0f}, 64, 64,
                           ScopedSettings),
          "bake the probe grid");
    Check(ScopedBake.ProbeCount() == 64u * 64u, "every probe is baked");

    // Probe (25,16) sits at (102,66) behind the wall for a sun from +X.
    const Fby::SdfGiBakedProbe* ScopedShadowed = ScopedBake.ProbeAt(25, 16);
    Check(ScopedShadowed != nullptr, "the shadowed probe exists");
    Check(ScopedShadowed->SunVisibility == 0.0f, "the wall blocks the direct sun");
    Check(ScopedShadowed->Indirect[0] > 0.0f, "the bounce brings light behind the wall");

    // Probe (10,28) sits at (42,114) above the wall and sees the sun.
    const Fby::SdfGiBakedProbe* ScopedLit = ScopedBake.ProbeAt(10, 28);
    Check(ScopedLit != nullptr, "the lit probe exists");
    Check(ScopedLit->SunVisibility > 0.5f, "open sky is lit directly");
    Check(ScopedLit->Direct[0] > 0.0f, "the direct colour carries the sun");

    // A probe inside the wall is dark on both terms.
    const Fby::SdfGiBakedProbe* ScopedSolid = ScopedBake.ProbeAt(32, 16);
    Check(ScopedSolid != nullptr, "the solid probe exists");
    Check(ScopedSolid->SunVisibility == 0.0f, "a solid probe has no sun");
    Check(ScopedSolid->Indirect[0] == 0.0f, "a solid probe has no bounce");

    // Determinism: the same world bakes to the same numbers.
    Fby::SdfGiBaked ScopedSecond;
    Check(ScopedSecond.Build(ScopedWorld.Bricks(), Fby::Vector2D{0.0f, 0.0f}, Fby::Vector2D{4.0f, 4.0f}, 64, 64,
                             ScopedSettings),
          "bake the grid twice");
    bool ScopedMatches = true;
    for (std::uint32_t ScopedY = 0; ScopedY < 64; ++ScopedY)
    {
        for (std::uint32_t ScopedX = 0; ScopedX < 64; ++ScopedX)
        {
            const Fby::SdfGiBakedProbe* ScopedA = ScopedBake.ProbeAt(ScopedX, ScopedY);
            const Fby::SdfGiBakedProbe* ScopedB = ScopedSecond.ProbeAt(ScopedX, ScopedY);
            if (ScopedA->SunVisibility != ScopedB->SunVisibility || ScopedA->Direct[0] != ScopedB->Direct[0] ||
                ScopedA->Indirect[0] != ScopedB->Indirect[0])
            {
                ScopedMatches = false;
            }
        }
    }
    Check(ScopedMatches, "the bake is deterministic");
}

void TestAsset()
{
    Fby::SdfWorld ScopedWorld;
    Check(ScopedWorld.Configure(MakeWorldDesc()), "configure the asset world");
    Check(ScopedWorld.AddBox(Fby::Vector2D{128.0f, 64.0f}, Fby::Vector2D{8.0f, 30.0f}), "asset wall");
    Check(ScopedWorld.BakeAll(), "bake the asset world");

    Fby::SdfGiBakeSettings ScopedSettings;
    ScopedSettings.SunDirection = Fby::Vector2D{1.0f, 0.0f};
    Fby::SdfGiBaked ScopedBake;
    Check(ScopedBake.Build(ScopedWorld.Bricks(), Fby::Vector2D{0.0f, 0.0f}, Fby::Vector2D{4.0f, 4.0f}, 32, 32,
                           ScopedSettings),
          "bake for the sidecar");

    const std::uint64_t ScopedHash = Fby::HashSdfWorld(ScopedWorld);
    std::vector<std::byte> ScopedBytes;
    Check(Fby::SdfGiAsset::Save(ScopedBake, ScopedHash, &ScopedBytes), "save the GI sidecar");

    Fby::SdfGiAsset ScopedAsset;
    std::string ScopedError;
    Check(ScopedAsset.Load(ScopedBytes, ScopedHash, &ScopedError), "load the GI sidecar");
    Check(ScopedError.empty(), "the load reports no error");
    Check(ScopedAsset.Bake().Width() == 32 && ScopedAsset.Bake().Height() == 32, "the grid is restored");
    bool ScopedProbesMatch = true;
    for (std::uint32_t ScopedY = 0; ScopedY < 32; ++ScopedY)
    {
        for (std::uint32_t ScopedX = 0; ScopedX < 32; ++ScopedX)
        {
            const Fby::SdfGiBakedProbe* ScopedA = ScopedBake.ProbeAt(ScopedX, ScopedY);
            const Fby::SdfGiBakedProbe* ScopedB = ScopedAsset.Bake().ProbeAt(ScopedX, ScopedY);
            if (ScopedA->SunVisibility != ScopedB->SunVisibility || ScopedA->Direct[0] != ScopedB->Direct[0] ||
                ScopedA->Indirect[0] != ScopedB->Indirect[0])
            {
                ScopedProbesMatch = false;
            }
        }
    }
    Check(ScopedProbesMatch, "the loaded probes match the bake");

    // A different world hash is rejected: the bake no longer describes the world.
    Fby::SdfGiAsset ScopedWrongHash;
    Check(!ScopedWrongHash.Load(ScopedBytes, ScopedHash ^ 1ull, &ScopedError), "a stale world hash is rejected");

    // Changing the world changes the hash.
    Check(ScopedWorld.AddBox(Fby::Vector2D{40.0f, 40.0f}, Fby::Vector2D{8.0f, 8.0f}), "add a box");
    Check(ScopedWorld.BakeAll(), "rebake the changed world");
    Check(Fby::HashSdfWorld(ScopedWorld) != ScopedHash, "the world hash changes with the geometry");
    Check(!ScopedWrongHash.Load(ScopedBytes, Fby::HashSdfWorld(ScopedWorld), &ScopedError),
          "the old sidecar no longer loads");

    // Corrupt containers are rejected.
    std::vector<std::byte> ScopedTruncated(ScopedBytes.begin(), ScopedBytes.end() - 8);
    Check(!ScopedWrongHash.Load(ScopedTruncated, ScopedHash, &ScopedError), "a truncated sidecar is rejected");
    std::vector<std::byte> ScopedMagic = ScopedBytes;
    ScopedMagic[0] = std::byte{0x00};
    Check(!ScopedWrongHash.Load(ScopedMagic, ScopedHash, &ScopedError), "a wrong magic is rejected");
}
}

int main()
{
    TestBake();
    TestAsset();
    if (GFailures != 0)
    {
        std::fprintf(stderr, "%d sdf gi bake test(s) failed\n", GFailures);
        return 1;
    }
    std::printf("sdf_gi_bake: OK\n");
    return 0;
}
