// FemboyEngine2D - SDF sun and occluder authoring panel.
#pragma once

#include "Engine/Core/Types.hpp"

#include <cstdint>

namespace Fby::Fcs
{
class Registry;
}

namespace Fby::Editor
{

// Edits the single SDF sun and the dynamic occluders that drive the screen-space
// SDF shadow mask. The panel writes components only; the scene serializer and
// the renderer pick them up on the next frame.
class SdfLightingPanel
{
public:
    void Draw(Fcs::Registry& InRegistry, Vector2D InSpawnPosition, bool* InOpen);

private:
    void AddSun(Fcs::Registry& InRegistry);
    void AddOccluder(Fcs::Registry& InRegistry, Vector2D InSpawnPosition, bool InBox);

    std::uint32_t MSelectedOccluder = 0xFFFFFFFFu;
};
}
