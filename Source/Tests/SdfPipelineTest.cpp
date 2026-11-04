// FemboyEngine2D - end-to-end SDF pipeline test: world, carve, imprint, nav, GI.
#include "Engine/Navigation/FlowField.hpp"
#include "Engine/Navigation/PagedNavGrid.hpp"
#include "Engine/Physics/SdfDestructible.hpp"
#include "Engine/Physics/SdfGiBake.hpp"
#include "Engine/Physics/SdfSmoke.hpp"
#include "Engine/Physics/SdfVolumeAsset.hpp"
#include "Engine/Physics/SdfWorld.hpp"

#include <cstdio>
#include <vector>

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

void TestPipeline()
{
    // 1. The world is baked from analytic sources.
    Fby::SdfWorldDesc ScopedWorldDesc;
    ScopedWorldDesc.Origin = Fby::Vector2D{0.0f, 0.0f};
    ScopedWorldDesc.CellSize = Fby::Vector2D{4.0f, 4.0f};
    ScopedWorldDesc.Width = 64;
    ScopedWorldDesc.Height = 64;
    ScopedWorldDesc.NarrowBand = 16.0f;
    ScopedWorldDesc.OutsideDistance = 1000.0f;
    Fby::SdfWorld ScopedWorld;
    Check(ScopedWorld.Configure(ScopedWorldDesc), "configure the world");
    Check(ScopedWorld.AddBox(Fby::Vector2D{128.0f, 40.0f}, Fby::Vector2D{64.0f, 8.0f}), "add the floor");
    Check(ScopedWorld.AddBox(Fby::Vector2D{128.0f, 120.0f}, Fby::Vector2D{8.0f, 48.0f}), "add the wall");
    Check(ScopedWorld.BakeAll(), "bake the world");
    Check(ScopedWorld.Field().Sample(Fby::Vector2D{128.0f, 40.0f}) < 0.0f, "the floor is solid");
    Check(ScopedWorld.Field().Sample(Fby::Vector2D{128.0f, 120.0f}) < 0.0f, "the wall is solid");

    // 2. A destructible is carved and imprinted into the world.
    Fby::SdfDestructibleDesc ScopedObjectDesc;
    ScopedObjectDesc.Origin = Fby::Vector2D{28.0f, 108.0f};
    ScopedObjectDesc.CellSize = Fby::Vector2D{1.0f, 1.0f};
    ScopedObjectDesc.Width = 64;
    ScopedObjectDesc.Height = 64;
    ScopedObjectDesc.BaseShape = Fby::SdfCarveShape::Box;
    ScopedObjectDesc.BaseCenter = Fby::Vector2D{60.0f, 140.0f};
    ScopedObjectDesc.HasBaseCenter = true;
    ScopedObjectDesc.HalfExtents = Fby::Vector2D{24.0f, 24.0f};
    ScopedObjectDesc.NarrowBand = 8.0f;
    Fby::SdfDestructible ScopedObject;
    Check(ScopedObject.Configure(ScopedObjectDesc), "configure the destructible");
    Fby::SdfCarveOp ScopedHole;
    ScopedHole.Shape = Fby::SdfCarveShape::Circle;
    ScopedHole.Center = Fby::Vector2D{60.0f, 140.0f};
    ScopedHole.Radius = 8.0f;
    Check(ScopedObject.Subtract(ScopedHole), "carve the destructible");
    Check(ScopedWorld.Imprint(ScopedObject), "imprint the destructible");
    Check(ScopedWorld.Field().Sample(Fby::Vector2D{72.0f, 140.0f}) < 0.0f, "the imprint is solid");
    Check(ScopedWorld.Field().Sample(Fby::Vector2D{60.0f, 140.0f}) > 0.0f, "the carve stays empty");

    // 3. Navigation costs come from the same field and a flow is built.
    Fby::Navigation::PagedNavGridDesc ScopedNavDesc;
    ScopedNavDesc.Origin = Fby::Vector2D{0.0f, 0.0f};
    ScopedNavDesc.CellSize = Fby::Vector2D{4.0f, 4.0f};
    ScopedNavDesc.PagesX = 1;
    ScopedNavDesc.PagesY = 1;
    Fby::Navigation::PagedNavGrid ScopedNav;
    Check(ScopedNav.Configure(ScopedNavDesc), "configure the nav grid");
    Check(ScopedNav.BakeFromWorld(ScopedWorld, 2.0f), "bake nav costs from the world");
    Check(ScopedNav.CostAt(32, 30) == 0, "the wall blocks navigation");
    Check(ScopedNav.BuildFlow(4, 4), "build the flow to the floor left corner");
    Check(ScopedNav.IsReachableCell(4, 8), "the floor start is reachable");
    Check(!ScopedNav.IsReachableCell(32, 32), "the wall cell stays unreachable");

    // 4. The GI bake sees the same geometry and smoke dims it. Probe (0,0) sits
    // at (8,8), outside the floor and the wall, so the sky ray is open.
    Fby::SdfGiBakeSettings ScopedGiSettings;
    ScopedGiSettings.SunDirection = Fby::Vector2D{0.0f, 1.0f};
    Fby::SdfGiBaked ScopedBake;
    Check(ScopedBake.Build(ScopedWorld.Bricks(), Fby::Vector2D{0.0f, 0.0f}, Fby::Vector2D{16.0f, 16.0f}, 16, 16,
                           ScopedGiSettings),
          "bake the GI probes");
    const Fby::SdfGiBakedProbe* ScopedProbe = ScopedBake.ProbeAt(0, 0);
    Check(ScopedProbe != nullptr, "the probe exists");
    const float ScopedDirect = ScopedProbe->Direct[0];
    Check(ScopedDirect > 0.0f, "the probe under the open sky is lit");
    Fby::SdfSmokeDesc ScopedSmokeDesc;
    ScopedSmokeDesc.Origin = Fby::Vector2D{0.0f, 0.0f};
    ScopedSmokeDesc.CellSize = Fby::Vector2D{16.0f, 16.0f};
    ScopedSmokeDesc.Width = 16;
    ScopedSmokeDesc.Height = 16;
    ScopedSmokeDesc.Absorption = 0.5f;
    Fby::SdfSmokeField ScopedSmoke;
    Check(ScopedSmoke.Configure(ScopedSmokeDesc), "configure the smoke");
    Check(ScopedSmoke.AddDensityWorld(Fby::Vector2D{8.0f, 40.0f}, 24.0f, 2.0f), "seed the smoke above the probe");
    ScopedSmoke.AttenuateGi(ScopedBake, Fby::Vector2D{0.0f, 1.0f}, 96.0f);
    Check(ScopedBake.ProbeAt(0, 0)->Direct[0] < ScopedDirect, "the smoke dims the lit probe");

    // 5. The asset round trip preserves the world, imprints included.
    std::vector<std::byte> ScopedBytes;
    Check(ScopedWorld.SaveAsset(&ScopedBytes), "save the world asset");
    Fby::SdfVolumeAsset ScopedAsset;
    Check(ScopedAsset.Load(ScopedBytes, nullptr), "load the world asset");
    std::vector<float> ScopedSamples;
    Fby::SdfField ScopedLoaded;
    Check(ScopedAsset.ExpandToField(&ScopedSamples, &ScopedLoaded), "expand the loaded field");
    // The loaded field is rebuilt from the narrow band, so compare the solid/empty
    // classification at every grid point rather than the exact distance.
    bool ScopedMatches = true;
    std::uint32_t ScopedSolid = 0;
    for (std::uint32_t ScopedY = 0; ScopedY < 64; ++ScopedY)
    {
        for (std::uint32_t ScopedX = 0; ScopedX < 64; ++ScopedX)
        {
            const Fby::Vector2D ScopedPoint{static_cast<float>(ScopedX) * 4.0f, static_cast<float>(ScopedY) * 4.0f};
            const float ScopedSaved = ScopedLoaded.Sample(ScopedPoint);
            const float ScopedLive = ScopedWorld.Field().Sample(ScopedPoint);
            if ((ScopedSaved < 0.0f) != (ScopedLive < 0.0f))
            {
                ScopedMatches = false;
            }
            if (ScopedSaved < 0.0f)
            {
                ++ScopedSolid;
            }
        }
    }
    Check(ScopedSolid > 0, "the loaded world still has geometry");
    Check(ScopedMatches, "the saved world reproduces the imprinted field");
}
}

int main()
{
    TestPipeline();
    if (GFailures != 0)
    {
        std::fprintf(stderr, "%d sdf pipeline test(s) failed\n", GFailures);
        return 1;
    }
    std::printf("sdf_pipeline: OK\n");
    return 0;
}
