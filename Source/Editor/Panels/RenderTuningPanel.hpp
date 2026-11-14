// FemboyEngine2D - live controls for the render-scale and static-tile caches.
#pragma once

#include "Engine/RHI/DynamicResolution.hpp"
#include "Engine/RHI/StaticBakeRenderer.hpp"
#include "Engine/RHI/StaticBake.hpp"

#include <cstdint>

namespace Fby::Ecs
{
struct SystemContext;
}

namespace Fby::Editor
{

class RenderTuningPanel
{
public:
    RenderTuningPanel() = default;
    ~RenderTuningPanel();

    void Draw(Ecs::SystemContext& InContext, bool* InOpen);

private:
    Fby::Rhi::DynamicResolution MResolution;
    Fby::Rhi::StaticBakeCache MBake;
    bool MConfigured = false;
    float MZoom = 1.0f;
    std::uint32_t MLightVersion = 1;
    Fby::Rhi::StaticBakeRenderer MBakeRenderer;
    Fby::Rhi::RHI* MGpu = nullptr;
    std::array<Fby::Rhi::RenderItem, 1> MBakeItems{};
    std::array<Fby::Rhi::RenderKey, 1> MBakeKeys{};
    std::array<Fby::Rhi::DrawBatch, 1> MBakeBatches{};
    std::string MBakeStatus;
};

}
