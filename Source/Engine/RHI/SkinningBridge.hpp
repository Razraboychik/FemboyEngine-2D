// FemboyEngine2D - the CPU -> GPU boundary for SkeletalAnimation skinning.
#pragma once

#include "Engine/Core/Mat4.hpp"
#include "Engine/ECS/Components.hpp"
#include "Engine/ECS/Skeleton.hpp"
#include "Engine/RHI/Mesh.hpp"

#include <cstdint>
#include <span>

namespace Fby::Render
{

// Converts up to kMaxSkinVertices SkeletalAnimation vertices into the std430 mesh layout.
void BuildMeshVertices(const SkinComponent& InSkin, std::span<const Vector2D> InUvs, Rhi::MeshVertex* Out,
                       std::uint32_t InColor = 0xFFFFFFFFu) noexcept;

// The final LBS palette. The span aliases SkeletonComponent::skinningMatrices
[[nodiscard]] std::span<const Mat4> SkeletonPalette(const SkeletonComponent& InSkeleton) noexcept;

// 2D entity transform -> column-major Mat4.
[[nodiscard]] Mat4 MakeModelMatrix(const Transform2D& InTransform) noexcept;
}
