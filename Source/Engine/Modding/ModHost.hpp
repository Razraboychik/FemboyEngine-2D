// FemboyEngine2D - transactional mod package host.
#pragma once

#include "Engine/Assets/VFS.hpp"
#include "Engine/Core/PluginHost.hpp"
#include "Engine/Modding/ModAPI.h"

namespace Fby
{
class ScriptRuntime;
}
#include "Engine/Modding/ModManifest.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace Fby::Modding
{
class ModApiHost;

struct ModHostConfig
{
    VirtualFileSystem* Vfs = nullptr;
    Plugin::PluginHost* Plugins = nullptr;
    // Optional safe data ABI handed to trusted native plugins.
    const FbyModApi* ModApi = nullptr;
    // Optional ModApiHost; when set, ModHost also sets the mod namespace.
    ModApiHost* ApiHost = nullptr;
    // Optional cooked-script host. Scripts are loaded after their package mounts.
    Fby::ScriptRuntime* Scripts = nullptr;
    // Native plugins execute trusted code. Keep this false for downloaded mods.
    bool AllowNative = false;
    bool RequireSignature = false;
    // EdDSA public key used when a manifest carries a signature.
    std::array<std::byte, 32> TrustedPublicKey{};
    bool HasTrustedKey = false;
    int BasePriority = 100;
    ModSide HostSide = ModSide::Both;
};

struct LoadedMod
{
    ModManifest Manifest;
    std::uint32_t MountId = 0;
    std::string PluginPath;
    std::vector<std::uint16_t> ScriptHandles;
    bool PluginLoaded = false;
};

class ModHost
{
public:
    explicit ModHost(ModHostConfig InConfig) noexcept;
    ~ModHost();

    ModHost(const ModHost&) = delete;
    ModHost& operator=(const ModHost&) = delete;

    // Discovers mod.toml files under InModsDirectory, parses, resolves and loads
    // them transactionally. On any failure nothing stays mounted or loaded.
    [[nodiscard]] bool LoadAll(const std::filesystem::path& InModsDirectory, std::string* InError = nullptr);

    // Reverse-order unload. Plugin systems leave the graph, then VFS mounts close.
    [[nodiscard]] bool UnloadAll(std::string* InError = nullptr);

    [[nodiscard]] std::span<const LoadedMod> Loaded() const noexcept
    {
        return MLoaded;
    }
    [[nodiscard]] const ModHostConfig& Config() const noexcept
    {
        return MConfig;
    }

    // SHA-256 over id@version:packageHash in load order. A server rejects a peer
    // with a different simulation mod set.
    [[nodiscard]] std::string ModSetHash() const;

    // Lower-case SHA-256 of a package file, used by manifests and tests.
    [[nodiscard]] static bool HashFile(const std::filesystem::path& InPath, std::array<std::byte, 32>* OutDigest,
                                       std::string* InError = nullptr);

private:
    [[nodiscard]] bool LoadOne(const ModManifest& InManifest, LoadedMod* OutLoaded, std::string* InError);
    void Rollback() noexcept;

    ModHostConfig MConfig;
    std::vector<LoadedMod> MLoaded;
};
}
