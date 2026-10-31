// FemboyEngine2D - editor as an ECS system.
#pragma once

#include "Engine/Assets/TextureFormat.hpp"
#include "Engine/Core/Types.hpp"
#include "Engine/ECS/SystemGraph.hpp"
#include "Engine/RHI/RHI.hpp"
#include "Engine/ECS/Scene.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace Fby
{
class VirtualFileSystem;

namespace Rhi
{
class VirtualTextureSystem;
}

namespace Net
{
class NetSession;
}

namespace Editor
{
struct VirtualTextureSettings;
}
}

namespace Fby::Editor
{

class AIStateTreeCanvasPanel;
class AnimationGraphCanvasPanel;
class AnimationTimelinePanel;
class AssetDatabaseWatchSystem;
class AssetImportService;
class CommandStack;
class ContentBrowserPanel;
class EditorSettingsPanel;
class MaterialGraphPanel;
class NetworkDiagnosticsPanel;
class ProjectConfigStore;
class ProjectDashboard;
class ProjectSettingsPanel;
struct ProjectConfig;
struct ProjectPaths;
class VisualScriptPanel;

struct EditorDesc
{
    // ImGui writes its docking/window state here. Removing the file restores
    std::filesystem::path iniPath = "editor.ini";
    // Project root that contains project.femproj and assets/. The editor starts
    // with this project when the file exists; otherwise the dashboard opens.
    std::filesystem::path projectRoot;
    // Project selected on the command line (a .femproj file or a project
    // folder). Takes precedence over projectRoot.
    std::filesystem::path initialProjectPath;
    // Ctrl+S target. Empty means "never auto-save from a shortcut".
    std::filesystem::path scenePath;
    std::string title = "FemboyEngine2D 1.0 - editor";
    bool startVisible = true;
    bool showDemoWindow = false;
    // Show the Open/New Project dashboard when no valid project is loaded.
    bool showProjectDashboard = true;
    // OS watcher + background importer. The editor still works with it off.
    bool watchAssets = true;
    // Texture array a hot-reloaded atlas layer writes into. Invalid = the
    Rhi::TextureArrayHandle textureArray{};
    Assets::TextureFormat textureArrayFormat = Assets::TextureFormat::Rgba8;
    Assets::TextureFormat defaultTextureFormat = Assets::TextureFormat::Bc7Srgb;
    float uiScale = 1.0f;
    // Sandbox executable launched as the PIE second copy (Play Multiplayer).
    // Empty keeps the local client world only, which is the headless/test path.
    std::filesystem::path pieClientExecutable;
};

struct EditorCamera
{
    Vector2D center{640.0f, 360.0f};
    float zoom = 1.0f;
    // Camera pan multiplier for the arrow keys, the drag pan and the toolbar
    // speed slider (0.25 = slow, 4 = fast).
    float panSpeed = 1.0f;
};

class EditorSystem
{
public:
    EditorSystem();
    ~EditorSystem();

    EditorSystem(const EditorSystem&) = delete;
    EditorSystem& operator=(const EditorSystem&) = delete;

    // Uses ctx.renderer and ctx.window; both must be set. Registers an SDL event
    [[nodiscard]] bool Initialize(Ecs::SystemContext& InContext, const EditorDesc& InDesc = {});
    void Shutdown();
    [[nodiscard]] bool IsInitialized() const noexcept;

    // Adds Tick as a Render-phase node. The graph is built once after every
    [[nodiscard]] bool Register(Ecs::SystemGraph& InGraph);

    // The Render-phase system body. Builds one ImGui frame and updates the
    void Tick(Ecs::SystemContext& InContext);

    // SDL_Event* from WindowManager's observer.
    void OnPlatformEvent(const void* InSdlEvent);

    [[nodiscard]] bool WantsMouse() const noexcept;
    [[nodiscard]] bool WantsKeyboard() const noexcept;

    [[nodiscard]] bool IsVisible() const noexcept;
    void SetVisible(bool InVisible);

    [[nodiscard]] Rhi::RenderTargetHandle ViewportTarget() const noexcept;
    [[nodiscard]] Rhi::Extent2D ViewportSize() const noexcept;
    [[nodiscard]] EditorCamera& Camera() noexcept;
    [[nodiscard]] const EditorCamera& Camera() const noexcept;
    [[nodiscard]] Scene::ComponentRegistry& Components() noexcept;

    // Authoring services. Both live for the lifetime of the editor instance;
    [[nodiscard]] AnimationTimelinePanel& TimelinePanel() noexcept;
    [[nodiscard]] CommandStack& Commands() noexcept;
    [[nodiscard]] ContentBrowserPanel& ContentBrowser() noexcept;
    // Step 2 visual editors. The host may bind MaterialGraph().BindRenderItem()
    [[nodiscard]] MaterialGraphPanel& MaterialGraph() noexcept;
    [[nodiscard]] VisualScriptPanel& VisualScript() noexcept;
    // AI State Tree authoring canvas (ImNodes) + its flat StateTreeDefinition
    [[nodiscard]] AIStateTreeCanvasPanel& AIStateTree() noexcept;
    [[nodiscard]] AnimationGraphCanvasPanel& AnimationGraph() noexcept;
    // PIE world seam shared by both ImNodes debug canvases.
    void BindAIPlayInEditor(const Ecs::Registry* InPlayWorld, bool InActive) noexcept;
    // The registry the viewport/render extract should sample. When PIE is
    [[nodiscard]] Ecs::Registry* ActiveWorld() noexcept;
    [[nodiscard]] const Ecs::Registry* ActiveWorld() const noexcept;
    // PIE client viewport: the second offscreen target that presents
    // PlayInEditor's replicated client world during Play (Multiplayer). The
    // target is invalid and the camera is unused outside such a session.
    [[nodiscard]] Rhi::RenderTargetHandle PieClientTarget() const noexcept;
    [[nodiscard]] Rhi::Extent2D PieClientSize() const noexcept;
    [[nodiscard]] EditorCamera& PieClientCamera() noexcept;
    [[nodiscard]] Ecs::Registry* PieClientWorld() noexcept;
    [[nodiscard]] bool IsPlaying() const noexcept;
    [[nodiscard]] bool IsPaused() const noexcept;
    [[nodiscard]] ProjectDashboard& Dashboard() noexcept;
    [[nodiscard]] ProjectConfigStore& Config() noexcept;
    [[nodiscard]] ProjectSettingsPanel& ProjectSettings() noexcept;
    [[nodiscard]] EditorSettingsPanel& EditorSettings() noexcept;
    [[nodiscard]] AssetImportService& Importer() noexcept;
    [[nodiscard]] AssetDatabaseWatchSystem& Watcher() noexcept;
    [[nodiscard]] bool ProjectOpen() const noexcept;
    // Set by the Project Dashboard's Quit action; the host checks it in its
    [[nodiscard]] bool ShouldQuit() const noexcept;

    // Asks the editor to quit. A dirty scene opens the save/discard/cancel
    // prompt first; a clean scene or the dashboard quits immediately.
    void RequestQuit();

    // Applies an already validated project: mounts its assets folder, loads the
    [[nodiscard]] bool OpenProject(Ecs::SystemContext& InContext, const ProjectPaths& InPaths,
                                   const ProjectConfig& InConfig, std::string* InError = nullptr,
                                   bool InLoadDefaultScene = false);

    // Appends a line to the editor console. Useful for gameplay/plugin messages
    void PrintLine(std::string_view InLine);

    // Scene IO. The editor's component table is used; custom components can be
    [[nodiscard]] bool LoadScene(Ecs::SystemContext& InContext, const std::filesystem::path& InPath,
                                 std::string* InError = nullptr);
    [[nodiscard]] bool SaveScene(Ecs::SystemContext& InContext, const std::filesystem::path& InPath,
                                 std::string* InError = nullptr);
    [[nodiscard]] const std::filesystem::path& CurrentScenePath() const noexcept;

    // Virtual-texture debug panel source (owned by the host application). Passing
    // nullptr keeps the panel in its opt-in "inactive" state.
    void SetVirtualTextureSource(Rhi::VirtualTextureSystem* InSystem) noexcept;

    // Binds the live session shown by the Network Diagnostics panel. Passing
    // null keeps the panel in its explicit inactive state.
    void SetNetworkSession(Net::NetSession* InSession) noexcept;
    void SetVirtualTextureDebugMode(std::uint32_t InMode) noexcept;
    [[nodiscard]] std::uint32_t VirtualTextureDebugMode() const noexcept;
    // Project-level VT layout and runtime defaults (see ProjectConfig.hpp).
    [[nodiscard]] const VirtualTextureSettings& VirtualTextureConfig() const noexcept;

    // Rebuilds runtime TilemapComponent grids from their authored
    // TilemapAssetRefComponent references. Called after a scene load and after
    // the PIE clone is created.
    void ResolveTilemapRefs(Ecs::Registry& InRegistry, VirtualFileSystem* InVfs) noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> MImpl;
};
}
