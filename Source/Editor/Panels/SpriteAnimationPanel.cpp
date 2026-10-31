#include "Editor/Panels/SpriteAnimationPanel.hpp"

#include "Engine/Assets/AssetDatabase.hpp"

#include "Editor/Infrastructure/EditorTheme.hpp"
#include "Editor/Infrastructure/TextureThumbnails.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string_view>

namespace Fby::Editor
{
namespace
{
[[nodiscard]] bool HasEventFrame(const SpriteAnimationInspectorState& InState, std::uint16_t InFrame) noexcept
{
    return std::find(InState.eventFrames.begin(), InState.eventFrames.begin() + InState.eventCount, InFrame) !=
           InState.eventFrames.begin() + InState.eventCount;
}

void ToggleEventFrame(SpriteAnimationInspectorState& InState, std::uint16_t InFrame) noexcept
{
    const auto ScopedEnd = InState.eventFrames.begin() + InState.eventCount;
    const auto ScopedFound = std::find(InState.eventFrames.begin(), ScopedEnd, InFrame);
    if (ScopedFound != ScopedEnd)
    {
        *ScopedFound = InState.eventFrames[InState.eventCount - 1];
        --InState.eventCount;
        return;
    }
    if (InState.eventCount < InState.eventFrames.size())
    {
        InState.eventFrames[InState.eventCount++] = InFrame;
    }
}

void DrawSheetPreview(const SpriteAnimationComponent& InSpriteAnimation, const SpriteSheetComponent* InGrid,
                      const SpriteAnimationInspectorState& InState)
{
    ImGui::BeginChild("##sprite_preview", ImVec2(0.0f, 130.0f), ImGuiChildFlags_Borders);
    ImDrawList* ScopedDraw = ImGui::GetWindowDrawList();
    const ImVec2 ScopedOrigin = ImGui::GetCursorScreenPos();
    const float ScopedWidth = std::max(ImGui::GetContentRegionAvail().x, 32.0f);
    const std::uint32_t ScopedColumns = InGrid != nullptr ? std::max(InGrid->Columns, 1u) : std::max(InSpriteAnimation.FrameCount, 1u);
    const std::uint32_t ScopedRows = InGrid != nullptr ? std::max(InGrid->Rows, 1u) : 1u;
    const float ScopedCell = std::max(std::min(ScopedWidth / static_cast<float>(ScopedColumns), 42.0f), 10.0f);
    const std::uint32_t ScopedFrames = std::max(InSpriteAnimation.FrameCount, 1u);

    // Real asset preview: the sheet is one texture-array layer, drawn once and
    // then annotated with the frame grid.
    ImTextureID ScopedTexture = ImTextureID_Invalid;
    if (InGrid != nullptr && InState.thumbnails != nullptr && InState.textureArray.IsValid()
        && ScopedColumns != 0 && ScopedRows != 0)
    {
        ScopedTexture = InState.thumbnails->Get(InState.textureArray, InGrid->Layer);
    }
    if (ScopedTexture != ImTextureID_Invalid)
    {
        const ImVec2 ScopedSheetMax{ScopedOrigin.x + ScopedCell * static_cast<float>(ScopedColumns),
                                    ScopedOrigin.y + ScopedCell * static_cast<float>(ScopedRows)};
        ScopedDraw->AddImage(ScopedTexture, ScopedOrigin, ScopedSheetMax,
                             ImVec2(InGrid->UVMin.X, InGrid->UVMin.Y), ImVec2(InGrid->UVMax.X, InGrid->UVMax.Y));
    }

    for (std::uint32_t ScopedFrame = 0; ScopedFrame < ScopedFrames; ++ScopedFrame)
    {
        const std::uint32_t ScopedColumn = ScopedFrame % ScopedColumns;
        const std::uint32_t ScopedRow = ScopedFrame / ScopedColumns;
        const ImVec2 ScopedMin{ScopedOrigin.x + static_cast<float>(ScopedColumn) * ScopedCell,
                               ScopedOrigin.y + static_cast<float>(ScopedRow) * ScopedCell};
        const ImVec2 ScopedMax{ScopedMin.x + ScopedCell - 1.0f, ScopedMin.y + ScopedCell - 1.0f};
        const bool ScopedCurrent = ScopedFrame == InSpriteAnimation.CurrentFrame;
        const bool ScopedOnion = InState.onionSkin && !ScopedCurrent &&
                                 (ScopedFrame + static_cast<std::uint32_t>(InState.onionFrames) >=
                                      InSpriteAnimation.CurrentFrame &&
                                  ScopedFrame <= InSpriteAnimation.CurrentFrame + static_cast<std::uint32_t>(InState.onionFrames));
        const bool ScopedTextured = ScopedTexture != ImTextureID_Invalid;
        const ImU32 ScopedFill = ScopedCurrent ? IM_COL32(200, 60, 120, ScopedTextured ? 90u : 210u)
                                              : (ScopedOnion ? IM_COL32(110, 80, 130, 110) : IM_COL32(48, 50, 62, ScopedTextured ? 0u : 220u));
        if (ScopedFill != 0)
        {
            ScopedDraw->AddRectFilled(ScopedMin, ScopedMax, ScopedFill);
        }
        ScopedDraw->AddRect(ScopedMin, ScopedMax, ScopedCurrent ? IM_COL32(255, 220, 235, 255) : IM_COL32(95, 95, 110, 255));
        char ScopedLabel[16] = {};
        std::snprintf(ScopedLabel, sizeof(ScopedLabel), "%u", ScopedFrame);
        ScopedDraw->AddText(ImVec2(ScopedMin.x + 3.0f, ScopedMin.y + 2.0f), IM_COL32(235, 235, 245, 255), ScopedLabel);
        if (HasEventFrame(InState, static_cast<std::uint16_t>(ScopedFrame)))
        {
            const ImVec2 ScopedA{ScopedMax.x - 8.0f, ScopedMin.y + 2.0f};
            const ImVec2 ScopedB{ScopedMax.x - 2.0f, ScopedMin.y + 2.0f};
            const ImVec2 ScopedC{ScopedMax.x - 5.0f, ScopedMin.y + 8.0f};
            ScopedDraw->AddTriangleFilled(ScopedA, ScopedB, ScopedC, IM_COL32(255, 205, 90, 255));
        }
    }
    ImGui::Dummy(ImVec2(ScopedCell * static_cast<float>(ScopedColumns), ScopedCell * static_cast<float>(ScopedRows)));
    if (ScopedTexture == ImTextureID_Invalid)
    {
        ImGui::TextDisabled("no texture preview: bake the sheet into the texture array to see it here");
    }
    ImGui::EndChild();
}

bool SaveClip(const std::filesystem::path& InPath, const SpriteAnimationComponent& InSpriteAnimation,
              const SpriteSheetComponent* InGrid, const SpriteAnimationInspectorState& InState)
{
    std::error_code ScopedError;
    if (!InPath.parent_path().empty())
    {
        std::filesystem::create_directories(InPath.parent_path(), ScopedError);
    }
    std::ofstream ScopedFile(InPath, std::ios::binary);
    if (!ScopedFile)
    {
        return false;
    }
    ScopedFile << "[sprite_clip]\n";
    ScopedFile << "start_frame = " << InSpriteAnimation.StartFrame << "\n";
    ScopedFile << "frame_count = " << InSpriteAnimation.FrameCount << "\n";
    ScopedFile << "frame_duration = " << InSpriteAnimation.FrameDuration << "\n";
    ScopedFile << "looping = " << (InSpriteAnimation.IsLooping ? "true" : "false") << "\n";
    ScopedFile << "playing = " << (InSpriteAnimation.IsPlaying ? "true" : "false") << "\n";
    ScopedFile << "columns = " << (InGrid != nullptr ? InGrid->Columns : 0) << "\n";
    ScopedFile << "rows = " << (InGrid != nullptr ? InGrid->Rows : 1) << "\n";
    ScopedFile << "events = [";
    for (std::uint16_t ScopedIndex = 0; ScopedIndex < InState.eventCount; ++ScopedIndex)
    {
        ScopedFile << (ScopedIndex == 0 ? "" : ", ") << InState.eventFrames[ScopedIndex];
    }
    ScopedFile << "]\n";
    return static_cast<bool>(ScopedFile);
}

bool LoadClip(const std::filesystem::path& InPath, SpriteAnimationComponent& InSpriteAnimation,
              SpriteSheetComponent* InGrid, SpriteAnimationInspectorState& InState)
{
    std::ifstream ScopedFile(InPath, std::ios::binary);
    if (!ScopedFile)
    {
        return false;
    }
    InState.eventCount = 0;
    std::string ScopedLine;
    while (std::getline(ScopedFile, ScopedLine))
    {
        const std::size_t ScopedEquals = ScopedLine.find('=');
        if (ScopedEquals == std::string::npos)
        {
            continue;
        }
        const std::string_view ScopedKey(ScopedLine.data(), ScopedEquals);
        const char* ScopedValue = ScopedLine.c_str() + ScopedEquals + 1;
        if (ScopedKey.find("start_frame") != std::string_view::npos)
        {
            InSpriteAnimation.StartFrame = static_cast<std::uint32_t>(std::strtoul(ScopedValue, nullptr, 10));
        }
        else if (ScopedKey.find("frame_count") != std::string_view::npos)
        {
            InSpriteAnimation.FrameCount = static_cast<std::uint32_t>(std::strtoul(ScopedValue, nullptr, 10));
        }
        else if (ScopedKey.find("frame_duration") != std::string_view::npos)
        {
            InSpriteAnimation.FrameDuration = std::strtof(ScopedValue, nullptr);
        }
        else if (ScopedKey.find("looping") != std::string_view::npos)
        {
            InSpriteAnimation.IsLooping = std::string_view(ScopedValue).find("true") != std::string_view::npos;
        }
        else if (ScopedKey.find("playing") != std::string_view::npos)
        {
            InSpriteAnimation.IsPlaying = std::string_view(ScopedValue).find("true") != std::string_view::npos;
        }
        else if (ScopedKey.find("columns") != std::string_view::npos && InGrid != nullptr)
        {
            InGrid->Columns = static_cast<std::uint32_t>(std::strtoul(ScopedValue, nullptr, 10));
        }
        else if (ScopedKey.find("rows") != std::string_view::npos && InGrid != nullptr)
        {
            InGrid->Rows = static_cast<std::uint32_t>(std::strtoul(ScopedValue, nullptr, 10));
        }
        else if (ScopedKey.find("events") != std::string_view::npos)
        {
            const char* ScopedCursor = ScopedValue;
            while (*ScopedCursor != '\0' && InState.eventCount < InState.eventFrames.size())
            {
                while (*ScopedCursor == '[' || *ScopedCursor == ' ' || *ScopedCursor == ',' ||
                       *ScopedCursor == ']')
                {
                    ++ScopedCursor;
                }
                if (*ScopedCursor == '\0')
                {
                    break;
                }
                char* ScopedEnd = nullptr;
                const unsigned long ScopedEvent = std::strtoul(ScopedCursor, &ScopedEnd, 10);
                if (ScopedEnd == ScopedCursor)
                {
                    break;
                }
                InState.eventFrames[InState.eventCount++] = static_cast<std::uint16_t>(ScopedEvent);
                ScopedCursor = ScopedEnd;
            }
        }
    }
    InSpriteAnimation.CurrentFrame = std::min(InSpriteAnimation.CurrentFrame, std::max(InSpriteAnimation.FrameCount, 1u) - 1u);
    InSpriteAnimation.IsPlaying = false;
    return true;
}

[[nodiscard]] std::uint32_t ClampFrameCount(const SpriteAnimationComponent& InSpriteAnimation) noexcept
{
    return std::max(InSpriteAnimation.FrameCount, 1u);
}

void DrawSheetGrid(Ecs::Registry& InRegistry, Entity InEntity, SpriteAnimationComponent& InSpriteAnimation, Sprite2D* InSprite,
                   SpriteAnimationInspectorState& InState)
{
    SpriteSheetComponent* ScopedGrid = InRegistry.try_get<SpriteSheetComponent>(InEntity);
    if (ScopedGrid == nullptr)
    {
        if (ImGui::SmallButton("Add sheet grid"))
        {
            SpriteSheetComponent ScopedCreated;
            ScopedCreated.Columns = ClampFrameCount(InSpriteAnimation);
            ScopedCreated.Rows = 1;
            ScopedCreated.Layer = InSprite != nullptr ? InSprite->Layer : 0;
            ScopedGrid = &InRegistry.emplace<SpriteSheetComponent>(InEntity, ScopedCreated);
            if (InSprite != nullptr)
            {
                ApplySpriteFrame(InSpriteAnimation, ScopedGrid, *InSprite);
            }
        }
        ImGui::SameLine();
        ImGui::TextDisabled("horizontal strip assumed");
        return;
    }

    InState.showGrid = ImGui::TreeNodeEx("Sheet grid", ImGuiTreeNodeFlags_DefaultOpen);
    if (!InState.showGrid)
    {
        return;
    }

    int ScopedColumns = static_cast<int>(std::min(ScopedGrid->Columns, 65535u));
    int ScopedRows = static_cast<int>(std::min(ScopedGrid->Rows, 65535u));
    int ScopedLayer = static_cast<int>(std::min(ScopedGrid->Layer, 65535u));
    bool ScopedChanged = false;
    ScopedChanged |= ImGui::DragInt("columns", &ScopedColumns, 1.0f, 1, 256);
    ScopedChanged |= ImGui::DragInt("rows", &ScopedRows, 1.0f, 1, 256);
    ScopedChanged |= ImGui::DragInt("texture layer", &ScopedLayer, 1.0f, 0, 65535);
    ScopedChanged |= ImGui::DragFloat2("uv region min", &ScopedGrid->UVMin.X, 0.001f, 0.0f, 1.0f, "%.4f");
    ScopedChanged |= ImGui::DragFloat2("uv region max", &ScopedGrid->UVMax.X, 0.001f, 0.0f, 1.0f, "%.4f");
    if (ScopedChanged)
    {
        ScopedGrid->Columns = static_cast<std::uint32_t>(std::max(ScopedColumns, 1));
        ScopedGrid->Rows = static_cast<std::uint32_t>(std::max(ScopedRows, 1));
        ScopedGrid->Layer = static_cast<std::uint32_t>(std::max(ScopedLayer, 0));
        if (InSprite != nullptr)
        {
            ApplySpriteFrame(InSpriteAnimation, ScopedGrid, *InSprite);
        }
    }

    ImGui::TextDisabled("cells: %u x %u, first absolute cell %u", ScopedGrid->Columns, ScopedGrid->Rows,
                        InSpriteAnimation.StartFrame);
    ImGui::TreePop();
}
}

void DrawSpriteAnimationInspector(Ecs::SystemContext& InContext, Entity InEntity, SpriteAnimationInspectorState& InState)
{
    auto* ScopedSpriteAnimation = InContext.registry.try_get<SpriteAnimationComponent>(InEntity);
    if (ScopedSpriteAnimation == nullptr)
    {
        return;
    }

    Sprite2D* ScopedSprite = InContext.registry.try_get<Sprite2D>(InEntity);
    SpriteSheetComponent* ScopedGrid = InContext.registry.try_get<SpriteSheetComponent>(InEntity);

    ImGui::PushID("spriteAnimation");
    ImGui::SetNextItemOpen(true, ImGuiCond_Once);
    const bool ScopedOpen = ImGui::CollapsingHeader("SpriteAnimation (flipbook)", ImGuiTreeNodeFlags_DefaultOpen);
    if (!ScopedOpen)
    {
        ImGui::PopID();
        return;
    }

    const char* ScopedMode =
        ScopedSpriteAnimation->IsPlaying
            ? "PLAYING"
            : (ScopedSpriteAnimation->CurrentFrame + 1u >= ClampFrameCount(*ScopedSpriteAnimation) && !ScopedSpriteAnimation->IsLooping ? "FINISHED"
                                                                                                          : "PAUSED");
    ImGui::PushStyleColor(ImGuiCol_Text, StatusColor(ScopedSpriteAnimation->IsPlaying));
    ImGui::TextUnformatted(ScopedMode);
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::TextDisabled("%u / %u", ScopedSpriteAnimation->CurrentFrame + 1u, ClampFrameCount(*ScopedSpriteAnimation));

    if (ImGui::Button(ScopedSpriteAnimation->IsPlaying ? "Pause" : "Play"))
    {
        if (!ScopedSpriteAnimation->IsPlaying && !ScopedSpriteAnimation->IsLooping &&
            ScopedSpriteAnimation->CurrentFrame + 1u >= ClampFrameCount(*ScopedSpriteAnimation))
        {
            ScopedSpriteAnimation->CurrentFrame = 0;
            ScopedSpriteAnimation->ElapsedTime = 0.0f;
        }
        ScopedSpriteAnimation->IsPlaying = !ScopedSpriteAnimation->IsPlaying;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset"))
    {
        ScopedSpriteAnimation->CurrentFrame = 0;
        ScopedSpriteAnimation->ElapsedTime = 0.0f;
        ScopedSpriteAnimation->IsPlaying = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("End"))
    {
        ScopedSpriteAnimation->CurrentFrame = ClampFrameCount(*ScopedSpriteAnimation) - 1u;
        ScopedSpriteAnimation->ElapsedTime = 0.0f;
        ScopedSpriteAnimation->IsPlaying = false;
    }

    const std::uint32_t ScopedFrameCount = ClampFrameCount(*ScopedSpriteAnimation);
    int ScopedFrame = static_cast<int>(std::min(ScopedSpriteAnimation->CurrentFrame, ScopedFrameCount - 1u));
    if (ImGui::SliderInt("frame", &ScopedFrame, 0, static_cast<int>(ScopedFrameCount - 1u), "%d"))
    {
        ScopedSpriteAnimation->CurrentFrame = static_cast<std::uint32_t>(std::max(ScopedFrame, 0));
        ScopedSpriteAnimation->ElapsedTime = 0.0f;
        if (ScopedSprite != nullptr)
        {
            ApplySpriteFrame(*ScopedSpriteAnimation, ScopedGrid, *ScopedSprite);
        }
    }

    int ScopedClipFrames = static_cast<int>(std::min(ScopedSpriteAnimation->FrameCount, 65535u));
    if (ImGui::DragInt("frame count", &ScopedClipFrames, 1.0f, 1, 65535))
    {
        ScopedSpriteAnimation->FrameCount = static_cast<std::uint32_t>(std::max(ScopedClipFrames, 1));
        ScopedSpriteAnimation->CurrentFrame = std::min(ScopedSpriteAnimation->CurrentFrame, ScopedSpriteAnimation->FrameCount - 1u);
        if (ScopedSprite != nullptr)
        {
            ApplySpriteFrame(*ScopedSpriteAnimation, ScopedGrid, *ScopedSprite);
        }
    }

    if (ImGui::DragFloat("frame duration", &ScopedSpriteAnimation->FrameDuration, 0.001f, 0.001f, 10.0f, "%.3f s"))
    {
        ScopedSpriteAnimation->FrameDuration = std::max(ScopedSpriteAnimation->FrameDuration, 0.001f);
        ScopedSpriteAnimation->ElapsedTime = 0.0f;
    }
    ImGui::Checkbox("looping", &ScopedSpriteAnimation->IsLooping);

    ImGui::Checkbox("onion skin", &InState.onionSkin);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90.0f);
    ImGui::DragInt("onion frames", &InState.onionFrames, 1.0f, 1, 8);

    DrawSheetPreview(*ScopedSpriteAnimation, ScopedGrid, InState);
    if (ImGui::Button(HasEventFrame(InState, static_cast<std::uint16_t>(ScopedSpriteAnimation->CurrentFrame))
                          ? "Unmark event"
                          : "Mark event"))
    {
        ToggleEventFrame(InState, static_cast<std::uint16_t>(ScopedSpriteAnimation->CurrentFrame));
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%u event marker(s)", static_cast<unsigned>(InState.eventCount));

    ImGui::InputText("clip name", InState.clipName.data(), InState.clipName.size());
    ImGui::InputText("clip path", InState.clipPath.data(), InState.clipPath.size());
    if (ImGui::Button("Save clip"))
    {
        InState.saveStatus = SaveClip(InState.clipPath.data(), *ScopedSpriteAnimation, ScopedGrid, InState)
                                 ? "clip saved"
                                 : "clip save failed";
    }
    ImGui::SameLine();
    if (ImGui::Button("Load clip"))
    {
        InState.saveStatus = LoadClip(InState.clipPath.data(), *ScopedSpriteAnimation, ScopedGrid, InState)
                                 ? "clip loaded"
                                 : "clip load failed";
        if (ScopedSprite != nullptr)
        {
            ApplySpriteFrame(*ScopedSpriteAnimation, ScopedGrid, *ScopedSprite);
        }
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", InState.saveStatus.c_str());

    ImGui::BeginChild("##frame_timeline", ImVec2(0.0f, 30.0f), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
    for (std::uint32_t ScopedI = 0; ScopedI < ScopedFrameCount; ++ScopedI)
    {
        char ScopedLabel[32] = {};
        std::snprintf(ScopedLabel, sizeof(ScopedLabel), "%s%u", HasEventFrame(InState, static_cast<std::uint16_t>(ScopedI)) ? "*" : "",
                      ScopedI);
        if (ImGui::Button(ScopedLabel))
        {
            ScopedSpriteAnimation->CurrentFrame = ScopedI;
            ScopedSpriteAnimation->ElapsedTime = 0.0f;
            if (ScopedSprite != nullptr)
            {
                ApplySpriteFrame(*ScopedSpriteAnimation, ScopedGrid, *ScopedSprite);
            }
        }
        ImGui::SameLine();
    }
    ImGui::EndChild();

    std::array<char, 24> ScopedAtlasBuffer{};
    std::snprintf(ScopedAtlasBuffer.data(), ScopedAtlasBuffer.size(), "0x%08X:%08X", ScopedSpriteAnimation->TextureAtlas.index,
                  ScopedSpriteAnimation->TextureAtlas.generation);
    ImGui::TextDisabled("atlas handle %s", ScopedSpriteAnimation->TextureAtlas.IsValid() ? ScopedAtlasBuffer.data() : "(none)");
    if (InContext.assetDb != nullptr)
    {
        // The component stores a metadata handle. The inspector accepts
        std::array<char, 24> ScopedAssetIdBuffer{};
        const AssetId ScopedCurrent = InContext.assetDb->Id(ScopedSpriteAnimation->TextureAtlas);
        std::snprintf(ScopedAssetIdBuffer.data(), ScopedAssetIdBuffer.size(), "0x%016llX",
                      static_cast<unsigned long long>(ScopedCurrent.value));
        if (ImGui::InputText("atlas AssetId", ScopedAssetIdBuffer.data(), ScopedAssetIdBuffer.size(),
                             ImGuiInputTextFlags_EnterReturnsTrue))
        {
            std::string_view ScopedText(ScopedAssetIdBuffer.data());
            if (ScopedText.starts_with("0x") || ScopedText.starts_with("0X"))
            {
                ScopedText.remove_prefix(2);
            }
            std::uint64_t ScopedValue = 0;
            const auto ScopedResult =
                std::from_chars(ScopedText.data(), ScopedText.data() + ScopedText.size(), ScopedValue, 16);
            if (ScopedResult.ec == std::errc{} && ScopedResult.ptr == ScopedText.data() + ScopedText.size())
            {
                const AssetId ScopedId{ScopedValue};
                const std::string_view ScopedPath = InContext.assetDb->Path(ScopedId);
                if (!ScopedPath.empty())
                {
                    ScopedSpriteAnimation->TextureAtlas =
                        InContext.assetDb->EnsureMeta(ScopedId, AssetKind::SpriteSheet, ScopedPath);
                }
            }
        }
    }
    ImGui::SameLine();
    ImGui::TextDisabled(ScopedSpriteAnimation->TextureAtlas.IsValid() ? "bound" : "no atlas");

    if (ScopedSprite == nullptr)
    {
        ImGui::TextDisabled("No Sprite2D target: add one to make SpriteAnimation visible.");
    }
    else
    {
        const float ScopedDuration = std::max(ScopedSpriteAnimation->FrameDuration, 0.001f);
        ImGui::ProgressBar(std::clamp(ScopedSpriteAnimation->ElapsedTime / ScopedDuration, 0.0f, 1.0f), ImVec2(-1.0f, 3.0f), "");
        ImGui::TextDisabled("sprite uv  (%.4f, %.4f) - (%.4f, %.4f)  layer %u", ScopedSprite->UVMin.X,
                            ScopedSprite->UVMin.Y, ScopedSprite->UVMax.X, ScopedSprite->UVMax.Y, ScopedSprite->Layer);
    }

    if (ImGui::TreeNodeEx("Frame layout", ImGuiTreeNodeFlags_DefaultOpen))
    {
        DrawSheetGrid(InContext.registry, InEntity, *ScopedSpriteAnimation, ScopedSprite, InState);
        ImGui::TreePop();
    }

    ImGui::PopID();
}
}
