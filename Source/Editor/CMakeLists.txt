# FemboyEngine2D - editor library.

add_library(fby_editor STATIC
    Infrastructure/AssetDatabaseWatchSystem.cpp
    Infrastructure/AssetImport.cpp
    Infrastructure/AssetImportService.cpp
    Infrastructure/CommandStack.cpp
    Infrastructure/EditorFonts.cpp
    Infrastructure/EditorSettings.cpp
    Infrastructure/EditorTypography.cpp
    Infrastructure/EditorSystem.cpp
    Infrastructure/FrameComposer.cpp
    Infrastructure/MaterialGraphRuntime.cpp
    Infrastructure/PhysicsDebugDraw.cpp
    Infrastructure/PlayInEditor.cpp
    Infrastructure/ProjectConfig.cpp
    Infrastructure/ReleaseCooker.cpp
    Infrastructure/TextureThumbnails.cpp
    Panels/AIStateTreeCanvasPanel.cpp
    Panels/AnimationGraphCanvasPanel.cpp
    Panels/AnimationTimelinePanel.cpp
    Panels/ContentBrowserPanel.cpp
    Panels/LogBoyPanel.cpp
    Panels/LicensesPanel.cpp
    Panels/NetworkDiagnosticsPanel.cpp
    Panels/PieClientViewport.cpp
    Panels/VoiceDevicePanel.cpp
    Panels/MaterialGraphPanel.cpp
    Panels/NodeCanvasPanel.cpp
    Panels/ProjectDashboard.cpp
    Panels/SceneDiagnosticsPanel.cpp
    Panels/ScriptDebuggerPanel.cpp
    Panels/SettingsPanels.cpp
    Panels/SkeletonAssetPanel.cpp
    Panels/SpriteAnimationPanel.cpp
    Panels/TilemapEditorPanel.cpp
    Panels/TextPreviewPanel.cpp
    Panels/VirtualTexturePanel.cpp
    Panels/VisualScriptPanel.cpp)

# The sanitizer gate covers the editor library where the toolchain allows
# mixing sanitized and plain static libraries. clang-cl enforces metadata
# matching, so the editor stays plain there and is covered on Linux and macOS.
if((FBY_WITH_ASAN OR FBY_WITH_UBSAN) AND NOT WIN32)
    fby_asan_apply(fby_editor)
endif()
add_library(FemboyEngine2D::Editor ALIAS fby_editor)

target_link_libraries(fby_editor
    PUBLIC
        FemboyEngine2D::Engine
        fby::imgui
        fby::compiler_flags
    PRIVATE
        FemboyEngine2D::AssetCook
        FemboyEngine2D::ShaderCompilerCore)

target_include_directories(fby_editor
    PUBLIC
        $<BUILD_INTERFACE:${FBY_SOURCE_ROOT}>
        $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/generated>)

# The VisualScript panel pre-fills AotBuildOptions::includeDirectory from this
target_compile_definitions(fby_editor
    PUBLIC FBY_ENGINE_INCLUDE_DIR="${FBY_SOURCE_ROOT}")

# Last-resort fallback for DefaultProjectsRoot(): the engine root recorded at
target_compile_definitions(fby_editor
    PRIVATE FBY_ENGINE_ROOT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../..")

set_target_properties(fby_editor PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${FBY_OUTPUT_DIR}/lib")
