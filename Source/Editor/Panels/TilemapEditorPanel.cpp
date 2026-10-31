// FemboyEngine2D - tilemap/isometric editor panel implementation.
#include "Editor/Panels/TilemapEditorPanel.hpp"

#include "Editor/Infrastructure/EditorTypography.hpp"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace Fby::Editor
{
namespace
{
constexpr std::uint32_t kPaletteTiles = 32;

[[nodiscard]] ImU32 PaletteColor(std::uint16_t InIndex) noexcept
{
    if (InIndex == 0)
    {
        return IM_COL32(40, 40, 48, 255);
    }
    const float ScopedHue = std::fmod(static_cast<float>(InIndex) * 0.113f, 1.0f);
    float ScopedRed = 0.0f;
    float ScopedGreen = 0.0f;
    float ScopedBlue = 0.0f;
    ImGui::ColorConvertHSVtoRGB(ScopedHue, 0.55f, 0.85f, ScopedRed, ScopedGreen, ScopedBlue);
    return ImGui::ColorConvertFloat4ToU32(ImVec4(ScopedRed, ScopedGreen, ScopedBlue, 1.0f));
}

[[nodiscard]] float CellHeightOf(const TilemapComponent& InMap, Grid::Cell InCell) noexcept
{
    return static_cast<float>(TileHeightOf(Tilemap::At(InMap, InCell))) * InMap.Grid.HeightStep;
}

void CellPolygon(const TilemapComponent& InMap, Grid::Cell InCell, ImVec2 InOrigin, float InScale,
                 ImVec2* OutPoints, int* OutCount) noexcept
{
    const Vector2D ScopedCenter = Tilemap::CellToWorld(InMap, InCell);
    const Vector2D ScopedExtent = Grid::CellExtent(InMap.Grid);
    const float ScopedLift = CellHeightOf(InMap, InCell);
    const ImVec2 ScopedPoint{InOrigin.x + ScopedCenter.X * InScale,
                             InOrigin.y + (ScopedCenter.Y - ScopedLift) * InScale};
    const float ScopedHalfWidth = ScopedExtent.X * 0.5f * InScale;
    const float ScopedHalfHeight = ScopedExtent.Y * 0.5f * InScale;
    if (InMap.Grid.Mode == Grid::ProjectionMode::IsoHex)
    {
        const float ScopedSize = ScopedHalfHeight;
        for (int ScopedVertex = 0; ScopedVertex < 6; ++ScopedVertex)
        {
            const float ScopedAngle = 1.5707963f + static_cast<float>(ScopedVertex) * 1.0471976f;
            OutPoints[ScopedVertex] =
                ImVec2(ScopedPoint.x + std::cos(ScopedAngle) * ScopedSize, ScopedPoint.y + std::sin(ScopedAngle) * ScopedSize);
        }
        *OutCount = 6;
        return;
    }
    if (InMap.Grid.Mode == Grid::ProjectionMode::IsoRect)
    {
        OutPoints[0] = ImVec2(ScopedPoint.x, ScopedPoint.y - ScopedHalfHeight);
        OutPoints[1] = ImVec2(ScopedPoint.x + ScopedHalfWidth, ScopedPoint.y);
        OutPoints[2] = ImVec2(ScopedPoint.x, ScopedPoint.y + ScopedHalfHeight);
        OutPoints[3] = ImVec2(ScopedPoint.x - ScopedHalfWidth, ScopedPoint.y);
        *OutCount = 4;
        return;
    }
    OutPoints[0] = ImVec2(ScopedPoint.x - ScopedHalfWidth, ScopedPoint.y - ScopedHalfHeight);
    OutPoints[1] = ImVec2(ScopedPoint.x + ScopedHalfWidth, ScopedPoint.y - ScopedHalfHeight);
    OutPoints[2] = ImVec2(ScopedPoint.x + ScopedHalfWidth, ScopedPoint.y + ScopedHalfHeight);
    OutPoints[3] = ImVec2(ScopedPoint.x - ScopedHalfWidth, ScopedPoint.y + ScopedHalfHeight);
    *OutCount = 4;
}
}

void TilemapEditorPanel::Reset(std::uint32_t InWidth, std::uint32_t InHeight, Grid::ProjectionMode InMode)
{
    MMap = TilemapComponent{};
    MMap.Grid.Mode = InMode;
    MMap.Grid.CellSize = InMode == Grid::ProjectionMode::IsoHex ? Vector2D{55.4f, 64.0f} : Vector2D{64.0f, 32.0f};
    MMap.AtlasColumns = 8;
    MMap.AtlasRows = 8;
    Tilemap::Resize(MMap, InWidth, InHeight);
    MInitialized = true;
    MStatus = "new map";
}

bool TilemapEditorPanel::SaveToText(std::string* OutText) const
{
    return Tilemap::SaveText(MMap, OutText);
}

bool TilemapEditorPanel::LoadFromText(std::string_view InText, std::string* InError)
{
    TilemapComponent ScopedMap;
    if (!Tilemap::LoadText(InText, &ScopedMap, InError))
    {
        return false;
    }
    MMap = ScopedMap;
    MInitialized = true;
    MStatus = "loaded";
    return true;
}

void TilemapEditorPanel::SetBrush(std::uint16_t InTileIndex, bool InSolid, std::uint8_t InHeight)
{
    MBrush = InTileIndex;
    MBrushSolid = InSolid;
    MBrushHeight = InHeight;
}

bool TilemapEditorPanel::Paint(Grid::Cell InCell)
{
    std::uint32_t ScopedFlags = MBrushSolid ? kTileFlagSolid : 0u;
    return Tilemap::Set(MMap, InCell, MakeTile(MBrush, ScopedFlags, MBrushHeight));
}

bool TilemapEditorPanel::Erase(Grid::Cell InCell)
{
    return Tilemap::Set(MMap, InCell, 0);
}

void TilemapEditorPanel::DrawHeader()
{
    ImGui::Text("Projection");
    ImGui::SameLine();
    static const char* const kModes[4] = {"side", "top-down", "iso-rect", "iso-hex"};
    int ScopedMode = static_cast<int>(MMap.Grid.Mode);
    ImGui::SetNextItemWidth(140.0f);
    if (ImGui::Combo("##mode", &ScopedMode, kModes, 4))
    {
        MMap.Grid.Mode = static_cast<Grid::ProjectionMode>(ScopedMode);
        if (MMap.Grid.Mode == Grid::ProjectionMode::IsoHex && MMap.Grid.CellSize.Y < 8.0f)
        {
            MMap.Grid.CellSize = Vector2D{55.4f, 64.0f};
        }
    }
    ImGui::SameLine();
    ImGui::Text("Cell");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(150.0f);
    float ScopedCell[2] = {MMap.Grid.CellSize.X, MMap.Grid.CellSize.Y};
    if (ImGui::DragFloat2("##cell", ScopedCell, 0.5f, 4.0f, 512.0f, "%.1f"))
    {
        MMap.Grid.CellSize = Vector2D{std::max(ScopedCell[0], 4.0f), std::max(ScopedCell[1], 4.0f)};
    }
    ImGui::SameLine();
    ImGui::Text("Size");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0f);
    int ScopedSize[2] = {static_cast<int>(MMap.Width), static_cast<int>(MMap.Height)};
    if (ImGui::DragInt2("##size", ScopedSize, 0.2f, 1, static_cast<int>(kTilemapMaxWidth)))
    {
        MMap.Width = static_cast<std::uint32_t>(std::max(ScopedSize[0], 1));
        MMap.Height = static_cast<std::uint32_t>(std::max(ScopedSize[1], 1));
    }
    ImGui::SameLine();
    if (ImGui::Button("Resize"))
    {
        Tilemap::Resize(MMap, MMap.Width, MMap.Height);
        MStatus = "resized";
    }

    ImGui::Text("Atlas cols/rows");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(130.0f);
    int ScopedAtlas[2] = {static_cast<int>(MMap.AtlasColumns), static_cast<int>(MMap.AtlasRows)};
    if (ImGui::DragInt2("##atlas", ScopedAtlas, 0.1f, 1, 64))
    {
        MMap.AtlasColumns = static_cast<std::uint32_t>(std::max(ScopedAtlas[0], 1));
        MMap.AtlasRows = static_cast<std::uint32_t>(std::max(ScopedAtlas[1], 1));
    }
    ImGui::SameLine();
    ImGui::Text("Layer");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80.0f);
    int ScopedLayer = static_cast<int>(MMap.TextureLayer);
    if (ImGui::DragInt("##layer", &ScopedLayer, 0.1f, 0, 4096))
    {
        MMap.TextureLayer = static_cast<std::uint32_t>(std::max(ScopedLayer, 0));
    }
    ImGui::SameLine();
    ImGui::Text("Sort");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80.0f);
    int ScopedSort = static_cast<int>(MMap.SortLayer);
    if (ImGui::DragInt("##sort", &ScopedSort, 0.1f, 0, 65535))
    {
        MMap.SortLayer = static_cast<std::uint16_t>(std::clamp(ScopedSort, 0, 65535));
    }
    ImGui::SameLine();
    float ScopedTint[4] = {static_cast<float>((MMap.Tint >> 0) & 0xFFu) / 255.0f,
                           static_cast<float>((MMap.Tint >> 8) & 0xFFu) / 255.0f,
                           static_cast<float>((MMap.Tint >> 16) & 0xFFu) / 255.0f,
                           static_cast<float>((MMap.Tint >> 24) & 0xFFu) / 255.0f};
    if (ImGui::ColorEdit4("##tint", ScopedTint, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel))
    {
        const auto ScopedByte = [](float InValue)
        {
            return static_cast<std::uint32_t>(std::lround(std::clamp(InValue, 0.0f, 1.0f) * 255.0f));
        };
        MMap.Tint = ScopedByte(ScopedTint[0]) | (ScopedByte(ScopedTint[1]) << 8) | (ScopedByte(ScopedTint[2]) << 16) |
                    (ScopedByte(ScopedTint[3]) << 24);
    }
}

void TilemapEditorPanel::DrawPalette()
{
    ImGui::Text("Brush");
    ImGui::SameLine();
    if (ImGui::Button("0 (erase)"))
    {
        MBrush = 0;
    }
    for (std::uint32_t ScopedI = 1; ScopedI <= kPaletteTiles; ++ScopedI)
    {
        if (ScopedI > 1)
        {
            ImGui::SameLine();
        }
        ImGui::PushID(static_cast<int>(ScopedI));
        const ImU32 ScopedColor = PaletteColor(static_cast<std::uint16_t>(ScopedI));
        if (MBrush == ScopedI)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(255, 140, 190, 255));
        }
        else
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ScopedColor);
        }
        char ScopedLabel[8] = {};
        std::snprintf(ScopedLabel, sizeof(ScopedLabel), "%u", static_cast<unsigned>(ScopedI));
        if (ImGui::Button(ScopedLabel, ImVec2(28.0f, 24.0f)))
        {
            MBrush = static_cast<std::uint16_t>(ScopedI);
        }
        ImGui::PopStyleColor();
        ImGui::PopID();
    }
    ImGui::Checkbox("Solid", &MBrushSolid);
    ImGui::SameLine();
    int ScopedHeight = MBrushHeight;
    ImGui::SetNextItemWidth(120.0f);
    if (ImGui::DragInt("height", &ScopedHeight, 0.1f, 0, 15))
    {
        MBrushHeight = static_cast<std::uint8_t>(std::clamp(ScopedHeight, 0, 15));
    }
}

void TilemapEditorPanel::DrawCanvas()
{
    const ImVec2 ScopedAvailable = ImGui::GetContentRegionAvail();
    const ImVec2 ScopedCanvasSize{std::max(ScopedAvailable.x, 64.0f), std::max(ScopedAvailable.y, 120.0f)};
    ImGui::BeginChild("##tilemap_canvas", ScopedCanvasSize, ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImDrawList* ScopedDraw = ImGui::GetWindowDrawList();
    const ImVec2 ScopedCanvasOrigin = ImGui::GetCursorScreenPos();
    const ImVec2 ScopedCanvasMax{ScopedCanvasOrigin.x + ScopedCanvasSize.x, ScopedCanvasOrigin.y + ScopedCanvasSize.y};

    // Fit the map bounds into the canvas with a small margin.
    Vector2D ScopedWorldMin{1.0e9f, 1.0e9f};
    Vector2D ScopedWorldMax{-1.0e9f, -1.0e9f};
    const Vector2D ScopedExtent = Grid::CellExtent(MMap.Grid);
    const Grid::Cell ScopedCorners[4] = {Grid::Cell{0, 0}, Grid::Cell{static_cast<std::int32_t>(MMap.Width) - 1, 0},
                                         Grid::Cell{0, static_cast<std::int32_t>(MMap.Height) - 1},
                                         Grid::Cell{static_cast<std::int32_t>(MMap.Width) - 1,
                                                    static_cast<std::int32_t>(MMap.Height) - 1}};
    for (const Grid::Cell& ScopedCorner : ScopedCorners)
    {
        const Vector2D ScopedWorld = Tilemap::CellToWorld(MMap, ScopedCorner);
        ScopedWorldMin.X = std::min(ScopedWorldMin.X, ScopedWorld.X - ScopedExtent.X * 0.5f);
        ScopedWorldMin.Y = std::min(ScopedWorldMin.Y, ScopedWorld.Y - ScopedExtent.Y * 0.5f);
        ScopedWorldMax.X = std::max(ScopedWorldMax.X, ScopedWorld.X + ScopedExtent.X * 0.5f);
        ScopedWorldMax.Y = std::max(ScopedWorldMax.Y, ScopedWorld.Y + ScopedExtent.Y * 0.5f);
    }
    const float ScopedWorldWidth = std::max(ScopedWorldMax.X - ScopedWorldMin.X, 1.0f);
    const float ScopedWorldHeight = std::max(ScopedWorldMax.Y - ScopedWorldMin.Y, 1.0f);
    const float ScopedScale = std::min(ScopedCanvasSize.x / ScopedWorldWidth, ScopedCanvasSize.y / ScopedWorldHeight) *
                              0.92f;
    const ImVec2 ScopedOrigin{ScopedCanvasOrigin.x + ScopedCanvasSize.x * 0.5f -
                                  (ScopedWorldMin.X + ScopedWorldWidth * 0.5f) * ScopedScale,
                              ScopedCanvasOrigin.y + ScopedCanvasSize.y * 0.5f -
                                  (ScopedWorldMin.Y + ScopedWorldHeight * 0.5f) * ScopedScale};

    ImVec2 ScopedPoints[8] = {};
    int ScopedCount = 0;
    for (std::int32_t ScopedY = 0; ScopedY < static_cast<std::int32_t>(MMap.Height); ++ScopedY)
    {
        for (std::int32_t ScopedX = 0; ScopedX < static_cast<std::int32_t>(MMap.Width); ++ScopedX)
        {
            const Grid::Cell ScopedCell{ScopedX, ScopedY};
            CellPolygon(MMap, ScopedCell, ScopedOrigin, ScopedScale, ScopedPoints, &ScopedCount);
            const std::uint32_t ScopedTile = Tilemap::At(MMap, ScopedCell);
            const bool ScopedEmpty = TileEmpty(ScopedTile);
            const ImU32 ScopedFill = ScopedEmpty ? IM_COL32(30, 32, 40, 200) : PaletteColor(TileIndexOf(ScopedTile));
            const ImU32 ScopedBorder = (ScopedTile & kTileFlagSolid) != 0 ? IM_COL32(255, 120, 120, 220)
                                                                         : IM_COL32(90, 95, 115, 160);
            ScopedDraw->AddConvexPolyFilled(ScopedPoints, ScopedCount, ScopedFill);
            ScopedDraw->AddPolyline(ScopedPoints, ScopedCount, ScopedBorder, ImDrawFlags_Closed, 1.0f);
        }
    }

    // Interaction: the whole child is one hit area.
    ImGui::InvisibleButton("##tilemap_hit", ScopedCanvasSize, ImGuiButtonFlags_MouseButtonLeft |
                                                                 ImGuiButtonFlags_MouseButtonRight);
    const ImVec2 ScopedMouse = ImGui::GetIO().MousePos;
    const bool ScopedHovered = ImGui::IsItemHovered();
    if (ScopedHovered)
    {
        const Vector2D ScopedWorld{(ScopedMouse.x - ScopedOrigin.x) / ScopedScale,
                                   (ScopedMouse.y - ScopedOrigin.y) / ScopedScale};
        const Grid::Cell ScopedCell = Tilemap::WorldToCell(MMap, ScopedWorld);
        if (Tilemap::Contains(MMap, ScopedCell))
        {
            CellPolygon(MMap, ScopedCell, ScopedOrigin, ScopedScale, ScopedPoints, &ScopedCount);
            ScopedDraw->AddPolyline(ScopedPoints, ScopedCount, IM_COL32(255, 220, 120, 255), ImDrawFlags_Closed, 2.0f);
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                (void)Paint(ScopedCell);
            }
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
            {
                (void)Erase(ScopedCell);
            }
            EditorTooltip("cell %d,%d tile %u", static_cast<int>(ScopedCell.X), static_cast<int>(ScopedCell.Y),
                              static_cast<unsigned>(TileIndexOf(Tilemap::At(MMap, ScopedCell))));
        }
    }
    (void)ScopedCanvasMax;
    ImGui::EndChild();
}

void TilemapEditorPanel::DrawToolbar(Ecs::Registry& InRegistry, Entity InSelected)
{
    if (ImGui::Button("Save"))
    {
        std::string ScopedText;
        if (!SaveToText(&ScopedText))
        {
            MStatus = "save: cannot serialize";
        }
        else
        {
            const std::filesystem::path ScopedPath(MPath.data());
            std::error_code ScopedError;
            if (!ScopedPath.parent_path().empty())
            {
                std::filesystem::create_directories(ScopedPath.parent_path(), ScopedError);
            }
            std::ofstream ScopedFile(ScopedPath, std::ios::binary);
            if (!ScopedFile)
            {
                MStatus = "save: cannot open " + ScopedPath.string();
            }
            else
            {
                ScopedFile << ScopedText;
                MStatus = "saved " + ScopedPath.string();
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Load"))
    {
        const std::filesystem::path ScopedPath(MPath.data());
        std::ifstream ScopedFile(ScopedPath, std::ios::binary);
        if (!ScopedFile)
        {
            MStatus = "load: cannot open " + ScopedPath.string();
        }
        else
        {
            std::ostringstream ScopedStream;
            ScopedStream << ScopedFile.rdbuf();
            std::string ScopedError;
            if (!LoadFromText(ScopedStream.str(), &ScopedError))
            {
                MStatus = "load: " + ScopedError;
            }
        }
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(220.0f);
    (void)ImGui::InputText("path", MPath.data(), MPath.size());
    ImGui::SameLine();
    ImGui::BeginDisabled(InSelected == entt::null);
    if (ImGui::Button("Bake to selection"))
    {
        // The runtime grid is rebuilt from the authored reference on load, so the
        // scene stores the `.tilemap` path (a small POD), not 16 KiB of tiles.
        std::string ScopedText;
        if (!SaveToText(&ScopedText))
        {
            MStatus = "bake: cannot serialize";
        }
        else
        {
            const std::filesystem::path ScopedPath(MPath.data());
            std::error_code ScopedError;
            if (!ScopedPath.parent_path().empty())
            {
                std::filesystem::create_directories(ScopedPath.parent_path(), ScopedError);
            }
            std::ofstream ScopedFile(ScopedPath, std::ios::binary | std::ios::trunc);
            if (!ScopedFile)
            {
                MStatus = "bake: cannot open " + ScopedPath.string();
            }
            else
            {
                ScopedFile << ScopedText;
                ScopedFile.close();
                TilemapAssetRefComponent ScopedRef;
                const std::size_t ScopedLength =
                    std::min(std::strlen(MPath.data()), static_cast<std::size_t>(kTilemapRefPathBytes - 1));
                std::memcpy(ScopedRef.Path, MPath.data(), ScopedLength);
                ScopedRef.SortLayer = MMap.SortLayer;
                InRegistry.emplace_or_replace<TilemapAssetRefComponent>(InSelected, ScopedRef);
                InRegistry.emplace_or_replace<TilemapComponent>(InSelected, MMap);
                MStatus = "baked reference " + ScopedPath.string();
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Load from selection"))
    {
        if (const TilemapComponent* ScopedExisting = InRegistry.try_get<TilemapComponent>(InSelected))
        {
            MMap = *ScopedExisting;
            MInitialized = true;
            MStatus = "read from the selected entity";
        }
        else
        {
            MStatus = "the selected entity has no TilemapComponent";
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Clear"))
    {
        Tilemap::Clear(MMap);
        MStatus = "cleared";
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", MStatus.c_str());
}

void TilemapEditorPanel::Draw(Ecs::Registry& InRegistry, Entity InSelected, bool* InOpen)
{
    if (!MInitialized)
    {
        Reset(16, 16, Grid::ProjectionMode::IsoRect);
    }
    bool ScopedOpen = InOpen != nullptr ? *InOpen : MOpen;
    ImGui::SetNextWindowSize(ImVec2(900.0f, 720.0f), ImGuiCond_FirstUseEver);
    if (!BeginEditorPanel("Tilemap", &ScopedOpen))
    {
        EndEditorPanel();
        if (InOpen != nullptr)
        {
            *InOpen = ScopedOpen;
        }
        return;
    }
    MOpen = ScopedOpen;

    DrawHeader();
    ImGui::Separator();
    DrawPalette();
    ImGui::Separator();
    DrawToolbar(InRegistry, InSelected);
    ImGui::Separator();
    DrawCanvas();

    EndEditorPanel();
    if (InOpen != nullptr)
    {
        *InOpen = ScopedOpen;
    }
}
}
