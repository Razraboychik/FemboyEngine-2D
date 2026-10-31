// FemboyEngine2D - release key baking and small orchestration checks.
#include "Editor/Infrastructure/ReleaseCooker.hpp"

#include "Editor/Infrastructure/ProjectConfig.hpp"
#include "Engine/Assets/CookProgress.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

namespace
{
int GFailures = 0;

void Check(bool InCondition, const char* InMessage)
{
    if (!InCondition)
    {
        std::fprintf(stderr, "FAIL: %s\n", InMessage);
        ++GFailures;
    }
}

bool ReadFile(const std::filesystem::path& InPath, std::string& OutText)
{
    std::ifstream ScopedFile(InPath, std::ios::binary);
    if (!ScopedFile)
    {
        return false;
    }
    OutText.assign(std::istreambuf_iterator<char>(ScopedFile), std::istreambuf_iterator<char>());
    return true;
}

bool ReadKeyFile(const std::filesystem::path& InPath, std::array<std::uint8_t, 32>& OutKey)
{
    std::ifstream ScopedFile(InPath, std::ios::binary);
    if (!ScopedFile)
    {
        return false;
    }
    ScopedFile.read(reinterpret_cast<char*>(OutKey.data()), static_cast<std::streamsize>(OutKey.size()));
    return ScopedFile.gcount() == static_cast<std::streamsize>(OutKey.size());
}

void ParseGeneratedArray(std::string_view InHeader, std::string_view InName, std::array<std::uint8_t, 32>& OutArray)
{
    const std::size_t ScopedName = InHeader.find(InName);
    if (ScopedName == std::string_view::npos)
    {
        return;
    }
    const std::size_t ScopedOpen = InHeader.find('{', ScopedName);
    const std::size_t ScopedClose = InHeader.find('}', ScopedOpen);
    if (ScopedOpen == std::string_view::npos || ScopedClose == std::string_view::npos)
    {
        return;
    }
    const char* ScopedCursor = InHeader.data() + ScopedOpen + 1;
    const char* const ScopedEnd = InHeader.data() + ScopedClose;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < OutArray.size() && ScopedCursor < ScopedEnd; ++ScopedIndex)
    {
        char* ScopedNext = nullptr;
        const unsigned long ScopedValue = std::strtoul(ScopedCursor, &ScopedNext, 10);
        if (ScopedNext == ScopedCursor)
        {
            return;
        }
        OutArray[ScopedIndex] = static_cast<std::uint8_t>(ScopedValue & 0xFFu);
        ScopedCursor = ScopedNext;
        while (ScopedCursor < ScopedEnd && (*ScopedCursor == ',' || *ScopedCursor == ' ' || *ScopedCursor == '\n' ||
                                            *ScopedCursor == '\r' || *ScopedCursor == '\t'))
        {
            ++ScopedCursor;
        }
    }
}

void TestKeyBaking()
{
    const std::filesystem::path ScopedRoot = std::filesystem::current_path() / "_fby_release_key_test";
    std::error_code ScopedError;
    std::filesystem::remove_all(ScopedRoot, ScopedError);
    std::filesystem::create_directories(ScopedRoot, ScopedError);
    if (ScopedError)
    {
        std::fprintf(stderr, "release cooker: cannot create '%s': %s\n", ScopedRoot.string().c_str(),
                     ScopedError.message().c_str());
    }
    const std::filesystem::path ScopedSeed = ScopedRoot / "release.seed";
    const std::filesystem::path ScopedKey = ScopedRoot / "release.key";
    const std::filesystem::path ScopedHeader = ScopedRoot / "ReleaseKey.generated.hpp";

    char ScopedErrorText[256]{};
    const bool ScopedBaked = Fby::Editor::BakeReleaseKey(ScopedSeed.string().c_str(), ScopedKey.string().c_str(),
                                                         ScopedHeader.string().c_str(), ScopedErrorText,
                                                         sizeof(ScopedErrorText));
    if (!ScopedBaked)
    {
        std::fprintf(stderr, "release cooker: BakeReleaseKey failed: %s\n",
                     ScopedErrorText[0] != '\0' ? ScopedErrorText : "(no detail)");
    }
    Check(ScopedBaked, "BakeReleaseKey generates seed, key and header");
    std::array<std::uint8_t, 32> ScopedFirstKey{};
    Check(ReadKeyFile(ScopedKey, ScopedFirstKey), "key file is 32 bytes");
    Check(std::filesystem::file_size(ScopedSeed, ScopedError) == 32, "seed file is 32 bytes");

    std::string ScopedHeaderText;
    Check(ReadFile(ScopedHeader, ScopedHeaderText), "generated header exists");
    Check(ScopedHeaderText.find("fby_release_archive_key") != std::string::npos,
          "generated header exports the C ABI key function");
    std::array<std::uint8_t, 32> ScopedMasked{};
    std::array<std::uint8_t, 32> ScopedMask{};
    ParseGeneratedArray(ScopedHeaderText, "kMasked[32]", ScopedMasked);
    ParseGeneratedArray(ScopedHeaderText, "kMask[32]", ScopedMask);
    bool ScopedHeaderMatches = true;
    for (std::size_t ScopedIndex = 0; ScopedIndex < ScopedFirstKey.size(); ++ScopedIndex)
    {
        const std::uint8_t ScopedDecoded =
            static_cast<std::uint8_t>(ScopedMasked[ScopedIndex] ^ ScopedMask[ScopedIndex]);
        ScopedHeaderMatches = ScopedHeaderMatches && ScopedDecoded == ScopedFirstKey[ScopedIndex];
    }
    Check(ScopedHeaderMatches, "header reconstructs the baked key");

    // A second run reuses the seed and is byte-identical.
    Check(Fby::Editor::BakeReleaseKey(ScopedSeed.string().c_str(), ScopedKey.string().c_str(),
                                      ScopedHeader.string().c_str(), ScopedErrorText, sizeof(ScopedErrorText)),
          "second BakeReleaseKey run succeeds");
    std::array<std::uint8_t, 32> ScopedSecondKey{};
    Check(ReadKeyFile(ScopedKey, ScopedSecondKey), "second key file is readable");
    Check(ScopedFirstKey == ScopedSecondKey, "same seed produces the same archive key");

    Fby::Editor::ReleaseCookConfig ScopedConfig;
    Fby::Editor::ReleaseCooker ScopedCooker;
    Check(!ScopedCooker.Start(ScopedConfig, nullptr, ScopedErrorText, sizeof(ScopedErrorText)),
          "Start rejects a config without a packer path");

    // FromProject must keep every output below the project root: build
    Fby::Editor::ProjectPaths ScopedPaths;
    ScopedPaths.root = ScopedRoot;
    ScopedPaths.assets = ScopedRoot / "assets";
    ScopedPaths.build = ScopedRoot / "build";
    ScopedPaths.release = ScopedPaths.build / "release";
    ScopedPaths.archives = ScopedRoot / "archives";
    ScopedPaths.stores = ScopedRoot / "stores";
    Fby::Editor::ProjectSettings ScopedSettings;
    const Fby::Editor::ReleaseCookConfig ScopedFromProject =
        Fby::Editor::ReleaseCooker::FromProject(ScopedPaths, ScopedSettings, "C:/FemboyEngine", "C:/fby_fempak.exe");
    Check(std::string_view(ScopedFromProject.outputRoot) == ScopedPaths.release.generic_string(),
          "release build output stays in <project>/build/release");
    Check(std::string_view(ScopedFromProject.archivePath) ==
              (ScopedPaths.archives / "game.femarc").generic_string(),
          "canonical archive stays in <project>/archives");
    Check(std::string_view(ScopedFromProject.storesRoot) == ScopedPaths.stores.generic_string(),
          "store staging root stays in <project>/stores");

    std::filesystem::remove_all(ScopedRoot, ScopedError);
}

void TestProgressRecord()
{
    static_assert(sizeof(Fby::Assets::CookProgressRecord) == 424);
    Fby::Assets::CookProgressRecord ScopedRecord;
    std::memcpy(ScopedRecord.magic, Fby::Assets::kCookProgressMagic, sizeof(ScopedRecord.magic));
    ScopedRecord.stage = static_cast<std::uint32_t>(Fby::Assets::CookProgressStage::Encrypting);
    ScopedRecord.completed = 3;
    ScopedRecord.total = 4;
    ScopedRecord.cipher = 1;
    ScopedRecord.flags = Fby::Assets::CookProgressActive | Fby::Assets::CookProgressEncrypted;
    Check(ScopedRecord.Active() && ScopedRecord.Encrypted(), "progress flags decode");
    Check(ScopedRecord.Fraction() == 0.75f, "progress fraction is completed / total");
    Check(std::string_view(Fby::Assets::CookCipherName(1)) == "ChaCha20-Poly1305", "cipher name is stable");
}

void TestStoreManifests()
{
    std::error_code ScopedError;
    const std::filesystem::path ScopedRoot =
        std::filesystem::current_path(ScopedError) / "fby_store_manifest_test";
    std::filesystem::remove_all(ScopedRoot, ScopedError);
    std::filesystem::create_directories(ScopedRoot, ScopedError);
    const std::filesystem::path ScopedArchive = ScopedRoot / "game.femarc";
    const std::filesystem::path ScopedPlugin = ScopedRoot / "logic_graph.dll";
    {
        std::ofstream ScopedArchiveFile(ScopedArchive, std::ios::binary);
        ScopedArchiveFile << "FEMARC-payload-0123456789";
    }
    {
        std::ofstream ScopedPluginFile(ScopedPlugin, std::ios::binary);
        ScopedPluginFile << "plugin-bytes";
    }

    const std::filesystem::path ScopedSteam = ScopedRoot / "steam";
    std::string ScopedManifestError;
    Check(Fby::Editor::WriteStoreManifest(ScopedSteam, "steam", ScopedArchive, ScopedPlugin, "Demo", 1, true,
                                          &ScopedManifestError),
          "steam store manifest is written");
    if (!ScopedManifestError.empty())
    {
        std::fprintf(stderr, "manifest error: %s (root %s)\n", ScopedManifestError.c_str(),
                     ScopedRoot.string().c_str());
    }
    Check(std::filesystem::is_regular_file(ScopedSteam / "store.txt", ScopedError), "store.txt exists");
    Check(std::filesystem::is_regular_file(ScopedSteam / "store_manifest.json", ScopedError),
          "store_manifest.json exists");
    Check(std::filesystem::is_regular_file(ScopedSteam / "depot_build.vdf", ScopedError),
          "steam depot script exists");
    std::string ScopedManifestText;
    Check(ReadFile(ScopedSteam / "store_manifest.json", ScopedManifestText), "manifest is readable");
    Check(ScopedManifestText.find("\"store\": \"steam\"") != std::string::npos, "manifest names the store");
    Check(ScopedManifestText.find("\"signing\": \"unsigned\"") != std::string::npos,
          "manifest records the unsigned signing state");
    Check(ScopedManifestText.find("\"bytes\": 25") != std::string::npos, "manifest records the archive size");
    Check(ScopedManifestText.find("\"fnv1a64\": \"0000000000000000\"") == std::string::npos,
          "manifest carries a real archive hash");
    std::string ScopedStoreText;
    Check(ReadFile(ScopedSteam / "store.txt", ScopedStoreText), "store.txt is readable");
    Check(ScopedStoreText.find("key_baked: true") != std::string::npos, "store.txt records key baking");

    // GOG and Epic get the JSON descriptor but no Steam depot script.
    const std::filesystem::path ScopedGog = ScopedRoot / "gog";
    Check(Fby::Editor::WriteStoreManifest(ScopedGog, "gog", ScopedArchive, std::filesystem::path{}, "Demo", 0, false,
                                          &ScopedManifestError),
          "gog store manifest is written without a plugin");
    Check(!std::filesystem::exists(ScopedGog / "depot_build.vdf", ScopedError), "gog has no steam depot script");

    // Attribution and the MPL source offer travel with every store folder.
    Check(Fby::Editor::WriteStoreNotices(ScopedGog, &ScopedManifestError), "gog store notices are written");
    std::string ScopedNoticesText;
    Check(ReadFile(ScopedGog / "THIRD_PARTY_NOTICES.md", ScopedNoticesText), "notices file is readable");
    Check(ScopedNoticesText.find("Mozilla Public License 2.0") != std::string::npos,
          "notices name the engine license");
    Check(ScopedNoticesText.find("Monocypher") != std::string::npos, "notices list Monocypher");
    std::string ScopedOfferText;
    Check(ReadFile(ScopedGog / "SOURCE_OFFER.txt", ScopedOfferText), "source offer is readable");
    Check(ScopedOfferText.find("Mozilla Public License 2.0") != std::string::npos,
          "source offer names the engine license");

    // A missing archive is a hard failure, not an empty-hash manifest.
    const std::filesystem::path ScopedEpic = ScopedRoot / "epic";
    Check(!Fby::Editor::WriteStoreManifest(ScopedEpic, "epic", ScopedRoot / "missing.femarc",
                                           std::filesystem::path{}, "Demo", 0, false, &ScopedManifestError),
          "missing archive is rejected");
    Check(ScopedManifestError.find("archive") != std::string::npos, "error names the archive");

    std::filesystem::remove_all(ScopedRoot, ScopedError);
}
}

int main()
{
    TestKeyBaking();
    TestProgressRecord();
    TestStoreManifests();
    if (GFailures != 0)
    {
        std::fprintf(stderr, "release cooker: %d failure(s)\n", GFailures);
        return 1;
    }
    std::printf("release cooker: OK (deterministic key, ABI header, progress record, store manifests)\n");
    return 0;
}
