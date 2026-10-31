// FemboyEngine2D - PIE client viewport: target lifetime and client frame composition.
#include "Editor/Infrastructure/FrameComposer.hpp"
#include "Editor/Infrastructure/PlayInEditor.hpp"
#include "Editor/Panels/PieClientViewport.hpp"

#include "Engine/Core/JobSystem.hpp"
#include "Engine/ECS/Components.hpp"
#include "Engine/ECS/SystemGraph.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>
#include <vector>

namespace
{
std::uint32_t GFailures = 0;

#define FBY_CHECK(condition)                                                                                           \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);                                  \
            ++GFailures;                                                                                               \
        }                                                                                                              \
    } while (false)

// Records the offscreen calls the panel and the host issue. There is no GPU in
// the headless test; the target id and the last packet prove the contract.
class RecordingRhi final : public Fby::Rhi::RHI
{
public:
    bool Initialize(const Fby::Rhi::DeviceDesc&) override
    {
        return true;
    }
    void Shutdown() override
    {
    }
    void Resize(Fby::Rhi::Extent2D) override
    {
    }
    void WaitIdle() override
    {
        ++waits;
    }
    bool BeginFrame() override
    {
        return true;
    }
    void Submit(const Fby::Rhi::FramePacket&) override
    {
    }
    void EndFrame() override
    {
    }
    Fby::Rhi::TextureArrayHandle CreateTextureArray(const Fby::Rhi::TextureArrayDesc&,
                                                    std::span<const std::byte>) override
    {
        return {};
    }
    void DestroyTextureArray(Fby::Rhi::TextureArrayHandle) override
    {
    }
    Fby::Rhi::RenderTargetHandle CreateRenderTarget(const Fby::Rhi::RenderTargetDesc& InDesc) override
    {
        ++created;
        lastSize = InDesc.size;
        createdId = Fby::Rhi::RenderTargetHandle{100u + created};
        return createdId;
    }
    void DestroyRenderTarget(Fby::Rhi::RenderTargetHandle InHandle) override
    {
        ++destroyed;
        destroyedId = InHandle;
    }
    bool RenderToTarget(Fby::Rhi::RenderTargetHandle InHandle, const Fby::Rhi::FramePacket& InPacket) override
    {
        (void)InHandle;
        ++renders;
        lastItems = InPacket.items.size();
        lastBatches = InPacket.batches.size();
        lastCamera = InPacket.camera;
        return true;
    }
    [[nodiscard]] const Fby::Rhi::DeviceInfo& Info() const noexcept override
    {
        return MInfo;
    }
    [[nodiscard]] Fby::Rhi::FrameStats Stats() const noexcept override
    {
        return MStats;
    }
    [[nodiscard]] Fby::Rhi::Backend GetBackend() const noexcept override
    {
        return Fby::Rhi::Backend::Null;
    }
    [[nodiscard]] std::size_t TransientCapacity() const noexcept override
    {
        return 0;
    }

    std::uint32_t created = 0;
    std::uint32_t destroyed = 0;
    std::uint32_t renders = 0;
    std::uint32_t waits = 0;
    std::size_t lastItems = 0;
    std::size_t lastBatches = 0;
    Fby::Rhi::RenderTargetHandle createdId{};
    Fby::Rhi::RenderTargetHandle destroyedId{};
    Fby::Rhi::Extent2D lastSize{};
    Fby::Rhi::CameraData lastCamera{};

private:
    Fby::Rhi::DeviceInfo MInfo{};
    Fby::Rhi::FrameStats MStats{};
};
}

int main()
{
    using namespace Fby;

    JobSystem ScopedJobs(1);
    Ecs::SnapshotSchema ScopedSchema = Ecs::SnapshotSchema::BuiltIns();

    std::vector<std::byte> ScopedArena(1u << 20);
    Editor::PlayInEditor ScopedPie;
    FBY_CHECK(ScopedPie.Initialize(ScopedArena, ScopedSchema, ScopedArena.size()));

    Ecs::Registry ScopedEditorWorld;
    for (std::uint32_t ScopedI = 0; ScopedI < 3; ++ScopedI)
    {
        const Entity ScopedEntity = ScopedEditorWorld.create();
        ScopedEditorWorld.emplace<Transform2D>(
            ScopedEntity, Transform2D{Vector2D{100.0f + static_cast<float>(ScopedI) * 64.0f, 100.0f},
                                      Vector2D{1.0f, 1.0f}, 0.0f});
        ScopedEditorWorld.emplace<Sprite2D>(ScopedEntity, Sprite2D{});
        ScopedEditorWorld.emplace<DrawOrder>(ScopedEntity, DrawOrder{0, 0.0f});
    }

    Ecs::SystemContext ScopedServices{ScopedEditorWorld, ScopedJobs};
    FBY_CHECK(ScopedPie.StartMultiplayer(ScopedEditorWorld, ScopedSchema, ScopedServices, 1, 1, nullptr) ==
              Editor::PlayInEditor::Result::Ok);
    FBY_CHECK(ScopedPie.Multiplayer());

    RecordingRhi ScopedRhi;
    Editor::PieClientViewport ScopedViewport;
    FBY_CHECK(!ScopedViewport.Active());

    // The second target is created on demand and matches the requested extent.
    FBY_CHECK(ScopedViewport.EnsureTarget(ScopedRhi, Rhi::Extent2D{640, 360}));
    FBY_CHECK(ScopedViewport.Active());
    FBY_CHECK(ScopedViewport.Target().IsValid());
    FBY_CHECK(ScopedRhi.created == 1);
    FBY_CHECK(ScopedViewport.Size() == (Rhi::Extent2D{640, 360}));
    FBY_CHECK(ScopedRhi.lastSize == (Rhi::Extent2D{640, 360}));

    // Re-requesting the same extent keeps the target instead of recreating it.
    FBY_CHECK(ScopedViewport.EnsureTarget(ScopedRhi, Rhi::Extent2D{640, 360}));
    FBY_CHECK(ScopedRhi.created == 1);

    // A multiplayer session keeps the target alive across a sync.
    ScopedViewport.SyncSession(ScopedRhi, true);
    FBY_CHECK(ScopedViewport.Active());
    FBY_CHECK(ScopedRhi.destroyed == 0);

    // Compose the replicated client world and record it into that target.
    Editor::FrameComposer ScopedComposer;
    const Rhi::CameraData ScopedCamera{Vector2D{320.0f, 180.0f}, Vector2D{640.0f, 360.0f}, 1.0f};
    ScopedComposer.BuildFramePacket(ScopedPie.ClientWorld(), ScopedCamera, Color32::FromBytes(12, 14, 22), 0u);
    FBY_CHECK(ScopedComposer.Packet().items.size() == 3);
    FBY_CHECK(!ScopedComposer.Packet().batches.empty());
    FBY_CHECK(!ScopedComposer.Packet().postFxEnabled);

    Rhi::PostFxSettings ScopedPostFx;
    ScopedPostFx.flags = Rhi::PostFxCrt;
    ScopedPostFx.crtCurvature = 0.2f;
    ScopedComposer.BuildFramePacket(ScopedPie.ClientWorld(), ScopedCamera, Color32::FromBytes(12, 14, 22), 0u, {}, {},
                                    ScopedPostFx);
    FBY_CHECK(ScopedComposer.Packet().postFxEnabled);
    FBY_CHECK(ScopedComposer.Packet().postFx.crtCurvature == 0.2f);

    FBY_CHECK(ScopedRhi.BeginFrame());
    FBY_CHECK(ScopedRhi.RenderToTarget(ScopedViewport.Target(), ScopedComposer.Packet()));
    ScopedRhi.EndFrame();
    FBY_CHECK(ScopedRhi.renders == 1);
    FBY_CHECK(ScopedRhi.lastItems == 3);
    FBY_CHECK(ScopedRhi.lastBatches == ScopedComposer.Packet().batches.size());
    FBY_CHECK(ScopedRhi.lastCamera.viewport == (Vector2D{640.0f, 360.0f}));

    // The client camera is independent from the editor camera and survives the
    // session; the host starts it from the editor camera when PIE starts.
    ScopedViewport.Camera().center = Vector2D{320.0f, 180.0f};
    ScopedViewport.Camera().zoom = 2.0f;

    // Stopping PIE must free the second target.
    ScopedPie.Stop();
    FBY_CHECK(!ScopedPie.Multiplayer());
    ScopedViewport.SyncSession(ScopedRhi, ScopedPie.Multiplayer());
    FBY_CHECK(!ScopedViewport.Active());
    FBY_CHECK(!ScopedViewport.Target().IsValid());
    FBY_CHECK(ScopedRhi.destroyed == 1);
    FBY_CHECK(ScopedRhi.destroyedId.id == ScopedRhi.createdId.id);
    FBY_CHECK(ScopedRhi.waits > 0);

    if (GFailures == 0)
    {
        std::printf("pie_viewport_test: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "pie_viewport_test: %u failure(s)\n", GFailures);
    return 1;
}
