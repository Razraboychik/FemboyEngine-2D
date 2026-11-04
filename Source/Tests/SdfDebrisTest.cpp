// FemboyEngine2D - debris spawn tests.
#include "Engine/Physics/SdfDebris.hpp"

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

[[nodiscard]] Fby::SdfDestructible MakeObject()
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
    Fby::SdfDestructible ScopedObject;
    (void)ScopedObject.Configure(ScopedDesc);
    return ScopedObject;
}

void TestSpawn()
{
    Fby::SdfDestructible ScopedObject = MakeObject();
    Fby::SdfCarveOp ScopedHole;
    ScopedHole.Shape = Fby::SdfCarveShape::Circle;
    ScopedHole.Center = Fby::Vector2D{32.0f, 32.0f};
    ScopedHole.Radius = 8.0f;

    Fby::SdfDebrisSettings ScopedSettings;
    ScopedSettings.MaxPieces = 24;
    ScopedSettings.Stride = 1;
    ScopedSettings.Lifetime = 2.0f;
    ScopedSettings.HalfExtent = 0.5f;
    std::vector<Fby::SdfDebrisPiece> ScopedPieces(64);
    const std::uint32_t ScopedCount = Fby::SpawnDebris(ScopedObject, ScopedHole, ScopedSettings, ScopedPieces);
    Check(ScopedCount > 0 && ScopedCount <= 24, "the piece count respects the cap");
    bool ScopedInside = true;
    bool ScopedSpeeds = true;
    bool ScopedLifetimes = true;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedCount; ++ScopedIndex)
    {
        const Fby::SdfDebrisPiece& ScopedPiece = ScopedPieces[ScopedIndex];
        const float ScopedDistance = (ScopedPiece.Position - ScopedHole.Center).LengthSquared();
        if (ScopedDistance > ScopedHole.Radius * ScopedHole.Radius + 1.0e-3f)
        {
            ScopedInside = false;
        }
        const float ScopedSpeed = std::sqrt(ScopedPiece.Velocity.LengthSquared());
        if (ScopedSpeed < ScopedSettings.SpeedMin - 1.0e-3f || ScopedSpeed > ScopedSettings.SpeedMax + 1.0e-3f)
        {
            ScopedSpeeds = false;
        }
        if (ScopedPiece.Lifetime != ScopedSettings.Lifetime)
        {
            ScopedLifetimes = false;
        }
    }
    Check(ScopedInside, "fragments spawn inside the carve");
    Check(ScopedSpeeds, "the speeds stay in range");
    Check(ScopedLifetimes, "the lifetime is copied");

    // Deterministic: the same carve and seed produce the same array.
    std::vector<Fby::SdfDebrisPiece> ScopedSecond(64);
    const std::uint32_t ScopedSecondCount =
        Fby::SpawnDebris(ScopedObject, ScopedHole, ScopedSettings, ScopedSecond);
    Check(ScopedSecondCount == ScopedCount, "the count is deterministic");
    bool ScopedIdentical = true;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedCount; ++ScopedIndex)
    {
        if (ScopedPieces[ScopedIndex].Position.X != ScopedSecond[ScopedIndex].Position.X ||
            ScopedPieces[ScopedIndex].Velocity.X != ScopedSecond[ScopedIndex].Velocity.X ||
            ScopedPieces[ScopedIndex].Rotation != ScopedSecond[ScopedIndex].Rotation)
        {
            ScopedIdentical = false;
        }
    }
    Check(ScopedIdentical, "the debris spawn is deterministic");

    // A different seed changes the velocities, not the positions.
    Fby::SdfDebrisSettings ScopedOtherSeed = ScopedSettings;
    ScopedOtherSeed.Seed = 99;
    std::vector<Fby::SdfDebrisPiece> ScopedSeeded(64);
    const std::uint32_t ScopedSeededCount =
        Fby::SpawnDebris(ScopedObject, ScopedHole, ScopedOtherSeed, ScopedSeeded);
    Check(ScopedSeededCount == ScopedCount, "the seed does not change the count");
    Check(ScopedSeeded[0].Position.X == ScopedPieces[0].Position.X, "the seed does not move the spawn points");
    Check(ScopedSeeded[0].Velocity.LengthSquared() != ScopedPieces[0].Velocity.LengthSquared(),
          "the seed changes the speed");

    // The cap is a hard limit, including a single piece.
    Fby::SdfDebrisSettings ScopedOne = ScopedSettings;
    ScopedOne.MaxPieces = 1;
    std::vector<Fby::SdfDebrisPiece> ScopedSingle(64);
    Check(Fby::SpawnDebris(ScopedObject, ScopedHole, ScopedOne, ScopedSingle) == 1, "the single-piece cap holds");
}

void TestInvalid()
{
    Fby::SdfDestructible ScopedObject = MakeObject();
    Fby::SdfCarveOp ScopedHole;
    ScopedHole.Center = Fby::Vector2D{32.0f, 32.0f};
    ScopedHole.Radius = 8.0f;
    Fby::SdfDebrisSettings ScopedSettings;
    std::vector<Fby::SdfDebrisPiece> ScopedPieces(8);
    Check(Fby::SpawnDebris(ScopedObject, ScopedHole, ScopedSettings, {}) == 0, "an empty output is rejected");
    ScopedSettings.MaxPieces = 0;
    Check(Fby::SpawnDebris(ScopedObject, ScopedHole, ScopedSettings, ScopedPieces) == 0,
          "a zero cap spawns nothing");
    Fby::SdfDestructible ScopedUnconfigured;
    ScopedSettings.MaxPieces = 8;
    Check(Fby::SpawnDebris(ScopedUnconfigured, ScopedHole, ScopedSettings, ScopedPieces) == 0,
          "an unconfigured object spawns nothing");
    Fby::SdfCarveOp ScopedOutside;
    ScopedOutside.Center = Fby::Vector2D{-1000.0f, -1000.0f};
    ScopedOutside.Radius = 4.0f;
    Check(Fby::SpawnDebris(ScopedObject, ScopedOutside, ScopedSettings, ScopedPieces) == 0,
          "a carve outside the object spawns nothing");
}
}

int main()
{
    TestSpawn();
    TestInvalid();
    if (GFailures != 0)
    {
        std::fprintf(stderr, "%d sdf debris test(s) failed\n", GFailures);
        return 1;
    }
    std::printf("sdf_debris: OK\n");
    return 0;
}
