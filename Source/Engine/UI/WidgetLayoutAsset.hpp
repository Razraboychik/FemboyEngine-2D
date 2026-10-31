// FemboyEngine2D - widget layout asset (TOML round-trip).
#pragma once

#include "Engine/UI/PinkUI.hpp"

#include <string>
#include <string_view>

namespace Fby::Ui
{

// Serializes the widget tree (kind, parent, anchors, offsets, flags, text and
// localization key) to the same TOML subset the asset pipeline stores under
// `ui/<name>.toml`. Painter callbacks and per-kind runtime state are not saved.
struct WidgetLayoutAsset
{
    [[nodiscard]] static std::string Serialize(const WidgetCanvas& InCanvas);

    // Replaces the canvas contents with the parsed tree. Returns false and sets
    // InError on a parse/validation failure; the canvas is then left empty.
    [[nodiscard]] static bool Load(WidgetCanvas& InCanvas, std::string_view InToml,
                                   std::string* InError = nullptr);
};
}
