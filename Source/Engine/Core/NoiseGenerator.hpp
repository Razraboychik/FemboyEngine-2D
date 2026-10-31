// FemboyEngine2D - allocation-free procedural noise.
#pragma once

#include <array>
#include <cstdint>

namespace Fby::Noise
{

class NoiseGenerator
{
public:
    // Default seed is the PCG stream constant, so a default-constructed
    explicit NoiseGenerator(std::uint64_t InSeed = 0x9E3779B97F4A7C15ull) noexcept;
    NoiseGenerator(const NoiseGenerator&) = default;
    NoiseGenerator& operator=(const NoiseGenerator&) = default;

    void Reseed(std::uint64_t InSeed) noexcept;
    [[nodiscard]] std::uint64_t Seed() const noexcept
    {
        return MSeed;
    }

    // Classic improved Perlin noise. Zero at every integer lattice point and
    [[nodiscard]] float Perlin2D(float InX, float InY) const noexcept;
    [[nodiscard]] float Perlin3D(float InX, float InY, float InZ) const noexcept;

    // 2D/3D simplex noise, roughly in [-1, 1]. Cheaper than Perlin for 3D and
    [[nodiscard]] float Simplex2D(float InX, float InY) const noexcept;
    [[nodiscard]] float Simplex3D(float InX, float InY, float InZ) const noexcept;

    // Worley/cellular noise. F1 is the distance to the nearest feature point,
    [[nodiscard]] float Worley2D(float InX, float InY) const noexcept;
    [[nodiscard]] float Worley3D(float InX, float InY, float InZ) const noexcept;
    [[nodiscard]] float WorleyEdge2D(float InX, float InY) const noexcept;
    [[nodiscard]] float WorleyEdge3D(float InX, float InY, float InZ) const noexcept;
    void Worley2D(float InX, float InY, float& InF1, float& InF2) const noexcept;
    void Worley3D(float InX, float InY, float InZ, float& InF1, float& InF2) const noexcept;

    // Fractal Brownian motion over the chosen base field. Normalized so the
    [[nodiscard]] float Fbm2D(float InX, float InY, int InOctaves, float InLacunarity = 2.0f,
                              float InGain = 0.5f) const noexcept;
    [[nodiscard]] float Fbm3D(float InX, float InY, float InZ, int InOctaves, float InLacunarity = 2.0f,
                              float InGain = 0.5f) const noexcept;

    [[nodiscard]] const std::array<std::uint8_t, 512>& Permutation() const noexcept
    {
        return MPermutation;
    }

private:
    void BuildPermutation(std::uint64_t InSeed) noexcept;

    std::array<std::uint8_t, 512> MPermutation{};
    std::uint64_t MSeed = 0;
};

// Shared math helpers. Exposed because the shader mirror tests them against the
[[nodiscard]] constexpr float Fade(float InT) noexcept
{
    return InT * InT * InT * (InT * (InT * 6.0f - 15.0f) + 10.0f);
}

[[nodiscard]] constexpr float Lerp(float InA, float InB, float InT) noexcept
{
    return InA + (InB - InA) * InT;
}
}
