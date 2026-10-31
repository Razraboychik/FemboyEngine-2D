// FemboyEngine2D - Content Browser (asset explorer + drag-and-drop spawn).
#pragma once

#include "Engine/Assets/AssetDatabase.hpp"
#include "Engine/Core/Types.hpp"
#include "Engine/ECS/Components.hpp"
#include "Engine/ECS/Schedule.hpp"
#include "Engine/ECS/ComponentRegistry.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace Fby
{
class VirtualFileSystem;
}

namespace Fby::Editor
{

class AssetImportService;
class CommandStack;

class ContentBrowserPanel
{
public:
    static constexpr std::uint32_t kMaxEntries = 1024;
    static constexpr std::uint32_t kMaxEntryPath = 192;
    static constexpr std::uint32_t kMaxEntryName = 96;
    static constexpr const char* kDragPayloadType = "FBY_CONTENT_ASSET";

    struct Config
    {
        std::filesystem::path assetsRoot;
        bool showCookedArtifacts = false;
        bool autoRefresh = true;
        float refreshIntervalSeconds = 3.0f;
    };

    struct Entry
    {
        char name[kMaxEntryName]{};
        char path[kMaxEntryPath]{};
        std::int16_t parent = -1;
        std::int16_t firstChild = -1;
        std::int16_t nextSibling = -1;
        bool directory = false;
        bool hasMeta = false;
        AssetId id{};
        Assets::AssetGuid guid = 0;
        AssetKind kind = AssetKind::Unknown;
        std::uint32_t size = 0;
        std::uint32_t generation = 0;
    };

    // Plain-old-data payload copied through ImGui's drag-and-drop storage. It is
    struct DragPayload
    {
        AssetId id{};
        Assets::AssetGuid guid = 0;
        AssetKind kind = AssetKind::Unknown;
        char path[kMaxEntryPath]{};
    };

    struct SpawnContext
    {
        CommandStack* commands = nullptr;
        Ecs::Registry* registry = nullptr;
        const Scene::ComponentRegistry* components = nullptr;
        VirtualFileSystem* assets = nullptr;
        Assets::AssetDatabase* database = nullptr;
        AssetImportService* importer = nullptr;
        Vector2D dropPosition{};
        std::uint32_t fallbackTextureLayer = 0;
    };

    ContentBrowserPanel() = default;

    void Initialize(Assets::AssetDatabase* InDatabase, const Config& InConfig,
                    AssetImportService* InImporter = nullptr);
    void SetProjectRoot(const std::filesystem::path& InAssetsRoot);
    void SetAutoRefresh(bool InEnabled) noexcept
    {
        MConfig.autoRefresh = InEnabled;
    }

    // Rebuilds the fixed entry table from disk and joins it with the database.
    void Refresh();

    // Draws the panel. `open` is the window's close flag, `status` receives an
    void Draw(Ecs::Registry& InRegistry, const Scene::ComponentRegistry& InComponents, CommandStack& InCommands,
              VirtualFileSystem* InAssets, bool* InOpen, std::string* InStatus);

    // Call immediately after ImGui::Image() in the Viewport. Returns true when a
    [[nodiscard]] bool AcceptViewportDrop(SpawnContext& InContext, std::string* InError = nullptr);

    // Test/tool seam: performs the same spawn without an ImGui context.
    [[nodiscard]] bool SpawnAsset(const DragPayload& InPayload, SpawnContext& InContext,
                                  std::string* InError = nullptr);

    [[nodiscard]] std::uint32_t Count() const noexcept
    {
        return MEntryCount;
    }
    [[nodiscard]] const Entry* Entries() const noexcept
    {
        return MEntries.data();
    }
    [[nodiscard]] const Entry* Selected() const noexcept;
    [[nodiscard]] const Config& GetConfig() const noexcept
    {
        return MConfig;
    }
    [[nodiscard]] bool NeedsRefresh() const noexcept
    {
        return MRefreshRequested;
    }
    void RequestRefresh() noexcept
    {
        MRefreshRequested = true;
    }
    // A scene the user asked to open (double-click); the editor consumes it after
    // drawing the panel so the load happens outside this panel's window.
    [[nodiscard]] std::string TakeOpenRequest();

private:
    void AddEntry(std::string_view InPath, std::string_view InName, std::int16_t InParent, bool InDirectory);
    [[nodiscard]] std::int16_t EnsureDirectory(std::string_view InRelativeDirectory);
    [[nodiscard]] std::int16_t FindEntry(std::string_view InPath) const noexcept;
    [[nodiscard]] std::int16_t AppendChild(std::int16_t InParent, std::int16_t InChild) noexcept;
    void DrawEntry(std::int16_t InIndex, Ecs::Registry& InRegistry, const Scene::ComponentRegistry& InComponents,
                   CommandStack& InCommands, VirtualFileSystem* InAssets, std::string* InStatus);
    void DrawDetails(Ecs::Registry& InRegistry, const Scene::ComponentRegistry& InComponents, CommandStack& InCommands,
                     VirtualFileSystem* InAssets, std::string* InStatus);
    void DrawDragSource(std::int16_t InIndex);

    enum class FileDialog : std::uint8_t
    {
        None = 0,
        NewFolder,
        NewScene,
        Rename,
    };
    [[nodiscard]] std::filesystem::path PhysicalPath(std::string_view InRelative) const;
    static bool IsSafeFileName(std::string_view InName);
    void DrawFileMenu(VirtualFileSystem* InAssets, std::string* InStatus);
    void ApplyFileDialog(VirtualFileSystem* InAssets, std::string* InStatus);
    void DuplicateSelectedFile(VirtualFileSystem* InAssets, std::string* InStatus);
    void DeleteTargetFile(VirtualFileSystem* InAssets, std::string* InStatus);

    Assets::AssetDatabase* MDatabase = nullptr;
    AssetImportService* MImporter = nullptr;
    Config MConfig{};
    std::array<Entry, kMaxEntries> MEntries{};
    std::uint32_t MEntryCount = 0;
    std::int16_t MSelected = -1;
    bool MRefreshRequested = true;
    float MLastRefreshTime = -1000.0f;
    FileDialog MFileDialog = FileDialog::None;
    char MFileName[128] = {};
    bool MFileDialogOpen = false;
    bool MFileDialogFocus = false;
    bool MDeleteOpen = false;
    std::string MDeleteTarget;
    std::string MOpenRequest;
};
}
