// FemboyEngine2D - configurable 2D particle effects.
#pragma once

#include "Engine/Core/Pcg32.hpp"
#include "Engine/Core/Types.hpp"
#include "Engine/RHI/RenderItem.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

namespace Fby
{

inline constexpr std::uint32_t kParticleEffectMaxParticles = 8192;
inline constexpr std::uint32_t kParticleEffectMaxEmitters = 64;
inline constexpr std::uint32_t kParticleShapeCount = 12;
inline constexpr std::uint32_t kInvalidParticleEmitter = 0xFFFFFFFFu;

// Shape slots are packed into the game's white-mask atlas. The engine never
// ships textures: it selects a slot, the game provides the mask layer and UVs.
enum class ParticleShape : std::uint8_t
{
    Square = 0,
    Circle,
    Ring,
    Glow,
    Star,
    Diamond,
    Spark,
    Smoke,
    Bubble,
    Leaf,
    Snowflake,
    Heart,
    Count,
};

// Where inside the emitter a particle is born. Direction and spread are a
// separate axis, so a box emitter can still fire in a narrow cone.
enum class ParticleEmissionShape : std::uint8_t
{
    Point = 0,
    Circle,
    Box,
    Line,
    Count,
};

struct ParticleShapeFrame
{
    std::uint32_t Layer = 0;
    Vector2D UvMin{0.0f, 0.0f};
    Vector2D UvMax{1.0f, 1.0f};
};

struct ParticleShapeAtlas
{
    std::array<ParticleShapeFrame, kParticleShapeCount> Frames{};

    [[nodiscard]] const ParticleShapeFrame& Frame(ParticleShape InShape) const noexcept;
    // One shape per texture-array layer, full-layer UVs. A game that packs all
    // masks into one layer overrides the UV rects instead.
    [[nodiscard]] static ParticleShapeAtlas Defaults() noexcept;
};

// Authoring data. Every field is a primitive so the settings can be saved,
// copied and edited without allocation.
struct ParticleEffectSettings
{
    std::uint32_t MaxParticles = 256;
    std::uint32_t Seed = 0x9E3779B9u;
    ParticleShape Shape = ParticleShape::Circle;
    ParticleEmissionShape EmissionShape = ParticleEmissionShape::Point;

    // Circle: X is the radius. Box: X/Y are half extents. Line: X is the half
    // length along the emitter's local X axis.
    Vector2D EmissionExtents{0.0f, 0.0f};

    Vector2D Direction{0.0f, -1.0f};
    Vector2D Gravity{0.0f, 160.0f};
    Vector2D Wind{0.0f, 0.0f};
    float SpreadRadians = 0.35f;

    float SpeedMin = 40.0f;
    float SpeedMax = 140.0f;
    float LifetimeMin = 0.4f;
    float LifetimeMax = 1.2f;
    float StartSize = 8.0f;
    float EndSize = 2.0f;
    float StartRotation = 0.0f;
    float RotationSpeed = 1.0f;

    // Forces are per second: drag is an exponential velocity decay, turbulence
    // adds a random acceleration, swirl adds a tangent around the emitter.
    float Drag = 0.0f;
    float Turbulence = 0.0f;
    float Swirl = 0.0f;

    // EmissionRate is particles per second. A positive BurstInterval adds
    // BurstCount particles every interval; with both zero the emitter fires one
    // BurstCount burst and stops. Duration zero means "until stopped".
    float EmissionRate = 60.0f;
    std::uint32_t BurstCount = 0;
    float BurstInterval = 0.0f;
    float Duration = 0.0f;
    bool Loop = true;
    bool Additive = false;

    Color32 StartColor = Color32::FromBytes(0xFF, 0x4F, 0xA8, 0xFF);
    Color32 EndColor = Color32::FromBytes(0xFF, 0x4F, 0xA8, 0x00);

    void Clamp() noexcept;

    [[nodiscard]] friend bool operator==(const ParticleEffectSettings&,
                                         const ParticleEffectSettings&) noexcept = default;
};

struct ParticleEffectPreset
{
    const char* Id = "";
    ParticleEffectSettings Settings{};
};

// Sixteen ready-made presets. Every field stays editable: a game copies the
// settings, changes direction, rate, colour or size, and spawns its own.
[[nodiscard]] std::span<const ParticleEffectPreset> ParticleEffectPresets() noexcept;
[[nodiscard]] const ParticleEffectPreset* FindParticleEffectPreset(std::string_view InId) noexcept;

// Generates a white alpha mask for one shape slot. A game may author masks
// instead; this helper exists so the 12 slots work without shipping textures.
// Returns the pixel count written, or 0 when the buffer is too small.
[[nodiscard]] std::uint32_t BuildParticleShapeMask(ParticleShape InShape, std::uint32_t InSize,
                                                   std::span<std::uint8_t> OutAlpha) noexcept;

// Fixed-state CPU particle pool. Emitters and particles live in arrays inside
// the object, so Update() never allocates. The object is large enough that the
// host should own it on the heap.
class ParticleEffectSystem
{
public:
    ParticleEffectSystem() noexcept;

    [[nodiscard]] std::uint32_t Spawn(const ParticleEffectSettings& InSettings, Vector2D InPosition,
                                      float InRotation = 0.0f) noexcept;
    [[nodiscard]] std::uint32_t SpawnPreset(std::string_view InPresetId, Vector2D InPosition,
                                            float InRotation = 0.0f) noexcept;
    // Reuses an existing emitter slot, kills its live particles and applies new
    // settings. Cheaper than StopEmitter plus Spawn when slots are scarce.
    [[nodiscard]] bool RestartEmitter(std::uint32_t InEmitter, const ParticleEffectSettings& InSettings,
                                      Vector2D InPosition, float InRotation = 0.0f) noexcept;
    [[nodiscard]] bool SetEmitterTransform(std::uint32_t InEmitter, Vector2D InPosition,
                                           float InRotation = 0.0f) noexcept;
    void StopEmitter(std::uint32_t InEmitter) noexcept;
    void Clear() noexcept;
    void Update(float InDeltaSeconds) noexcept;

    // Converts the live particles into render items. The caller supplies the
    // shape atlas; a missing atlas falls back to a full white quad.
    [[nodiscard]] std::uint32_t BuildRenderItems(std::span<Rhi::RenderItem> Out,
                                                 const ParticleShapeAtlas& InAtlas) const noexcept;

    [[nodiscard]] std::uint32_t ActiveParticles() const noexcept
    {
        return MActiveParticles;
    }
    [[nodiscard]] std::uint32_t ActiveEmitters() const noexcept
    {
        return MActiveEmitters;
    }

private:
    struct EmitterState
    {
        ParticleEffectSettings Settings{};
        Vector2D Position{};
        float Rotation = 0.0f;
        float Age = 0.0f;
        float SpawnAccumulator = 0.0f;
        float BurstTimer = 0.0f;
        bool Active = false;
        bool Emitting = false;
        Pcg32 Rng{};
    };

    struct ParticleState
    {
        Vector2D Position{};
        Vector2D Velocity{};
        Vector2D Origin{};
        float Age = 0.0f;
        float Lifetime = 0.0f;
        float StartSize = 0.0f;
        float EndSize = 0.0f;
        float Rotation = 0.0f;
        float RotationSpeed = 0.0f;
        Color32 StartColor{};
        Color32 EndColor{};
        std::uint32_t Emitter = 0;
        std::uint8_t Shape = 0;
        bool Alive = false;
    };

    void InitializeEmitter(std::uint32_t InEmitter, const ParticleEffectSettings& InSettings, Vector2D InPosition,
                           float InRotation) noexcept;
    void Emit(std::uint32_t InEmitter, std::uint32_t InCount) noexcept;
    void SpawnParticle(std::uint32_t InEmitter) noexcept;
    void KillParticle(std::uint32_t InIndex) noexcept;
    void ResetFreeList() noexcept;

    std::array<ParticleState, kParticleEffectMaxParticles> MParticles{};
    std::array<EmitterState, kParticleEffectMaxEmitters> MEmitters{};
    std::array<std::uint32_t, kParticleEffectMaxParticles> MFreeParticles{};
    std::array<std::uint32_t, kParticleEffectMaxEmitters> MParticleCounts{};
    std::uint32_t MFreeCount = 0;
    std::uint32_t MActiveParticles = 0;
    std::uint32_t MActiveEmitters = 0;
};
}
