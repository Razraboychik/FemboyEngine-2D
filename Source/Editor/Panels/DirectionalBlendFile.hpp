// FemboyEngine2D - shared .fblend authoring file codec.
#pragma once

#include "Engine/ECS/DirectionalBlend.hpp"

#include <array>
#include <filesystem>
#include <string>

namespace Fby::Editor::DirectionalBlendFile
{

using PathArray = std::array<std::string, Animation::kMaxDirectionalAnchors>;

// The panel file is editor metadata, not a runtime asset: anchors plus the
// source clip path each anchor resolves to. OutPaths may be null when the
// caller only needs the document.
[[nodiscard]] bool Save(const std::filesystem::path& InPath, const Animation::DirectionalBlendSpaceDocument& InDocument,
                        const PathArray& InPaths, std::string* OutError);

// Rejects a missing header, an unknown topology and a document the baker would
// refuse. On failure the output document is left untouched.
[[nodiscard]] bool Load(const std::filesystem::path& InPath, Animation::DirectionalBlendSpaceDocument* OutDocument,
                        PathArray* OutPaths, std::string* OutError);
}
