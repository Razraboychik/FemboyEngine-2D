// FemboyEngine2D - Inspector panel for the SpriteAnimation flipbook component.
#pragma once

#include "Engine/ECS/SpriteAnimation.hpp"
#include "Engine/RHI/RHI.hpp"

#include <array>
#include <cstdint>
#include <string>

namespace Fby::Ecs
{
struct SystemContext;
}

namespace Fby::Editor
{

class TextureThumbnailCache;

struct SpriteAnimationInspectorState
{
    bool open = true;
    bool showGrid = true;
    bool onionSkin = false;
    int onionFrames = 1;
    std::array<std::uint16_t, 32> eventFrames{};
    std::uint16_t eventCount = 0;
    std::array<char, 64> clipName{"sprite_clip"};
    std::array<char, 260> clipPath{"assets/clips/sprite_clip.toml"};
    std::string saveStatus{"ready"};
    // Set by the editor host. With both valid the sheet preview draws the real
    // texture layer instead of the placeholder cells.
    Rhi::TextureArrayHandle textureArray{};
    TextureThumbnailCache* thumbnails = nullptr;
};

// Draws the Play/Pause/scrub/duration controls for the selected entity. Safe to
void DrawSpriteAnimationInspector(Ecs::SystemContext& InContext, Entity InEntity, SpriteAnimationInspectorState& InState);
}
