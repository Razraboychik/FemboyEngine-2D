// FemboyEngine2D - Inspector panel for an asset-backed SkeletalAnimation skeleton.
#pragma once

#include "Engine/ECS/Components.hpp"
#include "Engine/ECS/SkeletonAssetRef.hpp"
#include "Engine/ECS/SkeletonAsset.hpp"

#include <cstdint>
#include <string>

namespace Fby::Ecs
{
struct SystemContext;
}

namespace Fby::Editor
{

struct SkeletonInspectorState
{
    Entity entity = entt::null;
    std::string path;
    std::string status;
    Skeleton::SkeletonNameTable names;
    Skeleton::FesklSprite sprite;
    std::uint64_t contentHash = 0;
    bool loaded = false;
    bool dirty = false;
    bool open = true;
};

void DrawSkeletonAssetInspector(Ecs::SystemContext& InContext, Entity InEntity, SkeletonInspectorState& InState);
}
