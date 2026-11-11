// FemboyEngine2D - ECS to SDF character mesh bridge.
#include "Engine/RHI/SdfCharacterRender.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace Fby::Rhi
{

void SdfCharacterRenderCache::Build(Ecs::Registry& InRegistry, std::uint32_t InResolution)
{
    MMeshes.clear();
    if (InResolution < 2 || InResolution > 256)
    {
        return;
    }
    std::vector<Entity> ScopedEntities;
    for (const Entity ScopedEntity : InRegistry.view<SkeletonComponent, SdfCharacterComponent>())
    {
        ScopedEntities.push_back(ScopedEntity);
    }
    for (const Entity ScopedEntity : ScopedEntities)
    {
        SkeletonComponent* ScopedSkeleton = InRegistry.try_get<SkeletonComponent>(ScopedEntity);
        const SdfCharacterComponent* ScopedCharacter = InRegistry.try_get<SdfCharacterComponent>(ScopedEntity);
        if (ScopedSkeleton == nullptr || ScopedCharacter == nullptr)
        {
            continue;
        }
        SkeletalAnimation::Evaluate(*ScopedSkeleton);
        Vector2D ScopedMin;
        Vector2D ScopedMax;
        if (!SdfCharacterBounds(*ScopedSkeleton, *ScopedCharacter, &ScopedMin, &ScopedMax))
        {
            continue;
        }
        const float ScopedWidth = ScopedMax.X - ScopedMin.X;
        const float ScopedHeight = ScopedMax.Y - ScopedMin.Y;
        if (ScopedWidth <= 0.0f || ScopedHeight <= 0.0f)
        {
            continue;
        }
        const float ScopedAspect = ScopedWidth / ScopedHeight;
        std::uint32_t ScopedWidthSamples = InResolution;
        std::uint32_t ScopedHeightSamples = InResolution;
        if (ScopedAspect > 1.0f)
        {
            ScopedHeightSamples = std::max<std::uint32_t>(2, static_cast<std::uint32_t>(
                                                                   static_cast<float>(InResolution) / ScopedAspect));
        }
        else
        {
            ScopedWidthSamples = std::max<std::uint32_t>(2, static_cast<std::uint32_t>(
                                                                  static_cast<float>(InResolution) * ScopedAspect));
        }
        SdfCharacterField ScopedField;
        if (!BakeSdfCharacterField(*ScopedSkeleton, *ScopedCharacter, ScopedMin, ScopedMax, ScopedWidthSamples,
                                   ScopedHeightSamples, &ScopedField))
        {
            continue;
        }
        SdfCharacterMeshData ScopedMesh;
        if (BuildSdfCharacterMesh(ScopedField, 0.0f, &ScopedMesh))
        {
            MMeshes.push_back(std::move(ScopedMesh));
        }
    }
}

void SdfCharacterRenderCache::Clear()
{
    MMeshes.clear();
}

}
