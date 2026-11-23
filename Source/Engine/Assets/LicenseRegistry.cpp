// FemboyEngine2D - plain-text asset license registry.
#include "Engine/Assets/LicenseRegistry.hpp"

#include <algorithm>
#include <cctype>

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
    for (const std::string_view ScopedName : InVfs.List(InDirectory))
    {
        if (!IsLicenseFile(ScopedName))
        {
            continue;
        }
        const std::string ScopedNameText(ScopedName);
        const std::string ScopedPrefix = std::string(InDirectory) + "/";
        const std::string ScopedPath =
            ScopedNameText.starts_with(ScopedPrefix) ? ScopedNameText : ScopedPrefix + ScopedNameText;
        const std::size_t ScopedSlash = ScopedNameText.find_last_of('/');
        const std::string_view ScopedFile =
            ScopedSlash == std::string::npos ? std::string_view(ScopedNameText)
                                             : std::string_view(ScopedNameText).substr(ScopedSlash + 1);
        const std::optional<std::span<const std::byte>> ScopedBytes = InVfs.Read(ScopedPath);
        if (!ScopedBytes)
        {
            if (InError != nullptr)
            {
                *InError = "cannot read license file: " + ScopedPath;
            }
            return false;
        }
        LicenseEntry ScopedEntry;
        ScopedEntry.Name = Stem(ScopedFile);
        ScopedEntry.Path = ScopedPath;
        ScopedEntry.Text.assign(reinterpret_cast<const char*>(ScopedBytes->data()), ScopedBytes->size());
        Add(std::move(ScopedEntry.Name), std::move(ScopedEntry.Text));
        MEntries.back().Path = std::move(ScopedPath);
    }
    std::sort(MEntries.begin(), MEntries.end(),
              [](const LicenseEntry& InLeft, const LicenseEntry& InRight) { return InLeft.Name < InRight.Name; });
    return true;
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

std::string_view LicenseRegistry::LicenseForAsset(std::string_view InAssetPath) const
{
    const auto Scoped = MAssetLicenses.find(std::string(InAssetPath));
    return Scoped != MAssetLicenses.end() ? std::string_view(Scoped->second) : std::string_view{};
}

}