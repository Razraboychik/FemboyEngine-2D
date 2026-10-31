# Third-party dependencies for FemboyEngine 2.0.

include(FetchContent)

set(FBY_SDL_VERSION "release-3.4.18" CACHE STRING "SDL3 git tag to fetch")
set(FBY_VULKAN_HEADERS_VERSION "v1.4.366" CACHE STRING "Vulkan-Headers git tag to fetch")

# Local dependency overrides
foreach(dep IN ITEMS SDL3 VulkanHeaders glslang)
    string(TOLOWER "${dep}" dep_lower)
    if(dep STREQUAL "SDL3")
        set(local_dir "${CMAKE_CURRENT_SOURCE_DIR}/third_party/sdl3-src")
    elseif(dep STREQUAL "VulkanHeaders")
        set(local_dir "${CMAKE_CURRENT_SOURCE_DIR}/third_party/vulkan-headers-src")
    else()
        set(local_dir "${CMAKE_CURRENT_SOURCE_DIR}/third_party/glslang-src")
    endif()
    if(EXISTS "${local_dir}/CMakeLists.txt")
        set(FETCHCONTENT_SOURCE_DIR_${dep} "${local_dir}" CACHE PATH "${dep} local source" FORCE)
        message(STATUS "${dep}: local source ${local_dir}")
    endif()
endforeach()

# EnTT (header only)
set(ENTT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/entt-main/entt-main")
if (EXISTS "${ENTT_DIR}/src/entt/entt.hpp")
    add_library(fby_entt INTERFACE)
    target_include_directories(fby_entt SYSTEM INTERFACE "${ENTT_DIR}/src")
    add_library(fby::entt ALIAS fby_entt)
else()
    message(FATAL_ERROR "EnTT not found in third_party/entt-main/entt-main")
endif()

# SDL3 (platform layer: window, input, native file dialogs)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
set(SDL_DISABLE_INSTALL_CMAKEDIR ON CACHE BOOL "" FORCE)

FetchContent_Declare(SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG ${FBY_SDL_VERSION}
    GIT_SHALLOW TRUE
    GIT_PROGRESS FALSE)
FetchContent_MakeAvailable(SDL3)

# SDL3::SDL3-static is the static archive; SDL3::Headers carries the includes.
if (TARGET SDL3::SDL3-static)
    set(FBY_SDL_TARGET SDL3::SDL3-static)
elseif (TARGET SDL3::SDL3)
    set(FBY_SDL_TARGET SDL3::SDL3)
else()
    message(FATAL_ERROR "SDL3 did not define a linkable target")
endif()

# Vulkan headers (types only; the loader is resolved at runtime, so a machine
if (FBY_RHI_BACKEND STREQUAL "auto" OR FBY_RHI_BACKEND STREQUAL "vulkan")
    if (NOT APPLE)
        FetchContent_Declare(VulkanHeaders
            GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
            GIT_TAG ${FBY_VULKAN_HEADERS_VERSION}
            GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(VulkanHeaders)
    endif()
endif()

# glslang - build-time shader compiler
set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "" FORCE)
set(ENABLE_OPT OFF CACHE BOOL "" FORCE)
set(ENABLE_SPVREMAPPER OFF CACHE BOOL "" FORCE)
set(ENABLE_HLSL OFF CACHE BOOL "" FORCE)
set(GLSLANG_TESTS OFF CACHE BOOL "" FORCE)
set(GLSLANG_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(SKIP_GLSLANG_INSTALL ON CACHE BOOL "" FORCE)

FetchContent_Declare(glslang
    GIT_REPOSITORY https://github.com/KhronosGroup/glslang.git
    # Pinned release (commit e1b562a8). A floating branch made two builds of the
    # same tree use different shader compilers.
    GIT_TAG 16.6.0
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(glslang)

# FlatBuffers - localization packs and cooked asset metadata
set(FBY_FLATBUFFERS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/flatbuffers-master/flatbuffers-master")
if (EXISTS "${FBY_FLATBUFFERS_DIR}/CMakeLists.txt")
    set(FLATBUFFERS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(FLATBUFFERS_BUILD_FLATC ON CACHE BOOL "" FORCE)
    set(FLATBUFFERS_BUILD_FLATLIB ON CACHE BOOL "" FORCE)
    set(FLATBUFFERS_BUILD_GRPCTEST OFF CACHE BOOL "" FORCE)
    set(FLATBUFFERS_BUILD_SHAREDLIB OFF CACHE BOOL "" FORCE)
    set(FLATBUFFERS_INSTALL OFF CACHE BOOL "" FORCE)
    add_subdirectory(${FBY_FLATBUFFERS_DIR} third_party/flatbuffers EXCLUDE_FROM_ALL)
    if (TARGET flatbuffers)
        add_library(fby::flatbuffers ALIAS flatbuffers)
    endif()
else()
    message(FATAL_ERROR "FlatBuffers not found in third_party/flatbuffers-master")
endif()

# Box2D v3
set(BOX2D_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/box2d-main/box2d-main")
if (EXISTS "${BOX2D_DIR}/CMakeLists.txt")
    set(BOX2D_BUILD_UNIT_TESTS OFF CACHE BOOL "" FORCE)
    set(BOX2D_BUILD_TESTBED OFF CACHE BOOL "" FORCE)
    set(BOX2D_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
    set(BOX2D_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    add_subdirectory(${BOX2D_DIR} third_party/box2d EXCLUDE_FROM_ALL)
    add_library(fby::box2d ALIAS box2d)
else()
    message(FATAL_ERROR "Box2D not found in third_party/box2d-main/box2d-main")
endif()

# Dear ImGui + ImGuizmo + ImPlot (editor UI)
set(FBY_IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui-master/imgui-master")
set(FBY_IMGUIZMO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ImGuizmo-master/ImGuizmo-master")
set(FBY_IMPLOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ImPlot-master/implot-master")
set(FBY_IMNODES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imnodes")

if (EXISTS "${FBY_IMGUI_DIR}/imgui.cpp" AND EXISTS "${FBY_IMGUIZMO_DIR}/src/ImGuizmo.cpp" AND EXISTS
    "${FBY_IMPLOT_DIR}/implot.cpp" AND EXISTS "${FBY_IMNODES_DIR}/imnodes.cpp")
    add_library(fby_imgui STATIC
        "${FBY_IMGUI_DIR}/imgui.cpp"
        "${FBY_IMGUI_DIR}/imgui_draw.cpp"
        "${FBY_IMGUI_DIR}/imgui_tables.cpp"
        "${FBY_IMGUI_DIR}/imgui_widgets.cpp"
        "${FBY_IMGUI_DIR}/imgui_demo.cpp"
        "${FBY_IMGUI_DIR}/backends/imgui_impl_sdl3.cpp"
        "${FBY_IMGUI_DIR}/backends/imgui_impl_vulkan.cpp"
        "${FBY_IMGUIZMO_DIR}/src/ImGuizmo.cpp"
        "${FBY_IMPLOT_DIR}/implot.cpp"
        "${FBY_IMPLOT_DIR}/implot_items.cpp"
        "${FBY_IMNODES_DIR}/imnodes.cpp")
    add_library(fby::imgui ALIAS fby_imgui)

    target_include_directories(fby_imgui
        PUBLIC
            "${FBY_IMGUI_DIR}"
            "${FBY_IMGUI_DIR}/backends"
            "${FBY_IMGUIZMO_DIR}/src"
            "${FBY_IMPLOT_DIR}"
            "${FBY_IMNODES_DIR}")
    target_compile_definitions(fby_imgui PUBLIC IMGUI_IMPL_VULKAN_NO_PROTOTYPES)
    target_link_libraries(fby_imgui PUBLIC ${FBY_SDL_TARGET})
    if (TARGET Vulkan::Headers)
        target_link_libraries(fby_imgui PUBLIC Vulkan::Headers)
    elseif (DEFINED SDL3_SOURCE_DIR AND EXISTS "${SDL3_SOURCE_DIR}/src/video/khronos/vulkan/vulkan.h")
# SDL vendors a minimal Khronos header set; use it when the full
        target_include_directories(fby_imgui PUBLIC "${SDL3_SOURCE_DIR}/src/video/khronos")
    endif()
    set_target_properties(fby_imgui PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
        ARCHIVE_OUTPUT_DIRECTORY "${FBY_OUTPUT_DIR}/lib")
    set(FBY_HAS_IMGUI TRUE)
else()
    message(STATUS "Dear ImGui / ImGuizmo / ImPlot not found; the editor target will be skipped")
    set(FBY_HAS_IMGUI FALSE)
endif()

# toml++ (configuration)
set(TOML_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/tomlplusplus-master/tomlplusplus-master")
if (EXISTS "${TOML_DIR}/toml.hpp")
    add_library(fby_toml INTERFACE)
    target_include_directories(fby_toml SYSTEM INTERFACE "${TOML_DIR}")
    target_compile_definitions(fby_toml INTERFACE TOML_ENABLE_FORMATTERS=0)
    add_library(fby::toml ALIAS fby_toml)
else()
    message(FATAL_ERROR "toml++ not found in third_party/tomlplusplus-master")
endif()

# Monocypher (network cryptography: BLAKE2b, HMAC helpers, X25519, EdDSA).
set(MONOCYPHER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/monocypher")
if (EXISTS "${MONOCYPHER_DIR}/monocypher.c")
    add_library(fby_monocypher STATIC "${MONOCYPHER_DIR}/monocypher.c")
    target_include_directories(fby_monocypher SYSTEM PUBLIC "${MONOCYPHER_DIR}")
    target_compile_definitions(fby_monocypher PRIVATE _CRT_SECURE_NO_WARNINGS)
    add_library(fby::monocypher ALIAS fby_monocypher)
    message(STATUS "Monocypher: ${MONOCYPHER_DIR}")
else()
    message(FATAL_ERROR "Monocypher not found in third_party/monocypher (run SetupScripts/Setup)")
endif()


# RNNoise (neural speech denoise). Optional: FBY_WITH_RNNOISE=OFF keeps the
# spectral tier as the fallback.
set(RNNOISE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/rnnoise")
if (FBY_WITH_RNNOISE AND EXISTS "${RNNOISE_DIR}/src/denoise.c")
    add_library(fby_rnnoise STATIC
        "${RNNOISE_DIR}/src/denoise.c"
        "${RNNOISE_DIR}/src/rnn.c"
        "${RNNOISE_DIR}/src/pitch.c"
        "${RNNOISE_DIR}/src/kiss_fft.c"
        "${RNNOISE_DIR}/src/celt_lpc.c"
        "${RNNOISE_DIR}/src/rnn_data.c")
    target_include_directories(fby_rnnoise SYSTEM PUBLIC "${RNNOISE_DIR}/src" "${RNNOISE_DIR}/include")
    target_compile_definitions(fby_rnnoise PRIVATE _CRT_SECURE_NO_WARNINGS _USE_MATH_DEFINES)
    add_library(fby::rnnoise ALIAS fby_rnnoise)
    message(STATUS "RNNoise: ${RNNOISE_DIR}")
elseif (FBY_WITH_RNNOISE)
    message(STATUS "RNNoise: sources missing, the rnnoise tier falls back to spectral")
endif()

# miniaudio (audio device) - compiled once, in the audio module
set(MINIAUDIO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniaudio-master/miniaudio-master")
if (EXISTS "${MINIAUDIO_DIR}/miniaudio.h")
    add_library(fby_miniaudio INTERFACE)
    target_include_directories(fby_miniaudio SYSTEM INTERFACE "${MINIAUDIO_DIR}")
    add_library(fby::miniaudio ALIAS fby_miniaudio)
else()
    message(FATAL_ERROR "miniaudio not found in third_party/miniaudio-master")
endif()

# Opus (voice codec)
set(OPUS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/opus-master/opus-main")
if (EXISTS "${OPUS_DIR}/CMakeLists.txt")
    set(OPUS_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
    set(OPUS_BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(OPUS_BUILD_SHARED_LIBRARY OFF CACHE BOOL "" FORCE)
    set(OPUS_INSTALL_PKG_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
    set(OPUS_INSTALL_CMAKE_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
    add_subdirectory(${OPUS_DIR} third_party/opus EXCLUDE_FROM_ALL)
# Opus::opus is itself an alias in Opus' own CMake, so it cannot be aliased
    set(FBY_OPUS_TARGET Opus::opus)
endif()

# Sanitizer builds instrument the vendored C libraries too: lld refuses to mix
# instrumented and plain objects in one executable (/failifmismatch), and a
# plain dependency hides bugs from the sanitizer that reads its output.
if(FBY_WITH_ASAN OR FBY_WITH_UBSAN)
    foreach(FbyVendoredTarget box2d fby_rnnoise opus SDL3-static)
        if(TARGET ${FbyVendoredTarget})
            target_compile_options(${FbyVendoredTarget} PRIVATE ${FBY_ASAN_COMPILE_OPTIONS})
        endif()
    endforeach()
endif()
