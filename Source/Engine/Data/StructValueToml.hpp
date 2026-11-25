// FemboyEngine2D - TOML authoring codec for bounded StructPayload values.
#pragma once

#include "Engine/Data/StructValue.hpp"

#include <toml.hpp>

#include <string>

namespace Fby::Data
{
// Encodes a payload as an array of tables. Every value must have a registered
// type; unknown/unsupported fields fail instead of being silently dropped.
[[nodiscard]] bool EncodeStructPayloadToml(const StructPayload& InPayload, const StructTypeRegistry& InRegistry,
                                           toml::array& OutValues, std::string* InError = nullptr);

[[nodiscard]] bool DecodeStructPayloadToml(const toml::array& InValues, const StructTypeRegistry& InRegistry,
                                           StructPayload* OutPayload, std::string* InError = nullptr);
}
