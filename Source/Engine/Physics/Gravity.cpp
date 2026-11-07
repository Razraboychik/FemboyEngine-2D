// FemboyEngine2D - level gravity and local gravity fields.
#include "Engine/Physics/Gravity.hpp"

#include "Engine/Core/SimMath.hpp"
#include "Engine/ECS/Components.hpp"
#include "Engine/ECS/SystemGraph.hpp"
#include "Engine/ECS/Tilemap.hpp"

#include <algorithm>
#include <cmath>

namespace Fby
{
namespace
{
[[nodiscard]] Vector2D NormalizedOrZero(Vector2D InValue) noexcept
{
    const float ScopedLengthSquared = InValue.LengthSquared();
    if (ScopedLengthSquared <= 1.0e-8f)
    {
        return Vector2D{0.0f, 0.0f};
    }
    const float ScopedInverse = 1.0f / std::sqrt(ScopedLengthSquared);
    return Vector2D{InValue.X * ScopedInverse, InValue.Y * ScopedInverse};
}

[[nodiscard]] float FalloffFactor(GravityFalloff InFalloff, float InT, float InPower) noexcept
{
    const float ScopedT = std::clamp(InT, 0.0f, 1.0f);
    switch (InFalloff)
    {
        case GravityFalloff::None:
            return 1.0f;
        case GravityFalloff::Linear:
            return ScopedT;
        case GravityFalloff::Quadratic:
            return ScopedT * ScopedT;
        case GravityFalloff::Exponential:
        {
            const float ScopedPower = std::clamp(InPower, 1.0f, 16.0f);
            const float ScopedScale = Sim::Exp(ScopedPower) - 1.0f;
            return ScopedScale > 1.0e-6f ? (Sim::Exp(ScopedPower * ScopedT) - 1.0f) / ScopedScale : ScopedT;
        }
    }
    return ScopedT;
}
}

Vector2D DefaultGravityVector(Grid::ProjectionMode InMode) noexcept
{
    switch (InMode)
    {
        case Grid::ProjectionMode::Side:
            return Vector2D{0.0f, 9.8f};
        case Grid::ProjectionMode::TopDown:
        case Grid::ProjectionMode::IsoRect:
        case Grid::ProjectionMode::IsoHex:
            return Vector2D{0.0f, 0.0f};
    }
    return Vector2D{0.0f, 0.0f};
}

Vector2D GravityVector(GravityDirection InDirection, Vector2D InCustom, float InStrength) noexcept
{
    const float ScopedStrength = std::max(InStrength, 0.0f);
    Vector2D ScopedDirection{0.0f, 0.0f};
    switch (InDirection)
    {
        case GravityDirection::Down:
            ScopedDirection = Vector2D{0.0f, 1.0f};
            break;
        case GravityDirection::Up:
            ScopedDirection = Vector2D{0.0f, -1.0f};
            break;
        case GravityDirection::Left:
            ScopedDirection = Vector2D{-1.0f, 0.0f};
            break;
        case GravityDirection::Right:
            ScopedDirection = Vector2D{1.0f, 0.0f};
            break;
        case GravityDirection::Custom:
            ScopedDirection = NormalizedOrZero(InCustom);
            break;
        case GravityDirection::None:
            ScopedDirection = Vector2D{0.0f, 0.0f};
            break;
    }
    return ScopedDirection * ScopedStrength;
}

const char* GravityDirectionName(GravityDirection InDirection) noexcept
{
    switch (InDirection)
    {
        case GravityDirection::Down:
            return "Down";
        case GravityDirection::Up:
            return "Up";
        case GravityDirection::Left:
            return "Left";
        case GravityDirection::Right:
            return "Right";
        case GravityDirection::Custom:
            return "Custom";
        case GravityDirection::None:
            return "None";
    }
    return "?";
}

void GravitySystem::Update(Registry& InRegistry, PhysicsSystem& InPhysics, float InDeltaSeconds) noexcept
{
    (void)InDeltaSeconds;
    // Local fields act on bodies that already exist. Creating missing bodies here
    // removes the one-tick delay on the first frame after a scene load.
    InPhysics.EnsureBodies(InRegistry);
    // The scene setting is a singleton by contract, but a duplicated component
    // must still resolve the same way every tick: pick the lowest entity id.
    const LevelGravityComponent* ScopedLevel = nullptr;
    Entity ScopedLevelOwner = entt::null;
    auto ScopedLevelView = InRegistry.view<LevelGravityComponent>();
    for (const Entity ScopedEntity : ScopedLevelView)
    {
        if (ScopedLevel == nullptr || entt::to_integral(ScopedEntity) < entt::to_integral(ScopedLevelOwner))
        {
            ScopedLevelOwner = ScopedEntity;
            ScopedLevel = &ScopedLevelView.get<LevelGravityComponent>(ScopedEntity);
        }
    }

    float ScopedMaxFallSpeed = 0.0f;
    Vector2D ScopedGravity = InPhysics.Gravity();
    if (ScopedLevel != nullptr)
    {
        // A later removal must let projection inference take over again.
        MWorldGravityApplied = false;
        if (ScopedLevel->ApplyWorldGravity)
        {
            ScopedGravity = ScopedLevel->Enabled ? GravityVector(ScopedLevel->Direction(), ScopedLevel->CustomDirection,
                                                                 ScopedLevel->Strength)
                                                 : Vector2D{0.0f, 0.0f};
            InPhysics.SetGravity(ScopedGravity);
        }
        ScopedMaxFallSpeed = ScopedLevel->Enabled ? std::max(ScopedLevel->MaxFallSpeed, 0.0f) : 0.0f;
    }
    else if (InferFromProjection)
    {
        const TilemapComponent* ScopedTilemap = nullptr;
        auto ScopedTilemapView = InRegistry.view<TilemapComponent>();
        for (const Entity ScopedEntity : ScopedTilemapView)
        {
            ScopedTilemap = &ScopedTilemapView.get<TilemapComponent>(ScopedEntity);
            break;
        }
        if (ScopedTilemap != nullptr &&
            (!MHasProjection || ScopedTilemap->Grid.Mode != MLastProjection || !MWorldGravityApplied))
        {
            MLastProjection = ScopedTilemap->Grid.Mode;
            MHasProjection = true;
            ScopedGravity = DefaultGravityVector(MLastProjection);
            InPhysics.SetGravity(ScopedGravity);
            MWorldGravityApplied = true;
        }
    }

    // Terminal velocity along the world-gravity direction. It is a gameplay
    // guard, not part of the Box2D solver.
    if (ScopedMaxFallSpeed > 0.0f)
    {
        const Vector2D ScopedDown = NormalizedOrZero(ScopedGravity);
        if (ScopedDown.LengthSquared() > 0.0f)
        {
            auto ScopedBodies = InRegistry.view<RigidBody2D>();
            for (const Entity ScopedEntity : ScopedBodies)
            {
                RigidBody2D& ScopedBody = ScopedBodies.get<RigidBody2D>(ScopedEntity);
                const float ScopedAlong =
                    ScopedBody.LinearVelocity.X * ScopedDown.X + ScopedBody.LinearVelocity.Y * ScopedDown.Y;
                if (ScopedAlong > ScopedMaxFallSpeed)
                {
                    // Write through the physics bridge: the DOD backend integrates
                    // its own velocity columns, so a component write is lost there.
                    const Vector2D ScopedClamped =
                        ScopedBody.LinearVelocity - ScopedDown * (ScopedAlong - ScopedMaxFallSpeed);
                    InPhysics.SetLinearVelocity(InRegistry, ScopedEntity, ScopedClamped, false);
                }
            }
        }
    }

    // Local fields are independent of world gravity and stack additively. The
    // field source is any transform entity; every dynamic body is a target.
    // Fields form the outer loop: the source pose, shape extents and rotation
    // are resolved once per field instead of once per body-field pair.
    auto ScopedBodyView = InRegistry.view<RigidBody2D>();
    auto ScopedFieldView = InRegistry.view<GravityBodyComponent, Transform2D>();
    for (auto [ScopedSource, ScopedField, ScopedTransform] : ScopedFieldView.each())
    {
        if (!ScopedField.Enabled)
        {
            continue;
        }
        const float ScopedCos = Sim::Cos(ScopedTransform.Rotation);
        const float ScopedSin = Sim::Sin(ScopedTransform.Rotation);
        const Vector2D ScopedCenter{
            ScopedTransform.Position.X + ScopedField.Offset.X * ScopedCos - ScopedField.Offset.Y * ScopedSin,
            ScopedTransform.Position.Y + ScopedField.Offset.X * ScopedSin + ScopedField.Offset.Y * ScopedCos};
        const GravityBodyShape ScopedShape = ScopedField.ShapeValue();
        const GravityBodyMode ScopedMode = ScopedField.ModeValue();
        const float ScopedRadius = std::max(ScopedField.Radius, 0.0f);
        const float ScopedHalfX = std::max(ScopedField.HalfExtents.X, 0.0f);
        const float ScopedHalfY = std::max(ScopedField.HalfExtents.Y, 0.0f);
        const float ScopedEdgeDepth =
            ScopedShape == GravityBodyShape::Circle
                ? std::max(ScopedField.FalloffDepth, ScopedRadius)
                : std::max(ScopedField.FalloffDepth, std::min(ScopedHalfX, ScopedHalfY));

        for (auto [ScopedTarget, ScopedBody] : ScopedBodyView.each())
        {
            if (ScopedTarget == ScopedSource || ScopedBody.Type != BodyType::Dynamic)
            {
                continue;
            }
            const auto* ScopedCollider = InRegistry.try_get<Collider2D>(ScopedTarget);
            if (ScopedCollider != nullptr && (ScopedCollider->CategoryBits & ScopedField.AffectsCategoryBits) == 0)
            {
                continue;
            }
            const Vector2D ScopedDelta = ScopedBody.Position - ScopedCenter;
            float ScopedEdge = 0.0f;
            if (ScopedShape == GravityBodyShape::Circle)
            {
                ScopedEdge = ScopedRadius - std::sqrt(ScopedDelta.LengthSquared());
            }
            else
            {
                const float ScopedLocalX = ScopedDelta.X * ScopedCos + ScopedDelta.Y * ScopedSin;
                const float ScopedLocalY = -ScopedDelta.X * ScopedSin + ScopedDelta.Y * ScopedCos;
                ScopedEdge = std::min(ScopedHalfX - std::fabs(ScopedLocalX), ScopedHalfY - std::fabs(ScopedLocalY));
            }
            if (ScopedEdge <= 0.0f)
            {
                continue;
            }
            const float ScopedT = ScopedEdgeDepth > 1.0e-6f ? std::min(ScopedEdge / ScopedEdgeDepth, 1.0f) : 1.0f;
            const float ScopedFactor = FalloffFactor(ScopedField.FalloffValue(), ScopedT, ScopedField.FalloffPower);
            float ScopedAcceleration = std::max(ScopedField.Strength, 0.0f) * ScopedFactor;
            if (ScopedField.MaxAcceleration > 0.0f)
            {
                ScopedAcceleration = std::min(ScopedAcceleration, ScopedField.MaxAcceleration);
            }
            if (!(ScopedAcceleration > 0.0f))
            {
                continue;
            }
            // Directional fields push along an explicit local axis; the zone
            // shape still bounds where the field acts.
            Vector2D ScopedDirection{0.0f, 0.0f};
            if (ScopedMode == GravityBodyMode::Directional)
            {
                const Vector2D ScopedLocal = ScopedField.Direction;
                ScopedDirection = NormalizedOrZero(Vector2D{
                    ScopedLocal.X * ScopedCos - ScopedLocal.Y * ScopedSin,
                    ScopedLocal.X * ScopedSin + ScopedLocal.Y * ScopedCos});
            }
            else
            {
                ScopedDirection = ScopedMode == GravityBodyMode::Attract ? ScopedCenter - ScopedBody.Position
                                                                        : ScopedBody.Position - ScopedCenter;
                ScopedDirection = NormalizedOrZero(ScopedDirection);
            }
            if (ScopedDirection.LengthSquared() <= 0.0f)
            {
                continue;
            }
            // Constant fields do not wake sleeping bodies; a force that only
            // exists while the body is already awake would otherwise keep every
            // body inside a field out of the sleep schedule.
            InPhysics.ApplyAcceleration(InRegistry, ScopedTarget, ScopedDirection * ScopedAcceleration, false);
        }
    }
}

void GravitySystem::Update(Ecs::PhysicsSystemView& InView) noexcept
{
    if (InView.services.physics != nullptr)
    {
        Update(InView.registry, *InView.services.physics, InView.dt);
    }
}

namespace
{
void GravitySystemThunk(Ecs::PhysicsSystemView& InView, void* InUser)
{
    static_cast<GravitySystem*>(InUser)->Update(InView);
}
}

bool GravitySystem::Register(Ecs::SystemGraph& InGraph, GravitySystem& InSystem)
{
    return InGraph.Add(Ecs::Phase::FixedUpdate, "GravitySystem", Ecs::BindPhysicsSystem<&GravitySystemThunk>(),
                       &InSystem);
}
}
