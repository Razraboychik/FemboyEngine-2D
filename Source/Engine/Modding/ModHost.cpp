#include "Engine/Modding/ModHost.hpp"

#include "Engine/Core/CryptoAsymmetric.hpp"
#include "Engine/Core/Sha256.hpp"
#include "Engine/Modding/ModApiHost.hpp"
#include "Engine/Script/ScriptSystem.hpp"

#include <algorithm>
#include <charconv>
#include <cstring>
#include <fstream>
#include <system_error>

namespace Fby::Modding
{
namespace
{
void SetError(std::string* InError, std::string_view InMessage)
{
    if (InError != nullptr)
    {
        InError->assign(InMessage);
    }
}

[[nodiscard]] std::string ToHex(std::span<const std::byte> InBytes)
{
    constexpr char kDigits[] = "0123456789abcdef";
    std::string ScopedResult;
    ScopedResult.resize(InBytes.size() * 2);
    for (std::size_t ScopedIndex = 0; ScopedIndex < InBytes.size(); ++ScopedIndex)
    {
        const std::uint8_t ScopedByte = static_cast<std::uint8_t>(InBytes[ScopedIndex]);
        ScopedResult[ScopedIndex * 2] = kDigits[ScopedByte >> 4u];
        ScopedResult[ScopedIndex * 2 + 1] = kDigits[ScopedByte & 0x0Fu];
    }
    return ScopedResult;
}

[[nodiscard]] bool FromHex(std::string_view InText, std::span<std::byte> OutBytes) noexcept
{
    if (InText.size() != OutBytes.size() * 2)
    {
        return false;
    }
    const auto ScopedNibble = [](char InChar) -> int
    {
        if (InChar >= '0' && InChar <= '9')
        {
            return InChar - '0';
        }
        if (InChar >= 'a' && InChar <= 'f')
        {
            return InChar - 'a' + 10;
        }
        return -1;
    };
    for (std::size_t ScopedIndex = 0; ScopedIndex < OutBytes.size(); ++ScopedIndex)
    {
        const int ScopedHigh = ScopedNibble(InText[ScopedIndex * 2]);
        const int ScopedLow = ScopedNibble(InText[ScopedIndex * 2 + 1]);
        if (ScopedHigh < 0 || ScopedLow < 0)
        {
            return false;
        }
        OutBytes[ScopedIndex] = static_cast<std::byte>((ScopedHigh << 4) | ScopedLow);
    }
    return true;
}

[[nodiscard]] bool PathInside(const std::filesystem::path& InRoot, const std::filesystem::path& InCandidate) noexcept
{
    const std::string ScopedRoot = InRoot.generic_string();
    const std::string ScopedCandidate = InCandidate.generic_string();
    if (ScopedCandidate == ScopedRoot)
    {
        return true;
    }
    if (ScopedCandidate.size() <= ScopedRoot.size() || ScopedCandidate.compare(0, ScopedRoot.size(), ScopedRoot) != 0)
    {
        return false;
    }
    return ScopedCandidate[ScopedRoot.size()] == '/';
}

[[nodiscard]] bool SideMatches(ModSide InMod, ModSide InHost) noexcept
{
    return InMod == ModSide::Both || InHost == ModSide::Both || InMod == InHost;
}

[[nodiscard]] bool ResolveInside(const std::filesystem::path& InRoot, std::string_view InRelative,
                                 std::filesystem::path* OutPath, std::string* InError)
{
    if (!IsSafeModRelativePath(InRelative))
    {
        SetError(InError, "mod package path is unsafe");
        return false;
    }
    std::error_code ScopedCode;
    const std::filesystem::path ScopedCandidate = InRoot / std::filesystem::path(InRelative);
    const std::filesystem::path ScopedCanonical = std::filesystem::weakly_canonical(ScopedCandidate, ScopedCode);
    if (ScopedCode || !PathInside(InRoot, ScopedCanonical))
    {
        SetError(InError, "mod package escapes its root");
        return false;
    }
    *OutPath = ScopedCanonical;
    return true;
}
}

ModHost::ModHost(ModHostConfig InConfig) noexcept : MConfig(InConfig) {}

ModHost::~ModHost()
{
    (void)UnloadAll(nullptr);
}

bool ModHost::HashFile(const std::filesystem::path& InPath, std::array<std::byte, 32>* OutDigest, std::string* InError)
{
    if (OutDigest == nullptr)
    {
        return false;
    }
    const std::unique_ptr<MappedFile> ScopedFile = MappedFile::Open(InPath.string(), InError);
    if (!ScopedFile || !ScopedFile->IsOpen() || ScopedFile->Bytes().empty())
    {
        SetError(InError, "mod package is missing or empty");
        return false;
    }
    Crypto::Sha256(ScopedFile->Bytes(), *OutDigest);
    return true;
}

bool ModHost::LoadAll(const std::filesystem::path& InModsDirectory, std::string* InError)
{
    if (MConfig.Vfs == nullptr)
    {
        SetError(InError, "mod host has no VFS");
        return false;
    }
    if (!MLoaded.empty())
    {
        SetError(InError, "mod host is already loaded");
        return false;
    }
    std::error_code ScopedCode;
    const std::filesystem::path ScopedRoot = std::filesystem::weakly_canonical(InModsDirectory, ScopedCode);
    if (ScopedCode || !std::filesystem::is_directory(ScopedRoot, ScopedCode))
    {
        SetError(InError, "mods directory does not exist");
        return false;
    }

    std::vector<std::filesystem::path> ScopedManifestFiles;
    for (const std::filesystem::directory_entry& ScopedEntry : std::filesystem::recursive_directory_iterator(
             ScopedRoot, std::filesystem::directory_options::skip_permission_denied, ScopedCode))
    {
        if (ScopedCode)
        {
            break;
        }
        if (!ScopedEntry.is_regular_file(ScopedCode) || ScopedEntry.path().filename() != "mod.toml")
        {
            continue;
        }
        ScopedManifestFiles.push_back(ScopedEntry.path());
        if (ScopedManifestFiles.size() > kMaxMods)
        {
            SetError(InError, "too many mod manifests");
            return false;
        }
    }
    std::sort(ScopedManifestFiles.begin(), ScopedManifestFiles.end());

    std::vector<ModManifest> ScopedManifests;
    for (const std::filesystem::path& ScopedPath : ScopedManifestFiles)
    {
        ModManifest ScopedManifest;
        std::string ScopedManifestError;
        if (!LoadModManifestFile(ScopedPath, &ScopedManifest, &ScopedManifestError))
        {
            SetError(InError, ScopedPath.generic_string() + ": " + ScopedManifestError);
            return false;
        }
        ScopedManifest.Root = std::filesystem::weakly_canonical(ScopedPath.parent_path(), ScopedCode);
        if (ScopedCode || !SideMatches(ScopedManifest.Side, MConfig.HostSide))
        {
            continue;
        }
        ScopedManifests.push_back(std::move(ScopedManifest));
    }

    std::vector<std::uint32_t> ScopedOrder;
    std::string ScopedResolveError;
    if (!ResolveModOrder(ScopedManifests, &ScopedOrder, &ScopedResolveError))
    {
        SetError(InError, ScopedResolveError);
        return false;
    }

    for (const std::uint32_t ScopedIndex : ScopedOrder)
    {
        LoadedMod ScopedLoaded;
        std::string ScopedLoadError;
        if (!LoadOne(ScopedManifests[ScopedIndex], &ScopedLoaded, &ScopedLoadError))
        {
            SetError(InError, ScopedManifests[ScopedIndex].Id + ": " + ScopedLoadError);
            Rollback();
            return false;
        }
        MLoaded.push_back(std::move(ScopedLoaded));
    }
    return true;
}

bool ModHost::LoadOne(const ModManifest& InManifest, LoadedMod* OutLoaded, std::string* InError)
{
    if (MConfig.Vfs == nullptr || OutLoaded == nullptr)
    {
        return false;
    }
    LoadedMod ScopedLoaded;
    ScopedLoaded.Manifest = InManifest;
    const auto ScopedAbort = [&ScopedLoaded, this]()
    {
        if (MConfig.Scripts != nullptr)
        {
            for (const std::uint16_t ScopedHandle : ScopedLoaded.ScriptHandles)
            {
                (void)MConfig.Scripts->UnloadProgram(ScopedHandle);
            }
        }
        if (MConfig.Vfs != nullptr && ScopedLoaded.MountId != 0)
        {
            MConfig.Vfs->Unmount(ScopedLoaded.MountId);
        }
    };

    std::error_code ScopedCode;
    const std::filesystem::path ScopedRoot = std::filesystem::weakly_canonical(InManifest.Root, ScopedCode);
    if (ScopedCode)
    {
        SetError(InError, "mod root is not usable");
        return false;
    }

    std::filesystem::path ScopedPackage;
    if (InManifest.Package.empty())
    {
        ScopedPackage = ScopedRoot;
    }
    else if (!ResolveInside(ScopedRoot, InManifest.Package, &ScopedPackage, InError))
    {
        return false;
    }

    const int ScopedPriority = std::clamp(MConfig.BasePriority + InManifest.Priority, -100000, 100000);
    if (std::filesystem::is_directory(ScopedPackage, ScopedCode))
    {
        std::string ScopedMountError;
        if (!MConfig.Vfs->MountDirectory(ScopedPackage.string(), ScopedPriority, &ScopedMountError,
                                         &ScopedLoaded.MountId))
        {
            SetError(InError, ScopedMountError);
            return false;
        }
    }
    else if (std::filesystem::is_regular_file(ScopedPackage, ScopedCode))
    {
        if (InManifest.PackageHash.empty())
        {
            SetError(InError, "file package requires a hash");
            return false;
        }
        std::array<std::byte, 32> ScopedDigest{};
        std::string ScopedHashError;
        if (!HashFile(ScopedPackage, &ScopedDigest, &ScopedHashError))
        {
            SetError(InError, ScopedHashError);
            return false;
        }
        if (ToHex(ScopedDigest) != InManifest.PackageHash)
        {
            SetError(InError, "package hash mismatch");
            return false;
        }
        if (!InManifest.Signature.empty())
        {
            if (!MConfig.HasTrustedKey)
            {
                SetError(InError, "signed mod requires a trusted key");
                return false;
            }
            std::array<std::byte, Crypto::kEddsaSignatureBytes> ScopedSignature{};
            if (!FromHex(InManifest.Signature, ScopedSignature) ||
                !Crypto::EddsaVerify(MConfig.TrustedPublicKey, ScopedDigest, ScopedSignature))
            {
                SetError(InError, "package signature is invalid");
                return false;
            }
        }
        else if (MConfig.RequireSignature)
        {
            SetError(InError, "package signature is required");
            return false;
        }
        std::string ScopedMountError;
        if (!MConfig.Vfs->MountPak(ScopedPackage.string(), ScopedPriority, &ScopedMountError, &ScopedLoaded.MountId))
        {
            SetError(InError, ScopedMountError);
            return false;
        }
    }
    else
    {
        SetError(InError, "mod package is not a file or directory");
        return false;
    }

    for (const std::string& ScopedScript : InManifest.Scripts)
    {
        const auto ScopedBytes = MConfig.Vfs->Read(ScopedScript);
        if (!ScopedBytes)
        {
            SetError(InError, "script is missing from the mounted package: " + ScopedScript);
            ScopedAbort();
            return false;
        }
        if (MConfig.Scripts != nullptr)
        {
            std::string ScopedScriptError;
            const std::uint16_t ScopedHandle = MConfig.Scripts->LoadProgram(*ScopedBytes, &ScopedScriptError);
            if (ScopedHandle == Fby::kScriptNoProgram)
            {
                SetError(InError, "script load failed: " + ScopedScriptError);
                ScopedAbort();
                return false;
            }
            ScopedLoaded.ScriptHandles.push_back(ScopedHandle);
        }
    }

    if (!InManifest.Plugin.empty())
    {
        if (!MConfig.AllowNative || MConfig.Plugins == nullptr)
        {
            SetError(InError, "native mods are disabled");
            ScopedAbort();
            return false;
        }
        std::filesystem::path ScopedPlugin;
        if (!ResolveInside(ScopedRoot, InManifest.Plugin, &ScopedPlugin, InError))
        {
            ScopedAbort();
            return false;
        }
        if (MConfig.ApiHost != nullptr)
        {
            MConfig.ApiHost->SetNamespace(InManifest.Id);
            MConfig.Plugins->BindModApi(MConfig.ApiHost->Api());
        }
        else
        {
            MConfig.Plugins->BindModApi(MConfig.ModApi);
        }
        std::string ScopedPluginError;
        if (!MConfig.Plugins->Load(ScopedPlugin.string(), &ScopedPluginError))
        {
            SetError(InError, "plugin load failed: " + ScopedPluginError);
            ScopedAbort();
            return false;
        }
        ScopedLoaded.PluginPath = ScopedPlugin.string();
        ScopedLoaded.PluginLoaded = true;
    }
    *OutLoaded = std::move(ScopedLoaded);
    return true;
}

bool ModHost::UnloadAll(std::string* InError)
{
    bool ScopedOk = true;
    for (auto ScopedIt = MLoaded.rbegin(); ScopedIt != MLoaded.rend(); ++ScopedIt)
    {
        if (MConfig.Scripts != nullptr)
        {
            for (const std::uint16_t ScopedHandle : ScopedIt->ScriptHandles)
            {
                (void)MConfig.Scripts->UnloadProgram(ScopedHandle);
            }
        }
        if (ScopedIt->PluginLoaded && MConfig.Plugins != nullptr)
        {
            std::string ScopedError;
            if (!MConfig.Plugins->Unload(ScopedIt->PluginPath, &ScopedError))
            {
                ScopedOk = false;
                SetError(InError, "plugin unload failed for " + ScopedIt->Manifest.Id + ": " + ScopedError);
            }
        }
        if (MConfig.Vfs != nullptr && ScopedIt->MountId != 0)
        {
            MConfig.Vfs->Unmount(ScopedIt->MountId);
        }
    }
    MLoaded.clear();
    return ScopedOk;
}

void ModHost::Rollback() noexcept
{
    std::string ScopedIgnored;
    (void)UnloadAll(&ScopedIgnored);
}

std::string ModHost::ModSetHash() const
{
    std::string ScopedCanonical;
    for (const LoadedMod& ScopedMod : MLoaded)
    {
        ScopedCanonical += ScopedMod.Manifest.Id;
        ScopedCanonical += '@';
        ScopedCanonical += ScopedMod.Manifest.Version;
        ScopedCanonical += ':';
        ScopedCanonical += ScopedMod.Manifest.PackageHash;
        ScopedCanonical += '\n';
    }
    std::array<std::byte, Crypto::kSha256Bytes> ScopedDigest{};
    Crypto::Sha256(std::span<const std::byte>(reinterpret_cast<const std::byte*>(ScopedCanonical.data()), ScopedCanonical.size()),
           ScopedDigest);
    return ToHex(ScopedDigest);
}
}
