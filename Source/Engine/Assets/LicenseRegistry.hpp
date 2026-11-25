// FemboyEngine2D - plain-text asset license registry.
#pragma once

#include "Engine/Assets/AssetMeta.hpp"
#include "Engine/Assets/VFS.hpp"

#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Fby::Assets
{

struct LicenseEntry
{
    std::string Name;
    std::string Path;
    std::string Text;
};

// Loads the Licenses directory into memory and keeps an optional asset-path to
// license-name mapping. A game or editor can read the exact text and display it.
class LicenseRegistry
{
public:
    bool LoadFromVfs(VirtualFileSystem& InVfs, std::string_view InDirectory = "Licenses",
                     std::string* InError = nullptr);
    void Add(std::string InName, std::string InText);
    void Clear();

    [[nodiscard]] const LicenseEntry* Find(std::string_view InName) const;
    [[nodiscard]] std::string_view Text(std::string_view InName) const;
    [[nodiscard]] std::span<const LicenseEntry> Entries() const noexcept
    {
        return MEntries;
    }
    [[nodiscard]] std::size_t Count() const noexcept
    {
        return MEntries.size();
    }

    void BindAsset(std::string_view InAssetPath, std::string_view InLicenseName);
    [[nodiscard]] bool BindAssetMeta(const AssetMeta& InMeta, std::string* InError = nullptr);
    [[nodiscard]] std::string_view LicenseForAsset(std::string_view InAssetPath) const;

private:
    std::vector<LicenseEntry> MEntries;
    std::map<std::string, std::string> MAssetLicenses;
};

}