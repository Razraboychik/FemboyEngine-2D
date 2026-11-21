// FemboyEngine2D - mod manifests, resolver and transactional mod host tests.
#include "Engine/Assets/VFS.hpp"
#include "Engine/Modding/ModHost.hpp"

#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace
{
std::uint32_t GFailures = 0;

void Check(bool InCondition, const char* InMessage)
{
    if (!InCondition)
    {
        std::fprintf(stderr, "FAIL: %s\n", InMessage);
        ++GFailures;
    }
}

[[nodiscard]] std::string ReadText(const Fby::VirtualFileSystem& InVfs, std::string_view InPath)
{
    const auto ScopedBytes = InVfs.Read(InPath);
    if (!ScopedBytes)
    {
        return {};
    }
    return std::string(reinterpret_cast<const char*>(ScopedBytes->data()), ScopedBytes->size());
}

[[nodiscard]] bool WriteText(const std::filesystem::path& InPath, std::string_view InText)
{
    std::error_code ScopedCode;
    std::filesystem::create_directories(InPath.parent_path(), ScopedCode);
    std::ofstream ScopedStream(InPath, std::ios::binary | std::ios::trunc);
    if (!ScopedStream)
    {
        return false;
    }
    ScopedStream.write(InText.data(), static_cast<std::streamsize>(InText.size()));
    return static_cast<bool>(ScopedStream);
}

[[nodiscard]] std::filesystem::path UniqueRoot(std::string_view InTag)
{
    const auto ScopedNow = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path ScopedRoot =
        std::filesystem::current_path() / "build" /
        ("fby_mod_" + std::string(InTag) + "_" + std::to_string(static_cast<long long>(ScopedNow)));
    std::error_code ScopedCode;
    std::filesystem::remove_all(ScopedRoot, ScopedCode);
    std::filesystem::create_directories(ScopedRoot, ScopedCode);
    return ScopedRoot;
}

[[nodiscard]] std::string ManifestText(std::string_view InId, std::string_view InPackage = "content",
                                       std::string_view InDependencies = "")
{
    std::string ScopedText;
    ScopedText += "id = \"" + std::string(InId) + "\"\n";
    ScopedText += "name = \"" + std::string(InId) + "\"\n";
    ScopedText += "version = \"1.0.0\"\n";
    ScopedText += "engine_api = 6\n";
    ScopedText += "side = \"both\"\n";
    ScopedText += "package = \"" + std::string(InPackage) + "\"\n";
    if (!InDependencies.empty())
    {
        ScopedText += "dependencies = [\"" + std::string(InDependencies) + "\"]\n";
    }
    return ScopedText;
}

void TestManifestParsing()
{
    Fby::Modding::ModManifest ScopedManifest;
    std::string ScopedError;
    Check(Fby::Modding::ParseModManifest(ManifestText("hardcore"), &ScopedManifest, &ScopedError),
          "valid manifest parses");
    Check(ScopedManifest.Id == "hardcore" && ScopedManifest.ApiVersion == 6, "manifest fields are loaded");
    Check(Fby::Modding::IsSafeModRelativePath("content") && Fby::Modding::IsSafeModRelativePath("a/b.txt"),
          "safe relative paths pass");
    Check(!Fby::Modding::IsSafeModRelativePath("../escape") && !Fby::Modding::IsSafeModRelativePath("C:/absolute") &&
              !Fby::Modding::IsSafeModRelativePath("a/../b") && !Fby::Modding::IsSafeModRelativePath("a\\b"),
          "unsafe paths are rejected");

    Check(!Fby::Modding::ParseModManifest("id = \"bad id\"\nname = \"x\"\nversion = \"1.0.0\"\nengine_api = 6\n",
                                         &ScopedManifest, &ScopedError),
          "invalid id is rejected");
    Check(!Fby::Modding::ParseModManifest("id = \"x\"\nname = \"x\"\nversion = \"1\"\nengine_api = 6\n",
                                         &ScopedManifest, &ScopedError),
          "invalid version is rejected");
    Check(!Fby::Modding::ParseModManifest("id = \"x\"\nname = \"x\"\nversion = \"1.0.0\"\nengine_api = 99\n",
                                         &ScopedManifest, &ScopedError),
          "unsupported engine ABI is rejected");
    Check(!Fby::Modding::ParseModManifest("id = \"x\"\nname = \"x\"\nversion = \"1.0.0\"\nengine_api = 6\n"
                                         "unknown = true\n",
                                         &ScopedManifest, &ScopedError),
          "unknown key is rejected");
    Check(!Fby::Modding::ParseModManifest("id = \"x\"\nname = \"x\"\nversion = \"1.0.0\"\nengine_api = 6\n"
                                         "package = \"../escape\"\n",
                                         &ScopedManifest, &ScopedError),
          "traversal package is rejected");
    Check(!Fby::Modding::ParseModManifest("id = \"x\"\nname = \"x\"\nversion = \"1.0.0\"\nengine_api = 6\n"
                                         "hash = \"zz\"\n",
                                         &ScopedManifest, &ScopedError),
          "malformed package hash is rejected");
}

void TestResolver()
{
    std::vector<Fby::Modding::ModManifest> ScopedManifests(3);
    ScopedManifests[0].Id = "a";
    ScopedManifests[0].Dependencies = {"b"};
    ScopedManifests[1].Id = "b";
    ScopedManifests[1].Dependencies = {"c"};
    ScopedManifests[2].Id = "c";
    std::vector<std::uint32_t> ScopedOrder;
    std::string ScopedError;
    Check(Fby::Modding::ResolveModOrder(ScopedManifests, &ScopedOrder, &ScopedError), "dependency chain resolves");
    Check(ScopedOrder.size() == 3 && ScopedManifests[ScopedOrder[0]].Id == "c" &&
              ScopedManifests[ScopedOrder[1]].Id == "b" && ScopedManifests[ScopedOrder[2]].Id == "a",
          "dependencies load before dependants");

    auto ScopedCycle = ScopedManifests;
    ScopedCycle[2].Dependencies = {"a"};
    Check(!Fby::Modding::ResolveModOrder(ScopedCycle, &ScopedOrder, &ScopedError), "dependency cycle is rejected");

    auto ScopedMissing = ScopedManifests;
    ScopedMissing[1].Dependencies = {"missing"};
    Check(!Fby::Modding::ResolveModOrder(ScopedMissing, &ScopedOrder, &ScopedError), "missing dependency is rejected");

    auto ScopedDuplicate = ScopedManifests;
    ScopedDuplicate[2].Id = "a";
    Check(!Fby::Modding::ResolveModOrder(ScopedDuplicate, &ScopedOrder, &ScopedError), "duplicate id is rejected");
}

void TestHostAndRollback()
{
    const std::filesystem::path ScopedRoot = UniqueRoot("host");
    const std::filesystem::path ScopedBase = ScopedRoot / "base";
    const std::filesystem::path ScopedMods = ScopedRoot / "mods";
    Check(WriteText(ScopedBase / "data.txt", "base"), "write base content");
    Check(WriteText(ScopedMods / "hardcore" / "mod.toml", ManifestText("hardcore")), "write mod manifest");
    Check(WriteText(ScopedMods / "hardcore" / "content" / "data.txt", "mod"), "write mod content");
    Check(WriteText(ScopedMods / "broken" / "mod.toml", ManifestText("broken", "missing")), "write broken manifest");
    std::string ScopedNativeManifest = ManifestText("native");
    ScopedNativeManifest += "plugin = \"mod.dll\"\n";
    Check(WriteText(ScopedMods / "native" / "mod.toml", ScopedNativeManifest), "write native manifest");
    Check(WriteText(ScopedMods / "native" / "content" / "data.txt", "native"), "write native content");

    Fby::VirtualFileSystem ScopedVfs;
    std::string ScopedError;
    Check(ScopedVfs.MountDirectory(ScopedBase.string(), 0, &ScopedError), "mount base directory");
    Check(ReadText(ScopedVfs, "data.txt") == "base", "base content is visible");

    Fby::Modding::ModHostConfig ScopedConfig;
    ScopedConfig.Vfs = &ScopedVfs;
    ScopedConfig.BasePriority = 10;
    Fby::Modding::ModHost ScopedHost(ScopedConfig);
    Check(!ScopedHost.LoadAll(ScopedMods, &ScopedError), "broken package fails the whole transaction");
    Check(ReadText(ScopedVfs, "data.txt") == "base", "failed load rolls the mount back");
    Check(ScopedHost.Loaded().empty(), "failed load leaves no loaded mods");

    std::error_code ScopedCode;
    std::filesystem::remove_all(ScopedMods / "broken", ScopedCode);
    std::filesystem::remove_all(ScopedMods / "native", ScopedCode);
    Check(ScopedHost.LoadAll(ScopedMods, &ScopedError), "valid mod set loads");
    Check(ScopedHost.Loaded().size() == 1, "one mod is loaded");
    Check(ReadText(ScopedVfs, "data.txt") == "mod", "mod content overrides base by priority");
    Check(!ScopedHost.ModSetHash().empty(), "mod set hash is produced");
    Check(ScopedHost.UnloadAll(&ScopedError), "mod host unloads");
    Check(ReadText(ScopedVfs, "data.txt") == "base", "unload restores base content");

    Check(WriteText(ScopedMods / "native" / "mod.toml", ScopedNativeManifest), "restore native manifest");
    Check(!ScopedHost.LoadAll(ScopedMods, &ScopedError), "native mods are rejected unless the host opts in");
    Check(ReadText(ScopedVfs, "data.txt") == "base", "rejected native mod rolls back its mount");

    std::filesystem::remove_all(ScopedRoot, ScopedCode);
}

void TestHashAndStress()
{
    const std::filesystem::path ScopedRoot = UniqueRoot("stress");
    const std::filesystem::path ScopedFile = ScopedRoot / "package.bin";
    Check(WriteText(ScopedFile, "mod package bytes"), "write hash fixture");
    std::array<std::byte, 32> ScopedFirst{};
    std::array<std::byte, 32> ScopedSecond{};
    std::string ScopedError;
    Check(Fby::Modding::ModHost::HashFile(ScopedFile, &ScopedFirst, &ScopedError), "hash package");
    Check(Fby::Modding::ModHost::HashFile(ScopedFile, &ScopedSecond, &ScopedError), "hash package again");
    Check(ScopedFirst == ScopedSecond, "package hash is deterministic");

    std::vector<Fby::Modding::ModManifest> ScopedManifests(Fby::Modding::kMaxMods);
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedManifests.size(); ++ScopedIndex)
    {
        ScopedManifests[ScopedIndex].Id = "stress." + std::to_string(ScopedIndex);
        if (ScopedIndex != 0)
        {
            ScopedManifests[ScopedIndex].Dependencies = {"stress." + std::to_string(ScopedIndex - 1)};
        }
    }
    std::vector<std::uint32_t> ScopedOrder;
    Check(Fby::Modding::ResolveModOrder(ScopedManifests, &ScopedOrder, &ScopedError),
          "maximum mod chain resolves");
    Check(ScopedOrder.size() == Fby::Modding::kMaxMods, "all mods are ordered");

    std::string ScopedManifestText = ManifestText("stress.0");
    for (std::uint32_t ScopedIteration = 0; ScopedIteration < 5000; ++ScopedIteration)
    {
        Fby::Modding::ModManifest ScopedParsed;
        Check(Fby::Modding::ParseModManifest(ScopedManifestText, &ScopedParsed, &ScopedError),
              "stress manifest parses");
    }
    std::error_code ScopedCode;
    std::filesystem::remove_all(ScopedRoot, ScopedCode);
}
}

int main()
{
    TestManifestParsing();
    TestResolver();
    TestHostAndRollback();
    TestHashAndStress();
    if (GFailures == 0)
    {
        std::printf("modding_test: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "modding_test: %u failure(s)\n", GFailures);
    return 1;
}
