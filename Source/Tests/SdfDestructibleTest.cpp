// FemboyEngine2D - destructible object tests.
#include "Engine/Physics/SdfDestructible.hpp"

#include <cmath>
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

[[nodiscard]] Fby::SdfDestructibleDesc MakeDesc()
{
    Fby::SdfDestructibleDesc ScopedDesc;
    ScopedDesc.Origin = Fby::Vector2D{0.0f, 0.0f};
    ScopedDesc.CellSize = Fby::Vector2D{1.0f, 1.0f};
    ScopedDesc.Width = 64;
    ScopedDesc.Height = 64;
    ScopedDesc.BaseShape = Fby::SdfCarveShape::Box;
    ScopedDesc.HalfExtents = Fby::Vector2D{32.0f, 32.0f};
    ScopedDesc.NarrowBand = 8.0f;
    ScopedDesc.OutsideDistance = 1000.0f;
    return ScopedDesc;
}

[[nodiscard]] std::vector<float> SampleGrid(const Fby::SdfDestructible& InObject)
{
    std::vector<float> ScopedSamples(64u * 64u, 0.0f);
    for (std::uint32_t ScopedY = 0; ScopedY < 64; ++ScopedY)
    {
        for (std::uint32_t ScopedX = 0; ScopedX < 64; ++ScopedX)
        {
            ScopedSamples[static_cast<std::size_t>(ScopedY) * 64 + ScopedX] =
                InObject.Sample(Fby::Vector2D{static_cast<float>(ScopedX), static_cast<float>(ScopedY)});
        }
    }
    return ScopedSamples;
}

void TestCarve()
{
    Fby::SdfDestructible ScopedObject;
    Check(ScopedObject.Configure(MakeDesc()), "configure the destructible");
    Check(ScopedObject.IsConfigured(), "destructible is configured");
    Check(ScopedObject.Sample(Fby::Vector2D{32.0f, 32.0f}) < 0.0f, "intact object is solid at the centre");
    const std::uint64_t ScopedVersion = ScopedObject.Version();

    Fby::SdfCarveOp ScopedHole;
    ScopedHole.Shape = Fby::SdfCarveShape::Circle;
    ScopedHole.Center = Fby::Vector2D{32.0f, 32.0f};
    ScopedHole.Radius = 10.0f;
    Check(ScopedObject.Subtract(ScopedHole), "subtract a hole");
    Check(ScopedObject.Ops().size() == 1, "the op list records the hole");
    Check(ScopedObject.Version() == ScopedVersion + 1, "the carve bumps the version");
    Check(ScopedObject.DirtyBricksRebaked() > 0, "the carve rebakes its bricks");
    Check(ScopedObject.Sample(Fby::Vector2D{32.0f, 32.0f}) > 0.0f, "the hole is empty");
    Check(ScopedObject.Sample(Fby::Vector2D{2.0f, 2.0f}) < 0.0f, "material far from the hole survives");

    // The op list is the source of truth: replaying it reproduces the carve.
    const std::vector<float> ScopedCarved = SampleGrid(ScopedObject);
    ScopedObject.Rebuild();
    const std::vector<float> ScopedReplayed = SampleGrid(ScopedObject);
    Check(ScopedCarved == ScopedReplayed, "replaying the op list reproduces the field");

    // Two objects with the same ops are identical.
    Fby::SdfDestructible ScopedSecond;
    Check(ScopedSecond.Configure(MakeDesc()), "configure a second destructible");
    Check(ScopedSecond.Subtract(ScopedHole), "second object takes the same hole");
    Check(SampleGrid(ScopedSecond) == ScopedCarved, "the same op list builds the same object");
}

void TestOpCapacity()
{
    Fby::SdfDestructible ScopedObject;
    Check(ScopedObject.Configure(MakeDesc()), "configure for the capacity case");
    Fby::SdfCarveOp ScopedOp;
    ScopedOp.Shape = Fby::SdfCarveShape::Circle;
    ScopedOp.Center = Fby::Vector2D{32.0f, 32.0f};
    ScopedOp.Radius = 1.0f;
    std::uint32_t ScopedApplied = 0;
    while (ScopedObject.Subtract(ScopedOp))
    {
        ++ScopedApplied;
    }
    Check(ScopedApplied == Fby::SdfDestructible::kMaxOps, "the op list fills to its capacity");
    Check(ScopedObject.Ops().size() == ScopedApplied, "the recorded op count matches");
}
void TestPersistence()
{
    Fby::SdfDestructible ScopedObject;
    Check(ScopedObject.Configure(MakeDesc()), "configure the persistent destructible");
    Fby::SdfCarveOp ScopedFirst;
    ScopedFirst.Shape = Fby::SdfCarveShape::Circle;
    ScopedFirst.Center = Fby::Vector2D{20.0f, 32.0f};
    ScopedFirst.Radius = 6.0f;
    Fby::SdfCarveOp ScopedSecond;
    ScopedSecond.Shape = Fby::SdfCarveShape::Box;
    ScopedSecond.Center = Fby::Vector2D{44.0f, 32.0f};
    ScopedSecond.HalfExtents = Fby::Vector2D{5.0f, 9.0f};
    Check(ScopedObject.Subtract(ScopedFirst), "carve the first hole");
    Check(ScopedObject.Subtract(ScopedSecond), "carve the second hole");
    const std::vector<float> ScopedCarved = SampleGrid(ScopedObject);

    std::vector<std::byte> ScopedBytes;
    Check(ScopedObject.SaveOps(&ScopedBytes), "save the op list");
    Check(ScopedBytes.size() > 24, "saved op list carries records");

    // A fresh object rebuilt from the saved op list matches the carved one.
    Fby::SdfDestructible ScopedLoaded;
    Check(ScopedLoaded.Configure(MakeDesc()), "configure the loaded destructible");
    std::string ScopedError;
    Check(ScopedLoaded.LoadOps(ScopedBytes, &ScopedError), "load the op list");
    Check(ScopedError.empty(), "load reports no error");
    Check(ScopedLoaded.Ops().size() == 2, "load restores both ops");
    const std::vector<float> ScopedReloaded = SampleGrid(ScopedLoaded);
    Check(ScopedReloaded == ScopedCarved, "the loaded object matches the carved one");

    // Truncated and mistyped containers are rejected.
    std::vector<std::byte> ScopedTruncated(ScopedBytes.begin(), ScopedBytes.end() - 4);
    Check(!ScopedLoaded.LoadOps(ScopedTruncated, &ScopedError), "truncated op list is rejected");
    std::vector<std::byte> ScopedWrongMagic = ScopedBytes;
    ScopedWrongMagic[0] = std::byte{0x00};
    Check(!ScopedLoaded.LoadOps(ScopedWrongMagic, &ScopedError), "wrong magic is rejected");
    Fby::SdfDestructible ScopedUnconfigured;
    Check(!ScopedUnconfigured.LoadOps(ScopedBytes, &ScopedError), "unconfigured object rejects the load");
}
}

int main()
{
    TestCarve();
    TestOpCapacity();
    TestPersistence();
    if (GFailures != 0)
    {
        std::fprintf(stderr, "%d sdf destructible test(s) failed\n", GFailures);
        return 1;
    }
    std::printf("sdf_destructible: OK\n");
    return 0;
}
