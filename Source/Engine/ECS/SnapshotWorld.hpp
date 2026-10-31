// FemboyEngine2D - Snapshot v4 world extension.
#pragma once

#include "Engine/Core/Types.hpp"
#include "Engine/ECS/Components.hpp"
#include "Engine/ECS/StableIds.hpp"
#include "Engine/Physics/Particles.hpp"
#include "Engine/Physics/Cloth.hpp"
#include "Engine/Physics/Fluid.hpp"
#include "Engine/Physics/PhysicsJoints.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>

namespace Fby::Ecs
{

inline constexpr std::uint32_t kWorldSnapshotVersion = 1;
inline constexpr std::uint32_t kMaxSnapshotChains = 32;
inline constexpr std::uint32_t kMaxSnapshotCloths = 8;
inline constexpr std::uint32_t kMaxSnapshotEmitters = 32;
inline constexpr std::uint32_t kMaxSnapshotChainPath = 64;

struct ChainSaveV3
{
    ChainId chainId{};
    JointChainKind kind = JointChainKind::Rope;
    std::uint32_t flags = 0;
    Entity anchorA = entt::null;
    Entity anchorB = entt::null;
    Vector2D start{};
    Vector2D end{};
    std::array<Vector2D, kMaxSnapshotChainPath> path{};
    std::uint32_t segmentCount = 0;
    std::uint32_t pathCount = 0;
    float radius = 0.0f;
    float density = 1.0f;
    float friction = 0.5f;
    float restitution = 0.0f;
    float hertz = 4.0f;
    float dampingRatio = 0.7f;
    float lowerAngle = -0.65f;
    float upperAngle = 0.65f;
    float motorSpeed = 0.0f;
    float maxMotorTorque = 0.0f;
    std::uint64_t categoryBits = 8u;
    std::uint64_t maskBits = 0xFFFFFFFFu;
    std::uint16_t linkLayer = 0;
    bool enableLimit = true;
    bool enableMotor = false;
    bool ownsAnchorA = false;
    bool ownsAnchorB = false;
};

struct ClothSaveV3
{
    ClothId clothId{};
    ClothDesc desc{};
    std::uint32_t columns = 0;
    std::uint32_t rows = 0;
    std::uint32_t particleCount = 0;
    std::array<Vector2D, ClothSystem::kMaxParticles> positions{};
    std::array<Vector2D, ClothSystem::kMaxParticles> previous{};
    std::array<std::uint8_t, ClothSystem::kMaxParticles> pinned{};
};

struct FluidSaveV3
{
    FluidDesc desc{};
    std::uint32_t columns = 0;
    std::uint32_t rows = 0;
    std::array<float, kMaxWaterCells> height{};
};

struct EmitterSaveV3
{
    EmitterId emitterId{};
    ParticleEmitterComponent component{};
    float simulationTime = 0.0f;
};

struct WorldSnapshotV3
{
    std::uint32_t version = kWorldSnapshotVersion;
    std::uint32_t chainCount = 0;
    std::uint32_t clothCount = 0;
    std::uint32_t emitterCount = 0;
    std::uint32_t hasFluid = 0;
    std::uint32_t reserved = 0;
    std::array<ChainSaveV3, kMaxSnapshotChains> chains{};
    std::array<ClothSaveV3, kMaxSnapshotCloths> cloths{};
    std::array<EmitterSaveV3, kMaxSnapshotEmitters> emitters{};
    FluidSaveV3 fluid{};
};

static_assert(std::is_trivially_copyable_v<WorldSnapshotV3>);

[[nodiscard]] constexpr std::size_t WorldSnapshotBytes() noexcept
{
    return sizeof(WorldSnapshotV3);
}

// Canonical copy. The struct is zero-initialised by the caller before fields are
[[nodiscard]] bool EncodeWorldSnapshot(const WorldSnapshotV3& InState, std::span<std::byte> InDestination,
                                       std::size_t* OutBytes = nullptr) noexcept;
[[nodiscard]] bool DecodeWorldSnapshot(std::span<const std::byte> InBytes, WorldSnapshotV3& InState,
                                       std::string_view* InError = nullptr) noexcept;
}
