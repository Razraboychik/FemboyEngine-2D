// FemboyEngine2D - tilemap/isometric editor panel.
#pragma once

#include "Engine/Core/Grid.hpp"
#include "Engine/ECS/Components.hpp"
#include "Engine/ECS/Schedule.hpp"
#include "Engine/ECS/Tilemap.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace Fby::Editor
{

// Edits one TilemapComponent: projection mode, atlas mapping, brush and a live
// isometric preview. The map is baked into the selected entity and rendered by
// TilemapRenderSystem, so the panel and the game share one code path.
class TilemapEditorPanel
{
public:
    static constexpr std::uint32_t kPathCapacity = 260;

    TilemapEditorPanel() = default;

    void Draw(Ecs::Registry& InRegistry, Entity InSelected, bool* InOpen);

    // Test seam: no ImGui context required.
    void Reset(std::uint32_t InWidth, std::uint32_t InHeight, Grid::ProjectionMode InMode);
    [[nodiscard]] bool SaveToText(std::string* OutText) const;
    [[nodiscard]] bool LoadFromText(std::string_view InText, std::string* InError = nullptr);
    void SetBrush(std::uint16_t InTileIndex, bool InSolid, std::uint8_t InHeight);
    [[nodiscard]] bool Paint(Grid::Cell InCell);
    [[nodiscard]] bool Erase(Grid::Cell InCell);
    [[nodiscard]] const TilemapComponent& Map() const noexcept
    {
        return MMap;
    }
    [[nodiscard]] const std::string& Status() const noexcept
    {
        return MStatus;
    }

private:
    void DrawToolbar(Ecs::Registry& InRegistry, Entity InSelected);
    void DrawPalette();
    void DrawCanvas();
    void DrawHeader();

    TilemapComponent MMap{};
    std::array<char, kPathCapacity> MPath{"assets/tilemaps/main.tilemap"};
    std::string MStatus{"ready"};
    std::uint16_t MBrush = 1;
    bool MBrushSolid = false;
    std::uint8_t MBrushHeight = 0;
    bool MInitialized = false;
    bool MOpen = true;
};
}
