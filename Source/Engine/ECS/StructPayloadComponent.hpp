// FemboyEngine2D - optional ECS carrier for bounded authoring payloads.
#pragma once

#include "Engine/Data/StructValue.hpp"

#include <type_traits>

namespace Fby
{
// This component is intentionally rare: only entities that need arbitrary
// authored values carry it, so the 300+ byte payload never widens a common
// archetype. Core gameplay state stays in typed components.
struct StructPayloadComponent
{
    Data::StructPayload Payload;
};

static_assert(std::is_trivially_copyable_v<StructPayloadComponent>);
static_assert(std::is_standard_layout_v<StructPayloadComponent>);
}
