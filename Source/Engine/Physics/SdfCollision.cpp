// FemboyEngine2D - SDF terrain collision for Box2D v3.
#include "Engine/Physics/SdfCollision.hpp"

#include "Engine/ECS/SystemGraph.hpp"

#include <box2d/box2d.h>

#include <algorithm>
#include <cmath>

namespace Fby
{
namespace
{

[[nodiscard]] b2Pos ToBox(Vector2D InValue, float InPixelsPerMeter) noexcept
{
    b2Pos ScopedResult{};
    ScopedResult.x = static_cast<decltype(ScopedResult.x)>(InValue.X / InPixelsPerMeter);
    ScopedResult.y = static_cast<decltype(ScopedResult.y)>(InValue.Y / InPixelsPerMeter);
    return ScopedResult;
}

[[nodiscard]] b2Vec2 ToBoxVelocity(Vector2D InValue, float InPixelsPerMeter) noexcept
{
    return b2Vec2{InValue.X / InPixelsPerMeter, InValue.Y / InPixelsPerMeter};
}

[[nodiscard]] Vector2D ToPixelsVelocity(b2Vec2 InValue, float InPixelsPerMeter) noexcept
{
    return Vector2D{InValue.x * InPixelsPerMeter, InValue.y * InPixelsPerMeter};
}

[[nodiscard]] Vector2D Rotate(Vector2D InValue, float InRadians) noexcept
{
    const float ScopedCos = std::cos(InRadians);
    const float ScopedSin = std::sin(InRadians);
    return Vector2D{InValue.X * ScopedCos - InValue.Y * ScopedSin,
                    InValue.X * ScopedSin + InValue.Y * ScopedCos};
}
}

float SdfField::At(std::uint32_t InX, std::uint32_t InY) const noexcept
{
    if (InX >= width || InY >= height || samples == nullptr)
    {
        return outsideDistance;
    }
    return View2D()[std::array<std::size_t, 2>{static_cast<std::size_t>(InY), static_cast<std::size_t>(InX)}];
}

float SdfField::Sample(Vector2D InWorld) const noexcept
{
    if (!Valid())
    {
        return outsideDistance;
    }
    const float ScopedX = (InWorld.X - origin.X) / cellSize.X;
    const float ScopedY = (InWorld.Y - origin.Y) / cellSize.Y;
    if (ScopedX < 0.0f || ScopedY < 0.0f || ScopedX > static_cast<float>(width - 1) ||
        ScopedY > static_cast<float>(height - 1))
    {
        return outsideDistance;
    }
    const auto ScopedX0 = static_cast<std::uint32_t>(ScopedX);
    const auto ScopedY0 = static_cast<std::uint32_t>(ScopedY);
    const std::uint32_t ScopedX1 = std::min(ScopedX0 + 1, width - 1);
    const std::uint32_t ScopedY1 = std::min(ScopedY0 + 1, height - 1);
    const float ScopedFx = ScopedX - static_cast<float>(ScopedX0);
    const float ScopedFy = ScopedY - static_cast<float>(ScopedY0);
    const float Scoped00 = At(ScopedX0, ScopedY0);
    const float Scoped10 = At(ScopedX1, ScopedY0);
    const float Scoped01 = At(ScopedX0, ScopedY1);
    const float Scoped11 = At(ScopedX1, ScopedY1);
    const float ScopedTop = Scoped00 + (Scoped10 - Scoped00) * ScopedFx;
    const float ScopedBottom = Scoped01 + (Scoped11 - Scoped01) * ScopedFx;
    return ScopedTop + (ScopedBottom - ScopedTop) * ScopedFy;
}

Vector2D SdfField::Gradient(Vector2D InWorld) const noexcept
{
    const float ScopedStepX = std::max(cellSize.X, 0.001f);
    const float ScopedStepY = std::max(cellSize.Y, 0.001f);
    const float ScopedDx = (Sample(Vector2D{InWorld.X + ScopedStepX, InWorld.Y}) -
                            Sample(Vector2D{InWorld.X - ScopedStepX, InWorld.Y})) /
                           (2.0f * ScopedStepX);
    const float ScopedDy = (Sample(Vector2D{InWorld.X, InWorld.Y + ScopedStepY}) -
                            Sample(Vector2D{InWorld.X, InWorld.Y - ScopedStepY})) /
                           (2.0f * ScopedStepY);
    Vector2D ScopedNormal{ScopedDx, ScopedDy};
    const float ScopedLength = std::sqrt(ScopedNormal.LengthSquared());
    if (ScopedLength < 1.0e-6f)
    {
        return Vector2D{0.0f, -1.0f};
    }
    return ScopedNormal / ScopedLength;
}

void SdfCollisionSystem::Initialize(const SdfCollisionDesc& InDesc) noexcept
{
    MDesc = InDesc;
    MContacts.fill(SdfContact{});
    MContactCount = 0;
    MSequence = 0;
    MLastCorrected = 0;
}

void SdfCollisionSystem::SetField(const SdfField& InField) noexcept
{
    MField = InField;
}

std::uint32_t SdfCollisionSystem::Step(Ecs::Registry& InRegistry, PhysicsSystem& InPhysics, float InFixedDt) noexcept
{
    MContactCount = 0;
    MLastCorrected = 0;
    if (!MField.Valid() || InFixedDt <= 0.0f)
    {
        return 0;
    }
    const float ScopedPixelsPerMeter = std::max(InPhysics.PixelsPerMeter(), 0.001f);
    auto ScopedView = InRegistry.view<RigidBody2D, Collider2D, Transform2D>();
    for (const Entity ScopedEntity : ScopedView)
    {
        RigidBody2D& ScopedBody = ScopedView.get<RigidBody2D>(ScopedEntity);
        const Collider2D& ScopedCollider = ScopedView.get<Collider2D>(ScopedEntity);
        Transform2D& ScopedTransform = ScopedView.get<Transform2D>(ScopedEntity);
        if (ScopedBody.Body == 0 || ScopedCollider.Sensor || (ScopedBody.Flags & RigidBodyEnabled) == 0)
        {
            continue;
        }
        if (MDesc.dynamicOnly && ScopedBody.Type != BodyType::Dynamic)
        {
            continue;
        }
        const b2BodyId ScopedBodyId = b2LoadBodyId(ScopedBody.Body);
        if (!b2Body_IsValid(ScopedBodyId))
        {
            continue;
        }
        const b2WorldTransform ScopedWorld = b2Body_GetTransform(ScopedBodyId);
        const Vector2D ScopedCenter{static_cast<float>(ScopedWorld.p.x) * ScopedPixelsPerMeter,
                                    static_cast<float>(ScopedWorld.p.y) * ScopedPixelsPerMeter};
        const float ScopedRotation = b2Rot_GetAngle(ScopedWorld.q);
        const std::uint32_t ScopedSamples = std::max(MDesc.subSamples, 1u) + 1u;

        float ScopedMinDistance = MField.outsideDistance;
        Vector2D ScopedDeepest = ScopedCenter;
        bool ScopedTouchedInside = false;
        for (std::uint32_t ScopedIy = 0; ScopedIy < ScopedSamples; ++ScopedIy)
        {
            const float ScopedY = -ScopedCollider.HalfExtents.Y +
                                  (2.0f * ScopedCollider.HalfExtents.Y) * static_cast<float>(ScopedIy) /
                                      static_cast<float>(ScopedSamples - 1);
            for (std::uint32_t ScopedIx = 0; ScopedIx < ScopedSamples; ++ScopedIx)
            {
                const float ScopedX = -ScopedCollider.HalfExtents.X +
                                      (2.0f * ScopedCollider.HalfExtents.X) * static_cast<float>(ScopedIx) /
                                          static_cast<float>(ScopedSamples - 1);
                const Vector2D ScopedLocal{ScopedX, ScopedY};
                const Vector2D ScopedSample = ScopedCenter + Rotate(ScopedLocal, ScopedRotation);
                const float ScopedDistance = MField.Sample(ScopedSample);
                if (ScopedDistance < MField.outsideDistance)
                {
                    ScopedTouchedInside = true;
                }
                if (ScopedDistance < ScopedMinDistance)
                {
                    ScopedMinDistance = ScopedDistance;
                    ScopedDeepest = ScopedSample;
                }
            }
        }
        if (!ScopedTouchedInside || ScopedMinDistance >= 0.0f)
        {
            continue;
        }

        const float ScopedPenetration = -ScopedMinDistance;
        Vector2D ScopedNormal = MField.Gradient(ScopedDeepest);
        if (ScopedNormal.LengthSquared() < 1.0e-8f)
        {
            ScopedNormal = Vector2D{0.0f, -1.0f};
        }
        const float ScopedMass = std::max(b2Body_GetMass(ScopedBodyId), 0.001f);
        const Vector2D ScopedVelocity = ToPixelsVelocity(b2Body_GetLinearVelocity(ScopedBodyId), ScopedPixelsPerMeter);
        const float ScopedInward = ScopedVelocity.Dot(ScopedNormal);
        Vector2D ScopedReflected = ScopedVelocity;
        if (ScopedInward < 0.0f)
        {
            ScopedReflected = ScopedVelocity - ScopedNormal * (ScopedInward * (1.0f + std::clamp(MDesc.restitution, 0.0f, 1.0f)));
        }
        const float ScopedImpulseMagnitude =
            std::min(ScopedPenetration * ScopedMass * 60.0f, std::max(MDesc.maxImpulse, 0.0f));
        // Set the reflected velocity first, then let the impulse add the push
        b2Body_SetLinearVelocity(ScopedBodyId, ToBoxVelocity(ScopedReflected, ScopedPixelsPerMeter));
        if (ScopedImpulseMagnitude > 0.0f)
        {
            b2Body_ApplyLinearImpulseToCenter(ScopedBodyId,
                                              ToBoxVelocity(ScopedNormal * ScopedImpulseMagnitude, ScopedPixelsPerMeter),
                                              true);
        }
        const Vector2D ScopedNewVelocity = ScopedReflected + ScopedNormal * (ScopedImpulseMagnitude / ScopedMass);
        const Vector2D ScopedCorrected =
            ScopedCenter + ScopedNormal * (ScopedPenetration * std::clamp(MDesc.pushOut, 0.0f, 1.0f));
        b2Body_SetTransform(ScopedBodyId, ToBox(ScopedCorrected, ScopedPixelsPerMeter), ScopedWorld.q);

        ScopedBody.PreviousPosition = ScopedBody.Position;
        ScopedBody.Position = ScopedCorrected;
        ScopedBody.PreviousRotation = ScopedBody.Rotation;
        ScopedBody.Rotation = ScopedRotation;
        ScopedBody.LinearVelocity = ScopedNewVelocity;
        ScopedTransform.Position = ScopedCorrected;
        ScopedTransform.Rotation = ScopedRotation;
        ++MLastCorrected;

        if (MContactCount < kMaxContacts)
        {
            SdfContact& ScopedContact = MContacts[MContactCount++];
            ScopedContact.entity = ScopedEntity;
            ScopedContact.point = ScopedDeepest;
            ScopedContact.normal = ScopedNormal;
            ScopedContact.penetration = ScopedPenetration;
            ScopedContact.impulse = ScopedImpulseMagnitude;
            ScopedContact.sequence = ++MSequence;
        }
    }
    return MLastCorrected;
}

std::uint32_t SdfCollisionSystem::Update(Ecs::SystemContext& InContext) noexcept
{
    if (InContext.physics == nullptr || InContext.dt <= 0.0f)
    {
        return 0;
    }
    return Step(InContext.registry, *InContext.physics, std::clamp(InContext.dt, 1.0f / 240.0f, 1.0f / 30.0f));
}

bool SdfCollisionSystem::Register(Ecs::SystemGraph& InGraph)
{
    return InGraph.Add(Ecs::Phase::FixedUpdate, "SdfCollisionSystem", &SdfCollisionSystem::Tick, this);
}

void SdfCollisionSystem::Tick(Ecs::SystemContext& InContext, void* InUser)
{
    static_cast<SdfCollisionSystem*>(InUser)->Update(InContext);
}
}
