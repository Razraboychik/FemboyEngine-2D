// FemboyEngine2D - editor Step 1 infrastructure tests.
#include "Engine/Assets/AssetDatabase.hpp"
#include "Engine/Assets/TextureFormat.hpp"
#include "Engine/Assets/VFS.hpp"
#include "Engine/Core/WindowManager.hpp"
#include "Editor/Infrastructure/AssetDatabaseWatchSystem.hpp"
#include "Editor/Infrastructure/AssetImport.hpp"
#include "Editor/Infrastructure/AssetImportService.hpp"
#include "Editor/Infrastructure/CommandStack.hpp"
#include "Editor/Panels/ContentBrowserPanel.hpp"
#include "Editor/Infrastructure/EditTransactions.hpp"
#include "Editor/Infrastructure/ProjectConfig.hpp"
#include "Editor/Panels/VirtualTexturePanel.hpp"
#include "Engine/ECS/ComponentRegistry.hpp"
#include "Engine/ECS/RegistryOps.hpp"
#include "Engine/ECS/Scene.hpp"

#include "Engine/ECS/Components.hpp"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace
{
int GFailures = 0;

void Check(bool InCondition, const char* InMessage)
{
    if (!InCondition)
    {
        std::printf("FAIL: %s\n", InMessage);
        ++GFailures;
    }
}

[[nodiscard]] std::filesystem::path TestRoot(std::string_view InName)
{
    std::filesystem::path ScopedBase = std::filesystem::current_path();
    if (ScopedBase.filename() != "build")
    {
        ScopedBase /= "build";
    }
    return ScopedBase / std::string(InName);
}

[[nodiscard]] bool WriteText(const std::filesystem::path& InPath, std::string_view InText)
{
    std::error_code ScopedCode;
    std::filesystem::create_directories(InPath.parent_path(), ScopedCode);
    std::ofstream ScopedStream(InPath, std::ios::binary | std::ios::trunc);
    ScopedStream.write(InText.data(), static_cast<std::streamsize>(InText.size()));
    return static_cast<bool>(ScopedStream);
}

[[nodiscard]] bool WritePng(const std::filesystem::path& InPath, int InWidth, int InHeight, int InBlue = 160)
{
    std::vector<unsigned char> ScopedPixels(static_cast<std::size_t>(InWidth * InHeight * 4));
    for (int ScopedY = 0; ScopedY < InHeight; ++ScopedY)
    {
        for (int ScopedX = 0; ScopedX < InWidth; ++ScopedX)
        {
            const std::size_t ScopedIndex = static_cast<std::size_t>((ScopedY * InWidth + ScopedX) * 4);
            ScopedPixels[ScopedIndex + 0] = static_cast<unsigned char>(ScopedX * 40);
            ScopedPixels[ScopedIndex + 1] = static_cast<unsigned char>(ScopedY * 40);
            ScopedPixels[ScopedIndex + 2] = static_cast<unsigned char>(std::clamp(InBlue, 0, 255));
            ScopedPixels[ScopedIndex + 3] = 255;
        }
    }
    std::error_code ScopedCode;
    std::filesystem::create_directories(InPath.parent_path(), ScopedCode);
    return stbi_write_png(InPath.string().c_str(), InWidth, InHeight, 4, ScopedPixels.data(), InWidth * 4) != 0;
}

void TestProjectCreationAndConfig()
{
    const std::filesystem::path ScopedRoot = TestRoot("_editor_infra_project");
    std::error_code ScopedCode;
    std::filesystem::remove_all(ScopedRoot, ScopedCode);

    Fby::Editor::ProjectPaths ScopedPaths;
    Fby::Editor::ProjectConfig ScopedConfig;
    Check(Fby::Editor::CreateProject(ScopedRoot, "DemoProject", &ScopedPaths, &ScopedConfig), "create project");
    Check(std::filesystem::is_directory(ScopedPaths.assets), "assets directory exists");
    Check(std::filesystem::is_directory(ScopedPaths.textures), "assets/textures exists");
    Check(std::filesystem::is_directory(ScopedPaths.audio), "assets/audio exists");
    Check(std::filesystem::is_directory(ScopedPaths.scenes), "assets/scenes exists");
    Check(std::filesystem::is_directory(ScopedPaths.configs), "configs exists");
    Check(std::filesystem::is_directory(ScopedPaths.build), "build exists");
    Check(std::filesystem::is_directory(ScopedPaths.release), "build/release exists");
    Check(std::filesystem::is_directory(ScopedPaths.archives), "archives exists");
    Check(std::filesystem::is_directory(ScopedPaths.steamStore), "stores/steam exists");
    Check(std::filesystem::is_directory(ScopedPaths.epicStore), "stores/epic exists");
    Check(std::filesystem::is_directory(ScopedPaths.gogStore), "stores/gog exists");
    Check(std::filesystem::is_regular_file(ScopedPaths.projectFile), "project.femproj exists");
    Check(std::filesystem::is_regular_file(ScopedPaths.root / ".gitignore"), "project .gitignore exists");
    Check(std::filesystem::is_regular_file(ScopedPaths.scenes / "main.toml"), "empty main scene exists");
    // The dashboard must produce a project that builds without manual
    const std::filesystem::path ScopedCMake = ScopedPaths.root / "CMakeLists.txt";
    const std::filesystem::path ScopedGameMain = ScopedPaths.root / "GameMain.cpp";
    Check(std::filesystem::is_regular_file(ScopedCMake), "generated CMakeLists.txt exists");
    Check(std::filesystem::is_regular_file(ScopedGameMain), "generated GameMain.cpp exists");
    {
        std::ifstream ScopedStream(ScopedCMake);
        const std::string ScopedText((std::istreambuf_iterator<char>(ScopedStream)), std::istreambuf_iterator<char>());
        Check(ScopedText.find("FemboyEngine2D::Engine") != std::string::npos,
              "generated CMakeLists links FemboyEngine2D::Engine");
        Check(ScopedText.find("GameMain.cpp") != std::string::npos, "generated CMakeLists compiles GameMain.cpp");
    }
    {
        std::ifstream ScopedStream(ScopedGameMain);
        const std::string ScopedText((std::istreambuf_iterator<char>(ScopedStream)), std::istreambuf_iterator<char>());
        Check(ScopedText.find("DemoProject") != std::string::npos, "generated GameMain carries the project name");
        Check(ScopedText.find("SystemGraph") != std::string::npos, "generated GameMain owns a SystemGraph");
    }

    entt::registry ScopedSceneRegistry;
    Fby::Scene::ComponentRegistry ScopedSceneComponents;
    Fby::Scene::RegisterBuiltInComponents(ScopedSceneComponents);
    const auto ScopedEmptyScene =
        Fby::Scene::LoadSceneFileToml(ScopedSceneRegistry, ScopedSceneComponents, ScopedPaths.scenes / "main.toml");
    Check(ScopedEmptyScene.has_value(), "generated main scene is valid TOML");

    // A scene reload replaces the world. EnTT's registry::clear() only clears
    // component pools, so a loader that relied on it kept component-less ghost
    // entities from the previous world in the hierarchy.
    {
        for (int ScopedIndex = 0; ScopedIndex < 3; ++ScopedIndex)
        {
            const Fby::Entity ScopedGhost = ScopedSceneRegistry.create();
            ScopedSceneRegistry.emplace<Fby::Transform2D>(ScopedGhost);
        }
        Check(ScopedSceneRegistry.storage<Fby::Entity>().size() == 3, "registry prepared with three entities");
        const auto ScopedReload =
            Fby::Scene::LoadSceneFileToml(ScopedSceneRegistry, ScopedSceneComponents, ScopedPaths.scenes / "main.toml");
        Check(ScopedReload.has_value(), "an empty scene reload succeeds");
        Check(ScopedSceneRegistry.storage<Fby::Entity>().size() == 0, "scene reload removes the previous world");

        Fby::Entity ScopedRemaining = ScopedSceneRegistry.create();
        ScopedSceneRegistry.emplace<Fby::Transform2D>(ScopedRemaining);
        Fby::Scene::DestroyAllEntities(ScopedSceneRegistry);
        Check(ScopedSceneRegistry.storage<Fby::Entity>().size() == 0, "DestroyAllEntities empties the registry");
    }

    // Typography migration: an untouched legacy 16 px block moves to the larger
    // defaults, while any custom size keeps the whole authored block.
    {
        const std::filesystem::path ScopedLegacyRoot = TestRoot("_editor_infra_typography");
        std::filesystem::remove_all(ScopedLegacyRoot, ScopedCode);
        const std::string_view ScopedLegacyProject =
            "format_version = 4\n"
            "[project]\n"
            "name = \"Legacy\"\n"
            "default_scene = \"scenes/main.toml\"\n"
            "[editor]\n"
            "font_panel_title = 20.0\n"
            "font_panel_body = 16.0\n"
            "font_tooltip = 16.0\n"
            "font_script_node_title = 18.0\n"
            "font_script_pin_input = 16.0\n"
            "font_script_pin_output = 16.0\n"
            "font_scene_text = 16.0\n";
        Check(WriteText(ScopedLegacyRoot / "project.femproj", ScopedLegacyProject), "write legacy project");
        Fby::Editor::ProjectConfig ScopedLegacy;
        Check(Fby::Editor::LoadProject(ScopedLegacyRoot, nullptr, &ScopedLegacy), "load legacy project");
        Check(ScopedLegacy.editor.fontPanelBody == 18.0f, "legacy body migrates to 18 px");
        Check(ScopedLegacy.editor.fontPanelTitle == 24.0f, "legacy title migrates to 24 px");

        const std::string_view ScopedCustomProject =
            "format_version = 4\n"
            "[project]\n"
            "name = \"Custom\"\n"
            "default_scene = \"scenes/main.toml\"\n"
            "[editor]\n"
            "font_panel_title = 20.0\n"
            "font_panel_body = 22.0\n"
            "font_tooltip = 16.0\n"
            "font_script_node_title = 18.0\n"
            "font_script_pin_input = 16.0\n"
            "font_script_pin_output = 16.0\n"
            "font_scene_text = 16.0\n";
        Check(WriteText(ScopedLegacyRoot / "project.femproj", ScopedCustomProject), "write custom project");
        Fby::Editor::ProjectConfig ScopedCustom;
        Check(Fby::Editor::LoadProject(ScopedLegacyRoot, nullptr, &ScopedCustom), "load custom project");
        Check(ScopedCustom.editor.fontPanelBody == 22.0f, "custom body size is kept");
        Check(ScopedCustom.editor.fontPanelTitle == 20.0f, "custom title size is kept");
    }

    // Refuse traversal and a duplicate project file.
    Check(!Fby::Editor::CreateProject(ScopedRoot, "../escape"), "project traversal is rejected");
    Check(!Fby::Editor::CreateProject(ScopedRoot, "DemoProject"), "duplicate project is rejected");

    Fby::Editor::ProjectPaths ScopedLoadedPaths;
    Fby::Editor::ProjectConfig ScopedLoaded;
    Check(Fby::Editor::LoadProject(ScopedPaths.root, &ScopedLoadedPaths, &ScopedLoaded), "load project");
    Check(std::strcmp(ScopedLoaded.project.name, "DemoProject") == 0, "project name round-trips");
    Check(std::strcmp(ScopedLoaded.project.defaultScene, "scenes/main.toml") == 0, "default scene round-trips");
    Check(ScopedLoaded.project.textureFormat == Fby::Editor::DefaultTextureCookFormat(), "cook format round-trips");

    Fby::Editor::ProjectConfigStore ScopedStore;
    Check(ScopedStore.SetProject(ScopedPaths), "adopt project paths");
    Check(ScopedStore.Load(), "load into config store");
    Fby::AccessibilitySetFlag(&ScopedStore.Project().accessibility.runtime.flags, Fby::AccessibilityBlindMode, true);
    Fby::AccessibilitySetFlag(&ScopedStore.Project().accessibility.runtime.flags, Fby::AccessibilityDeafMode, true);
    ScopedStore.Project().accessibility.runtime.showSubtitles = true;
    ScopedStore.Project().accessibility.presentation.oledPixelShift = false;
    Fby::AccessibilitySetFlag(&ScopedStore.Project().accessibility.runtime.flags, Fby::AccessibilityFlashGuard, false);
    ScopedStore.Project().accessibility.runtime.colorVision = Fby::ColorVisionDeficiency::Tritanopia;
    ScopedStore.Project().accessibility.runtime.colorVisionStrength = 0.5f;
    ScopedStore.Project().accessibility.presentation.motionScale = 0.25f;
    ScopedStore.Project().accessibility.presentation.hudFadeAfterIdleSeconds = 30.0f;
    ScopedStore.Project().accessibility.presentation.minimumContrastRatio = 7.0f;
    ScopedStore.Project().accessibility.editor.highContrast = true;
    ScopedStore.Project().fixedUpdateHz = 90.0f;
    ScopedStore.Editor().vsync = false;
    ScopedStore.Editor().windowMode = 2;
    ScopedStore.Editor().windowWidth = 1920;
    ScopedStore.Editor().windowHeight = 1080;
    Check(ScopedStore.PersistIfChanged(), "persist changed settings");
    Check(std::filesystem::is_regular_file(ScopedPaths.editorIni), "editor.ini mirrored");

    Fby::Editor::ProjectConfig ScopedReloaded;
    Check(Fby::Editor::LoadProject(ScopedPaths.root, nullptr, &ScopedReloaded), "reload persisted project");
    const Fby::AccessibilityProfile& ScopedProfile = ScopedReloaded.project.accessibility;
    Check(Fby::AccessibilityHasFlag(ScopedProfile.runtime.flags, Fby::AccessibilityBlindMode), "BlindMode persists in TOML");
    Check(Fby::AccessibilityHasFlag(ScopedProfile.runtime.flags, Fby::AccessibilityDeafMode), "DeafMode persists in TOML");
    Check(ScopedProfile.runtime.showSubtitles, "ShowSubtitles persists in TOML");
    Check(!ScopedProfile.presentation.oledPixelShift, "OLED shifter persists in TOML");
    Check(!Fby::AccessibilityHasFlag(ScopedProfile.runtime.flags, Fby::AccessibilityFlashGuard),
          "FlashGuard persists in TOML");
    Check(ScopedProfile.runtime.colorVision == Fby::ColorVisionDeficiency::Tritanopia,
          "colour vision persists in TOML");
    Check(ScopedProfile.runtime.colorVisionStrength >= 0.49f && ScopedProfile.runtime.colorVisionStrength <= 0.51f,
          "colour vision strength persists in TOML");
    Check(ScopedProfile.presentation.motionScale >= 0.24f && ScopedProfile.presentation.motionScale <= 0.26f,
          "motion scale persists in TOML");
    Check(ScopedProfile.presentation.hudFadeAfterIdleSeconds >= 29.0f &&
              ScopedProfile.presentation.hudFadeAfterIdleSeconds <= 31.0f,
          "HUD fade persists in TOML");
    Check(ScopedProfile.presentation.minimumContrastRatio >= 6.9f &&
              ScopedProfile.presentation.minimumContrastRatio <= 7.1f,
          "minimum contrast persists in TOML");
    Check(ScopedProfile.editor.highContrast, "editor high contrast persists in TOML");
    Check(ScopedReloaded.project.fixedUpdateHz == 90.0f, "FixedUpdate persists in TOML");
    Check(!ScopedReloaded.editor.vsync, "VSync persists in TOML");
    Check(ScopedReloaded.editor.windowMode == 2, "window mode persists in TOML");
    Check(ScopedReloaded.editor.windowWidth == 1920, "window width persists in TOML");
    Check(ScopedReloaded.editor.windowHeight == 1080, "window height persists in TOML");
    Check(std::strcmp(Fby::WindowModeName(Fby::WindowMode::BorderlessFullscreen), "borderless fullscreen") == 0,
          "borderless mode has a stable name");
    Check(Fby::WindowModeName(Fby::WindowMode::ExclusiveFullscreen) != nullptr, "exclusive mode has a name");

    std::ifstream ScopedIni(ScopedPaths.editorIni, std::ios::binary);
    const std::string ScopedIniText((std::istreambuf_iterator<char>(ScopedIni)), std::istreambuf_iterator<char>());
    Check(ScopedIniText.find("[FemboyEditor]") != std::string::npos, "editor.ini has the settings section");
    Check(ScopedIniText.find("vsync = false") != std::string::npos, "editor.ini mirrors VSync");

    // Defaulted EditorSettings::operator== compares fields, not padding. A
    Fby::Editor::EditorSettings ScopedLeft{};
    Fby::Editor::EditorSettings ScopedRight{};
    Check(ScopedLeft == ScopedRight, "default EditorSettings images compare equal");
    Fby::Editor::ProjectSettings ScopedProfileLeft{};
    Fby::Editor::ProjectSettings ScopedProfileRight{};
    ScopedProfileRight.accessibility.editor.blindMode = true;
    Check(!(ScopedProfileLeft == ScopedProfileRight),
          "the editor accessibility scope participates in ProjectSettings equality");
    ScopedRight.theme[0] = 'x';
    Check(!(ScopedLeft == ScopedRight), "theme[0] participates in EditorSettings equality");
    ScopedRight = ScopedLeft;
    Check(ScopedLeft == ScopedRight, "copy-restored EditorSettings compares equal");

    Fby::Editor::ProjectConfig ScopedVtConfig;
    ScopedVtConfig.project.virtualTexture.enabled = true;
    ScopedVtConfig.project.virtualTexture.layers = 4;
    ScopedVtConfig.project.virtualTexture.pageSize = 1024;
    ScopedVtConfig.project.virtualTexture.tileSize = 64;
    ScopedVtConfig.project.virtualTexture.chunkTiles = 2;
    ScopedVtConfig.project.virtualTexture.pageTableSize = 128;
    ScopedVtConfig.project.virtualTexture.worldTileSize = 512.0f;
    ScopedVtConfig.project.virtualTexture.budgetLoadsPerFrame = 12;
    ScopedVtConfig.project.virtualTexture.prefetchTiles = 2;
    ScopedVtConfig.project.virtualTexture.debugMode = 1;
    const std::string ScopedVtToml = Fby::Editor::EncodeProjectToml(ScopedVtConfig);
    Check(ScopedVtToml.find("[virtual_texture]") != std::string::npos, "project TOML carries the VT table");
    Fby::Editor::ProjectConfig ScopedVtDecoded;
    std::string ScopedVtError;
    Check(Fby::Editor::DecodeProjectToml(ScopedVtToml, &ScopedVtDecoded, &ScopedVtError), "VT TOML decodes");
    Check(ScopedVtDecoded.project.virtualTexture == ScopedVtConfig.project.virtualTexture,
          "VT settings roundtrip through TOML");

    Fby::Editor::VirtualTextureSettings ScopedLayout;
    ScopedLayout.pageSize = 1024;
    ScopedLayout.tileSize = 64;
    ScopedLayout.layers = 2;
    ScopedLayout.pageTableSize = 128;
    ScopedLayout.worldTileSize = 512.0f;
    Check(ScopedLayout.SlotsPerLayer() == 256, "VT slots per layer follow page/tile size");
    Check(ScopedLayout.BytesPerLayer() == 4ull * 1024ull * 1024ull, "VT page layer is 4 MiB");
    Check(ScopedLayout.TotalBytes() == 8ull * 1024ull * 1024ull, "VT total is layers * page bytes");
    Check(ScopedLayout.PageTableBytes() == 128ull * 128ull * 4ull, "VT page table is 64 KiB");
    Check(ScopedLayout.Issue() == nullptr, "default VT layout is valid");

    ScopedLayout.layers = 99;
    ScopedLayout.pageSize = 1000;
    ScopedLayout.tileSize = 4096;
    ScopedLayout.chunkTiles = 3;
    ScopedLayout.pageTableSize = 5000;
    ScopedLayout.budgetLoadsPerFrame = 999;
    ScopedLayout.prefetchTiles = 99;
    ScopedLayout.debugMode = 9;
    ScopedLayout.Clamp();
    Check(ScopedLayout.layers == 8, "VT layers clamp to 8");
    Check(ScopedLayout.pageSize == 1024, "VT page size rounds to the next power of two");
    Check(ScopedLayout.tileSize == 128, "VT tile size clamps to 128");
    Check(ScopedLayout.chunkTiles == 4, "VT chunk tiles round to a power of two");
    Check(ScopedLayout.pageTableSize == 1024, "VT table size clamps to 1024");
    Check(ScopedLayout.budgetLoadsPerFrame == 32, "VT budget clamps to 32");
    Check(ScopedLayout.prefetchTiles == 8, "VT prefetch clamps to 8");
    Check(ScopedLayout.debugMode == 2, "VT debug mode clamps to slot tint");
    Check(ScopedLayout.Issue() == nullptr, "clamped VT layout is valid");

    Fby::Editor::VirtualTexturePanel ScopedVtPanel;
    Check(ScopedVtPanel.DebugMode() == 0, "VT debug view defaults to off");
    ScopedVtPanel.SetDebugMode(9);
    Check(ScopedVtPanel.DebugMode() == 2, "VT debug mode clamps to slot tint");
    ScopedVtPanel.SetDebugMode(1);
    Check(ScopedVtPanel.DebugMode() == 1, "VT debug mode stores page id tint");

    Fby::Editor::ProjectSettings ScopedProjectLeft{};
    Fby::Editor::ProjectSettings ScopedProjectRight{};
    Check(ScopedProjectLeft == ScopedProjectRight, "default ProjectSettings compare equal");
    ScopedProjectRight.collision.masks[0] ^= 1ull;
    Check(!(ScopedProjectLeft == ScopedProjectRight), "CollisionFilterMatrix participates in equality");
    ScopedProjectRight.collision = ScopedProjectLeft.collision;
    Check(ScopedProjectLeft == ScopedProjectRight, "restored collision matrix compares equal");
    ScopedProjectRight.build.unityBuild = false;
    Check(!(ScopedProjectLeft == ScopedProjectRight), "BuildProfile participates in equality");
}

void TestAutoImportAndBackgroundCook()
{
    const std::filesystem::path ScopedRoot = TestRoot("_editor_infra_assets");
    std::error_code ScopedCode;
    std::filesystem::remove_all(ScopedRoot, ScopedCode);
    std::filesystem::create_directories(ScopedRoot / "textures", ScopedCode);
    Check(WritePng(ScopedRoot / "textures" / "hero.png", 8, 8), "write source png");
    Check(WritePng(ScopedRoot / "textures" / "villain.png", 8, 8), "write a second source png");

    Fby::Assets::AssetDatabase ScopedDatabase;
    Fby::VirtualFileSystem ScopedVfs;
    std::string ScopedError;
    Check(ScopedVfs.MountDirectory(ScopedRoot.string(), 10, &ScopedError), "mount asset root");

    Fby::Editor::AssetImportService ScopedImporter;
    Fby::Editor::AssetImportService::Config ScopedImportConfig;
    ScopedImportConfig.assetsRoot = ScopedRoot;
    ScopedImportConfig.textureFormat = Fby::Assets::TextureFormat::Bc7Srgb;
    ScopedImportConfig.background = true;
    ScopedImportConfig.workerCount = 2;
    Check(ScopedImporter.Initialize(ScopedDatabase, ScopedVfs, nullptr, ScopedImportConfig, &ScopedError),
          "initialize importer");

    Fby::Editor::AssetDatabaseWatchSystem ScopedWatcher;
    Fby::Editor::AssetDatabaseWatchSystem::Config ScopedWatchConfig;
    ScopedWatchConfig.projectRoot = ScopedRoot;
    ScopedWatchConfig.autoImport = true;
    ScopedWatchConfig.defaultTextureFormat = Fby::Assets::TextureFormat::Bc7Srgb;
    Fby::Editor::AssetDatabaseWatchSystem::Callbacks ScopedCallbacks;
    ScopedCallbacks.user = &ScopedImporter;
    ScopedCallbacks.scheduleCook = &Fby::Editor::AssetImportService::ScheduleCookThunk;
    Check(ScopedWatcher.Initialize(ScopedDatabase, ScopedWatchConfig, ScopedCallbacks, &ScopedError),
          "initialize watcher");
#ifdef _WIN32
    // A missing project root must surface as an initialization failure instead
    // of a watcher that silently never sees a change.
    {
        Fby::Editor::AssetDatabaseWatchSystem ScopedBadWatcher;
        Fby::Editor::AssetDatabaseWatchSystem::Config ScopedBadConfig;
        ScopedBadConfig.projectRoot = ScopedRoot / "missing_root";
        std::string ScopedBadError;
        Check(!ScopedBadWatcher.Initialize(ScopedDatabase, ScopedBadConfig, ScopedCallbacks, &ScopedBadError),
              "watcher reports a missing project root");
    }
#endif
    const std::uint32_t ScopedImported = ScopedWatcher.ScanForNewSources(&ScopedError);
    Check(ScopedImported == 2, "startup scan imports both new PNGs");

    const Fby::Assets::AssetHandle ScopedHandle = ScopedDatabase.FindHandleBySourcePath("textures/hero.png");
    Check(ScopedHandle.IsValid(), "auto-import registered a handle");
    Check(std::filesystem::is_regular_file(ScopedRoot / "textures" / "hero.png.femeta"), "auto-import wrote .femeta");
    const Fby::Assets::AssetMeta* ScopedMeta = ScopedDatabase.Meta(ScopedHandle);
    Check(ScopedMeta != nullptr, "auto-import metadata is live");
    if (ScopedMeta != nullptr)
    {
        Check(ScopedMeta->kind == Fby::AssetKind::Texture, "auto-import type is texture");
        Check(ScopedMeta->id == Fby::AssetId::FromPath("textures/hero.png", Fby::AssetKind::Texture),
              "AssetId is deterministic");
        Check(ScopedMeta->guid == Fby::Assets::ComputeAssetGuid("textures/hero.png", Fby::AssetKind::Texture),
              "GUID is deterministic");
        Check(ScopedMeta->import.textureFormat == static_cast<std::uint32_t>(Fby::Assets::TextureFormat::Bc7Srgb),
              "default cook format is BC7 sRGB");
    }

    // The watcher's scheduleCook thunk queued a background job. Wait for the
    std::uint32_t ScopedCommitted = 0;
    for (int ScopedAttempt = 0; ScopedAttempt < 600 && ScopedCommitted < 2; ++ScopedAttempt)
    {
        ScopedCommitted += ScopedImporter.Tick(&ScopedError);
        // Sleep until both cooks are in: a second worker can finish after the
        // first one, and busy-spinning the tick loop can outrun it.
        if (ScopedCommitted < 2)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    Check(ScopedCommitted >= 2, "both cook workers committed a texture");
    Check(std::filesystem::is_regular_file(ScopedRoot / "textures" / "villain.ftex"),
          "the second worker wrote its .ftex");
    Check(std::filesystem::is_regular_file(ScopedRoot / "textures" / "hero.ftex"),
          "cooker wrote .ftex beside the source");
    const Fby::Assets::AssetMeta* ScopedCooked = ScopedDatabase.Meta(ScopedHandle);
    Check(ScopedCooked != nullptr && ScopedCooked->contentGeneration > 0, "hot reload bumped content generation");
    if (ScopedCooked != nullptr)
    {
        Check(std::strcmp(ScopedCooked->cookedPath, "textures/hero.ftex") == 0, "cooked path was staged");
        Check(ScopedCooked->cookedHash != 0, "cooked hash was staged");
    }
    const auto ScopedBytes = ScopedVfs.Read("textures/hero.ftex");
    Check(ScopedBytes.has_value(), "VFS remapped the new cooked file");
    if (ScopedBytes)
    {
        const auto ScopedView = Fby::Assets::ParseCookedTexture(*ScopedBytes, &ScopedError);
        Check(ScopedView.has_value(), "cooked VFS bytes parse as .ftex");
        if (ScopedView)
        {
            Check(ScopedView->format == Fby::Assets::TextureFormat::Bc7Srgb, "cooked format is BC7 sRGB");
            Check(ScopedView->width == 8 && ScopedView->height == 8, "cooked dimensions match the source");
        }
    }

    // A changed source goes through the watcher's Poll() path (the same path
    const std::uint32_t ScopedGenerationBefore = ScopedCooked != nullptr ? ScopedCooked->contentGeneration : 0;
    Check(WritePng(ScopedRoot / "textures" / "hero.png", 8, 8, 80), "rewrite source png");
    Check(ScopedWatcher.InjectChange("textures/hero.png"), "inject a changed source");
    (void)ScopedWatcher.Poll();
    std::uint32_t ScopedReloaded = 0;
    for (int ScopedAttempt = 0; ScopedAttempt < 600 && ScopedReloaded == 0; ++ScopedAttempt)
    {
        ScopedReloaded = ScopedImporter.Tick(&ScopedError);
        if (ScopedReloaded == 0)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }
    Check(ScopedReloaded > 0, "watcher Poll hot-reloaded the changed PNG");
    const Fby::Assets::AssetMeta* ScopedAfterChange = ScopedDatabase.Meta(ScopedHandle);
    Check(ScopedAfterChange != nullptr && ScopedAfterChange->contentGeneration > ScopedGenerationBefore,
          "content generation advanced after Poll");

    // Content Browser reads the same database row.
    Fby::Editor::ContentBrowserPanel ScopedPanel;
    Fby::Editor::ContentBrowserPanel::Config ScopedBrowserConfig;
    ScopedBrowserConfig.assetsRoot = ScopedRoot;
    ScopedPanel.Initialize(&ScopedDatabase, ScopedBrowserConfig, &ScopedImporter);
    ScopedPanel.Refresh();
    bool ScopedFound = false;
    for (std::uint32_t ScopedI = 0; ScopedI < ScopedPanel.Count(); ++ScopedI)
    {
        if (std::strcmp(ScopedPanel.Entries()[ScopedI].path, "textures/hero.png") == 0)
        {
            ScopedFound = true;
            Check(ScopedPanel.Entries()[ScopedI].hasMeta, "Content Browser joins the .femeta row");
            Check(ScopedPanel.Entries()[ScopedI].kind == Fby::AssetKind::Texture, "Content Browser shows the type");
        }
    }
    Check(ScopedFound, "Content Browser lists the source file");

    ScopedImporter.Shutdown();
    ScopedWatcher.Shutdown();
}

void TestSpawnCommand()
{
    const std::filesystem::path ScopedRoot = TestRoot("_editor_infra_spawn");
    std::error_code ScopedCode;
    std::filesystem::remove_all(ScopedRoot, ScopedCode);
    std::filesystem::create_directories(ScopedRoot / "textures", ScopedCode);
    Check(WriteText(ScopedRoot / "textures" / "hero.png", "not a real png"), "spawn fixture");

    Fby::Scene::ComponentRegistry ScopedComponents;
    Fby::Scene::RegisterBuiltInComponents(ScopedComponents);
    entt::registry ScopedRegistry;
    Fby::Editor::CommandStack ScopedCommands;
    Check(ScopedCommands.Initialize(), "spawn command stack initializes");
    ScopedCommands.Bind(&ScopedRegistry, &ScopedComponents);
    for (const Fby::Scene::ComponentOps& ScopedOps : ScopedComponents.All())
    {
        if (ScopedOps.serializable && ScopedOps.size <= Fby::Editor::kMaxTrackedComponentBytes)
        {
            Check(ScopedCommands.RegisterComponentOps(ScopedOps) != 0xFFFFu, "component codec registered");
        }
    }

    Fby::Editor::ContentBrowserPanel ScopedPanel;
    Fby::Editor::ContentBrowserPanel::Config ScopedConfig;
    ScopedConfig.assetsRoot = ScopedRoot;
    ScopedPanel.Initialize(nullptr, ScopedConfig, nullptr);
    ScopedPanel.Refresh();

    Fby::Assets::AssetDatabase ScopedDatabase;
    Fby::Editor::ContentBrowserPanel::DragPayload ScopedPayload;
    ScopedPayload.kind = Fby::AssetKind::Texture;
    std::snprintf(ScopedPayload.path, sizeof(ScopedPayload.path), "textures/hero.png");

    Fby::Editor::ContentBrowserPanel::SpawnContext ScopedContext;
    ScopedContext.commands = &ScopedCommands;
    ScopedContext.registry = &ScopedRegistry;
    ScopedContext.components = &ScopedComponents;
    ScopedContext.database = &ScopedDatabase;
    ScopedContext.dropPosition = {100.0f, 50.0f};
    Check(ScopedPanel.SpawnAsset(ScopedPayload, ScopedContext), "texture drop commits a spawn command");
    Check(ScopedRegistry.view<Fby::Transform2D>().size() == 1, "drop created one transform entity");
    if (ScopedCommands.CanUndo())
    {
        const Fby::Entity ScopedSpawned = *ScopedRegistry.view<Fby::Transform2D>().begin();
        Check(ScopedCommands.Undo(), "undo spawn");
        Check(!ScopedRegistry.valid(ScopedSpawned), "undo destroyed the spawned entity");
        Check(ScopedRegistry.view<Fby::Transform2D>().size() == 0, "undo removed the spawned transform");
        Check(ScopedCommands.Redo(), "redo spawn");
        Check(ScopedRegistry.valid(ScopedSpawned), "redo recreated the same entity id");
        Check(ScopedRegistry.get<Fby::Transform2D>(ScopedSpawned).Position.X == 100.0f,
              "redo restored the component image");
    }
    else
    {
        Check(false, "spawn is undoable");
    }

    // A .toml prefab expands to its entities and is still one undo step.
    const std::string ScopedScene = R"TOML(version = 1
name = "prop"
author = "test"

[[entity]]
id = 0
name = "prop"

[[entity.components]]
type = "Transform2D"
Position = [10.0, 10.0]
Scale = [1.0, 1.0]
Rotation = 0.0
)TOML";
    Check(WriteText(ScopedRoot / "scenes" / "prop.toml", ScopedScene), "write prefab");
    Fby::Editor::ContentBrowserPanel::DragPayload ScopedPrefab;
    ScopedPrefab.kind = Fby::AssetKind::Scene;
    std::snprintf(ScopedPrefab.path, sizeof(ScopedPrefab.path), "scenes/prop.toml");
    ScopedContext.dropPosition = {300.0f, 200.0f};
    Check(ScopedPanel.SpawnAsset(ScopedPrefab, ScopedContext), "prefab drop commits a spawn command");
    Check(ScopedRegistry.view<Fby::Transform2D>().size() == 2, "prefab added its own entity");
    Check(ScopedCommands.Undo(), "undo prefab spawn");
    Check(ScopedRegistry.view<Fby::Transform2D>().size() == 1, "prefab undo removed only the prefab entity");
}

void TestDestroyCommand()
{
    entt::registry ScopedRegistry;
    Fby::Scene::ComponentRegistry ScopedComponents;
    Fby::Scene::RegisterBuiltInComponents(ScopedComponents);
    Fby::Editor::CommandStack ScopedCommands;
    Check(ScopedCommands.Initialize(), "destroy command stack initializes");
    ScopedCommands.Bind(&ScopedRegistry, &ScopedComponents);
    for (const Fby::Scene::ComponentOps& ScopedOps : ScopedComponents.All())
    {
        if (ScopedOps.serializable && ScopedOps.size <= Fby::Editor::kMaxTrackedComponentBytes)
        {
            Check(ScopedCommands.RegisterComponentOps(ScopedOps) != 0xFFFFu, "destroy codec registered");
        }
    }

    const Fby::Entity ScopedEntity = ScopedRegistry.create();
    Fby::Transform2D ScopedTransform;
    ScopedTransform.Position = {7.0f, 8.0f};
    ScopedRegistry.emplace<Fby::Transform2D>(ScopedEntity, ScopedTransform);
    Fby::NameComponent ScopedName;
    std::snprintf(ScopedName.Value, sizeof(ScopedName.Value), "boss");
    ScopedRegistry.emplace<Fby::NameComponent>(ScopedEntity, ScopedName);

    Check(ScopedCommands.BeginTransaction("Delete entities") == Fby::Editor::CommandStack::Result::Ok,
          "destroy transaction opens");
    Check(ScopedCommands.RecordDestroy(ScopedEntity) == Fby::Editor::CommandStack::Result::Ok,
          "destroy records the component image");
    ScopedRegistry.destroy(ScopedEntity);
    Check(ScopedCommands.CommitTransaction() == Fby::Editor::CommandStack::Result::Ok, "destroy commits");
    Check(!ScopedRegistry.valid(ScopedEntity), "entity is gone after delete");

    Check(ScopedCommands.Undo(), "undo resurrects the deleted entity");
    Check(ScopedRegistry.valid(ScopedEntity), "entity is back after undo");
    Check(ScopedRegistry.get<Fby::Transform2D>(ScopedEntity).Position.X == 7.0f, "undo restores components");
    Check(std::string_view(ScopedRegistry.get<Fby::NameComponent>(ScopedEntity).Value) == "boss",
          "undo restores the name");
    Check(ScopedCommands.Redo(), "redo deletes the entity again");
    Check(!ScopedRegistry.valid(ScopedEntity), "entity is gone after redo");
}

void TestPrefabSceneRoundTrip()
{
    const std::filesystem::path ScopedRoot = TestRoot("_editor_prefab");
    std::error_code ScopedCode;
    std::filesystem::remove_all(ScopedRoot, ScopedCode);
    std::filesystem::create_directories(ScopedRoot / "prefabs", ScopedCode);

    Fby::Scene::ComponentRegistry ScopedComponents;
    Fby::Scene::RegisterBuiltInComponents(ScopedComponents);
    entt::registry ScopedSource;
    const Fby::Entity ScopedFirst = ScopedSource.create();
    Fby::Transform2D ScopedTransform;
    ScopedTransform.Position = {12.0f, 34.0f};
    ScopedSource.emplace<Fby::Transform2D>(ScopedFirst, ScopedTransform);
    Fby::NameComponent ScopedName;
    std::snprintf(ScopedName.Value, sizeof(ScopedName.Value), "Crate");
    ScopedSource.emplace<Fby::NameComponent>(ScopedFirst, ScopedName);
    const Fby::Entity ScopedSecond = ScopedSource.create();
    ScopedSource.emplace<Fby::Transform2D>(ScopedSecond, ScopedTransform);
    Fby::Sprite2D ScopedSprite;
    ScopedSprite.Size = {24.0f, 24.0f};
    ScopedSource.emplace<Fby::Sprite2D>(ScopedSecond, ScopedSprite);

    Fby::Scene::SceneMeta ScopedMeta;
    ScopedMeta.name = "CratePrefab";
    ScopedMeta.author = "test";
    std::string ScopedError;
    const std::filesystem::path ScopedPrefab = ScopedRoot / "prefabs" / "crate.toml";
    Check(Fby::Scene::SaveSceneFileToml(ScopedPrefab, ScopedSource, ScopedComponents, ScopedMeta, &ScopedError),
          "selection saves as a prefab scene");

    // The Content Browser drop path loads a prefab with clearExisting=false.
    entt::registry ScopedTarget;
    Fby::Scene::SceneLoadOptions ScopedOptions;
    ScopedOptions.clearExisting = false;
    const auto ScopedLoaded =
        Fby::Scene::LoadSceneFileToml(ScopedTarget, ScopedComponents, ScopedPrefab, ScopedOptions);
    Check(ScopedLoaded.has_value(), "prefab loads back");
    Check(ScopedTarget.storage<Fby::Entity>().size() == 2, "prefab restores both entities");
    bool ScopedNameFound = false;
    for (const Fby::Entity ScopedEntity : ScopedTarget.view<Fby::NameComponent>())
    {
        if (std::string_view(ScopedTarget.get<Fby::NameComponent>(ScopedEntity).Value) == "Crate")
        {
            ScopedNameFound = true;
        }
    }
    Check(ScopedNameFound, "prefab keeps entity names");
}

void TestAssetIndexRefresh()
{
    const std::filesystem::path ScopedRoot = TestRoot("_editor_assets_refresh");
    std::error_code ScopedCode;
    std::filesystem::remove_all(ScopedRoot, ScopedCode);
    std::filesystem::create_directories(ScopedRoot, ScopedCode);
    Check(WriteText(ScopedRoot / "first.txt", "one"), "write first refresh fixture");

    Fby::VirtualFileSystem ScopedVfs;
    std::string ScopedError;
    Check(ScopedVfs.MountDirectory(ScopedRoot.string(), 0, &ScopedError), "mount the refresh directory");
    Check(ScopedVfs.Find("first.txt").has_value(), "the mounted file is indexed");

    // The editor's file manager creates/renames/deletes files outside the VFS
    // write path, so the index must be refreshable.
    Check(WriteText(ScopedRoot / "second.txt", "two"), "write second refresh fixture");
    Check(!ScopedVfs.Find("second.txt").has_value(), "a new file is invisible before the refresh");
    ScopedVfs.RefreshDirectoryMounts();
    Check(ScopedVfs.Find("second.txt").has_value(), "the refresh indexes the new file");

    std::filesystem::remove(ScopedRoot / "first.txt", ScopedCode);
    ScopedVfs.RefreshDirectoryMounts();
    Check(!ScopedVfs.Find("first.txt").has_value(), "the refresh drops the deleted file");
}

void TestSettingsExternalTransaction()
{
    Fby::Editor::ProjectSettings ScopedSettings;
    Fby::Editor::CommandStack ScopedCommands;
    Check(ScopedCommands.Initialize(), "settings command stack initializes");
    entt::registry ScopedRegistry;
    ScopedCommands.Bind(&ScopedRegistry, nullptr);
    const std::uint16_t ScopedCodec =
        ScopedCommands.RegisterExternalBuffer("ProjectSettings", &ScopedSettings, sizeof(Fby::Editor::ProjectSettings));
    Check(ScopedCodec != 0xFFFFu, "settings external codec registered");

    std::array<std::byte, sizeof(Fby::Editor::ProjectSettings)> ScopedBefore{};
    Check(ScopedCommands.CaptureComponent(entt::null, ScopedCodec, ScopedBefore) ==
              Fby::Editor::CommandStack::Result::Ok,
          "capture settings pre-image");
    Fby::AccessibilitySetFlag(&ScopedSettings.accessibility.runtime.flags, Fby::AccessibilityBlindMode, true);
    ScopedSettings.accessibility.presentation.oledPixelShift = false;
    ScopedSettings.fixedUpdateHz = 90.0f;

    Fby::Editor::InteractionTransaction ScopedTransaction;
    Check(ScopedTransaction.BeginWithSnapshot(ScopedCommands, entt::null, ScopedCodec, "Project settings",
                                              Fby::Editor::InteractionTransaction::Source::Settings,
                                              std::span<const std::byte>(ScopedBefore.data(), ScopedBefore.size())) ==
              Fby::Editor::CommandStack::Result::Ok,
          "settings transaction begins");
    Check(ScopedTransaction.Record() == Fby::Editor::CommandStack::Result::Ok, "settings transaction records");
    Check(ScopedTransaction.Commit() == Fby::Editor::CommandStack::Result::Ok, "settings transaction commits");
    Check(ScopedCommands.CanUndo(), "settings commit is undoable");
    Check(ScopedCommands.GetStats().bytesUsed < 128, "settings delta is sparse");
    Check(ScopedCommands.Undo(), "undo settings");
    Check(!Fby::AccessibilityHasFlag(ScopedSettings.accessibility.runtime.flags, Fby::AccessibilityBlindMode) &&
              ScopedSettings.accessibility.presentation.oledPixelShift && ScopedSettings.fixedUpdateHz == 120.0f,
          "undo restored the settings image");
    Check(ScopedCommands.Redo(), "redo settings");
    Check(Fby::AccessibilityHasFlag(ScopedSettings.accessibility.runtime.flags, Fby::AccessibilityBlindMode) &&
              !ScopedSettings.accessibility.presentation.oledPixelShift && ScopedSettings.fixedUpdateHz == 90.0f,
          "redo reapplied the settings image");
}

// A consumer that pins a VFS path (the VP9 decoder) must keep its bytes
void TestVfsPinnedFileLifetime()
{
    const std::filesystem::path ScopedRoot = TestRoot("_editor_infra_pin");
    std::error_code ScopedCode;
    std::filesystem::remove_all(ScopedRoot, ScopedCode);
    std::filesystem::create_directories(ScopedRoot, ScopedCode);

    const std::filesystem::path ScopedClip = ScopedRoot / "clip.webm";
    {
        std::ofstream ScopedStream(ScopedClip, std::ios::binary | std::ios::trunc);
        ScopedStream.write("OLD!", 4);
    }

    Fby::VirtualFileSystem ScopedVfs;
    Check(ScopedVfs.MountDirectory(ScopedRoot.string(), 1), "pin test mounts the directory");
    const auto ScopedPinned = ScopedVfs.Pin("clip.webm");
    Check(ScopedPinned.has_value(), "Pin returns an owning handle");
    if (ScopedPinned.has_value())
    {
        Check(ScopedPinned->mapping != nullptr, "Pin carries the mapping owner");
        Check(ScopedPinned->bytes.size() == 4 && std::memcmp(ScopedPinned->bytes.data(), "OLD!", 4) == 0,
              "pinned bytes are the original clip");
    }

    // VFS hot swap: the old entry is dropped and a higher-priority mount
    Check(ScopedVfs.InvalidateFile("clip.webm"), "InvalidateFile drops the old index entry");
    const std::filesystem::path ScopedReplacementRoot = TestRoot("_editor_infra_pin_new");
    std::filesystem::remove_all(ScopedReplacementRoot, ScopedCode);
    std::filesystem::create_directories(ScopedReplacementRoot, ScopedCode);
    {
        std::ofstream ScopedStream(ScopedReplacementRoot / "clip.webm", std::ios::binary | std::ios::trunc);
        ScopedStream.write("NEW-DATA", 8);
    }
    Check(ScopedVfs.MountDirectory(ScopedReplacementRoot.string(), 2), "hot-swap mount succeeds");

    if (ScopedPinned.has_value())
    {
        Check(ScopedPinned->bytes.size() == 4 && std::memcmp(ScopedPinned->bytes.data(), "OLD!", 4) == 0,
              "pinned mapping survives the hot swap unchanged");
    }
    const auto ScopedFresh = ScopedVfs.Read("clip.webm");
    Check(ScopedFresh.has_value() && ScopedFresh->size() == 8 && std::memcmp(ScopedFresh->data(), "NEW-DATA", 8) == 0,
          "a fresh read sees the hot-swapped bytes");
}
}

int main()
{
    TestProjectCreationAndConfig();
    TestAutoImportAndBackgroundCook();
    TestSpawnCommand();
    TestDestroyCommand();
    TestPrefabSceneRoundTrip();
    TestAssetIndexRefresh();
    TestSettingsExternalTransaction();
    TestVfsPinnedFileLifetime();
    if (GFailures != 0)
    {
        std::printf("%d editor infrastructure test(s) failed\n", GFailures);
        return 1;
    }
    std::printf("editor_infrastructure_test: all tests passed\n");
    return 0;
}
