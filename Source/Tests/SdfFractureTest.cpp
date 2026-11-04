// FemboyEngine2D - pre-fracture grid tests.
#include "Engine/Physics/SdfFracture.hpp"

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

void TestFracturePreservesField()
{
    Fby::SdfDestructible ScopedSource;
    Check(ScopedSource.Configure(MakeDesc()), "configure the source");
    Fby::SdfCarveOp ScopedHole;
    ScopedHole.Shape = Fby::SdfCarveShape::Circle;
    ScopedHole.Center = Fby::Vector2D{32.0f, 32.0f};
    ScopedHole.Radius = 8.0f;
    Check(ScopedSource.Subtract(ScopedHole), "carve the source");

    std::vector<Fby::SdfDestructible> ScopedPieces;
    Check(Fby::FractureGrid(ScopedSource, 2, 2, &ScopedPieces), "fracture into four pieces");
    Check(ScopedPieces.size() == 4, "four pieces are produced");

    // Every source grid point equals the sample of the piece that owns it.
    bool ScopedFieldsMatch = true;
    for (std::uint32_t ScopedY = 0; ScopedY < 64; ++ScopedY)
    {
        for (std::uint32_t ScopedX = 0; ScopedX < 64; ++ScopedX)
        {
            const std::uint32_t ScopedColumn = ScopedX / 32u;
            const std::uint32_t ScopedRow = ScopedY / 32u;
            const Fby::SdfDestructible& ScopedPiece = ScopedPieces[ScopedRow * 2u + ScopedColumn];
            const Fby::Vector2D ScopedWorld{static_cast<float>(ScopedX), static_cast<float>(ScopedY)};
            if (ScopedPiece.Sample(ScopedWorld) != ScopedSource.Sample(ScopedWorld))
            {
                ScopedFieldsMatch = false;
            }
        }
    }
    Check(ScopedFieldsMatch, "the pieces reproduce the parent field");

    // The hole lies on the shared corner, so all four pieces see it.
    Check(ScopedPieces[0].Sample(Fby::Vector2D{32.0f, 32.0f}) > 0.0f ||
              ScopedPieces[1].Sample(Fby::Vector2D{32.0f, 32.0f}) > 0.0f ||
              ScopedPieces[2].Sample(Fby::Vector2D{32.0f, 32.0f}) > 0.0f ||
              ScopedPieces[3].Sample(Fby::Vector2D{32.0f, 32.0f}) > 0.0f,
          "the carved corner stays empty");

    // A piece does not answer for another piece's area.
    Check(ScopedPieces[0].Sample(Fby::Vector2D{60.0f, 60.0f}) > 0.0f, "pieces are confined to their footprint");

    // Repeated fracture of a piece keeps the field too.
    std::vector<Fby::SdfDestructible> ScopedQuarter;
    Check(Fby::FractureGrid(ScopedPieces[0], 2, 2, &ScopedQuarter), "fracture a piece again");
    bool ScopedQuarterMatches = true;
    for (std::uint32_t ScopedY = 0; ScopedY < 32; ++ScopedY)
    {
        for (std::uint32_t ScopedX = 0; ScopedX < 32; ++ScopedX)
        {
            const std::uint32_t ScopedColumn = ScopedX / 16u;
            const std::uint32_t ScopedRow = ScopedY / 16u;
            const Fby::SdfDestructible& ScopedPiece = ScopedQuarter[ScopedRow * 2u + ScopedColumn];
            const Fby::Vector2D ScopedWorld{static_cast<float>(ScopedX), static_cast<float>(ScopedY)};
            if (ScopedPiece.Sample(ScopedWorld) != ScopedPieces[0].Sample(ScopedWorld))
            {
                ScopedQuarterMatches = false;
            }
        }
    }
    Check(ScopedQuarterMatches, "second-generation pieces keep the field");
}

void TestInvalid()
{
    Fby::SdfDestructible ScopedSource;
    Check(ScopedSource.Configure(MakeDesc()), "configure for the invalid case");
    std::vector<Fby::SdfDestructible> ScopedPieces;
    Check(!Fby::FractureGrid(ScopedSource, 0, 1, &ScopedPieces), "zero columns are rejected");
    Check(!Fby::FractureGrid(ScopedSource, 1, 0, &ScopedPieces), "zero rows are rejected");
    Check(!Fby::FractureGrid(ScopedSource, 1, 1, nullptr), "a null output is rejected");
    Fby::SdfDestructible ScopedUnconfigured;
    Check(!Fby::FractureGrid(ScopedUnconfigured, 2, 2, &ScopedPieces), "an unconfigured source is rejected");

    // A very fine request clamps to two-cell pieces instead of one-cell pieces.
    std::vector<Fby::SdfDestructible> ScopedFine;
    Check(Fby::FractureGrid(ScopedSource, 64, 64, &ScopedFine), "a fine fracture still succeeds");
    Check(ScopedFine.size() == 32u * 32u, "the fine fracture clamps to two-cell pieces");
}
}

int main()
{
    TestFracturePreservesField();
    TestInvalid();
    if (GFailures != 0)
    {
        std::fprintf(stderr, "%d sdf fracture test(s) failed\n", GFailures);
        return 1;
    }
    std::printf("sdf_fracture: OK\n");
    return 0;
}
