// FemboyEngine2D - ECS to SDF character mesh bridge.
#pragma once

#include "Engine/ECS/ComponentRegistry.hpp"
#include "Engine/Physics/SdfCharacter.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace Fby::Rhi
{

// Builds one mesh per entity that has both SkeletonComponent and
// SdfCharacterComponent. The field resolution is fixed by the caller, never by
// camera distance; the skeleton pose is evaluated before each bake.
class SdfCharacterRenderCache
{
public:
    void Build(Ecs::Registry& InRegistry, std::uint32_t InResolution = 64);
    void Clear();

    [[nodiscard]] std::span<const SdfCharacterMeshData> Meshes() const noexcept
    {
        return MMeshes;
    }

private:
    std::vector<SdfCharacterMeshData> MMeshes;
};

}
