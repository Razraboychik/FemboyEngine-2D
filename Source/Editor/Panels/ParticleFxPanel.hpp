// FemboyEngine2D - editor preview and tuning for the particle effect presets.
#pragma once

#include "Engine/Effects/ParticleEffects.hpp"
#include "Engine/Effects/ParticlePreset.hpp"

#include <array>
#include <cstdint>
#include <memory>

namespace Fby::Editor
{

// Owns one live ParticleEffectSystem. The panel tunes the effect and can save
// the current settings as a `.fbyparticle` preset pack; the gameplay code reads
// the same id through the preset registry.
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
    ParticlePresetRegistry MPresetRegistry;
    std::array<char, 512> MPresetPath{};
    std::array<char, 64> MSaveId{};
    std::array<char, 64> MSaveName{};
    std::string MStatus;
    int MExternalPreset = -1;
};
}
