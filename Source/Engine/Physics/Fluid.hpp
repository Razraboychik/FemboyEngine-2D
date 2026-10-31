// FemboyEngine2D - two water models in one system.
#pragma once

#include "Engine/Core/Types.hpp"
#include "Engine/ECS/Components.hpp"
#include "Engine/ECS/Schedule.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

#if defined(__cpp_lib_mdspan) && __cpp_lib_mdspan >= 202207L
#include <mdspan>
#define FBY_FLUID_HAS_MDSPAN 1
#else
#define FBY_FLUID_HAS_MDSPAN 0
#endif

namespace Fby
{

// Gerstner / trochoidal waves
struct GerstnerWave
{
    // Will be normalized by the field
    Vector2D direction{1.0f, 0.0f};
    // 0..1, sum should stay below 1
    float steepness = 0.12f;
    // Pixels
    float wavelength = 48.0f;
    // Multiplier on the deep-water phase speed
    float speed = 1.0f;
    // Radians
    float phase = 0.0f;
};

struct GerstnerSample
{
    // Horizontal particle displacement, pixels
    Vector2D displacement{};
    // Vertical offset above the mean surface, pixels
    float height = 0.0f;
    // < 0.65 means a crest that should get foam
    float jacobian = 1.0f;
};

class GerstnerField
{
public:
    static constexpr std::uint32_t kMaxWaves = 8;
    // Pixels / s^2
    static constexpr float kGravity = 980.0f;

    void Clear() noexcept;
    // Copies up to kMaxWaves entries; returns how many were accepted.
    std::uint32_t SetWaves(std::span<const GerstnerWave> InWaves) noexcept;
    [[nodiscard]] std::uint32_t WaveCount() const noexcept
    {
        return MWaveCount;
    }
    [[nodiscard]] std::span<const GerstnerWave> Waves() const noexcept
    {
        return {MWaves.data(), MWaveCount};
    }

    [[nodiscard]] GerstnerSample Sample(Vector2D InBase, float InTimeSeconds) const noexcept;
    [[nodiscard]] float Height(Vector2D InBase, float InTimeSeconds) const noexcept;

private:
    std::array<GerstnerWave, kMaxWaves> MWaves{};
    std::uint32_t MWaveCount = 0;
};

// Shallow water grid
inline constexpr std::uint32_t kMaxWaterColumns = 64;
inline constexpr std::uint32_t kMaxWaterRows = 64;
inline constexpr std::uint32_t kMaxWaterCells = kMaxWaterColumns * kMaxWaterRows;

#if FBY_FLUID_HAS_MDSPAN
using WaterView = std::mdspan<float, std::dextents<std::size_t, 2>>;
using WaterConstView = std::mdspan<const float, std::dextents<std::size_t, 2>>;
#else
class WaterView
{
public:
    constexpr WaterView() = default;
    constexpr WaterView(float* data, std::size_t rows, std::size_t columns) noexcept
        : MData(data), MRows(rows), MColumns(columns)
    {
    }
    [[nodiscard]] constexpr std::size_t extent(std::size_t axis) const noexcept
    {
        return axis == 0 ? MRows : MColumns;
    }
    [[nodiscard]] constexpr float& operator()(std::size_t row, std::size_t column) const noexcept
    {
        return MData[row * MColumns + column];
    }
    [[nodiscard]] constexpr float* data() const noexcept
    {
        return MData;
    }

private:
    float* MData = nullptr;
    std::size_t MRows = 0;
    std::size_t MColumns = 0;
};
using WaterConstView = WaterView;
#endif

struct FluidDesc
{
    // World position of cell (0, 0), pixels
    Vector2D origin{0.0f, 0.0f};
    Vector2D cellSize{16.0f, 16.0f};
    std::uint32_t columns = 32;
    std::uint32_t rows = 32;
    // H, pixels
    float baseDepth = 6.0f;
    // Pixels / s^2
    float gravity = 980.0f;
    // Velocity damping per step
    float damping = 0.995f;
    // Clamp for numerical safety
    float maxHeight = 64.0f;
    // In cells
    float dropRadius = 2.2f;
    float dropStrength = 1.0f;
};

class ShallowWaterGrid
{
public:
    [[nodiscard]] bool Configure(const FluidDesc& InDesc) noexcept;
    void Clear() noexcept;
    [[nodiscard]] bool IsConfigured() const noexcept
    {
        return MColumns > 0 && MRows > 0;
    }

    [[nodiscard]] const FluidDesc& Desc() const noexcept
    {
        return MDesc;
    }
    [[nodiscard]] std::uint32_t Columns() const noexcept
    {
        return MColumns;
    }
    [[nodiscard]] std::uint32_t Rows() const noexcept
    {
        return MRows;
    }

    // Explicit linearised shallow-water update. dt is clamped internally to
    void Step(float InDt) noexcept;

    // Injects a Gaussian height/velocity impulse at a world position. Used by
    void AddDrop(Vector2D InWorldPosition, float InRadiusCells, float InStrength) noexcept;

    [[nodiscard]] float SampleHeight(Vector2D InWorldPosition) const noexcept;
    [[nodiscard]] float SampleVelocityY(Vector2D InWorldPosition) const noexcept;

    [[nodiscard]] WaterView HeightView() noexcept
    {
#if FBY_FLUID_HAS_MDSPAN
        return WaterView(MHeight.data(), MRows, MColumns);
#else
        return WaterView(MHeight.data(), MRows, MColumns);
#endif
    }
    [[nodiscard]] WaterConstView HeightView() const noexcept
    {
#if FBY_FLUID_HAS_MDSPAN
        return WaterConstView(MHeight.data(), MRows, MColumns);
#else
        return WaterConstView(const_cast<float*>(MHeight.data()), MRows, MColumns);
#endif
    }
    [[nodiscard]] WaterView VelocityXView() noexcept
    {
        return WaterView(MVelocityX.data(), MRows, MColumns);
    }
    [[nodiscard]] WaterView VelocityYView() noexcept
    {
        return WaterView(MVelocityY.data(), MRows, MColumns);
    }

    // Copies row-major heights into a caller-owned R32F upload buffer. Returns
    [[nodiscard]] std::uint32_t CopyHeightField(std::span<float> InDestination) const noexcept;

private:
    [[nodiscard]] float CellHeight(std::uint32_t InX, std::uint32_t InY) const noexcept;
    [[nodiscard]] Vector2D WorldToCell(Vector2D InWorldPosition) const noexcept;

    FluidDesc MDesc{};
    std::uint32_t MColumns = 0;
    std::uint32_t MRows = 0;
    std::array<float, kMaxWaterCells> MHeight{};
    std::array<float, kMaxWaterCells> MHeightNext{};
    std::array<float, kMaxWaterCells> MVelocityX{};
    std::array<float, kMaxWaterCells> MVelocityY{};
};

// Add to a falling object. FluidWaveSystem watches Transform2D + RigidBody2D
struct FluidInteractorComponent
{
    float RadiusCells = 2.2f;
    float Mass = 1.0f;
    float SplashStrength = 1.0f;
    std::uint32_t Flags = 0;
};

static_assert(std::is_trivially_copyable_v<FluidInteractorComponent>);

// FluidWaveSystem
class FluidWaveSystem
{
public:
    static constexpr std::uint32_t kMaxTrackedInteractors = 64;

    void Initialize(const FluidDesc& InDesc = {}) noexcept;
    void Shutdown() noexcept;

    [[nodiscard]] GerstnerField& Gerstner() noexcept
    {
        return MGerstner;
    }
    [[nodiscard]] const GerstnerField& Gerstner() const noexcept
    {
        return MGerstner;
    }
    [[nodiscard]] ShallowWaterGrid& Water() noexcept
    {
        return MWater;
    }
    [[nodiscard]] const ShallowWaterGrid& Water() const noexcept
    {
        return MWater;
    }

    // Feeds new falling bodies into the grid, then advances the field. Safe to
    void Update(Ecs::Registry& InRegistry, float InDt) noexcept;

private:
    struct Tracked
    {
        Entity entity = entt::null;
        bool inside = false;
    };

    [[nodiscard]] Tracked* FindTracked(Entity InEntity) noexcept;
    void AddTracked(Entity InEntity) noexcept;

    GerstnerField MGerstner{};
    ShallowWaterGrid MWater{};
    std::array<Tracked, kMaxTrackedInteractors> MTracked{};
    std::uint32_t MTrackedCount = 0;
};
}

FBY_REFLECT_COMPONENT(Fby::FluidInteractorComponent, "FluidInteractorComponent",
                      FBY_FIELD(Fby::FluidInteractorComponent, RadiusCells, Float),
                      FBY_FIELD(Fby::FluidInteractorComponent, Mass, Float),
                      FBY_FIELD(Fby::FluidInteractorComponent, SplashStrength, Float),
                      FBY_FIELD(Fby::FluidInteractorComponent, Flags, UInt32))
