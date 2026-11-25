// FemboyEngine2D - plain-text asset license registry.
#include "Engine/Assets/LicenseRegistry.hpp"

#include <algorithm>
#include <cctype>
#include <functional>

namespace Fby::Assets
{
namespace
{
std::string Lower(std::string_view InText)
{
    std::string Scoped(InText);
    std::transform(Scoped.begin(), Scoped.end(), Scoped.begin(),
                   [](unsigned char InChar) { return static_cast<char>(std::tolower(InChar)); });
    return Scoped;
}

bool IsLicenseFile(std::string_view InName)
{
    const std::string ScopedLower = Lower(InName);
    return ScopedLower.ends_with(".txt") || ScopedLower.ends_with(".md") || ScopedLower.ends_with(".license");
}

std::string Stem(std::string_view InName)
{
    const std::size_t ScopedDot = InName.find_last_of('.');
    return std::string(ScopedDot == std::string_view::npos ? InName : InName.substr(0, ScopedDot));
}
}

bool LicenseRegistry::LoadFromVfs(VirtualFileSystem& InVfs, std::string_view InDirectory, std::string* InError)
{
    Clear();
    const std::string ScopedRoot(InDirectory);
    bool ScopedOk = true;
    std::function<void(const std::string&)> ScopedVisit = [&](const std::string& InPath)
    {
        if (!ScopedOk)
        {
            return;
        }
        for (const std::string_view ScopedEntry : InVfs.List(InPath))
        {
            const std::string ScopedPath(ScopedEntry);
            const std::string ScopedFile = ScopedPath.substr(ScopedPath.find_last_of('/') + 1);
            if (!IsLicenseFile(ScopedFile))
            {
                ScopedVisit(ScopedPath);
                continue;
            }
            const std::optional<std::span<const std::byte>> ScopedBytes = InVfs.Read(ScopedPath);
            if (!ScopedBytes)
            {
                ScopedOk = false;
                if (InError != nullptr)
                {
                    *InError = "cannot read license file: " + ScopedPath;
                }
                return;
            }
            std::string ScopedName = ScopedPath;
            if (ScopedName.starts_with(ScopedRoot + "/"))
            {
                ScopedName.erase(0, ScopedRoot.size() + 1);
            }
            ScopedName = Stem(ScopedName);
            if (Find(ScopedName) != nullptr)
            {
                ScopedOk = false;
                if (InError != nullptr)
                {
                    *InError = "duplicate license id: " + ScopedName;
                }
                return;
            }
            std::string ScopedText(reinterpret_cast<const char*>(ScopedBytes->data()), ScopedBytes->size());
            if (ScopedText.empty())
            {
                ScopedOk = false;
                if (InError != nullptr)
                {
                    *InError = "empty license file: " + ScopedPath;
                }
                return;
            }
            MEntries.push_back({std::move(ScopedName), ScopedPath, std::move(ScopedText)});
        }
    };
    ScopedVisit(ScopedRoot);
    std::sort(MEntries.begin(), MEntries.end(),
              [](const LicenseEntry& InLeft, const LicenseEntry& InRight) { return InLeft.Name < InRight.Name; });
    return ScopedOk;
}

void LicenseRegistry::Add(std::string InName, std::string InText)
{
    for (LicenseEntry& ScopedEntry : MEntries)
    {
        if (ScopedEntry.Name == InName)
        {
            ScopedEntry.Text = std::move(InText);
            return;
        }
    }
    MEntries.push_back({std::move(InName), {}, std::move(InText)});
}

void LicenseRegistry::Clear()
{
    MEntries.clear();
    MAssetLicenses.clear();
}

const LicenseEntry* LicenseRegistry::Find(std::string_view InName) const
{
    for (const LicenseEntry& ScopedEntry : MEntries)
    {
        if (ScopedEntry.Name == InName)
        {
            return &ScopedEntry;
        }
    }
    return nullptr;
}

std::string_view LicenseRegistry::Text(std::string_view InName) const
{
    const LicenseEntry* ScopedEntry = Find(InName);
    return ScopedEntry != nullptr ? std::string_view(ScopedEntry->Text) : std::string_view{};
}

void LicenseRegistry::BindAsset(std::string_view InAssetPath, std::string_view InLicenseName)
{
    MAssetLicenses[std::string(InAssetPath)] = std::string(InLicenseName);
}

bool LicenseRegistry::BindAssetMeta(const AssetMeta& InMeta, std::string* InError)
{
    if (InMeta.license[0] == '\0')
    {
        return true;
    }
    if (Find(InMeta.license) == nullptr)
    {
        if (InError != nullptr)
        {
            *InError = std::string("asset '") + InMeta.sourcePath + "' references missing license '" + InMeta.license +
                       "'";
        }
        return false;
    }
    BindAsset(InMeta.sourcePath, InMeta.license);
    return true;
}

std::string_view LicenseRegistry::LicenseForAsset(std::string_view InAssetPath) const
{
    const auto Scoped = MAssetLicenses.find(std::string(InAssetPath));
    return Scoped != MAssetLicenses.end() ? std::string_view(Scoped->second) : std::string_view{};
}

}