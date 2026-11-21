// FemboyEngine2D - mod manifests and deterministic load-order resolution.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Fby::Modding
{
inline constexpr std::uint32_t kMaxMods = 64;
inline constexpr std::uint32_t kMaxModManifestBytes = 64u * 1024u;
inline constexpr std::uint32_t kMaxModIdBytes = 64;
inline constexpr std::uint32_t kMaxModPathBytes = 260;

enum class ModSide : std::uint8_t
{
    Client = 0,
    Server = 1,
    Both = 2,
};

struct ModManifest
{
    std::string Id;
    std::string Name;
    std::string Version;
    std::string Author;
    std::string Description;
    std::uint32_t ApiVersion = 0;
    ModSide Side = ModSide::Both;
    std::int32_t Priority = 0;
    std::string Package;
    std::string Plugin;
    std::vector<std::string> Scripts;
    std::vector<std::string> Dependencies;
    // Lower-case SHA-256 of the .fempak package. Required for a file package.
    std::string PackageHash;
    // Optional EdDSA signature over the raw 32-byte package digest.
    std::string Signature;
    std::filesystem::path Root;
};

// Parses one strict mod.toml. Unknown keys, duplicate keys, unsafe paths and
// missing required fields are rejected instead of being ignored.
[[nodiscard]] bool ParseModManifest(std::string_view InToml, ModManifest* OutManifest, std::string* InError = nullptr);

[[nodiscard]] bool LoadModManifestFile(const std::filesystem::path& InPath, ModManifest* OutManifest,
                                       std::string* InError = nullptr);

// A relative path inside a mod package: no absolute path, drive letter,
// backslash, NUL, empty component or `..`.
[[nodiscard]] bool IsSafeModRelativePath(std::string_view InPath) noexcept;

// Resolves dependencies and returns indices in load order. Dependencies load
// before dependants; equal-ready mods load by ascending Priority, then by id.
// Duplicate ids, missing dependencies and cycles fail.
[[nodiscard]] bool ResolveModOrder(const std::vector<ModManifest>& InManifests, std::vector<std::uint32_t>* OutOrder,
                                   std::string* InError = nullptr);
}
