// FemboyEngine2D - allocation-free Perlin / Simplex / Worley noise.
#include "Engine/Core/NoiseGenerator.hpp"

#include <bit>
#include <cmath>

namespace Fby::Noise
{
namespace
{
constexpr float kInvSqrt2 = 0.70710678118654752440f;
constexpr float kInvSqrt3 = 0.57735026918962576451f;

[[nodiscard]] constexpr int FastFloor(float InValue) noexcept
{
    const int ScopedTruncated = static_cast<int>(InValue);
    return ScopedTruncated - (InValue < static_cast<float>(ScopedTruncated) ? 1 : 0);
}

[[nodiscard]] constexpr std::uint64_t SplitMixFinal(std::uint64_t InValue) noexcept
{
    InValue ^= InValue >> 30u;
    InValue *= 0xBF58476D1CE4E5B9ull;
    InValue ^= InValue >> 27u;
    InValue *= 0x94D049BB133111EBull;
    InValue ^= InValue >> 31u;
    return InValue;
}

[[nodiscard]] constexpr std::uint64_t NextSplitMix(std::uint64_t& InState) noexcept
{
    InState += 0x9E3779B97F4A7C15ull;
    return SplitMixFinal(InState);
}

// Integer hash of a Worley cell. Coordinates are folded as unsigned values so a
[[nodiscard]] std::uint64_t HashCell(int InX, int InY, int InZ, std::uint64_t InSeed) noexcept
{
    std::uint64_t ScopedHash = InSeed ^ 0x9E3779B97F4A7C15ull;
    ScopedHash ^= static_cast<std::uint64_t>(static_cast<std::uint32_t>(InX)) * 0x9E3779B185EBCA87ull;
    ScopedHash = std::rotl(ScopedHash, 27);
    ScopedHash ^= static_cast<std::uint64_t>(static_cast<std::uint32_t>(InY)) * 0xC2B2AE3D27D4EB4Full;
    ScopedHash = std::rotl(ScopedHash, 31);
    ScopedHash ^= static_cast<std::uint64_t>(static_cast<std::uint32_t>(InZ)) * 0x165667B19E3779F9ull;
    return SplitMixFinal(ScopedHash);
}

// One feature point inside a cell, in [0, 1) on each axis.
[[nodiscard]] constexpr float Feature01(std::uint64_t InHash) noexcept
{
    return static_cast<float>(InHash & 0xFFFFFFull) * (1.0f / 16777216.0f);
}

[[nodiscard]] constexpr float Grad2(std::uint32_t InHash, float InX, float InY) noexcept
{
    switch (InHash & 7u)
    {
        case 0:
            return InX + InY;
        case 1:
            return -InX + InY;
        case 2:
            return InX - InY;
        case 3:
            return -InX - InY;
        case 4:
            return InX;
        case 5:
            return -InX;
        case 6:
            return InY;
        default:
            return -InY;
    }
}

[[nodiscard]] constexpr float Grad3(std::uint32_t InHash, float InX, float InY, float InZ) noexcept
{
    switch (InHash % 12u)
    {
        case 0:
            return InX + InY;
        case 1:
            return -InX + InY;
        case 2:
            return InX - InY;
        case 3:
            return -InX - InY;
        case 4:
            return InX + InZ;
        case 5:
            return -InX + InZ;
        case 6:
            return InX - InZ;
        case 7:
            return -InX - InZ;
        case 8:
            return InY + InZ;
        case 9:
            return -InY + InZ;
        case 10:
            return InY - InZ;
        default:
            return -InY - InZ;
    }
}

// The twelve classic edge gradients from Gustavson's reference implementation.
[[nodiscard]] constexpr float Grad2Simplex(std::uint32_t InHash, float InX, float InY) noexcept
{
    switch (InHash % 12u)
    {
        case 0:
            return InX + InY;
        case 1:
            return -InX + InY;
        case 2:
            return InX - InY;
        case 3:
            return -InX - InY;
        case 4:
            return InX;
        case 5:
            return -InX;
        case 6:
            return InX;
        case 7:
            return -InX;
        case 8:
            return InY;
        case 9:
            return -InY;
        case 10:
            return InY;
        default:
            return -InY;
    }
}

[[nodiscard]] constexpr float Grad3Simplex(std::uint32_t InHash, float InX, float InY, float InZ) noexcept
{
    switch (InHash % 12u)
    {
        case 0:
            return InX + InY;
        case 1:
            return -InX + InY;
        case 2:
            return InX - InY;
        case 3:
            return -InX - InY;
        case 4:
            return InX + InZ;
        case 5:
            return -InX + InZ;
        case 6:
            return InX - InZ;
        case 7:
            return -InX - InZ;
        case 8:
            return InY + InZ;
        case 9:
            return -InY + InZ;
        case 10:
            return InY - InZ;
        default:
            return -InY - InZ;
    }
}

[[nodiscard]] constexpr float SimplexCorner2(std::uint32_t InHash, float InX, float InY) noexcept
{
    float ScopedT = 0.5f - InX * InX - InY * InY;
    if (ScopedT <= 0.0f)
    {
        return 0.0f;
    }
    ScopedT *= ScopedT;
    return ScopedT * ScopedT * Grad2Simplex(InHash, InX, InY);
}

[[nodiscard]] constexpr float SimplexCorner3(std::uint32_t InHash, float InX, float InY, float InZ) noexcept
{
    float ScopedT = 0.6f - InX * InX - InY * InY - InZ * InZ;
    if (ScopedT <= 0.0f)
    {
        return 0.0f;
    }
    ScopedT *= ScopedT;
    return ScopedT * ScopedT * Grad3Simplex(InHash, InX, InY, InZ);
}

[[nodiscard]] constexpr float Clamp01(float InValue) noexcept
{
    return InValue < 0.0f ? 0.0f : (InValue > 1.0f ? 1.0f : InValue);
}
}

NoiseGenerator::NoiseGenerator(std::uint64_t InSeed) noexcept
{
    BuildPermutation(InSeed);
}

void NoiseGenerator::Reseed(std::uint64_t InSeed) noexcept
{
    BuildPermutation(InSeed);
}

void NoiseGenerator::BuildPermutation(std::uint64_t InSeed) noexcept
{
    MSeed = InSeed;
    std::array<std::uint8_t, 256> ScopedTable{};
    for (std::uint32_t ScopedI = 0; ScopedI < ScopedTable.size(); ++ScopedI)
    {
        ScopedTable[ScopedI] = static_cast<std::uint8_t>(ScopedI);
    }
    std::uint64_t ScopedState = InSeed ^ 0xA0761D6478BD642Full;
    for (std::uint32_t ScopedI = 255; ScopedI > 0; --ScopedI)
    {
        const std::uint32_t ScopedJ = static_cast<std::uint32_t>(NextSplitMix(ScopedState) % (ScopedI + 1u));
        const std::uint8_t ScopedTemporary = ScopedTable[ScopedI];
        ScopedTable[ScopedI] = ScopedTable[ScopedJ];
        ScopedTable[ScopedJ] = ScopedTemporary;
    }
    for (std::uint32_t ScopedI = 0; ScopedI < 256; ++ScopedI)
    {
        MPermutation[ScopedI] = ScopedTable[ScopedI];
        MPermutation[ScopedI + 256] = ScopedTable[ScopedI];
    }
}

// Perlin
float NoiseGenerator::Perlin2D(float InX, float InY) const noexcept
{
    const int ScopedXi = FastFloor(InX);
    const int ScopedYi = FastFloor(InY);
    const float ScopedXf = InX - static_cast<float>(ScopedXi);
    const float ScopedYf = InY - static_cast<float>(ScopedYi);
    const float ScopedU = Fade(ScopedXf);
    const float ScopedV = Fade(ScopedYf);

    const std::uint32_t ScopedX = static_cast<std::uint32_t>(ScopedXi) & 255u;
    const std::uint32_t ScopedY = static_cast<std::uint32_t>(ScopedYi) & 255u;
    const std::uint32_t ScopedAa = MPermutation[MPermutation[ScopedX] + ScopedY];
    const std::uint32_t ScopedAb = MPermutation[MPermutation[ScopedX] + ScopedY + 1u];
    const std::uint32_t ScopedBa = MPermutation[MPermutation[ScopedX + 1u] + ScopedY];
    const std::uint32_t ScopedBb = MPermutation[MPermutation[ScopedX + 1u] + ScopedY + 1u];

    const float ScopedBottom =
        Lerp(Grad2(ScopedAa, ScopedXf, ScopedYf), Grad2(ScopedBa, ScopedXf - 1.0f, ScopedYf), ScopedU);
    const float ScopedTop =
        Lerp(Grad2(ScopedAb, ScopedXf, ScopedYf - 1.0f), Grad2(ScopedBb, ScopedXf - 1.0f, ScopedYf - 1.0f), ScopedU);
    return Lerp(ScopedBottom, ScopedTop, ScopedV);
}

float NoiseGenerator::Perlin3D(float InX, float InY, float InZ) const noexcept
{
    const int ScopedXi = FastFloor(InX);
    const int ScopedYi = FastFloor(InY);
    const int ScopedZi = FastFloor(InZ);
    const float ScopedXf = InX - static_cast<float>(ScopedXi);
    const float ScopedYf = InY - static_cast<float>(ScopedYi);
    const float ScopedZf = InZ - static_cast<float>(ScopedZi);
    const float ScopedU = Fade(ScopedXf);
    const float ScopedV = Fade(ScopedYf);
    const float ScopedW = Fade(ScopedZf);

    const std::uint32_t ScopedX = static_cast<std::uint32_t>(ScopedXi) & 255u;
    const std::uint32_t ScopedY = static_cast<std::uint32_t>(ScopedYi) & 255u;
    const std::uint32_t ScopedZ = static_cast<std::uint32_t>(ScopedZi) & 255u;

    const std::uint32_t ScopedA = MPermutation[ScopedX] + ScopedY;
    const std::uint32_t ScopedAa = MPermutation[ScopedA] + ScopedZ;
    const std::uint32_t ScopedAb = MPermutation[ScopedA + 1u] + ScopedZ;
    const std::uint32_t ScopedB = MPermutation[ScopedX + 1u] + ScopedY;
    const std::uint32_t ScopedBa = MPermutation[ScopedB] + ScopedZ;
    const std::uint32_t ScopedBb = MPermutation[ScopedB + 1u] + ScopedZ;

    const std::uint32_t ScopedP000 = MPermutation[ScopedAa];
    const std::uint32_t ScopedP001 = MPermutation[ScopedAa + 1u];
    const std::uint32_t ScopedP010 = MPermutation[ScopedAb];
    const std::uint32_t ScopedP011 = MPermutation[ScopedAb + 1u];
    const std::uint32_t ScopedP100 = MPermutation[ScopedBa];
    const std::uint32_t ScopedP101 = MPermutation[ScopedBa + 1u];
    const std::uint32_t ScopedP110 = MPermutation[ScopedBb];
    const std::uint32_t ScopedP111 = MPermutation[ScopedBb + 1u];

    const float ScopedN000 = Grad3(ScopedP000, ScopedXf, ScopedYf, ScopedZf);
    const float ScopedN100 = Grad3(ScopedP100, ScopedXf - 1.0f, ScopedYf, ScopedZf);
    const float ScopedN010 = Grad3(ScopedP010, ScopedXf, ScopedYf - 1.0f, ScopedZf);
    const float ScopedN110 = Grad3(ScopedP110, ScopedXf - 1.0f, ScopedYf - 1.0f, ScopedZf);
    const float ScopedN001 = Grad3(ScopedP001, ScopedXf, ScopedYf, ScopedZf - 1.0f);
    const float ScopedN101 = Grad3(ScopedP101, ScopedXf - 1.0f, ScopedYf, ScopedZf - 1.0f);
    const float ScopedN011 = Grad3(ScopedP011, ScopedXf, ScopedYf - 1.0f, ScopedZf - 1.0f);
    const float ScopedN111 = Grad3(ScopedP111, ScopedXf - 1.0f, ScopedYf - 1.0f, ScopedZf - 1.0f);

    const float ScopedX00 = Lerp(ScopedN000, ScopedN100, ScopedU);
    const float ScopedX10 = Lerp(ScopedN010, ScopedN110, ScopedU);
    const float ScopedX01 = Lerp(ScopedN001, ScopedN101, ScopedU);
    const float ScopedX11 = Lerp(ScopedN011, ScopedN111, ScopedU);
    return Lerp(Lerp(ScopedX00, ScopedX10, ScopedV), Lerp(ScopedX01, ScopedX11, ScopedV), ScopedW);
}

// Simplex
float NoiseGenerator::Simplex2D(float InX, float InY) const noexcept
{
    // 0.5 * (sqrt(3) - 1)
    constexpr float ScopedKF2 = 0.36602540378443864676f;
    // (3 - sqrt(3)) / 6
    constexpr float ScopedKG2 = 0.21132486540518711775f;

    const float ScopedSkew = (InX + InY) * ScopedKF2;
    const int ScopedI = FastFloor(InX + ScopedSkew);
    const int ScopedJ = FastFloor(InY + ScopedSkew);
    const float ScopedUnskew = static_cast<float>(ScopedI + ScopedJ) * ScopedKG2;
    const float ScopedX0 = InX - (static_cast<float>(ScopedI) - ScopedUnskew);
    const float ScopedY0 = InY - (static_cast<float>(ScopedJ) - ScopedUnskew);

    const int ScopedI1 = ScopedX0 > ScopedY0 ? 1 : 0;
    const int ScopedJ1 = ScopedX0 > ScopedY0 ? 0 : 1;

    const float ScopedX1 = ScopedX0 - static_cast<float>(ScopedI1) + ScopedKG2;
    const float ScopedY1 = ScopedY0 - static_cast<float>(ScopedJ1) + ScopedKG2;
    const float ScopedX2 = ScopedX0 - 1.0f + 2.0f * ScopedKG2;
    const float ScopedY2 = ScopedY0 - 1.0f + 2.0f * ScopedKG2;

    const std::uint32_t ScopedIi = static_cast<std::uint32_t>(ScopedI) & 255u;
    const std::uint32_t ScopedJj = static_cast<std::uint32_t>(ScopedJ) & 255u;
    const std::uint32_t ScopedP0 = MPermutation[ScopedIi + MPermutation[ScopedJj]];
    const std::uint32_t ScopedP1 = MPermutation[ScopedIi + static_cast<std::uint32_t>(ScopedI1) +
                                                MPermutation[ScopedJj + static_cast<std::uint32_t>(ScopedJ1)]];
    const std::uint32_t ScopedP2 = MPermutation[ScopedIi + 1u + MPermutation[ScopedJj + 1u]];

    const float ScopedN0 = SimplexCorner2(ScopedP0, ScopedX0, ScopedY0);
    const float ScopedN1 = SimplexCorner2(ScopedP1, ScopedX1, ScopedY1);
    const float ScopedN2 = SimplexCorner2(ScopedP2, ScopedX2, ScopedY2);
    return 70.0f * (ScopedN0 + ScopedN1 + ScopedN2);
}

float NoiseGenerator::Simplex3D(float InX, float InY, float InZ) const noexcept
{
    constexpr float ScopedKF3 = 1.0f / 3.0f;
    constexpr float ScopedKG3 = 1.0f / 6.0f;

    const float ScopedSkew = (InX + InY + InZ) * ScopedKF3;
    const int ScopedI = FastFloor(InX + ScopedSkew);
    const int ScopedJ = FastFloor(InY + ScopedSkew);
    const int ScopedK = FastFloor(InZ + ScopedSkew);
    const float ScopedUnskew = static_cast<float>(ScopedI + ScopedJ + ScopedK) * ScopedKG3;
    const float ScopedX0 = InX - (static_cast<float>(ScopedI) - ScopedUnskew);
    const float ScopedY0 = InY - (static_cast<float>(ScopedJ) - ScopedUnskew);
    const float ScopedZ0 = InZ - (static_cast<float>(ScopedK) - ScopedUnskew);

    int ScopedI1 = 0;
    int ScopedJ1 = 0;
    int ScopedK1 = 0;
    int ScopedI2 = 0;
    int ScopedJ2 = 0;
    int ScopedK2 = 0;
    if (ScopedX0 >= ScopedY0)
    {
        if (ScopedY0 >= ScopedZ0)
        {
            ScopedI1 = 1;
            ScopedI2 = 1;
            ScopedJ2 = 1;
        }
        else if (ScopedX0 >= ScopedZ0)
        {
            ScopedI1 = 1;
            ScopedI2 = 1;
            ScopedK2 = 1;
        }
        else
        {
            ScopedK1 = 1;
            ScopedI2 = 1;
            ScopedK2 = 1;
        }
    }
    else
    {
        if (ScopedY0 < ScopedZ0)
        {
            ScopedK1 = 1;
            ScopedJ2 = 1;
            ScopedK2 = 1;
        }
        else if (ScopedX0 < ScopedZ0)
        {
            ScopedJ1 = 1;
            ScopedJ2 = 1;
            ScopedK2 = 1;
        }
        else
        {
            ScopedJ1 = 1;
            ScopedI2 = 1;
            ScopedJ2 = 1;
        }
    }

    const float ScopedX1 = ScopedX0 - static_cast<float>(ScopedI1) + ScopedKG3;
    const float ScopedY1 = ScopedY0 - static_cast<float>(ScopedJ1) + ScopedKG3;
    const float ScopedZ1 = ScopedZ0 - static_cast<float>(ScopedK1) + ScopedKG3;
    const float ScopedX2 = ScopedX0 - static_cast<float>(ScopedI2) + 2.0f * ScopedKG3;
    const float ScopedY2 = ScopedY0 - static_cast<float>(ScopedJ2) + 2.0f * ScopedKG3;
    const float ScopedZ2 = ScopedZ0 - static_cast<float>(ScopedK2) + 2.0f * ScopedKG3;
    const float ScopedX3 = ScopedX0 - 1.0f + 3.0f * ScopedKG3;
    const float ScopedY3 = ScopedY0 - 1.0f + 3.0f * ScopedKG3;
    const float ScopedZ3 = ScopedZ0 - 1.0f + 3.0f * ScopedKG3;

    const std::uint32_t ScopedIi = static_cast<std::uint32_t>(ScopedI) & 255u;
    const std::uint32_t ScopedJj = static_cast<std::uint32_t>(ScopedJ) & 255u;
    const std::uint32_t ScopedKk = static_cast<std::uint32_t>(ScopedK) & 255u;
    const std::uint32_t ScopedP0 = MPermutation[ScopedIi + MPermutation[ScopedJj + MPermutation[ScopedKk]]];
    const std::uint32_t ScopedP1 =
        MPermutation[ScopedIi + static_cast<std::uint32_t>(ScopedI1) +
                     MPermutation[ScopedJj + static_cast<std::uint32_t>(ScopedJ1) +
                                  MPermutation[ScopedKk + static_cast<std::uint32_t>(ScopedK1)]]];
    const std::uint32_t ScopedP2 =
        MPermutation[ScopedIi + static_cast<std::uint32_t>(ScopedI2) +
                     MPermutation[ScopedJj + static_cast<std::uint32_t>(ScopedJ2) +
                                  MPermutation[ScopedKk + static_cast<std::uint32_t>(ScopedK2)]]];
    const std::uint32_t ScopedP3 =
        MPermutation[ScopedIi + 1u + MPermutation[ScopedJj + 1u + MPermutation[ScopedKk + 1u]]];

    const float ScopedN0 = SimplexCorner3(ScopedP0, ScopedX0, ScopedY0, ScopedZ0);
    const float ScopedN1 = SimplexCorner3(ScopedP1, ScopedX1, ScopedY1, ScopedZ1);
    const float ScopedN2 = SimplexCorner3(ScopedP2, ScopedX2, ScopedY2, ScopedZ2);
    const float ScopedN3 = SimplexCorner3(ScopedP3, ScopedX3, ScopedY3, ScopedZ3);
    return 32.0f * (ScopedN0 + ScopedN1 + ScopedN2 + ScopedN3);
}

// Worley / cellular
void NoiseGenerator::Worley2D(float InX, float InY, float& InF1, float& InF2) const noexcept
{
    const int ScopedCellX = FastFloor(InX);
    const int ScopedCellY = FastFloor(InY);
    float ScopedNearest = 1.0e30f;
    float ScopedSecond = 1.0e30f;

    for (int ScopedOffsetY = -1; ScopedOffsetY <= 1; ++ScopedOffsetY)
    {
        for (int ScopedOffsetX = -1; ScopedOffsetX <= 1; ++ScopedOffsetX)
        {
            const int ScopedCx = ScopedCellX + ScopedOffsetX;
            const int ScopedCy = ScopedCellY + ScopedOffsetY;
            const std::uint64_t ScopedHash = HashCell(ScopedCx, ScopedCy, 0, MSeed);
            const float ScopedFeatureX = static_cast<float>(ScopedOffsetX) + Feature01(ScopedHash);
            const float ScopedFeatureY =
                static_cast<float>(ScopedOffsetY) + Feature01(SplitMixFinal(ScopedHash ^ 0x94D049BB133111EBull));
            const float ScopedDx = ScopedFeatureX - (InX - static_cast<float>(ScopedCellX));
            const float ScopedDy = ScopedFeatureY - (InY - static_cast<float>(ScopedCellY));
            const float ScopedDistance = std::sqrt(ScopedDx * ScopedDx + ScopedDy * ScopedDy);
            if (ScopedDistance < ScopedNearest)
            {
                ScopedSecond = ScopedNearest;
                ScopedNearest = ScopedDistance;
            }
            else if (ScopedDistance < ScopedSecond)
            {
                ScopedSecond = ScopedDistance;
            }
        }
    }
    InF1 = ScopedNearest * kInvSqrt2;
    InF2 = ScopedSecond * kInvSqrt2;
}

float NoiseGenerator::Worley2D(float InX, float InY) const noexcept
{
    float ScopedF1 = 0.0f;
    float ScopedF2 = 0.0f;
    Worley2D(InX, InY, ScopedF1, ScopedF2);
    return Clamp01(ScopedF1);
}

float NoiseGenerator::WorleyEdge2D(float InX, float InY) const noexcept
{
    float ScopedF1 = 0.0f;
    float ScopedF2 = 0.0f;
    Worley2D(InX, InY, ScopedF1, ScopedF2);
    return Clamp01(ScopedF2 - ScopedF1);
}

void NoiseGenerator::Worley3D(float InX, float InY, float InZ, float& InF1, float& InF2) const noexcept
{
    const int ScopedCellX = FastFloor(InX);
    const int ScopedCellY = FastFloor(InY);
    const int ScopedCellZ = FastFloor(InZ);
    const float ScopedLocalX = InX - static_cast<float>(ScopedCellX);
    const float ScopedLocalY = InY - static_cast<float>(ScopedCellY);
    const float ScopedLocalZ = InZ - static_cast<float>(ScopedCellZ);
    float ScopedNearest = 1.0e30f;
    float ScopedSecond = 1.0e30f;

    for (int ScopedOffsetZ = -1; ScopedOffsetZ <= 1; ++ScopedOffsetZ)
    {
        for (int ScopedOffsetY = -1; ScopedOffsetY <= 1; ++ScopedOffsetY)
        {
            for (int ScopedOffsetX = -1; ScopedOffsetX <= 1; ++ScopedOffsetX)
            {
                const std::uint64_t ScopedHash = HashCell(ScopedCellX + ScopedOffsetX, ScopedCellY + ScopedOffsetY,
                                                          ScopedCellZ + ScopedOffsetZ, MSeed);
                const std::uint64_t ScopedHashY = SplitMixFinal(ScopedHash ^ 0x94D049BB133111EBull);
                const std::uint64_t ScopedHashZ = SplitMixFinal(ScopedHashY ^ 0xD6E8FEB86659FD93ull);
                const float ScopedDx = static_cast<float>(ScopedOffsetX) + Feature01(ScopedHash) - ScopedLocalX;
                const float ScopedDy = static_cast<float>(ScopedOffsetY) + Feature01(ScopedHashY) - ScopedLocalY;
                const float ScopedDz = static_cast<float>(ScopedOffsetZ) + Feature01(ScopedHashZ) - ScopedLocalZ;
                const float ScopedDistance = std::sqrt(ScopedDx * ScopedDx + ScopedDy * ScopedDy + ScopedDz * ScopedDz);
                if (ScopedDistance < ScopedNearest)
                {
                    ScopedSecond = ScopedNearest;
                    ScopedNearest = ScopedDistance;
                }
                else if (ScopedDistance < ScopedSecond)
                {
                    ScopedSecond = ScopedDistance;
                }
            }
        }
    }
    InF1 = ScopedNearest * kInvSqrt3;
    InF2 = ScopedSecond * kInvSqrt3;
}

float NoiseGenerator::Worley3D(float InX, float InY, float InZ) const noexcept
{
    float ScopedF1 = 0.0f;
    float ScopedF2 = 0.0f;
    Worley3D(InX, InY, InZ, ScopedF1, ScopedF2);
    return Clamp01(ScopedF1);
}

float NoiseGenerator::WorleyEdge3D(float InX, float InY, float InZ) const noexcept
{
    float ScopedF1 = 0.0f;
    float ScopedF2 = 0.0f;
    Worley3D(InX, InY, InZ, ScopedF1, ScopedF2);
    return Clamp01(ScopedF2 - ScopedF1);
}

// fBm
float NoiseGenerator::Fbm2D(float InX, float InY, int InOctaves, float InLacunarity, float InGain) const noexcept
{
    if (InOctaves <= 0)
    {
        return 0.0f;
    }
    float ScopedSum = 0.0f;
    float ScopedAmplitude = 1.0f;
    float ScopedFrequency = 1.0f;
    float ScopedNormalization = 0.0f;
    for (int ScopedOctave = 0; ScopedOctave < InOctaves; ++ScopedOctave)
    {
        ScopedSum += ScopedAmplitude * Simplex2D(InX * ScopedFrequency, InY * ScopedFrequency);
        ScopedNormalization += ScopedAmplitude;
        ScopedFrequency *= InLacunarity;
        ScopedAmplitude *= InGain;
    }
    return ScopedNormalization > 0.0f ? ScopedSum / ScopedNormalization : 0.0f;
}

float NoiseGenerator::Fbm3D(float InX, float InY, float InZ, int InOctaves, float InLacunarity,
                            float InGain) const noexcept
{
    if (InOctaves <= 0)
    {
        return 0.0f;
    }
    float ScopedSum = 0.0f;
    float ScopedAmplitude = 1.0f;
    float ScopedFrequency = 1.0f;
    float ScopedNormalization = 0.0f;
    for (int ScopedOctave = 0; ScopedOctave < InOctaves; ++ScopedOctave)
    {
        ScopedSum += ScopedAmplitude * Simplex3D(InX * ScopedFrequency, InY * ScopedFrequency, InZ * ScopedFrequency);
        ScopedNormalization += ScopedAmplitude;
        ScopedFrequency *= InLacunarity;
        ScopedAmplitude *= InGain;
    }
    return ScopedNormalization > 0.0f ? ScopedSum / ScopedNormalization : 0.0f;
}
}
