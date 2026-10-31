// FemboyEngine2D - RNG and procedural noise acceptance test.
#include "Engine/Core/DeterministicRng.hpp"
#include "Engine/Core/NoiseGenerator.hpp"
#include "Engine/Core/Pcg32.hpp"
#include "Engine/Core/SecureRng.hpp"

#include <array>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <span>

namespace
{
int GFailures = 0;
std::atomic<std::size_t> GAllocations{0};

void Check(bool InCondition, const char* InMessage)
{
    if (!InCondition)
    {
        std::fprintf(stderr, "FAIL: %s\n", InMessage);
        ++GFailures;
    }
}

constexpr std::uint32_t FirstReferenceWord()
{
    Fby::Pcg32 ScopedRng = Fby::Pcg32::FromSeed(42u, 54u);
    return ScopedRng.NextU32();
}

static_assert(FirstReferenceWord() == 0xA15C02B7u, "PCG32 seeding must follow the reference sequence");
static_assert(Fby::DeterministicRng::kMultiplier == 6364136223846793005ull);
static_assert(sizeof(Fby::RngState) == 16);
static_assert(sizeof(Fby::Pcg32) == 16);
}

// Count every heap allocation in the executable so the noise hot path can be
void* operator new(std::size_t InSize)
{
    GAllocations.fetch_add(1, std::memory_order_relaxed);
    void* ScopedMemory = std::malloc(InSize == 0 ? 1 : InSize);
    if (ScopedMemory == nullptr)
    {
        throw std::bad_alloc{};
    }
    return ScopedMemory;
}

void* operator new[](std::size_t InSize)
{
    GAllocations.fetch_add(1, std::memory_order_relaxed);
    void* ScopedMemory = std::malloc(InSize == 0 ? 1 : InSize);
    if (ScopedMemory == nullptr)
    {
        throw std::bad_alloc{};
    }
    return ScopedMemory;
}

void* operator new(std::size_t InSize, const std::nothrow_t&) noexcept
{
    GAllocations.fetch_add(1, std::memory_order_relaxed);
    return std::malloc(InSize == 0 ? 1 : InSize);
}

void* operator new[](std::size_t InSize, const std::nothrow_t&) noexcept
{
    GAllocations.fetch_add(1, std::memory_order_relaxed);
    return std::malloc(InSize == 0 ? 1 : InSize);
}

void operator delete(void* InMemory) noexcept
{
    std::free(InMemory);
}

void operator delete[](void* InMemory) noexcept
{
    std::free(InMemory);
}

void operator delete(void* InMemory, std::size_t) noexcept
{
    std::free(InMemory);
}

void operator delete[](void* InMemory, std::size_t) noexcept
{
    std::free(InMemory);
}

void operator delete(void* InMemory, const std::nothrow_t&) noexcept
{
    std::free(InMemory);
}

void operator delete[](void* InMemory, const std::nothrow_t&) noexcept
{
    std::free(InMemory);
}

namespace
{
using Fby::Pcg32;
using Fby::RngState;
using Fby::SecureRng;
using Fby::Noise::NoiseGenerator;

void TestPcg32ReferenceVectors()
{
    constexpr std::array<std::uint32_t, 6> ScopedExpected{0xA15C02B7u, 0x7B47F409u, 0xBA1D3330u,
                                                          0x83D2F293u, 0xBFA4784Bu, 0xCBED606Eu};
    Pcg32 ScopedRng = Pcg32::FromSeed(42u, 54u);
    for (std::size_t ScopedI = 0; ScopedI < ScopedExpected.size(); ++ScopedI)
    {
        const std::uint32_t ScopedWord = ScopedRng.NextU32();
        if (ScopedWord != ScopedExpected[ScopedI])
        {
            std::fprintf(stderr, "PCG32 vector[%zu] = 0x%08X, expected 0x%08X\n", ScopedI, ScopedWord,
                         ScopedExpected[ScopedI]);
            ++GFailures;
        }
    }
}

void TestPcg32StateAndFork()
{
    Pcg32 ScopedFirst = Pcg32::FromSeed(0xDEADBEEFull, 7u);
    for (int ScopedI = 0; ScopedI < 37; ++ScopedI)
    {
        (void)ScopedFirst.NextU32();
    }
    const RngState ScopedSaved = ScopedFirst.Save();

    std::array<std::uint32_t, 16> ScopedOriginal{};
    for (std::uint32_t& ScopedWord : ScopedOriginal)
    {
        ScopedWord = ScopedFirst.NextU32();
    }
    ScopedFirst.Restore(ScopedSaved);
    for (const std::uint32_t ScopedExpected : ScopedOriginal)
    {
        Check(ScopedFirst.NextU32() == ScopedExpected, "restore resumes the exact PCG32 word stream");
    }

    Pcg32 ScopedParentA = Pcg32::FromSeed(1234u, 9u);
    Pcg32 ScopedParentB = Pcg32::FromSeed(1234u, 9u);
    Pcg32 ScopedChildA = ScopedParentA.Fork();
    Pcg32 ScopedChildB = ScopedParentB.Fork();
    Check(ScopedParentA.Save() == ScopedParentB.Save(), "fork advances the parent deterministically");
    for (int ScopedI = 0; ScopedI < 32; ++ScopedI)
    {
        Check(ScopedChildA.NextU32() == ScopedChildB.NextU32(), "forked child streams are identical");
        Check(ScopedParentA.NextU32() == ScopedParentB.NextU32(), "parents stay identical after fork");
    }

    Pcg32 ScopedOtherStream = Pcg32::FromSeed(1234u, 10u);
    std::uint32_t ScopedDiffering = 0;
    Pcg32 ScopedA = Pcg32::FromSeed(99u, 1u);
    Pcg32 ScopedB = Pcg32::FromSeed(99u, 2u);
    for (int ScopedI = 0; ScopedI < 64; ++ScopedI)
    {
        ScopedDiffering += ScopedA.NextU32() != ScopedB.NextU32() ? 1u : 0u;
    }
    Check(ScopedDiffering > 60, "different PCG streams diverge immediately");
    (void)ScopedOtherStream;

    Pcg32 ScopedBounded = Pcg32::FromSeed(5u, 5u);
    for (int ScopedI = 0; ScopedI < 10000; ++ScopedI)
    {
        Check(ScopedBounded.Bounded(7u) < 7u, "Bounded stays inside the bound");
        Check(ScopedBounded.Range(9u) < 9u, "Range stays inside the bound");
        const float ScopedValue = ScopedBounded.NextFloat();
        Check(ScopedValue >= 0.0f && ScopedValue < 1.0f, "NextFloat is in [0,1)");
    }

    Pcg32 ScopedLowRng = Pcg32::FromSeed(17u, 3u);
    Pcg32 ScopedHighRng = ScopedLowRng;
    Check(ScopedLowRng.NextU64() == ((static_cast<std::uint64_t>(ScopedHighRng.NextU32()) |
                                      (static_cast<std::uint64_t>(ScopedHighRng.NextU32()) << 32u))),
          "NextU64 is low word first");
}

void TestSecureRng()
{
    Check(SecureRng::Supported(), "SecureRng reports an OS backend on this platform");
    if (!SecureRng::Supported())
    {
        return;
    }
    std::array<std::byte, 64> ScopedFirst{};
    std::array<std::byte, 64> ScopedSecond{};
    Check(SecureRng::Fill(ScopedFirst), "SecureRng fills from the OS");
    Check(SecureRng::Fill(ScopedSecond), "SecureRng fills a second buffer");
    Check(std::memcmp(ScopedFirst.data(), ScopedSecond.data(), ScopedFirst.size()) != 0, "two OS entropy reads differ");

    std::uint64_t ScopedWord = 0;
    Check(SecureRng::NextU64(ScopedWord), "SecureRng produces a 64-bit word");
    Check(ScopedWord != 0, "a 64-bit OS word is not all zero");
    Check(!SecureRng::Fill(nullptr, 16), "SecureRng rejects a null destination");
    Check(SecureRng::Fill(nullptr, 0), "SecureRng accepts an empty request");

    std::array<char, 33> ScopedToken{};
    Check(SecureRng::TokenHex(ScopedToken), "SecureRng formats a token");
    ScopedToken[ScopedToken.size() - 1] = '\0';
    bool ScopedHex = true;
    for (std::size_t ScopedI = 0; ScopedI + 1 < ScopedToken.size(); ++ScopedI)
    {
        const char ScopedCharacter = ScopedToken[ScopedI];
        ScopedHex = ScopedHex && ((ScopedCharacter >= '0' && ScopedCharacter <= '9') ||
                                  (ScopedCharacter >= 'a' && ScopedCharacter <= 'f'));
    }
    Check(ScopedHex, "token contains only lower-case hexadecimal digits");
}

void TestPcg32SnapshotState()
{
    Pcg32 ScopedRng = Pcg32::FromSeed(0xABCDEF01u, 0x1234u);
    for (int ScopedI = 0; ScopedI < 100; ++ScopedI)
    {
        (void)ScopedRng.NextU64();
    }
    const RngState ScopedState = ScopedRng.Save();
    Check(ScopedState.state != 0, "snapshot RNG state is populated");
    Check((ScopedState.inc & 1u) == 1u, "PCG increment is odd");
    Pcg32 ScopedRestored;
    ScopedRestored.Restore(ScopedState);
    for (int ScopedI = 0; ScopedI < 100; ++ScopedI)
    {
        Check(ScopedRestored.NextU32() == ScopedRng.NextU32(), "restored RNG matches after 100 draws");
    }
}

void TestNoiseDeterminism()
{
    NoiseGenerator ScopedFirst(0x5EEDu);
    NoiseGenerator ScopedSecond(0x5EEDu);
    for (int ScopedY = -16; ScopedY <= 16; ++ScopedY)
    {
        for (int ScopedX = -16; ScopedX <= 16; ++ScopedX)
        {
            const float ScopedPx = static_cast<float>(ScopedX) * 0.31f;
            const float ScopedPy = static_cast<float>(ScopedY) * 0.27f;
            Check(ScopedFirst.Perlin2D(ScopedPx, ScopedPy) == ScopedSecond.Perlin2D(ScopedPx, ScopedPy),
                  "Perlin2D is deterministic");
            Check(ScopedFirst.Simplex2D(ScopedPx, ScopedPy) == ScopedSecond.Simplex2D(ScopedPx, ScopedPy),
                  "Simplex2D is deterministic");
            Check(ScopedFirst.Worley2D(ScopedPx, ScopedPy) == ScopedSecond.Worley2D(ScopedPx, ScopedPy),
                  "Worley2D is deterministic");
            Check(ScopedFirst.Perlin3D(ScopedPx, ScopedPy, 0.5f) == ScopedSecond.Perlin3D(ScopedPx, ScopedPy, 0.5f),
                  "Perlin3D is deterministic");
            Check(ScopedFirst.Simplex3D(ScopedPx, ScopedPy, 0.5f) == ScopedSecond.Simplex3D(ScopedPx, ScopedPy, 0.5f),
                  "Simplex3D is deterministic");
            Check(ScopedFirst.Worley3D(ScopedPx, ScopedPy, 0.5f) == ScopedSecond.Worley3D(ScopedPx, ScopedPy, 0.5f),
                  "Worley3D is deterministic");
        }
    }

    NoiseGenerator ScopedDifferent(0x5EEEu);
    std::uint32_t ScopedDifferences = 0;
    for (int ScopedI = 0; ScopedI < 256; ++ScopedI)
    {
        const float ScopedX = static_cast<float>(ScopedI) * 0.137f;
        const float ScopedY = static_cast<float>(ScopedI) * 0.071f;
        ScopedDifferences +=
            ScopedFirst.Perlin2D(ScopedX, ScopedY) != ScopedDifferent.Perlin2D(ScopedX, ScopedY) ? 1u : 0u;
        ScopedDifferences +=
            ScopedFirst.Simplex2D(ScopedX, ScopedY) != ScopedDifferent.Simplex2D(ScopedX, ScopedY) ? 1u : 0u;
        ScopedDifferences +=
            ScopedFirst.Worley2D(ScopedX, ScopedY) != ScopedDifferent.Worley2D(ScopedX, ScopedY) ? 1u : 0u;
    }
    Check(ScopedDifferences > 700, "different seeds produce different fields");
}

void TestNoiseBoundsAndContinuity()
{
    NoiseGenerator ScopedNoise(0xC0FFEEu);
    float ScopedPerlinMin = 1.0f;
    float ScopedPerlinMax = -1.0f;
    float ScopedSimplexMin = 1.0f;
    float ScopedSimplexMax = -1.0f;
    float ScopedWorleyMin = 1.0f;
    float ScopedWorleyMax = 0.0f;

    for (int ScopedY = -128; ScopedY <= 128; ++ScopedY)
    {
        for (int ScopedX = -128; ScopedX <= 128; ++ScopedX)
        {
            const float ScopedPx = static_cast<float>(ScopedX) * 0.093f;
            const float ScopedPy = static_cast<float>(ScopedY) * 0.081f;

            const float ScopedPerlin = ScopedNoise.Perlin2D(ScopedPx, ScopedPy);
            ScopedPerlinMin = std::min(ScopedPerlinMin, ScopedPerlin);
            ScopedPerlinMax = std::max(ScopedPerlinMax, ScopedPerlin);
            Check(std::isfinite(ScopedPerlin), "Perlin2D is finite");
            Check(ScopedPerlin >= -1.05f && ScopedPerlin <= 1.05f, "Perlin2D is bounded");

            const float ScopedSimplex = ScopedNoise.Simplex2D(ScopedPx, ScopedPy);
            ScopedSimplexMin = std::min(ScopedSimplexMin, ScopedSimplex);
            ScopedSimplexMax = std::max(ScopedSimplexMax, ScopedSimplex);
            Check(std::isfinite(ScopedSimplex), "Simplex2D is finite");
            Check(ScopedSimplex >= -1.05f && ScopedSimplex <= 1.05f, "Simplex2D is bounded");

            const float ScopedWorley = ScopedNoise.Worley2D(ScopedPx, ScopedPy);
            ScopedWorleyMin = std::min(ScopedWorleyMin, ScopedWorley);
            ScopedWorleyMax = std::max(ScopedWorleyMax, ScopedWorley);
            Check(ScopedWorley >= 0.0f && ScopedWorley <= 1.0f, "Worley2D is normalized");

            const float ScopedPerlin3 = ScopedNoise.Perlin3D(ScopedPx, ScopedPy, ScopedPx * 0.5f);
            const float ScopedSimplex3 = ScopedNoise.Simplex3D(ScopedPx, ScopedPy, ScopedPx * 0.5f);
            const float ScopedWorley3 = ScopedNoise.Worley3D(ScopedPx, ScopedPy, ScopedPx * 0.5f);
            Check(std::isfinite(ScopedPerlin3) && ScopedPerlin3 >= -1.05f && ScopedPerlin3 <= 1.05f,
                  "Perlin3D is bounded");
            Check(std::isfinite(ScopedSimplex3) && ScopedSimplex3 >= -1.05f && ScopedSimplex3 <= 1.05f,
                  "Simplex3D is bounded");
            Check(ScopedWorley3 >= 0.0f && ScopedWorley3 <= 1.0f, "Worley3D is normalized");
        }
    }
    Check(ScopedPerlinMax - ScopedPerlinMin > 0.5f, "Perlin2D has a useful dynamic range");
    Check(ScopedSimplexMax - ScopedSimplexMin > 0.5f, "Simplex2D has a useful dynamic range");
    Check(ScopedWorleyMax - ScopedWorleyMin > 0.3f, "Worley2D has a useful dynamic range");

    for (int ScopedY = -8; ScopedY <= 8; ++ScopedY)
    {
        for (int ScopedX = -8; ScopedX <= 8; ++ScopedX)
        {
            Check(std::fabs(ScopedNoise.Perlin2D(static_cast<float>(ScopedX), static_cast<float>(ScopedY))) < 1.0e-5f,
                  "Perlin2D is zero at every lattice point");
            Check(std::fabs(ScopedNoise.Perlin3D(static_cast<float>(ScopedX), static_cast<float>(ScopedY), 3.0f)) <
                      1.0e-5f,
                  "Perlin3D is zero at every lattice point");
        }
    }

    float ScopedPrevious = ScopedNoise.Simplex2D(0.0f, 0.0f);
    for (int ScopedI = 1; ScopedI <= 2048; ++ScopedI)
    {
        const float ScopedX = static_cast<float>(ScopedI) * 0.001f;
        const float ScopedCurrent = ScopedNoise.Simplex2D(ScopedX, 0.37f);
        Check(std::fabs(ScopedCurrent - ScopedPrevious) < 0.6f, "Simplex2D is continuous across a 0.001 step");
        ScopedPrevious = ScopedCurrent;
    }

    float ScopedF1 = 0.0f;
    float ScopedF2 = 0.0f;
    ScopedNoise.Worley2D(0.3f, 0.8f, ScopedF1, ScopedF2);
    Check(ScopedF1 >= 0.0f && ScopedF2 >= ScopedF1 && ScopedF2 <= 1.0f, "Worley F1/F2 ordering holds");
    Check(ScopedNoise.WorleyEdge2D(0.3f, 0.8f) >= 0.0f, "Worley edge field is non-negative");
    Check(ScopedNoise.WorleyEdge3D(0.3f, 0.8f, 0.2f) >= 0.0f, "Worley3D edge field is non-negative");

    float ScopedFbmMin = 1.0f;
    float ScopedFbmMax = -1.0f;
    for (int ScopedI = 0; ScopedI < 512; ++ScopedI)
    {
        const float ScopedValue = ScopedNoise.Fbm2D(static_cast<float>(ScopedI) * 0.031f, 1.7f, 5, 2.0f, 0.5f);
        ScopedFbmMin = std::min(ScopedFbmMin, ScopedValue);
        ScopedFbmMax = std::max(ScopedFbmMax, ScopedValue);
        Check(std::isfinite(ScopedValue) && ScopedValue >= -1.05f && ScopedValue <= 1.05f, "Fbm2D is bounded");
        Check(ScopedNoise.Fbm3D(static_cast<float>(ScopedI) * 0.013f, 0.2f, 0.7f, 0) == 0.0f,
              "zero-octave fBm is zero");
    }
    Check(ScopedFbmMax - ScopedFbmMin > 0.1f, "Fbm2D has a useful range");
}

void TestNoiseAllocationFree()
{
    NoiseGenerator ScopedNoise(0x1234u);
    volatile float ScopedSink = 0.0f;
    const std::size_t ScopedBefore = GAllocations.load(std::memory_order_relaxed);
    for (int ScopedI = 0; ScopedI < 1024; ++ScopedI)
    {
        const float ScopedX = static_cast<float>(ScopedI) * 0.013f;
        ScopedSink = ScopedSink + ScopedNoise.Perlin2D(ScopedX, 0.5f);
        ScopedSink = ScopedSink + ScopedNoise.Perlin3D(ScopedX, 0.5f, 0.25f);
        ScopedSink = ScopedSink + ScopedNoise.Simplex2D(ScopedX, 0.5f);
        ScopedSink = ScopedSink + ScopedNoise.Simplex3D(ScopedX, 0.5f, 0.25f);
        ScopedSink = ScopedSink + ScopedNoise.Worley2D(ScopedX, 0.5f);
        ScopedSink = ScopedSink + ScopedNoise.Worley3D(ScopedX, 0.5f, 0.25f);
        ScopedSink = ScopedSink + ScopedNoise.WorleyEdge2D(ScopedX, 0.5f);
        ScopedSink = ScopedSink + ScopedNoise.Fbm2D(ScopedX, 0.5f, 4);
        ScopedSink = ScopedSink + ScopedNoise.Fbm3D(ScopedX, 0.5f, 0.25f, 4);
    }
    const std::size_t ScopedAfter = GAllocations.load(std::memory_order_relaxed);
    Check(ScopedAfter == ScopedBefore, "noise sampling performs zero heap allocations");
    (void)ScopedSink;
}

void TestRngAllocationFree()
{
    Pcg32 ScopedRng = Pcg32::FromSeed(7u, 7u);
    const std::size_t ScopedBefore = GAllocations.load(std::memory_order_relaxed);
    std::uint64_t ScopedSink = 0;
    for (int ScopedI = 0; ScopedI < 100000; ++ScopedI)
    {
        ScopedSink += ScopedRng.NextU32();
        ScopedSink += ScopedRng.NextU64();
        ScopedSink += ScopedRng.Bounded(1000u);
        ScopedSink += static_cast<std::uint64_t>(ScopedRng.NextFloat() * 1000.0f);
    }
    const std::size_t ScopedAfter = GAllocations.load(std::memory_order_relaxed);
    Check(ScopedAfter == ScopedBefore, "PCG32 draws perform zero heap allocations");
    (void)ScopedSink;
}
}

int main()
{
    TestPcg32ReferenceVectors();
    TestPcg32StateAndFork();
    TestSecureRng();
    TestPcg32SnapshotState();
    TestNoiseDeterminism();
    TestNoiseBoundsAndContinuity();
    TestNoiseAllocationFree();
    TestRngAllocationFree();

    if (GFailures != 0)
    {
        std::fprintf(stderr, "rng/noise: %d failure(s)\n", GFailures);
        return 1;
    }
    std::printf("rng/noise: OK (PCG32 vectors, secure entropy, Perlin/Simplex/Worley)\n");
    return 0;
}
