// FemboyEngine2D - editor preview and tuning for the particle effect presets.
#pragma once

#include "Engine/Effects/ParticleEffects.hpp"

#include <array>
#include <cstdint>
#include <memory>

namespace Fby::Editor
{

// Owns one live ParticleEffectSystem. The panel is a tuning surface: it is not
// an authoring asset, so it never writes project files; a developer copies the
// values into gameplay code after finding the look.
class ParticleFxPanel
{
public:
    ParticleFxPanel();
    ~ParticleFxPanel();

    ParticleFxPanel(const ParticleFxPanel&) = delete;
    ParticleFxPanel& operator=(const ParticleFxPanel&) = delete;

    void Draw(bool* InOpen);

private:
    void LoadPreset(std::uint32_t InIndex);
    void Restart();
    void DrawPreview();
    void ApplyDirection();

    std::unique_ptr<ParticleEffectSystem> MSystem;
    ParticleEffectSettings MSettings{};
    ParticleShapeAtlas MAtlas = ParticleShapeAtlas::Defaults();
    std::array<Rhi::RenderItem, 1024> MItems{};
    std::uint32_t MPresetIndex = 0;
    float MDirectionDegrees = 90.0f;
    float MSpreadDegrees = 20.0f;
    bool MPlaying = true;
    bool MDirty = false;
};
}
