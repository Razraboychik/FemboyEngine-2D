// FemboyEngine2D - import policy shared by the OS watcher and the editor.
#pragma once

#include "Engine/Assets/AssetDatabase.hpp"
#include "Engine/Assets/AssetMeta.hpp"
#include "Engine/Assets/TextureFormat.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace Fby::Editor
{

// Cheap extension policy. Returns false for cooked artifacts, .femeta and
[[nodiscard]] bool InferAssetKindFromPath(std::string_view InSourcePath, AssetKind* OutKind) noexcept;

// "textures/hero.png" -> "textures/hero.ftex"; audio -> .fpcm; video/scene
[[nodiscard]] std::string CookedPathForSource(std::string_view InSourcePath, AssetKind InKind);

// Canonical path, deterministic id/guid, cooked path and default import block.
[[nodiscard]] bool BuildDefaultAssetMeta(std::string_view InSourcePath, AssetKind InKind,
                                         Assets::TextureFormat InTextureFormat, Assets::AssetMeta* OutMeta,
                                         std::string* InError = nullptr);

// <source>.femeta, for example "textures/hero.png.femeta". Keeping the source
[[nodiscard]] std::string MetaFilePathForSource(std::string_view InSourcePath);

// Writes the metadata beside the source under `assetsRoot` atomically.
[[nodiscard]] bool WriteMetaBesideSource(const std::filesystem::path& InAssetsRoot, std::string_view InSourcePath,
                                         const Assets::AssetMeta& InMeta, std::string* InError = nullptr);

// FNV-1a over the file bytes, in fixed-size chunks. Returns 0 when the file
[[nodiscard]] std::uint64_t HashSourceFile(const std::filesystem::path& InPhysicalPath) noexcept;

// One-stop import used by the watcher and the Content Browser:
[[nodiscard]] Assets::AssetHandle ImportSourceFile(Assets::AssetDatabase& InDatabase,
                                                   const std::filesystem::path& InAssetsRoot,
                                                   std::string_view InSourcePath, Assets::TextureFormat InTextureFormat,
                                                   std::string* InError = nullptr);
}
