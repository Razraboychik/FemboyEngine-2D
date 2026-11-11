// FemboyEngine2D - minimal RGBA8 PNG writer used by tools and tests.
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace Fby::Assets
{

// Encodes an RGBA8 image as a PNG. The encoder uses stored DEFLATE blocks, so
// the output is valid but not compressed; it is meant for screenshots, atlas
// pages and test fixtures, not for shipping large textures.
[[nodiscard]] bool EncodePng(std::uint32_t InWidth, std::uint32_t InHeight,
                             std::span<const std::uint8_t> InRgba, std::vector<std::byte>* OutPng);

// Writes the same encoding to disk. Returns false for invalid dimensions, a
// mismatched pixel span, or an I/O failure.
[[nodiscard]] bool WritePng(const std::filesystem::path& InPath, std::uint32_t InWidth, std::uint32_t InHeight,
                            std::span<const std::uint8_t> InRgba);

}
