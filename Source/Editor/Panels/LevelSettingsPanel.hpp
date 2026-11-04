// FemboyEngine2D - per-scene level settings (gravity, fields, ropes).
#pragma once

#include "Engine/ECS/Schedule.hpp"

namespace Fby::Editor
{

// Edits components that belong to the open level, not to the project. The panel
// never owns the data: LevelGravityComponent, GravityBodyComponent and
// JointChainComponent live on scene entities and save with the scene.
class LevelSettingsPanel
{
public:
    LevelSettingsPanel() = default;

    void Draw(Ecs::SystemContext& InContext, bool* InOpen);
};
}
