// FemboyEngine2D - bundled base fonts and their license bindings.
#pragma once

#include "Engine/Assets/LicenseRegistry.hpp"
#include "Engine/Assets/VFS.hpp"
#include "Engine/Text/TextFontStack.hpp"
#include "Engine/Text/TextShaper.hpp"

#include <span>
#include <string>
#include <string_view>

namespace Fby::Text
{

enum class BaseFontRole : std::uint8_t
{
    Sans,
    Mono,
    Display
};

struct BaseFontInfo
{
    std::string_view Id;
    std::string_view Path;
    std::string_view LicenseId;
    BaseFontRole Role = BaseFontRole::Sans;
    TextScript Script = TextScript::Latin;
    float PixelSize = 32.0f;
    std::uint32_t AtlasSize = 2048;
    bool Paged = false;
    bool AllGlyphs = false;
};

// Stable catalogue shipped with the engine. The paths are VFS paths, so a game
// can shadow any face in its own assets mount without changing the catalogue.
[[nodiscard]] std::span<const BaseFontInfo> BaseFontCatalog() noexcept;

// Mounts every bundled face into an existing TextFontStack. Returns the number
// mounted; a missing optional face is reported through OutError and stops the
// mount so a caller never silently runs with a partial script stack.
[[nodiscard]] std::uint32_t MountBaseFonts(TextFontStack& InStack, VirtualFileSystem& InVfs,
                                           std::string* OutError = nullptr);

// Loads Assets/Licenses and binds every bundled font to its license entry.
[[nodiscard]] bool BindBaseFontLicenses(Assets::LicenseRegistry& InLicenses, VirtualFileSystem& InVfs,
                                        std::string* OutError = nullptr);

}