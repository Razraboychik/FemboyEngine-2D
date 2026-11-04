// FemboyEngine2D - TOML codec for the fixed arrays of JointChainComponent.
#pragma once

#include "Engine/Physics/PhysicsJoints.hpp"

#include <string>
#include <string_view>

namespace Fby
{

// Appends SectionsHex / PathHex lines for the scene TOML writer. The hex form
// keeps float round-tripping exact and avoids growing the reflection schema.
[[nodiscard]] std::string CaptureJointChainToml(const void* InComponent);
// Reads one extra key produced by CaptureJointChainToml into the component.
[[nodiscard]] bool RestoreJointChainToml(void* InComponent, std::string_view InKey, std::string_view InValue,
                                         std::string* OutError);
}
