// FemboyEngine2D - SDF character authoring panel.
#include "Editor/Panels/SdfCharacterPanel.hpp"

#include "Engine/ECS/Components.hpp"
#include "Engine/Physics/SdfCharacter.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>

namespace Fby::Editor
{
namespace
{

[[nodiscard]] const char* ShapeLabel(std::uint32_t InType) noexcept
{
    const std::span<const SdfPrimitiveInfo> ScopedPalette = SdfPrimitivePalette();
    if (InType >= ScopedPalette.size() || ScopedPalette[InType].Label == nullptr)
    {
        return "Circle";
    }
    return ScopedPalette[InType].Label;
}

}

void SdfCharacterPanel::Draw(Ecs::SystemContext& InContext, bool* InOpen)
{
    if (!ImGui::Begin("SDF Character", InOpen))
    {
        ImGui::End();
        return;
    }
    auto ScopedSelection = InContext.registry.view<EditorSelected>();
    if (ScopedSelection.begin() == ScopedSelection.end())
    {
        ImGui::TextUnformatted("Select an entity to author an SDF character.");
        ImGui::End();
        return;
    }
    const Entity ScopedEntity = *ScopedSelection.begin();
    SdfCharacterComponent* ScopedCharacter = InContext.registry.try_get<SdfCharacterComponent>(ScopedEntity);
    if (ScopedCharacter == nullptr)
    {
        if (ImGui::Button("Add SDF Character"))
        {
            ScopedCharacter = &InContext.registry.emplace<SdfCharacterComponent>(ScopedEntity);
        }
        ImGui::End();
        return;
    }

    int ScopedPartCount = static_cast<int>(ScopedCharacter->PartCount);
    if (ImGui::SliderInt("Parts", &ScopedPartCount, 0, static_cast<int>(kMaxSdfCharacterParts)))
    {
        ScopedCharacter->PartCount = static_cast<std::uint32_t>(ScopedPartCount);
    }
    std::uint32_t ScopedFlags = ScopedCharacter->Flags;
    if (ImGui::InputScalar("Flags", ImGuiDataType_U32, &ScopedFlags))
    {
        ScopedCharacter->Flags = ScopedFlags;
    }

    const std::uint32_t ScopedCount = std::min<std::uint32_t>(ScopedCharacter->PartCount, kMaxSdfCharacterParts);
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedCount; ++ScopedIndex)
    {
        SdfBonePart& ScopedPart = ScopedCharacter->Parts[ScopedIndex];
        ImGui::PushID(static_cast<int>(ScopedIndex));
        ImGui::SeparatorText("Part");
        int ScopedBone = static_cast<int>(ScopedPart.BoneIndex);
        if (ImGui::InputInt("Bone", &ScopedBone))
        {
            ScopedPart.BoneIndex = static_cast<std::uint16_t>(std::max(ScopedBone, 0));
        }
        int ScopedType = static_cast<int>(ScopedPart.Shape.Type);
        if (ImGui::BeginCombo("Shape", ShapeLabel(ScopedPart.Shape.Type)))
        {
            for (std::uint32_t ScopedShape = 0; ScopedShape < kSdfShapeTypeCount; ++ScopedShape)
            {
                const bool ScopedSelected = ScopedType == static_cast<int>(ScopedShape);
                if (ImGui::Selectable(ShapeLabel(ScopedShape), ScopedSelected))
                {
                    ScopedPart.Shape.SetType(static_cast<SdfShapeType>(ScopedShape));
                }
            }
            ImGui::EndCombo();
        }
        ImGui::DragFloat2("Half extents", &ScopedPart.Shape.HalfExtents.X, 0.1f);
        ImGui::DragFloat("Radius", &ScopedPart.Shape.Radius, 0.1f);
        ImGui::DragFloat("Angle", &ScopedPart.Shape.Angle, 0.01f);
        ImGui::DragFloat("Width", &ScopedPart.Shape.Width, 0.1f);
        ImGui::DragFloat2("Local offset", &ScopedPart.LocalOffset.X, 0.1f);
        ImGui::DragFloat("Local rotation", &ScopedPart.LocalRotation, 0.01f);
        ImGui::DragFloat("Blend radius", &ScopedPart.BlendRadius, 0.1f);
        ImGui::PopID();
    }

    const SkeletonComponent* ScopedSkeleton = InContext.registry.try_get<SkeletonComponent>(ScopedEntity);
    Vector2D ScopedMin;
    Vector2D ScopedMax;
    if (ScopedSkeleton != nullptr &&
        SdfCharacterBounds(*ScopedSkeleton, *ScopedCharacter, &ScopedMin, &ScopedMax))
    {
        constexpr std::uint32_t ScopedPreviewSize = 48;
        SdfCharacterField ScopedPreview;
        if (BakeSdfCharacterField(*ScopedSkeleton, *ScopedCharacter, ScopedMin, ScopedMax, ScopedPreviewSize,
                                  ScopedPreviewSize, &ScopedPreview))
        {
            ImGui::SeparatorText("Preview");
            ImDrawList* ScopedDraw = ImGui::GetWindowDrawList();
            const ImVec2 ScopedOrigin = ImGui::GetCursorScreenPos();
            const float ScopedCell = 4.0f;
            for (std::uint32_t ScopedY = 0; ScopedY < ScopedPreviewSize; ++ScopedY)
            {
                for (std::uint32_t ScopedX = 0; ScopedX < ScopedPreviewSize; ++ScopedX)
                {
                    const float ScopedDistance = ScopedPreview.At(ScopedX, ScopedY);
                    const ImU32 ScopedColor = ScopedDistance < 0.0f ? IM_COL32(90, 180, 255, 220)
                                                                    : IM_COL32(40, 40, 48, 160);
                    const ImVec2 ScopedMinPixel{ScopedOrigin.x + ScopedCell * static_cast<float>(ScopedX),
                                                ScopedOrigin.y + ScopedCell * static_cast<float>(ScopedY)};
                    const ImVec2 ScopedMaxPixel{ScopedMinPixel.x + ScopedCell, ScopedMinPixel.y + ScopedCell};
                    ScopedDraw->AddRectFilled(ScopedMinPixel, ScopedMaxPixel, ScopedColor);
                }
            }
            ImGui::Dummy(ImVec2(ScopedCell * static_cast<float>(ScopedPreviewSize),
                                ScopedCell * static_cast<float>(ScopedPreviewSize)));
            ImGui::Text("CPU bake 48x48, world bounds %.2f..%.2f / %.2f..%.2f", ScopedMin.X, ScopedMax.X,
                        ScopedMin.Y, ScopedMax.Y);
        }
    }
    else
    {
        ImGui::TextUnformatted("Add a SkeletonComponent to preview the posed field.");
    }
    ImGui::End();
}

}
