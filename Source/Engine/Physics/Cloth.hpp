// FemboyEngine2D - Verlet cloth on top of the Box2D collider world.
#pragma once

#include "Engine/Core/Types.hpp"
#include "Engine/ECS/Components.hpp"
#include "Engine/ECS/Schedule.hpp"
#include "Engine/ECS/StableIds.hpp"
#include "Engine/Physics/Physics.hpp"
#include "Engine/RHI/Mesh.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <type_traits>

namespace Fby
{

enum ClothFlags : std::uint32_t
{
    ClothNone = 0,
    ClothPinTopRow = 1u << 0,
    ClothPinBottomRow = 1u << 1,
    ClothPinCorners = 1u << 2,
    ClothCollideWorld = 1u << 3,
    ClothAffectedByWind = 1u << 4,
};

// Marker attached to the cloth entity. The geometry is owned by ClothSystem
struct ClothComponent
{
    ClothId ClothID{};
    std::uint32_t ClothIndex = 0;
    std::uint32_t Generation = 0;
    std::uint32_t Flags = 0;
    std::uint32_t TextureArray = 0xFFFFFFFFu;
    std::uint16_t Layer = 0;
    std::uint16_t SortLayer = 0;
    float Depth = 0.0f;
};

static_assert(std::is_trivially_copyable_v<ClothComponent>);
static_assert(std::is_standard_layout_v<ClothComponent>);

struct ClothHandle
{
    static constexpr std::uint32_t kInvalid = 0xFFFFFFFFu;
    std::uint32_t index = kInvalid;
    std::uint32_t generation = 0;
    ClothId stableId{};

    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return index != kInvalid;
    }
    [[nodiscard]] friend constexpr bool operator==(ClothHandle InA, ClothHandle InB) noexcept
    {
        return InA.index == InB.index && InA.generation == InB.generation;
    }
};

struct ClothDesc
{
    // Top-left particle, pixels
    Vector2D origin{0.0f, 0.0f};
    // Total sheet extent, pixels
    Vector2D size{256.0f, 256.0f};
    // 2..32 particles
    std::uint32_t columns = 16;
    // 2..32 particles
    std::uint32_t rows = 16;
    std::uint32_t flags = ClothPinTopRow | ClothCollideWorld | ClothAffectedByWind;

    // Pixels / s^2
    Vector2D gravity{0.0f, 980.0f};
    // Pixels / s^2, constant on every particle
    Vector2D wind{45.0f, 0.0f};
    // Per integration step, 0..1
    float damping = 0.995f;
    float structuralStiffness = 1.0f;
    float shearStiffness = 0.65f;
    float bendStiffness = 0.18f;
    std::uint32_t solverIterations = 8;

    // Pixels, collision probe radius
    float particleRadius = 2.0f;
    std::uint64_t collisionCategory = 0x0004u;
    std::uint64_t collisionMask = 0xFFFFFFFFu;

    // Render description copied into the marker and into WriteMesh().
    std::uint32_t textureArray = 0xFFFFFFFFu;
    std::uint16_t layer = 0;
    std::uint16_t sortLayer = 0;
    float depth = 0.0f;
    Color32 color = Color32::White();
};

struct ClothInfo
{
    ClothId clothId{};
    std::uint32_t columns = 0;
    std::uint32_t rows = 0;
    std::uint32_t particles = 0;
    std::uint32_t constraints = 0;
    Entity entity = entt::null;
};

// ClothSystem
class ClothSystem
{
public:
    static constexpr std::uint32_t kMaxCloths = 8;
    static constexpr std::uint32_t kMaxColumns = 32;
    static constexpr std::uint32_t kMaxRows = 32;
    static constexpr std::uint32_t kMaxParticles = kMaxColumns * kMaxRows;
    static constexpr std::uint32_t kMaxConstraints = 8192;
    static constexpr std::uint32_t kMaxIndices = 6 * (kMaxColumns - 1) * (kMaxRows - 1);

    ClothSystem();
    ~ClothSystem();

    ClothSystem(const ClothSystem&) = delete;
    ClothSystem& operator=(const ClothSystem&) = delete;

    // `physics` may be null for an offline/headless cloth; collisions are then
    [[nodiscard]] bool Initialize(PhysicsSystem* InPhysics = nullptr);
    void Shutdown(Ecs::Registry& InRegistry) noexcept;
    [[nodiscard]] bool IsInitialized() const noexcept;

    [[nodiscard]] ClothHandle Create(Ecs::Registry& InRegistry, const ClothDesc& InDesc) noexcept;
    bool Destroy(Ecs::Registry& InRegistry, ClothHandle InHandle) noexcept;

    void SetGravity(ClothHandle InHandle, Vector2D InGravity) noexcept;
    void SetWind(ClothHandle InHandle, Vector2D InWind) noexcept;
    void SetSolverIterations(ClothHandle InHandle, std::uint32_t InIterations) noexcept;
    // Weather hook: applies one wind vector to every active cloth that opted into
    void SetAmbientWind(Vector2D InWind) noexcept;

    // Fixed-step simulation. Collisions are resolved against PhysicsSystem's
    void Step(float InDt) noexcept;
    // ECS-facing convenience: uses context.dt if positive. Does not touch the
    void Update(Ecs::SystemContext& InContext) noexcept;

    [[nodiscard]] bool IsValid(ClothHandle InHandle) const noexcept;
    [[nodiscard]] ClothInfo Info(ClothHandle InHandle) const noexcept;
    [[nodiscard]] std::span<const Rhi::MeshVertex> Vertices(ClothHandle InHandle) const noexcept;
    [[nodiscard]] std::span<const std::uint16_t> Indices(ClothHandle InHandle) const noexcept;
    [[nodiscard]] std::span<const Vector2D> ParticlePositions(ClothHandle InHandle) const noexcept;

    // Fills one MeshRenderItem with spans into the cloth's stable vertex/index
    [[nodiscard]] bool WriteMesh(ClothHandle InHandle, Rhi::MeshRenderItem& InItem) const noexcept;

private:
    struct Storage;
    struct Runtime;

    [[nodiscard]] Runtime* Get(ClothHandle InHandle) noexcept;
    [[nodiscard]] const Runtime* Get(ClothHandle InHandle) const noexcept;

    std::unique_ptr<Storage> MStorage;
    PhysicsSystem* MPhysics = nullptr;
    std::uint32_t MLiveCloths = 0;
    StableIdAllocator<ClothId> MStableIds{};
};
}

FBY_REFLECT_COMPONENT(Fby::ClothComponent, "ClothComponent", FBY_FIELD(Fby::ClothComponent, ClothID, UInt32),
                      FBY_FIELD(Fby::ClothComponent, ClothIndex, UInt32),
                      FBY_FIELD(Fby::ClothComponent, Generation, UInt32), FBY_FIELD(Fby::ClothComponent, Flags, UInt32),
                      FBY_FIELD(Fby::ClothComponent, TextureArray, UInt32),
                      FBY_FIELD(Fby::ClothComponent, Layer, UInt32), FBY_FIELD(Fby::ClothComponent, SortLayer, UInt32),
                      FBY_FIELD(Fby::ClothComponent, Depth, Float))
