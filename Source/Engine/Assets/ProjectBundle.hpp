// FemboyEngine2D - portable source-only project archive (.fbyproj).
#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace Fby::Assets
{

inline constexpr std::string_view kProjectBundleExtension = ".fbyproj";
inline constexpr std::string_view kProjectDescriptorName = "project.femproj";

// Writes a source-only project archive. Build outputs, caches and editor-local
// state are excluded. The archive is a flat, checksummed container; cooked data
// is never transferred.
[[nodiscard]] bool ExportProjectBundle(const std::filesystem::path& InProjectRoot,
                                       const std::filesystem::path& InArchive, std::string* InError = nullptr);

// Imports an archive into InProjectRoot. The root must be empty unless
// InOverwrite is true. Paths are rejected if they escape the root.
[[nodiscard]] bool ImportProjectBundle(const std::filesystem::path& InArchive, const std::filesystem::path& InProjectRoot,
                                       bool InOverwrite = false, std::string* InError = nullptr);

}
