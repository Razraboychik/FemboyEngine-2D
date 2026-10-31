# FemboyEngine2D 0.1.0-alpha.1 - public alpha build
cmake_minimum_required(VERSION 3.28)

project(FemboyEngine2D
    VERSION 0.1.0
    DESCRIPTION "Data-oriented 2D game engine"
    LANGUAGES C CXX)

# CMake accepts a numeric project version only. The SemVer pre-release suffix is
# kept separately for the engine string, logs, manifests and artifact names.
set(FBY_VERSION_SUFFIX "-alpha.1" CACHE STRING "Pre-release suffix appended to the engine version")
set(FBY_VERSION_STRING "${PROJECT_VERSION}${FBY_VERSION_SUFFIX}")

if(APPLE)
    enable_language(OBJCXX)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

set(FBY_OUTPUT_DIR "${CMAKE_BINARY_DIR}/bin")
set(FBY_SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/Source")
# Absolute engine repository root. Nested generated projects set CMAKE_SOURCE_DIR
set(FBY_ENGINE_SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}" CACHE INTERNAL "FemboyEngine2D repository root")

include(FbyCompiler)
include(FbyDependencies)

# Options
option(FBY_BUILD_SANDBOX "Build the sandbox demo application" ON)
option(FBY_BUILD_EDITOR  "Build the editor application"       ON)
option(FBY_BUILD_TESTS   "Build the engine test suite"        OFF)
option(FBY_WITH_ASAN     "Instrument engine and tests with AddressSanitizer" OFF)
option(FBY_WITH_UBSAN    "Instrument engine and tests with UndefinedBehaviorSanitizer" OFF)
option(FBY_BUILD_TOOLS   "Build the asset pipeline tools"     ON)
option(FBY_BUILD_PLATFORM_PROVIDERS "Build Steam/EGS provider plugins" ON)
option(FBY_BUILD_SERVER "Build the dedicated server application" ON)
option(FBY_BUILD_LOBBY  "Build the lobby service application"     ON)
option(FBY_WITH_RNNOISE "Build the RNNoise neural denoise tier"    ON)
# Bytecode is a development format: the editor runs it in the VM for hot reload,
# a cooked game compiles every program to a native AOT module and never links the
# interpreter. Editor, tests and tools require the VM, so they are skipped when
# it is disabled.
option(FBY_WITH_SCRIPT_VM "Build the FemboyScript VM (editor/dev, hot reload)" ON)

if (NOT FBY_WITH_SCRIPT_VM)
    message(STATUS "FBY_WITH_SCRIPT_VM=OFF: the interpreter is excluded; scripts run as nativized AOT modules")
    if (FBY_BUILD_EDITOR)
        message(STATUS "FBY_WITH_SCRIPT_VM=OFF: the editor is a development tool and is skipped")
        set(FBY_BUILD_EDITOR OFF)
    endif()
    if (FBY_BUILD_TESTS)
        message(STATUS "FBY_WITH_SCRIPT_VM=OFF: the test suite exercises the VM and is skipped")
        set(FBY_BUILD_TESTS OFF)
    endif()
endif()

option(FBY_ASAN_LEAKS "Report leaks at exit (Linux and macOS; Windows has no LeakSanitizer)" ON)

if(FBY_WITH_ASAN OR FBY_WITH_UBSAN)
    # Instrument the engine, the editor and the test binaries. Build tools and
    # third-party shader compilers keep their normal runtime flags: mixing
    # instrumented and plain objects in one executable trips lld's
    # /failifmismatch.
    set(FBY_ASAN_COMPILE_OPTIONS "")
    if(FBY_WITH_ASAN)
        list(APPEND FBY_ASAN_COMPILE_OPTIONS -fsanitize=address)
    endif()
    if(FBY_WITH_UBSAN)
        list(APPEND FBY_ASAN_COMPILE_OPTIONS -fsanitize=undefined -fno-sanitize-recover=all)
    endif()
    if(WIN32 AND FBY_WITH_UBSAN AND NOT CMAKE_MSVC_RUNTIME_LIBRARY STREQUAL "MultiThreaded")
        message(FATAL_ERROR
            "FBY_WITH_UBSAN on Windows needs -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded: "
            "the clang UBSan C++ runtime is built against the static CRT")
    endif()
    if(WIN32 AND FBY_WITH_ASAN AND FBY_WITH_UBSAN)
        message(FATAL_ERROR "ASan and UBSan cannot be combined on Windows (different CRT runtimes)")
    endif()
    # clang-cl rejects the GCC spelling; the MSVC frontend keeps frame pointers
    # through /Oy- instead.
    if(NOT CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        list(APPEND FBY_ASAN_COMPILE_OPTIONS -fno-omit-frame-pointer)
    else()
        list(APPEND FBY_ASAN_COMPILE_OPTIONS /Oy-)
    endif()
    if(WIN32)
        get_filename_component(FBY_ASAN_CLANG_ROOT "${CMAKE_CXX_COMPILER}" DIRECTORY)
        get_filename_component(FBY_ASAN_CLANG_ROOT "${FBY_ASAN_CLANG_ROOT}" DIRECTORY)
        file(GLOB FBY_ASAN_RUNTIME_LIBS
             "${FBY_ASAN_CLANG_ROOT}/lib/clang/*/lib/windows/clang_rt.asan_dynamic-x86_64.lib"
             "${FBY_ASAN_CLANG_ROOT}/lib/clang/*/lib/windows/clang_rt.asan_dynamic_runtime_thunk-x86_64.lib")
        file(GLOB FBY_ASAN_RUNTIME_DLL
             "${FBY_ASAN_CLANG_ROOT}/lib/clang/*/lib/windows/clang_rt.asan_dynamic-x86_64.dll")
    endif()

    # One rule for every instrumented target: compile options everywhere, link
    # options and the runtime where the target is an executable.
    function(fby_asan_apply target)
        # Optional targets are skipped when the current configuration does not
        # build them.
        if(NOT TARGET ${target})
            return()
        endif()
        target_compile_options(${target} PRIVATE ${FBY_ASAN_COMPILE_OPTIONS})
        get_target_property(FbyAsanTargetType ${target} TYPE)
        if(FbyAsanTargetType STREQUAL "STATIC_LIBRARY" OR FbyAsanTargetType STREQUAL "OBJECT_LIBRARY" OR
           FbyAsanTargetType STREQUAL "INTERFACE_LIBRARY")
            return()
        endif()
        if(FBY_WITH_ASAN)
            target_link_options(${target} PRIVATE -fsanitize=address)
        endif()
        if(FBY_WITH_UBSAN)
            target_link_options(${target} PRIVATE -fsanitize=undefined)
        endif()
        if(WIN32 AND FBY_WITH_ASAN)
            target_link_options(${target} PRIVATE /STACK:16777216)
            if(FBY_ASAN_RUNTIME_LIBS)
                target_link_libraries(${target} PRIVATE ${FBY_ASAN_RUNTIME_LIBS})
            endif()
            # The runtime DLL is copied once into the shared output directory;
            # a POST_BUILD hook cannot be added from a parent directory.

        endif()
    endfunction()

    if(WIN32 AND FBY_ASAN_RUNTIME_DLL)
        file(COPY "${FBY_ASAN_RUNTIME_DLL}" DESTINATION "${FBY_OUTPUT_DIR}")
        # Test executables live in bin/tests, away from the loader search path.
        file(MAKE_DIRECTORY "${FBY_OUTPUT_DIR}/tests")
        file(COPY "${FBY_ASAN_RUNTIME_DLL}" DESTINATION "${FBY_OUTPUT_DIR}/tests")
    endif()
    if(WIN32)
        message(STATUS "sanitizers: AddressSanitizer=${FBY_WITH_ASAN} UBSan=${FBY_WITH_UBSAN} for engine and tests")
    else()
        message(STATUS "sanitizers: ASan=${FBY_WITH_ASAN} UBSan=${FBY_WITH_UBSAN} (leaks: ${FBY_ASAN_LEAKS})")
    endif()
endif()

set(FBY_RHI_BACKEND "auto" CACHE STRING
    "Rendering backend: auto, vulkan, metal or null")
set_property(CACHE FBY_RHI_BACKEND PROPERTY STRINGS auto vulkan metal null)

# Release executables ship as Windows GUI programs: no console window flashes
# when the editor or a game starts. Logs still go to Saved/Logs; configure with
# -DFBY_CONSOLE_APPS=ON to keep stdout/stderr attached for development.
option(FBY_CONSOLE_APPS "Keep the console window for the editor and games" OFF)

function(fby_make_gui_app target)
    if(NOT WIN32 OR FBY_CONSOLE_APPS)
        return()
    endif()
    set_target_properties(${target} PROPERTIES WIN32_EXECUTABLE TRUE)
    # The applications keep the portable int main() entry point, so the Windows
    # subsystem needs the plain CRT startup instead of WinMain.
    if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        target_link_options(${target} PRIVATE "/ENTRY:mainCRTStartup")
    endif()
endfunction()

# Shader compiler first: the engine embeds its SPIR-V output at build time.
add_subdirectory(Source/Tools/ShaderCompiler)

# Build metadata: compiler, options and the dependency lock are compiled into the
# engine, so an artifact can be traced back to the exact build inputs.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/FbyDependencyLock.cmake)
string(SUBSTRING "${FBY_LOCK_BOX2D_TREE}" 0 12 FBY_LOCK_BOX2D_SHORT)
string(SUBSTRING "${FBY_LOCK_OPUS_TREE}" 0 12 FBY_LOCK_OPUS_SHORT)
set(FBY_BUILD_DEPENDENCIES
    "SDL3=${FBY_LOCK_SDL3_COMMIT} Vulkan-Headers=${FBY_LOCK_VULKAN_HEADERS_COMMIT} glslang=${FBY_LOCK_GLSLANG_COMMIT} Box2D=${FBY_LOCK_BOX2D_SHORT} Opus=${FBY_LOCK_OPUS_SHORT}")
set(FBY_BUILD_FLAGS_ESCAPED "${FBY_CXX_STANDARD_FLAG} ${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_${CMAKE_BUILD_TYPE}}")
if(FBY_WITH_ASAN)
    string(APPEND FBY_BUILD_FLAGS_ESCAPED " -fsanitize=address")
endif()
if(FBY_WITH_UBSAN)
    string(APPEND FBY_BUILD_FLAGS_ESCAPED " -fsanitize=undefined")
endif()
string(REPLACE "\\" "\\\\" FBY_BUILD_FLAGS_ESCAPED "${FBY_BUILD_FLAGS_ESCAPED}")
string(REPLACE "\"" "\\\"" FBY_BUILD_FLAGS_ESCAPED "${FBY_BUILD_FLAGS_ESCAPED}")
string(REPLACE "\n" " " FBY_BUILD_FLAGS_ESCAPED "${FBY_BUILD_FLAGS_ESCAPED}")
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/cmake/BuildInfo.hpp.in
               ${CMAKE_BINARY_DIR}/generated/BuildInfo.generated.hpp @ONLY)

# Engine library
add_subdirectory(Source/Engine)

# Editor library and applications
if (FBY_BUILD_EDITOR AND TARGET fby_imgui)
    add_subdirectory(Source/Editor)
    add_subdirectory(Source/Apps/Editor)
elseif (FBY_BUILD_EDITOR)
    message(STATUS "FBY_BUILD_EDITOR=ON but Dear ImGui/ImGuizmo/ImPlot were not found; editor targets skipped")
endif()

if (FBY_BUILD_SANDBOX)
    add_subdirectory(Source/Apps/Sandbox)
endif()

if (FBY_BUILD_SERVER)
    add_subdirectory(Source/Apps/Server)
endif()

if (FBY_BUILD_LOBBY)
    add_subdirectory(Source/Apps/Lobby)
endif()

if (FBY_BUILD_TOOLS)
    add_subdirectory(Source/Tools)
endif()

if (FBY_BUILD_PLATFORM_PROVIDERS)
    add_subdirectory(Source/Plugins/PlatformService)
endif()

if (FBY_BUILD_EDITOR AND TARGET fby_imgui)
    add_subdirectory(Source/Plugins)
endif()

if (FBY_BUILD_TESTS)
    enable_testing()
    add_subdirectory(Source/Tests)
endif()

message(STATUS "")
message(STATUS "FemboyEngine2D ${FBY_VERSION_STRING} (Alpha)")
message(STATUS "  platform : ${CMAKE_SYSTEM_NAME}")
message(STATUS "  compiler : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  language : C++26 (${FBY_CXX_STANDARD_FLAG})")
message(STATUS "  rhi      : ${FBY_RHI_BACKEND}")
message(STATUS "  source   : ${FBY_SOURCE_ROOT}")
message(STATUS "  output   : ${FBY_OUTPUT_DIR}")
message(STATUS "")
