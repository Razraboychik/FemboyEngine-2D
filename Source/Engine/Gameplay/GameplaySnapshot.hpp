// FemboyEngine2D - snapshot codecs for the gameplay components.
#pragma once

#include "Engine/ECS/Snapshot.hpp"

namespace Fby
{

// Fixed arrays (tag masks, attribute arrays, effect and skill slots) are not
// expressible in Mirror, so the gameplay components use raw versioned codecs.
void RegisterGameplaySnapshotCodecs(Ecs::SnapshotSchema& InSchema);
}
