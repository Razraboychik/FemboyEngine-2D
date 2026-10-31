// FemboyEngine2D - cloth render bridge tests.
#include "Engine/Physics/ClothRenderSystem.hpp"

#include "Engine/Core/JobSystem.hpp"
#include "Engine/ECS/Components.hpp"
#include "Engine/RHI/Mesh.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace
{
std::uint32_t GFailures = 0;

#define FBY_CHECK(condition)                                                                                           \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);                                  \
            ++GFailures;                                                                                              \
        }                                                                                                              \
    } while (false)
}

int main()
{
    using namespace Fby;

    // RHI classification: the single source of truth the Vulkan path uses.
    Rhi::MeshRenderItem ScopedSkinned;
    ScopedSkinned.boneCount = 32;
    FBY_CHECK(!Rhi::IsUnskinnedMeshItem(ScopedSkinned));
    ScopedSkinned.boneCount = 0;
    FBY_CHECK(Rhi::IsUnskinnedMeshItem(ScopedSkinned));
    ScopedSkinned.boneCount = 32;
    ScopedSkinned.flags = Rhi::MeshItemUnskinned;
    FBY_CHECK(Rhi::IsUnskinnedMeshItem(ScopedSkinned));

    ClothSystem ScopedCloth;
    FBY_CHECK(ScopedCloth.Initialize(nullptr));

    Ecs::Registry ScopedRegistry;
    JobSystem ScopedJobs(1);
    Ecs::SystemContext ScopedContext{ScopedRegistry, ScopedJobs};
    ScopedContext.cloth = &ScopedCloth;

    ClothDesc ScopedDesc;
    ScopedDesc.origin = Vector2D{0.0f, 0.0f};
    ScopedDesc.size = Vector2D{64.0f, 64.0f};
    ScopedDesc.columns = 4;
    ScopedDesc.rows = 4;
    ScopedDesc.flags = 0;
    ScopedDesc.gravity = Vector2D{0.0f, 0.0f};
    ScopedDesc.wind = Vector2D{0.0f, 0.0f};
    ScopedDesc.solverIterations = 2;
    const ClothHandle ScopedHandle = ScopedCloth.Create(ScopedRegistry, ScopedDesc);
    FBY_CHECK(ScopedHandle.IsValid());
    // ClothSystem::Create() owns the marker entity: it emplaces ClothComponent
    FBY_CHECK(ScopedRegistry.view<ClothComponent>().size() == 1);

    ScopedCloth.Step(1.0f / 120.0f);

    ClothRenderSystem ScopedBridge;
    ScopedBridge.Initialize(&ScopedCloth);
    ScopedBridge.Collect(ScopedContext);
    FBY_CHECK(!ScopedBridge.Truncated());
    FBY_CHECK(ScopedBridge.DrawCount() == 1);

    const std::span<const Rhi::MeshRenderItem> ScopedItems = ScopedBridge.Items();
    FBY_CHECK(ScopedItems.size() == 1);
    const Rhi::MeshRenderItem& ScopedItem = ScopedItems[0];
    FBY_CHECK((ScopedItem.flags & Rhi::MeshItemUnskinned) != 0);
    FBY_CHECK(ScopedItem.boneCount == 0);
    FBY_CHECK(ScopedItem.vertices.size() == 16);
    FBY_CHECK(ScopedItem.indices.size() == 54);
    FBY_CHECK(Rhi::IsUnskinnedMeshItem(ScopedItem));

    // Non-collapse: the mesh still spans the authored 64x64 pixels. A cloth that
    float ScopedMinX = ScopedItem.vertices[0].position.X;
    float ScopedMaxX = ScopedMinX;
    float ScopedMinY = ScopedItem.vertices[0].position.Y;
    float ScopedMaxY = ScopedMinY;
    bool ScopedFinite = true;
    for (const Rhi::MeshVertex& ScopedVertex : ScopedItem.vertices)
    {
        ScopedFinite = ScopedFinite && std::isfinite(ScopedVertex.position.X) &&
                       std::isfinite(ScopedVertex.position.Y);
        ScopedMinX = std::min(ScopedMinX, ScopedVertex.position.X);
        ScopedMaxX = std::max(ScopedMaxX, ScopedVertex.position.X);
        ScopedMinY = std::min(ScopedMinY, ScopedVertex.position.Y);
        ScopedMaxY = std::max(ScopedMaxY, ScopedVertex.position.Y);
    }
    FBY_CHECK(ScopedFinite);
    FBY_CHECK(ScopedMaxX - ScopedMinX > 1.0f);
    FBY_CHECK(ScopedMaxY - ScopedMinY > 1.0f);

    // A missing cloth system is a clean no-op, not a crash.
    ClothRenderSystem ScopedDetached;
    ScopedDetached.Initialize(nullptr);
    Ecs::SystemContext ScopedNoCloth{ScopedRegistry, ScopedJobs};
    ScopedDetached.Collect(ScopedNoCloth);
    FBY_CHECK(ScopedDetached.DrawCount() == 0);

    ScopedCloth.Shutdown(ScopedRegistry);

    if (GFailures == 0)
    {
        std::printf("cloth_render_test: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "cloth_render_test: %u failure(s)\n", GFailures);
    return 1;
}
