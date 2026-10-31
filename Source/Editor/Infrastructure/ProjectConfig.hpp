// FemboyEngine2D - project descriptor, typed settings and the transactional
#pragma once

#include "Editor/Infrastructure/EditorSettings.hpp"
#include "Engine/Assets/TextureFormat.hpp"
#include "Engine/Core/BuildProfile.hpp"
#include "Engine/Audio/AudioDevices.hpp"
#include "Engine/Physics/CollisionFilter.hpp"
#include "Engine/UI/Accessibility.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <type_traits>

namespace Fby::Editor
{

inline constexpr std::uint32_t kProjectNameCapacity = 96;
inline constexpr std::uint32_t kProjectVersionCapacity = 32;
inline constexpr std::uint32_t kProjectScenePathCapacity = 160;
inline constexpr std::uint32_t kProjectFormatVersion = 4;

// Game-facing settings. Stored in `project.femproj`, consumed by the runtime
// Virtual-texture layout. Structural values (page/tile/chunk/table) are authored
// per project and baked into chunk data; runtime values (budget, prefetch,
// streaming, debug) can change live from the editor panel.
struct VirtualTextureSettings
{
    // Opt-in: no VT resource is created for a project that leaves this off.
    bool enabled = false;
    // Runtime switch for the streaming loop (the resident pages stay usable).
    bool streamingEnabled = true;
    std::uint32_t layers = 2;
    std::uint32_t pageSize = 128;
    std::uint32_t tileSize = 64;
    std::uint32_t chunkTiles = 2;
    std::uint32_t pageTableSize = 64;
    float worldTileSize = 256.0f;
    std::uint32_t budgetLoadsPerFrame = 4;
    std::uint32_t prefetchTiles = 0;
    std::uint32_t debugMode = 0;

    static constexpr std::uint32_t kMaxLayers = 8;
    static constexpr std::uint32_t kMaxPageSize = 4096;
    static constexpr std::uint32_t kMaxTileSize = 128;
    static constexpr std::uint32_t kMaxChunkTiles = 8;
    static constexpr std::uint32_t kMaxPageTableSize = 1024;
    static constexpr std::uint32_t kMaxBudgetLoads = 32;
    static constexpr std::uint32_t kMaxPrefetchTiles = 8;

    // Keeps every field inside the engine limits. Values are rounded to powers of
    // two where the format requires it, then the dependent bounds are re-applied.
    void Clamp() noexcept;

    // Human-readable layout problem (nullptr when the layout is usable).
    [[nodiscard]] const char* Issue() const noexcept;

    [[nodiscard]] std::uint32_t SlotsPerLayer() const noexcept
    {
        if (tileSize == 0 || pageSize < tileSize)
        {
            return 0;
        }
        const std::uint32_t ScopedAxis = pageSize / tileSize;
        return ScopedAxis * ScopedAxis;
    }
    [[nodiscard]] std::uint64_t BytesPerLayer() const noexcept
    {
        return static_cast<std::uint64_t>(pageSize) * pageSize * 4ull;
    }
    [[nodiscard]] std::uint64_t TotalBytes() const noexcept
    {
        return BytesPerLayer() * layers;
    }
    [[nodiscard]] std::uint64_t PageTableBytes() const noexcept
    {
        return static_cast<std::uint64_t>(pageTableSize) * pageTableSize * 4ull;
    }
    [[nodiscard]] float WorldSpan() const noexcept
    {
        return static_cast<float>(pageTableSize) * worldTileSize;
    }

    friend bool operator==(const VirtualTextureSettings&, const VirtualTextureSettings&) = default;
};

struct ProjectSettings
{
    char name[kProjectNameCapacity] = "Untitled Project";
    char version[kProjectVersionCapacity] = "0.1.0";
    char defaultScene[kProjectScenePathCapacity] = "scenes/main.toml";

    float fixedUpdateHz = 120.0f;
    std::uint32_t maxFixedSteps = 8;

    float masterVolume = 1.0f;
    float sfxVolume = 1.0f;
    float musicVolume = 1.0f;

    // Empty means the system default. The voice panel resolves a missing id to
    // the default and writes the fallback back into the project.
    char voiceInputDevice[kAudioDeviceIdChars] = {};
    char voiceOutputDevice[kAudioDeviceIdChars] = {};

    // Voice processing policy mirrored by the Voice Devices panel. 0 none,
    // 1 gate, 2 spectral (the default), 3 RNNoise when the build has it.
    std::uint8_t voiceDenoise = 2;
    float voiceGateDb = -45.0f;
    bool voiceAgc = true;
    bool voiceAec = true;
    // AEC echo-path tail and NLMS step; clamped to 8..160 ms and 0.01..1.
    float voiceAecTailMs = 128.0f;
    float voiceAecStep = 0.25f;
    float voiceAgcTargetDb = -18.0f;

    // The one accessibility profile: game defaults, the OLED orbit policy and
    // the developer-side editor preferences. The project file stores it, the
    // settings panel edits it, AccessibilityKernel::ApplyProfile applies it.
    AccessibilityProfile accessibility{};

    // Cooker target for imported textures. BC7 is the desktop default; ASTC is
    Assets::TextureFormat textureFormat = Assets::TextureFormat::Bc7Srgb;

    VirtualTextureSettings virtualTexture{};
    std::uint32_t maxTextureSize = 2048;

    // Final AOT cook policy. Persisted in [build] so the same project always
    Build::BuildProfile build{};

    // Layer matrix edited by ProjectSettingsPanel. The runtime receives it on
    CollisionFilterMatrix collision = CollisionFilterMatrix::Defaults();

    // Compiler-generated field-by-field comparison. Every member (including
    [[nodiscard]] friend bool operator==(const ProjectSettings&, const ProjectSettings&) noexcept = default;
};

// Editor-environment settings live in EditorSettings.hpp; this header keeps
// only the aggregated project descriptor.
struct ProjectConfig
{
    ProjectSettings project{};
    EditorSettings editor{};
};

static_assert(std::is_trivially_copyable_v<ProjectSettings>);
static_assert(std::is_standard_layout_v<ProjectSettings>);

// BC7 on desktop, ASTC 4x4 sRGB on the mobile targets. The project file stores
[[nodiscard]] Assets::TextureFormat DefaultTextureCookFormat() noexcept;

// Default project location
inline constexpr std::string_view kDefaultProjectsDirectoryName = "Projects";

// Absolute path of the engine root, or current_path() when nothing matches.
[[nodiscard]] std::filesystem::path ResolveEngineRoot();

// <engineRoot>/Projects, created when InCreate is true. On failure returns the
[[nodiscard]] std::filesystem::path DefaultProjectsRoot(bool InCreate, std::string* InError = nullptr);

// Resolved project layout. `CreateProject()` creates exactly these directories:
struct ProjectPaths
{
    std::filesystem::path root;
    std::filesystem::path projectFile;
    std::filesystem::path assets;
    std::filesystem::path textures;
    std::filesystem::path audio;
    std::filesystem::path scenes;
    std::filesystem::path configs;
    std::filesystem::path editorIni;
    std::filesystem::path build;
    std::filesystem::path release;
    std::filesystem::path archives;
    std::filesystem::path stores;
    std::filesystem::path steamStore;
    std::filesystem::path epicStore;
    std::filesystem::path gogStore;

    [[nodiscard]] bool IsValid() const noexcept
    {
        return !root.empty() && !projectFile.empty() && !assets.empty();
    }
};

// Builds the layout from a project root (or a path to project.femproj).
[[nodiscard]] bool ResolveProjectPaths(const std::filesystem::path& InRootOrProjectFile, ProjectPaths* OutPaths,
                                       std::string* InError = nullptr);

// Creates a clean, strictly typed project tree and a default project.femproj.
[[nodiscard]] bool CreateProject(const std::filesystem::path& InParentDirectory, std::string_view InProjectName,
                                 ProjectPaths* OutPaths = nullptr, ProjectConfig* OutConfig = nullptr,
                                 std::string* InError = nullptr);

// Reads project.femproj. Missing optional sections fall back to defaults; a
[[nodiscard]] bool LoadProject(const std::filesystem::path& InRootOrProjectFile, ProjectPaths* OutPaths,
                               ProjectConfig* OutConfig, std::string* InError = nullptr);

// In-memory owner of one project's configuration. Panels edit Project() and
class ProjectConfigStore
{
public:
    ProjectConfigStore() = default;

    [[nodiscard]] bool SetProject(const ProjectPaths& InPaths);
    [[nodiscard]] bool Load(std::string* InError = nullptr);

    [[nodiscard]] bool SaveProject(std::string* InError = nullptr) const;
    [[nodiscard]] bool SaveEditorIni(std::string* InError = nullptr) const;
    // Saves project.femproj when Project() differs from the last loaded/saved
    [[nodiscard]] bool PersistIfChanged(std::string* InError = nullptr);

    [[nodiscard]] ProjectSettings& Project() noexcept
    {
        return MConfig.project;
    }
    [[nodiscard]] const ProjectSettings& Project() const noexcept
    {
        return MConfig.project;
    }
    [[nodiscard]] EditorSettings& Editor() noexcept
    {
        return MConfig.editor;
    }
    [[nodiscard]] const EditorSettings& Editor() const noexcept
    {
        return MConfig.editor;
    }
    [[nodiscard]] ProjectConfig& Config() noexcept
    {
        return MConfig;
    }
    [[nodiscard]] const ProjectConfig& Config() const noexcept
    {
        return MConfig;
    }
    [[nodiscard]] const ProjectPaths& Paths() const noexcept
    {
        return MPaths;
    }
    [[nodiscard]] bool Loaded() const noexcept
    {
        return MLoaded;
    }
    [[nodiscard]] bool Dirty() const noexcept;

private:
    ProjectPaths MPaths{};
    ProjectConfig MConfig{};
    ProjectSettings MSavedProject{};
    EditorSettings MSavedEditor{};
    bool MLoaded = false;
};

// String helpers shared by the dashboard and the settings panel. They never
void CopyProjectString(char* InDestination, std::size_t InCapacity, std::string_view InValue) noexcept;
[[nodiscard]] std::string_view ProjectString(const char* InSource, std::size_t InCapacity) noexcept;

// Builds the default project.femproj TOML text through toml++. Public so tests
[[nodiscard]] std::string EncodeProjectToml(const ProjectConfig& InConfig);
[[nodiscard]] bool DecodeProjectToml(std::string_view InText, ProjectConfig* OutConfig, std::string* InError = nullptr);

// Writes a file beside the target and replaces it in one OS operation. Used by
[[nodiscard]] bool WriteFileAtomically(const std::filesystem::path& InPath, std::string_view InText,
                                       std::string* InError = nullptr);
}
