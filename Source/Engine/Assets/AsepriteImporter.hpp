// FemboyEngine2D - Aseprite JSON atlas import.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "Engine/ECS/SpriteAnimation.hpp"

namespace Fby::Assets
{
struct AsepriteFrame
{
    std::string Name;
    std::int32_t X = 0;
    std::int32_t Y = 0;
    std::uint32_t Width = 0;
    std::uint32_t Height = 0;
    std::uint32_t DurationMs = 100;
};

struct AsepriteTag
{
    std::string Name;
    std::uint32_t From = 0;
    std::uint32_t To = 0;
    bool Forward = true;
};

struct AsepriteSheet
{
    std::string ImagePath;
    std::uint32_t Width = 0;
    std::uint32_t Height = 0;
    std::vector<AsepriteFrame> Frames;
    std::vector<AsepriteTag> Tags;
};

// Reads the JSON sidecar produced by Aseprite's JSON array/object export.
// Pixel data stays in the PNG; this keeps the image path, frame rectangles and
// animation tags for the sprite/animation importers.
[[nodiscard]] bool ParseAsepriteJson(std::string_view InJson, AsepriteSheet* OutSheet,
                                     std::string* InError = nullptr);

// Converts a uniform, grid-aligned Aseprite sheet to the fixed-cell sprite
// animation path. Non-uniform frames are rejected; the Aseprite metadata stays
// available for tools that need arbitrary rectangles.
[[nodiscard]] bool AsepriteToSpriteSheet(const AsepriteSheet& InSheet, std::uint32_t InFirstFrame,
                                         std::uint32_t InFrameCount, SpriteSheetComponent* OutSheet,
                                         std::uint32_t* OutStartFrame, float* OutFrameDuration,
                                         std::string* InError = nullptr);
}
