// FemboyEngine2D - project layout and transactional settings persistence.
#include "Editor/Infrastructure/ProjectConfig.hpp"

#include "Engine/Assets/AssetMeta.hpp"
#include "Engine/Assets/ProjectScaffold.hpp"
#include "Engine/Core/Log.hpp"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <sstream>
#include <system_error>

// The engine links toml++ with formatters disabled (TOML_ENABLE_FORMATTERS=0)
#if defined(TOML_ENABLE_FORMATTERS)
#undef TOML_ENABLE_FORMATTERS
#endif
#define TOML_ENABLE_FORMATTERS 1
#include <toml.hpp>

#if defined(_WIN32)
#if !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace Fby::Editor
{
namespace
{
void SetError(std::string* InError, std::string InText)
{
    if (InError != nullptr)
    {
        *InError = std::move(InText);
    }
}

// TOML stores the typography sizes as float text, so equality with the shipped
// defaults needs a small tolerance.
[[nodiscard]] bool NearlyEqual(float InLeft, float InRight) noexcept
{
    return std::fabs(InLeft - InRight) <= 0.01f;
}

[[nodiscard]] std::string ReadTextFile(const std::filesystem::path& InPath)
{
    std::ifstream ScopedStream(InPath, std::ios::binary);
    if (!ScopedStream)
    {
        return {};
    }
    return std::string(std::istreambuf_iterator<char>(ScopedStream), std::istreambuf_iterator<char>());
}

[[nodiscard]] std::string_view StringField(const toml::table& InTable, std::string_view InKey)
{
    const auto ScopedNode = InTable[InKey];
    if (const auto ScopedValue = ScopedNode.value<std::string>(); ScopedValue)
    {
        return std::string_view(*ScopedValue);
    }
    return {};
}

[[nodiscard]] bool BoolField(const toml::table& InTable, std::string_view InKey, bool InFallback)
{
    if (const auto ScopedValue = InTable[InKey].value<bool>(); ScopedValue)
    {
        return *ScopedValue;
    }
    return InFallback;
}

[[nodiscard]] double FloatField(const toml::table& InTable, std::string_view InKey, double InFallback)
{
    if (const auto ScopedValue = InTable[InKey].value<double>(); ScopedValue)
    {
        return *ScopedValue;
    }
    if (const auto ScopedValue = InTable[InKey].value<std::int64_t>(); ScopedValue)
    {
        return static_cast<double>(*ScopedValue);
    }
    return InFallback;
}

[[nodiscard]] std::int64_t IntField(const toml::table& InTable, std::string_view InKey, std::int64_t InFallback)
{
    if (const auto ScopedValue = InTable[InKey].value<std::int64_t>(); ScopedValue)
    {
        return *ScopedValue;
    }
    if (const auto ScopedValue = InTable[InKey].value<double>(); ScopedValue)
    {
        return static_cast<std::int64_t>(*ScopedValue);
    }
    return InFallback;
}

[[nodiscard]] const toml::table* Section(const toml::table& InTable, std::string_view InKey)
{
    return InTable[InKey].as_table();
}

[[nodiscard]] Assets::TextureFormat ParseTextureFormat(std::string_view InText,
                                                       Assets::TextureFormat InFallback) noexcept
{
    for (std::uint32_t ScopedValue = 0; ScopedValue <= static_cast<std::uint32_t>(Assets::TextureFormat::Astc6x6Srgb);
         ++ScopedValue)
    {
        const auto ScopedFormat = static_cast<Assets::TextureFormat>(ScopedValue);
        if (InText == Assets::TextureFormatName(ScopedFormat))
        {
            return ScopedFormat;
        }
    }
    return InFallback;
}

[[nodiscard]] bool ValidProjectName(std::string_view InName)
{
    if (InName.empty() || InName.size() >= kProjectNameCapacity || InName == "." || InName == "..")
    {
        return false;
    }
    // The name is substituted into a TOML string, a C++ string literal and a
    for (const char ScopedCharacter : InName)
    {
        const auto ScopedByte = static_cast<unsigned char>(ScopedCharacter);
        if (ScopedByte < 0x20u || ScopedCharacter == '"' || ScopedCharacter == '\'' || ScopedCharacter == '`')
        {
            return false;
        }
    }
    return InName.find('/') == std::string_view::npos && InName.find('\\') == std::string_view::npos &&
           InName.find(':') == std::string_view::npos && InName.find('\0') == std::string_view::npos;
}

// CMake target identifiers are [A-Za-z_][A-Za-z0-9_]*. The display name stays
[[nodiscard]] std::string ScaffoldTargetName(std::string_view InName)
{
    std::string ScopedTarget;
    ScopedTarget.reserve(InName.size() + 1u);
    for (const char ScopedCharacter : InName)
    {
        const auto ScopedByte = static_cast<unsigned char>(ScopedCharacter);
        const bool ScopedAlnum = (ScopedByte >= 'a' && ScopedByte <= 'z') || (ScopedByte >= 'A' && ScopedByte <= 'Z') ||
                                 (ScopedByte >= '0' && ScopedByte <= '9');
        ScopedTarget.push_back(ScopedAlnum ? ScopedCharacter : '_');
    }
    if (ScopedTarget.empty())
    {
        ScopedTarget = "game";
    }
    if (ScopedTarget.front() >= '0' && ScopedTarget.front() <= '9')
    {
        ScopedTarget.insert(ScopedTarget.begin(), '_');
    }
    return ScopedTarget;
}

void AppendEditorIni(std::string& InText, const EditorSettings& InEditor, const AccessibilityEditorSettings& InIde)
{
    AppendEditorSettingsIni(InText, InEditor, InIde);
}
}

Assets::TextureFormat DefaultTextureCookFormat() noexcept
{
#if defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
    return Assets::TextureFormat::Astc4x4Srgb;
#else
    return Assets::TextureFormat::Bc7Srgb;
#endif
}

namespace
{
[[nodiscard]] bool LooksLikeEngineRoot(const std::filesystem::path& InCandidate) noexcept
{
    std::error_code ScopedCode;
    if (InCandidate.empty())
    {
        return false;
    }
    const bool ScopedSource = std::filesystem::is_regular_file(InCandidate / "CMakeLists.txt", ScopedCode) &&
                              std::filesystem::is_directory(InCandidate / "Source" / "Engine", ScopedCode);
    if (ScopedSource)
    {
        return true;
    }
    // Installed layout: the executable ships next to the cooked pack and the
    return std::filesystem::is_regular_file(InCandidate / "assets.fempak", ScopedCode);
}

[[nodiscard]] std::filesystem::path FindEngineRootFrom(std::filesystem::path InStart)
{
    std::error_code ScopedCode;
    if (InStart.empty())
    {
        return {};
    }
    InStart = std::filesystem::absolute(InStart, ScopedCode);
    if (ScopedCode)
    {
        return {};
    }
    for (std::uint32_t ScopedLevel = 0; ScopedLevel < 8 && !InStart.empty(); ++ScopedLevel)
    {
        if (LooksLikeEngineRoot(InStart))
        {
            return InStart;
        }
        const std::filesystem::path ScopedParent = InStart.parent_path();
        if (ScopedParent == InStart)
        {
            break;
        }
        InStart = ScopedParent;
    }
    return {};
}
}

std::filesystem::path ResolveEngineRoot()
{
    std::error_code ScopedCode;
    if (const char* ScopedOverride = std::getenv("FBY_ENGINE_ROOT");
        ScopedOverride != nullptr && ScopedOverride[0] != '\0')
    {
        const std::filesystem::path ScopedCandidate(ScopedOverride);
        if (LooksLikeEngineRoot(ScopedCandidate))
        {
            return std::filesystem::absolute(ScopedCandidate, ScopedCode);
        }
    }

    const std::filesystem::path ScopedFromCwd = FindEngineRootFrom(std::filesystem::current_path(ScopedCode));
    if (!ScopedFromCwd.empty())
    {
        return ScopedFromCwd;
    }

#if defined(FBY_ENGINE_ROOT_DIR)
    const std::filesystem::path ScopedCompiled(FBY_ENGINE_ROOT_DIR);
    if (LooksLikeEngineRoot(ScopedCompiled))
    {
        return std::filesystem::absolute(ScopedCompiled, ScopedCode);
    }
#endif

    return std::filesystem::current_path(ScopedCode);
}

std::filesystem::path DefaultProjectsRoot(bool InCreate, std::string* InError)
{
    const std::filesystem::path ScopedRoot =
        ResolveEngineRoot() / std::filesystem::path(std::string(kDefaultProjectsDirectoryName));
    if (!InCreate)
    {
        return ScopedRoot;
    }
    std::error_code ScopedCode;
    std::filesystem::create_directories(ScopedRoot, ScopedCode);
    if (ScopedCode)
    {
        SetError(InError,
                 "cannot create the default Projects folder '" + ScopedRoot.string() + "': " + ScopedCode.message());
    }
    return ScopedRoot;
}

void CopyProjectString(char* InDestination, std::size_t InCapacity, std::string_view InValue) noexcept
{
    if (InDestination == nullptr || InCapacity == 0)
    {
        return;
    }
    const std::size_t ScopedLength = std::min<std::size_t>(InValue.size(), InCapacity - 1);
    if (ScopedLength > 0)
    {
        std::memcpy(InDestination, InValue.data(), ScopedLength);
    }
    InDestination[ScopedLength] = '\0';
}

std::string_view ProjectString(const char* InSource, std::size_t InCapacity) noexcept
{
    if (InSource == nullptr || InCapacity == 0)
    {
        return {};
    }
    std::size_t ScopedLength = 0;
    while (ScopedLength < InCapacity && InSource[ScopedLength] != '\0')
    {
        ++ScopedLength;
    }
    return std::string_view(InSource, ScopedLength);
}

bool ResolveProjectPaths(const std::filesystem::path& InRootOrProjectFile, ProjectPaths* OutPaths, std::string* InError)
{
    if (OutPaths == nullptr)
    {
        SetError(InError, "project path output is null");
        return false;
    }
    if (InRootOrProjectFile.empty())
    {
        SetError(InError, "project path is empty");
        return false;
    }

    std::filesystem::path ScopedRoot = InRootOrProjectFile;
    if (ScopedRoot.filename() == "project.femproj" || ScopedRoot.extension() == ".femproj")
    {
        ScopedRoot = ScopedRoot.parent_path();
    }
    if (ScopedRoot.empty())
    {
        SetError(InError, "project root is empty");
        return false;
    }

    ProjectPaths ScopedPaths;
    ScopedPaths.root = ScopedRoot;
    ScopedPaths.projectFile = ScopedRoot / "project.femproj";
    ScopedPaths.assets = ScopedRoot / "assets";
    ScopedPaths.textures = ScopedPaths.assets / "textures";
    ScopedPaths.audio = ScopedPaths.assets / "audio";
    ScopedPaths.scenes = ScopedPaths.assets / "scenes";
    ScopedPaths.configs = ScopedRoot / "configs";
    ScopedPaths.editorIni = ScopedRoot / "editor.ini";
    ScopedPaths.build = ScopedRoot / "build";
    ScopedPaths.release = ScopedPaths.build / "release";
    ScopedPaths.archives = ScopedRoot / "archives";
    ScopedPaths.stores = ScopedRoot / "stores";
    ScopedPaths.steamStore = ScopedPaths.stores / "steam";
    ScopedPaths.epicStore = ScopedPaths.stores / "epic";
    ScopedPaths.gogStore = ScopedPaths.stores / "gog";
    *OutPaths = std::move(ScopedPaths);
    return true;
}

bool CreateProject(const std::filesystem::path& InParentDirectory, std::string_view InProjectName,
                   ProjectPaths* OutPaths, ProjectConfig* OutConfig, std::string* InError)
{
    if (!ValidProjectName(InProjectName))
    {
        SetError(InError, "project name must be a plain folder name without separators or '..'");
        return false;
    }
    if (InParentDirectory.empty())
    {
        SetError(InError, "project location is empty");
        return false;
    }

    ProjectPaths ScopedPaths;
    const std::filesystem::path ScopedRoot = InParentDirectory / std::filesystem::path(std::string(InProjectName));
    if (!ResolveProjectPaths(ScopedRoot, &ScopedPaths, InError))
    {
        return false;
    }
    std::error_code ScopedCode;
    if (std::filesystem::exists(ScopedPaths.projectFile, ScopedCode))
    {
        SetError(InError, "a project already exists at " + ScopedPaths.projectFile.string());
        return false;
    }

    // The layout is intentionally small and typed. Nothing is created lazily:
    const std::filesystem::path ScopedDirectories[] = {
        ScopedPaths.root,      ScopedPaths.assets,  ScopedPaths.textures,
        ScopedPaths.audio,     ScopedPaths.scenes,  ScopedPaths.configs,
        ScopedPaths.build,     ScopedPaths.release, ScopedPaths.release / "aot",
        ScopedPaths.archives,  ScopedPaths.stores,  ScopedPaths.steamStore,
        ScopedPaths.epicStore, ScopedPaths.gogStore};
    for (const std::filesystem::path& ScopedDirectory : ScopedDirectories)
    {
        std::filesystem::create_directories(ScopedDirectory, ScopedCode);
        if (ScopedCode)
        {
            SetError(InError, "cannot create '" + ScopedDirectory.string() + "': " + ScopedCode.message());
            return false;
        }
    }

    ProjectConfig ScopedConfig;
    CopyProjectString(ScopedConfig.project.name, sizeof(ScopedConfig.project.name), InProjectName);
    CopyProjectString(ScopedConfig.project.version, sizeof(ScopedConfig.project.version), "0.1.0");
    CopyProjectString(ScopedConfig.project.defaultScene, sizeof(ScopedConfig.project.defaultScene), "scenes/main.toml");
    // BC7 on desktop, ASTC on mobile
    ScopedConfig.project.textureFormat = DefaultTextureCookFormat();
    ScopedConfig.editor.theme[0] = '\0';
    CopyProjectString(ScopedConfig.editor.theme, sizeof(ScopedConfig.editor.theme), "graphite");

    // An empty but valid scene gives "New Project" something to open without
    const std::filesystem::path ScopedDefaultScene = ScopedPaths.scenes / "main.toml";
    if (!std::filesystem::exists(ScopedDefaultScene, ScopedCode))
    {
        const std::string ScopedEmptyScene = "version = 1\nname = \"main\"\nauthor = \"FemboyEngine\"\n";
        if (!WriteFileAtomically(ScopedDefaultScene, ScopedEmptyScene, InError))
        {
            return false;
        }
    }

    // a new project is buildable immediately. The three scaffold files are
    const std::string ScopedTargetName = ScaffoldTargetName(InProjectName);
    const std::string_view ScopedDisplayName = InProjectName;
    const std::filesystem::path ScopedCMakeLists = ScopedPaths.root / "CMakeLists.txt";
    if (!std::filesystem::exists(ScopedCMakeLists, ScopedCode))
    {
        if (!WriteFileAtomically(ScopedCMakeLists, Assets::Scaffold::CMakeLists(ScopedDisplayName, ScopedTargetName),
                                 InError))
        {
            return false;
        }
    }
    const std::filesystem::path ScopedGameMain = ScopedPaths.root / "GameMain.cpp";
    if (!std::filesystem::exists(ScopedGameMain, ScopedCode))
    {
        if (!WriteFileAtomically(ScopedGameMain, Assets::Scaffold::GameMain(ScopedDisplayName, ScopedTargetName),
                                 InError))
        {
            return false;
        }
    }

    if (!WriteFileAtomically(
            ScopedPaths.projectFile,
            Assets::Scaffold::ProjectToml(ScopedDisplayName, ScopedTargetName,
                                          Assets::TextureFormatName(ScopedConfig.project.textureFormat)),
            InError))
    {
        return false;
    }
    // Keep generated build/archive/store trees and the project-local release
    const std::filesystem::path ScopedIgnore = ScopedPaths.root / ".gitignore";
    if (!std::filesystem::exists(ScopedIgnore, ScopedCode))
    {
        if (!WriteFileAtomically(ScopedIgnore,
                                 "/build/\n/.cache/\n/archives/\n/stores/\n/editor.ini\n/.fbyrelease.seed\n", InError))
        {
            return false;
        }
    }
    if (OutPaths != nullptr)
    {
        *OutPaths = ScopedPaths;
    }
    if (OutConfig != nullptr)
    {
        *OutConfig = ScopedConfig;
    }
    return true;
}

bool LoadProject(const std::filesystem::path& InRootOrProjectFile, ProjectPaths* OutPaths, ProjectConfig* OutConfig,
                 std::string* InError)
{
    ProjectPaths ScopedPaths;
    if (!ResolveProjectPaths(InRootOrProjectFile, &ScopedPaths, InError))
    {
        return false;
    }
    const std::string ScopedText = ReadTextFile(ScopedPaths.projectFile);
    if (ScopedText.empty())
    {
        SetError(InError, "project file is missing or empty: " + ScopedPaths.projectFile.string());
        return false;
    }
    ProjectConfig ScopedConfig;
    if (!DecodeProjectToml(ScopedText, &ScopedConfig, InError))
    {
        return false;
    }
    if (OutPaths != nullptr)
    {
        *OutPaths = ScopedPaths;
    }
    if (OutConfig != nullptr)
    {
        *OutConfig = ScopedConfig;
    }
    return true;
}

bool ProjectConfigStore::SetProject(const ProjectPaths& InPaths)
{
    if (!InPaths.IsValid())
    {
        return false;
    }
    MPaths = InPaths;
    MLoaded = false;
    return true;
}

bool ProjectConfigStore::Load(std::string* InError)
{
    if (!MPaths.IsValid())
    {
        SetError(InError, "no project is open");
        return false;
    }
    ProjectConfig ScopedLoaded;
    if (!LoadProject(MPaths.root, nullptr, &ScopedLoaded, InError))
    {
        return false;
    }
    MConfig = ScopedLoaded;
    MSavedProject = MConfig.project;
    MSavedEditor = MConfig.editor;
    MLoaded = true;
    return true;
}

bool ProjectConfigStore::SaveProject(std::string* InError) const
{
    if (!MPaths.IsValid())
    {
        SetError(InError, "no project is open");
        return false;
    }
    return WriteFileAtomically(MPaths.projectFile, EncodeProjectToml(MConfig), InError);
}

bool ProjectConfigStore::SaveEditorIni(std::string* InError) const
{
    if (!MPaths.IsValid())
    {
        SetError(InError, "no project is open");
        return false;
    }
    std::string ScopedText = ReadTextFile(MPaths.editorIni);
    AppendEditorIni(ScopedText, MConfig.editor, MConfig.project.accessibility.editor);
    return WriteFileAtomically(MPaths.editorIni, ScopedText, InError);
}

bool ProjectConfigStore::PersistIfChanged(std::string* InError)
{
    if (!MLoaded || !MPaths.IsValid())
    {
        SetError(InError, "no project is open");
        return false;
    }
    const bool ScopedProjectChanged = MConfig.project != MSavedProject;
    const bool ScopedEditorChanged = MConfig.editor != MSavedEditor;
    if (!ScopedProjectChanged && !ScopedEditorChanged)
    {
        return true;
    }
    if (!SaveProject(InError))
    {
        return false;
    }
    if (!SaveEditorIni(InError))
    {
        return false;
    }
    MSavedProject = MConfig.project;
    MSavedEditor = MConfig.editor;
    return true;
}

bool ProjectConfigStore::Dirty() const noexcept
{
    if (!MLoaded)
    {
        return false;
    }
    return MConfig.project != MSavedProject || MConfig.editor != MSavedEditor;
}

namespace
{
constexpr const char* kCollisionKeys[kNamedCollisionLayers] = {"player", "enemy",       "bullet",
                                                               "water",  "sdf_terrain", "default"};

[[nodiscard]] std::string HexMask(std::uint64_t InMask)
{
    char ScopedBuffer[19]{};
    std::snprintf(ScopedBuffer, sizeof(ScopedBuffer), "%016llx", static_cast<unsigned long long>(InMask));
    return std::string(ScopedBuffer);
}

[[nodiscard]] std::uint64_t ParseHexMask(std::string_view InText)
{
    if (InText.size() > 2 && InText[0] == '0' && (InText[1] == 'x' || InText[1] == 'X'))
    {
        InText.remove_prefix(2);
    }
    std::string ScopedText(InText);
    return std::strtoull(ScopedText.c_str(), nullptr, 16);
}
}

namespace
{
[[nodiscard]] std::uint32_t RoundPowerOfTwo(std::uint32_t InValue) noexcept
{
    std::uint32_t ScopedResult = 1;
    while (ScopedResult < InValue && ScopedResult < (1u << 30))
    {
        ScopedResult <<= 1u;
    }
    return ScopedResult;
}

[[nodiscard]] bool IsPowerOfTwo(std::uint32_t InValue) noexcept
{
    return InValue != 0 && (InValue & (InValue - 1u)) == 0;
}
}

void VirtualTextureSettings::Clamp() noexcept
{
    layers = std::clamp(layers, 1u, kMaxLayers);
    pageSize = std::clamp(RoundPowerOfTwo(pageSize), 64u, kMaxPageSize);
    tileSize = std::clamp(RoundPowerOfTwo(tileSize), 16u, kMaxTileSize);
    if (pageSize < tileSize)
    {
        pageSize = tileSize;
    }
    if (pageSize / tileSize > 32u)
    {
        pageSize = tileSize * 32u;
    }
    chunkTiles = std::clamp(RoundPowerOfTwo(chunkTiles), 1u, kMaxChunkTiles);
    pageTableSize = std::clamp(RoundPowerOfTwo(pageTableSize), 16u, kMaxPageTableSize);
    worldTileSize = std::clamp(worldTileSize, 1.0f, 4096.0f);
    budgetLoadsPerFrame = std::clamp(budgetLoadsPerFrame, 1u, kMaxBudgetLoads);
    prefetchTiles = std::clamp(prefetchTiles, 0u, kMaxPrefetchTiles);
    debugMode = std::min(debugMode, 2u);
}

const char* VirtualTextureSettings::Issue() const noexcept
{
    if (layers == 0 || layers > kMaxLayers)
    {
        return "layers must be 1..8";
    }
    if (!IsPowerOfTwo(pageSize) || !IsPowerOfTwo(tileSize))
    {
        return "page and tile size must be powers of two";
    }
    if (tileSize > pageSize || pageSize / tileSize > 32u)
    {
        return "page size must be tile size * 1..32";
    }
    if (tileSize > kMaxTileSize)
    {
        return "tile size must be at most 128";
    }
    if (!IsPowerOfTwo(chunkTiles))
    {
        return "chunk tiles must be a power of two";
    }
    if (!IsPowerOfTwo(pageTableSize))
    {
        return "page table size must be a power of two";
    }
    return nullptr;
}

std::string EncodeProjectToml(const ProjectConfig& InConfig)
{
    const ProjectSettings& ScopedProject = InConfig.project;
    const EditorSettings& ScopedEditor = InConfig.editor;

    toml::table ScopedRoot;
    ScopedRoot.insert("format_version", static_cast<std::int64_t>(kProjectFormatVersion));

    toml::table ScopedProjectTable;
    ScopedProjectTable.insert("name", std::string(ProjectString(ScopedProject.name, sizeof(ScopedProject.name))));
    ScopedProjectTable.insert("version",
                              std::string(ProjectString(ScopedProject.version, sizeof(ScopedProject.version))));
    ScopedProjectTable.insert(
        "default_scene", std::string(ProjectString(ScopedProject.defaultScene, sizeof(ScopedProject.defaultScene))));
    ScopedRoot.insert("project", std::move(ScopedProjectTable));

    toml::table ScopedSimulation;
    ScopedSimulation.insert("fixed_update_hz", static_cast<double>(ScopedProject.fixedUpdateHz));
    ScopedSimulation.insert("max_fixed_steps", static_cast<std::int64_t>(ScopedProject.maxFixedSteps));
    ScopedRoot.insert("simulation", std::move(ScopedSimulation));

    toml::table ScopedAudio;
    ScopedAudio.insert("master", static_cast<double>(ScopedProject.masterVolume));
    ScopedAudio.insert("sfx", static_cast<double>(ScopedProject.sfxVolume));
    ScopedAudio.insert("music", static_cast<double>(ScopedProject.musicVolume));
    ScopedAudio.insert("input_device",
                       std::string(ProjectString(ScopedProject.voiceInputDevice, sizeof(ScopedProject.voiceInputDevice))));
    ScopedAudio.insert(
        "output_device",
        std::string(ProjectString(ScopedProject.voiceOutputDevice, sizeof(ScopedProject.voiceOutputDevice))));
    ScopedAudio.insert("denoise", static_cast<std::int64_t>(ScopedProject.voiceDenoise));
    ScopedAudio.insert("gate_db", static_cast<double>(ScopedProject.voiceGateDb));
    ScopedAudio.insert("agc", ScopedProject.voiceAgc);
    ScopedAudio.insert("aec", ScopedProject.voiceAec);
    ScopedAudio.insert("aec_tail_ms", static_cast<double>(ScopedProject.voiceAecTailMs));
    ScopedAudio.insert("aec_step", static_cast<double>(ScopedProject.voiceAecStep));
    ScopedAudio.insert("agc_target_db", static_cast<double>(ScopedProject.voiceAgcTargetDb));
    ScopedRoot.insert("audio", std::move(ScopedAudio));

    toml::table ScopedRender;
    ScopedRender.insert("vsync", ScopedEditor.vsync);
    ScopedRender.insert("texture_format", std::string(Assets::TextureFormatName(ScopedProject.textureFormat)));
    ScopedRender.insert("max_texture_size", static_cast<std::int64_t>(ScopedProject.maxTextureSize));
    ScopedRoot.insert("render", std::move(ScopedRender));

    toml::table ScopedVirtualTexture;
    ScopedVirtualTexture.insert("enabled", ScopedProject.virtualTexture.enabled);
    ScopedVirtualTexture.insert("streaming_enabled", ScopedProject.virtualTexture.streamingEnabled);
    ScopedVirtualTexture.insert("layers", static_cast<std::int64_t>(ScopedProject.virtualTexture.layers));
    ScopedVirtualTexture.insert("page_size", static_cast<std::int64_t>(ScopedProject.virtualTexture.pageSize));
    ScopedVirtualTexture.insert("tile_size", static_cast<std::int64_t>(ScopedProject.virtualTexture.tileSize));
    ScopedVirtualTexture.insert("chunk_tiles", static_cast<std::int64_t>(ScopedProject.virtualTexture.chunkTiles));
    ScopedVirtualTexture.insert("page_table_size",
                                static_cast<std::int64_t>(ScopedProject.virtualTexture.pageTableSize));
    ScopedVirtualTexture.insert("world_tile_size", static_cast<double>(ScopedProject.virtualTexture.worldTileSize));
    ScopedVirtualTexture.insert("budget_loads_per_frame",
                                static_cast<std::int64_t>(ScopedProject.virtualTexture.budgetLoadsPerFrame));
    ScopedVirtualTexture.insert("prefetch_tiles",
                                static_cast<std::int64_t>(ScopedProject.virtualTexture.prefetchTiles));
    ScopedVirtualTexture.insert("debug_mode", static_cast<std::int64_t>(ScopedProject.virtualTexture.debugMode));
    ScopedRoot.insert("virtual_texture", std::move(ScopedVirtualTexture));

    toml::table ScopedCollision;
    for (std::uint32_t ScopedLayer = 0; ScopedLayer < kNamedCollisionLayers; ++ScopedLayer)
    {
        ScopedCollision.insert(kCollisionKeys[ScopedLayer], HexMask(ScopedProject.collision.masks[ScopedLayer]));
    }
    ScopedRoot.insert("collision", std::move(ScopedCollision));

    toml::table ScopedBuild;
    ScopedBuild.insert("optimization", std::string(Build::OptimizationName(ScopedProject.build.optimization)));
    ScopedBuild.insert("lto", ScopedProject.build.linkTimeOptimization);
    ScopedBuild.insert("strip_debug_info", ScopedProject.build.stripDebugInfo);
    ScopedBuild.insert("unity_build", ScopedProject.build.unityBuild);
    ScopedBuild.insert("native_arch", ScopedProject.build.nativeArch);
    ScopedRoot.insert("build", std::move(ScopedBuild));

    const AccessibilityProfile& ScopedProfile = ScopedProject.accessibility;
    toml::table ScopedAccessibility;
    ScopedAccessibility.insert("profile_version", static_cast<std::int64_t>(ScopedProfile.version));
    ScopedAccessibility.insert("blind_mode", AccessibilityHasFlag(ScopedProfile.runtime.flags, AccessibilityBlindMode));
    ScopedAccessibility.insert("deaf_mode", AccessibilityHasFlag(ScopedProfile.runtime.flags, AccessibilityDeafMode));
    ScopedAccessibility.insert("show_subtitles", ScopedProfile.runtime.showSubtitles);
    ScopedAccessibility.insert("flash_guard",
                               AccessibilityHasFlag(ScopedProfile.runtime.flags, AccessibilityFlashGuard));
    ScopedAccessibility.insert("flash_threshold", static_cast<double>(ScopedProfile.runtime.flashThreshold));
    ScopedAccessibility.insert("flash_gain", static_cast<double>(ScopedProfile.runtime.flashGain));
    ScopedAccessibility.insert("color_vision", static_cast<std::int64_t>(ScopedProfile.runtime.colorVision));
    ScopedAccessibility.insert("color_vision_strength", static_cast<double>(ScopedProfile.runtime.colorVisionStrength));
    ScopedAccessibility.insert("oled_pixel_shift", ScopedProfile.presentation.oledPixelShift);
    ScopedAccessibility.insert("oled_interval_seconds",
                               static_cast<double>(ScopedProfile.presentation.oledIntervalSeconds));
    ScopedAccessibility.insert("motion_scale", static_cast<double>(ScopedProfile.presentation.motionScale));
    ScopedAccessibility.insert("hud_fade_after_idle_seconds",
                               static_cast<double>(ScopedProfile.presentation.hudFadeAfterIdleSeconds));
    ScopedAccessibility.insert("minimum_contrast_ratio",
                               static_cast<double>(ScopedProfile.presentation.minimumContrastRatio));
    ScopedRoot.insert("accessibility", std::move(ScopedAccessibility));

    toml::table ScopedEditorTable;
    ScopedEditorTable.insert("ui_scale", static_cast<double>(ScopedEditor.uiScale));
    ScopedEditorTable.insert("font_panel_title", static_cast<double>(ScopedEditor.fontPanelTitle));
    ScopedEditorTable.insert("font_panel_body", static_cast<double>(ScopedEditor.fontPanelBody));
    ScopedEditorTable.insert("font_tooltip", static_cast<double>(ScopedEditor.fontTooltip));
    ScopedEditorTable.insert("font_script_node_title", static_cast<double>(ScopedEditor.fontScriptNodeTitle));
    ScopedEditorTable.insert("font_script_pin_input", static_cast<double>(ScopedEditor.fontScriptPinInput));
    ScopedEditorTable.insert("font_script_pin_output", static_cast<double>(ScopedEditor.fontScriptPinOutput));
    ScopedEditorTable.insert("font_scene_text", static_cast<double>(ScopedEditor.fontSceneText));
    ScopedEditorTable.insert("show_demo", ScopedEditor.showDemoWindow);
    ScopedEditorTable.insert("auto_refresh_browser", ScopedEditor.autoRefreshBrowser);
    ScopedEditorTable.insert("watch_assets", ScopedEditor.watchAssets);
    ScopedEditorTable.insert("show_grid", ScopedEditor.showGrid);
    ScopedEditorTable.insert("viewport_view_mode", static_cast<std::int64_t>(ClampViewportViewMode(ScopedEditor.viewportViewMode)));
    ScopedEditorTable.insert("viewport_complexity_budget",
                             static_cast<std::int64_t>(ScopedEditor.viewportComplexityBudget));
    ScopedEditorTable.insert("window_mode", static_cast<std::int64_t>(std::min<std::uint8_t>(ScopedEditor.windowMode, 2)));
    ScopedEditorTable.insert("window_width", static_cast<std::int64_t>(ScopedEditor.windowWidth));
    ScopedEditorTable.insert("window_height", static_cast<std::int64_t>(ScopedEditor.windowHeight));
    ScopedEditorTable.insert("save_ini_on_commit", ScopedEditor.saveEditorIniOnCommit);
    ScopedEditorTable.insert("theme", std::string(ProjectString(ScopedEditor.theme, sizeof(ScopedEditor.theme))));
    const AccessibilityEditorSettings& ScopedEditorProfile = ScopedProfile.editor;
    ScopedEditorTable.insert("ide_blind_mode", ScopedEditorProfile.blindMode);
    ScopedEditorTable.insert("ide_deaf_mode", ScopedEditorProfile.deafMode);
    ScopedEditorTable.insert("ide_flash_guard", ScopedEditorProfile.flashGuard);
    ScopedEditorTable.insert("ide_high_contrast", ScopedEditorProfile.highContrast);
    ScopedEditorTable.insert("ide_reduce_motion", ScopedEditorProfile.reduceMotion);
    ScopedEditorTable.insert("ide_color_vision", static_cast<std::int64_t>(ScopedEditorProfile.colorVision));
    ScopedEditorTable.insert("ide_color_vision_strength", static_cast<double>(ScopedEditorProfile.colorVisionStrength));
    ScopedEditorTable.insert("ide_flash_threshold", static_cast<double>(ScopedEditorProfile.flashThreshold));
    ScopedRoot.insert("editor", std::move(ScopedEditorTable));

    std::ostringstream ScopedStream;
    ScopedStream << ScopedRoot;
    ScopedStream << '\n';
    return ScopedStream.str();
}

bool DecodeProjectToml(std::string_view InText, ProjectConfig* OutConfig, std::string* InError)
{
    if (OutConfig == nullptr)
    {
        SetError(InError, "project config output is null");
        return false;
    }
    toml::table ScopedRoot;
    try
    {
        ScopedRoot = toml::parse(InText);
    }
    catch (const toml::parse_error& ScopedParseError)
    {
        SetError(InError, "project TOML error: " + std::string(ScopedParseError.description()));
        return false;
    }

    ProjectConfig ScopedConfig;
    if (const toml::table* ScopedProject = Section(ScopedRoot, "project"); ScopedProject != nullptr)
    {
        CopyProjectString(ScopedConfig.project.name, sizeof(ScopedConfig.project.name),
                          StringField(*ScopedProject, "name"));
        CopyProjectString(ScopedConfig.project.version, sizeof(ScopedConfig.project.version),
                          StringField(*ScopedProject, "version"));
        CopyProjectString(ScopedConfig.project.defaultScene, sizeof(ScopedConfig.project.defaultScene),
                          StringField(*ScopedProject, "default_scene"));
    }
    else
    {
        SetError(InError, "project file has no [project] table");
        return false;
    }

    if (const toml::table* ScopedSimulation = Section(ScopedRoot, "simulation"); ScopedSimulation != nullptr)
    {
        ScopedConfig.project.fixedUpdateHz =
            static_cast<float>(std::clamp(FloatField(*ScopedSimulation, "fixed_update_hz", 120.0), 1.0, 1000.0));
        ScopedConfig.project.maxFixedSteps = static_cast<std::uint32_t>(
            std::clamp<std::int64_t>(IntField(*ScopedSimulation, "max_fixed_steps", 8), 1, 64));
    }
    if (const toml::table* ScopedAudio = Section(ScopedRoot, "audio"); ScopedAudio != nullptr)
    {
        ScopedConfig.project.masterVolume =
            static_cast<float>(std::clamp(FloatField(*ScopedAudio, "master", 1.0), 0.0, 8.0));
        ScopedConfig.project.sfxVolume = static_cast<float>(std::clamp(FloatField(*ScopedAudio, "sfx", 1.0), 0.0, 8.0));
        ScopedConfig.project.musicVolume =
            static_cast<float>(std::clamp(FloatField(*ScopedAudio, "music", 1.0), 0.0, 8.0));
        CopyProjectString(ScopedConfig.project.voiceInputDevice,
                          sizeof(ScopedConfig.project.voiceInputDevice),
                          StringField(*ScopedAudio, "input_device"));
        CopyProjectString(ScopedConfig.project.voiceOutputDevice,
                          sizeof(ScopedConfig.project.voiceOutputDevice),
                          StringField(*ScopedAudio, "output_device"));
        ScopedConfig.project.voiceDenoise = static_cast<std::uint8_t>(
            std::clamp<std::int64_t>(IntField(*ScopedAudio, "denoise", 2), 0, 3));
        ScopedConfig.project.voiceGateDb =
            static_cast<float>(std::clamp(FloatField(*ScopedAudio, "gate_db", -45.0), -90.0, 0.0));
        ScopedConfig.project.voiceAgc = BoolField(*ScopedAudio, "agc", true);
        ScopedConfig.project.voiceAec = BoolField(*ScopedAudio, "aec", true);
        ScopedConfig.project.voiceAecTailMs =
            static_cast<float>(std::clamp(FloatField(*ScopedAudio, "aec_tail_ms", 128.0), 8.0, 160.0));
        ScopedConfig.project.voiceAecStep =
            static_cast<float>(std::clamp(FloatField(*ScopedAudio, "aec_step", 0.25), 0.01, 1.0));
        ScopedConfig.project.voiceAgcTargetDb =
            static_cast<float>(std::clamp(FloatField(*ScopedAudio, "agc_target_db", -18.0), -40.0, -3.0));
    }
    if (const toml::table* ScopedRender = Section(ScopedRoot, "render"); ScopedRender != nullptr)
    {
        ScopedConfig.editor.vsync = BoolField(*ScopedRender, "vsync", ScopedConfig.editor.vsync);
        ScopedConfig.project.textureFormat =
            ParseTextureFormat(StringField(*ScopedRender, "texture_format"), ScopedConfig.project.textureFormat);
        ScopedConfig.project.maxTextureSize = static_cast<std::uint32_t>(
            std::clamp<std::int64_t>(IntField(*ScopedRender, "max_texture_size", 2048), 64, 16384));
    }
    if (const toml::table* ScopedVirtual = Section(ScopedRoot, "virtual_texture"); ScopedVirtual != nullptr)
    {
        VirtualTextureSettings& ScopedVt = ScopedConfig.project.virtualTexture;
        ScopedVt.enabled = BoolField(*ScopedVirtual, "enabled", ScopedVt.enabled);
        ScopedVt.streamingEnabled = BoolField(*ScopedVirtual, "streaming_enabled", ScopedVt.streamingEnabled);
        ScopedVt.layers =
            static_cast<std::uint32_t>(IntField(*ScopedVirtual, "layers", static_cast<std::int64_t>(ScopedVt.layers)));
        ScopedVt.pageSize = static_cast<std::uint32_t>(
            IntField(*ScopedVirtual, "page_size", static_cast<std::int64_t>(ScopedVt.pageSize)));
        ScopedVt.tileSize = static_cast<std::uint32_t>(
            IntField(*ScopedVirtual, "tile_size", static_cast<std::int64_t>(ScopedVt.tileSize)));
        ScopedVt.chunkTiles = static_cast<std::uint32_t>(
            IntField(*ScopedVirtual, "chunk_tiles", static_cast<std::int64_t>(ScopedVt.chunkTiles)));
        ScopedVt.pageTableSize = static_cast<std::uint32_t>(
            IntField(*ScopedVirtual, "page_table_size", static_cast<std::int64_t>(ScopedVt.pageTableSize)));
        ScopedVt.worldTileSize = static_cast<float>(
            FloatField(*ScopedVirtual, "world_tile_size", static_cast<double>(ScopedVt.worldTileSize)));
        ScopedVt.budgetLoadsPerFrame = static_cast<std::uint32_t>(IntField(
            *ScopedVirtual, "budget_loads_per_frame", static_cast<std::int64_t>(ScopedVt.budgetLoadsPerFrame)));
        ScopedVt.prefetchTiles = static_cast<std::uint32_t>(
            IntField(*ScopedVirtual, "prefetch_tiles", static_cast<std::int64_t>(ScopedVt.prefetchTiles)));
        ScopedVt.debugMode = static_cast<std::uint32_t>(
            IntField(*ScopedVirtual, "debug_mode", static_cast<std::int64_t>(ScopedVt.debugMode)));
        ScopedVt.Clamp();
    }
    if (const toml::table* ScopedCollision = Section(ScopedRoot, "collision"); ScopedCollision != nullptr)
    {
        for (std::uint32_t ScopedLayer = 0; ScopedLayer < kNamedCollisionLayers; ++ScopedLayer)
        {
            const std::string_view ScopedHex = StringField(*ScopedCollision, kCollisionKeys[ScopedLayer]);
            if (!ScopedHex.empty())
            {
                ScopedConfig.project.collision.masks[ScopedLayer] = ParseHexMask(ScopedHex);
            }
        }
    }
    if (const toml::table* ScopedBuild = Section(ScopedRoot, "build"); ScopedBuild != nullptr)
    {
        ScopedConfig.project.build.optimization = Build::ParseOptimizationLevel(
            StringField(*ScopedBuild, "optimization"), ScopedConfig.project.build.optimization);
        ScopedConfig.project.build.linkTimeOptimization =
            BoolField(*ScopedBuild, "lto", ScopedConfig.project.build.linkTimeOptimization);
        ScopedConfig.project.build.stripDebugInfo =
            BoolField(*ScopedBuild, "strip_debug_info", ScopedConfig.project.build.stripDebugInfo);
        ScopedConfig.project.build.unityBuild =
            BoolField(*ScopedBuild, "unity_build", ScopedConfig.project.build.unityBuild);
        ScopedConfig.project.build.nativeArch =
            BoolField(*ScopedBuild, "native_arch", ScopedConfig.project.build.nativeArch);
    }
    AccessibilityProfile ScopedProfile = ScopedConfig.project.accessibility;
    if (const toml::table* ScopedAccessibility = Section(ScopedRoot, "accessibility"); ScopedAccessibility != nullptr)
    {
        ScopedProfile.version = static_cast<std::uint16_t>(std::clamp<std::int64_t>(
            IntField(*ScopedAccessibility, "profile_version", kAccessibilityProfileVersion), 0, 0xFFFF));
        const bool ScopedBlind = BoolField(
            *ScopedAccessibility, "blind_mode",
            AccessibilityHasFlag(ScopedProfile.runtime.flags, AccessibilityBlindMode));
        const bool ScopedDeaf = BoolField(
            *ScopedAccessibility, "deaf_mode",
            AccessibilityHasFlag(ScopedProfile.runtime.flags, AccessibilityDeafMode));
        const bool ScopedFlash = BoolField(
            *ScopedAccessibility, "flash_guard",
            AccessibilityHasFlag(ScopedProfile.runtime.flags, AccessibilityFlashGuard));
        AccessibilitySetFlag(&ScopedProfile.runtime.flags, AccessibilityBlindMode, ScopedBlind);
        AccessibilitySetFlag(&ScopedProfile.runtime.flags, AccessibilityDeafMode, ScopedDeaf);
        AccessibilitySetFlag(&ScopedProfile.runtime.flags, AccessibilityFlashGuard, ScopedFlash);
        ScopedProfile.runtime.showSubtitles =
            BoolField(*ScopedAccessibility, "show_subtitles", ScopedProfile.runtime.showSubtitles);
        ScopedProfile.runtime.flashThreshold = static_cast<float>(
            std::clamp(FloatField(*ScopedAccessibility, "flash_threshold", ScopedProfile.runtime.flashThreshold), 0.02,
                       0.45));
        ScopedProfile.runtime.flashGain = static_cast<float>(
            std::clamp(FloatField(*ScopedAccessibility, "flash_gain", ScopedProfile.runtime.flashGain), 0.05, 1.0));
        const auto ScopedVision = std::clamp<std::int64_t>(
            IntField(*ScopedAccessibility, "color_vision", static_cast<std::int64_t>(ScopedProfile.runtime.colorVision)),
            0, 3);
        ScopedProfile.runtime.colorVision = static_cast<ColorVisionDeficiency>(ScopedVision);
        AccessibilitySetFlag(&ScopedProfile.runtime.flags, AccessibilityColorVisionMode, ScopedVision != 0);
        ScopedProfile.runtime.colorVisionStrength = static_cast<float>(std::clamp(
            FloatField(*ScopedAccessibility, "color_vision_strength", ScopedProfile.runtime.colorVisionStrength), 0.0,
            1.0));
        ScopedProfile.presentation.oledPixelShift = BoolField(*ScopedAccessibility, "oled_pixel_shift",
                                                              ScopedProfile.presentation.oledPixelShift);
        ScopedProfile.presentation.oledIntervalSeconds = static_cast<float>(std::clamp(
            FloatField(*ScopedAccessibility, "oled_interval_seconds", ScopedProfile.presentation.oledIntervalSeconds),
            1.0, 3600.0));
        ScopedProfile.presentation.motionScale = static_cast<float>(std::clamp(
            FloatField(*ScopedAccessibility, "motion_scale", ScopedProfile.presentation.motionScale), 0.0, 1.0));
        ScopedProfile.presentation.hudFadeAfterIdleSeconds =
            static_cast<float>(std::clamp(FloatField(*ScopedAccessibility, "hud_fade_after_idle_seconds",
                                                     ScopedProfile.presentation.hudFadeAfterIdleSeconds),
                                          0.0, 600.0));
        ScopedProfile.presentation.minimumContrastRatio = static_cast<float>(std::clamp(
            FloatField(*ScopedAccessibility, "minimum_contrast_ratio", ScopedProfile.presentation.minimumContrastRatio),
            1.0, 21.0));
    }
    if (const toml::table* ScopedEditor = Section(ScopedRoot, "editor"); ScopedEditor != nullptr)
    {
        ScopedConfig.editor.uiScale =
            static_cast<float>(std::clamp(FloatField(*ScopedEditor, "ui_scale", 1.0), 0.5, 3.0));
        ScopedConfig.editor.fontPanelTitle = static_cast<float>(
            std::clamp(FloatField(*ScopedEditor, "font_panel_title", ScopedConfig.editor.fontPanelTitle), 16.0, 48.0));
        ScopedConfig.editor.fontPanelBody = static_cast<float>(
            std::clamp(FloatField(*ScopedEditor, "font_panel_body", ScopedConfig.editor.fontPanelBody), 16.0, 40.0));
        ScopedConfig.editor.fontTooltip = static_cast<float>(
            std::clamp(FloatField(*ScopedEditor, "font_tooltip", ScopedConfig.editor.fontTooltip), 16.0, 40.0));
        ScopedConfig.editor.fontScriptNodeTitle = static_cast<float>(std::clamp(
            FloatField(*ScopedEditor, "font_script_node_title", ScopedConfig.editor.fontScriptNodeTitle), 16.0, 40.0));
        ScopedConfig.editor.fontScriptPinInput = static_cast<float>(std::clamp(
            FloatField(*ScopedEditor, "font_script_pin_input", ScopedConfig.editor.fontScriptPinInput), 16.0, 40.0));
        ScopedConfig.editor.fontScriptPinOutput = static_cast<float>(std::clamp(
            FloatField(*ScopedEditor, "font_script_pin_output", ScopedConfig.editor.fontScriptPinOutput), 16.0, 40.0));
        ScopedConfig.editor.fontSceneText = static_cast<float>(
            std::clamp(FloatField(*ScopedEditor, "font_scene_text", ScopedConfig.editor.fontSceneText), 16.0, 64.0));

        // Readability pass: an untouched typography block still carries the old
        // 16 px body / 20 px title / 18 px node title defaults. Migrate exactly
        // that combination once to the larger defaults; a project that changed
        // any single value keeps its authored sizes.
        const bool ScopedLegacyTypography =
            NearlyEqual(ScopedConfig.editor.fontPanelBody, 16.0f) &&
            NearlyEqual(ScopedConfig.editor.fontPanelTitle, 20.0f) &&
            NearlyEqual(ScopedConfig.editor.fontTooltip, 16.0f) &&
            NearlyEqual(ScopedConfig.editor.fontScriptNodeTitle, 18.0f) &&
            NearlyEqual(ScopedConfig.editor.fontScriptPinInput, 16.0f) &&
            NearlyEqual(ScopedConfig.editor.fontScriptPinOutput, 16.0f) &&
            NearlyEqual(ScopedConfig.editor.fontSceneText, 16.0f);
        if (ScopedLegacyTypography)
        {
            ScopedConfig.editor.fontPanelTitle = 24.0f;
            ScopedConfig.editor.fontPanelBody = 18.0f;
            ScopedConfig.editor.fontTooltip = 18.0f;
            ScopedConfig.editor.fontScriptNodeTitle = 21.0f;
            ScopedConfig.editor.fontScriptPinInput = 18.0f;
            ScopedConfig.editor.fontScriptPinOutput = 18.0f;
            ScopedConfig.editor.fontSceneText = 18.0f;
            LogInfo("config", "editor typography migrated to the larger defaults");
        }
        ScopedConfig.editor.showDemoWindow = BoolField(*ScopedEditor, "show_demo", ScopedConfig.editor.showDemoWindow);
        ScopedConfig.editor.autoRefreshBrowser =
            BoolField(*ScopedEditor, "auto_refresh_browser", ScopedConfig.editor.autoRefreshBrowser);
        ScopedConfig.editor.watchAssets = BoolField(*ScopedEditor, "watch_assets", ScopedConfig.editor.watchAssets);
        ScopedConfig.editor.showGrid = BoolField(*ScopedEditor, "show_grid", ScopedConfig.editor.showGrid);
        ScopedConfig.editor.viewportViewMode = ClampViewportViewMode(static_cast<std::uint32_t>(std::clamp<std::int64_t>(
            IntField(*ScopedEditor, "viewport_view_mode", ScopedConfig.editor.viewportViewMode), 0, 5)));
        ScopedConfig.editor.viewportComplexityBudget = static_cast<std::uint32_t>(std::clamp<std::int64_t>(
            IntField(*ScopedEditor, "viewport_complexity_budget", ScopedConfig.editor.viewportComplexityBudget), 1, 64));
        ScopedConfig.editor.windowMode = static_cast<std::uint8_t>(
            std::clamp<std::int64_t>(IntField(*ScopedEditor, "window_mode", ScopedConfig.editor.windowMode), 0, 2));
        ScopedConfig.editor.windowWidth = static_cast<std::uint32_t>(std::clamp<std::int64_t>(
            IntField(*ScopedEditor, "window_width", ScopedConfig.editor.windowWidth), 320, 16384));
        ScopedConfig.editor.windowHeight = static_cast<std::uint32_t>(std::clamp<std::int64_t>(
            IntField(*ScopedEditor, "window_height", ScopedConfig.editor.windowHeight), 240, 16384));
        ScopedConfig.editor.saveEditorIniOnCommit =
            BoolField(*ScopedEditor, "save_ini_on_commit", ScopedConfig.editor.saveEditorIniOnCommit);
        CopyProjectString(ScopedConfig.editor.theme, sizeof(ScopedConfig.editor.theme),
                          StringField(*ScopedEditor, "theme"));
        ScopedProfile.editor.blindMode =
            BoolField(*ScopedEditor, "ide_blind_mode", ScopedProfile.editor.blindMode);
        ScopedProfile.editor.deafMode = BoolField(*ScopedEditor, "ide_deaf_mode", ScopedProfile.editor.deafMode);
        ScopedProfile.editor.flashGuard =
            BoolField(*ScopedEditor, "ide_flash_guard", ScopedProfile.editor.flashGuard);
        ScopedProfile.editor.highContrast =
            BoolField(*ScopedEditor, "ide_high_contrast", ScopedProfile.editor.highContrast);
        ScopedProfile.editor.reduceMotion =
            BoolField(*ScopedEditor, "ide_reduce_motion", ScopedProfile.editor.reduceMotion);
        ScopedProfile.editor.colorVision = static_cast<std::uint8_t>(std::clamp<std::int64_t>(
            IntField(*ScopedEditor, "ide_color_vision", ScopedProfile.editor.colorVision), 0, 3));
        ScopedProfile.editor.colorVisionStrength = static_cast<float>(std::clamp(
            FloatField(*ScopedEditor, "ide_color_vision_strength", ScopedProfile.editor.colorVisionStrength), 0.0, 1.0));
        ScopedProfile.editor.flashThreshold = static_cast<float>(std::clamp(
            FloatField(*ScopedEditor, "ide_flash_threshold", ScopedProfile.editor.flashThreshold), 0.02, 1.0));
    }

    ScopedConfig.project.accessibility = ScopedProfile;

    if (ScopedConfig.project.name[0] == '\0')
    {
        CopyProjectString(ScopedConfig.project.name, sizeof(ScopedConfig.project.name), "Untitled Project");
    }
    if (ScopedConfig.project.defaultScene[0] == '\0')
    {
        CopyProjectString(ScopedConfig.project.defaultScene, sizeof(ScopedConfig.project.defaultScene),
                          "scenes/main.toml");
    }
    *OutConfig = ScopedConfig;
    return true;
}

bool WriteFileAtomically(const std::filesystem::path& InPath, std::string_view InText, std::string* InError)
{
    if (InPath.empty())
    {
        SetError(InError, "cannot write an empty path");
        return false;
    }
    std::error_code ScopedCode;
    if (InPath.has_parent_path())
    {
        std::filesystem::create_directories(InPath.parent_path(), ScopedCode);
        if (ScopedCode)
        {
            SetError(InError, "cannot create '" + InPath.parent_path().string() + "': " + ScopedCode.message());
            return false;
        }
    }

    std::filesystem::path ScopedTemporary = InPath;
    ScopedTemporary += ".fbtmp";
    {
        std::ofstream ScopedStream(ScopedTemporary, std::ios::binary | std::ios::trunc);
        if (!ScopedStream)
        {
            SetError(InError, "cannot open '" + ScopedTemporary.string() + "' for writing");
            return false;
        }
        ScopedStream.write(InText.data(), static_cast<std::streamsize>(InText.size()));
        ScopedStream.flush();
        if (!ScopedStream)
        {
            ScopedStream.close();
            std::filesystem::remove(ScopedTemporary, ScopedCode);
            SetError(InError, "write failed for '" + ScopedTemporary.string() + "'");
            return false;
        }
    }

#if defined(_WIN32)
    if (::MoveFileExW(ScopedTemporary.wstring().c_str(), InPath.wstring().c_str(),
                      MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0)
    {
        const DWORD ScopedWin32 = ::GetLastError();
        std::filesystem::remove(ScopedTemporary, ScopedCode);
        SetError(InError, "cannot replace '" + InPath.string() + "' (Win32 error " + std::to_string(ScopedWin32) + ")");
        return false;
    }
#else
    if (::rename(ScopedTemporary.string().c_str(), InPath.string().c_str()) != 0)
    {
        const std::string ScopedReason = std::strerror(errno);
        std::filesystem::remove(ScopedTemporary, ScopedCode);
        SetError(InError, "cannot replace '" + InPath.string() + "': " + ScopedReason);
        return false;
    }
#endif
    return true;
}
}
