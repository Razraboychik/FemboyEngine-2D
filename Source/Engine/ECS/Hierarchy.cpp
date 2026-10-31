// FemboyEngine2D - opt-in transform hierarchy implementation.
#include "Engine/ECS/Hierarchy.hpp"

#include <cmath>
#include <optional>

namespace Fby::Ecs
{
namespace
{
[[nodiscard]] Vector2D Rotate(Vector2D InValue, float InRadians) noexcept
{
    if (InRadians == 0.0f)
    {
        return InValue;
    }
    const float ScopedCos = std::cos(InRadians);
    const float ScopedSin = std::sin(InRadians);
    return Vector2D{InValue.X * ScopedCos - InValue.Y * ScopedSin,
                    InValue.X * ScopedSin + InValue.Y * ScopedCos};
}

[[nodiscard]] Transform2D Compose(const Transform2D& InParent, const LocalTransform2D& InLocal) noexcept
{
    Transform2D ScopedResult;
    const Vector2D ScopedScaled{InLocal.Position.X * InParent.Scale.X, InLocal.Position.Y * InParent.Scale.Y};
    const Vector2D ScopedOffset = Rotate(ScopedScaled, InParent.Rotation);
    ScopedResult.Position = Vector2D{InParent.Position.X + ScopedOffset.X, InParent.Position.Y + ScopedOffset.Y};
    ScopedResult.Scale = Vector2D{InParent.Scale.X * InLocal.Scale.X, InParent.Scale.Y * InLocal.Scale.Y};
    ScopedResult.Rotation = InParent.Rotation + InLocal.Rotation;
    return ScopedResult;
}

// Recursive world transform for one entity. A missing parent, a dangling handle
// or a cycle (bounded by kMaxHierarchyDepth) yields no value.
[[nodiscard]] std::optional<Transform2D> ComputeWorld(Registry& InRegistry, Entity InEntity,
                                                      std::uint32_t InDepth) noexcept
{
    const LocalTransform2D* ScopedLocal = InRegistry.try_get<LocalTransform2D>(InEntity);
    if (ScopedLocal == nullptr)
    {
        const Transform2D* ScopedWorld = InRegistry.try_get<Transform2D>(InEntity);
        return ScopedWorld != nullptr ? std::optional<Transform2D>(*ScopedWorld) : std::nullopt;
    }
    const Parent2D* ScopedParent = InRegistry.try_get<Parent2D>(InEntity);
    if (ScopedParent == nullptr || ScopedParent->Parent == entt::null)
    {
        return Transform2D{ScopedLocal->Position, ScopedLocal->Scale, ScopedLocal->Rotation};
    }
    if (InDepth >= kMaxHierarchyDepth || ScopedParent->Parent == InEntity ||
        !InRegistry.valid(ScopedParent->Parent))
    {
        return std::nullopt;
    }
    const std::optional<Transform2D> ScopedParentWorld =
        ComputeWorld(InRegistry, ScopedParent->Parent, InDepth + 1u);
    if (!ScopedParentWorld.has_value())
    {
        return std::nullopt;
    }
    return Compose(*ScopedParentWorld, *ScopedLocal);
}
}

bool UpdateHierarchy(Registry& InRegistry) noexcept
{
    auto ScopedLocals = InRegistry.view<LocalTransform2D>();
    bool ScopedOk = true;
    for (auto [ScopedEntity, ScopedLocal] : ScopedLocals.each())
    {
        (void)ScopedLocal;
        const std::optional<Transform2D> ScopedWorld = ComputeWorld(InRegistry, ScopedEntity, 0);
        if (!ScopedWorld.has_value())
        {
            ScopedOk = false;
            continue;
        }
        Transform2D* ScopedTarget = InRegistry.try_get<Transform2D>(ScopedEntity);
        if (ScopedTarget == nullptr)
        {
            ScopedTarget = &InRegistry.emplace<Transform2D>(ScopedEntity);
        }
        *ScopedTarget = *ScopedWorld;
    }
    return ScopedOk;
}
}
