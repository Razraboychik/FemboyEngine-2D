# FemboyEngine2D 1.0 - core library.

# This file is included as a subdirectory of a generated game project, where
get_filename_component(FBY_ENGINE_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

set(FBY_ENGINE_SOURCES
    Assets/AnimationClipAsset.cpp
    Assets/AssetDatabase.cpp
    Assets/AssetMeta.cpp
    Assets/AudioClipFormat.cpp
    Assets/FemArchiver.cpp
    Assets/FemCrypto.cpp
    Assets/FempakWriter.cpp
    Assets/Localization.cpp
    Assets/LocalizationCompiler.cpp
    Assets/TextureArray.cpp
    Assets/VFS.cpp
    Audio/Audio.cpp
    Audio/AudioDevices.cpp
    Audio/AudioStream.cpp
    Audio/AudioZones.cpp
    Audio/DspGraph.cpp
    Audio/MiniAudioImpl.c
    Audio/OggOpus.cpp
    Audio/VoiceAec.cpp
    Audio/VoiceChannel.cpp
    Audio/VoiceNetworkBridge.cpp
    Audio/VoiceSystem.cpp
    Audio/VoiceCodec.cpp
    Audio/VoiceDsp.cpp
    Audio/VoiceSession.cpp
    Core/CryptoAead.cpp
    Core/CryptoAsymmetric.cpp
    Core/CryptoKdf.cpp
    Core/Sha256.cpp
    Core/JobSystem.cpp
    Core/CrashHandler.cpp
    Core/BuildInfo.cpp
    Core/Log.cpp
    Core/NoiseGenerator.cpp
    Core/PluginHost.cpp
    Core/SecureRng.cpp
    Core/ThirdPartyNotices.cpp
    Core/ThreadAffinity.cpp
    Core/WindowManager.cpp
    ECS/AnimationCompression.cpp
    ECS/AnimationTimeline.cpp
    ECS/AnimGraph.cpp
    ECS/CommandLog.cpp
    ECS/Lighting.cpp
    ECS/LightSource.cpp
    ECS/Presentation.cpp
    ECS/PrefabSystem.cpp
    ECS/ComponentRegistry.cpp
    ECS/SceneBinary.cpp
    ECS/SceneSerialization.cpp
    ECS/SceneToml.cpp
    ECS/Schedule.cpp
    ECS/Hierarchy.cpp
    ECS/Skeleton.cpp
    ECS/SkeletonAsset.cpp
    ECS/SkeletonIk.cpp
    ECS/Tilemap.cpp
    ECS/Snapshot.cpp
    ECS/SnapshotWorld.cpp
    ECS/StateTree.cpp
    ECS/SystemGraph.cpp
    ECS/SpriteAnimation.cpp
    Network/NetRollback.cpp
    Network/NetProtocol.cpp
    Network/Identity.cpp
    Network/Lobby.cpp
    Network/NetLink.cpp
    Network/RelevanceGrid.cpp
    Network/Replication.cpp
    Network/ReplicationEcs.cpp
    Network/SecureChannel.cpp
    Network/SecureEndpoint.cpp
    Network/SecureSessionTransport.cpp
    Network/Session.cpp
    Network/Ticket.cpp
    Network/Transport.cpp
    Navigation/Navigation.cpp
    Physics/Cloth.cpp
    Physics/ClothRenderSystem.cpp
    Physics/Fluid.cpp
    Physics/Physics.cpp
    Physics/PhysicsJoints.cpp
    Physics/SdfCollision.cpp
    Physics/SdfPrimitives.cpp
    Physics/SnapshotRemap.cpp
    Platform/PlatformService.cpp
    Platform/PlatformSystems.cpp
    RHI/CameraStack.cpp
    RHI/FrameExporter.cpp
    RHI/MaterialAsset.cpp
    RHI/MaterialGraph.cpp
    RHI/MaterialPipelineCache.cpp
    RHI/RHI.cpp
    RHI/RenderGraph.cpp
    RHI/RenderPasses.cpp
    RHI/SkinningBridge.cpp
    RHI/VirtualTextureSystem.cpp
    RHI/ViewportModes.cpp
    RHI/ViewportPresenter.cpp
    RHI/Vulkan/VulkanRHI.cpp
    Script/AotBuilder.cpp
    Script/AotGraphCompiler.cpp
    Script/AotHost.cpp
    Script/AotModule.cpp
    Script/AotScriptSystem.cpp
    Script/AotTranspiler.cpp
    Script/Bytecode.cpp
    Script/EcsBridge.cpp
    Script/GraphCompiler.cpp
    Script/TextScript.cpp
    Text/TextBidi.cpp
    Text/TextShapeCache.cpp
    Text/TextShaper.cpp
    UI/Accessibility.cpp
    UI/AccessibilityOverlay.cpp
    UI/CardBoard.cpp
    UI/FontAtlas.cpp
    UI/FontBuilder.cpp
    UI/NarrativeTree.cpp
    UI/Hypertext.cpp
    UI/PinkSdf.cpp
    UI/UiTheme.cpp
    UI/PinkUI.cpp
    UI/WidgetLayoutAsset.cpp
    UI/SoundCues.cpp
    UI/TtsSapi.cpp
    UI/TtsNative.cpp
    Video/VideoPlayer.cpp
    Video/VideoSystem.cpp
    Video/Vp9Backend.cpp
    Video/WebmDemuxer.cpp
    Weather/Weather.cpp
    Weather/WeatherPreconciliated.cpp
)

# macOS native speech parity: AVSpeechSynthesizer is Objective-C++ and enabled
if(APPLE)
    list(APPEND FBY_ENGINE_SOURCES UI/TtsAvSpeech.mm)
    set_source_files_properties(UI/TtsAvSpeech.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
else()
    list(APPEND FBY_ENGINE_SOURCES UI/TtsAvSpeechStub.cpp)
endif()

# Shaders: GLSL -> SPIR-V -> embedded header
set(FBY_SHADER_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/RHI/Shaders")
set(FBY_SHADER_OUTPUT_DIR "${CMAKE_BINARY_DIR}/shaders")
file(MAKE_DIRECTORY "${FBY_SHADER_OUTPUT_DIR}" "${CMAKE_BINARY_DIR}/generated")

# Metal: MSL source -> embedded C++ string
if(APPLE)
    list(APPEND FBY_ENGINE_SOURCES RHI/Metal/MetalRHI.mm)
    set_source_files_properties(RHI/Metal/MetalRHI.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${FBY_SHADER_SOURCE_DIR}/metal.metal")

    file(READ "${FBY_SHADER_SOURCE_DIR}/metal.metal" FBY_METAL_MSL)
    set(FBY_METAL_HEADER "${CMAKE_BINARY_DIR}/generated/MetalShaderSource.hpp")
    file(WRITE "${FBY_METAL_HEADER}"
"// Generated from Source/Engine/RHI/Shaders/metal.metal - do not edit.
# pragma once
namespace fby::rhi::metal_shaders {
inline constexpr char kMetalLibrarySource[] = R\"FBYMSL(
${FBY_METAL_MSL}
)FBYMSL\";
}
")
    set_source_files_properties("${FBY_METAL_HEADER}" PROPERTIES GENERATED TRUE HEADER_FILE_ONLY TRUE)
    list(APPEND FBY_ENGINE_SOURCES "${FBY_METAL_HEADER}")

# The GPU weather kernel is a second MSL source. MetalRHI compiles both into
    file(READ "${FBY_SHADER_SOURCE_DIR}/weather_rain.metal" FBY_METAL_WEATHER_MSL)
    set(FBY_METAL_WEATHER_HEADER "${CMAKE_BINARY_DIR}/generated/MetalWeatherShaderSource.hpp")
    file(WRITE "${FBY_METAL_WEATHER_HEADER}"
"// Generated from Source/Engine/RHI/Shaders/weather_rain.metal - do not edit.
# pragma once
namespace fby::rhi::metal_shaders {
inline constexpr char kMetalWeatherSource[] = R\"FBYMSL(
${FBY_METAL_WEATHER_MSL}
)FBYMSL\";
}
")
    set_source_files_properties("${FBY_METAL_WEATHER_HEADER}" PROPERTIES GENERATED TRUE HEADER_FILE_ONLY TRUE)
    list(APPEND FBY_ENGINE_SOURCES "${FBY_METAL_WEATHER_HEADER}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${FBY_SHADER_SOURCE_DIR}/weather_rain.metal")
endif()

function(fby_compile_shader symbol source)
    set(output "${FBY_SHADER_OUTPUT_DIR}/${symbol}.spv")
    add_custom_command(
        OUTPUT "${output}"
        COMMAND $<TARGET_FILE:fby_shaderc> "${FBY_SHADER_SOURCE_DIR}/${source}" -o "${output}"
        DEPENDS "${FBY_SHADER_SOURCE_DIR}/${source}" fby_shaderc
        COMMENT "glslang: ${source}"
        VERBATIM)
    set(${symbol}_SPV "${output}" PARENT_SCOPE)
endfunction()

fby_compile_shader(quad_vert quad.vert)
fby_compile_shader(quad_frag quad.frag)
fby_compile_shader(mesh_skinning mesh_skinning.comp)
fby_compile_shader(mesh_vert mesh.vert)
fby_compile_shader(mesh_frag mesh.frag)
fby_compile_shader(fullscreen_vert fullscreen.vert)
fby_compile_shader(postfx_bloom_prefilter postfx_bloom_prefilter.comp)
fby_compile_shader(postfx_bloom_blur postfx_bloom_blur.comp)
fby_compile_shader(postfx_composite postfx_composite.frag)
fby_compile_shader(sdf_shadow sdf_shadow.comp)
fby_compile_shader(pink_sdf_vert pink_sdf.vert)
fby_compile_shader(pink_sdf_frag pink_sdf.frag)
fby_compile_shader(weather_rain weather_rain.comp)
fby_compile_shader(weather_rain_splash weather_rain_splash.comp)
fby_compile_shader(weather_rain_vert weather_rain.vert)
fby_compile_shader(weather_rain_frag weather_rain.frag)
fby_compile_shader(weather_splash_vert weather_splash.vert)
fby_compile_shader(weather_splash_frag weather_splash.frag)
fby_compile_shader(weather_clouds weather_clouds.comp)
fby_compile_shader(weather_cloud_shadow weather_cloud_shadow.comp)
fby_compile_shader(weather_fog weather_fog.comp)
fby_compile_shader(weather_fire weather_fire.comp)
fby_compile_shader(weather_composite weather_composite.frag)
fby_compile_shader(plant_wind_vert plant_wind.vert)
fby_compile_shader(plant_wind_frag plant_wind.frag)
fby_compile_shader(color_grading color_grading.frag)
fby_compile_shader(accessibility_composite accessibility_composite.frag)
fby_compile_shader(accessibility_flash accessibility_flash.comp)

set(FBY_SHADER_BLOBS "${CMAKE_BINARY_DIR}/generated/ShaderBlobs.hpp")
add_custom_command(
    OUTPUT "${FBY_SHADER_BLOBS}"
    COMMAND ${CMAKE_COMMAND}
            "-DOUTPUT=${FBY_SHADER_BLOBS}"
            "-DNAMESPACE=Fby::Rhi::Shaders"
            "-DSPEC=accessibility_flash=${accessibility_flash_SPV}|accessibility_composite=${accessibility_composite_SPV}|quad_vert=${quad_vert_SPV}|quad_frag=${quad_frag_SPV}|mesh_skinning=${mesh_skinning_SPV}|mesh_vert=${mesh_vert_SPV}|mesh_frag=${mesh_frag_SPV}|fullscreen_vert=${fullscreen_vert_SPV}|postfx_bloom_prefilter=${postfx_bloom_prefilter_SPV}|postfx_bloom_blur=${postfx_bloom_blur_SPV}|postfx_composite=${postfx_composite_SPV}|sdf_shadow=${sdf_shadow_SPV}|pink_sdf_vert=${pink_sdf_vert_SPV}|pink_sdf_frag=${pink_sdf_frag_SPV}|weather_rain=${weather_rain_SPV}|weather_rain_splash=${weather_rain_splash_SPV}|weather_rain_vert=${weather_rain_vert_SPV}|weather_rain_frag=${weather_rain_frag_SPV}|weather_splash_vert=${weather_splash_vert_SPV}|weather_splash_frag=${weather_splash_frag_SPV}|weather_clouds=${weather_clouds_SPV}|weather_cloud_shadow=${weather_cloud_shadow_SPV}|weather_fog=${weather_fog_SPV}|weather_fire=${weather_fire_SPV}|weather_composite=${weather_composite_SPV}|plant_wind_vert=${plant_wind_vert_SPV}|plant_wind_frag=${plant_wind_frag_SPV}|color_grading=${color_grading_SPV}"
            -P "${FBY_ENGINE_REPO_ROOT}/cmake/EmbedShaders.cmake"
    DEPENDS "${accessibility_flash_SPV}" "${accessibility_composite_SPV}" "${quad_vert_SPV}" "${quad_frag_SPV}" "${mesh_skinning_SPV}" "${mesh_vert_SPV}" "${mesh_frag_SPV}"
            "${fullscreen_vert_SPV}" "${postfx_bloom_prefilter_SPV}" "${postfx_bloom_blur_SPV}"
            "${postfx_composite_SPV}" "${sdf_shadow_SPV}" "${pink_sdf_vert_SPV}"
            "${pink_sdf_frag_SPV}" "${weather_rain_SPV}" "${weather_rain_splash_SPV}" "${weather_rain_vert_SPV}"
            "${weather_rain_frag_SPV}" "${weather_splash_vert_SPV}" "${weather_splash_frag_SPV}"
            "${weather_clouds_SPV}" "${weather_cloud_shadow_SPV}" "${weather_fog_SPV}" "${weather_fire_SPV}"
            "${weather_composite_SPV}" "${plant_wind_vert_SPV}" "${plant_wind_frag_SPV}" "${color_grading_SPV}"
            "${FBY_ENGINE_REPO_ROOT}/cmake/EmbedShaders.cmake"
    COMMENT "Embedding SPIR-V"
    VERBATIM)
set_source_files_properties("${FBY_SHADER_BLOBS}" PROPERTIES GENERATED TRUE HEADER_FILE_ONLY TRUE)

# FlatBuffers schema: localization
set(FBY_LOC_SCHEMA "${CMAKE_CURRENT_SOURCE_DIR}/Assets/Schemas/localization.fbs")
set(FBY_LOC_GENERATED "${CMAKE_BINARY_DIR}/generated/localization_generated.h")
add_custom_command(
    OUTPUT "${FBY_LOC_GENERATED}"
    COMMAND flatc --cpp --scoped-enums -o "${CMAKE_BINARY_DIR}/generated" "${FBY_LOC_SCHEMA}"
    DEPENDS "${FBY_LOC_SCHEMA}" flatc
    COMMENT "flatc: localization.fbs"
    VERBATIM)
set_source_files_properties("${FBY_LOC_GENERATED}" PROPERTIES GENERATED TRUE HEADER_FILE_ONLY TRUE)

# FlatBuffers schema: scene / prefab binary format
set(FBY_SCENE_SCHEMA "${CMAKE_CURRENT_SOURCE_DIR}/Assets/Schemas/scene.fbs")
set(FBY_SCENE_GENERATED "${CMAKE_BINARY_DIR}/generated/scene_generated.h")
add_custom_command(
    OUTPUT "${FBY_SCENE_GENERATED}"
    COMMAND flatc --cpp --scoped-enums -o "${CMAKE_BINARY_DIR}/generated" "${FBY_SCENE_SCHEMA}"
    DEPENDS "${FBY_SCENE_SCHEMA}" flatc
    COMMENT "flatc: scene.fbs"
    VERBATIM)
set_source_files_properties("${FBY_SCENE_GENERATED}" PROPERTIES GENERATED TRUE HEADER_FILE_ONLY TRUE)

# FlatBuffers schema: data-driven prefab descriptors
set(FBY_PREFAB_SCHEMA "${CMAKE_CURRENT_SOURCE_DIR}/ECS/Schemas/prefab.fbs")
set(FBY_PREFAB_GENERATED "${CMAKE_BINARY_DIR}/generated/prefab_generated.h")
add_custom_command(
    OUTPUT "${FBY_PREFAB_GENERATED}"
    COMMAND flatc --cpp --scoped-enums -o "${CMAKE_BINARY_DIR}/generated" "${FBY_PREFAB_SCHEMA}"
    DEPENDS "${FBY_PREFAB_SCHEMA}" flatc
    COMMENT "flatc: prefab.fbs"
    VERBATIM)
set_source_files_properties("${FBY_PREFAB_GENERATED}" PROPERTIES GENERATED TRUE HEADER_FILE_ONLY TRUE)

# Texture cook library
add_library(fby_assetcook STATIC
    Assets/TextureCook.cpp
    Assets/TextureFormat.cpp)
# PascalCase public aliases for FemboyEngine2D.
add_library(FemboyEngine2D::AssetCook ALIAS fby_assetcook)

target_link_libraries(fby_assetcook PUBLIC fby::compiler_flags)
target_include_directories(fby_assetcook
    PUBLIC
        $<BUILD_INTERFACE:${FBY_SOURCE_ROOT}>
    PRIVATE
        "${FBY_ENGINE_REPO_ROOT}/third_party/stb")
set_target_properties(fby_assetcook PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${FBY_OUTPUT_DIR}/lib")
# Tests that link the cooker must see the same instrumentation as the engine.
if(FBY_WITH_ASAN OR FBY_WITH_UBSAN)
    fby_asan_apply(fby_assetcook)
endif()

# Universal shaping: HarfBuzz is vendored so the text pipeline has stable
# OpenType GSUB/GPOS behavior on every platform. Subset/raster/GPU tools are
# not needed by the runtime.
option(FBY_WITH_HARFBUZZ "Build the vendored HarfBuzz shaping backend" ON)
option(FBY_WITH_SHEENBIDI "Build the vendored SheenBidi UAX #9 backend" ON)
if(FBY_WITH_SHEENBIDI)
    set(SB_CONFIG_EXPERIMENTAL_TEXT_API OFF CACHE BOOL "" FORCE)
    set(SB_CONFIG_UNITY ON CACHE BOOL "" FORCE)
    set(BUILD_GENERATOR OFF CACHE BOOL "" FORCE)
    add_subdirectory("${FBY_ENGINE_REPO_ROOT}/third_party/sheenbidi-download/SheenBidi-3.0.0"
                     "${CMAKE_BINARY_DIR}/sheenbidi" EXCLUDE_FROM_ALL)
endif()

if(FBY_WITH_HARFBUZZ)
    set(HB_BUILD_UTILS OFF CACHE BOOL "" FORCE)
    set(HB_BUILD_SUBSET OFF CACHE BOOL "" FORCE)
    set(HB_BUILD_RASTER OFF CACHE BOOL "" FORCE)
    set(HB_BUILD_VECTOR OFF CACHE BOOL "" FORCE)
    set(HB_BUILD_GPU OFF CACHE BOOL "" FORCE)
    set(HB_HAVE_GLIB OFF CACHE BOOL "" FORCE)
    set(HB_HAVE_ICU OFF CACHE BOOL "" FORCE)
    set(HB_HAVE_FREETYPE OFF CACHE BOOL "" FORCE)
    set(HB_HAVE_GRAPHITE2 OFF CACHE BOOL "" FORCE)
    add_subdirectory("${FBY_ENGINE_REPO_ROOT}/third_party/harfbuzz-download/harfbuzz-15.0.0"
                     "${CMAKE_BINARY_DIR}/harfbuzz" EXCLUDE_FROM_ALL)
endif()

add_library(fby_engine STATIC ${FBY_ENGINE_SOURCES} "${FBY_SHADER_BLOBS}" "${FBY_LOC_GENERATED}"
                                "${FBY_SCENE_GENERATED}" "${FBY_PREFAB_GENERATED}")
add_library(FemboyEngine2D::Engine ALIAS fby_engine)
add_dependencies(fby_engine fby_shaderc)
if(FBY_WITH_ASAN OR FBY_WITH_UBSAN)
    # Only the engine itself: the asset libraries are linked by the build-time
    # tools, and a sanitizer there would pull glslang into the instrumented set.
    fby_asan_apply(fby_engine)
endif()

# The interpreter is a development tool. A cooked build compiles it out and runs
# only nativized AOT modules (see AotScriptSystem / AotHost).
if(FBY_WITH_SCRIPT_VM)
    target_sources(fby_engine PRIVATE Script/ScriptSystem.cpp Script/Vm.cpp)
    target_compile_definitions(fby_engine PUBLIC FBY_WITH_SCRIPT_VM=1)
else()
    target_compile_definitions(fby_engine PUBLIC FBY_WITH_SCRIPT_VM=0)
    message(STATUS "FBY_WITH_SCRIPT_VM=OFF: the interpreter is not compiled; scripts run as AOT modules")
endif()

target_include_directories(fby_engine
    PUBLIC
        $<BUILD_INTERFACE:${FBY_SOURCE_ROOT}>
        $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/generated>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        "${FBY_ENGINE_REPO_ROOT}/third_party/stb")

target_link_libraries(fby_engine
    PUBLIC
        fby::compiler_flags
        fby::entt
        fby::toml
    PRIVATE
        ${FBY_SDL_TARGET}
        fby::miniaudio
        fby::monocypher
        fby::box2d
        fby::flatbuffers
        FemboyEngine2D::AssetCook)

if(FBY_WITH_HARFBUZZ AND TARGET harfbuzz)
    target_link_libraries(fby_engine PRIVATE harfbuzz)
    target_compile_definitions(fby_engine PRIVATE FBY_HAS_HARFBUZZ=1)
endif()
if(FBY_WITH_SHEENBIDI AND TARGET SheenBidi)
    target_link_libraries(fby_engine PRIVATE SheenBidi)
    target_compile_definitions(fby_engine PRIVATE FBY_HAS_SHEENBIDI=1)
endif()

# Ogg Opus decoding. Vendored libopus; the Ogg page demuxer is in-tree.
if(TARGET Opus::opus)
    target_link_libraries(fby_engine PRIVATE Opus::opus)
    target_compile_definitions(fby_engine PRIVATE FBY_HAS_OPUS=1)
elseif(TARGET opus)
    target_link_libraries(fby_engine PRIVATE opus)
    target_compile_definitions(fby_engine PRIVATE FBY_HAS_OPUS=1)
endif()

# RNNoise is an optional denoise tier; the spectral path stays the fallback.
if(TARGET fby::rnnoise)
    target_link_libraries(fby_engine PRIVATE fby::rnnoise)
    target_compile_definitions(fby_engine PUBLIC FBY_HAS_RNNOISE=1)
endif()

# Vulkan headers only; the loader is resolved at runtime by VulkanLoader.
if (TARGET Vulkan::Headers)
    target_link_libraries(fby_engine PRIVATE Vulkan::Headers)
    target_compile_definitions(fby_engine PUBLIC FBY_HAS_VULKAN_HEADERS=1)
endif()

if (WIN32)
# winmm is for the miniaudio backend; ole32/uuid for WASAPI; bcrypt is the
# entropy/AEAD backend; crypt32 is the DPAPI identity store.
    target_link_libraries(fby_engine PRIVATE user32 gdi32 shell32 ole32 oleaut32 uuid winmm bcrypt crypt32 ws2_32)
elseif (APPLE)
    target_link_libraries(fby_engine PRIVATE
        "-framework Cocoa" "-framework Metal" "-framework MetalKit" "-framework QuartzCore" "-framework Foundation")
    target_compile_definitions(fby_engine PRIVATE FBY_HAS_METAL=1)
else()
    target_link_libraries(fby_engine PRIVATE dl pthread m)
endif()

if (UNIX AND NOT APPLE)
# mmap, dlopen and the POSIX file layer.
    target_link_libraries(fby_engine PRIVATE dl)
endif()

target_compile_definitions(fby_engine PUBLIC
    FBY_ENGINE_VERSION="${FBY_VERSION_STRING}"
    FBY_PLATFORM_NAME="${CMAKE_SYSTEM_NAME}")

# Test-only conformance hooks (transport key export) are compiled only for test
# builds, so a release build ships no test-only key accessor.
if(FBY_BUILD_TESTS)
    target_compile_definitions(fby_engine PUBLIC FBY_ENGINE_TEST_HOOKS=1)
endif()

set_target_properties(fby_engine PROPERTIES
    OUTPUT_NAME fby_engine
    ARCHIVE_OUTPUT_DIRECTORY "${FBY_OUTPUT_DIR}/lib"
    LIBRARY_OUTPUT_DIRECTORY "${FBY_OUTPUT_DIR}/lib")

# Offline asset pipeline
add_library(fby_assetlib STATIC
    Assets/FemArchiver.cpp
    Assets/FemCrypto.cpp
    Assets/FempakWriter.cpp
    Assets/LocalizationCompiler.cpp
    "${FBY_LOC_GENERATED}")
add_library(FemboyEngine2D::AssetLib ALIAS fby_assetlib)

target_link_libraries(fby_assetlib
    PUBLIC
        fby::compiler_flags
        fby::toml
        fby::flatbuffers)

target_include_directories(fby_assetlib
    PUBLIC
        $<BUILD_INTERFACE:${FBY_SOURCE_ROOT}>
        $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/generated>)

set_target_properties(fby_assetlib PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${FBY_OUTPUT_DIR}/lib")

# LZMA SDK (7-Zip) - optional distribution codec.
set(FBY_LZMA_SDK_DIR "${FBY_ENGINE_REPO_ROOT}/third_party/lzma-sdk" CACHE PATH "LZMA SDK directory")
if(EXISTS "${FBY_LZMA_SDK_DIR}/LzmaLib.h")
    message(STATUS "LZMA SDK: ${FBY_LZMA_SDK_DIR}")
    set(FBY_LZMA_SOURCES
        "${FBY_LZMA_SDK_DIR}/Alloc.c"
        "${FBY_LZMA_SDK_DIR}/CpuArch.c"
        "${FBY_LZMA_SDK_DIR}/LzFind.c"
        "${FBY_LZMA_SDK_DIR}/LzmaDec.c"
        "${FBY_LZMA_SDK_DIR}/LzmaEnc.c"
        "${FBY_LZMA_SDK_DIR}/LzmaLib.c")
    foreach(target IN ITEMS fby_engine fby_assetlib)
        target_sources(${target} PRIVATE ${FBY_LZMA_SOURCES})
        target_include_directories(${target} PRIVATE "${FBY_LZMA_SDK_DIR}")
        target_compile_definitions(${target} PRIVATE FBY_HAS_LZMA_SDK=1)
    endforeach()
else()
    message(STATUS "LZMA SDK: not found (FbyLZ fallback is active)")
endif()
