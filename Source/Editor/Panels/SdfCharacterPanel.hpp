// FemboyEngine2D - SDF character authoring panel.
#pragma once

#include "Engine/ECS/Schedule.hpp"

namespace Fby::Editor
{

class SdfCharacterPanel
{
public:
    void Draw(Ecs::SystemContext& InContext, bool* InOpen);
};

}
