// FemboyEngine2D - particle effect preview panel implementation.
#include "Editor/Panels/ParticleFxPanel.hpp"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iterator>

namespace Fby::Editor
{
namespace
{
constexpr float kParticlePanelPi = 3.14159265358979323846f;
constexpr std::uint32_t kParticlePanelMaxItems = 1024;

[[nodiscard]] const char* PresetShortName(const ParticleEffectPreset& InPreset) noexcept
{
    const char* ScopedDot = std::strrchr(InPreset.Id, '.');
    return ScopedDot != nullptr ? ScopedDot + 1 : InPreset.Id;
}

[[nodiscard]] ImU32 ToImGuiColor(std::uint32_t InRgba) noexcept
{
    return IM_COL32(static_cast<int>(InRgba & 0xFFu), static_cast<int>((InRgba >> 8u) & 0xFFu),
                    static_cast<int>((InRgba >> 16u) & 0xFFu), static_cast<int>((InRgba >> 24u) & 0xFFu));
}

[[nodiscard]] ImVec2 ToScreen(ImVec2 InCenter, Vector2D InPosition, float InScale) noexcept
{
    return ImVec2{InCenter.x + InPosition.X * InScale, InCenter.y + InPosition.Y * InScale};
}

void DrawParticleShape(ImDrawList* InDraw, ImVec2 InCenter, float InRadius, ImU32 InColor,
                       ParticleShape InShape) noexcept
{
    const float ScopedRadius = std::clamp(InRadius, 0.75f, 24.0f);
    switch (InShape)
    {
        case ParticleShape::Square:
        {
            InDraw->AddRectFilled(ImVec2{InCenter.x - ScopedRadius, InCenter.y - ScopedRadius},
                                  ImVec2{InCenter.x + ScopedRadius, InCenter.y + ScopedRadius}, InColor);
            break;
        }
        case ParticleShape::Ring:
        {
            InDraw->AddCircle(InCenter, ScopedRadius, InColor, 16, std::max(1.0f, ScopedRadius * 0.22f));
            break;
        }
        case ParticleShape::Diamond:
        {
            InDraw->AddQuadFilled(
                ImVec2{InCenter.x, InCenter.y - ScopedRadius}, ImVec2{InCenter.x + ScopedRadius, InCenter.y},
                ImVec2{InCenter.x, InCenter.y + ScopedRadius}, ImVec2{InCenter.x - ScopedRadius, InCenter.y}, InColor);
            break;
        }
        case ParticleShape::Spark:
        {
            InDraw->AddLine(ImVec2{InCenter.x, InCenter.y - ScopedRadius * 2.0f},
                            ImVec2{InCenter.x, InCenter.y + ScopedRadius * 2.0f}, InColor,
                            std::max(1.0f, ScopedRadius * 0.5f));
            break;
        }
        case ParticleShape::Snowflake:
        {
            const float ScopedThickness = std::max(1.0f, ScopedRadius * 0.18f);
            InDraw->AddLine(ImVec2{InCenter.x - ScopedRadius, InCenter.y},
                            ImVec2{InCenter.x + ScopedRadius, InCenter.y}, InColor, ScopedThickness);
            InDraw->AddLine(ImVec2{InCenter.x - ScopedRadius * 0.5f, InCenter.y - ScopedRadius * 0.86f},
                            ImVec2{InCenter.x + ScopedRadius * 0.5f, InCenter.y + ScopedRadius * 0.86f}, InColor,
                            ScopedThickness);
            InDraw->AddLine(ImVec2{InCenter.x + ScopedRadius * 0.5f, InCenter.y - ScopedRadius * 0.86f},
                            ImVec2{InCenter.x - ScopedRadius * 0.5f, InCenter.y + ScopedRadius * 0.86f}, InColor,
                            ScopedThickness);
            break;
        }
        case ParticleShape::Heart:
        {
            InDraw->AddCircleFilled(ImVec2{InCenter.x - ScopedRadius * 0.45f, InCenter.y - ScopedRadius * 0.25f},
                                    ScopedRadius * 0.55f, InColor, 12);
            InDraw->AddCircleFilled(ImVec2{InCenter.x + ScopedRadius * 0.45f, InCenter.y - ScopedRadius * 0.25f},
                                    ScopedRadius * 0.55f, InColor, 12);
            InDraw->AddTriangleFilled(ImVec2{InCenter.x - ScopedRadius * 0.95f, InCenter.y},
                                      ImVec2{InCenter.x + ScopedRadius * 0.95f, InCenter.y},
                                      ImVec2{InCenter.x, InCenter.y + ScopedRadius}, InColor);
            break;
        }
        case ParticleShape::Star:
        {
            InDraw->AddNgonFilled(InCenter, ScopedRadius, InColor, 5);
            break;
        }
        case ParticleShape::Circle:
        case ParticleShape::Glow:
        case ParticleShape::Smoke:
        case ParticleShape::Bubble:
        case ParticleShape::Leaf:
        case ParticleShape::Count:
        {
            InDraw->AddCircleFilled(InCenter, ScopedRadius, InColor, 14);
            break;
        }
    }
}

void DragColor(const char* InLabel, Color32& InColor, bool& OutChanged)
{
    float ScopedValues[4] = {static_cast<float>(InColor.R()) / 255.0f, static_cast<float>(InColor.G()) / 255.0f,
                             static_cast<float>(InColor.B()) / 255.0f, static_cast<float>(InColor.A()) / 255.0f};
    if (ImGui::ColorEdit4(InLabel, ScopedValues, ImGuiColorEditFlags_AlphaBar))
    {
        InColor = Color32::FromBytes(static_cast<std::uint8_t>(ScopedValues[0] * 255.0f + 0.5f),
                                     static_cast<std::uint8_t>(ScopedValues[1] * 255.0f + 0.5f),
                                     static_cast<std::uint8_t>(ScopedValues[2] * 255.0f + 0.5f),
                                     static_cast<std::uint8_t>(ScopedValues[3] * 255.0f + 0.5f));
        OutChanged = true;
    }
}
}

ParticleFxPanel::ParticleFxPanel() : MSystem(std::make_unique<ParticleEffectSystem>())
{
    LoadPreset(0);
}

ParticleFxPanel::~ParticleFxPanel() = default;

void ParticleFxPanel::LoadPreset(std::uint32_t InIndex)
{
    const std::span<const ParticleEffectPreset> ScopedPresets = ParticleEffectPresets();
    if (ScopedPresets.empty())
    {
        return;
    }
    MPresetIndex = std::min(InIndex, static_cast<std::uint32_t>(ScopedPresets.size() - 1));
    MSettings = ScopedPresets[MPresetIndex].Settings;
    MDirectionDegrees = std::atan2(-MSettings.Direction.Y, MSettings.Direction.X) * 180.0f / kParticlePanelPi;
    MSpreadDegrees = MSettings.SpreadRadians * 180.0f / kParticlePanelPi;
    Restart();
}

void ParticleFxPanel::ApplyDirection()
{
    const float ScopedRadians = MDirectionDegrees * kParticlePanelPi / 180.0f;
    MSettings.Direction = Vector2D{std::cos(ScopedRadians), -std::sin(ScopedRadians)};
    MSettings.SpreadRadians = MSpreadDegrees * kParticlePanelPi / 180.0f;
}

void ParticleFxPanel::Restart()
{
    MSystem->Clear();
    if (MPlaying)
    {
        (void)MSystem->Spawn(MSettings, Vector2D{0.0f, 0.0f});
    }
}

void ParticleFxPanel::Draw(bool* InOpen)
{
    if (!ImGui::Begin("Particle FX", InOpen))
    {
        ImGui::End();
        return;
    }

    const std::span<const ParticleEffectPreset> ScopedPresets = ParticleEffectPresets();
    if (ImGui::BeginCombo("Preset", PresetShortName(ScopedPresets[MPresetIndex])))
    {
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedPresets.size(); ++ScopedIndex)
        {
            const bool ScopedSelected = ScopedIndex == MPresetIndex;
            if (ImGui::Selectable(PresetShortName(ScopedPresets[ScopedIndex]), ScopedSelected))
            {
                LoadPreset(ScopedIndex);
            }
            if (ScopedSelected)
            {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    if (ImGui::Button("Restart"))
    {
        Restart();
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset"))
    {
        LoadPreset(MPresetIndex);
    }
    ImGui::SameLine();
    if (ImGui::Checkbox("Playing", &MPlaying))
    {
        if (MPlaying)
        {
            Restart();
        }
        else
        {
            MSystem->Clear();
        }
    }

    bool ScopedChanged = false;
    if (ImGui::SliderFloat("Direction", &MDirectionDegrees, -180.0f, 180.0f, "%.0f deg"))
    {
        ScopedChanged = true;
    }
    if (ImGui::SliderFloat("Spread", &MSpreadDegrees, 0.0f, 180.0f, "%.0f deg"))
    {
        ScopedChanged = true;
    }
    if (ImGui::DragFloat("Rate", &MSettings.EmissionRate, 1.0f, 0.0f, 1000.0f, "%.0f/s"))
    {
        ScopedChanged = true;
    }
    int ScopedMaxParticles = static_cast<int>(MSettings.MaxParticles);
    if (ImGui::DragInt("Max particles", &ScopedMaxParticles, 1.0f, 1, 8192))
    {
        MSettings.MaxParticles = static_cast<std::uint32_t>(std::clamp(ScopedMaxParticles, 1, 8192));
        ScopedChanged = true;
    }
    int ScopedBurstCount = static_cast<int>(MSettings.BurstCount);
    if (ImGui::DragInt("Burst count", &ScopedBurstCount, 1.0f, 0, 512))
    {
        MSettings.BurstCount = static_cast<std::uint32_t>(std::clamp(ScopedBurstCount, 0, 512));
        ScopedChanged = true;
    }
    if (ImGui::DragFloat("Burst interval", &MSettings.BurstInterval, 0.01f, 0.0f, 5.0f, "%.2f s"))
    {
        ScopedChanged = true;
    }
    if (ImGui::DragFloat("Duration", &MSettings.Duration, 0.05f, 0.0f, 30.0f, "%.2f s"))
    {
        ScopedChanged = true;
    }
    if (ImGui::Checkbox("Loop", &MSettings.Loop))
    {
        ScopedChanged = true;
    }
    if (ImGui::Checkbox("Additive", &MSettings.Additive))
    {
        ScopedChanged = true;
    }

    ImGui::SeparatorText("Emission");
    static const char* ScopedShapes[] = {"Square", "Circle", "Ring",   "Glow", "Star",      "Diamond",
                                         "Spark",  "Smoke",  "Bubble", "Leaf", "Snowflake", "Heart"};
    int ScopedShape = static_cast<int>(MSettings.Shape);
    if (ImGui::Combo("Shape", &ScopedShape, ScopedShapes, static_cast<int>(std::size(ScopedShapes))))
    {
        MSettings.Shape = static_cast<ParticleShape>(ScopedShape);
        ScopedChanged = true;
    }
    static const char* ScopedEmissionShapes[] = {"Point", "Circle", "Box", "Line"};
    int ScopedEmissionShape = static_cast<int>(MSettings.EmissionShape);
    if (ImGui::Combo("Emitter shape", &ScopedEmissionShape, ScopedEmissionShapes,
                     static_cast<int>(std::size(ScopedEmissionShapes))))
    {
        MSettings.EmissionShape = static_cast<ParticleEmissionShape>(ScopedEmissionShape);
        ScopedChanged = true;
    }
    if (ImGui::DragFloat2("Emitter extents", &MSettings.EmissionExtents.X, 0.5f, 0.0f, 512.0f, "%.1f"))
    {
        ScopedChanged = true;
    }

    ImGui::SeparatorText("Motion");
    if (ImGui::DragFloat2("Gravity", &MSettings.Gravity.X, 1.0f, -2000.0f, 2000.0f, "%.0f"))
    {
        ScopedChanged = true;
    }
    if (ImGui::DragFloat2("Wind", &MSettings.Wind.X, 1.0f, -2000.0f, 2000.0f, "%.0f"))
    {
        ScopedChanged = true;
    }
    if (ImGui::DragFloat("Drag", &MSettings.Drag, 0.01f, 0.0f, 10.0f, "%.2f"))
    {
        ScopedChanged = true;
    }
    if (ImGui::DragFloat("Turbulence", &MSettings.Turbulence, 0.5f, 0.0f, 500.0f, "%.0f"))
    {
        ScopedChanged = true;
    }
    if (ImGui::DragFloat("Swirl", &MSettings.Swirl, 0.5f, -500.0f, 500.0f, "%.0f"))
    {
        ScopedChanged = true;
    }
    if (ImGui::DragFloat("Speed min", &MSettings.SpeedMin, 1.0f, 0.0f, 2000.0f, "%.0f"))
    {
        ScopedChanged = true;
    }
    if (ImGui::DragFloat("Speed max", &MSettings.SpeedMax, 1.0f, 0.0f, 2000.0f, "%.0f"))
    {
        ScopedChanged = true;
    }
    if (ImGui::DragFloat("Lifetime min", &MSettings.LifetimeMin, 0.01f, 0.01f, 30.0f, "%.2f s"))
    {
        ScopedChanged = true;
    }
    if (ImGui::DragFloat("Lifetime max", &MSettings.LifetimeMax, 0.01f, 0.01f, 30.0f, "%.2f s"))
    {
        ScopedChanged = true;
    }
    if (ImGui::DragFloat("Start size", &MSettings.StartSize, 0.1f, 0.0f, 256.0f, "%.1f"))
    {
        ScopedChanged = true;
    }
    if (ImGui::DragFloat("End size", &MSettings.EndSize, 0.1f, 0.0f, 256.0f, "%.1f"))
    {
        ScopedChanged = true;
    }
    if (ImGui::DragFloat("Start rotation", &MSettings.StartRotation, 0.05f, -12.0f, 12.0f, "%.2f"))
    {
        ScopedChanged = true;
    }
    if (ImGui::DragFloat("Rotation speed", &MSettings.RotationSpeed, 0.05f, -20.0f, 20.0f, "%.2f"))
    {
        ScopedChanged = true;
    }

    ImGui::SeparatorText("Colour");
    DragColor("Start colour", MSettings.StartColor, ScopedChanged);
    DragColor("End colour", MSettings.EndColor, ScopedChanged);

    ApplyDirection();
    MSettings.Clamp();
    if (ScopedChanged)
    {
        Restart();
    }

    if (MPlaying)
    {
        MSystem->Update(std::clamp(ImGui::GetIO().DeltaTime, 0.0f, 0.05f));
    }
    ImGui::Text("Particles: %u  Emitters: %u", MSystem->ActiveParticles(), MSystem->ActiveEmitters());
    DrawPreview();
    ImGui::End();
}

void ParticleFxPanel::DrawPreview()
{
    const ImVec2 ScopedAvailable = ImGui::GetContentRegionAvail();
    const float ScopedHeight = std::max(180.0f, ScopedAvailable.y);
    ImGui::BeginChild("##particle_fx_preview", ImVec2(0.0f, ScopedHeight), ImGuiChildFlags_Borders);
    const ImVec2 ScopedOrigin = ImGui::GetCursorScreenPos();
    const ImVec2 ScopedSize = ImGui::GetContentRegionAvail();
    ImDrawList* ScopedDraw = ImGui::GetWindowDrawList();
    const ImVec2 ScopedEnd{ScopedOrigin.x + ScopedSize.x, ScopedOrigin.y + ScopedSize.y};
    ScopedDraw->AddRectFilled(ScopedOrigin, ScopedEnd, IM_COL32(18, 18, 22, 255));
    const ImVec2 ScopedCenter{ScopedOrigin.x + ScopedSize.x * 0.5f, ScopedOrigin.y + ScopedSize.y * 0.5f};
    ScopedDraw->AddLine(ImVec2{ScopedOrigin.x, ScopedCenter.y}, ImVec2{ScopedEnd.x, ScopedCenter.y},
                        IM_COL32(40, 40, 48, 255));
    ScopedDraw->AddLine(ImVec2{ScopedCenter.x, ScopedOrigin.y}, ImVec2{ScopedCenter.x, ScopedEnd.y},
                        IM_COL32(40, 40, 48, 255));

    const float ScopedScale = std::min(ScopedSize.x, ScopedSize.y) / 480.0f;
    const std::uint32_t ScopedCount = MSystem->BuildRenderItems(
        std::span<Rhi::RenderItem>(MItems.data(), std::min<std::size_t>(MItems.size(), kParticlePanelMaxItems)),
        MAtlas);
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedCount; ++ScopedIndex)
    {
        const Rhi::RenderItem& ScopedItem = MItems[ScopedIndex];
        const ImVec2 ScopedPosition = ToScreen(ScopedCenter, ScopedItem.position, ScopedScale);
        DrawParticleShape(ScopedDraw, ScopedPosition, ScopedItem.size.X * ScopedScale * 0.5f,
                          ToImGuiColor(ScopedItem.color), MSettings.Shape);
    }
    ImGui::Dummy(ScopedSize);
    ImGui::EndChild();
}
}
