// FemboyEngine2D - opt-in transform hierarchy implementation.
#include "Engine/ECS/Hierarchy.hpp"

#include "Engine/Core/SimMath.hpp"

#include <unordered_map>

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
    const float ScopedCos = Sim::Cos(InRadians);
    const float ScopedSin = Sim::Sin(InRadians);
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

// One memoized world transform for a single update pass. InProgress marks a node
// on the current recursion stack, so a cycle is reported instead of reached again.
struct HierarchyEntry
{
    Transform2D World{};
    bool Resolved = false;
    bool InProgress = false;
};

// The scratch lives in the registry context and is reused across frames: after
// the first pass it only grows to the largest hierarchy the world has seen.
struct HierarchyScratch
{
    std::unordered_map<Entity, HierarchyEntry> Entries;
};

// Resolves the world transform of one entity and memoizes it, so a chain is
// composed once per update instead of once per descendant.
[[nodiscard]] const Transform2D* ResolveWorld(Registry& InRegistry, HierarchyScratch& InScratch, Entity InEntity,
                                              std::uint32_t InDepth, bool& OutOk)
{
    if (const auto ScopedFound = InScratch.Entries.find(InEntity); ScopedFound != InScratch.Entries.end())
    {
        if (ScopedFound->second.InProgress || !ScopedFound->second.Resolved)
        {
            OutOk = false;
            return nullptr;
        }
        return &ScopedFound->second.World;
    }

    const LocalTransform2D* ScopedLocal = InRegistry.try_get<LocalTransform2D>(InEntity);
    if (ScopedLocal == nullptr)
    {
        const Transform2D* ScopedWorld = InRegistry.try_get<Transform2D>(InEntity);
        if (ScopedWorld == nullptr)
        {
            OutOk = false;
            return nullptr;
        }
        HierarchyEntry& ScopedEntry = InScratch.Entries[InEntity];
        ScopedEntry.World = *ScopedWorld;
        ScopedEntry.Resolved = true;
        return &ScopedEntry.World;
    }

    const Parent2D* ScopedParent = InRegistry.try_get<Parent2D>(InEntity);
    if (ScopedParent == nullptr || ScopedParent->Parent == entt::null)
    {
        HierarchyEntry& ScopedEntry = InScratch.Entries[InEntity];
        ScopedEntry.World = Transform2D{ScopedLocal->Position, ScopedLocal->Scale, ScopedLocal->Rotation};
        ScopedEntry.Resolved = true;
        return &ScopedEntry.World;
    }
    if (InDepth >= kMaxHierarchyDepth || ScopedParent->Parent == InEntity || !InRegistry.valid(ScopedParent->Parent))
    {
        OutOk = false;
        return nullptr;
    }

    HierarchyEntry& ScopedEntry = InScratch.Entries[InEntity];
    ScopedEntry.InProgress = true;
    const Transform2D* ScopedParentWorld =
        ResolveWorld(InRegistry, InScratch, ScopedParent->Parent, InDepth + 1u, OutOk);
    if (ScopedParentWorld == nullptr)
    {
        return nullptr;
    }
    ScopedEntry.World = Compose(*ScopedParentWorld, *ScopedLocal);
    ScopedEntry.InProgress = false;
    ScopedEntry.Resolved = true;
    return &ScopedEntry.World;
}
}

bool UpdateHierarchy(Registry& InRegistry) noexcept
{
    HierarchyScratch& ScopedScratch = InRegistry.ctx().emplace<HierarchyScratch>();
    ScopedScratch.Entries.clear();
    bool ScopedOk = true;
    auto ScopedLocals = InRegistry.view<LocalTransform2D>();
    for (auto [ScopedEntity, ScopedLocal] : ScopedLocals.each())
    {
        (void)ScopedLocal;
        bool ScopedEntityOk = true;
        const Transform2D* ScopedWorld = ResolveWorld(InRegistry, ScopedScratch, ScopedEntity, 0, ScopedEntityOk);
        if (ScopedWorld == nullptr)
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
