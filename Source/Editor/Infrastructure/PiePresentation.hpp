// FemboyEngine2D - PIE client presentation helpers.
#pragma once

#include "Engine/ECS/Lighting.hpp"
#include "Engine/Physics/SdfAtlas.hpp"
#include "Engine/RHI/RHI.hpp"
#include "Engine/RHI/RenderItem.hpp"

namespace Fby::Editor
{

// Builds the SDF lighting frame for the replicated client world. The static
// field and atlas are shared with the authoring pass; dynamic casters and the
// camera come from the PIE world. Returns false when the world has no sun.
[[nodiscard]] bool BuildPieClientSdfFrame(Ecs::Registry& InWorld, const SdfField* InField,
                                          const SdfAtlasImage& InAtlas, Vector2D InCenter, float InZoom,
                                          Rhi::Extent2D InViewport, Vector4D* InPreviousCamera,
                                          Rhi::SdfLightingFrame* OutFrame) noexcept;

}
