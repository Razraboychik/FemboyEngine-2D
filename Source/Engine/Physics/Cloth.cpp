// FemboyEngine2D - Verlet cloth implementation.
#include "Engine/Physics/Cloth.hpp"

#include "Engine/Core/SimMath.hpp"

#include <box2d/box2d.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace Fby
{
namespace
{

struct Constraint
{
    std::uint16_t a = 0;
    std::uint16_t b = 0;
    float restLength = 0.0f;
    float stiffness = 0.0f;
};

struct ClothRuntime
{
    bool active = false;
    std::uint32_t index = 0;
    std::uint32_t generation = 1;
    ClothId stableId{};
    std::uint32_t columns = 0;
    std::uint32_t rows = 0;
    std::uint32_t particleCount = 0;
    std::uint32_t constraintCount = 0;
    std::uint32_t indexCount = 0;
    std::uint32_t solverIterations = 8;
    std::uint32_t flags = 0;
    std::uint64_t collisionCategory = 0x0004u;
    std::uint64_t collisionMask = 0xFFFFFFFFu;
    Vector2D origin{};
    Vector2D cellSize{1.0f, 1.0f};
    Vector2D gravity{0.0f, 980.0f};
    Vector2D wind{};
    float damping = 0.995f;
    float particleRadius = 2.0f;
    std::uint32_t textureArray = 0xFFFFFFFFu;
    std::uint16_t layer = 0;
    std::uint16_t sortLayer = 0;
    float depth = 0.0f;
    Color32 color = Color32::White();
    Entity entity = entt::null;

    std::array<Vector2D, ClothSystem::kMaxParticles> positions{};
    std::array<Vector2D, ClothSystem::kMaxParticles> previous{};
    std::array<Vector2D, ClothSystem::kMaxParticles> pins{};
    std::array<std::uint8_t, ClothSystem::kMaxParticles> pinned{};
    std::array<float, ClothSystem::kMaxParticles> invMass{};
    std::array<Constraint, ClothSystem::kMaxConstraints> constraints{};
    std::array<Rhi::MeshVertex, ClothSystem::kMaxParticles> vertices{};
    std::array<std::uint16_t, ClothSystem::kMaxIndices> indices{};
};

struct ClothStorage
{
    std::array<ClothRuntime, ClothSystem::kMaxCloths> cloths{};
};

[[nodiscard]] std::uint32_t VertexIndex(std::uint32_t InX, std::uint32_t InY, std::uint32_t InColumns) noexcept
{
    return InY * InColumns + InX;
}

struct CollisionContext
{
    b2Pos target{};
    float bestDistanceSquared = 1.0e30f;
    b2Pos closest{};
    bool found = false;
};

bool CollisionCallback(b2ShapeId InShape, void* InContext) noexcept
{
    auto* ScopedQuery = static_cast<CollisionContext*>(InContext);
    if (ScopedQuery == nullptr || !b2Shape_IsValid(InShape) || b2Shape_IsSensor(InShape))
    {
        // Keep looking: a sensor is not a solid surface
        return true;
    }
    const b2Pos ScopedClosest = b2Shape_GetClosestPoint(InShape, ScopedQuery->target);
    const float ScopedDx = ScopedClosest.x - ScopedQuery->target.x;
    const float ScopedDy = ScopedClosest.y - ScopedQuery->target.y;
    const float ScopedDistanceSquared = ScopedDx * ScopedDx + ScopedDy * ScopedDy;
    if (ScopedDistanceSquared < ScopedQuery->bestDistanceSquared)
    {
        ScopedQuery->bestDistanceSquared = ScopedDistanceSquared;
        ScopedQuery->closest = ScopedClosest;
        ScopedQuery->found = true;
    }
    return true;
}
}

// The public header only forward declares these two types so ClothRuntime stays
struct ClothSystem::Runtime : ClothRuntime
{
};

struct ClothSystem::Storage
{
    std::array<ClothSystem::Runtime, ClothSystem::kMaxCloths> cloths{};
};

ClothSystem::ClothSystem() = default;

ClothSystem::~ClothSystem() = default;

bool ClothSystem::Initialize(PhysicsSystem* InPhysics)
{
    if (MStorage != nullptr)
    {
        return true;
    }
    MStorage = std::make_unique<Storage>();
    MPhysics = (InPhysics != nullptr && InPhysics->IsValid()) ? InPhysics : nullptr;
    MLiveCloths = 0;
    return MStorage != nullptr;
}

bool ClothSystem::IsInitialized() const noexcept
{
    return MStorage != nullptr;
}

void ClothSystem::Shutdown(Ecs::Registry& InRegistry) noexcept
{
    if (MStorage == nullptr)
    {
        return;
    }
    for (std::uint32_t ScopedI = 0; ScopedI < kMaxCloths; ++ScopedI)
    {
        Runtime& ScopedCloth = MStorage->cloths[ScopedI];
        if (ScopedCloth.active)
        {
            if (ScopedCloth.entity != entt::null && InRegistry.valid(ScopedCloth.entity))
            {
                const auto* ScopedMarker = InRegistry.try_get<ClothComponent>(ScopedCloth.entity);
                if (ScopedMarker != nullptr && ScopedMarker->ClothIndex == ScopedCloth.index &&
                    ScopedMarker->Generation == ScopedCloth.generation)
                {
                    InRegistry.destroy(ScopedCloth.entity);
                }
            }
            ScopedCloth = Runtime{};
        }
    }
    MLiveCloths = 0;
    MPhysics = nullptr;
    MStorage.reset();
}

ClothSystem::Runtime* ClothSystem::Get(ClothHandle InHandle) noexcept
{
    if (MStorage == nullptr || InHandle.Index >= kMaxCloths)
    {
        return nullptr;
    }
    Runtime& ScopedCloth = MStorage->cloths[InHandle.Index];
    if (!ScopedCloth.active || ScopedCloth.generation != InHandle.Generation)
    {
        return nullptr;
    }
    return &ScopedCloth;
}

const ClothSystem::Runtime* ClothSystem::Get(ClothHandle InHandle) const noexcept
{
    if (MStorage == nullptr || InHandle.Index >= kMaxCloths)
    {
        return nullptr;
    }
    const Runtime& ScopedCloth = MStorage->cloths[InHandle.Index];
    if (!ScopedCloth.active || ScopedCloth.generation != InHandle.Generation)
    {
        return nullptr;
    }
    return &ScopedCloth;
}

bool ClothSystem::IsValid(ClothHandle InHandle) const noexcept
{
    return Get(InHandle) != nullptr;
}

ClothHandle ClothSystem::Create(Ecs::Registry& InRegistry, const ClothDesc& InDesc)
{
    if (MStorage == nullptr || InDesc.Columns < 2 || InDesc.Columns > kMaxColumns || InDesc.Rows < 2 ||
        InDesc.Rows > kMaxRows || InDesc.Size.X <= 0.0f || InDesc.Size.Y <= 0.0f)
    {
        return {};
    }

    std::uint32_t ScopedSlot = kMaxCloths;
    for (std::uint32_t ScopedI = 0; ScopedI < kMaxCloths; ++ScopedI)
    {
        if (!MStorage->cloths[ScopedI].active)
        {
            ScopedSlot = ScopedI;
            break;
        }
    }
    if (ScopedSlot == kMaxCloths)
    {
        return {};
    }

    Runtime& ScopedCloth = MStorage->cloths[ScopedSlot];
    const std::uint32_t ScopedGeneration = ScopedCloth.generation == 0 ? 1u : ScopedCloth.generation;
    ScopedCloth = Runtime{};
    ScopedCloth.index = ScopedSlot;
    ScopedCloth.generation = ScopedGeneration;
    ScopedCloth.stableId = MStableIds.Allocate();
    if (!ScopedCloth.stableId.IsValid())
    {
        ScopedCloth = Runtime{};
        return {};
    }
    ScopedCloth.columns = InDesc.Columns;
    ScopedCloth.rows = InDesc.Rows;
    ScopedCloth.particleCount = InDesc.Columns * InDesc.Rows;
    ScopedCloth.flags = InDesc.Flags;
    ScopedCloth.origin = InDesc.Origin;
    ScopedCloth.cellSize = Vector2D{InDesc.Size.X / static_cast<float>(InDesc.Columns - 1),
                                    InDesc.Size.Y / static_cast<float>(InDesc.Rows - 1)};
    ScopedCloth.gravity = InDesc.Gravity;
    ScopedCloth.wind = InDesc.Wind;
    ScopedCloth.damping = std::clamp(InDesc.Damping, 0.0f, 1.0f);
    ScopedCloth.particleRadius = std::max(InDesc.ParticleRadius, 0.1f);
    ScopedCloth.collisionCategory = InDesc.CollisionCategory;
    ScopedCloth.collisionMask = InDesc.CollisionMask;
    ScopedCloth.solverIterations = std::clamp(InDesc.SolverIterations, 1u, 32u);
    ScopedCloth.textureArray = InDesc.TextureArray;
    ScopedCloth.layer = InDesc.Layer;
    ScopedCloth.sortLayer = InDesc.SortLayer;
    ScopedCloth.depth = InDesc.Depth;
    ScopedCloth.color = InDesc.Color;

    const auto ScopedPin = [&ScopedCloth](std::uint32_t InX, std::uint32_t InY)
    {
        const std::uint32_t ScopedIndex = VertexIndex(InX, InY, ScopedCloth.columns);
        ScopedCloth.pinned[ScopedIndex] = 1;
        ScopedCloth.invMass[ScopedIndex] = 0.0f;
        ScopedCloth.pins[ScopedIndex] = ScopedCloth.positions[ScopedIndex];
    };

    for (std::uint32_t ScopedY = 0; ScopedY < InDesc.Rows; ++ScopedY)
    {
        for (std::uint32_t ScopedX = 0; ScopedX < InDesc.Columns; ++ScopedX)
        {
            const std::uint32_t ScopedIndex = VertexIndex(ScopedX, ScopedY, InDesc.Columns);
            const Vector2D ScopedPosition{InDesc.Origin.X + static_cast<float>(ScopedX) * ScopedCloth.cellSize.X,
                                          InDesc.Origin.Y + static_cast<float>(ScopedY) * ScopedCloth.cellSize.Y};
            ScopedCloth.positions[ScopedIndex] = ScopedPosition;
            ScopedCloth.previous[ScopedIndex] = ScopedPosition;
            ScopedCloth.pins[ScopedIndex] = ScopedPosition;
            ScopedCloth.invMass[ScopedIndex] = 1.0f;
            ScopedCloth.pinned[ScopedIndex] = 0;

            Rhi::MeshVertex& ScopedVertex = ScopedCloth.vertices[ScopedIndex];
            ScopedVertex.position = ScopedPosition;
            ScopedVertex.uv = Vector2D{static_cast<float>(ScopedX) / static_cast<float>(InDesc.Columns - 1),
                                       static_cast<float>(ScopedY) / static_cast<float>(InDesc.Rows - 1)};
            ScopedVertex.color = InDesc.Color.rgba;
            ScopedVertex.padding = 0;
            ScopedVertex.boneIndices[0] = 0;
            ScopedVertex.boneIndices[1] = 0;
            ScopedVertex.boneWeights[0] = 0.0f;
            ScopedVertex.boneWeights[1] = 0.0f;
        }
    }

    if ((InDesc.Flags & ClothPinTopRow) != 0)
    {
        for (std::uint32_t ScopedX = 0; ScopedX < InDesc.Columns; ++ScopedX)
        {
            ScopedPin(ScopedX, 0);
        }
    }
    if ((InDesc.Flags & ClothPinBottomRow) != 0)
    {
        for (std::uint32_t ScopedX = 0; ScopedX < InDesc.Columns; ++ScopedX)
        {
            ScopedPin(ScopedX, InDesc.Rows - 1);
        }
    }
    if ((InDesc.Flags & ClothPinCorners) != 0)
    {
        ScopedPin(0, 0);
        ScopedPin(InDesc.Columns - 1, 0);
        ScopedPin(0, InDesc.Rows - 1);
        ScopedPin(InDesc.Columns - 1, InDesc.Rows - 1);
    }

    const auto ScopedAddConstraint = [&ScopedCloth](std::uint32_t InA, std::uint32_t InB, float InStiffness)
    {
        if (InA == InB || ScopedCloth.constraintCount >= kMaxConstraints || InStiffness <= 0.0f)
        {
            return;
        }
        const Vector2D ScopedDelta = ScopedCloth.positions[InB] - ScopedCloth.positions[InA];
        const float ScopedRestLength = std::sqrt(ScopedDelta.LengthSquared());
        if (ScopedRestLength <= 1.0e-5f)
        {
            return;
        }
        ScopedCloth.constraints[ScopedCloth.constraintCount++] =
            Constraint{static_cast<std::uint16_t>(InA), static_cast<std::uint16_t>(InB), ScopedRestLength,
                       std::clamp(InStiffness, 0.0f, 1.0f)};
    };

    for (std::uint32_t ScopedY = 0; ScopedY < InDesc.Rows; ++ScopedY)
    {
        for (std::uint32_t ScopedX = 0; ScopedX < InDesc.Columns; ++ScopedX)
        {
            const std::uint32_t ScopedIndex = VertexIndex(ScopedX, ScopedY, InDesc.Columns);
            if (ScopedX + 1 < InDesc.Columns)
            {
                ScopedAddConstraint(ScopedIndex, ScopedIndex + 1, InDesc.StructuralStiffness);
            }
            if (ScopedY + 1 < InDesc.Rows)
            {
                ScopedAddConstraint(ScopedIndex, ScopedIndex + InDesc.Columns, InDesc.StructuralStiffness);
            }
            if (ScopedX + 1 < InDesc.Columns && ScopedY + 1 < InDesc.Rows)
            {
                ScopedAddConstraint(ScopedIndex, ScopedIndex + InDesc.Columns + 1, InDesc.ShearStiffness);
                ScopedAddConstraint(ScopedIndex + 1, ScopedIndex + InDesc.Columns, InDesc.ShearStiffness);
            }
            if (ScopedX + 2 < InDesc.Columns)
            {
                ScopedAddConstraint(ScopedIndex, ScopedIndex + 2, InDesc.BendStiffness);
            }
            if (ScopedY + 2 < InDesc.Rows)
            {
                ScopedAddConstraint(ScopedIndex, ScopedIndex + 2 * InDesc.Columns, InDesc.BendStiffness);
            }
        }
    }

    for (std::uint32_t ScopedY = 0; ScopedY + 1 < InDesc.Rows; ++ScopedY)
    {
        for (std::uint32_t ScopedX = 0; ScopedX + 1 < InDesc.Columns; ++ScopedX)
        {
            const std::uint32_t ScopedA = VertexIndex(ScopedX, ScopedY, InDesc.Columns);
            const std::uint32_t ScopedB = ScopedA + 1;
            const std::uint32_t ScopedC = ScopedA + InDesc.Columns;
            const std::uint32_t ScopedD = ScopedC + 1;
            if (ScopedCloth.indexCount + 6 > kMaxIndices)
            {
                break;
            }
            ScopedCloth.indices[ScopedCloth.indexCount++] = static_cast<std::uint16_t>(ScopedA);
            ScopedCloth.indices[ScopedCloth.indexCount++] = static_cast<std::uint16_t>(ScopedB);
            ScopedCloth.indices[ScopedCloth.indexCount++] = static_cast<std::uint16_t>(ScopedC);
            ScopedCloth.indices[ScopedCloth.indexCount++] = static_cast<std::uint16_t>(ScopedB);
            ScopedCloth.indices[ScopedCloth.indexCount++] = static_cast<std::uint16_t>(ScopedD);
            ScopedCloth.indices[ScopedCloth.indexCount++] = static_cast<std::uint16_t>(ScopedC);
        }
    }

    const Entity ScopedEntity = InRegistry.create();
    ScopedCloth.entity = ScopedEntity;
    InRegistry.emplace<Transform2D>(ScopedEntity, Transform2D{InDesc.Origin, Vector2D{1.0f, 1.0f}, 0.0f});
    InRegistry.emplace<ClothComponent>(ScopedEntity, ClothComponent{ScopedCloth.stableId, ScopedSlot, ScopedGeneration,
                                                                    InDesc.Flags, InDesc.TextureArray, InDesc.Layer,
                                                                    InDesc.SortLayer, InDesc.Depth});

    ScopedCloth.active = true;
    ++MLiveCloths;
    return ClothHandle{ScopedSlot, ScopedGeneration, ScopedCloth.stableId};
}

bool ClothSystem::Destroy(Ecs::Registry& InRegistry, ClothHandle InHandle) noexcept
{
    Runtime* ScopedCloth = Get(InHandle);
    if (ScopedCloth == nullptr)
    {
        return false;
    }
    if (ScopedCloth->entity != entt::null && InRegistry.valid(ScopedCloth->entity))
    {
        const auto* ScopedMarker = InRegistry.try_get<ClothComponent>(ScopedCloth->entity);
        if (ScopedMarker != nullptr && ScopedMarker->ClothIndex == ScopedCloth->index &&
            ScopedMarker->Generation == ScopedCloth->generation)
        {
            InRegistry.destroy(ScopedCloth->entity);
        }
    }
    const std::uint32_t ScopedNextGeneration = ScopedCloth->generation + 1u == 0 ? 1u : ScopedCloth->generation + 1u;
    if (ScopedCloth->stableId.IsValid())
    {
        MStableIds.Release(ScopedCloth->stableId);
    }
    *ScopedCloth = Runtime{};
    ScopedCloth->generation = ScopedNextGeneration;
    if (MLiveCloths > 0)
    {
        --MLiveCloths;
    }
    return true;
}

void ClothSystem::SetGravity(ClothHandle InHandle, Vector2D InGravity) noexcept
{
    if (Runtime* ScopedCloth = Get(InHandle); ScopedCloth != nullptr)
    {
        ScopedCloth->gravity = InGravity;
    }
}

void ClothSystem::SetWind(ClothHandle InHandle, Vector2D InWind) noexcept
{
    if (Runtime* ScopedCloth = Get(InHandle); ScopedCloth != nullptr)
    {
        ScopedCloth->wind = InWind;
    }
}

void ClothSystem::SetSolverIterations(ClothHandle InHandle, std::uint32_t InIterations) noexcept
{
    if (Runtime* ScopedCloth = Get(InHandle); ScopedCloth != nullptr)
    {
        ScopedCloth->solverIterations = std::clamp(InIterations, 1u, 32u);
    }
}

void ClothSystem::SetAmbientWind(Vector2D InWind) noexcept
{
    if (MStorage == nullptr)
    {
        return;
    }
    for (Runtime& ScopedCloth : MStorage->cloths)
    {
        if (ScopedCloth.active && (ScopedCloth.flags & ClothAffectedByWind) != 0)
        {
            ScopedCloth.wind = InWind;
        }
    }
}

void ClothSystem::Step(float InDt) noexcept
{
    if (MStorage == nullptr || !(InDt > 0.0f))
    {
        return;
    }
    // The caller owns the fixed step; silently clamping it hides a host bug and
    // changes the authored damping at the clamped rates.
    const float ScopedStep = std::max(InDt, 0.0f);
    const float ScopedStepSquared = ScopedStep * ScopedStep;

    for (std::uint32_t ScopedClothIndex = 0; ScopedClothIndex < kMaxCloths; ++ScopedClothIndex)
    {
        Runtime& ScopedCloth = MStorage->cloths[ScopedClothIndex];
        if (!ScopedCloth.active || ScopedCloth.particleCount == 0)
        {
            continue;
        }

        const Vector2D ScopedAcceleration =
            ScopedCloth.gravity + ((ScopedCloth.flags & ClothAffectedByWind) != 0 ? ScopedCloth.wind : Vector2D{});
        // Damping is authored per 60 Hz reference step; the exponent keeps its
        // meaning at any fixed step.
        const float ScopedDamping = Sim::Pow(std::clamp(ScopedCloth.damping, 0.0f, 1.0f), ScopedStep * 60.0f);
        const float ScopedMaxDisplacement = std::max(ScopedCloth.cellSize.X, ScopedCloth.cellSize.Y) * 0.5f;

        for (std::uint32_t ScopedI = 0; ScopedI < ScopedCloth.particleCount; ++ScopedI)
        {
            if (ScopedCloth.pinned[ScopedI] != 0)
            {
                ScopedCloth.positions[ScopedI] = ScopedCloth.pins[ScopedI];
                ScopedCloth.previous[ScopedI] = ScopedCloth.pins[ScopedI];
                continue;
            }
            const Vector2D ScopedVelocity =
                (ScopedCloth.positions[ScopedI] - ScopedCloth.previous[ScopedI]) * ScopedDamping;
            Vector2D ScopedDisplacement = ScopedVelocity + ScopedAcceleration * ScopedStepSquared;
            const float ScopedLengthSquared = ScopedDisplacement.LengthSquared();
            if (ScopedLengthSquared > ScopedMaxDisplacement * ScopedMaxDisplacement)
            {
                ScopedDisplacement = ScopedDisplacement * (ScopedMaxDisplacement / std::sqrt(ScopedLengthSquared));
            }
            ScopedCloth.previous[ScopedI] = ScopedCloth.positions[ScopedI];
            ScopedCloth.positions[ScopedI] += ScopedDisplacement;
        }

        for (std::uint32_t ScopedIteration = 0; ScopedIteration < ScopedCloth.solverIterations; ++ScopedIteration)
        {
            for (std::uint32_t ScopedC = 0; ScopedC < ScopedCloth.constraintCount; ++ScopedC)
            {
                const Constraint& ScopedConstraint = ScopedCloth.constraints[ScopedC];
                const std::uint32_t ScopedA = ScopedConstraint.a;
                const std::uint32_t ScopedB = ScopedConstraint.b;
                const float ScopedMassA = ScopedCloth.invMass[ScopedA];
                const float ScopedMassB = ScopedCloth.invMass[ScopedB];
                const float ScopedMassSum = ScopedMassA + ScopedMassB;
                if (ScopedMassSum <= 0.0f)
                {
                    continue;
                }
                const Vector2D ScopedDelta = ScopedCloth.positions[ScopedB] - ScopedCloth.positions[ScopedA];
                const float ScopedLength = std::sqrt(ScopedDelta.LengthSquared());
                if (ScopedLength <= 1.0e-5f)
                {
                    continue;
                }
                const float ScopedDifference =
                    (ScopedLength - ScopedConstraint.restLength) / ScopedLength * ScopedConstraint.stiffness;
                const Vector2D ScopedCorrection = ScopedDelta * ScopedDifference;
                ScopedCloth.positions[ScopedA] += ScopedCorrection * (ScopedMassA / ScopedMassSum);
                ScopedCloth.positions[ScopedB] -= ScopedCorrection * (ScopedMassB / ScopedMassSum);
            }
            for (std::uint32_t ScopedI = 0; ScopedI < ScopedCloth.particleCount; ++ScopedI)
            {
                if (ScopedCloth.pinned[ScopedI] != 0)
                {
                    ScopedCloth.positions[ScopedI] = ScopedCloth.pins[ScopedI];
                }
            }
        }

        if ((ScopedCloth.flags & ClothCollideWorld) != 0 && MPhysics != nullptr && MPhysics->IsValid())
        {
            const b2WorldId ScopedWorld = b2LoadWorldId(static_cast<std::uint32_t>(MPhysics->WorldId()));
            const float ScopedPixelsPerMeter = MPhysics->PixelsPerMeter();
            const float ScopedRadiusMetres = ScopedCloth.particleRadius / ScopedPixelsPerMeter;
            b2QueryFilter ScopedFilter = b2DefaultQueryFilter();
            ScopedFilter.categoryBits = ScopedCloth.collisionCategory;
            ScopedFilter.maskBits = ScopedCloth.collisionMask;

            for (std::uint32_t ScopedI = 0; ScopedI < ScopedCloth.particleCount; ++ScopedI)
            {
                if (ScopedCloth.pinned[ScopedI] != 0)
                {
                    continue;
                }
                const Vector2D ScopedPosition = ScopedCloth.positions[ScopedI];
                const b2Pos ScopedTarget{ScopedPosition.X / ScopedPixelsPerMeter,
                                         ScopedPosition.Y / ScopedPixelsPerMeter};
                b2AABB ScopedAabb{};
                ScopedAabb.lowerBound =
                    b2Vec2{ScopedTarget.x - ScopedRadiusMetres, ScopedTarget.y - ScopedRadiusMetres};
                ScopedAabb.upperBound =
                    b2Vec2{ScopedTarget.x + ScopedRadiusMetres, ScopedTarget.y + ScopedRadiusMetres};

                CollisionContext ScopedQuery{ScopedTarget, 1.0e30f, b2Pos{}, false};
                b2World_OverlapAABB(ScopedWorld, b2Pos_zero, ScopedAabb, ScopedFilter, &CollisionCallback,
                                    &ScopedQuery);
                if (!ScopedQuery.found || ScopedQuery.bestDistanceSquared >= ScopedRadiusMetres * ScopedRadiusMetres)
                {
                    continue;
                }
                const float ScopedDistance = std::sqrt(ScopedQuery.bestDistanceSquared);
                Vector2D ScopedNormal{ScopedTarget.x - ScopedQuery.closest.x, ScopedTarget.y - ScopedQuery.closest.y};
                if (ScopedDistance > 1.0e-6f)
                {
                    ScopedNormal = ScopedNormal * (1.0f / ScopedDistance);
                }
                else
                {
                    ScopedNormal = Vector2D{0.0f, -1.0f};
                }
                const float ScopedPush = (ScopedRadiusMetres - ScopedDistance) * ScopedPixelsPerMeter;
                ScopedCloth.positions[ScopedI] += ScopedNormal * ScopedPush;
                // Remove the colliding velocity component: keeping it turns a
                ScopedCloth.previous[ScopedI] += ScopedNormal * ScopedPush;
            }
        }

        for (std::uint32_t ScopedI = 0; ScopedI < ScopedCloth.particleCount; ++ScopedI)
        {
            ScopedCloth.vertices[ScopedI].position = ScopedCloth.positions[ScopedI];
        }
    }
}

void ClothSystem::Update(Ecs::PhysicsSystemView& InView) noexcept
{
    if (MStorage != nullptr)
    {
        // A cloth whose marker entity was destroyed by the scene still owns a
        // slot; release it here instead of waiting for Shutdown.
        for (Runtime& ScopedCloth : MStorage->cloths)
        {
            if (!ScopedCloth.active || InView.registry.valid(ScopedCloth.entity))
            {
                continue;
            }
            const std::uint32_t ScopedNextGeneration =
                ScopedCloth.generation + 1u == 0 ? 1u : ScopedCloth.generation + 1u;
            if (ScopedCloth.stableId.IsValid())
            {
                MStableIds.Release(ScopedCloth.stableId);
            }
            *static_cast<ClothRuntime*>(&ScopedCloth) = ClothRuntime{};
            ScopedCloth.generation = ScopedNextGeneration;
            if (MLiveCloths > 0)
            {
                --MLiveCloths;
            }
        }
    }
    if (InView.dt > 0.0f)
    {
        Step(InView.dt);
    }
}

ClothInfo ClothSystem::Info(ClothHandle InHandle) const noexcept
{
    ClothInfo ScopedInfo{};
    const Runtime* ScopedCloth = Get(InHandle);
    if (ScopedCloth == nullptr)
    {
        return ScopedInfo;
    }
    ScopedInfo.Id = ScopedCloth->stableId;
    ScopedInfo.Columns = ScopedCloth->columns;
    ScopedInfo.Rows = ScopedCloth->rows;
    ScopedInfo.Particles = ScopedCloth->particleCount;
    ScopedInfo.Constraints = ScopedCloth->constraintCount;
    ScopedInfo.Entity = ScopedCloth->entity;
    return ScopedInfo;
}

std::span<const Rhi::MeshVertex> ClothSystem::Vertices(ClothHandle InHandle) const noexcept
{
    const Runtime* ScopedCloth = Get(InHandle);
    if (ScopedCloth == nullptr)
    {
        return {};
    }
    return {ScopedCloth->vertices.data(), ScopedCloth->particleCount};
}

std::span<const std::uint16_t> ClothSystem::Indices(ClothHandle InHandle) const noexcept
{
    const Runtime* ScopedCloth = Get(InHandle);
    if (ScopedCloth == nullptr)
    {
        return {};
    }
    return {ScopedCloth->indices.data(), ScopedCloth->indexCount};
}

std::span<const Vector2D> ClothSystem::ParticlePositions(ClothHandle InHandle) const noexcept
{
    const Runtime* ScopedCloth = Get(InHandle);
    if (ScopedCloth == nullptr)
    {
        return {};
    }
    return {ScopedCloth->positions.data(), ScopedCloth->particleCount};
}

bool ClothSystem::WriteMesh(ClothHandle InHandle, Rhi::MeshRenderItem& InItem) const noexcept
{
    const Runtime* ScopedCloth = Get(InHandle);
    if (ScopedCloth == nullptr || ScopedCloth->particleCount == 0 || ScopedCloth->indexCount == 0)
    {
        return false;
    }
    InItem.vertices = {ScopedCloth->vertices.data(), ScopedCloth->particleCount};
    InItem.indices = {ScopedCloth->indices.data(), ScopedCloth->indexCount};
    InItem.boneOffset = 0;
    InItem.boneCount = 0;
    InItem.textureArray = ScopedCloth->textureArray;
    InItem.layer = ScopedCloth->layer;
    InItem.model = Mat4::Identity();
    InItem.color = ScopedCloth->color;
    InItem.sortLayer = ScopedCloth->sortLayer;
    InItem.flags = Rhi::MeshItemUnskinned;
    InItem.depth = ScopedCloth->depth;
    InItem.materialPipeline = Rhi::kDefaultMaterialPipeline;
    InItem.mesh = {};
    return true;
}
}
